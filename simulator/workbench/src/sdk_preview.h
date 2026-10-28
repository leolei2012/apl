#pragma once
#include "sdk_generator.h"
#include <QTemporaryDir>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <memory>

class QWidget;

struct SdkPreview
{
    std::shared_ptr<QTemporaryDir> directory;
    QString library;
    static SdkPreview build(const SdkGenerator::Files &files, const QString &compiler,
                            QWidget *parent, QString *error);
    static QString defaultCompiler();
};

/** One background build, using an immutable source/configuration snapshot. */
class SdkPreviewJob : public QObject
{
    Q_OBJECT
public:
    explicit SdkPreviewJob(QObject *parent = nullptr);
    ~SdkPreviewJob() override;
    void start(const SdkGenerator::Files &files, const QString &compiler);
    void cancel();
    bool running() const { return m_running; }
    SdkPreview result() const { return m_result; }
    QString error() const { return m_error; }
signals:
    void finished();
private:
    void complete(const QString &error = {});
    QProcess m_process;
    QTimer m_timeout;
    SdkPreview m_result;
    QString m_error;
    bool m_running = false;
};
