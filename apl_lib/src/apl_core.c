/**
 * @file    apl_core.c
 * @brief   主控对象：整机状态、风机、净化、加湿、水位、滤网、产测，以及快照。
 *
 * 规则来源是后端规格书第 5 节和《机型交互逻辑（Excel提炼）》。
 * 规格冲突处取的读法列在后端规格书第 6 节，并做成 apl_core_config.h 里的宏。
 */

#include "apl_core.h"
#include "apl_wifi_internal.h"
#include "apl_internal.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__)
#define APL_CORE_MAY_ALIAS __attribute__((__may_alias__))
#else
#define APL_CORE_MAY_ALIAS
#endif

APL_STATIC_ASSERT(APL_CORE_TICKS_PER_SECOND == 100u, "10 ms 一拍时，1 秒是 100 拍");
APL_STATIC_ASSERT(APL_CORE_VERSION_NUM == 0x000300u, "版本号与分段宏不一致");

/** 一小时的秒数。面板按小时设置定时。 */
#define APL_CORE_HOUR_S             3600u
/** 面板定时上限，小时。 */
#define APL_CORE_TIMER_MAX_H        24u
/** 滤网剩余百分比的上限。原表显示 1～99。 */
#define APL_CORE_FILTER_LEFT_MAX    99u
/** 恒湿目标的上下限与步进，百分比。 */
#define APL_CORE_HUMIDITY_MIN       40u
#define APL_CORE_HUMIDITY_MAX       80u
#define APL_CORE_HUMIDITY_STEP      5u
/** 恒湿风速表的分界，百分点。差值 < 10 一档，10～19 二档，≥ 20 三档。 */
#define APL_CORE_HUMIDITY_DIFF_2    10
#define APL_CORE_HUMIDITY_DIFF_3    20
#define APL_CORE_HAS_DRY (APL_COMPILE_DRY && APL_COMPILE_HUMIDIFY)
#define APL_CORE_HAS_AQ (APL_COMPILE_SENSOR_PM25 || APL_COMPILE_SENSOR_PM10 || APL_COMPILE_SENSOR_HCHO || APL_COMPILE_SENSOR_TVOC)
#define APL_CORE_HAS_RH (APL_COMPILE_HUMIDIFY && APL_COMPILE_SENSOR_HUMIDITY)
#define APL_CORE_HAS_SENSORS (APL_CORE_HAS_AQ || APL_CORE_HAS_RH)
#define APL_CORE_HAS_BEEP_EVENTS (APL_COMPILE_HUMIDIFY || APL_COMPILE_UV || APL_COMPILE_ANION || APL_COMPILE_TIMER || APL_COMPILE_FILTER || APL_COMPILE_FACTORY || APL_CORE_HAS_SENSORS)


#if !APL_COMPILE_TIMER
#define apl_core_timer_clear(state) APL_UNUSED_PARAM(state)
#define apl_core_timer_tick(self) APL_UNUSED_PARAM(self)
#endif
#if !APL_COMPILE_MEMORY
#define apl_core_memory_notify(self) APL_UNUSED_PARAM(self)
#endif
#if !APL_COMPILE_HUMIDIFY
#define apl_core_shortage_clear(state) APL_UNUSED_PARAM(state)
#define apl_core_water_sample(self) APL_UNUSED_PARAM(self)
#endif
#if !APL_CORE_HAS_SENSORS
#define apl_core_sensor_sample(state) APL_UNUSED_PARAM(state)
#endif
#if !APL_CORE_HAS_RH
#define apl_core_humidity_hold_update(state) APL_UNUSED_PARAM(state)
#endif

/** 对外对象里的真实状态。允许与占位对象别名，使用方不直接访问。 */
struct APL_CORE_MAY_ALIAS apl_core_state
{
    struct apl_profile profile;
    const struct apl_core_ops *ops;
    void *ctx;
#if APL_COMPILE_TIMER
    void (*on_timer)(struct apl_core *self, void *ctx) APL_CALL;
#endif
#if APL_COMPILE_TIMER
    void *timer_ctx;
#endif
    enum apl_power power;
    enum apl_run_state run_state;
    enum apl_fan_gear fan_gear;
#if APL_CORE_HAS_TURBO
    enum apl_fan_gear gear_before_turbo;
#endif
#if APL_COMPILE_SLEEP
    enum apl_fan_gear gear_before_sleep;
#endif
    enum apl_fan_gear fan_output;
    enum apl_switch fan;
#if APL_COMPILE_UV
    enum apl_switch uv;
#endif
#if APL_COMPILE_ANION
    enum apl_switch anion;
#endif
#if APL_COMPILE_HUMIDIFY
    enum apl_switch pump;
#endif
#if APL_COMPILE_HUMIDIFY
    enum apl_water_level water_level;
#endif
#if APL_COMPILE_HUMIDIFY
    enum apl_water_alert water_alert;
#endif
#if APL_CORE_HAS_AQ
    enum apl_aq_level aq_level;
#endif
#if APL_COMPILE_WIFI
    struct apl_wifi wifi;
#endif
    enum apl_beep beep;
#if APL_COMPILE_TIMER
    uint32_t timer_left_s;
#endif
#if APL_CORE_HAS_DRY
    uint32_t dry_left_s;
#endif
#if APL_COMPILE_HUMIDIFY
    uint32_t shortage_s;
#endif
#if APL_COMPILE_FACTORY
    uint32_t uptime_s;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
    uint32_t filter_purify_s;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
    uint32_t filter_humidify_s;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_FACTORY && APL_COMPILE_PURIFY
    uint32_t real_filter_purify_s;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_FACTORY && APL_COMPILE_HUMIDIFY
    uint32_t real_filter_humidify_s;
#endif
#if APL_COMPILE_FACTORY
    uint32_t cl_left_s;
#endif
#if APL_COMPILE_TIMER
    uint16_t timer_subtick;
#endif
    uint16_t second_subtick;
#if APL_CORE_HAS_SENSORS
    uint16_t sensor_warmup_ticks;
#endif
#if APL_COMPILE_SENSOR_PM25
    uint16_t pm25;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t water_valid;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t water_sample_ok; /**< 本拍水位是否有效；边沿起点仍保留最后有效值。 */
#endif
    uint8_t outputs_valid;
#if APL_COMPILE_PURIFY
    uint8_t purify_enabled;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t humidify_enabled;
#endif
    uint8_t purify_on;
#if APL_COMPILE_UV
    uint8_t uv_requested;
#endif
#if APL_COMPILE_ANION
    uint8_t anion_requested;
#endif
    uint8_t module_faults;
#if APL_CORE_HAS_SENSORS
    uint8_t sensors_ready;
#endif
#if APL_CORE_HAS_SENSORS
    uint8_t sensors_warming;
#endif
#if APL_CORE_HAS_SENSORS
    uint8_t sensor_faults;
#endif
#if APL_CORE_HAS_AQ
    uint8_t aq_ready;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t humidify_on;
#endif
#if APL_CORE_HAS_RH
    uint8_t humidity;
#endif
#if APL_CORE_HAS_RH
    uint8_t humidity_sampled;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t humidity_target;
#endif
#if APL_CORE_HAS_RH
    uint8_t humidity_hold;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t shortage;
#endif
#if APL_CORE_HAS_DRY
    uint8_t dry_needed; /**< 本次运行已提交过泵开启请求；不持久化。 */
#endif
#if APL_COMPILE_UV
    uint8_t uv_fault;
#endif
    uint8_t used;
#if APL_COMPILE_FACTORY
    uint8_t factory_pending;
#endif
#if APL_COMPILE_FACTORY
    uint8_t factory_done;
#endif
    uint8_t beep_seq;
#if APL_COMPILE_FILTER
    uint8_t filter_reset_seq;
#endif
#if APL_COMPILE_FILTER
    uint8_t filter_reset_forced;
#endif
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
#if APL_COMPILE_PURIFY
    uint8_t filter_purify_left;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t filter_humidify_left;
#endif
#endif
};

APL_STATIC_ASSERT(sizeof(struct apl_core_state) <= sizeof(struct apl_core),
               "APL_CORE_OPAQUE_SIZE 小于对象状态");
APL_STATIC_ASSERT(APL_ALIGNOF(struct apl_core_state) <= APL_ALIGNOF(struct apl_core),
               "占位对象的对齐小于对象状态");

static struct apl_core_state *apl_core_state_of(struct apl_core *self) APL_CALL
{
    return (struct apl_core_state *)(void *)self->storage.bytes;
}

static const struct apl_core_state *apl_core_state_of_const(const struct apl_core *self) APL_CALL
{
    return (const struct apl_core_state *)(const void *)self->storage.bytes;
}

#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
const struct apl_keymap *apl_core_keymap_target(const struct apl_core *self) APL_CALL
{
    return NULL == self ? NULL : &apl_core_state_of_const(self)->profile.keymap;
}
#endif

#if APL_COMPILE_PURIFY
static uint8_t apl_core_has_purify(const struct apl_core_state *state) APL_CALL
{
    return state->purify_enabled;
}
#else
#define apl_core_has_purify(state) 0u
#endif

#if APL_COMPILE_HUMIDIFY
static uint8_t apl_core_has_humidify(const struct apl_core_state *state) APL_CALL
{
    return state->humidify_enabled;
}
#else
#define apl_core_has_humidify(state) 0u
#endif

/** 定时被关机或重新设定打断时，旧回调不能再响。 */
#if APL_COMPILE_TIMER
static void apl_core_timer_clear(struct apl_core_state *state) APL_CALL
{
    state->timer_left_s = 0u;
    state->timer_subtick = 0u;
    state->on_timer = NULL;
    state->timer_ctx = NULL;
}
#endif

static uint8_t apl_core_gear_is_valid(enum apl_fan_gear gear) APL_CALL
{
    switch (gear)
    {
#if APL_COMPILE_SLEEP
    case APL_FAN_GEAR_SLEEP:
#endif
    case APL_FAN_GEAR_1:
    case APL_FAN_GEAR_2:
    case APL_FAN_GEAR_3:
    case APL_FAN_GEAR_AUTO:
#if APL_CORE_HAS_TURBO
    case APL_FAN_GEAR_TURBO:
#endif
        return 1u;
    default:
        return 0u;
    }
}

#if APL_COMPILE_HUMIDIFY
static uint8_t apl_core_water_is_valid(enum apl_water_level level) APL_CALL
{
    switch (level)
    {
    case APL_WATER_LEVEL_0:
    case APL_WATER_LEVEL_1:
    case APL_WATER_LEVEL_2:
    case APL_WATER_LEVEL_3:
    case APL_WATER_LEVEL_4:
        return 1u;
    default:
        return 0u;
    }
}
#endif

#if APL_CORE_HAS_BEEP_EVENTS
static void apl_core_beep(struct apl_core_state *state, enum apl_beep beep) APL_CALL
{
    state->beep = beep;
    state->beep_seq++;
}

