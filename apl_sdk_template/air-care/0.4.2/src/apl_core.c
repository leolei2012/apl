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

APL_STATIC_ASSERT(APL_CORE_TICKS_PER_SECOND == 100u, "10 ms 一拍时，1 秒是 100 拍");
APL_STATIC_ASSERT(APL_CORE_VERSION_NUM == 0x000202u, "版本号与分段宏不一致");

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
    void (*on_timer)(struct apl_core *self, void *ctx) APL_CALL;
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
    uint8_t water_sample_ok; /**< 本拍水位是否有效；边沿起点仍保留最后有效值。 */
    uint8_t outputs_valid;
    uint8_t purify_enabled;
    uint8_t humidify_enabled;
    uint8_t purify_on;
    uint8_t uv_requested;
    uint8_t anion_requested;
    uint8_t module_faults;
    uint8_t sensors_ready;
    uint8_t sensors_warming;
    uint8_t sensor_faults;
    uint8_t aq_ready;
    uint8_t humidify_on;
    uint8_t humidity;
    uint8_t humidity_sampled;
    uint8_t humidity_target;
    uint8_t humidity_hold;
    uint8_t shortage;
    uint8_t dry_needed; /**< 本次运行已提交过泵开启请求；不持久化。 */
    uint8_t uv_fault;
    uint8_t used;
    uint8_t factory_pending;
    uint8_t factory_done;
    uint8_t beep_seq;
    uint8_t filter_reset_seq;
    uint8_t filter_reset_forced;
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

static uint8_t apl_core_has_purify(const struct apl_core_state *state) APL_CALL
{
#if APL_COMPILE_PURIFY
    return state->purify_enabled;
#else
    APL_UNUSED_PARAM(state);
    return 0u;
#endif
}

static uint8_t apl_core_has_humidify(const struct apl_core_state *state) APL_CALL
{
#if APL_COMPILE_HUMIDIFY
    return state->humidify_enabled;
#else
    APL_UNUSED_PARAM(state);
    return 0u;
#endif
}

/** 定时被关机或重新设定打断时，旧回调不能再响。 */
static void apl_core_timer_clear(struct apl_core_state *state) APL_CALL
{
    state->timer_left_s = 0u;
    state->timer_subtick = 0u;
    state->on_timer = NULL;
    state->timer_ctx = NULL;
}

static uint8_t apl_core_gear_is_valid(enum apl_fan_gear gear) APL_CALL
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

static void apl_core_beep(struct apl_core_state *state, enum apl_beep beep) APL_CALL
{
    state->beep = beep;
    state->beep_seq++;
}

/** 睡眠静音时不发提示音。原表：蜂鸣按睡眠静音规则执行。 */
static void apl_core_beep_unless_sleep(struct apl_core_state *state, enum apl_beep beep) APL_CALL
{
    if ((APL_RUN_ON == state->run_state) && (APL_FAN_GEAR_SLEEP == state->fan_gear))
    {
        return;
    }

    apl_core_beep(state, beep);
}

static void apl_core_memory_fill(const struct apl_core_state *state,
                                 struct apl_core_memory *memory) APL_CALL
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
    memory->module_state_valid = 1u;
    memory->uv_requested = state->uv_requested;
    memory->anion_requested = state->anion_requested;
}

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

static uint8_t apl_core_filter_left(uint32_t used_s) APL_CALL
{
    uint32_t remaining;
    uint32_t gap;
    uint32_t remainder = 0u;
    uint8_t quotient = 0u;
    uint8_t index;

    if (used_s >= APL_CORE_FILTER_LIFE_S)
    {
        return (uint8_t)APL_FILTER_EXPIRED;
    }

    remaining = APL_CORE_FILTER_LIFE_S - used_s;
    gap = used_s;
    /** Exact ceil(remaining * 99 / life), with no overflowing 32-bit product.
     * In both branches remainder stays below life, even at maximum lifetime. */
    for (index = 0u; index < APL_CORE_FILTER_LEFT_MAX; index++)
    {
        if (remainder >= gap)
        {
            remainder -= gap;
            quotient++;
        }
        else
        {
            remainder += remaining;
        }
    }
    if (remainder != 0u)
    {
        quotient++;
    }
    return quotient;
}

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

    if (0u == available)
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

