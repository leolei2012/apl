/**
 * @file    apl_hmi.c
 * @brief   人机手势、显示对象和面板逻辑。业务判断在主控，这里只转操作、按快照绘制。
 */

#include "apl_hmi.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__)
#define APL_HMI_MAY_ALIAS __attribute__((__may_alias__))
#else
#define APL_HMI_MAY_ALIAS
#endif

#define APL_HMI_GESTURE_OPEN        0u
#define APL_HMI_GESTURE_LONG_DONE   1u
#define APL_HMI_GESTURE_CONSUMED    2u

#define APL_HMI_SETTING_NONE        0u
#define APL_HMI_SETTING_TIMER       1u
#define APL_HMI_SETTING_HUMIDITY    2u

/** 提示音与闪烁的时长，毫秒。来源见《机型交互逻辑（Excel提炼）》。 */
#define APL_HMI_BEEP_SHORT_MS       100u
#define APL_HMI_BLINK_FAST_MS       100u   /**< 强制重置红灯、水满闪烁 */
#define APL_HMI_BLINK_SLOW_MS       500u   /**< 到期开机红灯、配网、设置闪烁 */
#define APL_HMI_WATER_BLINK_ON_MS   3000u  /**< 缺水橙灯亮 */
#define APL_HMI_WATER_BLINK_OFF_MS  500u   /**< 缺水橙灯灭 */
#define APL_HMI_BLINK_TIMES         3u     /**< 滤网红灯闪 3 次 */
#define APL_HMI_FULL_BLINK_TIMES    4u     /**< 水满闪 4 次 */
#define APL_HMI_TIMER_MAX_H         24u
#define APL_HMI_HUMIDITY_MIN        40u
#define APL_HMI_HUMIDITY_MAX        80u
#define APL_HMI_HUMIDITY_STEP       5u
#define APL_HMI_SECONDS_PER_HOUR    3600u
#define APL_HMI_DIGIT_MAX           999u

_Static_assert(APL_HMI_KEY_COUNT <= 8, "键掩码放在 uint8_t 里");
_Static_assert(APL_HMI_LONG_PRESS_TICKS == 300u, "3 秒在 10 ms 拍上是 300 拍");
_Static_assert(APL_HMI_POWERUP_LAMP_TICKS == (APL_HMI_POWERUP_LAMP_MS / APL_HMI_TICK_MS),
               "上电全灯 2 秒");
_Static_assert(APL_HMI_LAMP_HALF == 128u, "半亮是 128");
_Static_assert(APL_HMI_LAMP_FULL == 255u, "全亮是 255");
_Static_assert(APL_HMI_VERSION_NUM == 0x000100u, "版本号与分段宏不一致");
_Static_assert(APL_HMI_WATER_LAMP_COUNT == 4u, "水位灯是四段");

struct apl_hmi_key
{
    uint8_t brightness;
    void (*on_short)(struct apl_hmi *self, void *ctx);
    void (*on_long)(struct apl_hmi *self, void *ctx);
    void *ctx;
};

struct apl_hmi_key_track
{
    uint8_t down;
    uint8_t gesture;
    uint16_t down_ticks;
};

struct apl_hmi_combo_slot
{
    uint8_t used;
    uint8_t fired;
    uint8_t key_mask;
    uint16_t hold_ticks;
    uint16_t held_ticks;
    void (*on_combo)(struct apl_hmi *self, void *ctx);
    void *ctx;
};

/** 面板逻辑的本地状态。只在接上 link 后使用。 */
struct apl_hmi_panel
{
    enum apl_link_result (*send)(const struct apl_link_command *command, void *ctx);
    void *send_ctx;
    struct apl_link_snapshot snap;
    uint32_t clock;
    enum apl_run_state prev_run;
    uint16_t wake_ticks;
    uint16_t setting_ticks;
    uint16_t query_ticks;
    uint16_t rotate_ticks;
    uint16_t red_blink_ticks;
    uint16_t red_blink_period;
    uint16_t full_blink_ticks;
    uint8_t snap_valid;
    uint8_t light_off;
    uint8_t prev_sleep;
    uint8_t setting;          /**< APL_HMI_SETTING_* */
    uint8_t setting_value;
    uint8_t last_beep_seq;
    uint8_t last_reset_seq;
    uint32_t alert_clock;     /**< 缺水闪烁从提醒开始时计相位 */
    enum apl_water_alert prev_alert;
    uint8_t keys_down;        /**< 本拍按住的键。按住期间唤醒窗口不倒计时 */
};

/** 对外对象里的真实状态。允许与占位对象别名，使用方不直接访问。 */
struct APL_HMI_MAY_ALIAS apl_hmi_state
{
    struct apl_hmi_key key[APL_HMI_KEY_COUNT];
    uint8_t ambient_brightness;
    uint8_t ambient_red;
    uint8_t ambient_green;
    uint8_t ambient_blue;
    uint8_t water[APL_HMI_WATER_LAMP_COUNT];
    uint8_t digit_brightness;
    char digit_content[APL_HMI_DIGIT_COUNT];
    enum apl_hmi_unit digit_unit;
    enum apl_hmi_child_lock child_lock;
    uint8_t purify_enabled;
    uint8_t humidify_enabled;
    uint16_t powerup_lamp_ticks;
    const struct apl_hmi_ops *ops;
    void *ctx;
    struct apl_hmi_key_track track[APL_HMI_KEY_COUNT];
    struct apl_hmi_combo_slot combo[APL_HMI_COMBO_MAX];
    uint8_t icon[APL_HMI_ICON_COUNT];
    enum apl_hmi_water_color water_color;
    uint8_t buzz_count;
    uint8_t buzz_on;
    uint16_t buzz_on_ticks;
    uint16_t buzz_off_ticks;
    uint16_t buzz_phase;
    struct apl_hmi_panel panel;
};

_Static_assert(sizeof(struct apl_hmi_state) <= sizeof(struct apl_hmi),
               "APL_HMI_OPAQUE_SIZE 小于对象状态");
_Static_assert(_Alignof(struct apl_hmi_state) <= _Alignof(struct apl_hmi),
               "占位对象的对齐小于对象状态");

static struct apl_hmi_state *apl_hmi_state_of(struct apl_hmi *self)
{
    return (struct apl_hmi_state *)(void *)self->storage.bytes;
}

static const struct apl_hmi_state *apl_hmi_state_of_const(const struct apl_hmi *self)
{
    return (const struct apl_hmi_state *)(const void *)self->storage.bytes;
}

static uint8_t apl_hmi_key_is_compiled(enum apl_hmi_key_id key)
{
    switch (key)
    {
    case APL_HMI_KEY_PURIFY:
#if APL_HMI_COMPILE_KEY_PURIFY
        return 1u;
#else
        return 0u;
#endif
    case APL_HMI_KEY_HUMIDITY:
#if APL_HMI_COMPILE_KEY_HUMIDITY
        return 1u;
#else
        return 0u;
#endif
    case APL_HMI_KEY_TIMER:
#if APL_HMI_COMPILE_KEY_TIMER
        return 1u;
#else
        return 0u;
#endif
    case APL_HMI_KEY_LIGHT:
#if APL_HMI_COMPILE_KEY_LIGHT
        return 1u;
#else
        return 0u;
#endif
    case APL_HMI_KEY_SPEED:
#if APL_HMI_COMPILE_KEY_SPEED
        return 1u;
#else
        return 0u;
#endif
    case APL_HMI_KEY_POWER:
#if APL_HMI_COMPILE_KEY_POWER
        return 1u;
#else
        return 0u;
#endif
    case APL_HMI_KEY_COUNT:
        return 0u;
    default:
        return 0u;
    }
}

