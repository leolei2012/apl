#pragma once
#include <QMainWindow>
#include "product_config.h"
#include "sdk_generator.h"

class AplPlatform;
class QAction;
class QLabel;
class QMenu;
class QPlainTextEdit;
class QQuickWidget;
class QSlider;
class QSpinBox;
class QPushButton;
class QToolButton;
class QButtonGroup;
class QStackedWidget;
class QLineEdit;
class ProductEditor;
class AplSource;
class SdkPreviewJob;

/** 产品配置、APL 预览和 SDK 交付共用一个工作区。 */
class WorkbenchWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit WorkbenchWindow(AplPlatform *platform, QWidget *parent = nullptr);
    void showPage(int page);
    void startDevelopment();
    bool setSourceRoot(const QString &path, QString *error);
    QString sourceRoot() const;
    bool buildingApl() const;
    SdkGenerator::Files verifiedFiles() const { return m_verifiedFiles; }
signals:
    void previewFinished(bool success);
private:
    void newProduct();
    void openProduct();
    bool saveProduct();
    void saveProductAs();
    void configureProduct();
    void applyProduct();
    void initializeDevelopment();
    void chooseSourceRoot();
    void rebuildApl();
    void startPreview(const ProductConfig &config, bool navigate = false, bool exportAfter = false);
    void finishPreview();
    void saveVerifiedSdk();
    void refreshSourceLabel();
    void exportSdk();
    void chooseCompiler();
    bool maybeSaveProduct();
    void refreshProduct();
    void closeEvent(QCloseEvent *event) override;
    void buildMenu();
    void buildChrome();
    QWidget *buildOverview();
    QWidget *buildConfiguration();
    QWidget *buildPreview();
    QWidget *buildDelivery();
    void bind();
    void primaryAction();
    void rebuildViewMenu();
    void syncViewChecks();
    void refreshTitle();
    void refreshFrame();
    void refreshSensors();
    void refreshLog();
    void syncFrontendRow();
    void loadStage();
    void onStageStatus();
    void showAbout();
    void saveEventLog();
    void showNotice(const QString &message, const QString &details = {}, bool error = false);
    void makeSensor(class QVBoxLayout *layout, const QString &title, int low, int high,
                    QSlider **slider, QSpinBox **reading, const QString &unit = {});

    ProductConfig m_product = ProductConfig::defaults();
    QJsonObject m_savedProduct;
    QString m_productPath;
    QByteArray m_previewFingerprint;
    QString m_lastExportPath;
    QByteArray m_lastExportFingerprint;
    bool m_productDirty = false;
    bool m_syncingEditor = false;
    AplSource *m_source = nullptr;
    SdkPreviewJob *m_build = nullptr;
    QLabel *m_sourceLabel = nullptr;
    ProductConfig m_reloadConfig = m_product;
    ProductConfig m_buildConfig;
    SdkGenerator::Files m_buildFiles;
    SdkGenerator::Files m_verifiedFiles;
    QString m_buildRoot;
    bool m_buildNavigate = false;
    bool m_buildExport = false;
    bool m_developmentStarted = false;
    ProductEditor *m_editor = nullptr;
    QStackedWidget *m_pages = nullptr;
    QButtonGroup *m_navigation = nullptr;
    QButtonGroup *m_panels = nullptr;
    QLabel *m_productLabel = nullptr;
    QLabel *m_productLocation = nullptr;
    QLabel *m_headerTitle = nullptr;
    QLabel *m_headerSubtitle = nullptr;
    QLabel *m_previewBadge = nullptr;
    QLabel *m_overviewFacts = nullptr;
    QLabel *m_deliveryStatus = nullptr;
    QLabel *m_deliveryDetails = nullptr;
    QLabel *m_exportLocation = nullptr;
    QPushButton *m_revealExport = nullptr;
    QPushButton *m_primary = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLabel *m_notice = nullptr;
    QPushButton *m_noticeDetails = nullptr;
    QString m_details;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_eventsButton = nullptr;
    QWidget *m_logDrawer = nullptr;
    QLineEdit *m_logFilter = nullptr;
    QToolButton *m_followEvents = nullptr;

    AplPlatform *m_platform;
    QQuickWidget *m_stage = nullptr;
    QLabel *m_stageNote = nullptr;
    QLabel *m_interactionHint = nullptr;
    QMenu *m_viewMenu = nullptr;
    QAction *m_supplyAction = nullptr;
    QAction *m_pauseAction = nullptr;
    QLabel *m_message = nullptr;
    QSlider *m_waterSlider = nullptr;
    QSlider *m_pm25Slider = nullptr;
    QSlider *m_humiditySlider = nullptr;
    QSlider *m_temperatureSlider = nullptr;
    QSpinBox *m_waterValue = nullptr;
    QSpinBox *m_pm25Value = nullptr;
    QSpinBox *m_humidityValue = nullptr;
    QSpinBox *m_temperatureValue = nullptr;
    QLabel *m_power = nullptr;
    QLabel *m_gear = nullptr;
    QLabel *m_timer = nullptr;
    QLabel *m_digits = nullptr;
    QLabel *m_childLock = nullptr;
    QLabel *m_panelLight = nullptr;
    QLabel *m_powerup = nullptr;
    QLabel *m_purify = nullptr;
    QLabel *m_humidify = nullptr;
    QLabel *m_fan = nullptr;
    QLabel *m_humidifier = nullptr;
    QLabel *m_uv = nullptr;
    QLabel *m_anion = nullptr;
    QLabel *m_pmReads = nullptr;
    QLabel *m_uvCalls = nullptr;
    QLabel *m_anionCalls = nullptr;
    QLabel *m_waterTriggers = nullptr;
    QLabel *m_waterRecovers = nullptr;
    QPlainTextEdit *m_log = nullptr;
};
