#include "apl_hmi.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
struct fixture
{
    struct apl_hmi hmi;
    struct apl_keymap map;
    struct apl_hmi_key_state keys;
    struct apl_link_snapshot snap;
    struct apl_link_command last_command;
    unsigned commands;
    unsigned events;
    enum apl_hmi_key_id last_key;
    uint8_t last_long;
#if APL_HMI_COMPILE_KEY_CALLBACKS
    unsigned callbacks;
#endif
};

static void read_keys(struct apl_hmi_key_state *keys, void *ctx)
{
    *keys = ((struct fixture *)ctx)->keys;
}

static void key_event(enum apl_hmi_key_id key, uint8_t is_long, void *ctx)
{
    struct fixture *f = ctx;
    f->events++;
    f->last_key = key;
    f->last_long = is_long;
}

static enum apl_link_result send_command(const struct apl_link_command *command, void *ctx)
{
    struct fixture *f = ctx;
    f->last_command = *command;
    f->commands++;
    return APL_LINK_OK;
}

static const struct apl_hmi_ops ops = {.get_key_state = read_keys, .key_event = key_event};

static void ticks(struct fixture *f, unsigned count)
{
    while (count-- != 0u) { apl_hmi_tick(&f->hmi); }
}

static void level(struct fixture *f, enum apl_hmi_key_id key, enum apl_hmi_key_level value)
{
    switch (key)
    {
    case APL_HMI_KEY_PURIFY: f->keys.purify = value; break;
    case APL_HMI_KEY_HUMIDITY: f->keys.humidity = value; break;
    case APL_HMI_KEY_TIMER: f->keys.timer = value; break;
    case APL_HMI_KEY_LIGHT: f->keys.light = value; break;
    case APL_HMI_KEY_SPEED: f->keys.speed = value; break;
    case APL_HMI_KEY_POWER: f->keys.power = value; break;
    case APL_HMI_KEY_FILTER: f->keys.filter = value; break;
    default: assert(0); break;
    }
}

static void press(struct fixture *f, enum apl_hmi_key_id key, unsigned duration)
{
    level(f, key, APL_HMI_KEY_PRESSED);
    ticks(f, duration);
    level(f, key, APL_HMI_KEY_RELEASED);
    ticks(f, 1u);
}

static void prepare(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->map.enabled = 1u;
    f->snap.run_state = APL_RUN_STANDBY;
    f->snap.fan_gear = APL_FAN_GEAR_1;
    f->snap.purify_enabled = 1u;
    f->snap.humidify_enabled = 1u;
    f->snap.uv_enabled = 1u;
    f->snap.anion_enabled = 1u;
    f->snap.wifi_enabled = 1u;
    f->snap.humidity_target = 50u;
    f->snap.humidity = 45u;
    f->snap.pm25 = 12u;
    f->snap.timer_step_h = 1u;
    f->snap.sensors_configured = APL_SENSOR_HUMIDITY_MASK | APL_SENSOR_PM25_MASK;
    f->snap.sensors_ready = f->snap.sensors_configured;
}

static void start(struct fixture *f)
{
    apl_hmi_init(&f->hmi, &ops, f);
    apl_hmi_set_keymap(&f->hmi, &f->map);
    apl_hmi_link_bind(&f->hmi, send_command, f);
    apl_hmi_set_snapshot(&f->hmi, &f->snap);
    ticks(f, APL_HMI_POWERUP_LAMP_TICKS + 1u);
}

static void test_disabled_map_is_standard(void)
{
    struct fixture f;
    prepare(&f);
    f.map.enabled = 0u;
    f.map.short_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_POWER;
    start(&f);
    assert(APL_HMI_LAMP_HALF == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_POWER));
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_LIGHT));
    press(&f, APL_HMI_KEY_LIGHT, 5u);
    assert(0u == f.commands);
    press(&f, APL_HMI_KEY_POWER, 5u);
    assert(1u == f.commands && APL_LINK_CMD_POWER == f.last_command.type);
    apl_hmi_set_keymap(&f.hmi, NULL);
    press(&f, APL_HMI_KEY_POWER, 5u);
    assert(2u == f.commands && APL_LINK_CMD_POWER == f.last_command.type);
}

