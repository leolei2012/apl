/**
 * @file    apl_core.c
 * @brief   主控对象的已定行为。未定的净化、加湿、故障和快照不在这里推测。
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

/** 对外对象里的真实状态。允许与占位对象别名，使用方不直接访问。 */
struct APL_CORE_MAY_ALIAS apl_core_state
{
    const struct apl_core_ops *ops;
    void *ctx;
    enum apl_power power;
    enum apl_fan_gear fan_gear;
    enum apl_switch uv;
    enum apl_switch anion;
    enum apl_water_level water_level;
    uint8_t water_valid;
#if APL_COMPILE_PURIFY
    uint8_t purify_enabled;
    uint32_t filter_purify_count;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t humidify_enabled;
    uint32_t filter_humidify_count;
#endif
    uint32_t timer_left_s;
    uint16_t timer_subtick;
    void (*on_timer)(struct apl_core *self, void *ctx);
    void *timer_ctx;
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
    if ((NULL == state->ops) || (NULL == state->ops->water_level_get))
    {
        return;
    }

    level = state->ops->water_level_get(state->ctx);
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
        if (NULL != state->ops->water_on_trigger)
        {
            state->ops->water_on_trigger(self, state->ctx);
        }
    }
    else if ((APL_WATER_LEVEL_0 == previous) && (level >= APL_WATER_LEVEL_1))
    {
        if (NULL != state->ops->water_on_recover)
        {
            state->ops->water_on_recover(self, state->ctx);
        }
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
    state->fan_gear = APL_FAN_GEAR_2;
    state->uv = APL_SWITCH_OFF;
    state->anion = APL_SWITCH_OFF;
#if APL_COMPILE_PURIFY
    state->purify_enabled = (uint8_t)APL_DEFAULT_PURIFY;
#endif
#if APL_COMPILE_HUMIDIFY
    state->humidify_enabled = (uint8_t)APL_DEFAULT_HUMIDIFY;
#endif
}

void apl_core_tick(struct apl_core *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_timer_tick(self);
    apl_core_water_sample(self);
}

void apl_core_power_on(struct apl_core *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->power = APL_POWER_ON;
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
    apl_core_timer_clear(state);
    if ((NULL != state->ops) && (NULL != state->ops->on_power_off))
    {
        state->ops->on_power_off(self, state->ctx);
    }
}

enum apl_power apl_core_power_get(const struct apl_core *self)
{
    if (NULL == self)
    {
        return APL_POWER_OFF;
    }

    return apl_core_state_of_const(self)->power;
}

void apl_core_timer_set(struct apl_core *self,
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

uint32_t apl_core_timer_get(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->timer_left_s;
}

void apl_core_fan_gear_set(struct apl_core *self, enum apl_fan_gear gear)
{
    if ((NULL == self) || (0u == apl_core_gear_is_valid(gear)))
    {
        return;
    }

    apl_core_state_of(self)->fan_gear = gear;
}

enum apl_fan_gear apl_core_fan_gear_get(const struct apl_core *self)
{
    if (NULL == self)
    {
        return APL_FAN_GEAR_2;
    }

    return apl_core_state_of_const(self)->fan_gear;
}

#if APL_COMPILE_PURIFY
void apl_core_filter_purify_reset(struct apl_core *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_state_of(self)->filter_purify_count = 0u;
}

uint32_t apl_core_filter_purify_get(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_purify_count;
}

uint8_t apl_core_purify_enabled_get(const struct apl_core *self)
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

    apl_core_state_of(self)->filter_humidify_count = 0u;
}

uint32_t apl_core_filter_humidify_get(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->filter_humidify_count;
}

uint8_t apl_core_humidify_enabled_get(const struct apl_core *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_core_state_of_const(self)->humidify_enabled;
}
#endif
