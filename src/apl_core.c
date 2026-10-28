/**
 * @file    apl_core.c
 * @brief   主控对象：整机状态、风机、净化、加湿、水位、滤网、产测，以及快照。
 *
 * 规则来源是后端规格书第 5 节和《机型交互逻辑（Excel提炼）》。
 * 规格冲突处取的读法列在后端规格书第 6 节，并做成 apl_core_config.h 里的宏。
 */

#include "apl_core.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__)
#define APL_CORE_MAY_ALIAS __attribute__((__may_alias__))
#else
#define APL_CORE_MAY_ALIAS
#endif

_Static_assert(APL_CORE_TICKS_PER_SECOND == 100u, "10 ms 一拍时，1 秒是 100 拍");
_Static_assert(APL_CORE_VERSION_NUM == 0x000100u, "版本号与分段宏不一致");

/** 一小时的秒数。面板按小时设置定时。 */
#define APL_CORE_HOUR_S             3600u
/** 面板定时上限，小时。 */
#define APL_CORE_TIMER_MAX_H        24u
/** 滤网寿命，秒。 */
#define APL_CORE_FILTER_LIFE_S      ((uint32_t)APL_CORE_FILTER_LIFE_H * APL_CORE_HOUR_S)
/** 滤网剩余百分比的上限。原表显示 1～99。 */
#define APL_CORE_FILTER_LEFT_MAX    99u
/** 恒湿目标的上下限与步进，百分比。 */
#define APL_CORE_HUMIDITY_MIN       40u
#define APL_CORE_HUMIDITY_MAX       80u
#define APL_CORE_HUMIDITY_STEP      5u
/** 恒湿风速表的分界，百分点。差值 < 10 一档，10～19 二档，≥ 20 三档。 */
#define APL_CORE_HUMIDITY_DIFF_2    10
#define APL_CORE_HUMIDITY_DIFF_3    20

/** 对外对象里的真实状态。允许与占位对象别名，使用方不直接访问。 */
struct APL_CORE_MAY_ALIAS apl_core_state
{
    const struct apl_core_ops *ops;
    void *ctx;
    void (*on_timer)(struct apl_core *self, void *ctx);
    void *timer_ctx;
    enum apl_power power;
    enum apl_run_state run_state;
    enum apl_fan_gear fan_gear;
    enum apl_fan_gear gear_before_turbo;
    enum apl_fan_gear gear_before_sleep;
    enum apl_fan_gear fan_output;
    enum apl_switch fan;
    enum apl_switch uv;
    enum apl_switch anion;
    enum apl_switch pump;
    enum apl_water_level water_level;
    enum apl_water_alert water_alert;
    enum apl_aq_level aq_level;
    enum apl_wifi_state wifi;
    enum apl_beep beep;
    uint32_t timer_left_s;
    uint32_t dry_left_s;
    uint32_t shortage_s;
    uint32_t uptime_s;
    uint32_t wifi_left_s;
    uint32_t filter_purify_s;
    uint32_t filter_humidify_s;
    uint32_t real_filter_purify_s;
    uint32_t real_filter_humidify_s;
    uint32_t cl_left_s;
    uint16_t timer_subtick;
    uint16_t second_subtick;
    uint16_t pm25;
    uint8_t water_valid;
    uint8_t outputs_valid;
    uint8_t purify_enabled;
    uint8_t humidify_enabled;
    uint8_t purify_on;
    uint8_t humidify_on;
    uint8_t humidity;
    uint8_t humidity_target;
    uint8_t humidity_hold;
    uint8_t shortage;
    uint8_t uv_fault;
    uint8_t used;
    uint8_t factory_pending;
    uint8_t factory_done;
    uint8_t beep_seq;
    uint8_t filter_reset_seq;
    uint8_t filter_reset_forced;
};

_Static_assert(sizeof(struct apl_core_state) <= sizeof(struct apl_core),
               "APL_CORE_OPAQUE_SIZE 小于对象状态");
_Static_assert(_Alignof(struct apl_core_state) <= _Alignof(struct apl_core),
               "占位对象的对齐小于对象状态");

static struct apl_core_state *apl_core_state_of(struct apl_core *self)
{
    return (struct apl_core_state *)(void *)self->storage.bytes;
}

static const struct apl_core_state *apl_core_state_of_const(const struct apl_core *self)
{
    return (const struct apl_core_state *)(const void *)self->storage.bytes;
}

static uint8_t apl_core_has_purify(const struct apl_core_state *state)
{
#if APL_COMPILE_PURIFY
    return state->purify_enabled;
#else
    (void)state;
    return 0u;
#endif
}

static uint8_t apl_core_has_humidify(const struct apl_core_state *state)
{
#if APL_COMPILE_HUMIDIFY
    return state->humidify_enabled;
#else
    (void)state;
    return 0u;
#endif
}