static uint8_t apl_hmi_key_is_valid(enum apl_hmi_key_id key)
{
    if ((unsigned)key >= APL_HMI_KEY_COUNT)
    {
        return 0u;
    }

    return apl_hmi_key_is_compiled(key);
}

static uint8_t apl_hmi_key_bit(uint8_t index)
{
    return (uint8_t)(1u << index);
}

static uint8_t apl_hmi_child_lock_is_valid(enum apl_hmi_child_lock lock)
{
    switch (lock)
    {
    case APL_HMI_CHILD_LOCK_OFF:
    case APL_HMI_CHILD_LOCK_ON:
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t apl_hmi_unit_is_valid(enum apl_hmi_unit unit)
{
    switch (unit)
    {
    case APL_HMI_UNIT_NONE:
    case APL_HMI_UNIT_HOUR:
    case APL_HMI_UNIT_PERCENT:
    case APL_HMI_UNIT_PM25:
        return 1u;
    default:
        return 0u;
    }
}

static uint8_t apl_hmi_enabled_is_valid(uint8_t enabled)
{
    return (uint8_t)((0u == enabled) || (1u == enabled));
}

static enum apl_hmi_key_level apl_hmi_level_at(const struct apl_hmi_key_state *keys,
                                               uint8_t index)
{
    switch (index)
    {
    case APL_HMI_KEY_PURIFY:
#if APL_HMI_COMPILE_KEY_PURIFY
        return keys->purify;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    case APL_HMI_KEY_HUMIDITY:
#if APL_HMI_COMPILE_KEY_HUMIDITY
        return keys->humidity;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    case APL_HMI_KEY_TIMER:
#if APL_HMI_COMPILE_KEY_TIMER
        return keys->timer;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    case APL_HMI_KEY_LIGHT:
#if APL_HMI_COMPILE_KEY_LIGHT
        return keys->light;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    case APL_HMI_KEY_SPEED:
#if APL_HMI_COMPILE_KEY_SPEED
        return keys->speed;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    case APL_HMI_KEY_POWER:
#if APL_HMI_COMPILE_KEY_POWER
        return keys->power;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    default:
        return APL_HMI_KEY_RELEASED;
    }
}

/** 未启用的功能，键灯不许亮。短按也不发出。长按限制尚未规定。 */
static uint8_t apl_hmi_lamp_forced_off(const struct apl_hmi_state *state, uint8_t index)
{
    if ((APL_HMI_KEY_PURIFY == index) && (0u == state->purify_enabled))
    {
        return 1u;
    }

    if ((APL_HMI_KEY_HUMIDITY == index) && (0u == state->humidify_enabled))
    {
        return 1u;
    }

    return 0u;
}

static void apl_hmi_force_disabled_lamps(struct apl_hmi_state *state)
{
    if (0u == state->purify_enabled)
    {
        state->key[APL_HMI_KEY_PURIFY].brightness = 0u;
    }

    if (0u == state->humidify_enabled)
    {
        state->key[APL_HMI_KEY_HUMIDITY].brightness = 0u;
    }
}

static void apl_hmi_lamps_fill(struct apl_hmi_state *state, uint8_t brightness)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        if (0u != apl_hmi_key_is_compiled((enum apl_hmi_key_id)index))
        {
            state->key[index].brightness = brightness;
        }
    }

    state->ambient_brightness = brightness;

    for (index = 0u; index < APL_HMI_WATER_LAMP_COUNT; index++)
    {
        state->water[index] = brightness;
    }

    state->digit_brightness = brightness;

    for (index = 0u; index < APL_HMI_ICON_COUNT; index++)
    {
        state->icon[index] = brightness;
    }
}

static void apl_hmi_powerup_show(struct apl_hmi_state *state)
{
    uint8_t index;

    apl_hmi_lamps_fill(state, APL_HMI_LAMP_FULL);

    for (index = 0u; index < APL_HMI_DIGIT_COUNT; index++)
    {
        state->digit_content[index] = '8';
    }

    state->digit_unit = APL_HMI_UNIT_NONE;
}

static void apl_hmi_powerup_idle(struct apl_hmi_state *state)
{
    uint8_t index;

    apl_hmi_lamps_fill(state, 0u);

    if (0u != apl_hmi_key_is_compiled(APL_HMI_KEY_POWER))
    {
        state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_HALF;
    }

    for (index = 0u; index < APL_HMI_DIGIT_COUNT; index++)
    {
        state->digit_content[index] = ' ';
    }

    state->digit_unit = APL_HMI_UNIT_NONE;
}

/** 全灯阶段每拍重画。未启用的净化、加湿键灯随后仍被强制熄灭。 */
static void apl_hmi_powerup_tick(struct apl_hmi_state *state)
{
    if (0u == state->powerup_lamp_ticks)
    {
        return;
    }

    state->powerup_lamp_ticks--;

    if (0u == state->powerup_lamp_ticks)
    {
        apl_hmi_powerup_idle(state);
    }
    else
    {
        apl_hmi_powerup_show(state);
    }
}

static void apl_hmi_panel_key(struct apl_hmi *self,
                              struct apl_hmi_state *state,
                              uint8_t index,
                              uint8_t is_long);
static void apl_hmi_panel_tick(struct apl_hmi_state *state);
static void apl_hmi_buzzer_tick(struct apl_hmi_state *state);

/**
 * locked 用本拍开始时的童锁。回调里改锁，要等下一拍才影响其他键，
 * 避免同一次采样里解锁顺带把别的键也放出去。
 */
static void apl_hmi_emit(struct apl_hmi *self,
                         struct apl_hmi_state *state,
                         uint8_t index,
                         uint8_t locked,
                         uint8_t is_long)
{
    struct apl_hmi_key *key;
    void (*callback)(struct apl_hmi *emit_self, void *emit_ctx);

    if (index >= APL_HMI_KEY_COUNT)
    {
        return;
    }

    if ((0u == is_long) && (0u != apl_hmi_lamp_forced_off(state, index)))
    {
        return;
    }

    if (0u != locked)
    {
        if ((0u == is_long) || (APL_HMI_KEY_TIMER != index))
        {
            return;
        }
    }

    if ((NULL != state->ops) && (NULL != state->ops->key_event))
    {
        state->ops->key_event((enum apl_hmi_key_id)index, is_long, state->ctx);
    }

    if (NULL != state->panel.send)
    {
        apl_hmi_panel_key(self, state, index, is_long);
        return;
    }

    key = &state->key[index];
    callback = (0u != is_long) ? key->on_long : key->on_short;
    if (NULL != callback)
    {
        callback(self, key->ctx);
    }
}

static uint8_t apl_hmi_mask_held(uint8_t down_mask, uint8_t key_mask)
{
    if (0u == key_mask)
    {
        return 0u;
    }

    return (uint8_t)((down_mask & key_mask) == key_mask);
}

static void apl_hmi_consume_mask(struct apl_hmi_state *state, uint8_t key_mask)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        if (0u != (key_mask & apl_hmi_key_bit(index)))
        {
            state->track[index].gesture = APL_HMI_GESTURE_CONSUMED;
        }
    }
}

