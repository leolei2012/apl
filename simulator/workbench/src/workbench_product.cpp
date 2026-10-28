#include "workbench_window.h"
#include "apl_platform.h"
#include "product_dialog.h"
#include "sdk_generator.h"
#include "sdk_preview.h"
#include "apl_source.h"
#include <QDir>
#include <QAction>
#include <QAbstractButton>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStyle>
#include <QStackedWidget>

void WorkbenchWindow::refreshProduct()
{
    if (!m_productLabel) return;
    QString error;
    const auto files = SdkGenerator::generate(m_product, &error, sourceRoot());
    const QByteArray fingerprint = files.isEmpty() ? QByteArray() : SdkGenerator::fingerprint(files);
    const bool applied = !files.isEmpty() && fingerprint == m_previewFingerprint;
    const bool valid = m_product.validate().isEmpty();
    m_productLabel->setText(m_product.name.isEmpty() ? "未命名产品" : m_product.name);
    m_productLocation->setText(m_productPath.isEmpty() ? "尚未保存" : QFileInfo(m_productPath).fileName());
    m_productLocation->setToolTip(m_productPath);
    m_previewBadge->setText(buildingApl() ? "正在构建 APL" : applied ? "已应用源码与配置" : m_previewFingerprint.isEmpty() ? "内置预览" : "源码或配置待应用");
    const QString tone = applied ? "ready" : "pending";
    if (m_previewBadge->property("tone").toString() != tone)
    {
        m_previewBadge->setProperty("tone", tone);
        m_previewBadge->style()->unpolish(m_previewBadge);
        m_previewBadge->style()->polish(m_previewBadge);
    }
    const bool purify = m_product.values.value("APL_COMPILE_PURIFY"), humidify = m_product.values.value("APL_COMPILE_HUMIDIFY");
    int keys = 0;
    for (const auto &field : ProductConfig::fields()) if (field.hmi && m_product.values.value(field.macro)) ++keys;
    const QString model = purify && humidify ? "净化 + 加湿" : purify ? "仅净化" : "仅加湿";
    m_overviewFacts->setText(QString("%1  ·  %2 个按键  ·  %3\n\n%4").arg(model).arg(keys)
        .arg(m_product.frontend == "round" ? "圆形面板" : "按键台")
        .arg(applied ? "当前产品配置已加载，可以在实时仿真中验证并准备交付。" : "配置准备好后，应用到仿真中查看效果。正在运行的预览不会随编辑改变。"));
    m_deliveryStatus->setText(applied ? "当前源码与配置已加载到预览" : "导出前将先应用当前源码与配置");
    if (!files.isEmpty())
    {
        const auto manifest = QJsonDocument::fromJson(files.value("manifest.json")).object();
        m_deliveryDetails->setText(QString("APL %1  ·  %2 个文件  ·  配置指纹 %3")
            .arg(manifest["sdkVersion"].toString()).arg(files.size()).arg(QString::fromLatin1(fingerprint.left(16))));
    }
    else m_deliveryDetails->setText("请先完成有效的产品配置。");
    m_saveButton->setText(m_productDirty ? "保存更改" : "保存");
    m_saveButton->setEnabled(valid && (m_productDirty || m_productPath.isEmpty()));
    if (m_pages) m_primary->setEnabled((m_pages->currentIndex() == 0 || m_pages->currentIndex() == 2 || valid) && !buildingApl());
    if (!m_lastExportPath.isEmpty())
    {
        m_exportLocation->setText((fingerprint == m_lastExportFingerprint ? QString("已导出：")
            : QString("上次导出（源码或配置有更新）：")) + m_lastExportPath);
        m_revealExport->show();
    }
    refreshTitle();
}

bool WorkbenchWindow::maybeSaveProduct()
{
    if (!m_productDirty) return true;
    QMessageBox box(QMessageBox::Question, "保存产品更改？", "当前产品有尚未保存的配置。", QMessageBox::NoButton, this);
    auto *save = box.addButton("保存", QMessageBox::AcceptRole);
    auto *discard = box.addButton("不保存", QMessageBox::DestructiveRole);
    auto *cancel = box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(save);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == save) return saveProduct();
    return box.clickedButton() == discard;
}

void WorkbenchWindow::newProduct()
{
    if (!maybeSaveProduct()) return;
    m_product = ProductConfig::defaults();
    m_savedProduct = {};
    m_productPath.clear();
    m_productDirty = true;
    m_lastExportPath.clear();
    m_exportLocation->clear();
    m_revealExport->hide();
    m_editor->setConfig(m_product);
    refreshProduct();
    showPage(1);
    showNotice("新产品已创建。选择产品类型，再应用到预览。");
}