static void apl_core_sensor_sample(struct apl_core_state *state) APL_CALL
{
    enum apl_aq_level level;
    uint8_t previous_faults;
    uint8_t aq_inputs;

    previous_faults = state->sensor_faults;
    state->sensors_ready = 0u;
    state->sensors_warming = 0u;
    state->sensor_faults = 0u;
    state->aq_ready = 0u;
    state->humidity_sampled = 0u;
    if (NULL == state->ops)
    {
        return;
    }
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
#if APL_COMPILE_SENSOR_HUMIDITY
    if ((0u != apl_core_has_humidify(state))
        && (APL_SENSOR_VALID == apl_core_sensor_state(state, APL_SENSOR_HUMIDITY,
                                                       (uint8_t)(NULL != state->ops->get_humidity))))
    {
        state->humidity = state->ops->get_humidity(state->ctx);
        if (state->humidity <= 100u)
        {
            state->humidity_sampled = 1u;
        }
        else
        {
            state->sensors_ready &= (uint8_t)~APL_SENSOR_HUMIDITY_MASK;
            state->sensor_faults |= APL_SENSOR_HUMIDITY_MASK;
        }
    }
#endif
    aq_inputs = (uint8_t)((state->sensors_ready | state->sensors_warming | state->sensor_faults) & 15u);
    state->aq_ready = ((0u != aq_inputs) && (aq_inputs == (state->sensors_ready & 15u))) ? 1u : 0u;
    if (0u != (state->sensor_faults & (uint8_t)~previous_faults))
    {
        apl_core_beep_unless_sleep(state, APL_BEEP_SHORT);
    }
}

static enum apl_fan_gear apl_core_preprocess_gear(void) APL_CALL
{
#if (APL_DEFAULT_FAN_GEAR <= 3u)
    return (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
#else
    return (enum apl_fan_gear)APL_CORE_SENSOR_FALLBACK_GEAR;
#endif
}

/** 恒湿风速表。差值 = 设定 − 实际；达到或超过设定仍是一档。 */
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

/** 纯净化按标准化表输出睡眠/一/二/三档；带加湿能力的映射暂保留原规则。 */
static enum apl_fan_gear apl_core_aq_gear(const struct apl_core_state *state) APL_CALL
{
    switch (state->aq_level)
    {
    case APL_AQ_EXCELLENT:
        return (0u != apl_core_has_humidify(state)) ? APL_FAN_GEAR_1 : APL_FAN_GEAR_SLEEP;
    case APL_AQ_GOOD:
        return (0u != apl_core_has_humidify(state)) ? APL_FAN_GEAR_2 : APL_FAN_GEAR_1;
    case APL_AQ_MODERATE:
        return (0u != apl_core_has_humidify(state)) ? APL_FAN_GEAR_3 : APL_FAN_GEAR_2;
    default:
        return APL_FAN_GEAR_3;
    }
}

static uint8_t apl_core_constant_humidity(const struct apl_core_state *state) APL_CALL
{
    return ((0u != apl_core_has_humidify(state))
            && (0u != state->humidify_on)
            && (0u != (state->sensors_ready & APL_SENSOR_HUMIDITY_MASK))
            && (APL_HUMIDITY_TARGET_OFF != state->humidity_target)) ? 1u : 0u;
}

/** 加湿器交互逻辑 C9:E9。已读取不等于传感器有效，后者仍由宿主保证。 */
static enum apl_fan_gear apl_core_humidify_auto_gear(const struct apl_core_state *state) APL_CALL
{
    /* 没有样本、回调或整数百分比时保留旧输出，不在此定义故障策略。 */
    if ((0u == state->humidity_sampled) || (NULL == state->ops)
        || (NULL == state->ops->get_humidity) || (state->humidity > 100u))
    {
        return (enum apl_fan_gear)APL_CORE_SENSOR_FALLBACK_GEAR;
    }

    if (state->humidity <= 30u)
    {
        return APL_FAN_GEAR_3;
    }

