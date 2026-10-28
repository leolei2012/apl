#include "sdk_preview.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>

QString SdkPreview::defaultCompiler()
{
    const QString configured = QString::fromUtf8(APL_PREVIEW_COMPILER);
    if (QFile::exists(configured)) return configured;
    const QString gcc = QStandardPaths::findExecutable("gcc");
    return gcc.isEmpty() ? QStandardPaths::findExecutable("clang") : gcc;
}

// Public declarations form the boundary between the running UI and the module.
// Size checks alone cannot detect reordered callbacks or changed enum values.
static QByteArray interfaceTokens(QByteArray source)
{
    QString text = QString::fromUtf8(source);
    text.remove(QRegularExpression("/\\*.*?\\*/|//[^\\r\\n]*", QRegularExpression::DotMatchesEverythingOption));
    text.replace(QRegularExpression("\\s+"), "");
    return text.toUtf8();
}

SdkPreviewJob::SdkPreviewJob(QObject *parent) : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(120000);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        m_error = "APL 构建超时。";
        m_process.kill();
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete("无法启动 C 编译器：" + m_process.errorString());
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_error.isEmpty()) complete(m_error);
        else if (status != QProcess::NormalExit || code != 0)
            complete("APL 构建失败：\n" + QString::fromUtf8(m_process.readAll()));
        else complete();
    });
}

SdkPreviewJob::~SdkPreviewJob()
{
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(3000); }
}

void SdkPreviewJob::start(const SdkGenerator::Files &files, const QString &compiler)
{
    if (m_running) return;
    m_running = true;
    m_error.clear();
    m_result = {};
    // Defer failures too: callers can connect once and treat all outcomes alike.
    QTimer::singleShot(0, this, [this, files, compiler]() {
        if (!m_running) return;
        if (compiler.isEmpty() || !QFile::exists(compiler))
        { complete("找不到本机 C 编译器，请在产品菜单选择 GCC 或 Clang。"); return; }
        for (const auto &path : {"include/apl.h", "include/apl_core.h", "include/apl_hmi.h", "include/apl_type.h"})
        {
            QFile host(":/sdk/" + QString::fromLatin1(path));
            if (!host.open(QIODevice::ReadOnly) || !files.contains(path)
                || interfaceTokens(host.readAll()) != interfaceTokens(files.value(path)))
            {
                complete(QString("公共接口发生变化（%1），需要重新构建 simulator 以更新接口；普通 C 实现修改可直接重载。").arg(path));
                return;
            }
        }
        m_result.directory = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/apl-preview-XXXXXX");
        QString error;
        if (!m_result.directory->isValid() || !SdkGenerator::writeDirectory(files, m_result.directory->path(), &error))
        { complete("无法准备 APL 构建目录：" + error); return; }
        const QString root = m_result.directory->path();
        QFile abi(root + "/preview_abi.c");
        const QByteArray code = "#include \"apl.h\"\nunsigned long long apl_preview_abi(void) { return "
            "((unsigned long long)sizeof(struct apl) << 32) | ((unsigned long long)sizeof(struct apl_core_ops) << 16)"
            " | sizeof(struct apl_hmi_ops); }\n";
        if (!abi.open(QIODevice::WriteOnly) || abi.write(code) != code.size())
        { complete(abi.errorString()); return; }
        abi.close();
#ifdef Q_OS_WIN
        m_result.library = root + "/apl_preview.dll";
#else
        m_result.library = root + "/libapl_preview.so";
#endif
        QStringList args = {"-std=c11", "-shared", "-Wall", "-Wextra", "-Werror", "-finput-charset=UTF-8",
            "-I" + root + "/include", "-I" + root + "/config"};
        for (auto it = files.begin(); it != files.end(); ++it)
            if (it.key().startsWith("src/") && it.key().endsWith(".c")) args.append(root + '/' + it.key());
        args << root + "/preview_abi.c" << "-o" << m_result.library;
#ifndef Q_OS_WIN
        args.prepend("-fPIC");
#endif
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PATH", QFileInfo(compiler).absolutePath() + QDir::listSeparator() + environment.value("PATH"));
        m_process.setProcessEnvironment(environment);
        m_process.start(compiler, args);
        m_timeout.start();
    });
}

void SdkPreviewJob::cancel()
{
    if (!m_running) return;
    m_error = "APL 构建已取消。";
    if (m_process.state() == QProcess::NotRunning) complete(m_error);
    else m_process.kill();
}

void SdkPreviewJob::complete(const QString &error)
{
    if (!m_running) return;
    m_timeout.stop();
    m_running = false;
    m_error = error;
    if (!error.isEmpty()) m_result = {};
    emit finished();
}

SdkPreview SdkPreview::build(const SdkGenerator::Files &files, const QString &compiler,
                            QWidget *parent, QString *error)
{
    SdkPreviewJob job;
    QProgressDialog progress("正在构建 APL 模块…", "取消", 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QObject::connect(&job, &SdkPreviewJob::finished, &progress, &QProgressDialog::accept);
    QObject::connect(&progress, &QProgressDialog::canceled, &job, &SdkPreviewJob::cancel);
    job.start(files, compiler);
    const int accepted = progress.exec();
    if (accepted != QDialog::Accepted || job.running())
    {
        job.cancel();
        if (error) *error = "APL 构建已取消。";
        return {};
    }
    if (error) *error = job.error();
    return job.result();
}