/** 睡眠静音时不发提示音。原表：蜂鸣按睡眠静音规则执行。 */
#if APL_COMPILE_SLEEP
static void apl_core_beep_unless_sleep(struct apl_core_state *state, enum apl_beep beep) APL_CALL
{
    if ((APL_RUN_ON == state->run_state) && (APL_FAN_GEAR_SLEEP == state->fan_gear))
    {
        return;
    }

    apl_core_beep(state, beep);
}
#else
#define apl_core_beep_unless_sleep apl_core_beep
#endif
#endif

#if APL_COMPILE_MEMORY
static void apl_core_memory_fill(const struct apl_core_state *state,
                                 struct apl_core_memory *memory) APL_CALL
{
    memset(memory, 0, sizeof(*memory));
#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
#if APL_COMPILE_FACTORY
    memory->filter_purify_s = (APL_RUN_FACTORY == state->run_state) ? state->real_filter_purify_s : state->filter_purify_s;
#else
    memory->filter_purify_s = state->filter_purify_s;
#endif
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
#if APL_COMPILE_FACTORY
    memory->filter_humidify_s = (APL_RUN_FACTORY == state->run_state) ? state->real_filter_humidify_s : state->filter_humidify_s;
#else
    memory->filter_humidify_s = state->filter_humidify_s;
#endif
#endif
    memory->fan_gear = state->fan_gear;
    memory->running = (APL_RUN_ON == state->run_state) ? 1u : 0u;
    memory->used = state->used;
    memory->purify_on = state->purify_on;
#if APL_COMPILE_HUMIDIFY
    memory->humidify_on = state->humidify_on;
#endif
#if APL_COMPILE_FACTORY
    memory->factory_pending = state->factory_pending;
#endif
    memory->module_state_valid = 1u;
#if APL_COMPILE_UV
    memory->uv_requested = state->uv_requested;
#endif
#if APL_COMPILE_ANION
    memory->anion_requested = state->anion_requested;
#endif
}
#endif

#if APL_COMPILE_MEMORY
static void apl_core_memory_notify(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    struct apl_core_memory memory;

    state = apl_core_state_of(self);
    if ((NULL == state->ops) || (NULL == state->ops->memory_save))
    {
        return;
    }

    apl_core_memory_fill(state, &memory);
    state->ops->memory_save(&memory, state->ctx);
}
#endif

#if APL_COMPILE_FILTER
static uint8_t apl_core_filter_left(const struct apl_core_state *state, uint32_t used_s) APL_CALL
{
    uint32_t remaining;
    uint32_t life;
    uint32_t remainder = 0u;
    uint8_t quotient = 0u;
    uint8_t mask;

    life = state->profile.filter_life_h * APL_CORE_HOUR_S;
    if (used_s >= life)
    {
        return (uint8_t)APL_FILTER_EXPIRED;
    }

    remaining = life - used_s;
    /** Binary long multiplication/division of remaining * 99 / life.
     * Seven bits instead of 99 additions; keep remainder < life without
     * overflowing uint32_t, including the maximum 1,000,000-hour lifetime. */
    for (mask = 64u; mask != 0u; mask >>= 1)
    {
        quotient = (uint8_t)(quotient << 1);
        if (remainder >= (life - remainder))
        {
            remainder -= life - remainder;
            quotient++;
        }
        else
        {
            remainder += remainder;
        }
        if (0u != (APL_CORE_FILTER_LEFT_MAX & mask))
        {
            if (remainder >= used_s)
            {
                remainder -= used_s;
                quotient++;
            }
            else
            {
                remainder += remaining;
            }
        }
    }
    if (remainder != 0u)
    {
        quotient++;
    }
    return quotient;
}
#endif

/** Refresh only when counters/configuration change, before any host callback. */
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
static void apl_core_filter_cache_update(struct apl_core_state *state) APL_CALL
{
#if APL_COMPILE_PURIFY
    state->filter_purify_left = (0u != apl_core_has_purify(state))
        ? apl_core_filter_left(state, state->filter_purify_s) : 0u;
#endif
#if APL_COMPILE_HUMIDIFY
    state->filter_humidify_left = (0u != apl_core_has_humidify(state))
        ? apl_core_filter_left(state, state->filter_humidify_s) : 0u;
#endif
}
#endif

#if (APL_COMPILE_SENSOR_PM25 || APL_COMPILE_SENSOR_PM10 || APL_COMPILE_SENSOR_HCHO || APL_COMPILE_SENSOR_TVOC)
static enum apl_aq_level apl_core_aq_grade(uint16_t value,
                                           uint16_t excellent,
                                           uint16_t good,
                                           uint16_t moderate) APL_CALL
{
    if (value <= excellent)
    {
        return APL_AQ_EXCELLENT;
    }

    if (value <= good)
    {
        return APL_AQ_GOOD;
    }

    if (value <= moderate)
    {
        return APL_AQ_MODERATE;
    }

    return APL_AQ_SEVERE;
}

static enum apl_aq_level apl_core_aq_worse(enum apl_aq_level a, enum apl_aq_level b) APL_CALL
{
    return (b > a) ? b : a;
}

#endif

/**
 * 原表多传感器分级。区间端点归较好的一档（待确认：TVOC 0.3、0.5 两档重叠）。
 * 甲醛没有「良」，0.050 以上直接是中度污染。
 */
#if (APL_COMPILE_SENSOR_PM25 || APL_COMPILE_SENSOR_PM10 || APL_COMPILE_SENSOR_HCHO || APL_COMPILE_SENSOR_TVOC || APL_COMPILE_SENSOR_HUMIDITY)
static enum apl_sensor_status apl_core_sensor_state(struct apl_core_state *state,
                                                     enum apl_sensor sensor,
                                                     uint8_t available) APL_CALL
{
    enum apl_sensor_status status;
    uint8_t mask;

    if ((0u == available) || (0u == (state->profile.sensors & (uint8_t)(1u << (unsigned)sensor))))
    {
        return APL_SENSOR_ABSENT;
    }
    status = APL_SENSOR_VALID;
    if ((NULL != state->ops) && (NULL != state->ops->get_sensor_status))
    {
        status = state->ops->get_sensor_status(sensor, state->ctx);
        if ((unsigned)status > APL_SENSOR_FAULT)
        {
            status = APL_SENSOR_FAULT;
        }
    }
    if ((APL_SENSOR_VALID == status) && (0u != state->sensor_warmup_ticks)
        && ((APL_SENSOR_PM25 == sensor) || (APL_SENSOR_HUMIDITY == sensor)))
    {
        status = APL_SENSOR_WARMUP;
    }
    mask = (uint8_t)(1u << (unsigned)sensor);
    if (APL_SENSOR_VALID == status)
    {
        state->sensors_ready |= mask;
    }
    else if (APL_SENSOR_WARMUP == status)
    {
        state->sensors_warming |= mask;
    }
    else if (APL_SENSOR_FAULT == status)
    {
        state->sensor_faults |= mask;
    }
    else
    {
        /* 未装配不参与空气质量判定，也不伪造有效值。 */
    }
    return status;
}
#endif

#if APL_CORE_HAS_SENSORS
static void apl_core_sensor_sample(struct apl_core_state *state) APL_CALL
{
#if APL_CORE_HAS_AQ
    enum apl_aq_level level;
    uint8_t aq_inputs;
#endif
    uint8_t previous_faults;

    previous_faults = state->sensor_faults;
#if APL_CORE_HAS_SENSORS
    state->sensors_ready = 0u;
#endif
#if APL_CORE_HAS_SENSORS
    state->sensors_warming = 0u;
#endif
#if APL_CORE_HAS_SENSORS
    state->sensor_faults = 0u;
#endif
#if APL_CORE_HAS_AQ
    state->aq_ready = 0u;
#endif
#if APL_CORE_HAS_RH
    state->humidity_sampled = 0u;
#endif
    if (NULL == state->ops)
    {
        return;
    }
#if APL_CORE_HAS_AQ
    if (0u != apl_core_has_purify(state))
    {
        level = APL_AQ_EXCELLENT;
#if APL_COMPILE_SENSOR_PM25
        if (APL_SENSOR_VALID == apl_core_sensor_state(state, APL_SENSOR_PM25,
                                                       (uint8_t)(NULL != state->ops->get_pm25)))
        {
            state->pm25 = state->ops->get_pm25(state->ctx);
            level = apl_core_aq_worse(level, apl_core_aq_grade(state->pm25, 35u, 75u, 150u));
        }
#endif
#if APL_COMPILE_SENSOR_PM10
        if (APL_SENSOR_VALID == apl_core_sensor_state(state, APL_SENSOR_PM10,
                                                       (uint8_t)(NULL != state->ops->get_pm10)))
        {
            level = apl_core_aq_worse(level, apl_core_aq_grade(state->ops->get_pm10(state->ctx), 40u, 70u, 150u));
        }
#endif
#if APL_COMPILE_SENSOR_HCHO
        if (APL_SENSOR_VALID == apl_core_sensor_state(state, APL_SENSOR_HCHO,
                                                       (uint8_t)(NULL != state->ops->get_hcho)))
        {
            level = apl_core_aq_worse(level, apl_core_aq_grade(state->ops->get_hcho(state->ctx), 50u, 50u, 80u));
        }
#endif
#if APL_COMPILE_SENSOR_TVOC
        if (APL_SENSOR_VALID == apl_core_sensor_state(state, APL_SENSOR_TVOC,
                                                       (uint8_t)(NULL != state->ops->get_tvoc)))
        {
            level = apl_core_aq_worse(level, apl_core_aq_grade(state->ops->get_tvoc(state->ctx), 300u, 500u, 1000u));
        }
#endif
        state->aq_level = level;
    }
#endif
#if APL_COMPILE_SENSOR_HUMIDITY
    if ((0u != apl_core_has_humidify(state))
        && (APL_SENSOR_VALID == apl_core_sensor_state(state, APL_SENSOR_HUMIDITY,
                                                       (uint8_t)(NULL != state->ops->get_humidity))))
    {
        state->humidity = state->ops->get_humidity(state->ctx);
        if (state->humidity <= 100u)
        {
#if APL_CORE_HAS_RH
            state->humidity_sampled = 1u;
#endif
        }
        else
        {
            state->sensors_ready &= (uint8_t)~APL_SENSOR_HUMIDITY_MASK;
            state->sensor_faults |= APL_SENSOR_HUMIDITY_MASK;
        }
    }
#endif
#if APL_CORE_HAS_AQ
    aq_inputs = (uint8_t)((state->sensors_ready | state->sensors_warming | state->sensor_faults) & 15u);
#endif
#if APL_CORE_HAS_AQ
    state->aq_ready = ((0u != aq_inputs) && (aq_inputs == (state->sensors_ready & 15u))) ? 1u : 0u;
#endif
    if (0u != (state->sensor_faults & (uint8_t)~previous_faults))
    {
        apl_core_beep_unless_sleep(state, APL_BEEP_SHORT);
    }
}
#endif