/** 定时被关机或重新设定打断时，旧回调不能再响。 */
static void apl_core_timer_clear(struct apl_core_state *state)
{
    state->timer_left_s = 0u;
    state->timer_subtick = 0u;
    state->on_timer = NULL;
    state->timer_ctx = NULL;
}

static uint8_t apl_core_gear_is_valid(enum apl_fan_gear gear)
{
    switch (gear)
    {
    case APL_FAN_GEAR_SLEEP:
    case APL_FAN_GEAR_1:
    case APL_FAN_GEAR_2:
    case APL_FAN_GEAR_3:
    case APL_FAN_GEAR_AUTO:
    case APL_FAN_GEAR_TURBO:
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t apl_core_water_is_valid(enum apl_water_level level)
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

static void apl_core_beep(struct apl_core_state *state, enum apl_beep beep)
{
    state->beep = beep;
    state->beep_seq++;
}

/** 睡眠静音时不发提示音。原表：蜂鸣按睡眠静音规则执行。 */
static void apl_core_beep_unless_sleep(struct apl_core_state *state, enum apl_beep beep)
{
    if ((APL_RUN_ON == state->run_state) && (APL_FAN_GEAR_SLEEP == state->fan_gear))
    {
        return;
    }

    apl_core_beep(state, beep);
}

static void apl_core_memory_fill(const struct apl_core_state *state,
                                 struct apl_core_memory *memory)
{
    memset(memory, 0, sizeof(*memory));
    if (APL_RUN_FACTORY == state->run_state)
    {
        memory->filter_purify_s = state->real_filter_purify_s;
        memory->filter_humidify_s = state->real_filter_humidify_s;
    }
    else
    {
        memory->filter_purify_s = state->filter_purify_s;
        memory->filter_humidify_s = state->filter_humidify_s;
    }

    memory->fan_gear = state->fan_gear;
    memory->running = (APL_RUN_ON == state->run_state) ? 1u : 0u;
    memory->used = state->used;
    memory->purify_on = state->purify_on;
    memory->humidify_on = state->humidify_on;
    memory->factory_pending = state->factory_pending;
}

static void apl_core_memory_notify(struct apl_core *self)
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

static uint8_t apl_core_filter_left(uint32_t used_s)
{
    uint64_t left;

    if (used_s >= APL_CORE_FILTER_LIFE_S)
    {
        return (uint8_t)APL_FILTER_EXPIRED;
    }

    /* 向上取整：刚开始用是 99，最后一秒前仍是 1，到期才是 0。 */
    left = ((uint64_t)(APL_CORE_FILTER_LIFE_S - used_s) * APL_CORE_FILTER_LEFT_MAX
            + APL_CORE_FILTER_LIFE_S - 1u) / APL_CORE_FILTER_LIFE_S;
    return (uint8_t)left;
}

static enum apl_aq_level apl_core_aq_grade(uint16_t value,
                                           uint16_t excellent,
                                           uint16_t good,
                                           uint16_t moderate)
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

static enum apl_aq_level apl_core_aq_worse(enum apl_aq_level a, enum apl_aq_level b)
{
    return (b > a) ? b : a;
}

/**
 * 原表多传感器分级。区间端点归较好的一档（待确认：TVOC 0.3、0.5 两档重叠）。
 * 甲醛没有「良」，0.050 以上直接是中度污染。
 */
static void apl_core_sensor_sample(struct apl_core_state *state)
{
    enum apl_aq_level level;

    if (NULL == state->ops)
    {
        return;
    }

    if (0u != apl_core_has_purify(state))
    {
        level = APL_AQ_EXCELLENT;
        if (NULL != state->ops->get_pm25)
        {
            state->pm25 = state->ops->get_pm25(state->ctx);
            level = apl_core_aq_worse(level, apl_core_aq_grade(state->pm25, 35u, 75u, 150u));
        }

        if (NULL != state->ops->get_pm10)
        {
            level = apl_core_aq_worse(level,
                                      apl_core_aq_grade(state->ops->get_pm10(state->ctx),
                                                        40u, 70u, 150u));
        }

        if (NULL != state->ops->get_hcho)
        {
            level = apl_core_aq_worse(level,
                                      apl_core_aq_grade(state->ops->get_hcho(state->ctx),
                                                        50u, 50u, 80u));
        }

        if (NULL != state->ops->get_tvoc)
        {
            level = apl_core_aq_worse(level,
                                      apl_core_aq_grade(state->ops->get_tvoc(state->ctx),
                                                        300u, 500u, 1000u));
        }

        state->aq_level = level;
    }

    if ((0u != apl_core_has_humidify(state)) && (NULL != state->ops->get_humidity))
    {
        state->humidity = state->ops->get_humidity(state->ctx);
    }
}

/** 恒湿风速表。差值 = 设定 − 实际；达到或超过设定仍是一档。 */
static enum apl_fan_gear apl_core_humidity_gear(const struct apl_core_state *state)
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

/** 自动档按空气质量。优一档、良二档、中度和重度三档（待确认：原表未给对应关系）。 */
static enum apl_fan_gear apl_core_aq_gear(const struct apl_core_state *state)
{
    switch (state->aq_level)
    {
    case APL_AQ_EXCELLENT:
        return APL_FAN_GEAR_1;
    case APL_AQ_GOOD:
        return APL_FAN_GEAR_2;
    default:
        return APL_FAN_GEAR_3;
    }
}

static uint8_t apl_core_constant_humidity(const struct apl_core_state *state)
{
    return ((0u != apl_core_has_humidify(state))
            && (0u != state->humidify_on)
            && (APL_HUMIDITY_TARGET_OFF != state->humidity_target)) ? 1u : 0u;
}

static enum apl_fan_gear apl_core_fan_output_compute(const struct apl_core_state *state)
{
    enum apl_fan_gear gear;
    enum apl_fan_gear humidity_gear;

    if (APL_RUN_DRY == state->run_state)
    {
        return (enum apl_fan_gear)APL_CORE_DRY_FAN_GEAR;
    }

    if ((APL_FAN_GEAR_TURBO == state->fan_gear) || (APL_FAN_GEAR_SLEEP == state->fan_gear))
    {
        return state->fan_gear;
    }

    if (0u != apl_core_constant_humidity(state))
    {
        humidity_gear = apl_core_humidity_gear(state);
        if ((APL_FAN_GEAR_AUTO == state->fan_gear) && (0u != apl_core_has_purify(state)))
        {
            gear = apl_core_aq_gear(state);
            return (gear > humidity_gear) ? gear : humidity_gear;
        }

        return humidity_gear;
    }

    if (APL_FAN_GEAR_AUTO == state->fan_gear)
    {
        if (0u != apl_core_has_purify(state))
        {
            return apl_core_aq_gear(state);
        }

        return APL_FAN_GEAR_2;
    }

    return state->fan_gear;
}

/** 达到目标停泵，低于目标一个回差再开泵。不恒湿时一直加湿。 */
static void apl_core_humidity_hold_update(struct apl_core_state *state)
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
    else if (((uint16_t)state->humidity + APL_CORE_HUMIDITY_HYSTERESIS)
             <= state->humidity_target)
    {
        state->humidity_hold = 0u;
    }
}

static void apl_core_outputs_update(struct apl_core *self)
{
    struct apl_core_state *state;
    enum apl_switch fan;
    enum apl_fan_gear output;
    enum apl_switch pump;
    enum apl_switch uv;
    enum apl_switch anion;
    uint8_t running;
    uint8_t interlock;
    const struct apl_core_ops *ops;

    state = apl_core_state_of(self);
    ops = state->ops;
    running = (APL_RUN_ON == state->run_state) ? 1u : 0u;

    fan = ((0u != running) || (APL_RUN_DRY == state->run_state)) ? APL_SWITCH_ON
                                                                 : APL_SWITCH_OFF;
    output = (APL_SWITCH_ON == fan) ? apl_core_fan_output_compute(state) : state->fan_output;

    apl_core_humidity_hold_update(state);
    pump = ((0u != running)
            && (0u != apl_core_has_humidify(state))
            && (0u != state->humidify_on)
            && (APL_FAN_GEAR_TURBO != state->fan_gear)
            && (0u == state->shortage)
            && (0u == state->humidity_hold)) ? APL_SWITCH_ON : APL_SWITCH_OFF;

    interlock = 1u;
    if ((NULL != ops) && (NULL != ops->get_uv_interlock))
    {
        interlock = ops->get_uv_interlock(state->ctx);
    }

    if ((APL_SWITCH_ON == state->uv) && (0u == interlock))
    {
        state->uv_fault = 1u;
        apl_core_beep(state, APL_BEEP_UV_ALARM);
    }

    anion = ((0u != running) && (0u != apl_core_has_purify(state)) && (0u != state->purify_on))
            ? APL_SWITCH_ON : APL_SWITCH_OFF;
    uv = ((APL_SWITCH_ON == anion) && (0u != interlock) && (0u == state->uv_fault))
         ? APL_SWITCH_ON : APL_SWITCH_OFF;

    if ((0u == state->outputs_valid) || (fan != state->fan) || (output != state->fan_output))
    {
        state->fan = fan;
        state->fan_output = output;
        /* 第一拍风机本来就停着，不必再发一次「关」。 */
        if ((NULL != ops) && (NULL != ops->set_fan)
            && ((0u != state->outputs_valid) || (APL_SWITCH_ON == fan)))
        {
            ops->set_fan(fan, output, state->ctx);
        }
    }

    if (pump != state->pump)
    {
        state->pump = pump;
        if ((NULL != ops) && (NULL != ops->set_pump))
        {
            ops->set_pump(pump, state->ctx);
        }
    }

    if (uv != state->uv)
    {
        state->uv = uv;
        if ((NULL != ops) && (NULL != ops->set_uv))
        {
            ops->set_uv(uv, state->ctx);
        }
    }

    if (anion != state->anion)
    {
        state->anion = anion;
        if ((NULL != ops) && (NULL != ops->set_anion))
        {
            ops->set_anion(anion, state->ctx);
        }
    }

    state->outputs_valid = 1u;
}

static void apl_core_shortage_start(struct apl_core_state *state)
{
    if ((0u != state->shortage) || (0u == apl_core_has_humidify(state)))
    {
        return;
    }

    state->shortage = 1u;
    state->shortage_s = 0u;
    state->water_alert = APL_WATER_ALERT_BLINK;
    apl_core_beep_unless_sleep(state, APL_BEEP_WATER_SHORTAGE);
}

static void apl_core_shortage_clear(struct apl_core_state *state)
{
    state->shortage = 0u;
    state->shortage_s = 0u;
    state->water_alert = APL_WATER_ALERT_NONE;
}

/**
 * 第一次读数只建立边沿起点。没有上一拍时，无从判断是下降还是上升。
 * 非法段数丢掉，避免一次坏读数把起点改掉。
 */
static void apl_core_water_sample(struct apl_core *self)
{
    struct apl_core_state *state;
    enum apl_water_level level;
    enum apl_water_level previous;

    state = apl_core_state_of(self);
    if ((NULL == state->ops) || (NULL == state->ops->get_water_level))
    {
        return;
    }

    level = state->ops->get_water_level(state->ctx);
    if (0u == apl_core_water_is_valid(level))
    {
        return;
    }

    if (0u == state->water_valid)
    {
        state->water_level = level;
        state->water_valid = 1u;
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

/** 关机期间也计时，这样到时回调可以负责开机。 */
static void apl_core_timer_tick(struct apl_core *self)
{
    struct apl_core_state *state;
    void (*on_timer)(struct apl_core *timer_self, void *timer_ctx);
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

static void apl_core_dry_start(struct apl_core *self)
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    state->run_state = APL_RUN_DRY;
    state->dry_left_s = APL_CORE_DRY_S;
    apl_core_timer_clear(state);
    apl_core_shortage_clear(state);
    if (APL_WIFI_PAIRING == state->wifi)
    {
        state->wifi = APL_WIFI_OFF;
    }

    apl_core_memory_notify(self);
}

/** 恢复出厂设置。真实滤网寿命不清零，产测注入值丢掉。 */
static void apl_core_factory_restore(struct apl_core *self)
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    if (APL_RUN_FACTORY == state->run_state)
    {
        state->filter_purify_s = state->real_filter_purify_s;
        state->filter_humidify_s = state->real_filter_humidify_s;
    }

    state->run_state = APL_RUN_STANDBY;
    state->power = APL_POWER_OFF;
    state->fan_gear = (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
    state->gear_before_turbo = state->fan_gear;
    state->gear_before_sleep = state->fan_gear;
    state->wifi = APL_WIFI_OFF;
    state->purify_on = 0u;
    state->humidify_on = 1u;
    state->humidity_target = APL_HUMIDITY_TARGET_OFF;
    state->used = 0u;
    state->factory_pending = 0u;
    state->factory_done = 1u;
    state->cl_left_s = APL_CORE_FACTORY_CL_S;
    state->uv_fault = 0u;
    apl_core_timer_clear(state);
    apl_core_shortage_clear(state);
    apl_core_memory_notify(self);
    if ((NULL != state->ops) && (NULL != state->ops->on_power_off))
    {
        state->ops->on_power_off(self, state->ctx);
    }
}

static void apl_core_second(struct apl_core *self)
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    if (state->uptime_s < UINT32_MAX)
    {
        state->uptime_s++;
    }

    if (0u != state->cl_left_s)
    {
        state->cl_left_s--;
        if (0u == state->cl_left_s)
        {
            state->factory_done = 0u;
        }
    }

    if (0u != state->wifi_left_s)
    {
        state->wifi_left_s--;
        if ((0u == state->wifi_left_s) && (APL_WIFI_PAIRING == state->wifi))
        {
            state->wifi = APL_WIFI_OFF;
        }
    }

    if ((APL_RUN_ON == state->run_state) || (APL_RUN_FACTORY == state->run_state))
    {
        apl_core_sensor_sample(state);
        if ((0u != apl_core_has_purify(state)) && (state->filter_purify_s < UINT32_MAX))
        {
            state->filter_purify_s++;
        }

        if ((0u != apl_core_has_humidify(state)) && (state->filter_humidify_s < UINT32_MAX))
        {
            state->filter_humidify_s++;
        }

        /* 一小时存一次，避免每秒写存储。 */
        if ((APL_RUN_ON == state->run_state)
            && ((0u == (state->filter_purify_s % APL_CORE_HOUR_S))
                || (0u == (state->filter_humidify_s % APL_CORE_HOUR_S))))
        {
            apl_core_memory_notify(self);
        }
    }

    if ((APL_RUN_ON == state->run_state) && (0u != state->shortage))
    {
        state->shortage_s++;
        if (state->shortage_s >= APL_CORE_WATER_BLINK_S)
        {
            state->water_alert = APL_WATER_ALERT_STEADY;
        }

        if (state->shortage_s >= APL_CORE_WATER_SHORTAGE_RUN_S)
        {
            apl_core_dry_start(self);
            state->water_alert = APL_WATER_ALERT_STEADY;
        }
    }

    if (APL_RUN_DRY == state->run_state)
    {
        if (0u != state->dry_left_s)
        {
            state->dry_left_s--;
        }

        if (0u == state->dry_left_s)
        {
            apl_core_power_off(self);
        }
    }
}

/** 面板定时到期：整机关机进入待机。 */
static void apl_core_panel_timer_expire(struct apl_core *self, void *ctx)
{
    (void)ctx;
    apl_core_power_off(self);
}

void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx)
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    memset(self, 0, sizeof(*self));
    state = apl_core_state_of(self);
    state->ops = ops;
    state->ctx = ctx;
    state->power = APL_POWER_OFF;
    state->run_state = APL_RUN_STANDBY;
    state->fan_gear = (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
    state->gear_before_turbo = state->fan_gear;
    state->gear_before_sleep = state->fan_gear;
    state->fan_output = state->fan_gear;
    state->fan = APL_SWITCH_OFF;
    state->uv = APL_SWITCH_OFF;
    state->anion = APL_SWITCH_OFF;
    state->pump = APL_SWITCH_OFF;
    state->humidify_on = 1u;
    state->humidity_target = APL_HUMIDITY_TARGET_OFF;
#if APL_COMPILE_PURIFY
    state->purify_enabled = (uint8_t)APL_DEFAULT_PURIFY;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidify_enabled = (uint8_t)APL_DEFAULT_HUMIDIFY;
#endif
}

void apl_core_tick(struct apl_core *self)
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of(self);
    apl_core_timer_tick(self);
    apl_core_water_sample(self);

    state->second_subtick++;
    if (state->second_subtick >= APL_CORE_TICKS_PER_SECOND)
    {
        state->second_subtick = 0u;
        apl_core_second(self);
    }

    apl_core_outputs_update(self);
}

