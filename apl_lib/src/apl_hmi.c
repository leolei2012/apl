/**
 * @file    apl_hmi.c
 * @brief   人机手势、显示对象和面板逻辑。业务判断在主控，这里只转操作、按快照绘制。
 */

#include "apl_hmi.h"
#include "apl_internal.h"

#include <stddef.h>
#include <string.h>

#if defined(__GNUC__)
#define APL_HMI_MAY_ALIAS __attribute__((__may_alias__))
#else
#define APL_HMI_MAY_ALIAS
#endif

#define APL_HMI_HAS_WARMUP (APL_COMPILE_SENSOR_PM25 || APL_COMPILE_SENSOR_PM10 || APL_COMPILE_SENSOR_HCHO || APL_COMPILE_SENSOR_TVOC || APL_COMPILE_SENSOR_HUMIDITY)
#define APL_HMI_HAS_ROTATE (APL_COMPILE_SENSOR_PM25 && APL_COMPILE_SENSOR_HUMIDITY)
#define APL_HMI_HAS_CLOCK (APL_COMPILE_TIMER || APL_COMPILE_HUMIDIFY || APL_COMPILE_WIFI)

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
#define APL_HMI_WARMUP_FRAME_TICKS  30u /* 300ms: top -> middle -> bottom */

APL_STATIC_ASSERT(APL_HMI_KEY_COUNT <= 8, "键掩码放在 uint8_t 里");
APL_STATIC_ASSERT(APL_HMI_LONG_PRESS_TICKS == 300u, "3 秒在 10 ms 拍上是 300 拍");
APL_STATIC_ASSERT(APL_HMI_POWERUP_LAMP_TICKS == (APL_HMI_POWERUP_LAMP_MS / APL_HMI_TICK_MS),
               "上电全灯 2 秒");
APL_STATIC_ASSERT(APL_HMI_LAMP_HALF == 128u, "半亮是 128");
APL_STATIC_ASSERT(APL_HMI_LAMP_FULL == 255u, "全亮是 255");
APL_STATIC_ASSERT(APL_HMI_VERSION_NUM == 0x000300u, "版本号与分段宏不一致");
APL_STATIC_ASSERT(APL_HMI_WATER_LAMP_COUNT == 4u, "水位灯是四段");
#if APL_HMI_COMPILE_KEYMAP
APL_STATIC_ASSERT(APL_HMI_KEY_COUNT == APL_KEYMAP_COUNT, "映射与输入槽位数量须一致");
#endif

struct apl_hmi_key
{
    uint8_t brightness;
#if APL_HMI_COMPILE_KEY_CALLBACKS
    void (*on_short)(struct apl_hmi *self, void *ctx) APL_CALL;
    void (*on_long)(struct apl_hmi *self, void *ctx) APL_CALL;
    void *ctx;
#endif
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
    void (*on_combo)(struct apl_hmi *self, void *ctx) APL_CALL;
    void *ctx;
};

/** 面板逻辑的本地状态。只在接上 link 后使用。 */
struct apl_hmi_panel
{
    enum apl_link_result (*send)(const struct apl_link_command *command, void *ctx) APL_CALL;
    void *send_ctx;
    struct apl_link_snapshot snap;
#if APL_HMI_HAS_CLOCK
    uint32_t clock;
#endif
    enum apl_run_state prev_run;
    uint16_t wake_ticks;
    uint16_t setting_ticks;
#if APL_COMPILE_TIMER
    uint16_t timer_repeat_ticks;
    uint8_t timer_editing;
#endif
#if APL_COMPILE_FILTER
    uint16_t query_ticks;
#endif
#if APL_HMI_HAS_ROTATE
    uint32_t rotate_ticks;
#endif
#if APL_HMI_HAS_WARMUP
    uint16_t warmup_animation_ticks;
#endif
#if APL_COMPILE_FILTER
    uint16_t red_blink_ticks;
    uint16_t red_blink_period;
#endif
#if APL_COMPILE_HUMIDIFY
    uint16_t full_blink_ticks;
#endif
    uint8_t snap_valid;
    uint8_t light_off;
#if APL_COMPILE_SLEEP
    uint8_t prev_sleep;
#endif
    uint8_t setting;          /**< APL_HMI_SETTING_* */
    uint8_t setting_value;
#if (APL_HMI_COMPILE_BUZZER || APL_COMPILE_HUMIDIFY)
    uint8_t last_beep_seq;
#endif
#if APL_COMPILE_FILTER
    uint8_t last_reset_seq;
#endif
#if APL_COMPILE_HUMIDIFY
    uint32_t alert_clock;     /**< 缺水闪烁从提醒开始时计相位 */
    enum apl_water_alert prev_alert;
#endif
    uint8_t keys_down;        /**< 本拍按住的键。按住期间唤醒窗口不倒计时 */
};

/** 对外对象里的真实状态。允许与占位对象别名，使用方不直接访问。 */
struct APL_HMI_MAY_ALIAS apl_hmi_state
{
    struct apl_hmi_key key[APL_HMI_KEY_COUNT];
#if APL_HMI_COMPILE_AMBIENT
    uint8_t ambient_brightness;
    uint8_t ambient_red;
    uint8_t ambient_green;
    uint8_t ambient_blue;
    uint8_t ambient_out_red;
    uint8_t ambient_out_green;
    uint8_t ambient_out_blue;
    uint8_t ambient_output_valid;
#endif
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
#if APL_HMI_COMPILE_KEYMAP
    const struct apl_keymap *keymap;
#endif
    struct apl_hmi_key_track track[APL_HMI_KEY_COUNT];
#if APL_HMI_COMBO_MAX > 0
    struct apl_hmi_combo_slot combo[APL_HMI_COMBO_MAX];
#endif
    uint8_t icon[APL_HMI_ICON_COUNT];
    enum apl_hmi_water_color water_color;
#if APL_HMI_COMPILE_BUZZER
    uint8_t buzz_count;
    uint8_t buzz_on;
    uint16_t buzz_on_ticks;
    uint16_t buzz_off_ticks;
    uint16_t buzz_phase;
#endif
    struct apl_hmi_panel panel;
};

#if APL_COMPILE_HMI
APL_STATIC_ASSERT(sizeof(struct apl_hmi_state) <= sizeof(struct apl_hmi),
               "APL_HMI_OPAQUE_SIZE 小于对象状态");
APL_STATIC_ASSERT(APL_ALIGNOF(struct apl_hmi_state) <= APL_ALIGNOF(struct apl_hmi),
               "占位对象的对齐小于对象状态");

static struct apl_hmi_state *apl_hmi_state_of(struct apl_hmi *self) APL_CALL
{
    return (struct apl_hmi_state *)(void *)self->storage.bytes;
}

static const struct apl_hmi_state *apl_hmi_state_of_const(const struct apl_hmi *self) APL_CALL
{
    return (const struct apl_hmi_state *)(const void *)self->storage.bytes;
}

#if APL_HMI_COMPILE_KEYMAP
static uint8_t apl_hmi_key_is_compiled(enum apl_hmi_key_id key) APL_CALL;

static uint8_t apl_hmi_keymap_active(const struct apl_hmi_state *state) APL_CALL
{
    return ((NULL != state->panel.send) && (NULL != state->keymap)
            && (0u != state->keymap->enabled)) ? 1u : 0u;
}

static uint8_t apl_hmi_mapped_action(const struct apl_hmi_state *state,
                                    uint8_t index, uint8_t is_long) APL_CALL
{
    return (0u != is_long) ? state->keymap->long_action[index]
                           : state->keymap->short_action[index];
}

/** 能力检查与输入槽位无关。运行状态限制仍由对应动作/主控处理。 */
static uint8_t apl_hmi_action_available(const struct apl_hmi_state *state, uint8_t action) APL_CALL
{
    const struct apl_link_snapshot *snap;

    snap = &state->panel.snap;
    APL_UNUSED_PARAM(snap);
    switch (action)
    {
    case APL_KEY_ACTION_POWER:
    case APL_KEY_ACTION_FAN_CYCLE:
    case APL_KEY_ACTION_CHILD_LOCK_TOGGLE:
    case APL_KEY_ACTION_LIGHT_TOGGLE:
        return 1u;
#if APL_COMPILE_DRY
    case APL_KEY_ACTION_DRY_STOP:
        return (0u != snap->humidify_enabled) ? 1u : 0u;
#endif
#if APL_CORE_HAS_TURBO
    case APL_KEY_ACTION_TURBO_TOGGLE:
        return 1u;
#endif
#if (APL_COMPILE_UV || APL_COMPILE_ANION)
    case APL_KEY_ACTION_PURIFY_TOGGLE:
        return ((0u != snap->uv_enabled) || (0u != snap->anion_enabled)) ? 1u : 0u;
#endif
#if APL_COMPILE_WIFI
    case APL_KEY_ACTION_WIFI_PAIR:
        return (0u != snap->wifi_enabled) ? 1u : 0u;
#endif
#if APL_COMPILE_TIMER
    case APL_KEY_ACTION_TIMER_EDIT:
        return 1u;
#endif
#if APL_COMPILE_HUMIDIFY
    case APL_KEY_ACTION_HUMIDITY_CYCLE:
        return ((0u != snap->humidify_enabled)
                && ((0u != snap->purify_enabled)
                    || (0u != (snap->sensors_configured & APL_SENSOR_HUMIDITY_MASK)))) ? 1u : 0u;
    case APL_KEY_ACTION_HUMIDITY_EDIT:
        return ((0u != snap->purify_enabled) && (0u != snap->humidify_enabled)
                && (0u != (snap->sensors_configured & APL_SENSOR_HUMIDITY_MASK))) ? 1u : 0u;
    case APL_KEY_ACTION_HUMIDIFY_TOGGLE:
        return (0u != snap->humidify_enabled) ? 1u : 0u;
#endif
#if APL_COMPILE_FILTER
    case APL_KEY_ACTION_FILTER_QUERY:
    case APL_KEY_ACTION_FILTER_RESET_QUERY:
        return ((0u != snap->purify_enabled) && (0u == snap->humidify_enabled)) ? 1u : 0u;
#endif
#if APL_COMPILE_SLEEP
    case APL_KEY_ACTION_SLEEP_TOGGLE:
        return 1u;
#endif
    default:
        return 0u;
    }
}