#if APL_CORE_HAS_SENSORS
static enum apl_fan_gear apl_core_preprocess_gear(const struct apl_core_state *state) APL_CALL
{
    return (enum apl_fan_gear)((state->profile.default_fan_gear <= 3u)
        ? state->profile.default_fan_gear : state->profile.sensor_fallback_gear);
}
#endif

/** 恒湿风速表。差值 = 设定 − 实际；达到或超过设定仍是一档。 */
#if APL_CORE_HAS_RH
static enum apl_fan_gear apl_core_humidity_gear(const struct apl_core_state *state) APL_CALL
{
    int16_t diff;

    diff = (int16_t)((int16_t)state->humidity_target - (int16_t)state->humidity);
    if (diff < APL_CORE_HUMIDITY_DIFF_2)
    {
        return APL_FAN_GEAR_1;
    }

    if (diff < APL_CORE_HUMIDITY_DIFF_3)
    {
        return APL_FAN_GEAR_2;
    }

    return APL_FAN_GEAR_3;
}
#endif

/** 纯净化按标准化表输出睡眠/一/二/三档；带加湿能力的映射暂保留原规则。 */
#if APL_CORE_HAS_AQ
static enum apl_fan_gear apl_core_aq_gear(const struct apl_core_state *state) APL_CALL
{
    switch (state->aq_level)
    {
    case APL_AQ_EXCELLENT:
#if APL_COMPILE_SLEEP
        return (0u != apl_core_has_humidify(state)) ? APL_FAN_GEAR_1 : APL_FAN_GEAR_SLEEP;
#else
        return APL_FAN_GEAR_1;
#endif
    case APL_AQ_GOOD:
        return (0u != apl_core_has_humidify(state)) ? APL_FAN_GEAR_2 : APL_FAN_GEAR_1;
    case APL_AQ_MODERATE:
        return (0u != apl_core_has_humidify(state)) ? APL_FAN_GEAR_3 : APL_FAN_GEAR_2;
    default:
        return APL_FAN_GEAR_3;
    }
}
#endif

#if APL_CORE_HAS_RH
static uint8_t apl_core_constant_humidity(const struct apl_core_state *state) APL_CALL
{
    return ((0u != apl_core_has_humidify(state))
            && (0u != state->humidify_on)
            && (0u != (state->sensors_ready & APL_SENSOR_HUMIDITY_MASK))
            && (APL_HUMIDITY_TARGET_OFF != state->humidity_target)) ? 1u : 0u;
}
#endif

/** 加湿器交互逻辑 C9:E9。已读取不等于传感器有效，后者仍由宿主保证。 */
#if APL_CORE_HAS_RH
static enum apl_fan_gear apl_core_humidify_auto_gear(const struct apl_core_state *state) APL_CALL
{
    /* 没有样本、回调或整数百分比时保留旧输出，不在此定义故障策略。 */
    if ((0u == state->humidity_sampled) || (NULL == state->ops)
        || (NULL == state->ops->get_humidity) || (state->humidity > 100u))
    {
        return (enum apl_fan_gear)state->profile.sensor_fallback_gear;
    }

    if (state->humidity <= 30u)
    {
        return APL_FAN_GEAR_3;
    }

    return (state->humidity <= 50u) ? APL_FAN_GEAR_2 : APL_FAN_GEAR_1;
}
#endif

static enum apl_fan_gear apl_core_fan_output_compute(const struct apl_core_state *state) APL_CALL
{
#if APL_CORE_HAS_RH
    enum apl_fan_gear humidity_gear;
#endif
#if APL_CORE_HAS_AQ && APL_CORE_HAS_RH
    enum apl_fan_gear gear;
#endif
#if APL_CORE_HAS_DRY
    if (APL_RUN_DRY == state->run_state)
    {
#if APL_COMPILE_SLEEP
        if ((0u == apl_core_has_purify(state)) && (APL_FAN_GEAR_SLEEP == state->fan_gear))
        { return APL_FAN_GEAR_SLEEP; }
#endif
        return (enum apl_fan_gear)state->profile.dry_fan_gear;
    }
#endif
#if APL_CORE_HAS_TURBO
    if (APL_FAN_GEAR_TURBO == state->fan_gear) { return state->fan_gear; }
#endif
#if APL_COMPILE_SLEEP
    if (APL_FAN_GEAR_SLEEP == state->fan_gear) { return state->fan_gear; }
#endif
    if (APL_FAN_GEAR_AUTO == state->fan_gear)
    {
#if APL_CORE_HAS_AQ
        if ((0u != apl_core_has_purify(state)) && (0u == state->aq_ready))
        { return (0u != (state->sensors_warming & 15u)) ? apl_core_preprocess_gear(state)
                    : (enum apl_fan_gear)state->profile.sensor_fallback_gear; }
#else
        if (0u != apl_core_has_purify(state)) { return (enum apl_fan_gear)state->profile.sensor_fallback_gear; }
#endif
#if APL_CORE_HAS_RH
        if ((0u == apl_core_has_purify(state)) && (0u != apl_core_has_humidify(state))
            && (0u == (state->sensors_ready & APL_SENSOR_HUMIDITY_MASK)))
        { return (0u != (state->sensors_warming & APL_SENSOR_HUMIDITY_MASK)) ? apl_core_preprocess_gear(state)
                    : (enum apl_fan_gear)state->profile.sensor_fallback_gear; }
#endif
    }
#if APL_CORE_HAS_RH
    if (0u != apl_core_constant_humidity(state))
    {
        humidity_gear = apl_core_humidity_gear(state);
#if APL_CORE_HAS_AQ
        if ((APL_FAN_GEAR_AUTO == state->fan_gear) && (0u != apl_core_has_purify(state)))
        { gear = apl_core_aq_gear(state); return (gear > humidity_gear) ? gear : humidity_gear; }
#endif
        return humidity_gear;
    }
#endif
    if (APL_FAN_GEAR_AUTO == state->fan_gear)
    {
#if APL_CORE_HAS_AQ
        if (0u != apl_core_has_purify(state)) { return apl_core_aq_gear(state); }
#endif
#if APL_CORE_HAS_RH
        return apl_core_humidify_auto_gear(state);
#else
        return (enum apl_fan_gear)state->profile.sensor_fallback_gear;
#endif
    }
    return state->fan_gear;
}

/** 达到目标停泵，低于目标一个回差再开泵。不恒湿时一直加湿。 */
#if APL_CORE_HAS_RH
static void apl_core_humidity_hold_update(struct apl_core_state *state) APL_CALL
{
    if (0u == apl_core_constant_humidity(state))
    {
        state->humidity_hold = 0u;
        return;
    }

    if (state->humidity >= state->humidity_target)
    {
        state->humidity_hold = 1u;
    }
    else if (((uint16_t)state->humidity + state->profile.humidity_hysteresis)
             <= state->humidity_target)
    {
        state->humidity_hold = 0u;
    }
}
#endif

#if (APL_COMPILE_UV || APL_COMPILE_ANION || APL_COMPILE_WIFI)
static uint8_t apl_core_module_fault(struct apl_core_state *state,
                                      enum apl_module module) APL_CALL
{
    uint8_t fault;
    uint8_t mask;

    mask = (uint8_t)(1u << (unsigned)module);
    if (0u == (state->profile.modules & mask))
    {
        return 0u;
    }
    fault = 0u;
    if ((NULL != state->ops) && (NULL != state->ops->get_module_fault))
    {
        fault = (0u != state->ops->get_module_fault(module, state->ctx)) ? 1u : 0u;
    }
    if (0u != fault)
    {
        state->module_faults |= mask;
    }
    else
    {
        state->module_faults &= (uint8_t)~mask;
    }
    return fault;
}
#endif

/** 仲裁后的执行器目标。仅在本拍栈上存在，不改变对象或公开接口布局。 */
struct apl_core_output_targets
{
    enum apl_switch fan;
    enum apl_fan_gear fan_gear;
#if APL_COMPILE_HUMIDIFY
    enum apl_switch pump;
#endif
#if APL_COMPILE_UV
    enum apl_switch uv;
#endif
#if APL_COMPILE_ANION
    enum apl_switch anion;
#endif
};

/** 按原顺序提交变化；每次回调前只更新对应执行器，保留回调内可见快照。 */
static void apl_core_outputs_apply(struct apl_core *self,
                                   const struct apl_core_ops *ops,
                                   const struct apl_core_output_targets *targets) APL_CALL
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    if ((0u == state->outputs_valid) || (targets->fan != state->fan)
        || (targets->fan_gear != state->fan_output))
    {
        state->fan = targets->fan;
        state->fan_output = targets->fan_gear;
        /* 第一拍风机本来就停着，不必再发一次「关」。 */
        if ((NULL != ops) && (NULL != ops->set_fan)
            && ((0u != state->outputs_valid) || (APL_SWITCH_ON == targets->fan)))
        {
            ops->set_fan(targets->fan, targets->fan_gear, state->ctx);
        }
    }

#if APL_COMPILE_HUMIDIFY
    if (targets->pump != state->pump)
    {
#if APL_COMPILE_HUMIDIFY
        state->pump = targets->pump;
#endif
        if (APL_SWITCH_ON == targets->pump)
        {
            /* E17/E21：依据提交的负载状态，不以功能启用或模式代替运行历史。
             * 硬件是否成功执行仍由port负责；本接口没有执行确认反馈。 */
#if APL_CORE_HAS_DRY
            state->dry_needed = 1u;
#endif
        }
        if ((NULL != ops) && (NULL != ops->set_pump))
        {
            ops->set_pump(targets->pump, state->ctx);
        }
    }
#endif

#if APL_COMPILE_UV
    if (targets->uv != state->uv)
    {
#if APL_COMPILE_UV
        state->uv = targets->uv;
#endif
        if ((NULL != ops) && (NULL != ops->set_uv))
        {
            ops->set_uv(targets->uv, state->ctx);
        }
    }
#endif

#if APL_COMPILE_ANION
    if (targets->anion != state->anion)
    {
#if APL_COMPILE_ANION
        state->anion = targets->anion;
#endif
        if ((NULL != ops) && (NULL != ops->set_anion))
        {
            ops->set_anion(targets->anion, state->ctx);
        }
    }
#endif

    state->outputs_valid = 1u;
}

