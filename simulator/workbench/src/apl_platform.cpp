#include "apl_platform.h"

#include <QTime>

#include <climits>
#include <cstdio>
#include <cstring>

namespace {

const uint32_t k_hour_seconds = 3600u;
const uint32_t k_timer_hour_max = 12u;

template <typename T>
bool assign(T *dst, const T &src)
{
    if (*dst == src)
    {
        return false;
    }

    *dst = src;
    return true;
}

}

AplPlatform::AplPlatform(QObject *parent)
    : QObject(parent)
    , m_catalog(new FrontendCatalog(this))
    , m_apl()
    , m_coreOps()
    , m_hmiOps()
    , m_keySlot()
    , m_down()
    , m_accounted(0)
    , m_inStep(false)
    , m_frameDirty(false)
    , m_eventsDirty(false)
    , m_supplied(true)
    , m_panelLight(true)
    , m_water(2)
    , m_pm25(28)
    , m_humidity(55)
    , m_temperature(25)
    , m_pmReads(0)
    , m_uvCalls(0)
    , m_anionCalls(0)
    , m_triggers(0)
    , m_recovers(0)
    , m_fanKnown(false)
    , m_fanRpm(0)
    , m_humidifierKnown(false)
    , m_humidifierOn(false)
    , m_uvKnown(false)
    , m_uvOn(false)
    , m_anionKnown(false)
    , m_anionOn(false)
    , m_powerOn(false)
    , m_gear(-1)
    , m_timerSeconds(-1)
    , m_childLock(false)
    , m_powerupShow(false)
    , m_keyLampRaw()
    , m_waterLampRaw()
    , m_digitBrightness(-1)
    , m_digitUnit(-1)
    , m_ambientBrightness(-1)
    , m_ambientRed(-1)
    , m_ambientGreen(-1)
    , m_ambientBlue(-1)
    , m_purifyEnabled(false)
    , m_humidifyEnabled(false)
    , m_haveSnapshot(false)
    , m_frontendIndex(0)
{
    m_keyNames = {
        QStringLiteral("Purify"),
        QStringLiteral("Humidity"),
        QStringLiteral("Timer"),
        QStringLiteral("Light"),
        QStringLiteral("Speed"),
        QStringLiteral("Power"),
    };
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        m_keySlot[key].platform = this;
        m_keySlot[key].key = key;
        m_down[key] = false;
        m_keyLampRaw[key] = -1;
    }
    for (int index = 0; index < static_cast<int>(APL_HMI_WATER_LAMP_COUNT); ++index)
    {
        m_waterLampRaw[index] = -1;
    }

    wireOps();
    connect(&m_timer, &QTimer::timeout, this, &AplPlatform::onClock);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(APL_CORE_TICK_MS);
    reset();
}

void AplPlatform::start()
{
    m_clock.start();
    m_accounted = 0;
    m_timer.start();
}

bool AplPlatform::openMemory(const QString &path)
{
    if(m_memory.open(path)) return true;
    setMessage("存储器初始化失败: "+m_memory.error()); flush(); return false;
}
bool AplPlatform::memoryRead(uint32_t offset,uint8_t *data,uint32_t length) const
{
    return m_supplied && m_memory.read(offset,data,length);
}
bool AplPlatform::memoryWrite(uint32_t offset,const uint8_t *data,uint32_t length)
{
    if(!m_supplied) return false;
    const int before=m_memory.writes();
    if(!m_memory.write(offset,data,length)) { setMessage("存储写入失败: "+m_memory.error()); flush(); return false; }
    if(m_memory.writes()!=before) { appendEvent(QString("存储写入: %1 / %2 字节").arg(offset).arg(length)); flush(); }
    return true;
}
bool AplPlatform::memoryErase()
{
    if(!m_supplied) return false;
    const int before=m_memory.writes();
    if(!m_memory.erase()) { setMessage("存储擦除失败: "+m_memory.error()); flush(); return false; }
    if(m_memory.writes()!=before) { appendEvent(QStringLiteral("存储器已擦除")); flush(); }
    return true;
}
QVariantList AplPlatform::displaySlots() const
{
    QVariantList entries;
    for(int i=0;i<6;++i) entries.append(m_displaySlots.value(i));
    return entries;
}
void AplPlatform::displaySet(int slot,const QString &label,const QString &value)
{
    if(slot<0 || slot>=6) return;
    const QVariantMap next{{"label",label},{"value",value}};
    if(m_displaySlots.value(slot)==next) return;
    m_displaySlots[slot]=next;
    appendEvent(QString("槽位 %1: %2 = %3").arg(slot).arg(label,value)); m_frameDirty=true; flush();
}
void AplPlatform::displayClear(int slot)
{
    if(m_displaySlots.remove(slot)) { appendEvent(QString("槽位 %1 清空").arg(slot)); m_frameDirty=true; flush(); }
}
void AplPlatform::fanSet(int rpm)
{
    if(!m_supplied || rpm<0 || (m_fanKnown && m_fanRpm==rpm)) return;
    m_fanKnown=true; m_fanRpm=rpm;
    appendEvent(QString("风机输出: %1 rpm").arg(rpm)); pull(); flush();
}
void AplPlatform::humidifierSet(bool on)
{
    if(!m_supplied) return;
    applySwitch(&m_humidifierKnown,&m_humidifierOn,on,QStringLiteral("加湿器")); pull(); flush();
}
void AplPlatform::setPaused(bool value)
{
    if(m_paused==value) return;
    m_paused = value;
    appendEvent(value ? QStringLiteral("仿真时钟暂停") : QStringLiteral("仿真时钟恢复"));
    flush();
    m_clock.restart(); m_accounted = 0;
    emit frameChanged();
}