    return (state->humidity <= 50u) ? APL_FAN_GEAR_2 : APL_FAN_GEAR_1;
}

static enum apl_fan_gear apl_core_fan_output_compute(const struct apl_core_state *state) APL_CALL
{
    enum apl_fan_gear gear;
    enum apl_fan_gear humidity_gear;

    if (APL_RUN_DRY == state->run_state)
    {
        if ((0u == apl_core_has_purify(state)) && (APL_FAN_GEAR_SLEEP == state->fan_gear))
        {
            return APL_FAN_GEAR_SLEEP;
        }
        return (enum apl_fan_gear)APL_CORE_DRY_FAN_GEAR;
    }

    if ((APL_FAN_GEAR_TURBO == state->fan_gear) || (APL_FAN_GEAR_SLEEP == state->fan_gear))
    {
        return state->fan_gear;
    }

    if (APL_FAN_GEAR_AUTO == state->fan_gear)
    {
        if ((0u != apl_core_has_purify(state)) && (0u == state->aq_ready))
        {
            return (0u != (state->sensors_warming & 15u)) ? apl_core_preprocess_gear()
                                                        : (enum apl_fan_gear)APL_CORE_SENSOR_FALLBACK_GEAR;
        }
        if ((0u == apl_core_has_purify(state)) && (0u != apl_core_has_humidify(state))
            && (0u == (state->sensors_ready & APL_SENSOR_HUMIDITY_MASK)))
        {
            return (0u != (state->sensors_warming & APL_SENSOR_HUMIDITY_MASK)) ? apl_core_preprocess_gear()
                                                                           : (enum apl_fan_gear)APL_CORE_SENSOR_FALLBACK_GEAR;
        }
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

        return apl_core_humidify_auto_gear(state);
    }

    return state->fan_gear;
}

/** 达到目标停泵，低于目标一个回差再开泵。不恒湿时一直加湿。 */
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
    else if (((uint16_t)state->humidity + APL_CORE_HUMIDITY_HYSTERESIS)
             <= state->humidity_target)
    {
        state->humidity_hold = 0u;
    }
}

#if (APL_COMPILE_UV || APL_COMPILE_ANION || APL_COMPILE_WIFI)
static uint8_t apl_core_module_fault(struct apl_core_state *state,
                                      enum apl_module module) APL_CALL
{
    uint8_t fault;
    uint8_t mask;

    fault = 0u;
    mask = (uint8_t)(1u << (unsigned)module);
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
    enum apl_switch pump;
    enum apl_switch uv;
    enum apl_switch anion;
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

    if (targets->pump != state->pump)
    {
        state->pump = targets->pump;
        if (APL_SWITCH_ON == targets->pump)
        {
            /* E17/E21：依据提交的负载状态，不以功能启用或模式代替运行历史。
             * 硬件是否成功执行仍由port负责；本接口没有执行确认反馈。 */
            state->dry_needed = 1u;
        }
        if ((NULL != ops) && (NULL != ops->set_pump))
        {
            ops->set_pump(targets->pump, state->ctx);
        }
    }

    if (targets->uv != state->uv)
    {
        state->uv = targets->uv;
        if ((NULL != ops) && (NULL != ops->set_uv))
        {
            ops->set_uv(targets->uv, state->ctx);
        }
    }

    if (targets->anion != state->anion)
    {
        state->anion = targets->anion;
        if ((NULL != ops) && (NULL != ops->set_anion))
        {
            ops->set_anion(targets->anion, state->ctx);
        }
    }

    state->outputs_valid = 1u;
}