/**
 * 童锁期间不武装组合，按住的时间也清掉。否则一解锁就会按锁住期间的时长触发。
 */
static uint8_t apl_hmi_combos_update(struct apl_hmi *self,
                                     struct apl_hmi_state *state,
                                     uint8_t down_mask,
                                     uint8_t locked)
{
    uint8_t slot_index;
    uint8_t suppress;

    suppress = 0u;
    for (slot_index = 0u; slot_index < APL_HMI_COMBO_MAX; slot_index++)
    {
        struct apl_hmi_combo_slot *slot;

        slot = &state->combo[slot_index];
        if (0u == slot->used)
        {
            continue;
        }

        if ((0u != locked) || (0u == apl_hmi_mask_held(down_mask, slot->key_mask)))
        {
            slot->held_ticks = 0u;
            slot->fired = 0u;
            continue;
        }

        if (slot->held_ticks < UINT16_MAX)
        {
            slot->held_ticks++;
        }

        if ((0u == slot->fired) && (slot->held_ticks >= slot->hold_ticks))
        {
            slot->fired = 1u;
            apl_hmi_consume_mask(state, slot->key_mask);
            if (NULL != slot->on_combo)
            {
                slot->on_combo(self, slot->ctx);
            }
        }
        else if (0u == slot->fired)
        {
            suppress |= slot->key_mask;
        }
    }

    return suppress;
}

static void apl_hmi_keys_resolve(struct apl_hmi *self,
                                 struct apl_hmi_state *state,
                                 uint8_t down_mask,
                                 uint8_t suppress,
                                 uint8_t locked)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        struct apl_hmi_key_track *track;
        uint8_t down;
        uint8_t bit;

        track = &state->track[index];
        bit = apl_hmi_key_bit(index);
        down = (uint8_t)(0u != (down_mask & bit));
        if (0u != down)
        {
            if ((APL_HMI_GESTURE_OPEN == track->gesture)
                && (0u == (suppress & bit))
                && (track->down_ticks >= APL_HMI_LONG_PRESS_TICKS))
            {
                track->gesture = APL_HMI_GESTURE_LONG_DONE;
                apl_hmi_emit(self, state, index, locked, 1u);
            }
        }
        else if (0u != track->down)
        {
            if (APL_HMI_GESTURE_OPEN == track->gesture)
            {
                if (track->down_ticks >= APL_HMI_LONG_PRESS_TICKS)
                {
                    apl_hmi_emit(self, state, index, locked, 1u);
                }
                else
                {
                    apl_hmi_emit(self, state, index, locked, 0u);
                }
            }

            track->down_ticks = 0u;
            track->gesture = APL_HMI_GESTURE_OPEN;
        }

        track->down = down;
    }
}

static uint8_t apl_hmi_sample(struct apl_hmi_state *state, const struct apl_hmi_key_state *keys)
{
    uint8_t index;
    uint8_t down_mask;

    down_mask = 0u;
    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        struct apl_hmi_key_track *track;
        uint8_t down;

        track = &state->track[index];
        down = (uint8_t)(APL_HMI_KEY_PRESSED == apl_hmi_level_at(keys, index));
        if (0u != down)
        {
            down_mask |= apl_hmi_key_bit(index);
            if (track->down_ticks < UINT16_MAX)
            {
                track->down_ticks++;
            }
        }
    }

    return down_mask;
}

void apl_hmi_init(struct apl_hmi *self, const struct apl_hmi_ops *ops, void *ctx)
{
    struct apl_hmi_state *state;

    if (NULL == self)
    {
        return;
    }

    memset(self, 0, sizeof(*self));
    state = apl_hmi_state_of(self);
    state->ops = ops;
    state->ctx = ctx;
    state->child_lock = APL_HMI_CHILD_LOCK_OFF;
    state->purify_enabled = 1u;
    state->humidify_enabled = 1u;
    state->digit_unit = APL_HMI_UNIT_NONE;
    state->powerup_lamp_ticks = APL_HMI_POWERUP_LAMP_TICKS;
    apl_hmi_powerup_show(state);
    /* 原表：上电蜂鸣一声。 */
    state->buzz_count = 1u;
    state->buzz_on_ticks = APL_HMI_BEEP_SHORT_MS / APL_HMI_TICK_MS;
    state->buzz_off_ticks = 0u;
}

void apl_hmi_tick(struct apl_hmi *self)
{
    struct apl_hmi_state *state;
    struct apl_hmi_key_state keys;
    uint8_t down_mask;
    uint8_t suppress;
    uint8_t locked;

    if (NULL == self)
    {
        return;
    }

    state = apl_hmi_state_of(self);
    if ((NULL != state->ops) && (NULL != state->ops->get_key_state))
    {
        memset(&keys, 0, sizeof(keys));
        state->ops->get_key_state(&keys, state->ctx);
        down_mask = apl_hmi_sample(state, &keys);
        locked = (uint8_t)(APL_HMI_CHILD_LOCK_ON == state->child_lock);
        suppress = apl_hmi_combos_update(self, state, down_mask, locked);
        apl_hmi_keys_resolve(self, state, down_mask, suppress, locked);
        state->panel.keys_down = down_mask;
    }

    if ((NULL != state->panel.send) && (0u == state->powerup_lamp_ticks))
    {
        apl_hmi_panel_tick(state);
    }

    apl_hmi_powerup_tick(state);
    apl_hmi_force_disabled_lamps(state);
    apl_hmi_buzzer_tick(state);
}

uint8_t apl_hmi_get_powerup_lamp(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return (uint8_t)(0u != apl_hmi_state_of_const(self)->powerup_lamp_ticks);
}

void apl_hmi_set_key_lamp(struct apl_hmi *self, enum apl_hmi_key_id key, uint8_t brightness)
{
    struct apl_hmi_state *state;

    if ((NULL == self) || (0u == apl_hmi_key_is_valid(key)))
    {
        return;
    }

    state = apl_hmi_state_of(self);
    if (0u != apl_hmi_lamp_forced_off(state, (uint8_t)key))
    {
        brightness = 0u;
    }

    state->key[key].brightness = brightness;
}

uint8_t apl_hmi_get_key_lamp(const struct apl_hmi *self, enum apl_hmi_key_id key)
{
    if ((NULL == self) || (0u == apl_hmi_key_is_valid(key)))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->key[key].brightness;
}

void apl_hmi_set_key_callback(struct apl_hmi *self,
                              enum apl_hmi_key_id key,
                              void (*on_short)(struct apl_hmi *key_self, void *key_ctx),
                              void (*on_long)(struct apl_hmi *key_self, void *key_ctx),
                              void *ctx)
{
    struct apl_hmi_key *slot;

    if ((NULL == self) || (0u == apl_hmi_key_is_valid(key)))
    {
        return;
    }

    slot = &apl_hmi_state_of(self)->key[key];
    slot->on_short = on_short;
    slot->on_long = on_long;
    slot->ctx = ctx;
}

void apl_hmi_set_ambient_brightness(struct apl_hmi *self, uint8_t brightness)
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->ambient_brightness = brightness;
}

uint8_t apl_hmi_get_ambient_brightness(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->ambient_brightness;
}