void AplPlatform::advance(int count)
{
    for (int step = 0; step < count; ++step)
    {
        stepOne();
    }
}

bool AplPlatform::supplied() const
{
    return m_supplied;
}

void AplPlatform::setSupplied(bool on)
{
    if (on == m_supplied)
    {
        return;
    }

    if (on)
    {
        supplyOn();
    }
    else
    {
        supplyOff();
    }
}

bool AplPlatform::powerOn() const
{
    return m_powerOn;
}

int AplPlatform::gear() const
{
    return m_gear;
}

QString AplPlatform::gearName() const
{
    return m_gearName;
}

int AplPlatform::timerSeconds() const
{
    return m_timerSeconds;
}

bool AplPlatform::childLock() const
{
    return m_childLock;
}

bool AplPlatform::panelLight() const
{
    return m_panelLight;
}

bool AplPlatform::powerupShow() const
{
    return m_powerupShow;
}

QString AplPlatform::message() const
{
    return m_message;
}

QVariantList AplPlatform::keyLamps() const
{
    return m_keyLamps;
}

QVariantList AplPlatform::waterLamps() const
{
    return m_waterLamps;
}

QString AplPlatform::digits() const
{
    return m_digits;
}

int AplPlatform::digitBrightness() const
{
    return m_digitBrightness;
}

int AplPlatform::digitUnit() const
{
    return m_digitUnit;
}

int AplPlatform::ambientBrightness() const
{
    return m_ambientBrightness;
}

int AplPlatform::ambientRed() const
{
    return m_ambientRed;
}

int AplPlatform::ambientGreen() const
{
    return m_ambientGreen;
}

int AplPlatform::ambientBlue() const
{
    return m_ambientBlue;
}

bool AplPlatform::purifyEnabled() const
{
    return m_purifyEnabled;
}

bool AplPlatform::humidifyEnabled() const
{
    return m_humidifyEnabled;
}

QString AplPlatform::fanText() const
{
    return m_fanText;
}

QString AplPlatform::humidifierText() const
{
    return m_humidifierText;
}

QString AplPlatform::uvText() const
{
    return m_uvText;
}

QString AplPlatform::anionText() const
{
    return m_anionText;
}

int AplPlatform::pmReads() const
{
    return m_pmReads;
}

int AplPlatform::uvCalls() const
{
    return m_uvCalls;
}

int AplPlatform::anionCalls() const
{
    return m_anionCalls;
}

int AplPlatform::waterTriggers() const
{
    return m_triggers;
}

int AplPlatform::waterRecovers() const
{
    return m_recovers;
}

int AplPlatform::water() const
{
    return m_water;
}

void AplPlatform::setWater(int value)
{
    if (value < APL_WATER_LEVEL_0 || value > APL_WATER_LEVEL_4 || value == m_water)
    {
        return;
    }

    const int old = m_water;
    m_water = value;
    appendEvent(QStringLiteral("水位输入: %1 -> %2").arg(old).arg(value));
    emit waterChanged();
    flush();
}

int AplPlatform::pm25() const
{
    return m_pm25;
}

void AplPlatform::setPm25(int value)
{
    if (value < 0 || value > 1000 || value == m_pm25)
    {
        return;
    }

    const int old = m_pm25;
    m_pm25 = value;
    appendEvent(QStringLiteral("PM2.5 输入: %1 -> %2").arg(old).arg(value));
    emit pm25Changed();
    flush();
}