static uint8_t apl_hmi_mapped_key_available(const struct apl_hmi_state *state, uint8_t index) APL_CALL
{
    return ((0u != apl_hmi_action_available(state, apl_hmi_mapped_action(state, index, 0u)))
            || (0u != apl_hmi_action_available(state, apl_hmi_mapped_action(state, index, 1u)))) ? 1u : 0u;
}

static void apl_hmi_mapped_power_lamps(struct apl_hmi_state *state, uint8_t brightness) APL_CALL
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        if ((0u != apl_hmi_key_is_compiled((enum apl_hmi_key_id)index))
            && ((APL_KEY_ACTION_POWER == apl_hmi_mapped_action(state, index, 0u))
                || (APL_KEY_ACTION_POWER == apl_hmi_mapped_action(state, index, 1u))))
        {
            state->key[index].brightness = brightness;
        }
    }
}
#endif

static uint8_t apl_hmi_key_is_compiled(enum apl_hmi_key_id key) APL_CALL
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
    case APL_HMI_KEY_FILTER:
#if APL_HMI_COMPILE_KEY_FILTER
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

static uint8_t apl_hmi_key_is_valid(enum apl_hmi_key_id key) APL_CALL
{
    if ((unsigned)key >= APL_HMI_KEY_COUNT)
    {
        return 0u;
    }

    return apl_hmi_key_is_compiled(key);
}

static uint8_t apl_hmi_key_bit(uint8_t index) APL_CALL
{
    return (uint8_t)(1u << index);
}

static uint8_t apl_hmi_child_lock_is_valid(enum apl_hmi_child_lock lock) APL_CALL
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

static uint8_t apl_hmi_unit_is_valid(enum apl_hmi_unit unit) APL_CALL
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

static uint8_t apl_hmi_enabled_is_valid(uint8_t enabled) APL_CALL
{
    return (uint8_t)((0u == enabled) || (1u == enabled));
}

static enum apl_hmi_key_level apl_hmi_level_at(const struct apl_hmi_key_state *keys,
                                               uint8_t index) APL_CALL
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
    case APL_HMI_KEY_FILTER:
#if APL_HMI_COMPILE_KEY_FILTER
        return keys->filter;
#else
        return APL_HMI_KEY_RELEASED;
#endif
    default:
        return APL_HMI_KEY_RELEASED;
    }
}

/** 未启用的功能，键灯不许亮。短按也不发出。长按限制尚未规定。 */
static uint8_t apl_hmi_lamp_forced_off(const struct apl_hmi_state *state, uint8_t index) APL_CALL
{
#if APL_HMI_COMPILE_KEYMAP
    if (0u != apl_hmi_keymap_active(state))
    {
        return (0u == apl_hmi_mapped_key_available(state, index)) ? 1u : 0u;
    }
#endif
    if ((APL_HMI_KEY_PURIFY == index) && (0u == state->purify_enabled))
    {
        return 1u;
    }

    if ((APL_HMI_KEY_HUMIDITY == index) && (0u == state->humidify_enabled))
    {
        return 1u;
    }

    if (NULL != state->panel.send)
    {
        if ((APL_HMI_KEY_PURIFY == index) && (0u == state->panel.snap.uv_enabled)
            && (0u == state->panel.snap.anion_enabled))
        {
            return 1u;
        }
        if ((APL_HMI_KEY_HUMIDITY == index) && (0u == state->panel.snap.purify_enabled)
            && (0u == (state->panel.snap.sensors_configured & APL_SENSOR_HUMIDITY_MASK)))
        {
            return 1u;
        }
    }
    if ((APL_HMI_KEY_FILTER == index) && (NULL != state->panel.send)
        && ((0u == state->purify_enabled) || (0u != state->humidify_enabled)))
    {
        return 1u;
    }
    return 0u;
}

static void apl_hmi_force_disabled_lamps(struct apl_hmi_state *state) APL_CALL
{
#if APL_HMI_COMPILE_KEYMAP
    uint8_t index;

    if (0u != apl_hmi_keymap_active(state))
    {
        for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
        {
            if ((0u == apl_hmi_key_is_compiled((enum apl_hmi_key_id)index))
                || (0u == apl_hmi_mapped_key_available(state, index)))
            {
                state->key[index].brightness = 0u;
            }
        }
        return;
    }
#endif
    if ((0u == apl_hmi_key_is_compiled(APL_HMI_KEY_FILTER))
        || (0u != apl_hmi_lamp_forced_off(state, APL_HMI_KEY_FILTER)))
    {
        state->key[APL_HMI_KEY_FILTER].brightness = 0u;
    }
    if ((0u == state->purify_enabled) || (0u != apl_hmi_lamp_forced_off(state, APL_HMI_KEY_PURIFY)))
    {
        state->key[APL_HMI_KEY_PURIFY].brightness = 0u;
    }

    if ((0u == state->humidify_enabled) || (0u != apl_hmi_lamp_forced_off(state, APL_HMI_KEY_HUMIDITY)))
    {
        state->key[APL_HMI_KEY_HUMIDITY].brightness = 0u;
    }
}

static void apl_hmi_lamps_fill(struct apl_hmi_state *state, uint8_t brightness) APL_CALL
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        if (0u != apl_hmi_key_is_compiled((enum apl_hmi_key_id)index))
        {
            state->key[index].brightness = brightness;
        }
    }

#if APL_HMI_COMPILE_AMBIENT
    state->ambient_brightness = brightness;

    /* 全灯自检/产测使用RGB三通道全亮；亮度由输出阶段统一合并。 */
    state->ambient_red = APL_HMI_LAMP_FULL;
    state->ambient_green = APL_HMI_LAMP_FULL;
    state->ambient_blue = APL_HMI_LAMP_FULL;
#endif

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

static void apl_hmi_powerup_show(struct apl_hmi_state *state) APL_CALL
{
    uint8_t index;

    apl_hmi_lamps_fill(state, APL_HMI_LAMP_FULL);

    for (index = 0u; index < APL_HMI_DIGIT_COUNT; index++)
    {
        state->digit_content[index] = '8';
    }

    state->digit_unit = APL_HMI_UNIT_NONE;
}

