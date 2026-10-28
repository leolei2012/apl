#include "apl_source.h"
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>

AplSource::AplSource(QObject *parent) : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(650);
    auto schedule = [this]() { m_debounce.start(); };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, schedule);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, schedule);
    connect(&m_debounce, &QTimer::timeout, this, &AplSource::scan);
    // Recovers deleted/recreated directories and notifications lost by the OS.
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, [this]() { if (!m_debounce.isActive()) scan(); });
}

bool AplSource::setRoot(const QString &path, QString *error)
{
    const QString root = path.isEmpty() ? QString() : QFileInfo(path).absoluteFilePath();
    const auto sources = SdkGenerator::readSources(root, error);
    if (sources.isEmpty()) return false;
    m_root = root;
    m_fingerprint = SdkGenerator::fingerprint(sources);
    m_debounce.stop();
    m_poll.stop();
    if (!m_watcher.files().isEmpty()) m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    if (!m_root.isEmpty()) { watchDirectories(); m_poll.start(); }
    emit changed();
    return true;
}

void AplSource::watchDirectories()
{
    QStringList paths = {QFileInfo(m_root).absolutePath(), m_root};
    for (const auto &name : {"src", "include", "config"})
    {
        const QString folder = QDir(m_root).filePath(name);
        if (!QFileInfo::exists(folder)) continue;
        paths.append(folder);
        QDirIterator it(folder, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) paths.append(it.next());
    }
    QString ignored;
    const auto sources = SdkGenerator::readSources(m_root, &ignored);
    for (auto it = sources.begin(); it != sources.end(); ++it) paths.append(QDir(m_root).filePath(it.key()));
    for (const auto &path : paths)
        if (QFileInfo::exists(path) && !m_watcher.files().contains(path) && !m_watcher.directories().contains(path))
            m_watcher.addPath(path);
}

void AplSource::scan()
{
    if (m_root.isEmpty()) return;
    QString error;
    const auto sources = SdkGenerator::readSources(m_root, &error);
    const QByteArray hash = sources.isEmpty() ? error.toUtf8() : SdkGenerator::fingerprint(sources);
    watchDirectories();
    if (hash == m_fingerprint) return;
    m_fingerprint = hash;
    emit changed();
}

QString AplSource::discover()
{
    for (const auto &start : {QDir::currentPath(), QCoreApplication::applicationDirPath(), QString::fromUtf8(APL_SOURCE_ROOT)})
    {
        QDir dir(start);
        do {
            if (QFileInfo::exists(dir.filePath("src/apl.c")) && QFileInfo::exists(dir.filePath("include/apl.h"))
                && QFileInfo::exists(dir.filePath("config/apl_core_config.h"))) return dir.absolutePath();
        } while (dir.cdUp());
    }
    return {};
}
