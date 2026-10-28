#pragma once
#include "sdk_generator.h"
#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>

/** Watches live source files, including editor atomic-save replacements. */
class AplSource : public QObject
{
    Q_OBJECT
public:
    explicit AplSource(QObject *parent = nullptr);
    QString root() const { return m_root; }
    bool setRoot(const QString &path, QString *error);
    static QString discover();
signals:
    void changed();
private:
    void scan();
    void watchDirectories();
    QString m_root;
    QByteArray m_fingerprint;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QTimer m_poll;
};