static void apl_hmi_powerup_idle(struct apl_hmi_state *state) APL_CALL
{
    uint8_t index;

    apl_hmi_lamps_fill(state, 0u);

#if APL_HMI_COMPILE_KEYMAP
    if (0u != apl_hmi_keymap_active(state))
    {
        apl_hmi_mapped_power_lamps(state, APL_HMI_LAMP_HALF);
    }
    else
#endif
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
static void apl_hmi_panel_tick(struct apl_hmi_state *state) APL_CALL;

static void apl_hmi_powerup_tick(struct apl_hmi_state *state) APL_CALL
{
    if (0u == state->powerup_lamp_ticks)
    {
        return;
    }

    state->powerup_lamp_ticks--;

    if (0u == state->powerup_lamp_ticks)
    {
        apl_hmi_powerup_idle(state);
        if ((NULL != state->panel.send) && (0u != state->panel.snap_valid))
        {
            apl_hmi_panel_tick(state);
        }
    }
    else
    {
        apl_hmi_powerup_show(state);
    }
}

static void apl_hmi_panel_key(struct apl_hmi *self,
                              struct apl_hmi_state *state,
                              uint8_t index,
                              uint8_t is_long) APL_CALL;
static void apl_hmi_panel_tick(struct apl_hmi_state *state) APL_CALL;
static uint8_t apl_hmi_panel_dark(const struct apl_hmi_state *state) APL_CALL;
#if APL_COMPILE_TIMER
static void apl_hmi_panel_timer_key(struct apl_hmi_state *state) APL_CALL;
#endif

#if APL_HMI_COMPILE_BUZZER
static void apl_hmi_buzzer_tick(struct apl_hmi_state *state) APL_CALL;
#endif

/**
 * locked 用本拍开始时的童锁。回调里改锁，要等下一拍才影响其他键，
 * 避免同一次采样里解锁顺带把别的键也放出去。
 */
static void apl_hmi_emit(struct apl_hmi *self,
                         struct apl_hmi_state *state,
                         uint8_t index,
                         uint8_t locked,
                         uint8_t is_long) APL_CALL
{
#if APL_HMI_COMPILE_KEY_CALLBACKS
    struct apl_hmi_key *key;
    void (*callback)(struct apl_hmi *emit_self, void *emit_ctx) APL_CALL;
#endif
#if APL_HMI_COMPILE_KEYMAP
    uint8_t action;
#endif

    if (index >= APL_HMI_KEY_COUNT)
    {
        return;
    }

#if APL_HMI_COMPILE_KEYMAP
    if (0u != apl_hmi_keymap_active(state))
    {
        action = apl_hmi_mapped_action(state, index, is_long);
        if ((0u != locked) && (APL_KEY_ACTION_CHILD_LOCK_TOGGLE != action))
        {
            return;
        }
        /* 事件保持原始槽位，包括 NONE/不可用动作，便于宿主检测输入。 */
        if ((NULL != state->ops) && (NULL != state->ops->key_event))
        {
            state->ops->key_event((enum apl_hmi_key_id)index, is_long, state->ctx);
        }
        if (0u != apl_hmi_action_available(state, action))
        {
            apl_hmi_panel_key(self, state, index, is_long);
        }
        return;
    }
#endif

    if (((0u == is_long) || (APL_HMI_KEY_FILTER == index))
        && (0u != apl_hmi_lamp_forced_off(state, index)))
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

#if APL_HMI_COMPILE_KEY_CALLBACKS
    key = &state->key[index];
    callback = (0u != is_long) ? key->on_long : key->on_short;
    if (NULL != callback)
    {
        callback(self, key->ctx);
    }
#endif
}

#if APL_HMI_COMBO_MAX > 0
static uint8_t apl_hmi_mask_held(uint8_t down_mask, uint8_t key_mask) APL_CALL
{
    if (0u == key_mask)
    {
        return 0u;
    }

    return (uint8_t)((down_mask & key_mask) == key_mask);
}

static void apl_hmi_consume_mask(struct apl_hmi_state *state, uint8_t key_mask) APL_CALL
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
                                     uint8_t locked) APL_CALL
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
#endif

/** D21：仅在定时设置中武装连增，设置外3秒长按仍负责童锁。 */
#if APL_COMPILE_TIMER
static void apl_hmi_timer_repeat(struct apl_hmi_state *state,
                                 struct apl_hmi_key_track *track,
                                 uint8_t down_mask,
                                 uint8_t suppress,
                                 uint8_t locked,
                                 uint8_t timer_bit) APL_CALL
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (0u == track->down)
    {
        panel->timer_editing = ((NULL != panel->send) && (0u != panel->snap_valid)
                               && (APL_RUN_ON == panel->snap.run_state)
                               && (APL_HMI_SETTING_TIMER == panel->setting)
                               && (0u == locked) && (down_mask == timer_bit)
                               && (0u == apl_hmi_panel_dark(state))) ? 1u : 0u;
        panel->timer_repeat_ticks = 150u;
    }
    if (0u == panel->timer_editing)
    {
        return;
    }
    if ((0u != locked) || (down_mask != timer_bit) || (0u != (suppress & timer_bit))
        || (APL_HMI_SETTING_TIMER != panel->setting) || (APL_RUN_ON != panel->snap.run_state))
    {
        panel->timer_editing = 0u;
        track->gesture = APL_HMI_GESTURE_CONSUMED;
        return;
    }
    panel->setting_ticks = APL_HMI_IDLE_TICKS;
    if (0u != panel->timer_repeat_ticks)
    {
        panel->timer_repeat_ticks--;
    }
    if (0u == panel->timer_repeat_ticks)
    {
        track->gesture = APL_HMI_GESTURE_CONSUMED;
        apl_hmi_panel_timer_key(state);
        panel->timer_repeat_ticks = (0u == panel->setting_value) ? 200u : 50u;
    }
}

#endif

static void apl_hmi_keys_resolve(struct apl_hmi *self,
                                 struct apl_hmi_state *state,
                                 uint8_t down_mask,
                                 uint8_t suppress,
                                 uint8_t locked) APL_CALL
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        struct apl_hmi_key_track *track;
        uint8_t down;
        uint8_t key_bit;

        track = &state->track[index];
        key_bit = apl_hmi_key_bit(index);
        down = (uint8_t)(0u != (down_mask & key_bit));
        if (0u != down)
        {
#if APL_COMPILE_TIMER
            if (
#if APL_HMI_COMPILE_KEYMAP
                (0u != apl_hmi_keymap_active(state))
                ? (APL_KEY_ACTION_TIMER_EDIT == apl_hmi_mapped_action(state, index, 0u))
                :
#endif
                  (APL_HMI_KEY_TIMER == index))
            {
                apl_hmi_timer_repeat(state, track, down_mask, suppress, locked, key_bit);
            }
#endif
            if ((APL_HMI_GESTURE_OPEN == track->gesture)
                && (0u == (suppress & key_bit))
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

#if APL_COMPILE_TIMER
            if (
#if APL_HMI_COMPILE_KEYMAP
                (0u != apl_hmi_keymap_active(state))
                ? (APL_KEY_ACTION_TIMER_EDIT == apl_hmi_mapped_action(state, index, 0u))
                :
#endif
                  (APL_HMI_KEY_TIMER == index))
            {
                state->panel.timer_editing = 0u;
            }
#endif
            track->down_ticks = 0u;
            track->gesture = APL_HMI_GESTURE_OPEN;
        }

        track->down = down;
    }
}

static uint8_t apl_hmi_sample(struct apl_hmi_state *state, const struct apl_hmi_key_state *keys) APL_CALL
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

void apl_hmi_init(struct apl_hmi *self, const struct apl_hmi_ops *ops, void *ctx) APL_CALL
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
#if APL_HMI_COMPILE_BUZZER
    state->buzz_count = 1u;
    state->buzz_on_ticks = APL_HMI_BEEP_SHORT_MS / APL_HMI_TICK_MS;
    state->buzz_off_ticks = 0u;
#endif
}

#if APL_HMI_COMPILE_KEYMAP
void apl_hmi_set_keymap(struct apl_hmi *self, const struct apl_keymap *keymap) APL_CALL
{
    if (NULL != self)
    {
        apl_hmi_state_of(self)->keymap = ((NULL != keymap) && (0u != keymap->enabled)) ? keymap : NULL;
    }
}
#endif

#if APL_HMI_COMPILE_AMBIENT
static void apl_hmi_apply_ambient(struct apl_hmi_state *state) APL_CALL
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    if ((NULL == state->ops) || (NULL == state->ops->set_ambient_rgb)) { return; }
    red = (uint8_t)(((uint16_t)state->ambient_red * state->ambient_brightness + 127u) / 255u);
    green = (uint8_t)(((uint16_t)state->ambient_green * state->ambient_brightness + 127u) / 255u);
    blue = (uint8_t)(((uint16_t)state->ambient_blue * state->ambient_brightness + 127u) / 255u);
    if ((0u == state->ambient_output_valid) || (red != state->ambient_out_red)
        || (green != state->ambient_out_green) || (blue != state->ambient_out_blue))
    {
        state->ambient_output_valid = 1u;
        state->ambient_out_red = red;
        state->ambient_out_green = green;
        state->ambient_out_blue = blue;
        state->ops->set_ambient_rgb(red, green, blue, state->ctx);
    }
}

#endif

void apl_hmi_tick(struct apl_hmi *self) APL_CALL
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
#if APL_HMI_COMBO_MAX > 0
        suppress = apl_hmi_combos_update(self, state, down_mask, locked);
#else
        suppress = 0u;
#endif
        apl_hmi_keys_resolve(self, state, down_mask, suppress, locked);
        state->panel.keys_down = down_mask;
    }

    if ((NULL != state->panel.send) && (0u == state->powerup_lamp_ticks))
    {
        apl_hmi_panel_tick(state);
    }

    apl_hmi_powerup_tick(state);
    apl_hmi_force_disabled_lamps(state);
#if APL_HMI_COMPILE_BUZZER
    apl_hmi_buzzer_tick(state);
#endif
#if APL_HMI_COMPILE_AMBIENT
    apl_hmi_apply_ambient(state);
#endif
}

uint8_t apl_hmi_get_powerup_lamp(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return (uint8_t)(0u != apl_hmi_state_of_const(self)->powerup_lamp_ticks);
}