int AplPlatform::humidity() const
{
    return m_humidity;
}

void AplPlatform::setHumidity(int value)
{
    if (value < 0 || value > 100 || value == m_humidity)
    {
        return;
    }

    const int old = m_humidity;
    m_humidity = value;
    appendEvent(QStringLiteral("湿度输入: %1 -> %2").arg(old).arg(value));
    emit humidityChanged();
    flush();
}

int AplPlatform::temperature() const
{
    return m_temperature;
}

void AplPlatform::setTemperature(int value)
{
    if (value < -20 || value > 60 || value == m_temperature)
    {
        return;
    }

    const int old = m_temperature;
    m_temperature = value;
    appendEvent(QStringLiteral("温度输入: %1 -> %2").arg(old).arg(value));
    emit temperatureChanged();
    flush();
}

int AplPlatform::keyMask() const
{
    int mask = 0;
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        if (m_down[key])
        {
            mask |= (1 << key);
        }
    }

    return mask;
}

QStringList AplPlatform::keyNames() const
{
    return m_keyNames;
}

FrontendCatalog *AplPlatform::frontends()
{
    return m_catalog;
}

int AplPlatform::frontendIndex() const
{
    return m_frontendIndex;
}

QUrl AplPlatform::frontendUrl() const
{
    return m_catalog->urlAt(m_frontendIndex);
}

QString AplPlatform::frontendName() const
{
    return m_catalog->nameAt(m_frontendIndex);
}

QString AplPlatform::frontendDescription() const
{
    return m_catalog->descriptionAt(m_frontendIndex);
}

QString AplPlatform::eventLog() const
{
    return m_events.join(QStringLiteral("\n"));
}

void AplPlatform::reset()
{
    if (!m_supplied)
    {
        setMessage(QStringLiteral("无供电，无法重置"));
        return;
    }

    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        m_down[key] = false;
    }
    emit keysChanged();
    m_panelLight = true;
    m_frameDirty = true;
    setMessage(QStringLiteral("重新初始化"));
    apl_init(&m_apl, &m_coreOps, this, &m_hmiOps, this);
    bindKeys();
    publish();
    pull();
    flush();
}

void AplPlatform::keyDown(int key)
{
    if (!m_supplied || key < 0 || key >= APL_HMI_KEY_COUNT || m_down[key])
    {
        return;
    }

    m_down[key] = true;
    emit keysChanged();
    stepOne();
}

void AplPlatform::keyUp(int key)
{
    if (!m_supplied || key < 0 || key >= APL_HMI_KEY_COUNT || !m_down[key])
    {
        return;
    }

    m_down[key] = false;
    emit keysChanged();
    stepOne();
}

void AplPlatform::selectFrontend(int index)
{
    if (index < 0 || index >= m_catalog->rowCount() || index == m_frontendIndex)
    {
        return;
    }

    m_frontendIndex = index;
    emit frontendChanged();
    releaseKeys();
}

enum apl_water_level AplPlatform::waterLevelGet(void *ctx)
{
    return static_cast<enum apl_water_level>(static_cast<AplPlatform *>(ctx)->m_water);
}

uint16_t AplPlatform::pm25Get(void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_pmReads += 1;
    self->m_frameDirty = true;
    return static_cast<uint16_t>(self->m_pm25);
}

uint8_t AplPlatform::humidityGet(void *ctx)
{
    return static_cast<uint8_t>(static_cast<AplPlatform *>(ctx)->m_humidity);
}

int16_t AplPlatform::temperatureGet(void *ctx)
{
    return static_cast<int16_t>(static_cast<AplPlatform *>(ctx)->m_temperature);
}

void AplPlatform::waterTrigger(struct apl_core *core, void *ctx)
{
    (void)core;
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_triggers += 1;
    self->m_frameDirty = true;
    self->setMessage(QStringLiteral("水位降到 0"));
}

void AplPlatform::waterRecover(struct apl_core *core, void *ctx)
{
    (void)core;
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_recovers += 1;
    self->m_frameDirty = true;
    self->setMessage(QStringLiteral("水位恢复"));
}

void AplPlatform::uvSet(enum apl_switch state, void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_uvCalls += 1;
    self->m_frameDirty = true;
    self->applySwitch(&self->m_uvKnown, &self->m_uvOn, state == APL_SWITCH_ON, QStringLiteral("UV"));
}