void apl_hmi_set_ambient_color(struct apl_hmi *self,
                               uint8_t red,
                               uint8_t green,
                               uint8_t blue)
{
    struct apl_hmi_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_hmi_state_of(self);
    state->ambient_red = red;
    state->ambient_green = green;
    state->ambient_blue = blue;
}

void apl_hmi_get_ambient_color(const struct apl_hmi *self,
                               uint8_t *red,
                               uint8_t *green,
                               uint8_t *blue)
{
    uint8_t red_value;
    uint8_t green_value;
    uint8_t blue_value;

    red_value = 0u;
    green_value = 0u;
    blue_value = 0u;
    if (NULL != self)
    {
        const struct apl_hmi_state *state;

        state = apl_hmi_state_of_const(self);
        red_value = state->ambient_red;
        green_value = state->ambient_green;
        blue_value = state->ambient_blue;
    }

    if (NULL != red)
    {
        *red = red_value;
    }

    if (NULL != green)
    {
        *green = green_value;
    }

    if (NULL != blue)
    {
        *blue = blue_value;
    }
}

void apl_hmi_set_water_lamp(struct apl_hmi *self, uint8_t index, uint8_t brightness)
{
    if ((NULL == self) || (0u == index) || (index > APL_HMI_WATER_LAMP_COUNT))
    {
        return;
    }

    apl_hmi_state_of(self)->water[index - 1u] = brightness;
}

uint8_t apl_hmi_get_water_lamp(const struct apl_hmi *self, uint8_t index)
{
    if ((NULL == self) || (0u == index) || (index > APL_HMI_WATER_LAMP_COUNT))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->water[index - 1u];
}

void apl_hmi_set_digits_brightness(struct apl_hmi *self, uint8_t brightness)
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->digit_brightness = brightness;
}

uint8_t apl_hmi_get_digits_brightness(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->digit_brightness;
}

void apl_hmi_set_digits_content(struct apl_hmi *self,
                                const char content[APL_HMI_DIGIT_COUNT])
{
    if ((NULL == self) || (NULL == content))
    {
        return;
    }

    memcpy(apl_hmi_state_of(self)->digit_content, content, APL_HMI_DIGIT_COUNT);
}

void apl_hmi_get_digits_content(const struct apl_hmi *self,
                                char content[APL_HMI_DIGIT_COUNT])
{
    if (NULL == content)
    {
        return;
    }

    if (NULL == self)
    {
        memset(content, 0, APL_HMI_DIGIT_COUNT);
        return;
    }

    memcpy(content, apl_hmi_state_of_const(self)->digit_content, APL_HMI_DIGIT_COUNT);
}

void apl_hmi_set_digits_unit(struct apl_hmi *self, enum apl_hmi_unit unit)
{
    if ((NULL == self) || (0u == apl_hmi_unit_is_valid(unit)))
    {
        return;
    }

    apl_hmi_state_of(self)->digit_unit = unit;
}

enum apl_hmi_unit apl_hmi_get_digits_unit(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return APL_HMI_UNIT_NONE;
    }

    return apl_hmi_state_of_const(self)->digit_unit;
}

void apl_hmi_set_child_lock(struct apl_hmi *self, enum apl_hmi_child_lock lock)
{
    if ((NULL == self) || (0u == apl_hmi_child_lock_is_valid(lock)))
    {
        return;
    }

    apl_hmi_state_of(self)->child_lock = lock;
}

enum apl_hmi_child_lock apl_hmi_get_child_lock(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return APL_HMI_CHILD_LOCK_OFF;
    }

    return apl_hmi_state_of_const(self)->child_lock;
}

void apl_hmi_set_purify_enabled(struct apl_hmi *self, uint8_t enabled)
{
    if ((NULL == self) || (0u == apl_hmi_enabled_is_valid(enabled)))
    {
        return;
    }

    apl_hmi_state_of(self)->purify_enabled = enabled;
}

uint8_t apl_hmi_get_purify_enabled(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->purify_enabled;
}

void apl_hmi_set_humidify_enabled(struct apl_hmi *self, uint8_t enabled)
{
    if ((NULL == self) || (0u == apl_hmi_enabled_is_valid(enabled)))
    {
        return;
    }

    apl_hmi_state_of(self)->humidify_enabled = enabled;
}

uint8_t apl_hmi_get_humidify_enabled(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->humidify_enabled;
}

int apl_hmi_set_combo(struct apl_hmi *self,
                      const enum apl_hmi_key_id *keys,
                      uint8_t key_count,
                      uint16_t hold_ms,
                      void (*on_combo)(struct apl_hmi *combo_self, void *combo_ctx),
                      void *ctx)
{
    struct apl_hmi_state *state;
    uint8_t index;
    uint8_t mask;
    uint16_t hold_ticks;
    struct apl_hmi_combo_slot *slot;

    if ((NULL == self) || (NULL == keys) || (NULL == on_combo))
    {
        return -1;
    }

    if ((key_count < 2u) || (0u == hold_ms))
    {
        return -1;
    }

    mask = 0u;
    for (index = 0u; index < key_count; index++)
    {
        uint8_t bit;

        if (0u == apl_hmi_key_is_valid(keys[index]))
        {
            return -1;
        }

        bit = apl_hmi_key_bit((uint8_t)keys[index]);
        if (0u != (mask & bit))
        {
            return -1;
        }

        mask |= bit;
    }

    hold_ticks = (uint16_t)(((uint32_t)hold_ms + (APL_HMI_TICK_MS - 1u)) / APL_HMI_TICK_MS);
    if (0u == hold_ticks)
    {
        return -1;
    }

    state = apl_hmi_state_of(self);
    slot = NULL;
    for (index = 0u; index < APL_HMI_COMBO_MAX; index++)
    {
        if ((0u != state->combo[index].used) && (state->combo[index].key_mask == mask))
        {
            return -1;
        }

        if ((0u == state->combo[index].used) && (NULL == slot))
        {
            slot = &state->combo[index];
        }
    }

    if (NULL == slot)
    {
        return -1;
    }

    slot->used = 1u;
    slot->fired = 0u;
    slot->key_mask = mask;
    slot->hold_ticks = hold_ticks;
    slot->held_ticks = 0u;
    slot->on_combo = on_combo;
    slot->ctx = ctx;
    return 0;
}

static uint8_t apl_hmi_icon_is_valid(enum apl_hmi_icon icon)
{
    return ((uint32_t)icon < (uint32_t)APL_HMI_ICON_COUNT) ? 1u : 0u;
}

void apl_hmi_set_icon(struct apl_hmi *self, enum apl_hmi_icon icon, uint8_t brightness)
{
    if ((NULL == self) || (0u == apl_hmi_icon_is_valid(icon)))
    {
        return;
    }

    apl_hmi_state_of(self)->icon[icon] = brightness;
}

uint8_t apl_hmi_get_icon(const struct apl_hmi *self, enum apl_hmi_icon icon)
{
    if ((NULL == self) || (0u == apl_hmi_icon_is_valid(icon)))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->icon[icon];
}

void apl_hmi_set_water_color(struct apl_hmi *self, enum apl_hmi_water_color color)
{
    if ((NULL == self) || ((APL_HMI_WATER_GREEN != color) && (APL_HMI_WATER_ORANGE != color)))
    {
        return;
    }

    apl_hmi_state_of(self)->water_color = color;
}

enum apl_hmi_water_color apl_hmi_get_water_color(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return APL_HMI_WATER_GREEN;
    }

    return apl_hmi_state_of_const(self)->water_color;
}