/** 保留原有策略及副作用的计算时机，再交由统一执行器边界提交。 */
static void apl_core_outputs_update(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    struct apl_core_output_targets targets;
    uint8_t running;
#if APL_COMPILE_UV
    uint8_t interlock;
    uint8_t uv_fault;
#endif
#if APL_COMPILE_ANION
    uint8_t anion_fault;
#endif
    const struct apl_core_ops *ops;
    state = apl_core_state_of(self);
    ops = state->ops;
    running = (APL_RUN_ON == state->run_state) ? 1u : 0u;
    targets.fan = ((0u != running)
#if APL_CORE_HAS_DRY
                   || (APL_RUN_DRY == state->run_state)
#endif
                  ) ? APL_SWITCH_ON : APL_SWITCH_OFF;
    targets.fan_gear = (APL_SWITCH_ON == targets.fan) ? apl_core_fan_output_compute(state) : state->fan_output;
#if APL_COMPILE_HUMIDIFY
    apl_core_humidity_hold_update(state);
    targets.pump = ((0u != running) && (0u != apl_core_has_humidify(state))
                    && (0u != state->humidify_on) && (0u != state->water_sample_ok)
                    && (APL_WATER_LEVEL_0 != state->water_level)
#if APL_CORE_HAS_TURBO
                    && (APL_FAN_GEAR_TURBO != state->fan_gear)
#endif
                    && (0u == state->shortage)
#if APL_CORE_HAS_RH
                    && (0u == state->humidity_hold)
                    && ((APL_HUMIDITY_TARGET_OFF == state->humidity_target)
                        || (0u != (state->sensors_ready & APL_SENSOR_HUMIDITY_MASK)))
#endif
                   ) ? APL_SWITCH_ON : APL_SWITCH_OFF;
#endif
#if APL_COMPILE_UV
    interlock = 0u;
    if ((0u != (state->profile.modules & APL_PROFILE_MODULE_UV))
        && (NULL != ops) && (NULL != ops->get_uv_interlock))
    { interlock = (1u == ops->get_uv_interlock(state->ctx)) ? 1u : 0u; }
    uv_fault = apl_core_module_fault(state, APL_MODULE_UV);
#endif
#if APL_COMPILE_ANION
    anion_fault = apl_core_module_fault(state, APL_MODULE_ANION);
#endif
#if APL_COMPILE_WIFI
    if (0u != apl_core_module_fault(state, APL_MODULE_WIFI)) { apl_wifi_reset(&state->wifi); }
#endif
#if APL_COMPILE_UV
    if (((APL_SWITCH_ON == state->uv) && (0u == interlock))
        || ((0u != running) && (0u != uv_fault) && (0u != (state->profile.modules & APL_PROFILE_MODULE_UV))
                  && (0u != state->uv_requested) && (0u == state->uv_fault)))
    { state->uv_fault = 1u; apl_core_beep(state, APL_BEEP_UV_ALARM); }
    targets.uv = ((0u != running) && (0u != (state->profile.modules & APL_PROFILE_MODULE_UV))
                  && (0u != state->uv_requested) && (0u != interlock)
                  && (0u == uv_fault) && (0u == state->uv_fault)) ? APL_SWITCH_ON : APL_SWITCH_OFF;
#endif
#if APL_COMPILE_ANION
    targets.anion = ((0u != running) && (0u != (state->profile.modules & APL_PROFILE_MODULE_ANION))
                     && (0u != state->anion_requested) && (0u == anion_fault)) ? APL_SWITCH_ON : APL_SWITCH_OFF;
#endif

    apl_core_outputs_apply(self, ops, &targets);
}

#if APL_COMPILE_HUMIDIFY
static void apl_core_shortage_start(struct apl_core_state *state) APL_CALL
{
    if ((0u != state->shortage) || (0u == apl_core_has_humidify(state)))
    {
        return;
    }

    state->shortage = 1u;
    state->shortage_s = 0u;
    state->water_alert = (0u != apl_core_has_purify(state)) ? APL_WATER_ALERT_STEADY
                                                         : APL_WATER_ALERT_BLINK;
    apl_core_beep_unless_sleep(state, APL_BEEP_WATER_SHORTAGE);
}
#endif

#if APL_COMPILE_HUMIDIFY
static void apl_core_shortage_clear(struct apl_core_state *state) APL_CALL
{
    state->shortage = 0u;
    state->shortage_s = 0u;
    state->water_alert = APL_WATER_ALERT_NONE;
}
#endif

/**
 * 第一次读数只建立边沿起点。没有上一拍时，无从判断是下降还是上升。
 * 非法段数丢掉，避免一次坏读数把起点改掉。
 */
#if APL_COMPILE_HUMIDIFY
static void apl_core_water_sample(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    enum apl_water_level level;
    enum apl_water_level previous;

    state = apl_core_state_of(self);
    state->water_sample_ok = 0u;
    if ((NULL == state->ops) || (NULL == state->ops->get_water_level))
    {
        return;
    }

    level = state->ops->get_water_level(state->ctx);
    if (0u == apl_core_water_is_valid(level))
    {
        return;
    }
    state->water_sample_ok = 1u;

    if (0u == state->water_valid)
    {
        state->water_level = level;
        state->water_valid = 1u;
        if ((APL_RUN_ON == state->run_state) && (APL_WATER_LEVEL_0 == level))
        {
            /* 恢复运行后的首个零水位也必须保护；不是伪造下降沿回调。 */
            apl_core_shortage_start(state);
        }
        return;
    }

    previous = state->water_level;
    state->water_level = level;

    if ((previous >= APL_WATER_LEVEL_1) && (APL_WATER_LEVEL_0 == level))
    {
        if (APL_RUN_ON == state->run_state)
        {
            apl_core_shortage_start(state);
        }

        if (NULL != state->ops->water_on_trigger)
        {
            state->ops->water_on_trigger(self, state->ctx);
        }
    }
    else if ((APL_WATER_LEVEL_0 == previous) && (level >= APL_WATER_LEVEL_1))
    {
        apl_core_shortage_clear(state);
        if (NULL != state->ops->water_on_recover)
        {
            state->ops->water_on_recover(self, state->ctx);
        }
    }
    else
    {
        /* 只在 1 到 4 之间变化。 */
    }

    /* 四段全亮视为加满。只有段数时 85% 与 100% 无法区分，按水满提示。 */
    if ((APL_WATER_LEVEL_4 == level) && (previous < APL_WATER_LEVEL_4)
        && (0u != apl_core_has_humidify(state)))
    {
        apl_core_beep_unless_sleep(state, APL_BEEP_WATER_FULL);
    }
}
#endif

/** 关机期间也计时，这样到时回调可以负责开机。 */
#if APL_COMPILE_TIMER
static void apl_core_timer_tick(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    void (*on_timer)(struct apl_core *timer_self, void *timer_ctx) APL_CALL;
    void *timer_ctx;

    state = apl_core_state_of(self);
    if (0u == state->timer_left_s)
    {
        return;
    }

    state->timer_subtick++;
    if (state->timer_subtick < APL_CORE_TICKS_PER_SECOND)
    {
        return;
    }

    state->timer_subtick = 0u;
    state->timer_left_s--;
    if (0u != state->timer_left_s)
    {
        return;
    }

    on_timer = state->on_timer;
    timer_ctx = state->timer_ctx;
    state->on_timer = NULL;
    state->timer_ctx = NULL;
    if (NULL != on_timer)
    {
        on_timer(self, timer_ctx);
    }
}
#endif

#if APL_CORE_HAS_DRY
static void apl_core_dry_start(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    state->run_state = APL_RUN_DRY;
#if APL_CORE_HAS_DRY
    state->dry_left_s = state->profile.dry_s;
#endif
    apl_core_timer_clear(state);
    apl_core_shortage_clear(state);
#if APL_COMPILE_WIFI
    apl_wifi_cancel_pairing(&state->wifi);
#endif

    apl_core_memory_notify(self);
}
#endif

/** 恢复出厂设置。真实滤网寿命不清零，产测注入值丢掉。 */
#if APL_COMPILE_FACTORY
static void apl_core_factory_restore(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    if (APL_RUN_FACTORY == state->run_state)
    {
#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
        state->filter_purify_s = state->real_filter_purify_s;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
        state->filter_humidify_s = state->real_filter_humidify_s;
#endif
    }

    state->run_state = APL_RUN_STANDBY;
#if APL_CORE_HAS_DRY
    state->dry_needed = 0u;
#endif
    state->power = APL_POWER_OFF;
    state->fan_gear = (enum apl_fan_gear)state->profile.default_fan_gear;
#if APL_CORE_HAS_TURBO
    state->gear_before_turbo = state->fan_gear;
#endif
#if APL_COMPILE_SLEEP
    state->gear_before_sleep = state->fan_gear;
#endif
#if APL_COMPILE_WIFI
    apl_wifi_reset(&state->wifi);
#endif
    state->purify_on = 0u;
#if APL_COMPILE_UV
    state->uv_requested = 0u;
#endif
#if APL_COMPILE_ANION
    state->anion_requested = 0u;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidify_on = 1u;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidity_target = APL_HUMIDITY_TARGET_OFF;
#endif
    state->used = 0u;
    state->factory_pending = 0u;
#if APL_COMPILE_FACTORY
    state->factory_done = 1u;
#endif
#if APL_COMPILE_FACTORY
    state->cl_left_s = APL_CORE_FACTORY_CL_S;
#endif
#if APL_COMPILE_UV
    state->uv_fault = 0u;
#endif
    apl_core_timer_clear(state);
    apl_core_shortage_clear(state);
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(state);
#endif
    apl_core_memory_notify(self);
    if ((NULL != state->ops) && (NULL != state->ops->on_power_off))
    {
        state->ops->on_power_off(self, state->ctx);
    }
}
#endif

static void apl_core_second(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    state = apl_core_state_of(self);
#if APL_COMPILE_FACTORY
    if (state->uptime_s < UINT32_MAX) { state->uptime_s++; }
    if (0u != state->cl_left_s) { state->cl_left_s--; if (0u == state->cl_left_s) { state->factory_done = 0u; } }
#endif
#if APL_COMPILE_WIFI
    apl_wifi_second(&state->wifi);
#endif
    if ((APL_RUN_ON == state->run_state)
#if APL_COMPILE_FACTORY
        || (APL_RUN_FACTORY == state->run_state)
#endif
       )
    {
        apl_core_sensor_sample(state);
#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
        if ((0u != apl_core_has_purify(state)) && (state->filter_purify_s < UINT32_MAX)) { state->filter_purify_s++; }
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
        if ((0u != apl_core_has_humidify(state)) && (state->filter_humidify_s < UINT32_MAX)) { state->filter_humidify_s++; }
#endif
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
        apl_core_filter_cache_update(state);
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_MEMORY
        if ((APL_RUN_ON == state->run_state) && (0
#if APL_COMPILE_PURIFY
            || ((0u != apl_core_has_purify(state)) && (0u == (state->filter_purify_s % APL_CORE_HOUR_S)))
#endif
#if APL_COMPILE_HUMIDIFY
            || ((0u != apl_core_has_humidify(state)) && (0u == (state->filter_humidify_s % APL_CORE_HOUR_S)))
#endif
           )) { apl_core_memory_notify(self); }
#endif
    }
#if APL_COMPILE_HUMIDIFY
    if ((APL_RUN_ON == state->run_state) && (0u != state->shortage))
    {
        if (state->shortage_s < UINT32_MAX) { state->shortage_s++; }
        if (state->shortage_s >= state->profile.water_blink_s) { state->water_alert = APL_WATER_ALERT_STEADY; }
        if ((0u == apl_core_has_purify(state)) && (state->shortage_s >= state->profile.shortage_run_s))
        {
#if APL_CORE_HAS_DRY
            apl_core_dry_start(self);
#else
            apl_core_power_off(self);
#endif
            state->water_alert = APL_WATER_ALERT_STEADY;
        }
    }
#endif
#if APL_CORE_HAS_DRY
    if (APL_RUN_DRY == state->run_state)
    { if (0u != state->dry_left_s) { state->dry_left_s--; } if (0u == state->dry_left_s) { apl_core_power_off(self); } }
#endif
}