void apl_core_power_on(struct apl_core *self)
{
    struct apl_core_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of(self);
    if (0u == state->used)
    {
        state->fan_gear = (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
        state->used = 1u;
    }

    state->power = APL_POWER_ON;
    state->run_state = APL_RUN_ON;
    state->dry_left_s = 0u;
    state->factory_done = 0u;
    state->cl_left_s = 0u;
    if ((0u != state->water_valid) && (APL_WATER_LEVEL_0 == state->water_level))
    {
        apl_core_shortage_start(state);
    }

    apl_core_memory_notify(self);
}

void apl_core_power_off(struct apl_core *self)
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
    state->dry_left_s = 0u;
    state->humidity_target = APL_HUMIDITY_TARGET_OFF;
    if (APL_FAN_GEAR_TURBO == state->fan_gear)
    {
        state->fan_gear = state->gear_before_turbo;
    }

    if (APL_WIFI_PAIRING == state->wifi)
    {
        state->wifi = APL_WIFI_OFF;
    }

    apl_core_shortage_clear(state);
    apl_core_timer_clear(state);
    apl_core_memory_notify(self);
    if ((NULL != state->ops) && (NULL != state->ops->on_power_off))
    {
        state->ops->on_power_off(self, state->ctx);
    }
}

enum apl_power apl_core_get_power(const struct apl_core *self)
{
    if (NULL == self)
    {
        return APL_POWER_OFF;
    }