void WorkbenchWindow::openProduct()
{
    if (!maybeSaveProduct()) return;
    const QString path = QFileDialog::getOpenFileName(this, "打开 APL 产品", {}, "APL 产品 (*.apl.json);;JSON (*.json)");
    if (path.isEmpty()) return;
    ProductConfig next;
    QString error;
    if (!ProductConfig::load(path, &next, &error)) { showNotice("产品配置未能打开。", error, true); return; }
    m_product = next;
    m_savedProduct = next.toJson();
    m_productPath = path;
    m_productDirty = false;
    m_lastExportPath.clear();
    m_exportLocation->clear();
    m_revealExport->hide();
    m_editor->setConfig(m_product);
    refreshProduct();
    showPage(1);
    showNotice("产品已打开。可以继续编辑，或应用到预览。");
}

bool WorkbenchWindow::saveProduct()
{
    const QString why = m_product.validate();
    if (!why.isEmpty()) { showPage(1); showNotice(why, {}, true); return false; }
    QString path = m_productPath;
    if (path.isEmpty())
    {
        QFileDialog dialog(this, "保存产品配置");
        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setNameFilter("APL 产品 (*.apl.json)");
        dialog.setDefaultSuffix("apl.json");
        dialog.selectFile("product.apl.json");
        if (dialog.exec() != QDialog::Accepted) return false;
        path = dialog.selectedFiles().value(0);
    }
    if (path.isEmpty()) return false;
    QString error;
    if (!m_product.save(path, &error)) { showNotice("产品配置未能保存。", error, true); return false; }
    m_productPath = path;
    m_productDirty = false;
    m_savedProduct = m_product.toJson();
    refreshProduct();
    showNotice("产品配置已保存。");
    return true;
}

void WorkbenchWindow::saveProductAs()
{
    const QString previous = m_productPath;
    m_productPath.clear();
    if (!saveProduct()) m_productPath = previous;
    refreshProduct();
}

void WorkbenchWindow::configureProduct() { showPage(1); }

void WorkbenchWindow::initializeDevelopment()
{
    m_source = new AplSource(this);
    m_build = new SdkPreviewJob(this);
    connect(m_build, &SdkPreviewJob::finished, this, &WorkbenchWindow::finishPreview);
    connect(m_source, &AplSource::changed, this, [this]() {
        refreshSourceLabel();
        if (buildingApl()) return;
        startPreview(m_reloadConfig);
    });
}

QString WorkbenchWindow::sourceRoot() const { return m_source ? m_source->root() : QString(); }
bool WorkbenchWindow::buildingApl() const { return m_build && m_build->running(); }

void WorkbenchWindow::refreshSourceLabel()
{
    m_sourceLabel->setText(sourceRoot().isEmpty() ? "内置 APL" : "APL 源码 · 自动重载");
    m_sourceLabel->setToolTip(sourceRoot().isEmpty() ? "未绑定源码目录，使用随程序附带的 APL。" : sourceRoot());
}

bool WorkbenchWindow::setSourceRoot(const QString &path, QString *error)
{
    return m_source->setRoot(path, error);
}

void WorkbenchWindow::startDevelopment()
{
    if (m_developmentStarted) return;
    m_developmentStarted = true;
    const auto settings = QSettings("APL", "Workbench");
    const QString root = settings.value("product/sourceRoot", AplSource::discover()).toString();
    QString error;
    if (!root.isEmpty() && !setSourceRoot(root, &error))
        showNotice("无法读取已绑定的 APL 工程，请重新选择源码目录。当前为内置预览。", error, true);
    refreshSourceLabel();
}

void WorkbenchWindow::chooseSourceRoot()
{
    const QString path = QFileDialog::getExistingDirectory(this, "选择 APL 工程根目录（包含 src、include、config）",
        sourceRoot().isEmpty() ? AplSource::discover() : sourceRoot());
    if (path.isEmpty()) return;
    QString error;
    if (!setSourceRoot(path, &error)) { showNotice("这个目录不是完整的 APL 工程。", error, true); return; }
    QSettings("APL", "Workbench").setValue("product/sourceRoot", sourceRoot());
}

void WorkbenchWindow::rebuildApl()
{
    if (buildingApl()) { showNotice("APL 正在构建，源码保存后会自动加载最新版本。"); return; }
    startPreview(m_reloadConfig);
}

void WorkbenchWindow::applyProduct() { startPreview(m_product, true); }