static enum apl_link_result apl_core_cmd_power(struct apl_core *self) APL_CALL;

/** 面板定时到期执行正常关机；保留通用用户定时回调的既有契约。 */
#if APL_COMPILE_TIMER
static void apl_core_panel_timer_expire(struct apl_core *self, void *ctx) APL_CALL
{
    APL_UNUSED_PARAM(ctx);
    if (APL_RUN_ON == apl_core_get_run_state(self))
    {
        apl_core_beep_unless_sleep(apl_core_state_of(self), APL_BEEP_SHORT);
        (void)apl_core_cmd_power(self);
    }
}
#endif

void apl_profile_defaults(struct apl_profile *profile) APL_CALL
{
    if (NULL == profile) { return; }
    memset(profile, 0, sizeof(*profile));
    profile->version = APL_PROFILE_VERSION;
#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
    profile->keymap.short_action[0] = APL_KEY_ACTION_PURIFY_TOGGLE;
    profile->keymap.short_action[1] = APL_KEY_ACTION_HUMIDITY_CYCLE;
    profile->keymap.short_action[2] = APL_KEY_ACTION_TIMER_EDIT;
    profile->keymap.short_action[3] = APL_KEY_ACTION_LIGHT_TOGGLE;
    profile->keymap.short_action[4] = APL_KEY_ACTION_FAN_CYCLE;
    profile->keymap.short_action[5] = APL_KEY_ACTION_POWER;
    profile->keymap.short_action[6] = APL_KEY_ACTION_FILTER_QUERY;
    profile->keymap.long_action[0] = APL_KEY_ACTION_WIFI_PAIR;
    profile->keymap.long_action[1] = APL_KEY_ACTION_HUMIDITY_EDIT;
    profile->keymap.long_action[2] = APL_KEY_ACTION_CHILD_LOCK_TOGGLE;
    profile->keymap.long_action[4] = APL_KEY_ACTION_TURBO_TOGGLE;
    profile->keymap.long_action[5] = APL_KEY_ACTION_DRY_STOP;
    profile->keymap.long_action[6] = APL_KEY_ACTION_FILTER_RESET_QUERY;
#endif
    profile->purify_enabled = APL_DEFAULT_PURIFY;
    profile->humidify_enabled = APL_DEFAULT_HUMIDIFY;
    profile->modules = (uint8_t)((APL_COMPILE_UV * APL_PROFILE_MODULE_UV)
        | (APL_COMPILE_ANION * APL_PROFILE_MODULE_ANION) | (APL_COMPILE_WIFI * APL_PROFILE_MODULE_WIFI));
    profile->sensors = (uint8_t)((APL_COMPILE_SENSOR_PM25 * APL_SENSOR_PM25_MASK)
        | (APL_COMPILE_SENSOR_PM10 * APL_SENSOR_PM10_MASK) | (APL_COMPILE_SENSOR_HCHO * APL_SENSOR_HCHO_MASK)
        | (APL_COMPILE_SENSOR_TVOC * APL_SENSOR_TVOC_MASK) | (APL_COMPILE_SENSOR_HUMIDITY * APL_SENSOR_HUMIDITY_MASK));
    /* Disabled defaults do not subscribe to their sensors. */
    if (0u == profile->purify_enabled) { profile->sensors &= (uint8_t)~15u; }
    if (0u == profile->humidify_enabled) { profile->sensors &= (uint8_t)~APL_SENSOR_HUMIDITY_MASK; }
    profile->default_fan_gear = APL_DEFAULT_FAN_GEAR;
#if APL_CORE_HAS_DRY
    profile->dry_fan_gear = APL_CORE_DRY_FAN_GEAR;
#endif
    profile->sensor_fallback_gear = APL_CORE_SENSOR_FALLBACK_GEAR;
#if APL_CORE_HAS_TURBO
    profile->turbo_enabled = APL_CORE_HAS_TURBO;
#endif
#if APL_COMPILE_FILTER
    profile->filter_shared = APL_CORE_FILTER_SHARED;
#endif
#if APL_COMPILE_HUMIDIFY
    profile->humidity_hysteresis = APL_CORE_HUMIDITY_HYSTERESIS;
#endif
#if APL_CORE_HAS_DRY
    profile->dry_s = APL_CORE_DRY_S;
#endif
#if APL_COMPILE_FILTER
    profile->filter_life_h = APL_CORE_FILTER_LIFE_H;
#endif
#if APL_COMPILE_HUMIDIFY
    profile->shortage_run_s = APL_CORE_WATER_SHORTAGE_RUN_S;
#endif
#if APL_COMPILE_HUMIDIFY
    profile->water_blink_s = APL_CORE_WATER_BLINK_S;
#endif
#if APL_COMPILE_WIFI
    profile->wifi_pair_s = APL_CORE_WIFI_PAIR_S;
#endif
    profile->display_rotate_s = APL_DISPLAY_ROTATE_DEFAULT_S;
#if APL_COMPILE_TIMER
    profile->timer_step_h = APL_TIMER_STEP_DEFAULT_H;
#endif
#if APL_COMPILE_SLEEP
    profile->fan_sleep_rpm = APL_DEFAULT_FAN_SLEEP_RPM;
#endif
    profile->fan_gear1_rpm = APL_DEFAULT_FAN_GEAR1_RPM;
    profile->fan_gear2_rpm = APL_DEFAULT_FAN_GEAR2_RPM;
    profile->fan_gear3_rpm = APL_DEFAULT_FAN_GEAR3_RPM;
#if APL_CORE_HAS_TURBO
    profile->fan_turbo_rpm = APL_DEFAULT_FAN_TURBO_RPM;
#endif
}

enum apl_profile_result apl_profile_validate(const struct apl_profile *profile) APL_CALL
{
    uint8_t modules;
    uint8_t sensors;
#if APL_COMPILE_WIFI || (APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP)
    uint8_t i;
#endif
#if APL_COMPILE_WIFI
    uint8_t ch;
#endif
    if (NULL == profile) { return APL_PROFILE_INVALID_ARGUMENT; }
    if (APL_PROFILE_VERSION != profile->version) { return APL_PROFILE_UNSUPPORTED_VERSION; }
#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
    if (profile->keymap.enabled > 1u) { return APL_PROFILE_INVALID_VALUE; }
    for (i = 0u; i < APL_KEYMAP_COUNT; ++i)
    {
        if ((profile->keymap.short_action[i] >= APL_KEY_ACTION_COUNT)
            || (profile->keymap.long_action[i] >= APL_KEY_ACTION_COUNT))
        { return APL_PROFILE_INVALID_VALUE; }
    }
#endif
#if !APL_COMPILE_SLEEP
    if ((APL_FAN_GEAR_SLEEP == profile->default_fan_gear) || (APL_FAN_GEAR_SLEEP == profile->sensor_fallback_gear)
#if APL_CORE_HAS_DRY
        || (APL_FAN_GEAR_SLEEP == profile->dry_fan_gear)
#endif
       ) { return APL_PROFILE_UNSUPPORTED_CAPABILITY; }
#endif
#if APL_COMPILE_WIFI
    /* Bounded validation: a malformed non-terminated table must never overread. */
    for (i = 0u; i <= APL_PRODUCT_PID_MAX; ++i)
    {
        ch = (uint8_t)profile->pid[i];
        if (0u == ch) { break; }
        if ((i == APL_PRODUCT_PID_MAX)
            || !(((ch >= 'a') && (ch <= 'z')) || ((ch >= 'A') && (ch <= 'Z'))
                || ((ch >= '0') && (ch <= '9'))))
        { return APL_PROFILE_INVALID_VALUE; }
    }
#endif
    modules = (uint8_t)((APL_COMPILE_UV * APL_PROFILE_MODULE_UV)
        | (APL_COMPILE_ANION * APL_PROFILE_MODULE_ANION) | (APL_COMPILE_WIFI * APL_PROFILE_MODULE_WIFI));
    sensors = (uint8_t)((APL_COMPILE_SENSOR_PM25 * APL_SENSOR_PM25_MASK)
        | (APL_COMPILE_SENSOR_PM10 * APL_SENSOR_PM10_MASK) | (APL_COMPILE_SENSOR_HCHO * APL_SENSOR_HCHO_MASK)
        | (APL_COMPILE_SENSOR_TVOC * APL_SENSOR_TVOC_MASK) | (APL_COMPILE_SENSOR_HUMIDITY * APL_SENSOR_HUMIDITY_MASK));
    if ((profile->purify_enabled > 1u) || (profile->humidify_enabled > 1u)
        || (0u == (profile->purify_enabled | profile->humidify_enabled))
#if APL_CORE_HAS_TURBO
        || (profile->turbo_enabled > 1u)
#endif
#if APL_COMPILE_FILTER
        || (profile->filter_shared > 1u)
#endif
        || (profile->default_fan_gear > APL_FAN_GEAR_AUTO)
#if APL_CORE_HAS_DRY
        || (profile->dry_fan_gear > APL_FAN_GEAR_3)
#endif
        || (profile->sensor_fallback_gear > APL_FAN_GEAR_3)
#if APL_COMPILE_HUMIDIFY
        || (profile->humidity_hysteresis > 100u)
#endif
#if APL_COMPILE_FILTER
        || (0u == profile->filter_life_h) || (profile->filter_life_h > 1000000UL)
#endif
#if APL_CORE_HAS_DRY
        || (profile->dry_s > 86400UL)
#endif
#if APL_COMPILE_HUMIDIFY
        || (profile->shortage_run_s > 86400UL)
#endif
#if APL_COMPILE_HUMIDIFY
        || (profile->water_blink_s > 86400UL)
#endif
#if APL_COMPILE_WIFI
        || (0u == profile->wifi_pair_s) || (profile->wifi_pair_s > 86400UL)
#endif
        || (0u == profile->display_rotate_s) || (profile->display_rotate_s > 3600u)
#if APL_COMPILE_TIMER
        || (0u == profile->timer_step_h) || (profile->timer_step_h > APL_CORE_TIMER_MAX_H)
#endif
        || ((0u == profile->purify_enabled) && (0u != (profile->sensors & 15u)))
        || ((0u == profile->humidify_enabled) && (0u != (profile->sensors & APL_SENSOR_HUMIDITY_MASK))))
    {
        return APL_PROFILE_INVALID_VALUE;
    }
    if (((0u != profile->purify_enabled) && (0u == APL_COMPILE_PURIFY))
        || ((0u != profile->humidify_enabled) && (0u == APL_COMPILE_HUMIDIFY))
        || (0u != (profile->modules & (uint8_t)~modules))
        || (0u != (profile->sensors & (uint8_t)~sensors))
#if APL_CORE_HAS_TURBO
        || ((0u != profile->turbo_enabled) && (0u == APL_CORE_HAS_TURBO))
#endif
       )
    {
        return APL_PROFILE_UNSUPPORTED_CAPABILITY;
    }
    return APL_PROFILE_OK;
}

