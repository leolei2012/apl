#include "apl_platform.h"

#include <QTime>

#include <climits>
#include <cstdio>
#include <cstring>

namespace {

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

bool AplPlatform::loadRuntime(const QString &path, QString *error, std::shared_ptr<QTemporaryDir> directory)
{
    if (!m_runtime.load(path, error, std::move(directory))) return false;
    if (!m_supplied) { m_supplied = true; emit suppliedChanged(); }
    reset();
    appendEvent(QStringLiteral("已应用产品配置，重新上电仿真"));
    flush();
    return true;
}

QString AplPlatform::runStateText() const
{
    if (!m_supplied) return QStringLiteral("未供电");
    switch (m_runState)
    {
    case APL_RUN_STANDBY: return QStringLiteral("待机");
    case APL_RUN_ON: return QStringLiteral("正常运行");
    case APL_RUN_DRY: return QStringLiteral("关机风干");
    case APL_RUN_FACTORY: return QStringLiteral("产测");
    default: return QStringLiteral("未知状态");
    }
}

QString AplPlatform::interactionHint() const
{
    if (!m_supplied) return QStringLiteral("供电已断开，请先接通供电。");
    if (m_paused) return QStringLiteral("仿真已暂停；继续仿真后，长按和倒计时才会随时间推进。");
    if (m_childLock) return QStringLiteral("童锁已开启；长按 Timer 3 秒解锁。");
    if (m_runState == APL_RUN_DRY)
        return QStringLiteral("关机风干 · 剩余 %1 秒 · 短按 Power 重新开机，长按 3 秒结束风干。").arg(m_drySeconds);
    if (m_powerupShow) return QStringLiteral("上电自检 2 秒；自检结束后短按 Power 开机。");
    if (m_runState == APL_RUN_STANDBY) return QStringLiteral("待机 · 短按 Power 开机。");
    if ((!m_panelLight || m_gear == APL_FAN_GEAR_SLEEP) && m_digitBrightness == 0)
        return QStringLiteral("显示已熄灭；首次按键只唤醒，再按执行操作。");
    if (!m_panelLight) return QStringLiteral("显示已临时唤醒；再次按键执行操作，3 秒无操作后熄灭。");
    if (m_runState == APL_RUN_FACTORY) return QStringLiteral("产测中 · 短按 Power 退出并恢复出厂设置。");
    return QStringLiteral("正常运行 · 鼠标点击或 Tab 选择按键，空格 / Enter 操作；持续按住 3 秒触发长按。");
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
    if(!m_supplied || rpm<0 || (m_fanKnown && m_fanOutputGear<0 && m_fanRpm==rpm)) return;
    m_fanKnown=true; m_fanRpm=rpm; m_fanOutputGear=-1;
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

void AplPlatform::queryFilter()
{
    if (!m_supplied) return;
    appendEvent(QStringLiteral("查询滤芯寿命"));
    m_runtime.api().apl_hmi_filter_query(&m_apl.hmi);
    stepOne();
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
    m_fanKnown = m_humidifierKnown = m_uvKnown = m_anionKnown = false;
    m_fanRpm = 0;
    m_fanOutputGear = -1;
    m_humidifierOn = m_uvOn = m_anionOn = false;
    m_panelLight = true;
    m_frameDirty = true;
    setMessage(QStringLiteral("重新初始化"));
    m_runtime.api().apl_init(&m_apl, &m_coreOps, this, &m_hmiOps, this);
    m_runtime.api().apl_link_bind(&m_apl);
    observe();
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

enum apl_water_level AplPlatform::getWaterLevel(void *ctx)
{
    return static_cast<enum apl_water_level>(static_cast<AplPlatform *>(ctx)->m_water);
}

uint16_t AplPlatform::getPm25(void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_pmReads += 1;
    self->m_frameDirty = true;
    return static_cast<uint16_t>(self->m_pm25);
}

uint8_t AplPlatform::getHumidity(void *ctx)
{
    return static_cast<uint8_t>(static_cast<AplPlatform *>(ctx)->m_humidity);
}

int16_t AplPlatform::getTemperature(void *ctx)
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

void AplPlatform::setFan(enum apl_switch state, enum apl_fan_gear gear, void *ctx)
{
    auto *self = static_cast<AplPlatform *>(ctx);
    if (state == APL_SWITCH_OFF) { self->fanSet(0); return; }
    if (self->m_fanKnown && self->m_fanOutputGear == static_cast<int>(gear)) return;
    self->m_fanKnown = true;
    self->m_fanRpm = 0;
    self->m_fanOutputGear = static_cast<int>(gear);
    self->appendEvent(QStringLiteral("风机输出: ") + self->gearNameFor(gear));
    self->pull();
    self->flush();
}

void AplPlatform::setPump(enum apl_switch state, void *ctx)
{
    static_cast<AplPlatform *>(ctx)->humidifierSet(state == APL_SWITCH_ON);
}

void AplPlatform::setUv(enum apl_switch state, void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    self->m_uvCalls += 1;
    self->m_frameDirty = true;
    self->applySwitch(&self->m_uvKnown, &self->m_uvOn, state == APL_SWITCH_ON, QStringLiteral("UV"));
}

void AplPlatform::setAnion(enum apl_switch state, void *ctx)
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
    self->m_runtime.api().apl_hmi_set_child_lock(&self->m_apl.hmi, APL_HMI_CHILD_LOCK_OFF);
}

void AplPlatform::getKeyState(struct apl_hmi_key_state *state, void *ctx)
{
    AplPlatform *self = static_cast<AplPlatform *>(ctx);
    state->purify = self->m_down[APL_HMI_KEY_PURIFY] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->humidity = self->m_down[APL_HMI_KEY_HUMIDITY] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->timer = self->m_down[APL_HMI_KEY_TIMER] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->light = self->m_down[APL_HMI_KEY_LIGHT] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->speed = self->m_down[APL_HMI_KEY_SPEED] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
    state->power = self->m_down[APL_HMI_KEY_POWER] ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
}

void AplPlatform::keyEvent(enum apl_hmi_key_id key, uint8_t isLong, void *ctx)
{
    auto *self = static_cast<AplPlatform *>(ctx);
    self->appendEvent(self->m_keyNames.at(key)
                      + (isLong ? QStringLiteral(" 长按") : QStringLiteral(" 短按")));
}

void AplPlatform::wireOps()
{
    std::memset(&m_coreOps, 0, sizeof(m_coreOps));
    std::memset(&m_hmiOps, 0, sizeof(m_hmiOps));
    m_coreOps.get_water_level = &AplPlatform::getWaterLevel;
    m_coreOps.get_pm25 = &AplPlatform::getPm25;
    m_coreOps.get_humidity = &AplPlatform::getHumidity;
    m_coreOps.get_temperature = &AplPlatform::getTemperature;
    m_coreOps.water_on_trigger = &AplPlatform::waterTrigger;
    m_coreOps.water_on_recover = &AplPlatform::waterRecover;
    m_coreOps.set_fan = &AplPlatform::setFan;
    m_coreOps.set_pump = &AplPlatform::setPump;
    m_coreOps.set_uv = &AplPlatform::setUv;
    m_coreOps.set_anion = &AplPlatform::setAnion;
    m_coreOps.on_power_off = &AplPlatform::powerOffEvent;
    m_hmiOps.get_key_state = &AplPlatform::getKeyState;
    m_hmiOps.key_event = &AplPlatform::keyEvent;
}

void AplPlatform::stepOne()
{
    if (!m_supplied || m_inStep)
    {
        return;
    }

    m_inStep = true;
    m_runtime.api().apl_tick(&m_apl);
    observe();
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

void AplPlatform::pull()
{
    bool dirty = false;
    const bool power_on = m_runtime.api().apl_core_get_power(&m_apl.core) == APL_POWER_ON;
    if (m_powerOn != power_on)
        appendEvent(power_on ? QStringLiteral("开机") : QStringLiteral("关机"));
    dirty = assign(&m_powerOn, power_on) || dirty;

    const int gear = static_cast<int>(m_runtime.api().apl_core_get_fan_gear(&m_apl.core));
    if (assign(&m_gear, gear))
    {
        m_gearName = gearNameFor(gear);
        appendEvent(QStringLiteral("档位: ") + m_gearName);
        dirty = true;
    }

    const uint32_t raw_seconds = m_runtime.api().apl_core_get_timer(&m_apl.core);
    const int seconds = raw_seconds > static_cast<uint32_t>(INT_MAX)
        ? INT_MAX
        : static_cast<int>(raw_seconds);
    if (m_timerSeconds != seconds && (seconds == 0 || seconds > m_timerSeconds))
        appendEvent(QStringLiteral("定时: %1 秒").arg(seconds));
    dirty = assign(&m_timerSeconds, seconds) || dirty;
    const bool locked = m_runtime.api().apl_hmi_get_child_lock(&m_apl.hmi) == APL_HMI_CHILD_LOCK_ON;
    if (m_childLock != locked)
        appendEvent(locked ? QStringLiteral("童锁开") : QStringLiteral("童锁关"));
    dirty = assign(&m_childLock, locked) || dirty;
    dirty = assign(&m_powerupShow, m_runtime.api().apl_hmi_get_powerup_lamp(&m_apl.hmi) != 0u) || dirty;

    bool lamps_changed = false;
    for (int key = 0; key < APL_HMI_KEY_COUNT; ++key)
    {
        const int lamp = m_runtime.api().apl_hmi_get_key_lamp(&m_apl.hmi, static_cast<enum apl_hmi_key_id>(key));
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
        const int lamp = m_runtime.api().apl_hmi_get_water_lamp(&m_apl.hmi, static_cast<uint8_t>(index + 1));
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

    QVariantList icons;
    for (int i = 0; i < APL_HMI_ICON_COUNT; ++i)
        icons.append(m_runtime.api().apl_hmi_get_icon(&m_apl.hmi, static_cast<enum apl_hmi_icon>(i)));
    if (m_iconLamps != icons && !m_iconLamps.isEmpty())
        appendEvent(QStringLiteral("状态图标变化"));
    dirty = assign(&m_iconLamps, icons) || dirty;
    dirty = assign(&m_waterColor, static_cast<int>(m_runtime.api().apl_hmi_get_water_color(&m_apl.hmi))) || dirty;
    const bool light = m_supplied && m_runtime.api().apl_hmi_get_light(&m_apl.hmi) != 0u;
    if (m_panelLight != light)
    {
        m_panelLight = light;
        appendEvent(light ? QStringLiteral("灯光开") : QStringLiteral("灯光关"));
        dirty = true;
    }

    dirty = assign(&m_digits, readDigits()) || dirty;
    dirty = assign(&m_digitBrightness, static_cast<int>(m_runtime.api().apl_hmi_get_digits_brightness(&m_apl.hmi))) || dirty;
    dirty = assign(&m_digitUnit, static_cast<int>(m_runtime.api().apl_hmi_get_digits_unit(&m_apl.hmi))) || dirty;

    uint8_t red = 0u;
    uint8_t green = 0u;
    uint8_t blue = 0u;
    m_runtime.api().apl_hmi_get_ambient_color(&m_apl.hmi, &red, &green, &blue);
    dirty = assign(&m_ambientBrightness, static_cast<int>(m_runtime.api().apl_hmi_get_ambient_brightness(&m_apl.hmi))) || dirty;
    dirty = assign(&m_ambientRed, static_cast<int>(red)) || dirty;
    dirty = assign(&m_ambientGreen, static_cast<int>(green)) || dirty;
    dirty = assign(&m_ambientBlue, static_cast<int>(blue)) || dirty;

    struct apl_link_snapshot state;
    m_runtime.api().apl_core_get_snapshot(&m_apl.core, &state);
    if (assign(&m_runState, static_cast<int>(state.run_state)))
    {
        const QString event = QStringLiteral("运行状态：") + runStateText();
        appendEvent(event);
        m_message = event;
        dirty = true;
    }
    dirty = assign(&m_drySeconds, static_cast<int>(qMin(state.dry_left_s, static_cast<uint32_t>(INT_MAX)))) || dirty;
    dirty = assign(&m_purifyEnabled, state.purify_enabled != 0u) || dirty;
    dirty = assign(&m_humidifyEnabled, state.humidify_enabled != 0u) || dirty;
    if (assign(&m_purifyOn, state.purify_on != 0u))
    {
        appendEvent(m_purifyOn ? QStringLiteral("净化开关开") : QStringLiteral("净化开关关"));
        dirty = true;
    }
    if (assign(&m_humidifyOn, state.humidify_on != 0u))
    {
        appendEvent(m_humidifyOn ? QStringLiteral("加湿开关开") : QStringLiteral("加湿开关关"));
        dirty = true;
    }

    QString fan = QStringLiteral("未驱动");
    if (!m_supplied)
    {
        fan = QStringLiteral("停止（无供电）");
    }
    else if (m_fanKnown)
    {
        fan = m_fanOutputGear >= 0 ? QStringLiteral("运行 / %1（输出档位）").arg(gearNameFor(m_fanOutputGear))
            : m_fanRpm == 0
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
    m_runtime.api().apl_hmi_get_digits_content(&m_apl.hmi, raw);
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
        now.keys[key] = m_runtime.api().apl_hmi_get_key_lamp(&m_apl.hmi, static_cast<enum apl_hmi_key_id>(key));
    }
    for (int index = 0; index < static_cast<int>(APL_HMI_WATER_LAMP_COUNT); ++index)
    {
        now.water[index] = m_runtime.api().apl_hmi_get_water_lamp(&m_apl.hmi, static_cast<uint8_t>(index + 1));
    }
    now.digits = readDigits();
    now.unit = static_cast<int>(m_runtime.api().apl_hmi_get_digits_unit(&m_apl.hmi));
    now.bright = m_runtime.api().apl_hmi_get_digits_brightness(&m_apl.hmi);
    return now;
}
