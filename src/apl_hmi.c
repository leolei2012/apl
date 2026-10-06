/**
 * @file    apl_hmi.c
 * @brief   人机手势。短按、长按、组合键和童锁。不判断净化或加湿业务。
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
    if ((NULL == state->ops) || (NULL == state->ops->key_state_get))
    {
        apl_hmi_powerup_tick(state);
        apl_hmi_force_disabled_lamps(state);
        return;
    }

    memset(&keys, 0, sizeof(keys));
    state->ops->key_state_get(&keys, state->ctx);
    down_mask = apl_hmi_sample(state, &keys);
    locked = (uint8_t)(APL_HMI_CHILD_LOCK_ON == state->child_lock);
    suppress = apl_hmi_combos_update(self, state, down_mask, locked);
    apl_hmi_keys_resolve(self, state, down_mask, suppress, locked);
    apl_hmi_powerup_tick(state);
    apl_hmi_force_disabled_lamps(state);
}

uint8_t apl_hmi_powerup_lamp_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return (uint8_t)(0u != apl_hmi_state_of_const(self)->powerup_lamp_ticks);
}

void apl_hmi_key_lamp_set(struct apl_hmi *self, enum apl_hmi_key_id key, uint8_t brightness)
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

uint8_t apl_hmi_key_lamp_get(const struct apl_hmi *self, enum apl_hmi_key_id key)
{
    if ((NULL == self) || (0u == apl_hmi_key_is_valid(key)))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->key[key].brightness;
}

void apl_hmi_key_callback_set(struct apl_hmi *self,
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

void apl_hmi_ambient_brightness_set(struct apl_hmi *self, uint8_t brightness)
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->ambient_brightness = brightness;
}

uint8_t apl_hmi_ambient_brightness_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->ambient_brightness;
}

void apl_hmi_ambient_color_set(struct apl_hmi *self,
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

void apl_hmi_ambient_color_get(const struct apl_hmi *self,
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

void apl_hmi_water_lamp_set(struct apl_hmi *self, uint8_t index, uint8_t brightness)
{
    if ((NULL == self) || (0u == index) || (index > APL_HMI_WATER_LAMP_COUNT))
    {
        return;
    }

    apl_hmi_state_of(self)->water[index - 1u] = brightness;
}

uint8_t apl_hmi_water_lamp_get(const struct apl_hmi *self, uint8_t index)
{
    if ((NULL == self) || (0u == index) || (index > APL_HMI_WATER_LAMP_COUNT))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->water[index - 1u];
}

void apl_hmi_digits_brightness_set(struct apl_hmi *self, uint8_t brightness)
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->digit_brightness = brightness;
}

uint8_t apl_hmi_digits_brightness_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->digit_brightness;
}

void apl_hmi_digits_content_set(struct apl_hmi *self,
                                const char content[APL_HMI_DIGIT_COUNT])
{
    if ((NULL == self) || (NULL == content))
    {
        return;
    }

    memcpy(apl_hmi_state_of(self)->digit_content, content, APL_HMI_DIGIT_COUNT);
}

void apl_hmi_digits_content_get(const struct apl_hmi *self,
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

void apl_hmi_digits_unit_set(struct apl_hmi *self, enum apl_hmi_unit unit)
{
    if ((NULL == self) || (0u == apl_hmi_unit_is_valid(unit)))
    {
        return;
    }

    apl_hmi_state_of(self)->digit_unit = unit;
}

enum apl_hmi_unit apl_hmi_digits_unit_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return APL_HMI_UNIT_NONE;
    }

    return apl_hmi_state_of_const(self)->digit_unit;
}

void apl_hmi_child_lock_set(struct apl_hmi *self, enum apl_hmi_child_lock lock)
{
    if ((NULL == self) || (0u == apl_hmi_child_lock_is_valid(lock)))
    {
        return;
    }

    apl_hmi_state_of(self)->child_lock = lock;
}

enum apl_hmi_child_lock apl_hmi_child_lock_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return APL_HMI_CHILD_LOCK_OFF;
    }

    return apl_hmi_state_of_const(self)->child_lock;
}

void apl_hmi_purify_enabled_set(struct apl_hmi *self, uint8_t enabled)
{
    if ((NULL == self) || (0u == apl_hmi_enabled_is_valid(enabled)))
    {
        return;
    }

    apl_hmi_state_of(self)->purify_enabled = enabled;
}

uint8_t apl_hmi_purify_enabled_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->purify_enabled;
}

void apl_hmi_humidify_enabled_set(struct apl_hmi *self, uint8_t enabled)
{
    if ((NULL == self) || (0u == apl_hmi_enabled_is_valid(enabled)))
    {
        return;
    }

    apl_hmi_state_of(self)->humidify_enabled = enabled;
}

uint8_t apl_hmi_humidify_enabled_get(const struct apl_hmi *self)
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->humidify_enabled;
}

int apl_hmi_combo_set(struct apl_hmi *self,
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