    return apl_core_state_of_const(self)->power;
}

enum apl_run_state apl_core_get_run_state(const struct apl_core *self)
{
    if (NULL == self)
    {
        return APL_RUN_STANDBY;
    }

    return apl_core_state_of_const(self)->run_state;
}

void apl_core_set_timer(struct apl_core *self,
                        uint32_t seconds,
                        void (*on_timer)(struct apl_core *timer_self, void *timer_ctx),
                        void *ctx)
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

uint32_t apl_core_get_timer(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->timer_left_s;
}

void apl_core_set_fan_gear(struct apl_core *self, enum apl_fan_gear gear)
{
    if ((NULL == self) || (0u == apl_core_gear_is_valid(gear)))
    {
        return;
    }

    apl_core_state_of(self)->fan_gear = gear;
}

enum apl_fan_gear apl_core_get_fan_gear(const struct apl_core *self)
{
    if (NULL == self)
    {
        return (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
    }

    return apl_core_state_of_const(self)->fan_gear;
}

static enum apl_fan_gear apl_core_gear_next(enum apl_fan_gear gear)
{
    switch (gear)
    {
    case APL_FAN_GEAR_SLEEP:
        return APL_FAN_GEAR_1;
    case APL_FAN_GEAR_1:
        return APL_FAN_GEAR_2;
    case APL_FAN_GEAR_2:
        return APL_FAN_GEAR_3;
    case APL_FAN_GEAR_3:
        return APL_FAN_GEAR_AUTO;
    default:
        return APL_FAN_GEAR_SLEEP;
    }
}

static enum apl_link_result apl_core_cmd_power(struct apl_core *self)
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    switch (state->run_state)
    {
    case APL_RUN_STANDBY:
    case APL_RUN_DRY:
        /* 风干是关机过程；用户再次短按电源时恢复运行。 */
        apl_core_power_on(self);
        return APL_LINK_OK;
    case APL_RUN_ON:
        if (0u != apl_core_has_humidify(state))
        {
            apl_core_dry_start(self);
        }
        else
        {
            apl_core_power_off(self);
        }
        return APL_LINK_OK;
    default:
        return APL_LINK_REJECTED;
    }
}