static void test_physical_events_and_power_lamps(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_POWER;
    f.map.short_action[APL_HMI_KEY_PURIFY] = APL_KEY_ACTION_FAN_CYCLE;
    f.snap.purify_enabled = 0u;
    f.snap.uv_enabled = 0u;
    f.snap.anion_enabled = 0u;
    start(&f);
    assert(APL_HMI_LAMP_HALF == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_LIGHT));
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_POWER));
    press(&f, APL_HMI_KEY_LIGHT, 5u);
    assert(APL_LINK_CMD_POWER == f.last_command.type);
    assert(1u == f.events && APL_HMI_KEY_LIGHT == f.last_key && 0u == f.last_long);
    f.snap.run_state = APL_RUN_ON;
    apl_hmi_set_snapshot(&f.hmi, &f.snap);
    ticks(&f, 1u);
    assert(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_PURIFY));
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_SPEED));
    press(&f, APL_HMI_KEY_PURIFY, 5u);
    assert(2u == f.commands && APL_LINK_CMD_FAN_CYCLE == f.last_command.type);
    assert(APL_HMI_KEY_PURIFY == f.last_key);
#if APL_COMPILE_DRY
    f.snap.run_state = APL_RUN_DRY;
    apl_hmi_set_snapshot(&f.hmi, &f.snap);
    ticks(&f, 1u);
    assert(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_LIGHT));
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_POWER));
#endif
}

static void test_per_gesture_availability(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_HUMIDITY] = APL_KEY_ACTION_HUMIDITY_CYCLE;
    f.map.long_action[APL_HMI_KEY_HUMIDITY] = APL_KEY_ACTION_WIFI_PAIR;
    f.snap.humidify_enabled = 0u;
    f.snap.run_state = APL_RUN_ON;
    start(&f);
#if APL_COMPILE_WIFI
    assert(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_HUMIDITY));
#else
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_HUMIDITY));
#endif
    press(&f, APL_HMI_KEY_HUMIDITY, 5u);
    assert(0u == f.commands && 1u == f.events);
    press(&f, APL_HMI_KEY_HUMIDITY, APL_HMI_LONG_PRESS_TICKS + 1u);
#if APL_COMPILE_WIFI
    assert(1u == f.commands && APL_LINK_CMD_WIFI_PAIR == f.last_command.type);
#else
    assert(0u == f.commands);
#endif
    assert(2u == f.events && APL_HMI_KEY_HUMIDITY == f.last_key && 1u == f.last_long);
    f.snap.wifi_enabled = 0u;
    apl_hmi_set_snapshot(&f.hmi, &f.snap);
    ticks(&f, 1u);
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_HUMIDITY));
    f.commands = 0u;
    press(&f, APL_HMI_KEY_HUMIDITY, APL_HMI_LONG_PRESS_TICKS + 1u);
    assert(0u == f.commands);
}

static void test_child_lock_follows_action(void)
{
    struct fixture f;
    prepare(&f);
    f.map.long_action[APL_HMI_KEY_SPEED] = APL_KEY_ACTION_CHILD_LOCK_TOGGLE;
    f.map.long_action[APL_HMI_KEY_TIMER] = APL_KEY_ACTION_POWER;
    start(&f);
    press(&f, APL_HMI_KEY_SPEED, APL_HMI_LONG_PRESS_TICKS + 1u);
    assert(APL_HMI_CHILD_LOCK_ON == apl_hmi_get_child_lock(&f.hmi));
    press(&f, APL_HMI_KEY_TIMER, APL_HMI_LONG_PRESS_TICKS + 1u);
    assert(0u == f.commands && APL_HMI_CHILD_LOCK_ON == apl_hmi_get_child_lock(&f.hmi));
    press(&f, APL_HMI_KEY_SPEED, APL_HMI_LONG_PRESS_TICKS + 1u);
    assert(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&f.hmi));
}