void apl_hmi_set_key_lamp(struct apl_hmi *self, enum apl_hmi_key_id key, uint8_t brightness) APL_CALL
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

uint8_t apl_hmi_get_key_lamp(const struct apl_hmi *self, enum apl_hmi_key_id key) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_key_is_valid(key)))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->key[key].brightness;
}

#if APL_HMI_COMPILE_KEY_CALLBACKS
void apl_hmi_set_key_callback(struct apl_hmi *self,
                              enum apl_hmi_key_id key,
                              void (*on_short)(struct apl_hmi *key_self, void *key_ctx) APL_CALL,
                              void (*on_long)(struct apl_hmi *key_self, void *key_ctx) APL_CALL,
                              void *ctx) APL_CALL
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
#endif

#if APL_HMI_COMPILE_AMBIENT
void apl_hmi_set_ambient_brightness(struct apl_hmi *self, uint8_t brightness) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->ambient_brightness = brightness;
}

uint8_t apl_hmi_get_ambient_brightness(const struct apl_hmi *self) APL_CALL
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
                               uint8_t blue) APL_CALL
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
                               uint8_t *blue) APL_CALL
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

#endif

void apl_hmi_set_water_lamp(struct apl_hmi *self, uint8_t index, uint8_t brightness) APL_CALL
{
    if ((NULL == self) || (0u == index) || (index > APL_HMI_WATER_LAMP_COUNT))
    {
        return;
    }

    apl_hmi_state_of(self)->water[index - 1u] = brightness;
}

uint8_t apl_hmi_get_water_lamp(const struct apl_hmi *self, uint8_t index) APL_CALL
{
    if ((NULL == self) || (0u == index) || (index > APL_HMI_WATER_LAMP_COUNT))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->water[index - 1u];
}

void apl_hmi_set_digits_brightness(struct apl_hmi *self, uint8_t brightness) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->digit_brightness = brightness;
}

uint8_t apl_hmi_get_digits_brightness(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->digit_brightness;
}

void apl_hmi_set_digits_content(struct apl_hmi *self,
                                const char content[APL_HMI_DIGIT_COUNT]) APL_CALL
{
    if ((NULL == self) || (NULL == content))
    {
        return;
    }

    memcpy(apl_hmi_state_of(self)->digit_content, content, APL_HMI_DIGIT_COUNT);
}

uint8_t apl_hmi_get_digit_segments(const struct apl_hmi *self, uint8_t index) APL_CALL
{
    char value;
    if ((NULL == self) || (index >= APL_HMI_DIGIT_COUNT)) { return 0u; }
    value = apl_hmi_state_of_const(self)->digit_content[index];
    switch (value)
    {
    case '0': case 'O': return 0x3Fu;
    case '1': return 0x06u;
    case '2': return 0x5Bu;
    case '3': return 0x4Fu;
    case '4': return 0x66u;
    case '5': return 0x6Du;
    case '6': return 0x7Du;
    case '7': return 0x07u;
    case '8': return 0x7Fu;
    case '9': return 0x6Fu;
    case 'A': return 0x77u;
    case 'b': return 0x7Cu;
    case 'C': return 0x39u;
    case 'c': return 0x58u;
    case 'd': return 0x5Eu;
    case 'E': return 0x79u;
    case 'F': return 0x71u;
    case 'H': return 0x76u;
    case 'h': return 0x74u;
    case 'L': return 0x38u;
    case 'o': return 0x5Cu;
    case 'P': return 0x73u;
    case 'U': return 0x3Eu;
    case 'u': return 0x1Cu;
    case '^': return 0x01u;
    case '-': return 0x40u;
    case '_': return 0x08u;
    default: return 0u;
    }
}

void apl_hmi_get_digits_content(const struct apl_hmi *self,
                                char content[APL_HMI_DIGIT_COUNT]) APL_CALL
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

void apl_hmi_set_digits_unit(struct apl_hmi *self, enum apl_hmi_unit unit) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_unit_is_valid(unit)))
    {
        return;
    }

    apl_hmi_state_of(self)->digit_unit = unit;
}

enum apl_hmi_unit apl_hmi_get_digits_unit(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return APL_HMI_UNIT_NONE;
    }

    return apl_hmi_state_of_const(self)->digit_unit;
}

void apl_hmi_set_child_lock(struct apl_hmi *self, enum apl_hmi_child_lock lock) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_child_lock_is_valid(lock)))
    {
        return;
    }

    apl_hmi_state_of(self)->child_lock = lock;
}

enum apl_hmi_child_lock apl_hmi_get_child_lock(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return APL_HMI_CHILD_LOCK_OFF;
    }

    return apl_hmi_state_of_const(self)->child_lock;
}

void apl_hmi_set_purify_enabled(struct apl_hmi *self, uint8_t enabled) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_enabled_is_valid(enabled)))
    {
        return;
    }

    apl_hmi_state_of(self)->purify_enabled = enabled;
}

uint8_t apl_hmi_get_purify_enabled(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->purify_enabled;
}

void apl_hmi_set_humidify_enabled(struct apl_hmi *self, uint8_t enabled) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_enabled_is_valid(enabled)))
    {
        return;
    }

    apl_hmi_state_of(self)->humidify_enabled = enabled;
}

uint8_t apl_hmi_get_humidify_enabled(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->humidify_enabled;
}

#if APL_HMI_COMBO_MAX > 0
int apl_hmi_set_combo(struct apl_hmi *self,
                      const enum apl_hmi_key_id *keys,
                      uint8_t key_count,
                      uint16_t hold_ms,
                      void (*on_combo)(struct apl_hmi *combo_self, void *combo_ctx) APL_CALL,
                      void *ctx) APL_CALL
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
        uint8_t key_bit;

        if (0u == apl_hmi_key_is_valid(keys[index]))
        {
            return -1;
        }

        key_bit = apl_hmi_key_bit((uint8_t)keys[index]);
        if (0u != (mask & key_bit))
        {
            return -1;
        }

        mask |= key_bit;
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
#endif

static uint8_t apl_hmi_icon_is_valid(enum apl_hmi_icon icon) APL_CALL
{
    return ((uint32_t)icon < (uint32_t)APL_HMI_ICON_COUNT) ? 1u : 0u;
}

void apl_hmi_set_icon(struct apl_hmi *self, enum apl_hmi_icon icon, uint8_t brightness) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_icon_is_valid(icon)))
    {
        return;
    }

    apl_hmi_state_of(self)->icon[icon] = brightness;
}

uint8_t apl_hmi_get_icon(const struct apl_hmi *self, enum apl_hmi_icon icon) APL_CALL
{
    if ((NULL == self) || (0u == apl_hmi_icon_is_valid(icon)))
    {
        return 0u;
    }

    return apl_hmi_state_of_const(self)->icon[icon];
}

void apl_hmi_set_water_color(struct apl_hmi *self, enum apl_hmi_water_color color) APL_CALL
{
    if ((NULL == self) || ((APL_HMI_WATER_GREEN != color) && (APL_HMI_WATER_ORANGE != color)))
    {
        return;
    }

    apl_hmi_state_of(self)->water_color = color;
}

enum apl_hmi_water_color apl_hmi_get_water_color(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return APL_HMI_WATER_GREEN;
    }

    return apl_hmi_state_of_const(self)->water_color;
}

#if APL_HMI_COMPILE_BUZZER
static uint16_t apl_hmi_ms_to_ticks(uint16_t ms) APL_CALL
{
    return (uint16_t)((ms + APL_HMI_TICK_MS - 1u) / APL_HMI_TICK_MS);
}

static void apl_hmi_buzzer_write(struct apl_hmi_state *state, uint8_t on) APL_CALL
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

void apl_hmi_buzzer_play(struct apl_hmi *self, uint8_t count, uint16_t on_ms, uint16_t off_ms) APL_CALL
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

uint8_t apl_hmi_get_buzzer_busy(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return (0u != apl_hmi_state_of_const(self)->buzz_count) ? 1u : 0u;
}

/** 一声 = 响 on 拍 + 停 off 拍。最后一声的停顿也走完，便于连续播放时不粘连。 */
static void apl_hmi_buzzer_tick(struct apl_hmi_state *state) APL_CALL
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

static void apl_hmi_play(struct apl_hmi_state *state, uint8_t count, uint16_t on_ms, uint16_t off_ms) APL_CALL
{
    state->buzz_count = count;
    state->buzz_on_ticks = apl_hmi_ms_to_ticks(on_ms);
    state->buzz_off_ticks = apl_hmi_ms_to_ticks(off_ms);
    state->buzz_phase = 0u;
}