/** 保留原有策略及副作用的计算时机，再交由统一执行器边界提交。 */
static void apl_core_outputs_update(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;
    struct apl_core_output_targets targets;
    uint8_t running;
    uint8_t interlock;
    uint8_t uv_fault;
    uint8_t anion_fault;
    const struct apl_core_ops *ops;

    state = apl_core_state_of(self);
    ops = state->ops;
    running = (APL_RUN_ON == state->run_state) ? 1u : 0u;

    targets.fan = ((0u != running) || (APL_RUN_DRY == state->run_state)) ? APL_SWITCH_ON
                                                                    : APL_SWITCH_OFF;
    targets.fan_gear = (APL_SWITCH_ON == targets.fan) ? apl_core_fan_output_compute(state)
                                                    : state->fan_output;

    apl_core_humidity_hold_update(state);
    targets.pump = ((0u != running)
                    && (0u != apl_core_has_humidify(state))
                    && (0u != state->humidify_on)
                    && (0u != state->water_sample_ok)
                    && (APL_WATER_LEVEL_0 != state->water_level)
                    && (APL_FAN_GEAR_TURBO != state->fan_gear)
                    && (0u == state->shortage)
                    && (0u == state->humidity_hold)
                    && ((APL_HUMIDITY_TARGET_OFF == state->humidity_target)
                        || (0u != (state->sensors_ready & APL_SENSOR_HUMIDITY_MASK))))
                   ? APL_SWITCH_ON : APL_SWITCH_OFF;

    interlock = 0u;
    uv_fault = 0u;
    anion_fault = 0u;
#if APL_COMPILE_UV
    if ((NULL != ops) && (NULL != ops->get_uv_interlock))
    {
        interlock = (1u == ops->get_uv_interlock(state->ctx)) ? 1u : 0u;
    }
    uv_fault = apl_core_module_fault(state, APL_MODULE_UV);
#endif
#if APL_COMPILE_ANION
    anion_fault = apl_core_module_fault(state, APL_MODULE_ANION);
#endif
#if APL_COMPILE_WIFI
    if (0u != apl_core_module_fault(state, APL_MODULE_WIFI))
    {
        state->wifi = APL_WIFI_OFF;
        state->wifi_left_s = 0u;
    }
#endif

    if (((APL_SWITCH_ON == state->uv) && (0u == interlock))
        || ((0u != running) && (0u != uv_fault) && (0u != state->uv_requested) && (0u == state->uv_fault)))
    {
        state->uv_fault = 1u;
        apl_core_beep(state, APL_BEEP_UV_ALARM);
    }

#if APL_COMPILE_ANION
    targets.anion = ((0u != running)
                     && (0u != state->anion_requested) && (0u == anion_fault))
                    ? APL_SWITCH_ON : APL_SWITCH_OFF;
#else
    APL_UNUSED_PARAM(anion_fault);
    targets.anion = APL_SWITCH_OFF;
#endif
#if APL_COMPILE_UV
    targets.uv = ((0u != running)
                  && (0u != state->uv_requested) && (0u != interlock)
                  && (0u == uv_fault) && (0u == state->uv_fault)) ? APL_SWITCH_ON : APL_SWITCH_OFF;
#else
    targets.uv = APL_SWITCH_OFF;
#endif

    /* ops与旧实现一样在联锁回调前捕获，不能在提交阶段重新选择依赖。 */
    apl_core_outputs_apply(self, ops, &targets);
}

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

static void apl_core_shortage_clear(struct apl_core_state *state) APL_CALL
{
    state->shortage = 0u;
    state->shortage_s = 0u;
    state->water_alert = APL_WATER_ALERT_NONE;
}

/**
 * 第一次读数只建立边沿起点。没有上一拍时，无从判断是下降还是上升。
 * 非法段数丢掉，避免一次坏读数把起点改掉。
 */
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

/** 关机期间也计时，这样到时回调可以负责开机。 */
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

