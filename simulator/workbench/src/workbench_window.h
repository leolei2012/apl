#pragma once

#include <QMainWindow>

class AplPlatform;
class QAction;
class QLabel;
class QListView;
class QMenu;
class QPlainTextEdit;
class QQuickWidget;
class QSlider;

/** 仿真台窗口。顶部是菜单栏，中间的面贴是 QML。 */
class WorkbenchWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit WorkbenchWindow(AplPlatform *platform, QWidget *parent = nullptr);

private:
    void buildMenu();
    void buildChrome();
    void bind();
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
    QWidget *makePanel() const;
    QLabel *addSection(class QVBoxLayout *layout, const QString &text);
    QLabel *addLine(class QVBoxLayout *layout, const QString &color);
    void makeSensor(class QVBoxLayout *layout, const QString &title, int low, int high,
                    QSlider **slider, QLabel **reading);

    AplPlatform *m_platform;
    QQuickWidget *m_stage;
    QLabel *m_stageNote;
    QListView *m_frontends;
    QLabel *m_description;
    QMenu *m_viewMenu;
    QAction *m_supplyAction;
    QLabel *m_message;
    QSlider *m_waterSlider;
    QSlider *m_pm25Slider;
    QSlider *m_humiditySlider;
    QSlider *m_temperatureSlider;
    QLabel *m_waterValue;
    QLabel *m_pm25Value;
    QLabel *m_humidityValue;
    QLabel *m_temperatureValue;
    QLabel *m_power;
    QLabel *m_gear;
    QLabel *m_timer;
    QLabel *m_digits;
    QLabel *m_childLock;
    QLabel *m_panelLight;
    QLabel *m_powerup;
    QLabel *m_purify;
    QLabel *m_humidify;
    QLabel *m_fan;
    QLabel *m_humidifier;
    QLabel *m_uv;
    QLabel *m_anion;
    QLabel *m_pmReads;
    QLabel *m_uvCalls;
    QLabel *m_anionCalls;
    QLabel *m_waterTriggers;
    QLabel *m_waterRecovers;
    QPlainTextEdit *m_log;
};