enum apl_profile_result apl_core_init_profile(struct apl_core *self,
    const struct apl_core_ops *ops, void *ctx, const struct apl_profile *profile) APL_CALL
{
    struct apl_core_state *state;
    enum apl_profile_result result;
    if (NULL == self) { return APL_PROFILE_INVALID_ARGUMENT; }
    if (NULL == profile)
    {
        apl_core_init(self, ops, ctx);
        return APL_PROFILE_OK;
    }
    result = apl_profile_validate(profile);
    if (APL_PROFILE_OK != result) { return result; }
    apl_core_init(self, ops, ctx);
    state = apl_core_state_of(self);
    state->profile = *profile;
#if APL_COMPILE_PURIFY
    state->purify_enabled = profile->purify_enabled;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidify_enabled = profile->humidify_enabled;
#endif
    state->fan_gear = (enum apl_fan_gear)profile->default_fan_gear;
#if APL_CORE_HAS_TURBO
    state->gear_before_turbo = state->fan_gear;
#endif
#if APL_COMPILE_SLEEP
    state->gear_before_sleep = state->fan_gear;
#endif
    state->fan_output = state->fan_gear;
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(state);
#endif
    return APL_PROFILE_OK;
}

void apl_core_get_profile(const struct apl_core *self, struct apl_profile *profile) APL_CALL
{
    if (NULL == profile) { return; }
    if (NULL == self) { apl_profile_defaults(profile); return; }
    *profile = apl_core_state_of_const(self)->profile;
}

#if APL_COMPILE_WIFI
void apl_core_get_pid(const struct apl_core *self, char *pid) APL_CALL
{
    if (NULL == pid) { return; }
    if (NULL == self) { memset(pid, 0, APL_PRODUCT_PID_MAX + 1u); return; }
    memcpy(pid, apl_core_state_of_const(self)->profile.pid, APL_PRODUCT_PID_MAX + 1u);
}
#endif

void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx) APL_CALL
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    memset(self, 0, sizeof(*self));
    state = apl_core_state_of(self);
    apl_profile_defaults(&state->profile);
    state->ops = ops;
    state->ctx = ctx;
    state->power = APL_POWER_OFF;
    state->run_state = APL_RUN_STANDBY;
#if APL_CORE_HAS_DRY
    state->dry_needed = 0u;
#endif
    state->fan_gear = (enum apl_fan_gear)state->profile.default_fan_gear;
#if APL_CORE_HAS_TURBO
    state->gear_before_turbo = state->fan_gear;
#endif
#if APL_COMPILE_SLEEP
    state->gear_before_sleep = state->fan_gear;
#endif
    state->fan_output = state->fan_gear;
    state->fan = APL_SWITCH_OFF;
#if APL_COMPILE_UV
    state->uv = APL_SWITCH_OFF;
#endif
#if APL_COMPILE_ANION
    state->anion = APL_SWITCH_OFF;
#endif
#if APL_COMPILE_HUMIDIFY
    state->pump = APL_SWITCH_OFF;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidify_on = 1u;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidity_target = APL_HUMIDITY_TARGET_OFF;
#endif
#if APL_COMPILE_PURIFY
    state->purify_enabled = (uint8_t)APL_DEFAULT_PURIFY;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidify_enabled = (uint8_t)APL_DEFAULT_HUMIDIFY;
#endif
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(state);
#endif
}

void apl_core_tick(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    uint8_t warmup_finished;

    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of(self);
    apl_core_timer_tick(self);
    apl_core_water_sample(self);

    warmup_finished = 0u;
#if APL_CORE_HAS_SENSORS
    if ((APL_RUN_ON == state->run_state) && (0u != state->sensor_warmup_ticks))
    {
        state->sensor_warmup_ticks--;
        warmup_finished = (0u == state->sensor_warmup_ticks) ? 1u : 0u;
    }
#endif
    state->second_subtick++;
    if (state->second_subtick >= APL_CORE_TICKS_PER_SECOND)
    {
        state->second_subtick = 0u;
        apl_core_second(self);
    }
    else if (0u != warmup_finished)
    {
        /* Align the first usable sample to the opening transition, not uptime seconds. */
        apl_core_sensor_sample(state);
    }

    apl_core_outputs_update(self);
}

void apl_core_power_on(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of(self);
    if (0u == state->used)
    {
        state->fan_gear = (enum apl_fan_gear)state->profile.default_fan_gear;
        state->used = 1u;
    }

    if (APL_RUN_ON != state->run_state)
    {
#if APL_CORE_HAS_SENSORS
        state->sensor_warmup_ticks = (uint16_t)(APL_CORE_SENSOR_WARMUP_MS / APL_CORE_TICK_MS);
#endif
    }
    state->power = APL_POWER_ON;
    state->run_state = APL_RUN_ON;
#if APL_CORE_HAS_RH
    state->humidity_sampled = 0u;
#endif
#if APL_CORE_HAS_SENSORS
    state->sensors_ready = 0u;
#endif
#if APL_CORE_HAS_SENSORS
    state->sensor_faults = 0u;
#endif
#if APL_CORE_HAS_SENSORS
    state->sensors_warming = 0u;
#endif
#if APL_CORE_HAS_AQ
    state->aq_ready = 0u;
#endif
#if APL_COMPILE_SENSOR_PM25
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_pm25))
    {
        state->sensors_warming |= (uint8_t)(state->profile.sensors & APL_SENSOR_PM25_MASK);
    }
#endif
#if APL_COMPILE_SENSOR_PM10
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_pm10))
    {
        state->sensors_warming |= (uint8_t)(state->profile.sensors & APL_SENSOR_PM10_MASK);
    }
#endif
#if APL_COMPILE_SENSOR_HCHO
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_hcho))
    {
        state->sensors_warming |= (uint8_t)(state->profile.sensors & APL_SENSOR_HCHO_MASK);
    }
#endif
#if APL_COMPILE_SENSOR_TVOC
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_tvoc))
    {
        state->sensors_warming |= (uint8_t)(state->profile.sensors & APL_SENSOR_TVOC_MASK);
    }
#endif
#if APL_COMPILE_SENSOR_HUMIDITY
    if ((0u != apl_core_has_humidify(state)) && (NULL != state->ops) && (NULL != state->ops->get_humidity))
    {
        state->sensors_warming |= (uint8_t)(state->profile.sensors & APL_SENSOR_HUMIDITY_MASK);
    }
#endif
#if APL_CORE_HAS_DRY
    state->dry_left_s = 0u;
#endif
#if APL_COMPILE_FACTORY
    state->factory_done = 0u;
#endif
#if APL_COMPILE_FACTORY
    state->cl_left_s = 0u;
#endif
#if APL_COMPILE_HUMIDIFY
    if ((0u != state->water_valid) && (APL_WATER_LEVEL_0 == state->water_level))
    {
        apl_core_shortage_start(state);
    }
#endif

    apl_core_memory_notify(self);
}

void apl_core_power_off(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of(self);
    if (APL_POWER_OFF == state->power)
    {
        return;
    }

    state->power = APL_POWER_OFF;
    state->run_state = APL_RUN_STANDBY;
#if APL_CORE_HAS_DRY
    state->dry_needed = 0u;
#endif
#if APL_CORE_HAS_DRY
    state->dry_left_s = 0u;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidity_target = APL_HUMIDITY_TARGET_OFF;
#endif
#if APL_CORE_HAS_TURBO
    if (APL_FAN_GEAR_TURBO == state->fan_gear)
    {
        state->fan_gear = state->gear_before_turbo;
    }
#endif

#if APL_COMPILE_WIFI
    apl_wifi_cancel_pairing(&state->wifi);
#endif

    apl_core_shortage_clear(state);
    apl_core_timer_clear(state);
    apl_core_memory_notify(self);
    if ((NULL != state->ops) && (NULL != state->ops->on_power_off))
    {
        state->ops->on_power_off(self, state->ctx);
    }
}

enum apl_power apl_core_get_power(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return APL_POWER_OFF;
    }

    return apl_core_state_of_const(self)->power;
}

enum apl_run_state apl_core_get_run_state(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return APL_RUN_STANDBY;
    }

    return apl_core_state_of_const(self)->run_state;
}

#if APL_COMPILE_TIMER
void apl_core_set_timer(struct apl_core *self,
                        uint32_t seconds,
                        void (*on_timer)(struct apl_core *timer_self, void *timer_ctx) APL_CALL,
                        void *ctx) APL_CALL
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of(self);
    state->timer_subtick = 0u;
    state->timer_left_s = seconds;
    if (0u == seconds)
    {
        state->on_timer = NULL;
        state->timer_ctx = NULL;
        return;
    }

    state->on_timer = on_timer;
    state->timer_ctx = ctx;
}
#endif

#if APL_COMPILE_TIMER
uint32_t apl_core_get_timer(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->timer_left_s;
}
#endif

void apl_core_set_fan_gear(struct apl_core *self, enum apl_fan_gear gear) APL_CALL
{
    if ((NULL == self) || (0u == apl_core_gear_is_valid(gear)))
    {
        return;
    }

#if APL_CORE_HAS_TURBO
    if ((APL_FAN_GEAR_TURBO == gear) && (0u == apl_core_state_of_const(self)->profile.turbo_enabled))
    {
        return;
    }
#endif
    apl_core_state_of(self)->fan_gear = gear;
}

enum apl_fan_gear apl_core_get_fan_gear(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
    }

    return apl_core_state_of_const(self)->fan_gear;
}

uint16_t apl_core_get_fan_target_rpm(const struct apl_core *self) APL_CALL
{
    const struct apl_core_state *state;
    if (NULL == self) { return 0u; }
    state = apl_core_state_of_const(self);
    if (APL_SWITCH_ON != state->fan) { return 0u; }
    switch (state->fan_output)
    {
#if APL_COMPILE_SLEEP
    case APL_FAN_GEAR_SLEEP: return state->profile.fan_sleep_rpm;
#endif
    case APL_FAN_GEAR_1: return state->profile.fan_gear1_rpm;
    case APL_FAN_GEAR_2: return state->profile.fan_gear2_rpm;
    case APL_FAN_GEAR_3: return state->profile.fan_gear3_rpm;
#if APL_CORE_HAS_TURBO
    case APL_FAN_GEAR_TURBO: return state->profile.fan_turbo_rpm;
#endif
    default: return 0u;
    }
}