static enum apl_link_result apl_core_cmd_fan(struct apl_core *self,
                                             const struct apl_link_command *command)
{
    struct apl_core_state *state;
    enum apl_fan_gear base;

    state = apl_core_state_of(self);
    if (APL_RUN_ON != state->run_state)
    {
        return APL_LINK_REJECTED;
    }

    switch (command->type)
    {
    case APL_LINK_CMD_FAN_CYCLE:
        base = (APL_FAN_GEAR_TURBO == state->fan_gear) ? state->gear_before_turbo
                                                         : state->fan_gear;
        state->fan_gear = apl_core_gear_next(base);
        break;
    case APL_LINK_CMD_FAN_SET:
        if ((0u == apl_core_gear_is_valid((enum apl_fan_gear)command->arg))
            || ((0u == APL_CORE_HAS_TURBO) && (APL_FAN_GEAR_TURBO == command->arg)))
        {
            return APL_LINK_REJECTED;
        }
        state->fan_gear = (enum apl_fan_gear)command->arg;
        break;
    case APL_LINK_CMD_TURBO_TOGGLE:
        if (0u == APL_CORE_HAS_TURBO)
        {
            return APL_LINK_REJECTED;
        }
        if (APL_FAN_GEAR_TURBO == state->fan_gear)
        {
            state->fan_gear = state->gear_before_turbo;
        }
        else
        {
            state->gear_before_turbo = state->fan_gear;
            state->fan_gear = APL_FAN_GEAR_TURBO;
        }
        break;
    case APL_LINK_CMD_SLEEP_TOGGLE:
        if (APL_FAN_GEAR_SLEEP == state->fan_gear)
        {
            state->fan_gear = (APL_FAN_GEAR_SLEEP == state->gear_before_sleep)
                              ? (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR
                              : state->gear_before_sleep;
        }
        else
        {
            state->gear_before_sleep = state->fan_gear;
            state->fan_gear = APL_FAN_GEAR_SLEEP;
        }
        break;
    default:
        return APL_LINK_REJECTED;
    }

    apl_core_memory_notify(self);
    return APL_LINK_OK;
}

static uint8_t apl_core_target_is_valid(uint8_t target)
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

static uint8_t apl_core_water_ok(const struct apl_core_state *state)
{
    return ((0u == state->water_valid) || (APL_WATER_LEVEL_0 != state->water_level)) ? 1u : 0u;
}

static uint8_t apl_core_on_or_standby(const struct apl_core_state *state)
{
    return ((APL_RUN_ON == state->run_state) || (APL_RUN_STANDBY == state->run_state))
           ? 1u : 0u;
}

static enum apl_link_result apl_core_cmd_filter_reset(struct apl_core *self)
{
    struct apl_core_state *state;
    uint8_t purify;
    uint8_t humidify;
    uint8_t expired;

    state = apl_core_state_of(self);
    if (0u == apl_core_on_or_standby(state))
    {
        return APL_LINK_REJECTED;
    }

    purify = apl_core_has_purify(state);
    humidify = apl_core_has_humidify(state);
    if ((0u == purify) && (0u == humidify))
    {
        return APL_LINK_REJECTED;
    }

    expired = 0u;
    if ((0u != purify) && (state->filter_purify_s >= APL_CORE_FILTER_LIFE_S))
    {
        expired = 1u;
    }

    if ((0u != humidify) && (state->filter_humidify_s >= APL_CORE_FILTER_LIFE_S))
    {
        expired = 1u;
    }

    /* 一体机共用计时时两套一起清零；否则只清启用的那一套。 */
    if ((0u != purify) && (0u != humidify) && (0u == APL_CORE_FILTER_SHARED))
    {
        if (state->filter_purify_s >= state->filter_humidify_s)
        {
            state->filter_purify_s = 0u;
        }
        else
        {
            state->filter_humidify_s = 0u;
        }
    }
    else
    {
        if (0u != purify)
        {
            state->filter_purify_s = 0u;
        }

        if (0u != humidify)
        {
            state->filter_humidify_s = 0u;
        }
    }

    state->filter_reset_forced = (0u == expired) ? 1u : 0u;
    state->filter_reset_seq++;
    apl_core_beep_unless_sleep(state, (0u == expired) ? APL_BEEP_FILTER_FORCED
                                                      : APL_BEEP_FILTER_RESET);
    apl_core_memory_notify(self);
    return APL_LINK_OK;
}

static enum apl_link_result apl_core_cmd_factory_enter(struct apl_core *self)
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    if ((APL_RUN_STANDBY != state->run_state) || (state->uptime_s >= APL_CORE_FACTORY_WINDOW_S))
    {
        return APL_LINK_REJECTED;
    }

    state->factory_pending = 1u;
    state->real_filter_purify_s = state->filter_purify_s;
    state->real_filter_humidify_s = state->filter_humidify_s;
    state->run_state = APL_RUN_FACTORY;
    state->power = APL_POWER_ON;
    state->filter_purify_s = APL_CORE_FILTER_LIFE_S - APL_CORE_FACTORY_FILTER_LEFT_S;
    state->filter_humidify_s = APL_CORE_FILTER_LIFE_S - APL_CORE_FACTORY_FILTER_LEFT_S;
    apl_core_beep(state, APL_BEEP_FACTORY);
    apl_core_memory_notify(self);
    return APL_LINK_OK;
}