static void test_long_power_and_remapped_light(void)
{
    struct fixture f;
    prepare(&f);
    f.map.long_action[APL_HMI_KEY_FILTER] = APL_KEY_ACTION_POWER;
    f.map.short_action[APL_HMI_KEY_TIMER] = APL_KEY_ACTION_LIGHT_TOGGLE;
    f.map.short_action[APL_HMI_KEY_PURIFY] = APL_KEY_ACTION_FAN_CYCLE;
    start(&f);
    assert(APL_HMI_LAMP_HALF == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_FILTER));
    press(&f, APL_HMI_KEY_FILTER, 5u);
    assert(0u == f.commands);
    press(&f, APL_HMI_KEY_FILTER, APL_HMI_LONG_PRESS_TICKS + 1u);
    assert(1u == f.commands && APL_LINK_CMD_POWER == f.last_command.type);
    f.commands = 0u;
    f.snap.run_state = APL_RUN_ON;
    apl_hmi_set_snapshot(&f.hmi, &f.snap);
    ticks(&f, 1u);
    press(&f, APL_HMI_KEY_TIMER, 5u);
    assert(0u == apl_hmi_get_light(&f.hmi));
    assert(0u == apl_hmi_get_digits_brightness(&f.hmi));
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_TIMER));
    press(&f, APL_HMI_KEY_LIGHT, 5u); /* NONE cannot wake the dark display. */
    assert(0u == apl_hmi_get_digits_brightness(&f.hmi));
    press(&f, APL_HMI_KEY_PURIFY, 5u);
    assert(0u == f.commands && 0u != apl_hmi_get_digits_brightness(&f.hmi));
    press(&f, APL_HMI_KEY_PURIFY, 5u);
    assert(1u == f.commands && APL_LINK_CMD_FAN_CYCLE == f.last_command.type);
}

#if APL_COMPILE_HUMIDIFY
static void test_humidity_capabilities(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_SPEED] = APL_KEY_ACTION_HUMIDITY_EDIT;
    f.map.short_action[APL_HMI_KEY_FILTER] = APL_KEY_ACTION_HUMIDITY_CYCLE;
    f.snap.sensors_configured = APL_SENSOR_PM25_MASK;
    f.snap.sensors_ready = f.snap.sensors_configured;
    f.snap.run_state = APL_RUN_ON;
    start(&f);
    assert(0u == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_SPEED));
    assert(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&f.hmi, APL_HMI_KEY_FILTER));
    press(&f, APL_HMI_KEY_SPEED, 5u);
    ticks(&f, APL_HMI_IDLE_TICKS + 1u);
    assert(0u == f.commands);
    press(&f, APL_HMI_KEY_FILTER, 5u);
    assert(1u == f.commands && APL_LINK_CMD_HUMIDIFY_TOGGLE == f.last_command.type);
}
#endif

#if APL_COMPILE_SLEEP
static void test_dark_wake_and_none(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_POWER;
    f.map.short_action[APL_HMI_KEY_POWER] = APL_KEY_ACTION_FAN_CYCLE;
    f.snap.run_state = APL_RUN_ON;
    f.snap.fan_gear = APL_FAN_GEAR_SLEEP;
    f.snap.sleep = 1u;
    start(&f);
    ticks(&f, APL_HMI_SLEEP_IDLE_TICKS + 1u);
    assert(0u == apl_hmi_get_digits_brightness(&f.hmi));
    press(&f, APL_HMI_KEY_FILTER, 5u);
    assert(0u == apl_hmi_get_digits_brightness(&f.hmi));
    assert(1u == f.events && 0u == f.commands);
    press(&f, APL_HMI_KEY_POWER, 5u);
    assert(0u == f.commands && 0u != apl_hmi_get_digits_brightness(&f.hmi));
    ticks(&f, APL_HMI_SLEEP_IDLE_TICKS + 1u);
    assert(0u == apl_hmi_get_digits_brightness(&f.hmi));
    press(&f, APL_HMI_KEY_LIGHT, 5u);
    assert(1u == f.commands && APL_LINK_CMD_POWER == f.last_command.type);
}
#endif