void AplPlatform::anionSet(enum apl_switch state, void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_anionCalls += 1;
    self->m_frameDirty = true;
    self->applySwitch(&self->m_anionKnown, &self->m_anionOn, state == APL_SWITCH_ON, QStringLiteral("负离子"));
}

void AplPlatform::powerOffEvent(struct apl_core *core, void *ctx)
{
    (void)core;
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    apl_hmi_child_lock_set(&self->m_apl.hmi, APL_HMI_CHILD_LOCK_OFF);
}

void AplPlatform::keyStateGet(struct apl_hmi_key_state *state, void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    state->purify = self->m_down[APL_HMI_KEY_PURIFY] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->humidity = self->m_down[APL_HMI_KEY_HUMIDITY] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->timer = self->m_down[APL_HMI_KEY_TIMER] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->light = self->m_down[APL_HMI_KEY_LIGHT] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->speed = self->m_down[APL_HMI_KEY_SPEED] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->power = self->m_down[APL_HMI_KEY_POWER] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
}

void AplPlatform::onTimer(struct apl_core *core, void *ctx)
{
    (void)core;
    static_cast<AplPlatform *>(ctx)->handleTimer();
}

void AplPlatform::onShort(struct apl_hmi *hmi, void *ctx)
{
    (void)hmi;
    KeySlot *slot = static_cast<KeySlot *>(ctx);
    slot->platform->handleShort(slot->key);
}

void AplPlatform::onLong(struct apl_hmi *hmi, void *ctx)
{
    (void)hmi;
    KeySlot *slot = static_cast<KeySlot *>(ctx);
    slot->platform->handleLong(slot->key);
}

void AplPlatform::wireOps()
{
    std::memset(&m_coreOps, 0, sizeof(m_coreOps));
    std::memset(&m_hmiOps, 0, sizeof(m_hmiOps));
    m_coreOps.water_level_get = &AplPlatform::waterLevelGet;
    m_coreOps.pm25_get = &AplPlatform::pm25Get;
    m_coreOps.humidity_get = &AplPlatform::humidityGet;
    m_coreOps.temperature_get = &AplPlatform::temperatureGet;
    m_coreOps.water_on_trigger = &AplPlatform::waterTrigger;
    m_coreOps.water_on_recover = &AplPlatform::waterRecover;
    m_coreOps.uv_set = &AplPlatform::uvSet;
    m_coreOps.anion_set = &AplPlatform::anionSet;
    m_coreOps.on_power_off = &AplPlatform::powerOffEvent;
    m_hmiOps.key_state_get = &AplPlatform::keyStateGet;
}

void AplPlatform::bindKeys()
{
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        apl_hmi_key_callback_set(&m_apl.hmi,
                                 static_cast<enum apl_hmi_key_id>(key),
                                 &AplPlatform::onShort,
                                 &AplPlatform::onLong,
                                 &m_keySlot[key]);
    }
}

void AplPlatform::stepOne()
{
    if (!m_supplied || m_inStep)
    {
        return;
    }

    m_inStep = true;
    apl_tick(&m_apl);
    publish();
    pull();
    m_inStep = false;
    flush();
}

void AplPlatform::onClock()
{
    if (!m_supplied || m_paused)
    {
        return;
    }

    const qint64 now = m_clock.elapsed();
    int due = static_cast<int>((now - m_accounted) / APL_CORE_TICK_MS);
    if (due < 1)
    {
        due = 1;
    }
    if (due > 20)
    {
        m_accounted = now;
        due = 20;
    }
    else
    {
        m_accounted += static_cast<qint64>(due) * APL_CORE_TICK_MS;
    }

    advance(due);
}