enum apl_link_result apl_core_command(struct apl_core *self,
                                      const struct apl_link_command *command)
{
    struct apl_core_state *state;

    if ((NULL == self) || (NULL == command))
    {
        return APL_LINK_REJECTED;
    }

    state = apl_core_state_of(self);
    switch (command->type)
    {
    case APL_LINK_CMD_POWER:
        return apl_core_cmd_power(self);
    case APL_LINK_CMD_DRY_STOP:
        if (APL_RUN_DRY != state->run_state)
        {
            return APL_LINK_REJECTED;
        }
        apl_core_power_off(self);
        return APL_LINK_OK;
    case APL_LINK_CMD_FAN_CYCLE:
    case APL_LINK_CMD_FAN_SET:
    case APL_LINK_CMD_TURBO_TOGGLE:
    case APL_LINK_CMD_SLEEP_TOGGLE:
        return apl_core_cmd_fan(self, command);
    case APL_LINK_CMD_TIMER_SET:
        if ((APL_RUN_ON != state->run_state) || (command->arg > APL_CORE_TIMER_MAX_H))
        {
            return APL_LINK_REJECTED;
        }
        apl_core_set_timer(self, (uint32_t)command->arg * APL_CORE_HOUR_S,
                           apl_core_panel_timer_expire, NULL);
        return APL_LINK_OK;
    case APL_LINK_CMD_PURIFY_TOGGLE:
        if ((0u == apl_core_has_purify(state)) || (0u == apl_core_on_or_standby(state)))
        {
            return APL_LINK_REJECTED;
        }
        state->purify_on = (0u == state->purify_on) ? 1u : 0u;
        state->uv_fault = 0u;
        apl_core_memory_notify(self);
        return APL_LINK_OK;
    case APL_LINK_CMD_HUMIDIFY_TOGGLE:
        if ((0u == apl_core_has_humidify(state)) || (0u == apl_core_on_or_standby(state))
            || ((0u == state->humidify_on) && (0u == apl_core_water_ok(state))))
        {
            return APL_LINK_REJECTED;
        }
        state->humidify_on = (0u == state->humidify_on) ? 1u : 0u;
        apl_core_memory_notify(self);
        return APL_LINK_OK;
    case APL_LINK_CMD_HUMIDITY_TARGET:
        if ((0u == apl_core_has_humidify(state)) || (0u == apl_core_on_or_standby(state))
            || (0u == apl_core_target_is_valid(command->arg))
            || ((APL_HUMIDITY_TARGET_OFF != command->arg) && (0u == apl_core_water_ok(state))))
        {
            return APL_LINK_REJECTED;
        }
        state->humidity_target = command->arg;
        return APL_LINK_OK;
    case APL_LINK_CMD_FILTER_RESET:
        return apl_core_cmd_filter_reset(self);
    case APL_LINK_CMD_WIFI_PAIR:
        if (0u == apl_core_on_or_standby(state))
        {
            return APL_LINK_REJECTED;
        }
        state->wifi = APL_WIFI_PAIRING;
        state->wifi_left_s = APL_CORE_WIFI_PAIR_S;
        return APL_LINK_OK;
    case APL_LINK_CMD_FACTORY_ENTER:
        return apl_core_cmd_factory_enter(self);
    case APL_LINK_CMD_FACTORY_EXIT:
        if (APL_RUN_FACTORY != state->run_state)
        {
            return APL_LINK_REJECTED;
        }
        apl_core_factory_restore(self);
        return APL_LINK_OK;
    default:
        return APL_LINK_REJECTED;
    }
}