static uint16_t apl_hmi_ms_to_ticks(uint16_t ms)
{
    return (uint16_t)((ms + APL_HMI_TICK_MS - 1u) / APL_HMI_TICK_MS);
}

static void apl_hmi_buzzer_write(struct apl_hmi_state *state, uint8_t on)
{
    if (on == state->buzz_on)
    {
        return;
    }

    state->buzz_on = on;
    if ((NULL != state->ops) && (NULL != state->ops->set_buzzer))
    {
        state->ops->set_buzzer(on, state->ctx);
    }
}

void apl_hmi_buzzer_play(struct apl_hmi *self, uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    struct apl_hmi_state *state;

    if (NULL == self)
    {
        return;
    }

    state = apl_hmi_state_of(self);
    state->buzz_count = count;
    state->buzz_on_ticks = apl_hmi_ms_to_ticks(on_ms);
    state->buzz_off_ticks = apl_hmi_ms_to_ticks(off_ms);
    state->buzz_phase = 0u;
    if ((0u == count) || (0u == state->buzz_on_ticks))
    {
        state->buzz_count = 0u;
        apl_hmi_buzzer_write(state, 0u);
    }
}

uint8_t apl_hmi_get_buzzer_busy(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return (0u != apl_hmi_state_of_const(self)->buzz_count) ? 1u : 0u;
}

/** 一声 = 响 on 拍 + 停 off 拍。最后一声的停顿也走完，便于连续播放时不粘连。 */
static void apl_hmi_buzzer_tick(struct apl_hmi_state *state)
{
    if (0u == state->buzz_count)
    {
        apl_hmi_buzzer_write(state, 0u);
        return;
    }

    apl_hmi_buzzer_write(state, (state->buzz_phase < state->buzz_on_ticks) ? 1u : 0u);
    state->buzz_phase++;
    if (state->buzz_phase >= (uint16_t)(state->buzz_on_ticks + state->buzz_off_ticks))
    {
        state->buzz_phase = 0u;
        state->buzz_count--;
        if (0u == state->buzz_count)
        {
            apl_hmi_buzzer_write(state, 0u);
        }
    }
}

static void apl_hmi_play(struct apl_hmi_state *state, uint8_t count, uint16_t on_ms, uint16_t off_ms)
{
    state->buzz_count = count;
    state->buzz_on_ticks = apl_hmi_ms_to_ticks(on_ms);
    state->buzz_off_ticks = apl_hmi_ms_to_ticks(off_ms);
    state->buzz_phase = 0u;
}

static void apl_hmi_beep_play(struct apl_hmi_state *state, enum apl_beep beep)
{
    switch (beep)
    {
    case APL_BEEP_SHORT:
        apl_hmi_play(state, 1u, APL_HMI_BEEP_SHORT_MS, 0u);
        break;
    case APL_BEEP_FILTER_RESET:
    case APL_BEEP_FACTORY:
        apl_hmi_play(state, 1u, 500u, 0u);
        break;
    case APL_BEEP_FILTER_FORCED:
    case APL_BEEP_WATER_85:
        apl_hmi_play(state, 1u, 300u, 0u);
        break;
    case APL_BEEP_WATER_FULL:
        apl_hmi_play(state, 4u, 100u, 100u);
        break;
    case APL_BEEP_WATER_SHORTAGE:
    case APL_BEEP_UV_ALARM:
        apl_hmi_play(state, 3u, 100u, 100u);
        break;
    default:
        break;
    }
}

static uint8_t apl_hmi_blink(uint32_t clock, uint16_t on_ms, uint16_t off_ms)
{
    uint32_t period;

    period = (uint32_t)(on_ms + off_ms) / APL_HMI_TICK_MS;
    if (0u == period)
    {
        return 1u;
    }

    return ((clock % period) < ((uint32_t)on_ms / APL_HMI_TICK_MS)) ? 1u : 0u;
}

static void apl_hmi_digits_number(struct apl_hmi_state *state, uint16_t value)
{
    uint8_t index;

    if (value > APL_HMI_DIGIT_MAX)
    {
        value = APL_HMI_DIGIT_MAX;
    }

    /* 右对齐，不补前导零。 */
    for (index = APL_HMI_DIGIT_COUNT; index > 0u; index--)
    {
        if ((index < APL_HMI_DIGIT_COUNT) && (0u == value))
        {
            state->digit_content[index - 1u] = ' ';
        }
        else
        {
            state->digit_content[index - 1u] = (char)('0' + (value % 10u));
            value = (uint16_t)(value / 10u);
        }
    }
}

static void apl_hmi_digits_text(struct apl_hmi_state *state, char a, char b, char c)
{
    state->digit_content[0] = a;
    state->digit_content[1] = b;
    state->digit_content[2] = c;
}

static uint8_t apl_hmi_panel_combo_model(const struct apl_link_snapshot *snap)
{
    return ((0u != snap->purify_enabled) && (0u != snap->humidify_enabled)) ? 1u : 0u;
}

static void apl_hmi_panel_send(struct apl_hmi_state *state,
                               enum apl_link_command_type type,
                               uint8_t arg)
{
    struct apl_link_command command;

    command.type = type;
    command.arg = arg;
    (void)state->panel.send(&command, state->panel.send_ctx);
}

static uint8_t apl_hmi_panel_dark(const struct apl_hmi_state *state)
{
    const struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (APL_RUN_ON != panel->snap.run_state)
    {
        return 0u;
    }

    return (((0u != panel->light_off) || (0u != panel->snap.sleep)) && (0u == panel->wake_ticks))
           ? 1u : 0u;
}

static uint8_t apl_hmi_humidity_next(uint8_t value, uint8_t with_off)
{
    if (APL_HUMIDITY_TARGET_OFF == value)
    {
        return APL_HMI_HUMIDITY_MIN;
    }

    if (value >= APL_HMI_HUMIDITY_MAX)
    {
        return (0u != with_off) ? APL_HUMIDITY_TARGET_OFF : APL_HMI_HUMIDITY_MIN;
    }

    return (uint8_t)(value + APL_HMI_HUMIDITY_STEP);
}

static void apl_hmi_panel_timer_key(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (APL_RUN_ON != panel->snap.run_state)
    {
        return;
    }

    if (APL_HMI_SETTING_TIMER != panel->setting)
    {
        panel->setting = APL_HMI_SETTING_TIMER;
        if (0u != panel->snap.timer_left_s)
        {
            /* 定时运行中：先显示剩余小时数，向上取整。 */
            panel->setting_value = (uint8_t)((panel->snap.timer_left_s
                                              + APL_HMI_SECONDS_PER_HOUR - 1u)
                                             / APL_HMI_SECONDS_PER_HOUR);
        }
        else
        {
            panel->setting_value = 1u;
        }
    }
    else
    {
        panel->setting_value = (panel->setting_value >= APL_HMI_TIMER_MAX_H)
                               ? 0u : (uint8_t)(panel->setting_value + 1u);
    }

    panel->setting_ticks = APL_HMI_IDLE_TICKS;
}

