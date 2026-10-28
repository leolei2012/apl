#pragma once

#include "frontend_catalog.h"
#include "apl_memory.h"
#include "apl_runtime.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QMap>
#include <array>

extern "C" {
#include "apl.h"
}

/**
 * 仿真台宿主。持有一份 struct apl，注入按键和传感器，每 10 ms 调用 apl_tick。
 * QML 只读这里的属性和上报按键电平。
 */
class AplPlatform : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList displaySlots READ displaySlots NOTIFY frameChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY frameChanged)
    Q_PROPERTY(bool supplied READ supplied WRITE setSupplied NOTIFY suppliedChanged)
    Q_PROPERTY(bool powerOn READ powerOn NOTIFY frameChanged)
    Q_PROPERTY(int runState READ runState NOTIFY frameChanged)
    Q_PROPERTY(QString runStateText READ runStateText NOTIFY frameChanged)
    Q_PROPERTY(int drySeconds READ drySeconds NOTIFY frameChanged)
    Q_PROPERTY(QString interactionHint READ interactionHint NOTIFY frameChanged)
    Q_PROPERTY(int gear READ gear NOTIFY frameChanged)
    Q_PROPERTY(QString gearName READ gearName NOTIFY frameChanged)
    Q_PROPERTY(int timerSeconds READ timerSeconds NOTIFY frameChanged)
    Q_PROPERTY(bool childLock READ childLock NOTIFY frameChanged)
    Q_PROPERTY(bool panelLight READ panelLight NOTIFY frameChanged)
    Q_PROPERTY(bool powerupShow READ powerupShow NOTIFY frameChanged)
    Q_PROPERTY(QString message READ message NOTIFY frameChanged)
    Q_PROPERTY(QVariantList keyLamps READ keyLamps NOTIFY frameChanged)
    Q_PROPERTY(QVariantList waterLamps READ waterLamps NOTIFY frameChanged)
    Q_PROPERTY(QVariantList iconLamps READ iconLamps NOTIFY frameChanged)
    Q_PROPERTY(int waterColor READ waterColor NOTIFY frameChanged)
    Q_PROPERTY(QString digits READ digits NOTIFY frameChanged)
    Q_PROPERTY(int digitBrightness READ digitBrightness NOTIFY frameChanged)
    Q_PROPERTY(int digitUnit READ digitUnit NOTIFY frameChanged)
    Q_PROPERTY(int ambientBrightness READ ambientBrightness NOTIFY frameChanged)
    Q_PROPERTY(int ambientRed READ ambientRed NOTIFY frameChanged)
    Q_PROPERTY(int ambientGreen READ ambientGreen NOTIFY frameChanged)
    Q_PROPERTY(int ambientBlue READ ambientBlue NOTIFY frameChanged)
    Q_PROPERTY(bool purifyEnabled READ purifyEnabled NOTIFY frameChanged)
    Q_PROPERTY(bool humidifyEnabled READ humidifyEnabled NOTIFY frameChanged)
    Q_PROPERTY(bool purifyOn READ purifyOn NOTIFY frameChanged)
    Q_PROPERTY(bool humidifyOn READ humidifyOn NOTIFY frameChanged)
    Q_PROPERTY(QString fanText READ fanText NOTIFY frameChanged)
    Q_PROPERTY(QString humidifierText READ humidifierText NOTIFY frameChanged)
    Q_PROPERTY(QString uvText READ uvText NOTIFY frameChanged)
    Q_PROPERTY(QString anionText READ anionText NOTIFY frameChanged)
    Q_PROPERTY(int pmReads READ pmReads NOTIFY frameChanged)
    Q_PROPERTY(int uvCalls READ uvCalls NOTIFY frameChanged)
    Q_PROPERTY(int anionCalls READ anionCalls NOTIFY frameChanged)
    Q_PROPERTY(int waterTriggers READ waterTriggers NOTIFY frameChanged)
    Q_PROPERTY(int waterRecovers READ waterRecovers NOTIFY frameChanged)
    Q_PROPERTY(int water READ water WRITE setWater NOTIFY waterChanged)
    Q_PROPERTY(int pm25 READ pm25 WRITE setPm25 NOTIFY pm25Changed)
    Q_PROPERTY(int humidity READ humidity WRITE setHumidity NOTIFY humidityChanged)
    Q_PROPERTY(int temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(int keyMask READ keyMask NOTIFY keysChanged)
    Q_PROPERTY(QStringList keyNames READ keyNames CONSTANT)
    Q_PROPERTY(FrontendCatalog *frontends READ frontends CONSTANT)
    Q_PROPERTY(int frontendIndex READ frontendIndex NOTIFY frontendChanged)
    Q_PROPERTY(QUrl frontendUrl READ frontendUrl NOTIFY frontendChanged)
    Q_PROPERTY(QString frontendName READ frontendName NOTIFY frontendChanged)
    Q_PROPERTY(QString frontendDescription READ frontendDescription NOTIFY frontendChanged)
    Q_PROPERTY(QString eventLog READ eventLog NOTIFY eventsChanged)

public:
    explicit AplPlatform(QObject *parent = nullptr);

    void start();
    bool loadRuntime(const QString &path, QString *error, std::shared_ptr<QTemporaryDir> directory = {});
    bool openMemory(const QString &path);
    bool memoryRead(uint32_t offset,uint8_t *data,uint32_t length) const;
    bool memoryWrite(uint32_t offset,const uint8_t *data,uint32_t length);
    bool memoryErase();
    int memoryWrites() const { return m_memory.writes(); }
    QVariantList displaySlots() const;
    void displaySet(int slot,const QString &label,const QString &value);
    void displayClear(int slot);
    void fanSet(int rpm);
    void humidifierSet(bool on);
    bool paused() const { return m_paused; }
    void setPaused(bool value);
    void advance(int count);
    void queryFilter();

    bool supplied() const;
    void setSupplied(bool on);
    bool powerOn() const;
    int runState() const { return m_runState; }
    QString runStateText() const;
    int drySeconds() const { return m_drySeconds; }
    QString interactionHint() const;
    bool purifyOn() const { return m_purifyOn; }
    bool humidifyOn() const { return m_humidifyOn; }
    int gear() const;
    QString gearName() const;
    int timerSeconds() const;
    bool childLock() const;
    bool panelLight() const;
    bool powerupShow() const;
    QString message() const;
    QVariantList keyLamps() const;
    QVariantList waterLamps() const;
    QVariantList iconLamps() const { return m_iconLamps; }
    int waterColor() const { return m_waterColor; }
    QString digits() const;
    int digitBrightness() const;
    int digitUnit() const;
    int ambientBrightness() const;
    int ambientRed() const;
    int ambientGreen() const;
    int ambientBlue() const;
    bool purifyEnabled() const;
    bool humidifyEnabled() const;
    QString fanText() const;
    QString humidifierText() const;
    QString uvText() const;
    QString anionText() const;
    int pmReads() const;
    int uvCalls() const;
    int anionCalls() const;
    int waterTriggers() const;
    int waterRecovers() const;
    int water() const;
    void setWater(int value);
    int pm25() const;
    void setPm25(int value);
    int humidity() const;
    void setHumidity(int value);
    int temperature() const;
    void setTemperature(int value);
    int keyMask() const;
    QStringList keyNames() const;
    FrontendCatalog *frontends();
    int frontendIndex() const;
    QUrl frontendUrl() const;
    QString frontendName() const;
    QString frontendDescription() const;
    QString eventLog() const;

    Q_INVOKABLE void reset();
    Q_INVOKABLE void keyDown(int key);
    Q_INVOKABLE void keyUp(int key);
    Q_INVOKABLE void selectFrontend(int index);

signals:
    void frameChanged();
    void suppliedChanged();
    void keysChanged();
    void waterChanged();
    void pm25Changed();
    void humidityChanged();
    void temperatureChanged();
    void frontendChanged();
    void eventsChanged();

private:
    struct Snapshot
    {
        std::array<int, APL_HMI_KEY_COUNT> keys;
        std::array<int, APL_HMI_WATER_LAMP_COUNT> water;
        QString digits;
        int unit;
        int bright;
    };

    static enum apl_water_level getWaterLevel(void *ctx);
    static uint16_t getPm25(void *ctx);
    static uint8_t getHumidity(void *ctx);
    static int16_t getTemperature(void *ctx);
    static void waterTrigger(struct apl_core *core, void *ctx);
    static void waterRecover(struct apl_core *core, void *ctx);
    static void setFan(enum apl_switch state, enum apl_fan_gear gear, void *ctx);
    static void setPump(enum apl_switch state, void *ctx);
    static void setUv(enum apl_switch state, void *ctx);
    static void setAnion(enum apl_switch state, void *ctx);
    static void powerOffEvent(struct apl_core *core, void *ctx);
    static void getKeyState(struct apl_hmi_key_state *state, void *ctx);
    static void keyEvent(enum apl_hmi_key_id key, uint8_t isLong, void *ctx);

    void wireOps();
    void stepOne();
    void onClock();
    void pull();
    void observe();
    void flush();
    void appendEvent(const QString &text);
    void setMessage(const QString &text);
    void releaseKeys();
    void supplyOff();
    void supplyOn();
    void applySwitch(bool *known, bool *on, bool next, const QString &label);
    QString readDigits() const;
    QString gearNameFor(int gear) const;
    QString switchText(bool known, bool on) const;
    Snapshot capture() const;

    AplRuntime m_runtime;
    AplMemory m_memory;
    QMap<int,QVariantMap> m_displaySlots;
    bool m_paused = false;
    FrontendCatalog *m_catalog;
    struct apl m_apl;
    struct apl_core_ops m_coreOps;
    struct apl_hmi_ops m_hmiOps;
    std::array<bool, APL_HMI_KEY_COUNT> m_down;
    QStringList m_keyNames;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_accounted;
    bool m_inStep;
    bool m_frameDirty;
    bool m_eventsDirty;
    bool m_supplied;
    bool m_panelLight;
    int m_water;
    int m_pm25;
    int m_humidity;
    int m_temperature;
    int m_pmReads;
    int m_uvCalls;
    int m_anionCalls;
    int m_triggers;
    int m_recovers;
    bool m_fanKnown;
    int m_fanRpm;
    int m_fanOutputGear = -1;
    bool m_humidifierKnown;
    bool m_humidifierOn;
    bool m_uvKnown;
    bool m_uvOn;
    bool m_anionKnown;
    bool m_anionOn;
    bool m_powerOn;
    int m_runState = APL_RUN_STANDBY;
    int m_drySeconds = 0;
    bool m_purifyOn = false;
    bool m_humidifyOn = false;
    int m_gear;
    QString m_gearName;
    int m_timerSeconds;
    bool m_childLock;
    bool m_powerupShow;
    QString m_message;
    std::array<int, APL_HMI_KEY_COUNT> m_keyLampRaw;
    std::array<int, APL_HMI_WATER_LAMP_COUNT> m_waterLampRaw;
    QVariantList m_keyLamps;
    QVariantList m_waterLamps;
    QVariantList m_iconLamps;
    int m_waterColor = 0;
    QString m_digits;
    int m_digitBrightness;
    int m_digitUnit;
    int m_ambientBrightness;
    int m_ambientRed;
    int m_ambientGreen;
    int m_ambientBlue;
    bool m_purifyEnabled;
    bool m_humidifyEnabled;
    QString m_fanText;
    QString m_humidifierText;
    QString m_uvText;
    QString m_anionText;
    QStringList m_events;
    Snapshot m_snapshot;
    bool m_haveSnapshot;
    int m_frontendIndex;
};