static void apl_hmi_beep_play(struct apl_hmi_state *state, enum apl_beep beep) APL_CALL
{
    switch (beep)
    {
    case APL_BEEP_SHORT:
        apl_hmi_play(state, 1u, APL_HMI_BEEP_SHORT_MS, 0u);
        break;
#if APL_COMPILE_FILTER
    case APL_BEEP_FILTER_RESET:
#endif
#if APL_COMPILE_FACTORY
    case APL_BEEP_FACTORY:
#endif
#if (APL_COMPILE_FILTER || APL_COMPILE_FACTORY)
        apl_hmi_play(state, 1u, 500u, 0u);
        break;
#endif
#if APL_COMPILE_FILTER
    case APL_BEEP_FILTER_FORCED:
#endif
#if APL_COMPILE_HUMIDIFY
    case APL_BEEP_WATER_85:
#endif
#if (APL_COMPILE_FILTER || APL_COMPILE_HUMIDIFY)
        apl_hmi_play(state, 1u, 300u, 0u);
        break;
#endif
#if APL_COMPILE_HUMIDIFY
    case APL_BEEP_WATER_FULL:
        apl_hmi_play(state, 4u, 100u, 100u);
        break;
#endif
#if APL_COMPILE_HUMIDIFY
    case APL_BEEP_WATER_SHORTAGE:
#endif
#if APL_COMPILE_UV
    case APL_BEEP_UV_ALARM:
#endif
#if (APL_COMPILE_HUMIDIFY || APL_COMPILE_UV)
        apl_hmi_play(state, 3u, 100u, 100u);
        break;
#endif
    default:
        break;
    }
}

#endif

#if (APL_COMPILE_TIMER || APL_COMPILE_FILTER || APL_COMPILE_HUMIDIFY || APL_COMPILE_WIFI)
static uint8_t apl_hmi_blink(uint32_t clock, uint16_t on_ms, uint16_t off_ms) APL_CALL
{
    uint32_t period;

    period = (uint32_t)(on_ms + off_ms) / APL_HMI_TICK_MS;
    if (0u == period)
    {
        return 1u;
    }

    return ((clock % period) < ((uint32_t)on_ms / APL_HMI_TICK_MS)) ? 1u : 0u;
}

#endif

#if (APL_COMPILE_TIMER || APL_COMPILE_FILTER || APL_COMPILE_HUMIDIFY || APL_COMPILE_SENSOR_PM25 || APL_COMPILE_SENSOR_HUMIDITY)
static void apl_hmi_digits_number(struct apl_hmi_state *state, uint16_t value) APL_CALL
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

#endif

static void apl_hmi_digits_text(struct apl_hmi_state *state, char a, char b, char c) APL_CALL
{
    state->digit_content[0] = a;
    state->digit_content[1] = b;
    state->digit_content[2] = c;
}

#if (APL_COMPILE_HUMIDIFY || APL_HMI_HAS_ROTATE)
static uint8_t apl_hmi_panel_combo_model(const struct apl_link_snapshot *snap) APL_CALL
{
    return ((0u != snap->purify_enabled) && (0u != snap->humidify_enabled)) ? 1u : 0u;
}

#endif

static void apl_hmi_panel_send(struct apl_hmi_state *state,
                               enum apl_link_command_type type,
                               uint8_t arg) APL_CALL
{
    struct apl_link_command command;

    command.type = type;
    command.arg = arg;
    (void)state->panel.send(&command, state->panel.send_ctx);
}

static uint8_t apl_hmi_panel_dark(const struct apl_hmi_state *state) APL_CALL
{
    const struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (APL_RUN_ON != panel->snap.run_state)
    {
        return 0u;
    }

    return (((0u != panel->light_off)
#if APL_COMPILE_SLEEP
             || (0u != panel->snap.sleep)
#endif
            ) && (0u == panel->wake_ticks))
           ? 1u : 0u;
}

static uint16_t apl_hmi_panel_wake_window(const struct apl_hmi_panel *panel) APL_CALL
{
#if APL_COMPILE_SLEEP
    return (0u != panel->snap.sleep) ? APL_HMI_SLEEP_IDLE_TICKS : APL_HMI_IDLE_TICKS;
#else
    APL_UNUSED_PARAM(panel);
    return APL_HMI_IDLE_TICKS;
#endif
}

#if APL_COMPILE_HUMIDIFY
static uint8_t apl_hmi_humidity_next(uint8_t value, uint8_t with_off) APL_CALL
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

#endif

#if APL_COMPILE_TIMER
static void apl_hmi_panel_timer_key(struct apl_hmi_state *state) APL_CALL
{
    struct apl_hmi_panel *panel;
    uint8_t step;
    uint16_t next;

    panel = &state->panel;
    if (APL_RUN_ON != panel->snap.run_state)
    {
        return;
    }

    step = ((0u != panel->snap.timer_step_h) && (panel->snap.timer_step_h <= APL_HMI_TIMER_MAX_H))
           ? panel->snap.timer_step_h : APL_TIMER_STEP_DEFAULT_H;
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
            panel->setting_value = step;
        }
    }
    else
    {
        next = (uint16_t)((uint16_t)panel->setting_value + step);
        panel->setting_value = (next > APL_HMI_TIMER_MAX_H) ? 0u : (uint8_t)next;
    }

    panel->setting_ticks = APL_HMI_IDLE_TICKS;
}

#endif

#if APL_COMPILE_HUMIDIFY
static void apl_hmi_panel_humidity_short(struct apl_hmi_state *state) APL_CALL
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

#endif

#if APL_HMI_COMPILE_KEYMAP
/** 自定义映射只更换语义动作，仍使用标准唤醒、设置和主控命令规则。 */
static void apl_hmi_panel_action(struct apl_hmi_state *state, uint8_t action) APL_CALL
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    if ((0u != apl_hmi_panel_dark(state))
#if APL_COMPILE_SLEEP
        && !((APL_KEY_ACTION_POWER == action) && (APL_FAN_GEAR_SLEEP == panel->snap.fan_gear))
#endif
       )
    {
        panel->wake_ticks = apl_hmi_panel_wake_window(panel);
        return;
    }
    if (0u != panel->wake_ticks)
    {
        panel->wake_ticks = apl_hmi_panel_wake_window(panel);
    }
    switch (action)
    {
    case APL_KEY_ACTION_POWER:
#if APL_COMPILE_FACTORY
        if (APL_RUN_FACTORY == panel->snap.run_state)
        {
            apl_hmi_panel_send(state, APL_LINK_CMD_FACTORY_EXIT, 0u);
        }
        else
#endif
        {
            panel->setting = APL_HMI_SETTING_NONE;
            apl_hmi_panel_send(state, APL_LINK_CMD_POWER, 0u);
        }
        break;
#if APL_COMPILE_DRY
    case APL_KEY_ACTION_DRY_STOP:
        apl_hmi_panel_send(state, APL_LINK_CMD_DRY_STOP, 0u);
        break;
#endif
    case APL_KEY_ACTION_FAN_CYCLE:
        apl_hmi_panel_send(state, APL_LINK_CMD_FAN_CYCLE, 0u);
        break;
#if APL_CORE_HAS_TURBO
    case APL_KEY_ACTION_TURBO_TOGGLE:
        apl_hmi_panel_send(state, APL_LINK_CMD_TURBO_TOGGLE, 0u);
        break;
#endif
#if (APL_COMPILE_UV || APL_COMPILE_ANION)
    case APL_KEY_ACTION_PURIFY_TOGGLE:
        apl_hmi_panel_send(state, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
        break;
#endif
#if APL_COMPILE_WIFI
    case APL_KEY_ACTION_WIFI_PAIR:
        apl_hmi_panel_send(state, APL_LINK_CMD_WIFI_PAIR, 0u);
        break;
#endif
#if APL_COMPILE_TIMER
    case APL_KEY_ACTION_TIMER_EDIT:
        apl_hmi_panel_timer_key(state);
        break;
#endif
    case APL_KEY_ACTION_CHILD_LOCK_TOGGLE:
        state->child_lock = (APL_HMI_CHILD_LOCK_ON == state->child_lock)
                            ? APL_HMI_CHILD_LOCK_OFF : APL_HMI_CHILD_LOCK_ON;
        break;
    case APL_KEY_ACTION_LIGHT_TOGGLE:
        if (APL_RUN_ON == panel->snap.run_state)
        {
            panel->light_off = (0u == panel->light_off) ? 1u : 0u;
            panel->wake_ticks = 0u;
        }
        break;
#if APL_COMPILE_HUMIDIFY
    case APL_KEY_ACTION_HUMIDITY_CYCLE:
        apl_hmi_panel_humidity_short(state);
        break;
    case APL_KEY_ACTION_HUMIDITY_EDIT:
        panel->setting = APL_HMI_SETTING_HUMIDITY;
        panel->setting_value = panel->snap.humidity_target;
        panel->setting_ticks = APL_HMI_IDLE_TICKS;
        break;
    case APL_KEY_ACTION_HUMIDIFY_TOGGLE:
        apl_hmi_panel_send(state, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u);
        break;
#endif
#if APL_COMPILE_FILTER
    case APL_KEY_ACTION_FILTER_QUERY:
    case APL_KEY_ACTION_FILTER_RESET_QUERY:
        if ((APL_RUN_ON == panel->snap.run_state) || (APL_RUN_STANDBY == panel->snap.run_state))
        {
            if (APL_KEY_ACTION_FILTER_RESET_QUERY == action)
            {
                apl_hmi_panel_send(state, APL_LINK_CMD_FILTER_RESET, 0u);
            }
            panel->query_ticks = APL_HMI_IDLE_TICKS;
        }
        break;
#endif
#if APL_COMPILE_SLEEP
    case APL_KEY_ACTION_SLEEP_TOGGLE:
        apl_hmi_panel_send(state, APL_LINK_CMD_SLEEP_TOGGLE, 0u);
        break;
#endif
    default:
        break;
    }
}
#endif