void WorkbenchWindow::startPreview(const ProductConfig &config, bool navigate, bool exportAfter)
{
    if (buildingApl()) { showNotice("APL 正在构建，请稍后再应用配置。"); return; }
    if (config.validate().isEmpty()) m_reloadConfig = config;
    QString error;
    const auto files = SdkGenerator::generate(config, &error, sourceRoot());
    if (files.isEmpty())
    {
        showNotice("无法准备 APL 源码与配置，原有预览保持不变。", error, true);
        refreshProduct();
        emit previewFinished(false);
        return;
    }
    m_buildConfig = config;
    m_buildFiles = files;
    m_buildRoot = sourceRoot();
    m_buildNavigate = navigate;
    m_buildExport = exportAfter;
    const QString compiler = QSettings("APL", "Workbench").value("product/compiler", SdkPreview::defaultCompiler()).toString();
    m_build->start(files, compiler);
    refreshProduct();
    showNotice("正在构建 APL 模块，当前仿真可以继续操作。");
}

void WorkbenchWindow::finishPreview()
{
    QString error;
    // Editors can save again while the compiler runs. Never activate an obsolete snapshot.
    const auto latest = SdkGenerator::generate(m_buildConfig, &error, sourceRoot());
    const bool changed = sourceRoot() != m_buildRoot || (!latest.isEmpty()
        && SdkGenerator::fingerprint(latest) != SdkGenerator::fingerprint(m_buildFiles));
    if (changed)
    {
        const bool navigate = m_buildNavigate, exportAfter = m_buildExport;
        showNotice("源码在构建期间有更新，正在准备最新版本。");
        startPreview(m_reloadConfig, navigate, exportAfter);
        return;
    }
    const auto preview = m_build->result();
    bool applied = false;
    if (!latest.isEmpty())
    {
        if (preview.library.isEmpty()) error = m_build->error();
        else applied = m_platform->loadRuntime(preview.library, &error, preview.directory);
    }
    const bool exportAfter = m_buildExport;
    m_buildExport = false;
    if (!applied)
    {
        showNotice("APL 未能更新，原有预览保持不变。修正并保存后会自动重试。", error, true);
        refreshProduct();
        emit previewFinished(false);
        return;
    }
    m_verifiedFiles = m_buildFiles;
    m_previewFingerprint = SdkGenerator::fingerprint(m_verifiedFiles);
    m_platform->selectFrontend(m_buildConfig.frontend == "round" ? 0 : 1);
    refreshProduct();
    if (m_buildNavigate) showPage(2);
    showNotice("APL 已更新，设备已重新上电。无需重启 simulator。");
    emit previewFinished(true);
    if (exportAfter)
    {
        const auto current = SdkGenerator::generate(m_product, &error, sourceRoot());
        if (!current.isEmpty() && SdkGenerator::fingerprint(current) == m_previewFingerprint) saveVerifiedSdk();
        else showNotice("构建期间产品配置有更新，请确认后重新导出 SDK。");
    }
}

void WorkbenchWindow::exportSdk()
{
    if (buildingApl()) { showNotice("请等待 APL 构建完成后导出。"); return; }
    QString error;
    const auto files = SdkGenerator::generate(m_product, &error, sourceRoot());
    if (files.isEmpty()) { showNotice("请先检查产品配置和 APL 源码。", error, true); return; }
    if (SdkGenerator::fingerprint(files) != m_previewFingerprint)
    { startPreview(m_product, false, true); return; }
    saveVerifiedSdk();
}

void WorkbenchWindow::saveVerifiedSdk()
{
    // Freeze the verified bytes before opening the dialog: a new save may arrive while it is open.
    const auto files = m_verifiedFiles;
    if (files.isEmpty()) return;
    showPage(3);
    QFileDialog dialog(this, "导出 APL SDK");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter("ZIP 软件包 (*.zip)");
    dialog.setDefaultSuffix("zip");
    dialog.selectFile("apl-sdk.zip");
    if (dialog.exec() != QDialog::Accepted) return;
    const QString path = dialog.selectedFiles().value(0);
    if (path.isEmpty()) return;
    QString error;
    if (!SdkGenerator::writeZip(files, path, &error)) { showNotice("SDK 导出失败。", error, true); return; }
    m_lastExportPath = path;
    m_lastExportFingerprint = SdkGenerator::fingerprint(files);
    refreshProduct();
    showNotice("SDK 已导出，包含仿真已加载验证的源码、配置和移植说明。");
}

void WorkbenchWindow::chooseCompiler()
{
    const QString path = QFileDialog::getOpenFileName(this, "选择本机 GCC / Clang C 编译器",
        QSettings("APL", "Workbench").value("product/compiler", SdkPreview::defaultCompiler()).toString(),
#ifdef Q_OS_WIN
        "C 编译器 (*.exe)"
#else
        "C 编译器 (*)"
#endif
    );
    if (!path.isEmpty())
    {
        QSettings("APL", "Workbench").setValue("product/compiler", path);
        showNotice("本机编译器已更新。");
        rebuildApl();
    }
}

void WorkbenchWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSaveProduct())
    {
        m_build->disconnect(this);
        m_build->cancel();
        event->accept();
    }
    else event->ignore();
}