static enum apl_fan_gear apl_core_gear_next(enum apl_fan_gear gear) APL_CALL
{
switch (gear)
    {
#if APL_COMPILE_SLEEP
    case APL_FAN_GEAR_SLEEP: return APL_FAN_GEAR_1;
#endif
    case APL_FAN_GEAR_1: return APL_FAN_GEAR_2;
    case APL_FAN_GEAR_2: return APL_FAN_GEAR_3;
    case APL_FAN_GEAR_3: return APL_FAN_GEAR_AUTO;
#if APL_COMPILE_SLEEP
    default: return APL_FAN_GEAR_SLEEP;
#else
    default: return APL_FAN_GEAR_1;
#endif
    }
}

static enum apl_link_result apl_core_cmd_power(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    state = apl_core_state_of(self);
    switch (state->run_state)
    {
    case APL_RUN_STANDBY:
#if APL_CORE_HAS_DRY
    case APL_RUN_DRY:
#endif
        apl_core_power_on(self); return APL_LINK_OK;
    case APL_RUN_ON:
#if APL_CORE_HAS_DRY
        if ((0u != apl_core_has_humidify(state)) && (0u != state->dry_needed)) { apl_core_dry_start(self); }
        else
#endif
        { apl_core_power_off(self); }
        return APL_LINK_OK;
    default: return APL_LINK_REJECTED;
    }
}

static enum apl_link_result apl_core_cmd_fan(struct apl_core *self,
                                             const struct apl_link_command *command) APL_CALL
{
    struct apl_core_state *state;
    enum apl_fan_gear base;
    state = apl_core_state_of(self);
    if (APL_RUN_ON != state->run_state) { return APL_LINK_REJECTED; }
    switch (command->type)
    {
    case APL_LINK_CMD_FAN_CYCLE:
        base = state->fan_gear;
#if APL_CORE_HAS_TURBO
        if (APL_FAN_GEAR_TURBO == base) { base = state->gear_before_turbo; }
#endif
        state->fan_gear = apl_core_gear_next(base); break;
    case APL_LINK_CMD_FAN_SET:
        if (0u == apl_core_gear_is_valid((enum apl_fan_gear)command->arg)) { return APL_LINK_REJECTED; }
#if APL_CORE_HAS_TURBO
        if ((0u == state->profile.turbo_enabled) && (APL_FAN_GEAR_TURBO == command->arg)) { return APL_LINK_REJECTED; }
#endif
        state->fan_gear = (enum apl_fan_gear)command->arg; break;
#if APL_CORE_HAS_TURBO
    case APL_LINK_CMD_TURBO_TOGGLE:
        if (0u == state->profile.turbo_enabled) { return APL_LINK_REJECTED; }
        if (APL_FAN_GEAR_TURBO == state->fan_gear) { state->fan_gear = state->gear_before_turbo; }
        else { state->gear_before_turbo = state->fan_gear; state->fan_gear = APL_FAN_GEAR_TURBO; }
        break;
#endif
#if APL_COMPILE_SLEEP
    case APL_LINK_CMD_SLEEP_TOGGLE:
        if (APL_FAN_GEAR_SLEEP == state->fan_gear)
        { state->fan_gear = (APL_FAN_GEAR_SLEEP == state->gear_before_sleep) ? (enum apl_fan_gear)state->profile.default_fan_gear : state->gear_before_sleep; }
        else { state->gear_before_sleep = state->fan_gear; state->fan_gear = APL_FAN_GEAR_SLEEP; }
        break;
#endif
    default: return APL_LINK_REJECTED;
    }
    apl_core_memory_notify(self); return APL_LINK_OK;
}

#if APL_CORE_HAS_RH
static uint8_t apl_core_target_is_valid(uint8_t target) APL_CALL
{
    if (APL_HUMIDITY_TARGET_OFF == target)
    {
        return 1u;
    }

    if ((target < APL_CORE_HUMIDITY_MIN) || (target > APL_CORE_HUMIDITY_MAX))
    {
        return 0u;
    }

    return (0u == ((target - APL_CORE_HUMIDITY_MIN) % APL_CORE_HUMIDITY_STEP)) ? 1u : 0u;
}
#endif

#if APL_COMPILE_HUMIDIFY
static uint8_t apl_core_water_ok(const struct apl_core_state *state) APL_CALL
{
    return ((0u == state->water_valid) || (APL_WATER_LEVEL_0 != state->water_level)) ? 1u : 0u;
}
#endif

static uint8_t apl_core_on_or_standby(const struct apl_core_state *state) APL_CALL
{
    return ((APL_RUN_ON == state->run_state) || (APL_RUN_STANDBY == state->run_state))
           ? 1u : 0u;
}

#if APL_COMPILE_FILTER
static enum apl_link_result apl_core_cmd_filter_reset(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    uint8_t purify;
    uint8_t humidify;
    uint8_t expired;
    state = apl_core_state_of(self);
    if (0u == apl_core_on_or_standby(state)) { return APL_LINK_REJECTED; }
    purify = apl_core_has_purify(state);
    humidify = apl_core_has_humidify(state);
    if ((0u == purify) && (0u == humidify)) { return APL_LINK_REJECTED; }
    expired = 0u;
#if APL_COMPILE_PURIFY
    if ((0u != purify) && (state->filter_purify_s >= state->profile.filter_life_h * APL_CORE_HOUR_S)) { expired = 1u; }
#endif
#if APL_COMPILE_HUMIDIFY
    if ((0u != humidify) && (state->filter_humidify_s >= state->profile.filter_life_h * APL_CORE_HOUR_S)) { expired = 1u; }
#endif
#if APL_COMPILE_PURIFY && APL_COMPILE_HUMIDIFY
    if ((0u == state->profile.filter_shared) && (0u != purify) && (0u != humidify))
    { if (state->filter_purify_s >= state->filter_humidify_s) { state->filter_purify_s = 0u; } else { state->filter_humidify_s = 0u; } }
    else
#endif
    {
#if APL_COMPILE_PURIFY
        if (0u != purify) { state->filter_purify_s = 0u; }
#endif
#if APL_COMPILE_HUMIDIFY
        if (0u != humidify) { state->filter_humidify_s = 0u; }
#endif
    }
#if APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(state);
#endif
    state->filter_reset_forced = (0u == expired) ? 1u : 0u;
    state->filter_reset_seq++;
    apl_core_beep_unless_sleep(state, expired ? APL_BEEP_FILTER_RESET : APL_BEEP_FILTER_FORCED);
    apl_core_memory_notify(self);
    return APL_LINK_OK;
}
#endif

#if APL_COMPILE_FACTORY
static enum apl_link_result apl_core_cmd_factory_enter(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    state = apl_core_state_of(self);
    if ((APL_RUN_STANDBY != state->run_state) || (state->uptime_s >= APL_CORE_FACTORY_WINDOW_S)) { return APL_LINK_REJECTED; }
    state->factory_pending = 1u;
#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
    state->real_filter_purify_s = state->filter_purify_s;
    state->filter_purify_s = state->profile.filter_life_h * APL_CORE_HOUR_S - APL_CORE_FACTORY_FILTER_LEFT_S;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
    state->real_filter_humidify_s = state->filter_humidify_s;
    state->filter_humidify_s = state->profile.filter_life_h * APL_CORE_HOUR_S - APL_CORE_FACTORY_FILTER_LEFT_S;
#endif
    state->run_state = APL_RUN_FACTORY;
    state->power = APL_POWER_ON;
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(state);
#endif
    apl_core_beep(state, APL_BEEP_FACTORY);
    apl_core_memory_notify(self);
    return APL_LINK_OK;
}
#endif

enum apl_link_result apl_core_command(struct apl_core *self,
                                      const struct apl_link_command *command) APL_CALL
{
    struct apl_core_state *state;
    if ((NULL == self) || (NULL == command)) { return APL_LINK_REJECTED; }
    state = apl_core_state_of(self);
    switch (command->type)
    {
    case APL_LINK_CMD_POWER: return apl_core_cmd_power(self);
#if APL_CORE_HAS_DRY
    case APL_LINK_CMD_DRY_STOP:
        if (APL_RUN_DRY != state->run_state) { return APL_LINK_REJECTED; }
        apl_core_power_off(self); return APL_LINK_OK;
#endif
    case APL_LINK_CMD_FAN_CYCLE:
    case APL_LINK_CMD_FAN_SET:
#if APL_CORE_HAS_TURBO
    case APL_LINK_CMD_TURBO_TOGGLE:
#endif
#if APL_COMPILE_SLEEP
    case APL_LINK_CMD_SLEEP_TOGGLE:
#endif
        return apl_core_cmd_fan(self, command);
#if APL_COMPILE_TIMER
    case APL_LINK_CMD_TIMER_SET:
        if ((APL_RUN_ON != state->run_state) || (command->arg > APL_CORE_TIMER_MAX_H)) { return APL_LINK_REJECTED; }
        apl_core_set_timer(self, (uint32_t)command->arg * APL_CORE_HOUR_S, apl_core_panel_timer_expire, NULL);
        return APL_LINK_OK;
#endif
#if (APL_COMPILE_PURIFY || APL_COMPILE_UV || APL_COMPILE_ANION)
    case APL_LINK_CMD_PURIFY_TOGGLE:
        if ((0u == apl_core_has_purify(state)) && (0u == (state->profile.modules & (APL_PROFILE_MODULE_UV | APL_PROFILE_MODULE_ANION)))) { return APL_LINK_REJECTED; }
        if (0u == apl_core_on_or_standby(state)) { return APL_LINK_REJECTED; }
        state->purify_on = (0u == state->purify_on) ? 1u : 0u;
#if APL_COMPILE_UV
        state->uv_requested = (0u != (state->profile.modules & APL_PROFILE_MODULE_UV)) ? state->purify_on : 0u;
        state->uv_fault = 0u;
#endif
#if APL_COMPILE_ANION
        state->anion_requested = (0u != (state->profile.modules & APL_PROFILE_MODULE_ANION)) ? state->purify_on : 0u;
#endif
        apl_core_memory_notify(self); return APL_LINK_OK;
#endif
#if APL_COMPILE_UV
    case APL_LINK_CMD_UV_SET:
        if ((0u == (state->profile.modules & APL_PROFILE_MODULE_UV)) || (0u == apl_core_on_or_standby(state)) || (command->arg > 1u)) { return APL_LINK_REJECTED; }
        if ((state->uv_requested != command->arg) || ((0u != command->arg) && (0u != state->uv_fault)))
        {
            state->uv_requested = command->arg;
            state->purify_on = ((0u != state->uv_requested)
#if APL_COMPILE_ANION
                                || (0u != state->anion_requested)
#endif
                              ) ? 1u : 0u;
            state->uv_fault = 0u; apl_core_beep_unless_sleep(state, APL_BEEP_SHORT); apl_core_memory_notify(self);
        }
        return APL_LINK_OK;
#endif
#if APL_COMPILE_ANION
    case APL_LINK_CMD_ANION_SET:
        if ((0u == (state->profile.modules & APL_PROFILE_MODULE_ANION)) || (0u == apl_core_on_or_standby(state)) || (command->arg > 1u)) { return APL_LINK_REJECTED; }
        if (state->anion_requested != command->arg)
        {
            state->anion_requested = command->arg;
            state->purify_on = ((0u != state->anion_requested)
#if APL_COMPILE_UV
                                || (0u != state->uv_requested)
#endif
                              ) ? 1u : 0u;
            apl_core_beep_unless_sleep(state, APL_BEEP_SHORT); apl_core_memory_notify(self);
        }
        return APL_LINK_OK;
#endif
#if APL_COMPILE_HUMIDIFY
    case APL_LINK_CMD_HUMIDIFY_TOGGLE:
        if ((0u == apl_core_has_humidify(state)) || (0u == apl_core_on_or_standby(state)) || ((0u == state->humidify_on) && (0u == apl_core_water_ok(state)))) { return APL_LINK_REJECTED; }
        state->humidify_on = (0u == state->humidify_on) ? 1u : 0u;
#if APL_CORE_HAS_TURBO
        if ((0u != apl_core_has_purify(state)) && (APL_FAN_GEAR_TURBO == state->fan_gear)) { state->fan_gear = APL_FAN_GEAR_AUTO; }
#endif
        apl_core_memory_notify(self); return APL_LINK_OK;
    case APL_LINK_CMD_HUMIDITY_TARGET:
        if ((0u == apl_core_has_humidify(state)) || (0u == apl_core_on_or_standby(state))) { return APL_LINK_REJECTED; }
#if APL_CORE_HAS_RH
        if ((0u == apl_core_target_is_valid(command->arg))
            || ((APL_HUMIDITY_TARGET_OFF != command->arg) && ((0u == (state->profile.sensors & APL_SENSOR_HUMIDITY_MASK)) || (0u == apl_core_water_ok(state))))) { return APL_LINK_REJECTED; }
#else
        if (APL_HUMIDITY_TARGET_OFF != command->arg) { return APL_LINK_REJECTED; }
#endif
        state->humidity_target = command->arg; return APL_LINK_OK;
#endif
#if APL_COMPILE_FILTER
    case APL_LINK_CMD_FILTER_RESET: return apl_core_cmd_filter_reset(self);
#endif
#if APL_COMPILE_WIFI
    case APL_LINK_CMD_WIFI_PAIR:
        if ((0u == (state->profile.modules & APL_PROFILE_MODULE_WIFI)) || (0u == apl_core_on_or_standby(state))) { return APL_LINK_REJECTED; }
        apl_wifi_pair(&state->wifi, state->profile.wifi_pair_s); return APL_LINK_OK;
#endif
#if APL_COMPILE_FACTORY
    case APL_LINK_CMD_FACTORY_ENTER: return apl_core_cmd_factory_enter(self);
    case APL_LINK_CMD_FACTORY_EXIT:
        if (APL_RUN_FACTORY != state->run_state) { return APL_LINK_REJECTED; }
        apl_core_factory_restore(self); return APL_LINK_OK;
#endif
    default: return APL_LINK_REJECTED;
    }
}