static void apl_hmi_panel_key(struct apl_hmi *self,
                              struct apl_hmi_state *state,
                              uint8_t index,
                              uint8_t is_long) APL_CALL
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    APL_UNUSED_PARAM(self);

#if APL_HMI_COMPILE_KEYMAP
    if (0u != apl_hmi_keymap_active(state))
    {
        apl_hmi_panel_action(state, apl_hmi_mapped_action(state, index, is_long));
        return;
    }
#endif

    /* 更改记录D23：睡眠熄屏的电源键直接执行；其他键先唤醒。
     * 非睡眠灯光关闭仍保留既有唤醒策略，灯位冲突另行确认。 */
    if ((0u != apl_hmi_panel_dark(state))
#if APL_COMPILE_SLEEP
        && !((APL_HMI_KEY_POWER == index) && (APL_FAN_GEAR_SLEEP == panel->snap.fan_gear))
#endif
       )
    {
        panel->wake_ticks = apl_hmi_panel_wake_window(panel);
        return;
    }

    if (0u != panel->wake_ticks)
    {
        panel->wake_ticks = apl_hmi_panel_wake_window(panel);
    }

    switch (index)
    {
    case APL_HMI_KEY_POWER:
        if (0u != is_long)
        {
#if APL_COMPILE_DRY
            apl_hmi_panel_send(state, APL_LINK_CMD_DRY_STOP, 0u);
#endif
        }
#if APL_COMPILE_FACTORY
        else if (APL_RUN_FACTORY == panel->snap.run_state)
        {
            apl_hmi_panel_send(state, APL_LINK_CMD_FACTORY_EXIT, 0u);
        }
#endif
        else
        {
            panel->setting = APL_HMI_SETTING_NONE;
            apl_hmi_panel_send(state, APL_LINK_CMD_POWER, 0u);
        }
        break;
    case APL_HMI_KEY_SPEED:
        if (0u == is_long) { apl_hmi_panel_send(state, APL_LINK_CMD_FAN_CYCLE, 0u); }
#if APL_CORE_HAS_TURBO
        else { apl_hmi_panel_send(state, APL_LINK_CMD_TURBO_TOGGLE, 0u); }
#endif
        break;
    case APL_HMI_KEY_PURIFY:
        if (0u != is_long)
        {
#if APL_COMPILE_WIFI
            if (0u != panel->snap.wifi_enabled)
            {
                apl_hmi_panel_send(state, APL_LINK_CMD_WIFI_PAIR, 0u);
            }
#endif
        }
        else
        {
#if (APL_COMPILE_UV || APL_COMPILE_ANION)
            apl_hmi_panel_send(state, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
#endif
        }
        break;
    case APL_HMI_KEY_TIMER:
        if (0u != is_long)
        {
            state->child_lock = (APL_HMI_CHILD_LOCK_ON == state->child_lock)
                                ? APL_HMI_CHILD_LOCK_OFF : APL_HMI_CHILD_LOCK_ON;
        }
        else
        {
#if APL_COMPILE_TIMER
            apl_hmi_panel_timer_key(state);
#endif
        }
        break;
    case APL_HMI_KEY_LIGHT:
        /* 按键说明E11：灯光键无长按功能，睡眠经风速循环进入。 */
        if ((0u == is_long) && (APL_RUN_ON == panel->snap.run_state))
        {
            panel->light_off = (0u == panel->light_off) ? 1u : 0u;
            panel->wake_ticks = 0u;
        }
        else
        {
            /* 待机、风干时灯光键无作用。 */
        }
        break;
#if APL_COMPILE_FILTER
    case APL_HMI_KEY_FILTER:
        if ((0u != panel->snap.purify_enabled) && (0u == panel->snap.humidify_enabled)
            && ((APL_RUN_ON == panel->snap.run_state) || (APL_RUN_STANDBY == panel->snap.run_state)))
        {
            if (0u != is_long)
            {
                apl_hmi_panel_send(state, APL_LINK_CMD_FILTER_RESET, 0u);
            }
            panel->query_ticks = APL_HMI_IDLE_TICKS;
        }
        break;
#endif
#if APL_COMPILE_HUMIDIFY
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
#endif
    default:
        break;
    }
}

#if APL_HMI_COMBO_MAX > 0
#if APL_COMPILE_FILTER
static void apl_hmi_panel_combo_filter(struct apl_hmi *self, void *ctx) APL_CALL
{
    struct apl_hmi_state *state;

    APL_UNUSED_PARAM(ctx);
    state = apl_hmi_state_of(self);
    if (0u != state->panel.snap.humidify_enabled)
    {
        /* 按键说明J9/J12：仅加湿/一体机使用定时+风速重置。 */
        apl_hmi_panel_send(state, APL_LINK_CMD_FILTER_RESET, 0u);
    }
}

#endif

#if APL_COMPILE_FACTORY
static void apl_hmi_panel_combo_factory(struct apl_hmi *self, void *ctx) APL_CALL
{
    APL_UNUSED_PARAM(ctx);
    apl_hmi_panel_send(apl_hmi_state_of(self), APL_LINK_CMD_FACTORY_ENTER, 0u);
}
#endif

#endif

static void apl_hmi_panel_confirm(struct apl_hmi_state *state) APL_CALL
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
#if APL_COMPILE_TIMER
        apl_hmi_panel_send(state, APL_LINK_CMD_TIMER_SET, panel->setting_value);
#endif
    }
    else
    {
#if APL_COMPILE_HUMIDIFY
        apl_hmi_panel_send(state, APL_LINK_CMD_HUMIDITY_TARGET, panel->setting_value);
#endif
    }

    panel->setting = APL_HMI_SETTING_NONE;
}

static void apl_hmi_panel_clear(struct apl_hmi_state *state) APL_CALL
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

#if APL_HMI_COMPILE_AMBIENT
    state->ambient_brightness = 0u;
#endif
    state->digit_brightness = 0u;
    state->digit_unit = APL_HMI_UNIT_NONE;
    apl_hmi_digits_text(state, ' ', ' ', ' ');
}

static uint8_t apl_hmi_on(uint8_t cond) APL_CALL
{
    return (0u != cond) ? APL_HMI_LAMP_FULL : 0u;
}

#if APL_COMPILE_FILTER
static uint8_t apl_hmi_panel_filter_expired(const struct apl_link_snapshot *snap) APL_CALL
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

static void apl_hmi_panel_draw_filter(struct apl_hmi_state *state) APL_CALL
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

#endif

#if APL_COMPILE_HUMIDIFY
static void apl_hmi_panel_draw_water(struct apl_hmi_state *state) APL_CALL
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

#endif

