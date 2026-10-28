#include "sdk_generator.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QSaveFile>

SdkGenerator::Files SdkGenerator::readSources(const QString &root, QString *error)
{
    Files files;
    const QStringList paths = {"include/apl.h", "include/apl_core.h", "include/apl_hmi.h",
        "include/apl_type.h", "src/apl.c", "src/apl_core.c", "src/apl_hmi.c",
        "config/apl_core_config.h", "config/apl_hmi_config.h"};
    QStringList inputs = paths;
    if (!root.isEmpty())
    {
        inputs.clear();
        for (const auto &folder : {"src", "include", "config"})
        {
            QDirIterator it(QDir(root).filePath(folder), {"*.c", "*.h", "*.inc"},
                            QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) inputs.append(QDir(root).relativeFilePath(it.next()));
        }
    }
    for (const auto &path : inputs)
    {
        QFile file(root.isEmpty() ? ":/sdk/" + path : QDir(root).filePath(path));
        if (!file.open(QIODevice::ReadOnly)) { if (error) *error = "无法读取 APL 源码: " + file.fileName() + '\n' + file.errorString(); return {}; }
        files[path] = file.readAll();
        if (file.error() != QFileDevice::NoError) { if (error) *error = file.errorString(); return {}; }
    }
    for (const auto &path : paths)
        if (!files.contains(path)) { if (error) *error = "APL 工程缺少文件: " + path; return {}; }
    return files;
}

SdkGenerator::Files SdkGenerator::generate(const ProductConfig &config, QString *error, const QString &sourceRoot)
{
    const QString why = config.validate();
    if (!why.isEmpty()) { if (error) *error = why; return {}; }
    Files files = readSources(sourceRoot, error);
    if (files.isEmpty()) return {};
    const QStringList templates = {"port/apl_port.h", "port/apl_port.c", "examples/app.c",
                                    "CMakeLists.txt", "README.md"};
    for (const auto &path : templates)
    {
        QFile file(":/sdk-templates/" + path);
        if (!file.open(QIODevice::ReadOnly)) { if (error) *error = "移植模板缺失: " + path; return {}; }
        files[path] = file.readAll();
    }
    // Only product fields are overridden; other live header edits remain in the snapshot.
    for (const bool hmi : {false, true})
    {
        const QString path = hmi ? "config/apl_hmi_config.h" : "config/apl_core_config.h";
        QString header = QString::fromUtf8(files[path]);
        for (const auto &field : ProductConfig::fields())
        {
            if (field.hmi != hmi) continue;
            const QRegularExpression rx("(?m)^([ \\t]*#\\s*define\\s+" + field.macro + "[ \\t]+)[^\\r\\n]*");
            auto matches = rx.globalMatch(header);
            if (!matches.hasNext()) { if (error) *error = path + " 缺少配置项: " + field.macro; return {}; }
            const auto match = matches.next();
            if (matches.hasNext()) { if (error) *error = path + " 配置项重复: " + field.macro; return {}; }
            header.replace(match.capturedStart(), match.capturedLength(),
                "#define " + field.macro + " " + QString::number(config.values.value(field.macro)) + "u");
        }
        files[path] = header.toUtf8();
    }
    files["product.apl.json"] = QJsonDocument(config.toJson()).toJson();
    QJsonObject hashes;
    for (auto it = files.begin(); it != files.end(); ++it)
        hashes[it.key()] = QString::fromLatin1(QCryptographicHash::hash(it.value(), QCryptographicHash::Sha256).toHex());
    const QRegularExpression versionRx("#define\\s+APL_VERSION_STRING\\s+\"([^\"]+)\"");
    const auto version = versionRx.match(QString::fromUtf8(files["include/apl.h"])).captured(1);
    const QJsonObject manifest{{"schemaVersion", 1}, {"sdkVersion", version}, {"product", config.name},
        {"fingerprint", QString::fromLatin1(fingerprint(files))}, {"files", hashes}};
    files["manifest.json"] = QJsonDocument(manifest).toJson();
    return files;
}

QByteArray SdkGenerator::fingerprint(const Files &files)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (auto it = files.begin(); it != files.end(); ++it)
    {
        if (it.key() == "manifest.json") continue;
        hash.addData(it.key().toUtf8());
        hash.addData(QByteArray(1, '\0'));
        hash.addData(it.value());
        hash.addData(QByteArray(1, '\0'));
    }
    return hash.result().toHex();
}

bool SdkGenerator::writeDirectory(const Files &files, const QString &path, QString *error)
{
    if (files.isEmpty()) { if (error) *error = "SDK 内容为空。"; return false; }
    for (auto it = files.begin(); it != files.end(); ++it)
    {
        const QString target = QDir(path).filePath(it.key());
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) { if (error) *error = "无法创建 SDK 目录。"; return false; }
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size() || !file.commit())
        { if (error) *error = file.errorString(); return false; }
    }
    return true;
}

static quint32 crc32(const QByteArray &data)
{
    quint32 crc = 0xffffffffu;
    for (unsigned char ch : data)
    {
        crc ^= ch;
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

bool SdkGenerator::writeZip(const Files &files, const QString &path, QString *error)
{
    if (files.isEmpty()) { if (error) *error = "SDK 内容为空。"; return false; }
    // ZIP store 格式：无需外部压缩程序，内容固定为本生成器的相对路径。
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) { if (error) *error = output.errorString(); return false; }
    QDataStream zip(&output);
    zip.setByteOrder(QDataStream::LittleEndian);
    struct Entry { QByteArray name; quint32 crc, size, offset; };
    QVector<Entry> entries;
    for (auto it = files.begin(); it != files.end(); ++it)
    {
        const Entry entry{("apl-sdk/" + it.key()).toUtf8(), crc32(it.value()),
                          quint32(it.value().size()), quint32(output.pos())};
        entries.append(entry);
        zip << quint32(0x04034b50) << quint16(20) << quint16(0x0800) << quint16(0)
            << quint16(0) << quint16(33) << entry.crc << entry.size << entry.size
            << quint16(entry.name.size()) << quint16(0);
        zip.writeRawData(entry.name.constData(), entry.name.size());
        zip.writeRawData(it.value().constData(), it.value().size());
    }
    const quint32 central = quint32(output.pos());
    for (const auto &entry : entries)
    {
        zip << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x0800)
            << quint16(0) << quint16(0) << quint16(33) << entry.crc << entry.size << entry.size
            << quint16(entry.name.size()) << quint16(0) << quint16(0) << quint16(0)
            << quint16(0) << quint32(0) << entry.offset;
        zip.writeRawData(entry.name.constData(), entry.name.size());
    }
    const quint32 centralSize = quint32(output.pos()) - central;
    zip << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size())
        << quint16(entries.size()) << centralSize << central << quint16(0);
    if (zip.status() != QDataStream::Ok || !output.commit())
    { if (error) *error = output.errorString(); return false; }
    return true;
}