void apl_core_get_snapshot(const struct apl_core *self, struct apl_link_snapshot *snapshot) APL_CALL
{
    const struct apl_core_state *state;
    if (NULL == snapshot) { return; }
    memset(snapshot, 0, sizeof(*snapshot));
    if (NULL == self) { return; }
    state = apl_core_state_of_const(self);
    snapshot->run_state = state->run_state; snapshot->fan_gear = state->fan_gear;
    snapshot->fan_output = state->fan_output; snapshot->beep = state->beep; snapshot->beep_seq = state->beep_seq;
    snapshot->purify_on = state->purify_on; snapshot->module_faults = state->module_faults;
    snapshot->sensors_configured = state->profile.sensors;
    snapshot->display_rotate_s = state->profile.display_rotate_s;
    snapshot->fan_target_rpm = apl_core_get_fan_target_rpm(self);
#if APL_COMPILE_TIMER
    snapshot->timer_left_s = state->timer_left_s; snapshot->timer_step_h = state->profile.timer_step_h;
#endif
#if APL_CORE_HAS_DRY
    snapshot->dry_left_s = state->dry_left_s;
#endif
#if APL_COMPILE_SLEEP
    snapshot->sleep = ((APL_RUN_ON == state->run_state) && (APL_FAN_GEAR_SLEEP == state->fan_gear)) ? 1u : 0u;
#endif
#if APL_COMPILE_FILTER
    snapshot->filter_reset_seq = state->filter_reset_seq; snapshot->filter_reset_forced = state->filter_reset_forced;
#endif
#if APL_COMPILE_FACTORY
    snapshot->factory_done = state->factory_done;
#endif
#if APL_CORE_HAS_SENSORS
    snapshot->sensors_ready = state->sensors_ready; snapshot->sensors_warming = state->sensors_warming; snapshot->sensor_faults = state->sensor_faults;
#endif
#if APL_CORE_HAS_AQ
    snapshot->aq_ready = state->aq_ready;
#endif
#if APL_COMPILE_UV
    snapshot->uv = state->uv;
    snapshot->uv_enabled = (0u != (state->profile.modules & APL_PROFILE_MODULE_UV)) ? 1u : 0u;
    snapshot->uv_requested = state->uv_requested; snapshot->uv_fault = state->uv_fault;
#endif
#if APL_COMPILE_ANION
    snapshot->anion = state->anion;
    snapshot->anion_enabled = (0u != (state->profile.modules & APL_PROFILE_MODULE_ANION)) ? 1u : 0u;
    snapshot->anion_requested = state->anion_requested;
#endif
#if APL_COMPILE_WIFI
    snapshot->wifi = state->wifi.state;
    snapshot->wifi_enabled = (0u != (state->profile.modules & APL_PROFILE_MODULE_WIFI)) ? 1u : 0u;
#endif
#if APL_COMPILE_PURIFY
    if (0u != apl_core_has_purify(state))
    {
        snapshot->purify_enabled = 1u;
#if APL_COMPILE_SENSOR_PM25
        snapshot->pm25 = state->pm25;
#endif
#if APL_CORE_HAS_AQ
        snapshot->aq_level = state->aq_level;
#endif
#if APL_COMPILE_FILTER
#if APL_CORE_COMPILE_FILTER_CACHE
        snapshot->filter_purify_left = state->filter_purify_left;
#else
        snapshot->filter_purify_left = apl_core_filter_left(state, state->filter_purify_s);
#endif
#endif
    }
#endif
#if APL_COMPILE_HUMIDIFY
    snapshot->pump = state->pump; snapshot->water_alert = state->water_alert;
    if (0u != apl_core_has_humidify(state))
    {
        snapshot->humidify_enabled = 1u; snapshot->humidify_on = state->humidify_on;
        snapshot->humidity_target = state->humidity_target; snapshot->water_level = state->water_level;
#if APL_CORE_HAS_RH
        snapshot->humidity = state->humidity;
#endif
#if APL_COMPILE_FILTER
#if APL_CORE_COMPILE_FILTER_CACHE
        snapshot->filter_humidify_left = state->filter_humidify_left;
#else
        snapshot->filter_humidify_left = apl_core_filter_left(state, state->filter_humidify_s);
#endif
#endif
    }
#endif
}

#if APL_COMPILE_MEMORY
void apl_core_get_memory(const struct apl_core *self, struct apl_core_memory *memory) APL_CALL
{
    if (NULL == memory)
    {
        return;
    }

    if (NULL == self)
    {
        memset(memory, 0, sizeof(*memory));
        return;
    }

    apl_core_memory_fill(apl_core_state_of_const(self), memory);
}
#endif

#if APL_COMPILE_MEMORY
void apl_core_memory_restore(struct apl_core *self, const struct apl_core_memory *memory) APL_CALL
{
    struct apl_core_state *state;

    if ((NULL == self) || (NULL == memory))
    {
        return;
    }

    state = apl_core_state_of(self);
#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
    state->filter_purify_s = memory->filter_purify_s;
#endif
#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
    state->filter_humidify_s = memory->filter_humidify_s;
#endif
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(state);
#endif
#if APL_COMPILE_FACTORY
    if (0u != memory->factory_pending)
    {
        apl_core_factory_restore(self);
        return;
    }
#endif

    if ((0u != apl_core_gear_is_valid(memory->fan_gear))
        && (APL_FAN_GEAR_TURBO != memory->fan_gear))
    {
        state->fan_gear = memory->fan_gear;
    }

#if APL_COMPILE_UV
    state->uv_requested = (1u == memory->module_state_valid)
                          ? ((1u == memory->uv_requested) ? 1u : 0u)
                          : ((0u != memory->purify_on) ? 1u : 0u);
#endif
#if APL_COMPILE_ANION
    state->anion_requested = (1u == memory->module_state_valid)
                             ? ((1u == memory->anion_requested) ? 1u : 0u)
                             : ((0u != memory->purify_on) ? 1u : 0u);
#endif
#if APL_COMPILE_UV
    if (0u == (state->profile.modules & APL_PROFILE_MODULE_UV))
    {
#if APL_COMPILE_UV
        state->uv_requested = 0u;
#endif
    }
#endif
#if APL_COMPILE_ANION
    if (0u == (state->profile.modules & APL_PROFILE_MODULE_ANION))
    {
#if APL_COMPILE_ANION
        state->anion_requested = 0u;
#endif
    }
#endif
    state->used = (0u != memory->used) ? 1u : 0u;
    state->purify_on = (0u != memory->purify_on) ? 1u : 0u;
#if APL_COMPILE_HUMIDIFY
    state->humidify_on = (0u != memory->humidify_on) ? 1u : 0u;
#endif
    if ((0u != memory->running) && (0u != state->used))
    {
        apl_core_power_on(self);
    }
}
#endif

#if APL_COMPILE_WIFI
void apl_core_set_wifi_connected(struct apl_core *self, uint8_t connected) APL_CALL
{
    struct apl_core_state *state;
    if ((NULL == self) || (connected > 1u)) { return; }
    state = apl_core_state_of(self);
    apl_wifi_connected(&state->wifi, (uint8_t)((0u != connected)
        && (0u != (state->profile.modules & APL_PROFILE_MODULE_WIFI))));
}
#endif

#if APL_COMPILE_FILTER && APL_COMPILE_PURIFY
void apl_core_filter_purify_reset(struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_purify_s = 0u;
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(apl_core_state_of(self));
#endif
}

uint32_t apl_core_get_filter_purify(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_purify_s;
}

#endif
#if APL_COMPILE_PURIFY
uint8_t apl_core_get_purify_enabled(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->purify_enabled;
}
#endif

#if APL_COMPILE_FILTER && APL_COMPILE_HUMIDIFY
void apl_core_filter_humidify_reset(struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_humidify_s = 0u;
#if APL_COMPILE_FILTER && APL_CORE_COMPILE_FILTER_CACHE
    apl_core_filter_cache_update(apl_core_state_of(self));
#endif
}

uint32_t apl_core_get_filter_humidify(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_humidify_s;
}

#endif
#if APL_COMPILE_HUMIDIFY
uint8_t apl_core_get_humidify_enabled(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->humidify_enabled;
}
#endif