#if APL_HMI_COMPILE_AMBIENT
static void apl_hmi_panel_draw_ambient(struct apl_hmi_state *state) APL_CALL
{
    const struct apl_link_snapshot *snap;

    snap = &state->panel.snap;
    if (0u == snap->purify_enabled)
    {
        return;
    }

    state->ambient_brightness = APL_HMI_LAMP_FULL;
    if (0u == state->panel.snap.aq_ready)
    {
        state->ambient_brightness = 0u;
        state->ambient_red = 0u;
        state->ambient_green = 0u;
        state->ambient_blue = 0u;
        return;
    }

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

#endif

#if APL_HMI_HAS_ROTATE
static uint32_t apl_hmi_panel_rotate_ticks(const struct apl_hmi_panel *panel) APL_CALL
{
    return (0u != panel->snap.display_rotate_s)
        ? ((uint32_t)panel->snap.display_rotate_s * (1000UL / APL_HMI_TICK_MS))
        : APL_HMI_ROTATE_TICKS;
}

#endif

static void apl_hmi_panel_draw_digits(struct apl_hmi_state *state) APL_CALL
{
#if (APL_COMPILE_TIMER || APL_COMPILE_HUMIDIFY || APL_COMPILE_FILTER || APL_HMI_HAS_WARMUP)
    struct apl_hmi_panel *panel;
#endif
#if (APL_COMPILE_FILTER || APL_HMI_HAS_WARMUP)
    const struct apl_link_snapshot *snap;
#endif
#if APL_COMPILE_SENSOR_HUMIDITY
    uint8_t show_humidity;
#endif

#if (APL_COMPILE_TIMER || APL_COMPILE_HUMIDIFY || APL_COMPILE_FILTER || APL_HMI_HAS_WARMUP)
    panel = &state->panel;
#endif
#if (APL_COMPILE_FILTER || APL_HMI_HAS_WARMUP)
    snap = &panel->snap;
#endif
    state->digit_brightness = APL_HMI_LAMP_FULL;

#if APL_COMPILE_TIMER
    if (APL_HMI_SETTING_TIMER == panel->setting)
    {
        apl_hmi_digits_number(state, panel->setting_value);
        /* H 闪烁。 */
        state->digit_unit = (0u != apl_hmi_blink(panel->clock, APL_HMI_BLINK_SLOW_MS,
                                                 APL_HMI_BLINK_SLOW_MS))
                            ? APL_HMI_UNIT_HOUR : APL_HMI_UNIT_NONE;
        return;
    }

#endif
#if APL_COMPILE_HUMIDIFY
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

#endif
#if APL_COMPILE_FILTER
    if (0u != panel->query_ticks)
    {
        apl_hmi_digits_number(state, (0u != snap->purify_enabled) ? snap->filter_purify_left
                                                                  : snap->filter_humidify_left);
        state->digit_unit = APL_HMI_UNIT_NONE;
        return;
    }

#endif
#if APL_COMPILE_SENSOR_PM25
    if ((0u != snap->purify_enabled) && (0u != (snap->sensor_faults & APL_SENSOR_PM25_MASK)))
    {
        apl_hmi_digits_text(state, ' ', 'E', '9');
        state->digit_unit = APL_HMI_UNIT_NONE;
        return;
    }
#endif
#if APL_HMI_HAS_WARMUP
    if (0u != snap->sensors_warming)
    {
        char bar;
        uint16_t phase;
        phase = (uint16_t)(panel->warmup_animation_ticks / APL_HMI_WARMUP_FRAME_TICKS);
        bar = (0u == phase) ? '^' : ((1u == phase) ? '-' : '_');
        apl_hmi_digits_text(state, bar, bar, bar);
        state->digit_unit = APL_HMI_UNIT_NONE;
        return;
    }
#endif
#if APL_COMPILE_SENSOR_HUMIDITY
#if APL_COMPILE_SENSOR_PM25
    show_humidity = (0u == snap->purify_enabled) ? 1u : 0u;
#else
    show_humidity = 1u;
#endif
#if APL_HMI_HAS_ROTATE
    if (0u != apl_hmi_panel_combo_model(snap))
    { show_humidity = (panel->rotate_ticks >= apl_hmi_panel_rotate_ticks(panel)) ? 1u : 0u; }
#endif
    if (0u != show_humidity)
    {
        if (0u != (snap->sensors_ready & APL_SENSOR_HUMIDITY_MASK))
        { apl_hmi_digits_number(state, snap->humidity); }
        else { apl_hmi_digits_text(state, ' ', '-', '-'); }
        state->digit_unit = APL_HMI_UNIT_PERCENT;
        return;
    }
#endif
#if APL_COMPILE_SENSOR_PM25
    if (0u != (snap->sensors_ready & APL_SENSOR_PM25_MASK))
    { apl_hmi_digits_number(state, snap->pm25); }
    else { apl_hmi_digits_text(state, ' ', '-', '-'); }
    state->digit_unit = APL_HMI_UNIT_PM25;
#else
    apl_hmi_digits_text(state, ' ', '-', '-');
    state->digit_unit = APL_HMI_UNIT_NONE;
#endif
}

static void apl_hmi_panel_draw_on(struct apl_hmi_state *state) APL_CALL
{
    struct apl_hmi_panel *panel;
    const struct apl_link_snapshot *snap;
#if APL_HMI_COMPILE_KEYMAP
    uint8_t index;
#endif

    panel = &state->panel;
    snap = &panel->snap;

    /* 用户确认：手动关灯时所有灯位熄灭；首次触摸的临时唤醒仍可显示。 */
    if ((0u != panel->light_off) && (0u != apl_hmi_panel_dark(state)))
    {
        return;
    }

    /* 显示开启时可用按键常亮；末尾统一屏蔽未配置功能。 */
#if APL_HMI_COMPILE_KEYMAP
    if (0u != apl_hmi_keymap_active(state))
    {
        for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
        {
            state->key[index].brightness = apl_hmi_on(
                (0u != apl_hmi_key_is_compiled((enum apl_hmi_key_id)index))
                && (0u != apl_hmi_mapped_key_available(state, index)));
        }
    }
    else
#endif
    {
        state->key[APL_HMI_KEY_FILTER].brightness =
            apl_hmi_on((0u != snap->purify_enabled) && (0u == snap->humidify_enabled));
        state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_FULL;
        state->key[APL_HMI_KEY_SPEED].brightness = APL_HMI_LAMP_FULL;
        state->key[APL_HMI_KEY_PURIFY].brightness = APL_HMI_LAMP_FULL;
        state->key[APL_HMI_KEY_HUMIDITY].brightness = APL_HMI_LAMP_FULL;
        state->key[APL_HMI_KEY_TIMER].brightness = APL_HMI_LAMP_FULL;
        state->key[APL_HMI_KEY_LIGHT].brightness =
            apl_hmi_on((0u == panel->light_off) || (0u != panel->wake_ticks));
    }

    if (0u != apl_hmi_panel_dark(state))
    {
        /* All three Sleep rows retain the Sleep indicator; other lamp policy is unchanged. */
#if APL_COMPILE_SLEEP
        if (0u != snap->sleep)
        {
            state->icon[APL_HMI_ICON_GEAR_SLEEP] = APL_HMI_LAMP_FULL;
        }
#endif
        return;
    }

    switch (snap->fan_gear)
    {
#if APL_COMPILE_SLEEP
    case APL_FAN_GEAR_SLEEP:
        state->icon[APL_HMI_ICON_GEAR_SLEEP] = APL_HMI_LAMP_FULL;
        break;
#endif
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
#if APL_CORE_HAS_TURBO
    case APL_FAN_GEAR_TURBO:
        /* 三机型极速条目：三档灯同时亮。 */
        state->icon[APL_HMI_ICON_GEAR_1] = APL_HMI_LAMP_FULL;
        state->icon[APL_HMI_ICON_GEAR_2] = APL_HMI_LAMP_FULL;
        state->icon[APL_HMI_ICON_GEAR_3] = APL_HMI_LAMP_FULL;
        break;
#endif
    default:
        break;
    }

#if APL_COMPILE_WIFI
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

#endif

    state->icon[APL_HMI_ICON_LOCK] = apl_hmi_on(APL_HMI_CHILD_LOCK_ON == state->child_lock);
#if APL_COMPILE_ANION
    state->icon[APL_HMI_ICON_ANION] = apl_hmi_on(APL_SWITCH_ON == snap->anion);
#endif
#if APL_COMPILE_UV
    state->icon[APL_HMI_ICON_UV] = apl_hmi_on(APL_SWITCH_ON == snap->uv);
#endif
#if APL_COMPILE_HUMIDIFY
    state->icon[APL_HMI_ICON_HUMIDITY] =
        apl_hmi_on((0u != snap->humidify_on)
                   && (0u != (snap->sensors_ready & APL_SENSOR_HUMIDITY_MASK))
                   && (APL_HUMIDITY_TARGET_OFF != snap->humidity_target));
#endif
#if APL_COMPILE_FILTER
    apl_hmi_panel_draw_filter(state);
#endif
#if APL_COMPILE_HUMIDIFY
    apl_hmi_panel_draw_water(state);
#endif
#if APL_HMI_COMPILE_AMBIENT
    apl_hmi_panel_draw_ambient(state);
#endif
    apl_hmi_panel_draw_digits(state);
}

static void apl_hmi_panel_draw(struct apl_hmi_state *state) APL_CALL
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    apl_hmi_panel_clear(state);
    switch (panel->snap.run_state)
    {
    case APL_RUN_ON:
        apl_hmi_panel_draw_on(state);
        break;
#if APL_COMPILE_DRY
    case APL_RUN_DRY:
        /* 风干只亮电源键灯和风干图标。 */
#if APL_HMI_COMPILE_KEYMAP
        if (0u != apl_hmi_keymap_active(state))
        {
            apl_hmi_mapped_power_lamps(state, APL_HMI_LAMP_FULL);
        }
        else
#endif
        {
            state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_FULL;
        }
        state->icon[APL_HMI_ICON_DRY] = APL_HMI_LAMP_FULL;
        break;
#endif
#if APL_COMPILE_FACTORY
    case APL_RUN_FACTORY:
        /* 产测：全部灯光点亮供目检。逐个点亮的顺序尚未规定。 */
        apl_hmi_lamps_fill(state, APL_HMI_LAMP_FULL);
        apl_hmi_digits_text(state, '8', '8', '8');
        break;
#endif
    default:
#if APL_HMI_COMPILE_KEYMAP
        if (0u != apl_hmi_keymap_active(state))
        {
            apl_hmi_mapped_power_lamps(state, APL_HMI_LAMP_HALF);
        }
        else
#endif
        {
            state->key[APL_HMI_KEY_POWER].brightness = APL_HMI_LAMP_HALF;
        }
#if APL_COMPILE_FACTORY
        if (0u != panel->snap.factory_done)
        {
            apl_hmi_digits_text(state, ' ', 'C', 'L');
            state->digit_brightness = APL_HMI_LAMP_FULL;
        }
#if APL_COMPILE_FILTER
        else
#endif
#endif
#if APL_COMPILE_FILTER
        if ((0u != panel->snap.purify_enabled) && (0u == panel->snap.humidify_enabled))
        {
            if (0u != panel->query_ticks) { apl_hmi_panel_draw_digits(state); }
            if ((0u != panel->query_ticks) || (0u != panel->red_blink_ticks))
            { apl_hmi_panel_draw_filter(state); }
        }
#endif
        break;
    }
}