void AplPlatform::publish()
{
    if (!m_supplied)
    {
        return;
    }

    const bool power_on = apl_core_power_get(&m_apl.core) == APL_POWER_ON;
#if APL_COMPILE_PURIFY
    const uint8_t purify = apl_core_purify_enabled_get(&m_apl.core);
#else
    const uint8_t purify = 0u;
#endif
#if APL_COMPILE_HUMIDIFY
    const uint8_t humidify = apl_core_humidify_enabled_get(&m_apl.core);
#else
    const uint8_t humidify = 0u;
#endif
    apl_hmi_purify_enabled_set(&m_apl.hmi, purify);
    apl_hmi_humidify_enabled_set(&m_apl.hmi, humidify);
    if (apl_hmi_powerup_lamp_get(&m_apl.hmi) != 0u)
    {
        observe();
        return;
    }

    apl_hmi_key_lamp_set(&m_apl.hmi, APL_HMI_KEY_POWER, power_on ? APL_HMI_LAMP_FULL : APL_HMI_LAMP_HALF);
    apl_hmi_key_lamp_set(&m_apl.hmi, APL_HMI_KEY_SPEED, power_on ? APL_HMI_LAMP_FULL : 0u);
    apl_hmi_key_lamp_set(&m_apl.hmi,
                         APL_HMI_KEY_TIMER,
                         apl_core_timer_get(&m_apl.core) > 0u ? APL_HMI_LAMP_FULL : 0u);
    apl_hmi_key_lamp_set(&m_apl.hmi, APL_HMI_KEY_PURIFY, (power_on && purify != 0u) ? APL_HMI_LAMP_FULL : 0u);
    apl_hmi_key_lamp_set(&m_apl.hmi, APL_HMI_KEY_HUMIDITY, (power_on && humidify != 0u) ? APL_HMI_LAMP_FULL : 0u);
    apl_hmi_key_lamp_set(&m_apl.hmi, APL_HMI_KEY_LIGHT, (power_on && m_panelLight) ? APL_HMI_LAMP_FULL : 0u);
    for (int index = 1; index <= static_cast<int>(APL_HMI_WATER_LAMP_COUNT); ++index)
    {
        const uint8_t lit = (humidify != 0u && m_water >= index) ? APL_HMI_LAMP_FULL : 0u;
        apl_hmi_water_lamp_set(&m_apl.hmi, static_cast<uint8_t>(index), lit);
    }

    const uint32_t seconds = apl_core_timer_get(&m_apl.core);
    char text[APL_HMI_DIGIT_COUNT] = {' ', ' ', ' '};
    enum apl_hmi_unit unit = APL_HMI_UNIT_NONE;
    if (seconds > 0u)
    {
        const unsigned shown_source = seconds >= k_hour_seconds ? (seconds / k_hour_seconds) : seconds;
        const unsigned shown = shown_source > 999u ? 999u : shown_source;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%3u", shown);
        text[0] = buf[0];
        text[1] = buf[1];
        text[2] = buf[2];
        if (seconds >= k_hour_seconds)
        {
            unit = APL_HMI_UNIT_HOUR;
        }
    }

    apl_hmi_digits_brightness_set(&m_apl.hmi, (power_on && m_panelLight) ? APL_HMI_LAMP_FULL : 0u);
    apl_hmi_digits_content_set(&m_apl.hmi, text);
    apl_hmi_digits_unit_set(&m_apl.hmi, unit);
    apl_hmi_ambient_brightness_set(&m_apl.hmi, 0u);
    observe();
}

void AplPlatform::pull()
{
    bool dirty = false;
    const bool power_on = apl_core_power_get(&m_apl.core) == APL_POWER_ON;
    dirty = assign(&m_powerOn, power_on) || dirty;

    const int gear = static_cast<int>(apl_core_fan_gear_get(&m_apl.core));
    if (assign(&m_gear, gear))
    {
        m_gearName = gearNameFor(gear);
        dirty = true;
    }

    const uint32_t raw_seconds = apl_core_timer_get(&m_apl.core);
    const int seconds = raw_seconds > static_cast<uint32_t>(INT_MAX)
        ? INT_MAX
        : static_cast<int>(raw_seconds);
    dirty = assign(&m_timerSeconds, seconds) || dirty;
    dirty = assign(&m_childLock, apl_hmi_child_lock_get(&m_apl.hmi) == APL_HMI_CHILD_LOCK_ON) || dirty;
    dirty = assign(&m_powerupShow, apl_hmi_powerup_lamp_get(&m_apl.hmi) != 0u) || dirty;

    bool lamps_changed = false;
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        const int lamp = apl_hmi_key_lamp_get(&m_apl.hmi, static_cast<enum apl_hmi_key_id>(key));
        if (m_keyLampRaw[key] != lamp)
        {
            m_keyLampRaw[key] = lamp;
            lamps_changed = true;
        }
    }
    if (lamps_changed)
    {
        QVariantList list;
        for (int lamp : m_keyLampRaw)
        {
            list.append(lamp);
        }
        m_keyLamps = list;
        dirty = true;
    }

    bool water_changed = false;
    for (int index = 0; index < static_cast<int>(APL_HMI_WATER_LAMP_COUNT); ++index)
    {
        const int lamp = apl_hmi_water_lamp_get(&m_apl.hmi, static_cast<uint8_t>(index + 1));
        if (m_waterLampRaw[index] != lamp)
        {
            m_waterLampRaw[index] = lamp;
            water_changed = true;
        }
    }
    if (water_changed)
    {
        QVariantList list;
        for (int lamp : m_waterLampRaw)
        {
            list.append(lamp);
        }
        m_waterLamps = list;
        dirty = true;
    }

    dirty = assign(&m_digits, readDigits()) || dirty;
    dirty = assign(&m_digitBrightness, static_cast<int>(apl_hmi_digits_brightness_get(&m_apl.hmi))) || dirty;
    dirty = assign(&m_digitUnit, static_cast<int>(apl_hmi_digits_unit_get(&m_apl.hmi))) || dirty;

    uint8_t red = 0u;
    uint8_t green = 0u;
    uint8_t blue = 0u;
    apl_hmi_ambient_color_get(&m_apl.hmi, &red, &green, &blue);
    dirty = assign(&m_ambientBrightness, static_cast<int>(apl_hmi_ambient_brightness_get(&m_apl.hmi))) || dirty;
    dirty = assign(&m_ambientRed, static_cast<int>(red)) || dirty;
    dirty = assign(&m_ambientGreen, static_cast<int>(green)) || dirty;
    dirty = assign(&m_ambientBlue, static_cast<int>(blue)) || dirty;