static void apl_hmi_panel_humidity_short(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;
    uint8_t combo;

    panel = &state->panel;
    combo = apl_hmi_panel_combo_model(&panel->snap);
    if (APL_HMI_SETTING_HUMIDITY == panel->setting)
    {
        panel->setting_value = apl_hmi_humidity_next(panel->setting_value, combo);
        panel->setting_ticks = APL_HMI_IDLE_TICKS;
        return;
    }

    if (0u != combo)
    {
        apl_hmi_panel_send(state, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u);
        return;
    }

    /* 加湿器：短按进入设置，显示当前目标；不恒湿时从 40% 开始。 */
    panel->setting = APL_HMI_SETTING_HUMIDITY;
    panel->setting_value = (APL_HUMIDITY_TARGET_OFF == panel->snap.humidity_target)
                           ? APL_HMI_HUMIDITY_MIN : panel->snap.humidity_target;
    panel->setting_ticks = APL_HMI_IDLE_TICKS;
}

static void apl_hmi_panel_key(struct apl_hmi *self,
                              struct apl_hmi_state *state,
                              uint8_t index,
                              uint8_t is_long)
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    (void)self;

    /* 灯光关闭或睡眠熄屏时，第一次触摸只唤醒。 */
    if (0u != apl_hmi_panel_dark(state))
    {
        panel->wake_ticks = APL_HMI_IDLE_TICKS;
        return;
    }

    if (0u != panel->wake_ticks)
    {
        panel->wake_ticks = APL_HMI_IDLE_TICKS;
    }

    switch (index)
    {
    case APL_HMI_KEY_POWER:
        if (0u != is_long)
        {
            apl_hmi_panel_send(state, APL_LINK_CMD_DRY_STOP, 0u);
        }
        else if (APL_RUN_FACTORY == panel->snap.run_state)
        {
            apl_hmi_panel_send(state, APL_LINK_CMD_FACTORY_EXIT, 0u);
        }
        else
        {
            panel->setting = APL_HMI_SETTING_NONE;
            apl_hmi_panel_send(state, APL_LINK_CMD_POWER, 0u);
        }
        break;
    case APL_HMI_KEY_SPEED:
        apl_hmi_panel_send(state, (0u != is_long) ? APL_LINK_CMD_TURBO_TOGGLE
                                                  : APL_LINK_CMD_FAN_CYCLE, 0u);
        break;
    case APL_HMI_KEY_PURIFY:
        apl_hmi_panel_send(state, (0u != is_long) ? APL_LINK_CMD_WIFI_PAIR
                                                  : APL_LINK_CMD_PURIFY_TOGGLE, 0u);
        break;
    case APL_HMI_KEY_TIMER:
        if (0u != is_long)
        {
            state->child_lock = (APL_HMI_CHILD_LOCK_ON == state->child_lock)
                                ? APL_HMI_CHILD_LOCK_OFF : APL_HMI_CHILD_LOCK_ON;
        }
        else
        {
            apl_hmi_panel_timer_key(state);
        }
        break;
    case APL_HMI_KEY_LIGHT:
        if (0u != is_long)
        {
            apl_hmi_panel_send(state, APL_LINK_CMD_SLEEP_TOGGLE, 0u);
        }
        else if (APL_RUN_ON == panel->snap.run_state)
        {
            panel->light_off = (0u == panel->light_off) ? 1u : 0u;
            panel->wake_ticks = 0u;
        }
        else
        {
            /* 待机、风干时灯光键无作用。 */
        }
        break;
    case APL_HMI_KEY_HUMIDITY:
        if (0u == is_long)
        {
            apl_hmi_panel_humidity_short(state);
        }
        else if (0u != apl_hmi_panel_combo_model(&panel->snap))
        {
            panel->setting = APL_HMI_SETTING_HUMIDITY;
            panel->setting_value = panel->snap.humidity_target;
            panel->setting_ticks = APL_HMI_IDLE_TICKS;
        }
        else
        {
            /* 加湿器恒湿键没有长按。 */
        }
        break;
    default:
        break;
    }
}

static void apl_hmi_panel_combo_filter(struct apl_hmi *self, void *ctx)
{
    (void)ctx;
    apl_hmi_panel_send(apl_hmi_state_of(self), APL_LINK_CMD_FILTER_RESET, 0u);
}

static void apl_hmi_panel_combo_factory(struct apl_hmi *self, void *ctx)
{
    (void)ctx;
    apl_hmi_panel_send(apl_hmi_state_of(self), APL_LINK_CMD_FACTORY_ENTER, 0u);
}

static void apl_hmi_panel_confirm(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (APL_HMI_SETTING_NONE == panel->setting)
    {
        return;
    }

    if (0u != panel->setting_ticks)
    {
        panel->setting_ticks--;
        if (0u != panel->setting_ticks)
        {
            return;
        }
    }

    if (APL_HMI_SETTING_TIMER == panel->setting)
    {
        apl_hmi_panel_send(state, APL_LINK_CMD_TIMER_SET, panel->setting_value);
    }
    else
    {
        apl_hmi_panel_send(state, APL_LINK_CMD_HUMIDITY_TARGET, panel->setting_value);
    }

    panel->setting = APL_HMI_SETTING_NONE;
}

static void apl_hmi_panel_clear(struct apl_hmi_state *state)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        state->key[index].brightness = 0u;
    }

    for (index = 0u; index < APL_HMI_ICON_COUNT; index++)
    {
        state->icon[index] = 0u;
    }

    for (index = 0u; index < APL_HMI_WATER_LAMP_COUNT; index++)
    {
        state->water[index] = 0u;
    }

    state->ambient_brightness = 0u;
    state->digit_brightness = 0u;
    state->digit_unit = APL_HMI_UNIT_NONE;
    apl_hmi_digits_text(state, ' ', ' ', ' ');
}

static uint8_t apl_hmi_on(uint8_t cond)
{
    return (0u != cond) ? APL_HMI_LAMP_FULL : 0u;
}

static uint8_t apl_hmi_panel_filter_expired(const struct apl_link_snapshot *snap)
{
    if ((0u != snap->purify_enabled) && (APL_FILTER_EXPIRED == snap->filter_purify_left))
    {
        return 1u;
    }

    if ((0u != snap->humidify_enabled) && (APL_FILTER_EXPIRED == snap->filter_humidify_left))
    {
        return 1u;
    }

    return 0u;
}

static void apl_hmi_panel_draw_filter(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;
    uint8_t red;

    panel = &state->panel;
    red = apl_hmi_panel_filter_expired(&panel->snap);
    if (0u != panel->red_blink_ticks)
    {
        /* 闪烁期间白灯灭，红灯按周期亮灭。 */
        state->icon[APL_HMI_ICON_FILTER_RED] =
            apl_hmi_on(apl_hmi_blink(panel->red_blink_ticks, panel->red_blink_period,
                                     panel->red_blink_period));
        return;
    }

    state->icon[APL_HMI_ICON_FILTER_RED] = apl_hmi_on(red);
    state->icon[APL_HMI_ICON_FILTER_WHITE] = apl_hmi_on(0u == red);
}