static void apl_core_dry_start(struct apl_core *self) APL_CALL
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
static void apl_core_factory_restore(struct apl_core *self) APL_CALL
{
    struct apl_core_state *state;

    state = apl_core_state_of(self);
    if (APL_RUN_FACTORY == state->run_state)
    {
        state->filter_purify_s = state->real_filter_purify_s;
        state->filter_humidify_s = state->real_filter_humidify_s;
    }

    state->run_state = APL_RUN_STANDBY;
    state->dry_needed = 0u;
    state->power = APL_POWER_OFF;
    state->fan_gear = (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
    state->gear_before_turbo = state->fan_gear;
    state->gear_before_sleep = state->fan_gear;
    state->wifi = APL_WIFI_OFF;
    state->purify_on = 0u;
    state->uv_requested = 0u;
    state->anion_requested = 0u;
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

static void apl_core_second(struct apl_core *self) APL_CALL
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

        /* 仅启用能力的累计整小时触发通知；未启用计数的零值不是整点。 */
        if ((APL_RUN_ON == state->run_state)
            && (((0u != apl_core_has_purify(state))
                 && (0u == (state->filter_purify_s % APL_CORE_HOUR_S)))
                || ((0u != apl_core_has_humidify(state))
                    && (0u == (state->filter_humidify_s % APL_CORE_HOUR_S)))))
        {
            apl_core_memory_notify(self);
        }
    }

    if ((APL_RUN_ON == state->run_state) && (0u != state->shortage))
    {
        if (state->shortage_s < UINT32_MAX)
        {
            state->shortage_s++;
        }
        if (state->shortage_s >= APL_CORE_WATER_BLINK_S)
        {
            state->water_alert = APL_WATER_ALERT_STEADY;
        }

        /* 一体机D20/E20：缺水只停加湿，净化持续运行。
         * 纯加湿E16：5分钟恢复窗口结束后进入风干。 */
        if ((0u == apl_core_has_purify(state))
            && (state->shortage_s >= APL_CORE_WATER_SHORTAGE_RUN_S))
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

static enum apl_link_result apl_core_cmd_power(struct apl_core *self) APL_CALL;

/** 面板定时到期执行正常关机；保留通用用户定时回调的既有契约。 */
static void apl_core_panel_timer_expire(struct apl_core *self, void *ctx) APL_CALL
{
    APL_UNUSED_PARAM(ctx);
    if (APL_RUN_ON == apl_core_get_run_state(self))
    {
        apl_core_beep_unless_sleep(apl_core_state_of(self), APL_BEEP_SHORT);
        (void)apl_core_cmd_power(self);
    }
}

void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx) APL_CALL
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
    state->dry_needed = 0u;
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

void apl_core_tick(struct apl_core *self) APL_CALL
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
        state->fan_gear = (enum apl_fan_gear)APL_DEFAULT_FAN_GEAR;
        state->used = 1u;
    }

    state->power = APL_POWER_ON;
    state->run_state = APL_RUN_ON;
    state->humidity_sampled = 0u;
    state->sensors_ready = 0u;
    state->sensor_faults = 0u;
    state->sensors_warming = 0u;
    state->aq_ready = 0u;
#if APL_COMPILE_SENSOR_PM25
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_pm25))
    {
        state->sensors_warming |= APL_SENSOR_PM25_MASK;
    }
#endif
#if APL_COMPILE_SENSOR_PM10
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_pm10))
    {
        state->sensors_warming |= APL_SENSOR_PM10_MASK;
    }
#endif
#if APL_COMPILE_SENSOR_HCHO
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_hcho))
    {
        state->sensors_warming |= APL_SENSOR_HCHO_MASK;
    }
#endif
#if APL_COMPILE_SENSOR_TVOC
    if ((0u != apl_core_has_purify(state)) && (NULL != state->ops) && (NULL != state->ops->get_tvoc))
    {
        state->sensors_warming |= APL_SENSOR_TVOC_MASK;
    }
#endif
#if APL_COMPILE_SENSOR_HUMIDITY
    if ((0u != apl_core_has_humidify(state)) && (NULL != state->ops) && (NULL != state->ops->get_humidity))
    {
        state->sensors_warming |= APL_SENSOR_HUMIDITY_MASK;
    }
#endif
    state->dry_left_s = 0u;
    state->factory_done = 0u;
    state->cl_left_s = 0u;
    if ((0u != state->water_valid) && (APL_WATER_LEVEL_0 == state->water_level))
    {
        apl_core_shortage_start(state);
    }

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
    state->dry_needed = 0u;
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

uint32_t apl_core_get_timer(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->timer_left_s;
}