#if APL_COMPILE_TIMER
static void test_mapped_timer_hold(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_TIMER_EDIT;
    f.map.long_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_CHILD_LOCK_TOGGLE;
    f.snap.run_state = APL_RUN_ON;
    start(&f);
    press(&f, APL_HMI_KEY_LIGHT, 5u);
    press(&f, APL_HMI_KEY_LIGHT, APL_HMI_LONG_PRESS_TICKS + 10u);
    assert(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&f.hmi));
    assert(0u == f.commands);
    ticks(&f, APL_HMI_IDLE_TICKS + 1u);
    assert(1u == f.commands && APL_LINK_CMD_TIMER_SET == f.last_command.type);
    assert(5u == f.last_command.arg);
    /* The released slot no longer owns repeat state: a new short edit is one step. */
    press(&f, APL_HMI_KEY_LIGHT, 5u);
    ticks(&f, APL_HMI_IDLE_TICKS + 1u);
    assert(2u == f.commands && 1u == f.last_command.arg);
}
#endif

#if APL_HMI_COMBO_MAX > 0 && APL_COMPILE_FILTER
static void test_combinations_remain_physical(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_TIMER] = APL_KEY_ACTION_POWER;
    f.map.short_action[APL_HMI_KEY_SPEED] = APL_KEY_ACTION_LIGHT_TOGGLE;
    start(&f);
    level(&f, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    level(&f, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    ticks(&f, APL_HMI_LONG_PRESS_TICKS + 1u);
    assert(1u == f.commands && APL_LINK_CMD_FILTER_RESET == f.last_command.type);
    level(&f, APL_HMI_KEY_TIMER, APL_HMI_KEY_RELEASED);
    level(&f, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    ticks(&f, 1u);
    assert(1u == f.commands);
}
#endif

#if APL_HMI_COMPILE_KEY_CALLBACKS
static void physical_callback(struct apl_hmi *self, void *ctx)
{
    struct fixture *f = ctx;
    (void)self;
    f->callbacks++;
}

static void test_standalone_callbacks_are_physical(void)
{
    struct fixture f;
    prepare(&f);
    f.map.short_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_POWER;
    apl_hmi_init(&f.hmi, &ops, &f);
    apl_hmi_set_keymap(&f.hmi, &f.map);
    apl_hmi_set_key_callback(&f.hmi, APL_HMI_KEY_LIGHT, physical_callback, NULL, &f);
    ticks(&f, APL_HMI_POWERUP_LAMP_TICKS + 1u);
    press(&f, APL_HMI_KEY_LIGHT, 5u);
    assert(1u == f.callbacks && 0u == f.commands);
    assert(APL_HMI_KEY_LIGHT == f.last_key);
}
#endif
#endif

int main(void)
{
#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
    test_disabled_map_is_standard();
    test_physical_events_and_power_lamps();
    test_per_gesture_availability();
    test_child_lock_follows_action();
    test_long_power_and_remapped_light();
#if APL_COMPILE_HUMIDIFY
    test_humidity_capabilities();
#endif
#if APL_COMPILE_SLEEP
    test_dark_wake_and_none();
#endif
#if APL_COMPILE_TIMER
    test_mapped_timer_hold();
#endif
#if APL_HMI_COMBO_MAX > 0 && APL_COMPILE_FILTER
    test_combinations_remain_physical();
#endif
#if APL_HMI_COMPILE_KEY_CALLBACKS
    test_standalone_callbacks_are_physical();
#endif
#endif
    puts("PASS: internal key mapping, physical inputs/events, capabilities, lamps, lock, wake, repeat and combinations");
    return 0;
}