static void apl_hmi_panel_draw_water(struct apl_hmi_state *state)
{
    const struct apl_link_snapshot *snap;
    struct apl_hmi_panel *panel;
    uint8_t index;
    uint8_t lit;

    panel = &state->panel;
    snap = &panel->snap;
    if (0u == snap->humidify_enabled)
    {
        return;
    }

    if (APL_WATER_LEVEL_0 == snap->water_level)
    {
        state->water_color = APL_HMI_WATER_ORANGE;
        if ((APL_WATER_ALERT_STEADY == snap->water_alert)
            || ((APL_WATER_ALERT_BLINK == snap->water_alert)
                && (0u != apl_hmi_blink(panel->clock - panel->alert_clock,
                                        APL_HMI_WATER_BLINK_ON_MS,
                                        APL_HMI_WATER_BLINK_OFF_MS))))
        {
            state->water[0] = APL_HMI_LAMP_FULL;
        }
        return;
    }

    /* 只亮最低一段视为低于 25%，橙色；两段以上为绿色。 */
    state->water_color = (APL_WATER_LEVEL_1 == snap->water_level) ? APL_HMI_WATER_ORANGE
                                                                  : APL_HMI_WATER_GREEN;
    lit = 1u;
    if (0u != panel->full_blink_ticks)
    {
        lit = apl_hmi_blink(panel->full_blink_ticks, APL_HMI_BLINK_FAST_MS,
                            APL_HMI_BLINK_FAST_MS);
    }

    for (index = 0u; index < APL_HMI_WATER_LAMP_COUNT; index++)
    {
        state->water[index] = apl_hmi_on((index < (uint8_t)snap->water_level) && (0u != lit));
    }
}

static void apl_hmi_panel_draw_ambient(struct apl_hmi_state *state)
{
    const struct apl_link_snapshot *snap;

    snap = &state->panel.snap;
    if (0u == snap->purify_enabled)
    {
        return;
    }

    state->ambient_brightness = APL_HMI_LAMP_FULL;
    switch (snap->aq_level)
    {
    case APL_AQ_EXCELLENT:
        state->ambient_red = 0u;
        state->ambient_green = 0u;
        state->ambient_blue = APL_HMI_LAMP_FULL;
        break;
    case APL_AQ_GOOD:
        state->ambient_red = 0u;
        state->ambient_green = APL_HMI_LAMP_FULL;
        state->ambient_blue = 0u;
        break;
    case APL_AQ_MODERATE:
        state->ambient_red = APL_HMI_LAMP_FULL;
        state->ambient_green = APL_HMI_LAMP_FULL;
        state->ambient_blue = 0u;
        break;
    default:
        state->ambient_red = APL_HMI_LAMP_FULL;
        state->ambient_green = 0u;
        state->ambient_blue = 0u;
        break;
    }
}

static void apl_hmi_panel_draw_digits(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;
    const struct apl_link_snapshot *snap;
    uint8_t show_humidity;

    panel = &state->panel;
    snap = &panel->snap;
    state->digit_brightness = APL_HMI_LAMP_FULL;

    if (APL_HMI_SETTING_TIMER == panel->setting)
    {
        apl_hmi_digits_number(state, panel->setting_value);
        /* H 闪烁。 */
        state->digit_unit = (0u != apl_hmi_blink(panel->clock, APL_HMI_BLINK_SLOW_MS,
                                                 APL_HMI_BLINK_SLOW_MS))
                            ? APL_HMI_UNIT_HOUR : APL_HMI_UNIT_NONE;
        return;
    }

    if (APL_HMI_SETTING_HUMIDITY == panel->setting)
    {
        if (APL_HUMIDITY_TARGET_OFF == panel->setting_value)
        {
            apl_hmi_digits_text(state, ' ', 'O', 'F');
        }
        else
        {
            apl_hmi_digits_number(state, panel->setting_value);
        }
        state->digit_unit = APL_HMI_UNIT_PERCENT;
        state->digit_brightness = apl_hmi_on(apl_hmi_blink(panel->clock, APL_HMI_BLINK_SLOW_MS,
                                                           APL_HMI_BLINK_SLOW_MS));
        return;
    }

    if (0u != panel->query_ticks)
    {
        apl_hmi_digits_number(state, (0u != snap->purify_enabled) ? snap->filter_purify_left
                                                                  : snap->filter_humidify_left);
        state->digit_unit = APL_HMI_UNIT_NONE;
        return;
    }

    show_humidity = (0u == snap->purify_enabled) ? 1u : 0u;
    if (0u != apl_hmi_panel_combo_model(snap))
    {
        show_humidity = (panel->rotate_ticks >= APL_HMI_ROTATE_TICKS) ? 1u : 0u;
    }

    if (0u != show_humidity)
    {
        apl_hmi_digits_number(state, snap->humidity);
        state->digit_unit = APL_HMI_UNIT_PERCENT;
    }
    else
    {
        apl_hmi_digits_number(state, snap->pm25);
        state->digit_unit = APL_HMI_UNIT_PM25;
    }
}

static void apl_hmi_panel_draw_on(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;
    const struct apl_link_snapshot *snap;

    panel = &state->panel;
    snap = &panel->snap;

    /* 键下灯不受灯光开关影响。 */
    state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_FULL;
    state->key[APL_HMI_KEY_SPEED].brightness = APL_HMI_LAMP_FULL;
    state->key[APL_HMI_KEY_PURIFY].brightness = apl_hmi_on(snap->purify_on);
    state->key[APL_HMI_KEY_HUMIDITY].brightness = apl_hmi_on(snap->humidify_on);
    state->key[APL_HMI_KEY_TIMER].brightness = apl_hmi_on(0u != snap->timer_left_s);
    state->key[APL_HMI_KEY_LIGHT].brightness = apl_hmi_on(0u == panel->light_off);

    if (0u != apl_hmi_panel_dark(state))
    {
        return;
    }

    switch (snap->fan_gear)
    {
    case APL_FAN_GEAR_SLEEP:
        state->icon[APL_HMI_ICON_GEAR_SLEEP] = APL_HMI_LAMP_FULL;
        break;
    case APL_FAN_GEAR_1:
        state->icon[APL_HMI_ICON_GEAR_1] = APL_HMI_LAMP_FULL;
        break;
    case APL_FAN_GEAR_2:
        state->icon[APL_HMI_ICON_GEAR_2] = APL_HMI_LAMP_FULL;
        break;
    case APL_FAN_GEAR_3:
        state->icon[APL_HMI_ICON_GEAR_3] = APL_HMI_LAMP_FULL;
        break;
    case APL_FAN_GEAR_AUTO:
        state->icon[APL_HMI_ICON_GEAR_AUTO] = APL_HMI_LAMP_FULL;
        break;
    default:
        /* 极速的呈现尚未规定，档位图标全灭。 */
        break;
    }

    if (APL_WIFI_CONNECTED == snap->wifi)
    {
        state->icon[APL_HMI_ICON_WIFI] = APL_HMI_LAMP_FULL;
    }
    else if (APL_WIFI_PAIRING == snap->wifi)
    {
        state->icon[APL_HMI_ICON_WIFI] = apl_hmi_on(apl_hmi_blink(panel->clock,
                                                                  APL_HMI_BLINK_SLOW_MS,
                                                                  APL_HMI_BLINK_SLOW_MS));
    }
    else
    {
        /* 未配网。 */
    }

    state->icon[APL_HMI_ICON_LOCK] = apl_hmi_on(APL_HMI_CHILD_LOCK_ON == state->child_lock);
    state->icon[APL_HMI_ICON_ANION] = apl_hmi_on(APL_SWITCH_ON == snap->anion);
    state->icon[APL_HMI_ICON_UV] = apl_hmi_on(APL_SWITCH_ON == snap->uv);
    state->icon[APL_HMI_ICON_HUMIDITY] =
        apl_hmi_on((0u != snap->humidify_on)
                   && (APL_HUMIDITY_TARGET_OFF != snap->humidity_target));
    apl_hmi_panel_draw_filter(state);
    apl_hmi_panel_draw_water(state);
    apl_hmi_panel_draw_ambient(state);
    apl_hmi_panel_draw_digits(state);
}