#if APL_COMPILE_PURIFY
    dirty = assign(&m_purifyEnabled, apl_core_purify_enabled_get(&m_apl.core) != 0u) || dirty;
#else
    dirty = assign(&m_purifyEnabled, false) || dirty;
#endif
#if APL_COMPILE_HUMIDIFY
    dirty = assign(&m_humidifyEnabled, apl_core_humidify_enabled_get(&m_apl.core) != 0u) || dirty;
#else
    dirty = assign(&m_humidifyEnabled, false) || dirty;
#endif

    QString fan = QStringLiteral("未驱动");
    if (!m_supplied)
    {
        fan = QStringLiteral("停止（无供电）");
    }
    else if (m_fanKnown)
    {
        fan = m_fanRpm == 0
            ? QStringLiteral("停止 / 0 rpm")
            : QStringLiteral("运行 %1 rpm").arg(m_fanRpm);
    }
    dirty = assign(&m_fanText, fan) || dirty;
    dirty = assign(&m_humidifierText, switchText(m_humidifierKnown, m_humidifierOn)) || dirty;
    dirty = assign(&m_uvText, switchText(m_uvKnown, m_uvOn)) || dirty;
    dirty = assign(&m_anionText, switchText(m_anionKnown, m_anionOn)) || dirty;

    if (dirty)
    {
        m_frameDirty = true;
    }
}

void AplPlatform::observe()
{
    const Snapshot now = capture();
    if (m_haveSnapshot)
    {
        if (m_snapshot.keys != now.keys)
        {
            appendEvent(QStringLiteral("键灯: (%1, %2, %3, %4, %5, %6)")
                            .arg(now.keys[0])
                            .arg(now.keys[1])
                            .arg(now.keys[2])
                            .arg(now.keys[3])
                            .arg(now.keys[4])
                            .arg(now.keys[5]));
        }
        if (m_snapshot.water != now.water)
        {
            appendEvent(QStringLiteral("水位灯: (%1, %2, %3, %4)")
                            .arg(now.water[0])
                            .arg(now.water[1])
                            .arg(now.water[2])
                            .arg(now.water[3]));
        }
        if (m_snapshot.digits != now.digits || m_snapshot.unit != now.unit || m_snapshot.bright != now.bright)
        {
            appendEvent(QStringLiteral("数码管: ('%1', %2, %3)")
                            .arg(now.digits)
                            .arg(now.unit)
                            .arg(now.bright));
        }
    }

    m_snapshot = now;
    m_haveSnapshot = true;
}

void AplPlatform::flush()
{
    if (m_inStep)
    {
        return;
    }

    if (m_frameDirty)
    {
        m_frameDirty = false;
        emit frameChanged();
    }
    if (m_eventsDirty)
    {
        m_eventsDirty = false;
        emit eventsChanged();
    }
}

void AplPlatform::appendEvent(const QString &text)
{
    m_events.append(QTime::currentTime().toString(QStringLiteral("HH:mm:ss"))
                    + QStringLiteral("  ")
                    + text);
    if (m_events.size() > 500)
    {
        m_events.removeFirst();
    }
    m_eventsDirty = true;
}

void AplPlatform::setMessage(const QString &text)
{
    m_message = text;
    m_frameDirty = true;
    appendEvent(text);
    flush();
}