void apl_core_set_fan_gear(struct apl_core *self, enum apl_fan_gear gear) APL_CALL
{
    if ((NULL == self) || (0u == apl_core_gear_is_valid(gear)))
    {
        return;
    }

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

static enum apl_fan_gear apl_core_gear_next(enum apl_fan_gear gear) APL_CALL
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

static enum apl_link_result apl_core_cmd_power(struct apl_core *self) APL_CALL
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
        if ((0u != apl_core_has_humidify(state)) && (0u != state->dry_needed))
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
                                             const struct apl_link_command *command) APL_CALL
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
#if (APL_CORE_HAS_TURBO == 0u)
        return APL_LINK_REJECTED;
#else
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
#endif
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

static uint8_t apl_core_water_ok(const struct apl_core_state *state) APL_CALL
{
    return ((0u == state->water_valid) || (APL_WATER_LEVEL_0 != state->water_level)) ? 1u : 0u;
}

static uint8_t apl_core_on_or_standby(const struct apl_core_state *state) APL_CALL
{
    return ((APL_RUN_ON == state->run_state) || (APL_RUN_STANDBY == state->run_state))
           ? 1u : 0u;
}

static enum apl_link_result apl_core_cmd_filter_reset(struct apl_core *self) APL_CALL
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
#if (APL_CORE_FILTER_SHARED == 0u)
    if ((0u != purify) && (0u != humidify))
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
#endif
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

static enum apl_link_result apl_core_cmd_factory_enter(struct apl_core *self) APL_CALL
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
                                      const struct apl_link_command *command) APL_CALL
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
#if (APL_COMPILE_UV || APL_COMPILE_ANION)
        if (0u == apl_core_on_or_standby(state))
#else
        if ((0u == apl_core_has_purify(state)) || (0u == apl_core_on_or_standby(state)))
#endif
        {
            return APL_LINK_REJECTED;
        }
        state->purify_on = (0u == state->purify_on) ? 1u : 0u;
#if APL_COMPILE_UV
        state->uv_requested = state->purify_on;
#endif
#if APL_COMPILE_ANION
        state->anion_requested = state->purify_on;
#endif
        state->uv_fault = 0u;
        apl_core_memory_notify(self);
        return APL_LINK_OK;
    case APL_LINK_CMD_UV_SET:
#if APL_COMPILE_UV
        if ((0u == apl_core_on_or_standby(state)) || (command->arg > 1u))
        {
            return APL_LINK_REJECTED;
        }
        if ((state->uv_requested != command->arg) || ((0u != command->arg) && (0u != state->uv_fault)))
        {
            state->uv_requested = command->arg;
            state->purify_on = ((0u != state->uv_requested) || (0u != state->anion_requested)) ? 1u : 0u;
            state->uv_fault = 0u;
            apl_core_beep_unless_sleep(state, APL_BEEP_SHORT);
            apl_core_memory_notify(self);
        }
        return APL_LINK_OK;
#else
        return APL_LINK_REJECTED;
#endif
    case APL_LINK_CMD_ANION_SET:
#if APL_COMPILE_ANION
        if ((0u == apl_core_on_or_standby(state)) || (command->arg > 1u))
        {
            return APL_LINK_REJECTED;
        }
        if (state->anion_requested != command->arg)
        {
            state->anion_requested = command->arg;
            state->purify_on = ((0u != state->uv_requested) || (0u != state->anion_requested)) ? 1u : 0u;
            apl_core_beep_unless_sleep(state, APL_BEEP_SHORT);
            apl_core_memory_notify(self);
        }
        return APL_LINK_OK;
#else
        return APL_LINK_REJECTED;
#endif
    case APL_LINK_CMD_HUMIDIFY_TOGGLE:
        if ((0u == apl_core_has_humidify(state)) || (0u == apl_core_on_or_standby(state))
            || ((0u == state->humidify_on) && (0u == apl_core_water_ok(state))))
        {
            return APL_LINK_REJECTED;
        }
        state->humidify_on = (0u == state->humidify_on) ? 1u : 0u;
        if ((0u != apl_core_has_purify(state)) && (APL_FAN_GEAR_TURBO == state->fan_gear))
        {
            /* 一体机E11：湿度键退出Turbo到Auto。恒湿目标策略保持原样。 */
            state->fan_gear = APL_FAN_GEAR_AUTO;
        }
        apl_core_memory_notify(self);
        return APL_LINK_OK;
    case APL_LINK_CMD_HUMIDITY_TARGET:
#if !APL_COMPILE_SENSOR_HUMIDITY
        if (APL_HUMIDITY_TARGET_OFF != command->arg)
        {
            return APL_LINK_REJECTED;
        }
#endif
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
#if !APL_COMPILE_WIFI
        return APL_LINK_REJECTED;
#else
        if (0u == apl_core_on_or_standby(state))
        {
            return APL_LINK_REJECTED;
        }
        state->wifi = APL_WIFI_PAIRING;
        state->wifi_left_s = APL_CORE_WIFI_PAIR_S;
        return APL_LINK_OK;
#endif
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

void apl_core_get_snapshot(const struct apl_core *self, struct apl_link_snapshot *snapshot) APL_CALL
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
    snapshot->purify_on = state->purify_on;
    snapshot->module_faults = state->module_faults;
#if APL_COMPILE_SENSOR_PM25
    snapshot->sensors_configured |= APL_SENSOR_PM25_MASK;
#endif
#if APL_COMPILE_SENSOR_PM10
    snapshot->sensors_configured |= APL_SENSOR_PM10_MASK;
#endif
#if APL_COMPILE_SENSOR_HCHO
    snapshot->sensors_configured |= APL_SENSOR_HCHO_MASK;
#endif
#if APL_COMPILE_SENSOR_TVOC
    snapshot->sensors_configured |= APL_SENSOR_TVOC_MASK;
#endif
#if APL_COMPILE_SENSOR_HUMIDITY
    snapshot->sensors_configured |= APL_SENSOR_HUMIDITY_MASK;
#endif
    snapshot->sensors_ready = state->sensors_ready;
    snapshot->sensors_warming = state->sensors_warming;
    snapshot->sensor_faults = state->sensor_faults;
    snapshot->aq_ready = state->aq_ready;
#if APL_COMPILE_UV
    snapshot->uv_enabled = 1u;
    snapshot->uv_requested = state->uv_requested;
    snapshot->uv_fault = state->uv_fault;
#endif
#if APL_COMPILE_ANION
    snapshot->anion_enabled = 1u;
    snapshot->anion_requested = state->anion_requested;
#endif
#if APL_COMPILE_WIFI
    snapshot->wifi_enabled = 1u;
#endif
    if (0u != apl_core_has_purify(state))
    {
        snapshot->purify_enabled = 1u;
        snapshot->purify_on = state->purify_on;

        snapshot->pm25 = state->pm25;
        snapshot->aq_level = state->aq_level;
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

void apl_core_memory_restore(struct apl_core *self, const struct apl_core_memory *memory) APL_CALL
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
    state->used = (0u != memory->used) ? 1u : 0u;
    state->purify_on = (0u != memory->purify_on) ? 1u : 0u;
    state->humidify_on = (0u != memory->humidify_on) ? 1u : 0u;
    if ((0u != memory->running) && (0u != state->used))
    {
        apl_core_power_on(self);
    }
}

void apl_core_set_wifi_connected(struct apl_core *self, uint8_t connected) APL_CALL
{
    struct apl_core_state *state;

    if ((NULL == self) || (connected > 1u))
    {
        return;
    }

    state = apl_core_state_of(self);
#if APL_COMPILE_WIFI
    state->wifi = (0u != connected) ? APL_WIFI_CONNECTED : APL_WIFI_OFF;
#else
    state->wifi = APL_WIFI_OFF;
#endif
    state->wifi_left_s = 0u;
}

#if APL_COMPILE_PURIFY
void apl_core_filter_purify_reset(struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_purify_s = 0u;
}

uint32_t apl_core_get_filter_purify(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_purify_s;
}

uint8_t apl_core_get_purify_enabled(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->purify_enabled;
}
#endif

#if APL_COMPILE_HUMIDIFY
void apl_core_filter_humidify_reset(struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_humidify_s = 0u;
}

uint32_t apl_core_get_filter_humidify(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_humidify_s;
}

uint8_t apl_core_get_humidify_enabled(const struct apl_core *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->humidify_enabled;
}
#endif