static void apl_hmi_panel_draw(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    apl_hmi_panel_clear(state);
    switch (panel->snap.run_state)
    {
    case APL_RUN_ON:
        apl_hmi_panel_draw_on(state);
        break;
    case APL_RUN_DRY:
        /* 风干只亮电源键灯和风干图标。 */
        state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_FULL;
        state->icon[APL_HMI_ICON_DRY] = APL_HMI_LAMP_FULL;
        break;
    case APL_RUN_FACTORY:
        /* 产测：全部灯光点亮供目检。逐个点亮的顺序尚未规定。 */
        apl_hmi_lamps_fill(state, APL_HMI_LAMP_FULL);
        apl_hmi_digits_text(state, '8', '8', '8');
        break;
    default:
        state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_HALF;
        if (0u != panel->snap.factory_done)
        {
            apl_hmi_digits_text(state, ' ', 'C', 'L');
            state->digit_brightness = APL_HMI_LAMP_FULL;
        }
        break;
    }
}

/** 快照变化引起的一次性效果：提示音、红灯闪烁、水满闪烁、睡眠熄屏计时。 */
static void apl_hmi_panel_events(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;
    const struct apl_link_snapshot *snap;

    panel = &state->panel;
    snap = &panel->snap;

    if (snap->beep_seq != panel->last_beep_seq)
    {
        panel->last_beep_seq = snap->beep_seq;
        apl_hmi_beep_play(state, snap->beep);
        if (APL_BEEP_WATER_FULL == snap->beep)
        {
            panel->full_blink_ticks = (uint16_t)(APL_HMI_FULL_BLINK_TIMES * 2u
                                                 * APL_HMI_BLINK_FAST_MS / APL_HMI_TICK_MS);
        }
    }

    if (snap->filter_reset_seq != panel->last_reset_seq)
    {
        panel->last_reset_seq = snap->filter_reset_seq;
        if (0u != snap->filter_reset_forced)
        {
            panel->red_blink_period = APL_HMI_BLINK_FAST_MS;
            panel->red_blink_ticks = (uint16_t)(APL_HMI_BLINK_TIMES * 2u
                                                * APL_HMI_BLINK_FAST_MS / APL_HMI_TICK_MS);
        }
    }

    if ((APL_RUN_ON == snap->run_state) && (APL_RUN_ON != panel->prev_run))
    {
        panel->light_off = 0u;
        panel->rotate_ticks = 0u;
        if (0u != apl_hmi_panel_filter_expired(snap))
        {
            /* 到期后开机：红灯闪 3 次后常亮。 */
            panel->red_blink_period = APL_HMI_BLINK_SLOW_MS;
            panel->red_blink_ticks = (uint16_t)(APL_HMI_BLINK_TIMES * 2u
                                                * APL_HMI_BLINK_SLOW_MS / APL_HMI_TICK_MS);
        }
    }

    if (APL_RUN_ON != snap->run_state)
    {
        panel->setting = APL_HMI_SETTING_NONE;
        panel->query_ticks = 0u;
    }

    if ((0u != snap->sleep) && (0u == panel->prev_sleep))
    {
        /* 进入睡眠：3 秒无操作后熄灭状态显示。 */
        panel->wake_ticks = APL_HMI_IDLE_TICKS;
    }

    if ((APL_WATER_ALERT_BLINK == snap->water_alert) && (APL_WATER_ALERT_BLINK != panel->prev_alert))
    {
        panel->alert_clock = panel->clock;
    }

    panel->prev_alert = snap->water_alert;
    panel->prev_run = snap->run_state;
    panel->prev_sleep = snap->sleep;
}

static void apl_hmi_panel_tick(struct apl_hmi_state *state)
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (0u == panel->snap_valid)
    {
        return;
    }

    panel->clock++;
    apl_hmi_panel_events(state);
    apl_hmi_panel_confirm(state);

    if ((0u != panel->wake_ticks) && (0u != panel->keys_down))
    {
        panel->wake_ticks = APL_HMI_IDLE_TICKS;
    }
    else if (0u != panel->wake_ticks)
    {
        panel->wake_ticks--;
    }
    else
    {
        /* 已熄屏。 */
    }

    if (0u != panel->query_ticks)
    {
        panel->query_ticks--;
    }

    if (0u != panel->red_blink_ticks)
    {
        panel->red_blink_ticks--;
    }

    if (0u != panel->full_blink_ticks)
    {
        panel->full_blink_ticks--;
    }

    panel->rotate_ticks++;
    if (panel->rotate_ticks >= (uint16_t)(2u * APL_HMI_ROTATE_TICKS))
    {
        panel->rotate_ticks = 0u;
    }

    apl_hmi_panel_draw(state);
}

void apl_hmi_link_bind(struct apl_hmi *self,
                       enum apl_link_result (*send)(const struct apl_link_command *command,
                                                    void *ctx),
                       void *ctx)
{
    struct apl_hmi_state *state;
    static const enum apl_hmi_key_id filter_keys[2] = { APL_HMI_KEY_TIMER, APL_HMI_KEY_SPEED };
    static const enum apl_hmi_key_id factory_keys[2] = { APL_HMI_KEY_POWER, APL_HMI_KEY_TIMER };

    if (NULL == self)
    {
        return;
    }

    state = apl_hmi_state_of(self);
    state->panel.send = send;
    state->panel.send_ctx = ctx;
    if (NULL == send)
    {
        return;
    }

    (void)apl_hmi_set_combo(self, filter_keys, 2u, (uint16_t)APL_HMI_LONG_PRESS_MS,
                            apl_hmi_panel_combo_filter, NULL);
    (void)apl_hmi_set_combo(self, factory_keys, 2u, (uint16_t)APL_HMI_LONG_PRESS_MS,
                            apl_hmi_panel_combo_factory, NULL);
}

void apl_hmi_set_snapshot(struct apl_hmi *self, const struct apl_link_snapshot *snapshot)
{
    struct apl_hmi_state *state;

    if ((NULL == self) || (NULL == snapshot))
    {
        return;
    }

    state = apl_hmi_state_of(self);
    if (0u == state->panel.snap_valid)
    {
        /* 第一份快照只作起点，不补播此前的提示音。 */
        state->panel.last_beep_seq = snapshot->beep_seq;
        state->panel.last_reset_seq = snapshot->filter_reset_seq;
        state->panel.prev_run = APL_RUN_STANDBY;
    }

    state->panel.snap = *snapshot;
    state->panel.snap_valid = 1u;
    if (NULL != state->panel.send)
    {
        state->purify_enabled = (0u != snapshot->purify_enabled) ? 1u : 0u;
        state->humidify_enabled = (0u != snapshot->humidify_enabled) ? 1u : 0u;
    }
}

void apl_hmi_filter_query(struct apl_hmi *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->panel.query_ticks = APL_HMI_IDLE_TICKS;
}

uint8_t apl_hmi_get_light(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return (0u == apl_hmi_state_of_const(self)->panel.light_off) ? 1u : 0u;
}