void AplPlatform::releaseKeys()
{
    bool any = false;
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        if (m_down[key])
        {
            m_down[key] = false;
            any = true;
        }
    }

    if (!any)
    {
        return;
    }

    emit keysChanged();
    stepOne();
}

void AplPlatform::supplyOff()
{
    if (!m_supplied)
    {
        return;
    }

    m_supplied = false;
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        m_down[key] = false;
    }
    emit keysChanged();
    emit suppliedChanged();
    appendEvent(QStringLiteral("风机: 停止（无供电）"));
    appendEvent(QStringLiteral("加湿器: 停止（无供电）"));
    appendEvent(QStringLiteral("UV: 停止（无供电）"));
    appendEvent(QStringLiteral("负离子: 停止（无供电）"));
    m_fanKnown = false;
    m_fanRpm = 0;
    m_humidifierKnown = false;
    m_humidifierOn = false;
    m_uvKnown = false;
    m_uvOn = false;
    m_anionKnown = false;
    m_anionOn = false;
    std::memset(&m_apl, 0, sizeof(m_apl));
    m_message = QStringLiteral("供电已断开");
    m_frameDirty = true;
    appendEvent(m_message);
    observe();
    pull();
    flush();
}

void AplPlatform::supplyOn()
{
    if (m_supplied)
    {
        return;
    }

    m_supplied = true;
    emit suppliedChanged();
    reset();
    m_message = QStringLiteral("供电已接通");
    m_frameDirty = true;
    appendEvent(m_message);
    flush();
}

void AplPlatform::handleShort(int key)
{
    if (!m_supplied || key < 0 || key >= APL_HMI_KEY_COUNT)
    {
        return;
    }

    appendEvent(m_keyNames.at(key) + QStringLiteral(" 短按"));
    switch (key)
    {
    case APL_HMI_KEY_POWER:
        if (apl_core_power_get(&m_apl.core) == APL_POWER_ON)
        {
            apl_core_power_off(&m_apl.core);
            setMessage(QStringLiteral("关机"));
        }
        else
        {
            apl_core_power_on(&m_apl.core);
            setMessage(QStringLiteral("开机"));
        }
        break;
    case APL_HMI_KEY_SPEED:
    {
        enum apl_fan_gear next = APL_FAN_GEAR_SLEEP;
        switch (apl_core_fan_gear_get(&m_apl.core))
        {
        case APL_FAN_GEAR_SLEEP:
            next = APL_FAN_GEAR_1;
            break;
        case APL_FAN_GEAR_1:
            next = APL_FAN_GEAR_2;
            break;
        case APL_FAN_GEAR_2:
            next = APL_FAN_GEAR_3;
            break;
        case APL_FAN_GEAR_3:
            next = APL_FAN_GEAR_AUTO;
            break;
        case APL_FAN_GEAR_AUTO:
        case APL_FAN_GEAR_TURBO:
            next = APL_FAN_GEAR_SLEEP;
            break;
        }
        apl_core_fan_gear_set(&m_apl.core, next);
        setMessage(gearNameFor(static_cast<int>(next)));
        break;
    }
    case APL_HMI_KEY_TIMER:
    {
        const uint32_t seconds = apl_core_timer_get(&m_apl.core);
        uint32_t hours = seconds == 0u ? 0u : (seconds + k_hour_seconds - 1u) / k_hour_seconds;
        if (hours >= k_timer_hour_max)
        {
            hours = 0u;
        }
        else
        {
            hours += 1u;
        }
        if (hours == 0u)
        {
            apl_core_timer_set(&m_apl.core, 0u, &AplPlatform::onTimer, this);
            setMessage(QStringLiteral("定时取消"));
        }
        else
        {
            apl_core_timer_set(&m_apl.core, hours * k_hour_seconds, &AplPlatform::onTimer, this);
            setMessage(QStringLiteral("定时 %1 小时").arg(hours));
        }
        break;
    }
    case APL_HMI_KEY_LIGHT:
        m_panelLight = !m_panelLight;
        m_frameDirty = true;
        setMessage(m_panelLight ? QStringLiteral("灯光开") : QStringLiteral("灯光关"));
        break;
    case APL_HMI_KEY_PURIFY:
        setMessage(QStringLiteral("净化输出尚未规定"));
        break;
    case APL_HMI_KEY_HUMIDITY:
        setMessage(QStringLiteral("加湿输出尚未规定"));
        break;
    default:
        break;
    }
}