void apl_core_get_snapshot(const struct apl_core *self, struct apl_link_snapshot *snapshot)
{
    const struct apl_core_state *state;

    if (NULL == snapshot)
    {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (NULL == self)
    {
        return;
    }

    state = apl_core_state_of_const(self);
    snapshot->run_state = state->run_state;
    snapshot->fan_gear = state->fan_gear;
    snapshot->fan_output = state->fan_output;
    snapshot->water_alert = state->water_alert;
    snapshot->wifi = state->wifi;
    snapshot->uv = state->uv;
    snapshot->anion = state->anion;
    snapshot->pump = state->pump;
    snapshot->beep = state->beep;
    snapshot->beep_seq = state->beep_seq;
    snapshot->timer_left_s = state->timer_left_s;
    snapshot->dry_left_s = state->dry_left_s;
    snapshot->sleep = ((APL_RUN_ON == state->run_state)
                       && (APL_FAN_GEAR_SLEEP == state->fan_gear)) ? 1u : 0u;
    snapshot->filter_reset_seq = state->filter_reset_seq;
    snapshot->filter_reset_forced = state->filter_reset_forced;
    snapshot->factory_done = state->factory_done;
    if (0u != apl_core_has_purify(state))
    {
        snapshot->purify_enabled = 1u;
        snapshot->purify_on = state->purify_on;
        snapshot->pm25 = state->pm25;
        snapshot->aq_level = state->aq_level;
        snapshot->uv_fault = state->uv_fault;
        snapshot->filter_purify_left = apl_core_filter_left(state->filter_purify_s);
    }

    if (0u != apl_core_has_humidify(state))
    {
        snapshot->humidify_enabled = 1u;
        snapshot->humidify_on = state->humidify_on;
        snapshot->humidity = state->humidity;
        snapshot->humidity_target = state->humidity_target;
        snapshot->water_level = state->water_level;
        snapshot->filter_humidify_left = apl_core_filter_left(state->filter_humidify_s);
    }
}

void apl_core_get_memory(const struct apl_core *self, struct apl_core_memory *memory)
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

void apl_core_memory_restore(struct apl_core *self, const struct apl_core_memory *memory)
{
    struct apl_core_state *state;

    if ((NULL == self) || (NULL == memory))
    {
        return;
    }

    state = apl_core_state_of(self);
    state->filter_purify_s = memory->filter_purify_s;
    state->filter_humidify_s = memory->filter_humidify_s;
    if (0u != memory->factory_pending)
    {
        apl_core_factory_restore(self);
        return;
    }

    if ((0u != apl_core_gear_is_valid(memory->fan_gear))
        && (APL_FAN_GEAR_TURBO != memory->fan_gear))
    {
        state->fan_gear = memory->fan_gear;
    }

    state->used = (0u != memory->used) ? 1u : 0u;
    state->purify_on = (0u != memory->purify_on) ? 1u : 0u;
    state->humidify_on = (0u != memory->humidify_on) ? 1u : 0u;
    if ((0u != memory->running) && (0u != state->used))
    {
        apl_core_power_on(self);
    }
}

void apl_core_set_wifi_connected(struct apl_core *self, uint8_t connected)
{
    struct apl_core_state *state;

    if ((NULL == self) || (connected > 1u))
    {
        return;
    }

    state = apl_core_state_of(self);
    state->wifi = (0u != connected) ? APL_WIFI_CONNECTED : APL_WIFI_OFF;
    state->wifi_left_s = 0u;
}

#if APL_COMPILE_PURIFY
void apl_core_filter_purify_reset(struct apl_core *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_purify_s = 0u;
}

uint32_t apl_core_get_filter_purify(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_purify_s;
}

uint8_t apl_core_get_purify_enabled(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->purify_enabled;
}
#endif

#if APL_COMPILE_HUMIDIFY
void apl_core_filter_humidify_reset(struct apl_core *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_humidify_s = 0u;
}

uint32_t apl_core_get_filter_humidify(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_humidify_s;
}

uint8_t apl_core_get_humidify_enabled(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->humidify_enabled;
}
#endif