/** 快照变化引起的一次性效果：提示音、红灯闪烁、水满闪烁、睡眠熄屏计时。 */
static void apl_hmi_panel_events(struct apl_hmi_state *state) APL_CALL
{
    struct apl_hmi_panel *panel;
    const struct apl_link_snapshot *snap;
    panel = &state->panel;
    snap = &panel->snap;
#if (APL_HMI_COMPILE_BUZZER || APL_COMPILE_HUMIDIFY)
    if (snap->beep_seq != panel->last_beep_seq)
    {
        panel->last_beep_seq = snap->beep_seq;
#if APL_HMI_COMPILE_BUZZER
        apl_hmi_beep_play(state, snap->beep);
#endif
#if APL_COMPILE_HUMIDIFY
        if (APL_BEEP_WATER_FULL == snap->beep)
        {
            panel->full_blink_ticks = (uint16_t)(APL_HMI_FULL_BLINK_TIMES * 2u
                                                 * APL_HMI_BLINK_FAST_MS / APL_HMI_TICK_MS);
        }
#endif
    }
#endif
#if APL_COMPILE_FILTER
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
#endif
    if ((APL_RUN_ON == snap->run_state) && (APL_RUN_ON != panel->prev_run))
    {
        panel->light_off = 0u;
#if APL_HMI_HAS_ROTATE
        panel->rotate_ticks = 0u;
#endif
#if APL_HMI_HAS_WARMUP
        panel->warmup_animation_ticks = 0u;
#endif
#if APL_COMPILE_FILTER
        if (0u != apl_hmi_panel_filter_expired(snap))
        {
            panel->red_blink_period = APL_HMI_BLINK_SLOW_MS;
            panel->red_blink_ticks = (uint16_t)(APL_HMI_BLINK_TIMES * 2u
                                                * APL_HMI_BLINK_SLOW_MS / APL_HMI_TICK_MS);
        }
#endif
    }
    if (APL_RUN_ON != snap->run_state)
    {
        panel->setting = APL_HMI_SETTING_NONE;
#if APL_COMPILE_FILTER
        if ((APL_RUN_STANDBY != snap->run_state) || (APL_RUN_ON == panel->prev_run))
        { panel->query_ticks = 0u; }
#endif
    }
#if APL_COMPILE_SLEEP
    if ((0u != snap->sleep) && (0u == panel->prev_sleep))
    { panel->wake_ticks = apl_hmi_panel_wake_window(panel); }
    panel->prev_sleep = snap->sleep;
#endif
#if APL_COMPILE_HUMIDIFY
    if ((APL_WATER_ALERT_BLINK == snap->water_alert) && (APL_WATER_ALERT_BLINK != panel->prev_alert))
    { panel->alert_clock = panel->clock; }
    panel->prev_alert = snap->water_alert;
#endif
    panel->prev_run = snap->run_state;
}

static void apl_hmi_panel_tick(struct apl_hmi_state *state) APL_CALL
{
    struct apl_hmi_panel *panel;

    panel = &state->panel;
    if (0u == panel->snap_valid)
    {
        return;
    }

#if APL_HMI_HAS_CLOCK
    panel->clock++;
#endif
    apl_hmi_panel_events(state);
    apl_hmi_panel_confirm(state);

    if ((0u != panel->wake_ticks) && (0u != panel->keys_down))
    {
        panel->wake_ticks = apl_hmi_panel_wake_window(panel);
    }
    else if (0u != panel->wake_ticks)
    {
        panel->wake_ticks--;
    }
    else
    {
        /* 已熄屏。 */
    }

#if APL_COMPILE_FILTER
    if (0u != panel->query_ticks) { panel->query_ticks--; }
    if (0u != panel->red_blink_ticks) { panel->red_blink_ticks--; }
#endif
#if APL_COMPILE_HUMIDIFY
    if (0u != panel->full_blink_ticks) { panel->full_blink_ticks--; }
#endif

#if APL_HMI_HAS_ROTATE
    panel->rotate_ticks++;
    if (panel->rotate_ticks >= (2UL * apl_hmi_panel_rotate_ticks(panel)))
    {
        panel->rotate_ticks = 0u;
    }

#endif

    apl_hmi_panel_draw(state);
#if APL_HMI_HAS_WARMUP
    if ((APL_RUN_ON == panel->snap.run_state) && (0u != panel->snap.sensors_warming))
    {
        panel->warmup_animation_ticks++;
        if (panel->warmup_animation_ticks >= (3u * APL_HMI_WARMUP_FRAME_TICKS))
        {
            panel->warmup_animation_ticks = 0u;
        }
    }
    else
    {
        panel->warmup_animation_ticks = 0u;
    }
#endif

}

void apl_hmi_link_bind(struct apl_hmi *self,
                       enum apl_link_result (*send)(const struct apl_link_command *command,
                                                    void *ctx) APL_CALL,
                       void *ctx) APL_CALL
{
    struct apl_hmi_state *state;
#if APL_HMI_COMBO_MAX > 0
#if APL_COMPILE_FILTER
    static const enum apl_hmi_key_id filter_keys[2] = { APL_HMI_KEY_TIMER, APL_HMI_KEY_SPEED };
#endif
#if APL_COMPILE_FACTORY
    static const enum apl_hmi_key_id factory_keys[2] = { APL_HMI_KEY_POWER, APL_HMI_KEY_TIMER };
#endif
#endif

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

#if APL_HMI_COMBO_MAX > 0
#if APL_COMPILE_FILTER
    (void)apl_hmi_set_combo(self, filter_keys, 2u, (uint16_t)APL_HMI_LONG_PRESS_MS,
                            apl_hmi_panel_combo_filter, NULL);
#endif
#if APL_COMPILE_FACTORY
    (void)apl_hmi_set_combo(self, factory_keys, 2u, (uint16_t)APL_HMI_LONG_PRESS_MS,
                            apl_hmi_panel_combo_factory, NULL);
#endif
#endif
}

struct apl_link_snapshot *apl_hmi_snapshot_target(struct apl_hmi *self) APL_CALL
{
    return (NULL != self) ? &apl_hmi_state_of(self)->panel.snap : NULL;
}

void apl_hmi_snapshot_commit(struct apl_hmi *self) APL_CALL
{
    struct apl_hmi_state *state;
    const struct apl_link_snapshot *snapshot;

    if (NULL == self)
    {
        return;
    }

    state = apl_hmi_state_of(self);
    snapshot = &state->panel.snap;
    if (0u == state->panel.snap_valid)
    {
        /* 第一份快照只作起点，不补播此前的提示音。 */
#if (APL_HMI_COMPILE_BUZZER || APL_COMPILE_HUMIDIFY)
        state->panel.last_beep_seq = snapshot->beep_seq;
#endif
#if APL_COMPILE_FILTER
        state->panel.last_reset_seq = snapshot->filter_reset_seq;
#endif
        state->panel.prev_run = APL_RUN_STANDBY;
    }

    state->panel.snap_valid = 1u;
    if (NULL != state->panel.send)
    {
        state->purify_enabled = ((0u != snapshot->purify_enabled) || (0u != snapshot->uv_enabled)
                                 || (0u != snapshot->anion_enabled)) ? 1u : 0u;
        state->humidify_enabled = (0u != snapshot->humidify_enabled) ? 1u : 0u;
    }
}

void apl_hmi_set_snapshot(struct apl_hmi *self, const struct apl_link_snapshot *snapshot) APL_CALL
{
    if ((NULL == self) || (NULL == snapshot))
    {
        return;
    }
    apl_hmi_state_of(self)->panel.snap = *snapshot;
    apl_hmi_snapshot_commit(self);
}

#if APL_COMPILE_FILTER
void apl_hmi_filter_query(struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_state_of(self)->panel.query_ticks = APL_HMI_IDLE_TICKS;
}

#endif

uint8_t apl_hmi_get_light(const struct apl_hmi *self) APL_CALL
{
    if (NULL == self)
    {
        return 0u;
    }

    return (0u == apl_hmi_state_of_const(self)->panel.light_off) ? 1u : 0u;
}
#endif /* APL_COMPILE_HMI */