void AplPlatform::handleLong(int key)
{
    if (!m_supplied || key < 0 || key >= APL_HMI_KEY_COUNT)
    {
        return;
    }

    appendEvent(m_keyNames.at(key) + QStringLiteral(" 长按"));
    switch (key)
    {
    case APL_HMI_KEY_TIMER:
    {
        const enum apl_hmi_child_lock next = apl_hmi_child_lock_get(&m_apl.hmi) == APL_HMI_CHILD_LOCK_ON
            ? APL_HMI_CHILD_LOCK_OFF
            : APL_HMI_CHILD_LOCK_ON;
        apl_hmi_child_lock_set(&m_apl.hmi, next);
        setMessage(next == APL_HMI_CHILD_LOCK_ON ? QStringLiteral("童锁开") : QStringLiteral("童锁关"));
        break;
    }
    case APL_HMI_KEY_SPEED:
        if (apl_core_fan_gear_get(&m_apl.core) == APL_FAN_GEAR_TURBO)
        {
            apl_core_fan_gear_set(&m_apl.core, APL_FAN_GEAR_2);
            setMessage(QStringLiteral("退出极速"));
        }
        else
        {
            apl_core_fan_gear_set(&m_apl.core, APL_FAN_GEAR_TURBO);
            setMessage(QStringLiteral("极速"));
        }
        break;
    case APL_HMI_KEY_LIGHT:
        setMessage(QStringLiteral("睡眠尚未规定"));
        break;
    case APL_HMI_KEY_POWER:
        setMessage(QStringLiteral("风干尚未规定"));
        break;
    case APL_HMI_KEY_PURIFY:
        setMessage(QStringLiteral("配网尚未规定"));
        break;
    case APL_HMI_KEY_HUMIDITY:
        setMessage(QStringLiteral("自清洁尚未规定"));
        break;
    default:
        break;
    }
}

void AplPlatform::handleTimer()
{
    if (apl_core_power_get(&m_apl.core) == APL_POWER_ON)
    {
        apl_core_power_off(&m_apl.core);
        setMessage(QStringLiteral("定时到，关机"));
    }
    else
    {
        apl_core_power_on(&m_apl.core);
        setMessage(QStringLiteral("定时到，开机"));
    }
}

void AplPlatform::applySwitch(bool *known, bool *on, bool next, const QString &label)
{
    if (!m_supplied)
    {
        return;
    }
    if (*known && *on == next)
    {
        return;
    }

    *known = true;
    *on = next;
    appendEvent(label + (next ? QStringLiteral("开") : QStringLiteral("关")));
    m_frameDirty = true;
}

QString AplPlatform::readDigits() const
{
    char raw[APL_HMI_DIGIT_COUNT];
    apl_hmi_digits_content_get(&m_apl.hmi, raw);
    QString text;
    for (char ch : raw)
    {
        text.append((ch >= 32 && ch < 127) ? QChar(ch) : QChar(QLatin1Char(' ')));
    }
    return text;
}

QString AplPlatform::gearNameFor(int gear) const
{
    switch (gear)
    {
    case APL_FAN_GEAR_SLEEP:
        return QStringLiteral("睡眠");
    case APL_FAN_GEAR_1:
        return QStringLiteral("一档");
    case APL_FAN_GEAR_2:
        return QStringLiteral("二档");
    case APL_FAN_GEAR_3:
        return QStringLiteral("三档");
    case APL_FAN_GEAR_AUTO:
        return QStringLiteral("自动");
    case APL_FAN_GEAR_TURBO:
        return QStringLiteral("极速");
    default:
        return QStringLiteral("二档");
    }
}

QString AplPlatform::switchText(bool known, bool on) const
{
    if (!m_supplied)
    {
        return QStringLiteral("停止（无供电）");
    }
    if (!known)
    {
        return QStringLiteral("未驱动");
    }

    return on ? QStringLiteral("开启") : QStringLiteral("关闭");
}

AplPlatform::Snapshot AplPlatform::capture() const
{
    Snapshot now;
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        now.keys[key] = apl_hmi_key_lamp_get(&m_apl.hmi, static_cast<enum apl_hmi_key_id>(key));
    }
    for (int index = 0; index < static_cast<int>(APL_HMI_WATER_LAMP_COUNT); ++index)
    {
        now.water[index] = apl_hmi_water_lamp_get(&m_apl.hmi, static_cast<uint8_t>(index + 1));
    }
    now.digits = readDigits();
    now.unit = static_cast<int>(apl_hmi_digits_unit_get(&m_apl.hmi));
    now.bright = apl_hmi_digits_brightness_get(&m_apl.hmi);
    return now;
}
