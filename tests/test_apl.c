/**
 * @file    test_apl.c
 * @brief   宿主侧检查已实现的主控和人机行为。
 *
 * cl /nologo /std:c11 /utf-8 /W4 /WX /I include /I config
 *    tests\test_apl.c src\apl.c src\apl_core.c src\apl_hmi.c
 */

#include "apl.h"

#include <stdio.h>
#include <string.h>

static int g_failed;

static void expect_true(int cond, const char *file, int line, const char *text)
{
    if (0 == cond)
    {
        printf("FAIL %s:%d %s\n", file, line, text);
        g_failed++;
    }
}

#define EXPECT(cond) expect_true((cond) ? 1 : 0, __FILE__, __LINE__, #cond)

struct core_host
{
    enum apl_water_level water;
    int force_bad_water;
    int trigger;
    int recover;
    int power_off;
    int timer_fire;
    int uv_set_calls;
    int anion_set_calls;
    int pm_reads;
    uint32_t timer_at_fire;
    uint32_t timer_at_off;
    struct apl_core *core;
};

static enum apl_water_level get_water_level(void *ctx)
{
    struct core_host *host;

    host = ctx;
    if (0 != host->force_bad_water)
    {
        return (enum apl_water_level)9;
    }

    return host->water;
}

static uint16_t get_pm25(void *ctx)
{
    struct core_host *host;

    host = ctx;
    host->pm_reads++;
    return 28u;
}

static uint8_t get_humidity(void *ctx)
{
    (void)ctx;
    return 55u;
}

static int16_t get_temperature(void *ctx)
{
    (void)ctx;
    return 25;
}

static void water_on_trigger(struct apl_core *self, void *ctx)
{
    struct core_host *host;

    (void)self;
    host = ctx;
    host->trigger++;
}

static void water_on_recover(struct apl_core *self, void *ctx)
{
    struct core_host *host;

    (void)self;
    host = ctx;
    host->recover++;
}

static void set_uv(enum apl_switch state, void *ctx)
{
    struct core_host *host;

    (void)state;
    host = ctx;
    host->uv_set_calls++;
}

static void set_anion(enum apl_switch state, void *ctx)
{
    struct core_host *host;

    (void)state;
    host = ctx;
    host->anion_set_calls++;
}

static void on_power_off(struct apl_core *self, void *ctx)
{
    struct core_host *host;

    host = ctx;
    host->power_off++;
    host->timer_at_off = apl_core_get_timer(self);
}

static void on_timer_off(struct apl_core *self, void *ctx)
{
    struct core_host *host;

    host = ctx;
    host->timer_fire++;
    host->timer_at_fire = apl_core_get_timer(self);
    apl_core_power_off(self);
}

static void on_timer_on(struct apl_core *self, void *ctx)
{
    struct core_host *host;

    host = ctx;
    host->timer_fire++;
    host->timer_at_fire = apl_core_get_timer(self);
    apl_core_power_on(self);
}

static void core_ticks(struct apl_core *self, uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; index++)
    {
        apl_core_tick(self);
    }
}

static void test_core(void)
{
    struct core_host host;
    struct apl_core core;
    struct apl_core_ops ops;
    enum apl_fan_gear invalid;

    host.water = APL_WATER_LEVEL_2;
    host.force_bad_water = 0;
    host.trigger = 0;
    host.recover = 0;
    host.power_off = 0;
    host.timer_fire = 0;
    host.uv_set_calls = 0;
    host.anion_set_calls = 0;
    host.pm_reads = 0;
    host.timer_at_fire = 99u;
    host.timer_at_off = 99u;
    host.core = &core;

    memset(&ops, 0, sizeof(ops));
    ops.get_water_level = get_water_level;
    ops.get_pm25 = get_pm25;
    ops.get_humidity = get_humidity;
    ops.get_temperature = get_temperature;
    ops.water_on_trigger = water_on_trigger;
    ops.water_on_recover = water_on_recover;
    ops.set_uv = set_uv;
    ops.set_anion = set_anion;
    ops.on_power_off = on_power_off;

    apl_core_init(NULL, &ops, &host);
    apl_core_tick(NULL);
    apl_core_power_on(NULL);
    apl_core_power_off(NULL);
    apl_core_set_timer(NULL, 1u, on_timer_off, &host);
    EXPECT(0u == apl_core_get_timer(NULL));
    EXPECT(APL_POWER_OFF == apl_core_get_power(NULL));
    EXPECT(APL_FAN_GEAR_1 == apl_core_get_fan_gear(NULL));
    apl_core_set_fan_gear(NULL, APL_FAN_GEAR_1);
#if APL_COMPILE_PURIFY
    EXPECT(0u == apl_core_get_filter_purify(NULL));
    EXPECT(0u == apl_core_get_purify_enabled(NULL));
    apl_core_filter_purify_reset(NULL);
#endif
#if APL_COMPILE_HUMIDIFY
    EXPECT(0u == apl_core_get_filter_humidify(NULL));
    EXPECT(0u == apl_core_get_humidify_enabled(NULL));
#endif

    apl_core_init(&core, &ops, &host);
    EXPECT(APL_POWER_OFF == apl_core_get_power(&core));
    EXPECT(APL_FAN_GEAR_1 == apl_core_get_fan_gear(&core));
#if APL_COMPILE_PURIFY
    EXPECT((uint8_t)APL_DEFAULT_PURIFY == apl_core_get_purify_enabled(&core));
    /* 除重置外没有写入方法，这里只核对初始为 0，重置后仍为 0。 */
    EXPECT(0u == apl_core_get_filter_purify(&core));
    apl_core_filter_purify_reset(&core);
    EXPECT(0u == apl_core_get_filter_purify(&core));
#endif
#if APL_COMPILE_HUMIDIFY
    EXPECT((uint8_t)APL_DEFAULT_HUMIDIFY == apl_core_get_humidify_enabled(&core));
    EXPECT(0u == apl_core_get_filter_humidify(&core));
    apl_core_filter_humidify_reset(&core);
    EXPECT(0u == apl_core_get_filter_humidify(&core));
#endif

    apl_core_power_off(&core);
    EXPECT(0 == host.power_off);

    apl_core_power_on(&core);
    EXPECT(APL_POWER_ON == apl_core_get_power(&core));
    apl_core_set_fan_gear(&core, APL_FAN_GEAR_3);
    invalid = (enum apl_fan_gear)99;
    apl_core_set_fan_gear(&core, invalid);
    EXPECT(APL_FAN_GEAR_3 == apl_core_get_fan_gear(&core));

    apl_core_set_timer(&core, 10u, on_timer_off, &host);
    core_ticks(&core, 10u);
    apl_core_power_off(&core);
    EXPECT(1 == host.power_off);
    EXPECT(0u == host.timer_at_off);
    EXPECT(0u == apl_core_get_timer(&core));
    EXPECT(0 == host.timer_fire);
    EXPECT(APL_FAN_GEAR_3 == apl_core_get_fan_gear(&core));
    apl_core_power_off(&core);
    EXPECT(1 == host.power_off);

    apl_core_power_on(&core);
    apl_core_set_timer(&core, 1u, on_timer_off, &host);
    core_ticks(&core, APL_CORE_TICKS_PER_SECOND - 1u);
    EXPECT(1u == apl_core_get_timer(&core));
    EXPECT(0 == host.timer_fire);
    core_ticks(&core, 1u);
    EXPECT(2 == host.power_off);
    EXPECT(1 == host.timer_fire);
    EXPECT(0u == host.timer_at_fire);
    EXPECT(APL_POWER_OFF == apl_core_get_power(&core));
    core_ticks(&core, APL_CORE_TICKS_PER_SECOND);
    EXPECT(1 == host.timer_fire);

    host.timer_fire = 0;
    apl_core_set_timer(&core, 1u, on_timer_on, &host);
    core_ticks(&core, APL_CORE_TICKS_PER_SECOND);
    EXPECT(1 == host.timer_fire);
    EXPECT(APL_POWER_ON == apl_core_get_power(&core));
    EXPECT(2 == host.power_off);

    apl_core_set_timer(&core, 5u, on_timer_off, &host);
    core_ticks(&core, 50u);
    apl_core_set_timer(&core, 3u, on_timer_off, &host);
    core_ticks(&core, APL_CORE_TICKS_PER_SECOND);
    EXPECT(2u == apl_core_get_timer(&core));
    EXPECT(1 == host.timer_fire);

    apl_core_set_timer(&core, 0u, on_timer_off, &host);
    core_ticks(&core, APL_CORE_TICKS_PER_SECOND * 2u);
    EXPECT(0u == apl_core_get_timer(&core));
    EXPECT(1 == host.timer_fire);

    apl_core_init(&core, &ops, &host);
    host.trigger = 0;
    host.recover = 0;
    host.water = APL_WATER_LEVEL_2;
    apl_core_tick(&core);
    EXPECT(0 == host.trigger);
    host.water = APL_WATER_LEVEL_4;
    apl_core_tick(&core);
    EXPECT(0 == host.trigger);
    EXPECT(0 == host.recover);
    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    EXPECT(1 == host.trigger);
    apl_core_tick(&core);
    EXPECT(1 == host.trigger);
    host.water = APL_WATER_LEVEL_1;
    apl_core_tick(&core);
    EXPECT(1 == host.recover);
    host.water = APL_WATER_LEVEL_3;
    apl_core_tick(&core);
    EXPECT(1 == host.recover);
    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    EXPECT(2 == host.trigger);

    host.force_bad_water = 1;
    apl_core_tick(&core);
    host.force_bad_water = 0;
    host.water = APL_WATER_LEVEL_2;
    apl_core_tick(&core);
    EXPECT(2 == host.recover);
    EXPECT(2 == host.trigger);

    EXPECT(0 == host.uv_set_calls);
    EXPECT(0 == host.anion_set_calls);
    /* 运行中每秒读一次 PM2.5，只在编入净化时。 */
    EXPECT((0u != APL_COMPILE_PURIFY) == (0 < host.pm_reads));

    apl_core_init(&core, NULL, NULL);
    apl_core_power_on(&core);
    apl_core_set_timer(&core, 1u, NULL, NULL);
    core_ticks(&core, APL_CORE_TICKS_PER_SECOND);
    EXPECT(0u == apl_core_get_timer(&core));
    apl_core_power_off(&core);
    EXPECT(APL_POWER_OFF == apl_core_get_power(&core));
}

struct key_ctx
{
    struct hmi_host *host;
    uint8_t index;
};

struct hmi_host
{
    struct apl_hmi_key_state level;
    struct key_ctx key_ctx[APL_HMI_KEY_COUNT];
    int short_count[APL_HMI_KEY_COUNT];
    int long_count[APL_HMI_KEY_COUNT];
    int combo_count;
    int lock_on_timer_long;
};

static void get_key_state(struct apl_hmi_key_state *state, void *ctx)
{
    struct hmi_host *host;

    host = ctx;
    *state = host->level;
}

static void on_short(struct apl_hmi *self, void *ctx)
{
    struct key_ctx *key;

    (void)self;
    key = ctx;
    key->host->short_count[key->index]++;
}

static void on_long(struct apl_hmi *self, void *ctx)
{
    struct key_ctx *key;

    key = ctx;
    key->host->long_count[key->index]++;
    if ((0 != key->host->lock_on_timer_long) && (APL_HMI_KEY_TIMER == key->index))
    {
        apl_hmi_set_child_lock(self, APL_HMI_CHILD_LOCK_ON);
    }
}

static void on_combo(struct apl_hmi *self, void *ctx)
{
    struct hmi_host *host;

    (void)self;
    host = ctx;
    host->combo_count++;
}

static void set_level(struct hmi_host *host, enum apl_hmi_key_id id, enum apl_hmi_key_level level)
{
    switch (id)
    {
    case APL_HMI_KEY_PURIFY:
        host->level.purify = level;
        break;
    case APL_HMI_KEY_HUMIDITY:
        host->level.humidity = level;
        break;
    case APL_HMI_KEY_TIMER:
        host->level.timer = level;
        break;
    case APL_HMI_KEY_LIGHT:
        host->level.light = level;
        break;
    case APL_HMI_KEY_SPEED:
        host->level.speed = level;
        break;
    case APL_HMI_KEY_POWER:
        host->level.power = level;
        break;
    default:
        break;
    }
}

static void release_all(struct hmi_host *host)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        set_level(host, (enum apl_hmi_key_id)index, APL_HMI_KEY_RELEASED);
    }
}

static void bind_keys(struct apl_hmi *hmi, struct hmi_host *host)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        host->key_ctx[index].host = host;
        host->key_ctx[index].index = index;
        apl_hmi_set_key_callback(hmi,
                                 (enum apl_hmi_key_id)index,
                                 on_short,
                                 on_long,
                                 &host->key_ctx[index]);
    }
}

static void hmi_ticks(struct apl_hmi *hmi, uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; index++)
    {
        apl_hmi_tick(hmi);
    }
}

static void clear_counts(struct hmi_host *host)
{
    uint8_t index;

    for (index = 0u; index < APL_HMI_KEY_COUNT; index++)
    {
        host->short_count[index] = 0;
        host->long_count[index] = 0;
    }

    host->combo_count = 0;
    host->lock_on_timer_long = 0;
}

static void test_hmi(void)
{
    struct hmi_host host;
    struct apl_hmi hmi;
    struct apl_hmi_ops ops;
    enum apl_hmi_key_id pair[2];
    enum apl_hmi_key_id same[2];
    enum apl_hmi_key_id one[1];
    enum apl_hmi_key_id bad[2];

    memset(&ops, 0, sizeof(ops));
    ops.get_key_state = get_key_state;
    clear_counts(&host);
    release_all(&host);

    apl_hmi_init(NULL, &ops, &host);
    apl_hmi_tick(NULL);
    EXPECT(-1 == apl_hmi_set_combo(NULL, pair, 2u, 100u, on_combo, &host));

    apl_hmi_init(&hmi, &ops, &host);
    hmi_ticks(&hmi, APL_HMI_POWERUP_LAMP_TICKS);
    EXPECT(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&hmi));
    EXPECT(1u == apl_hmi_get_purify_enabled(&hmi));
    EXPECT(1u == apl_hmi_get_humidify_enabled(&hmi));
    EXPECT(0u == apl_hmi_get_key_lamp(NULL, APL_HMI_KEY_POWER));
    EXPECT(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(NULL));
    EXPECT(APL_HMI_UNIT_NONE == apl_hmi_get_digits_unit(NULL));
    apl_hmi_set_child_lock(NULL, APL_HMI_CHILD_LOCK_ON);
    apl_hmi_set_key_callback(NULL, APL_HMI_KEY_POWER, on_short, on_long, &host);

    {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t zero_red;
        uint8_t zero_green;
        uint8_t zero_blue;
        char content[APL_HMI_DIGIT_COUNT];
        char out[APL_HMI_DIGIT_COUNT];

        apl_hmi_set_key_lamp(&hmi, APL_HMI_KEY_POWER, 3u);
        EXPECT(3u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_POWER));
        apl_hmi_set_key_lamp(&hmi, (enum apl_hmi_key_id)99, 9u);
        EXPECT(0u == apl_hmi_get_key_lamp(&hmi, (enum apl_hmi_key_id)99));

        apl_hmi_set_ambient_brightness(&hmi, 7u);
        EXPECT(7u == apl_hmi_get_ambient_brightness(&hmi));
        apl_hmi_set_ambient_color(&hmi, 1u, 2u, 3u);
        red = 0u;
        green = 0u;
        blue = 0u;
        apl_hmi_get_ambient_color(&hmi, &red, &green, &blue);
        EXPECT(1u == red);
        EXPECT(2u == green);
        EXPECT(3u == blue);
        apl_hmi_set_ambient_brightness(&hmi, 8u);
        apl_hmi_get_ambient_color(&hmi, &red, &green, &blue);
        EXPECT(1u == red);
        EXPECT(8u == apl_hmi_get_ambient_brightness(&hmi));
        apl_hmi_get_ambient_color(&hmi, NULL, NULL, NULL);
        zero_red = 9u;
        zero_green = 9u;
        zero_blue = 9u;
        apl_hmi_get_ambient_color(NULL, &zero_red, &zero_green, &zero_blue);
        EXPECT(0u == zero_red);
        EXPECT(0u == zero_green);
        EXPECT(0u == zero_blue);

        apl_hmi_set_water_lamp(&hmi, 1u, 1u);
        apl_hmi_set_water_lamp(&hmi, 4u, 4u);
        apl_hmi_set_water_lamp(&hmi, 0u, 9u);
        apl_hmi_set_water_lamp(&hmi, 5u, 9u);
        EXPECT(1u == apl_hmi_get_water_lamp(&hmi, 1u));
        EXPECT(0u == apl_hmi_get_water_lamp(&hmi, 2u));
        EXPECT(4u == apl_hmi_get_water_lamp(&hmi, 4u));
        EXPECT(0u == apl_hmi_get_water_lamp(&hmi, 0u));
        EXPECT(0u == apl_hmi_get_water_lamp(&hmi, 5u));

        apl_hmi_set_digits_brightness(&hmi, 2u);
        EXPECT(2u == apl_hmi_get_digits_brightness(&hmi));
        content[0] = '1';
        content[1] = '2';
        content[2] = '3';
        apl_hmi_set_digits_content(&hmi, NULL);
        apl_hmi_set_digits_content(NULL, content);
        apl_hmi_set_digits_content(&hmi, content);
        apl_hmi_get_digits_content(&hmi, out);
        EXPECT('1' == out[0]);
        EXPECT('2' == out[1]);
        EXPECT('3' == out[2]);
        out[0] = 'x';
        out[1] = 'x';
        out[2] = 'x';
        apl_hmi_get_digits_content(NULL, out);
        EXPECT(0 == out[0]);
        EXPECT(0 == out[1]);
        EXPECT(0 == out[2]);
        apl_hmi_set_digits_unit(&hmi, APL_HMI_UNIT_PERCENT);
        EXPECT(APL_HMI_UNIT_PERCENT == apl_hmi_get_digits_unit(&hmi));
        apl_hmi_set_digits_unit(&hmi, (enum apl_hmi_unit)40);
        EXPECT(APL_HMI_UNIT_PERCENT == apl_hmi_get_digits_unit(&hmi));

        apl_hmi_set_purify_enabled(&hmi, 2u);
        EXPECT(1u == apl_hmi_get_purify_enabled(&hmi));
        apl_hmi_set_humidify_enabled(&hmi, 3u);
        EXPECT(1u == apl_hmi_get_humidify_enabled(&hmi));
        apl_hmi_set_child_lock(&hmi, (enum apl_hmi_child_lock)7);
        EXPECT(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&hmi));
    }

    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, 5u);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);
    clear_counts(&host);
    bind_keys(&hmi, &host);

    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, 5u);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(1 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);

    clear_counts(&host);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS - 1u);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(1 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);

    clear_counts(&host);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(1 == host.long_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);
    hmi_ticks(&hmi, 50u);
    EXPECT(1 == host.long_count[APL_HMI_KEY_SPEED]);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(1 == host.long_count[APL_HMI_KEY_SPEED]);

    clear_counts(&host);
    apl_hmi_set_key_callback(&hmi, APL_HMI_KEY_LIGHT, NULL, NULL, NULL);
    set_level(&host, APL_HMI_KEY_LIGHT, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    set_level(&host, APL_HMI_KEY_LIGHT, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_LIGHT]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_LIGHT]);
    bind_keys(&hmi, &host);

    clear_counts(&host);
    apl_hmi_set_child_lock(&hmi, APL_HMI_CHILD_LOCK_ON);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, 5u);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);

    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);
    EXPECT(1 == host.long_count[APL_HMI_KEY_TIMER]);
    hmi_ticks(&hmi, 20u);
    EXPECT(1 == host.long_count[APL_HMI_KEY_TIMER]);
    release_all(&host);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.short_count[APL_HMI_KEY_TIMER]);

    clear_counts(&host);
    apl_hmi_set_child_lock(&hmi, APL_HMI_CHILD_LOCK_OFF);
    host.lock_on_timer_long = 1;
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(1 == host.long_count[APL_HMI_KEY_TIMER]);
    EXPECT(1 == host.long_count[APL_HMI_KEY_SPEED]);
    EXPECT(APL_HMI_CHILD_LOCK_ON == apl_hmi_get_child_lock(&hmi));
    release_all(&host);
    apl_hmi_tick(&hmi);
    apl_hmi_set_child_lock(&hmi, APL_HMI_CHILD_LOCK_OFF);

    clear_counts(&host);
    apl_hmi_set_purify_enabled(&hmi, 1u);
    apl_hmi_set_key_lamp(&hmi, APL_HMI_KEY_PURIFY, 4u);
    EXPECT(4u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_PURIFY));
    apl_hmi_set_purify_enabled(&hmi, 0u);
    apl_hmi_tick(&hmi);
    EXPECT(0u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_PURIFY));
    apl_hmi_set_key_lamp(&hmi, APL_HMI_KEY_PURIFY, 4u);
    EXPECT(0u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_PURIFY));
    set_level(&host, APL_HMI_KEY_PURIFY, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, 5u);
    set_level(&host, APL_HMI_KEY_PURIFY, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_PURIFY]);
    set_level(&host, APL_HMI_KEY_PURIFY, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(1 == host.long_count[APL_HMI_KEY_PURIFY]);
    EXPECT(0u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_PURIFY));
    release_all(&host);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_PURIFY]);
    apl_hmi_set_purify_enabled(&hmi, 1u);

    pair[0] = APL_HMI_KEY_TIMER;
    pair[1] = APL_HMI_KEY_SPEED;
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    EXPECT(-1 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    pair[0] = APL_HMI_KEY_SPEED;
    pair[1] = APL_HMI_KEY_TIMER;
    EXPECT(-1 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    one[0] = APL_HMI_KEY_POWER;
    EXPECT(-1 == apl_hmi_set_combo(&hmi, one, 1u, 100u, on_combo, &host));
    EXPECT(-1 == apl_hmi_set_combo(&hmi, pair, 2u, 0u, on_combo, &host));
    EXPECT(-1 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, NULL, &host));
    same[0] = APL_HMI_KEY_LIGHT;
    same[1] = APL_HMI_KEY_LIGHT;
    EXPECT(-1 == apl_hmi_set_combo(&hmi, same, 2u, 100u, on_combo, &host));
    bad[0] = APL_HMI_KEY_POWER;
    bad[1] = (enum apl_hmi_key_id)99;
    EXPECT(-1 == apl_hmi_set_combo(&hmi, bad, 2u, 100u, on_combo, &host));

    clear_counts(&host);
    release_all(&host);
    apl_hmi_init(&hmi, &ops, &host);
    bind_keys(&hmi, &host);
    pair[0] = APL_HMI_KEY_TIMER;
    pair[1] = APL_HMI_KEY_SPEED;
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, 10u);
    EXPECT(1 == host.combo_count);
    EXPECT(0 == host.long_count[APL_HMI_KEY_TIMER]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);
    hmi_ticks(&hmi, 10u);
    EXPECT(1 == host.combo_count);
    release_all(&host);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_TIMER]);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);

    clear_counts(&host);
    apl_hmi_init(&hmi, &ops, &host);
    bind_keys(&hmi, &host);
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 5000u, on_combo, &host));
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, 5u);
    release_all(&host);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.combo_count);
    EXPECT(1 == host.short_count[APL_HMI_KEY_TIMER]);
    EXPECT(1 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_TIMER]);

    clear_counts(&host);
    apl_hmi_init(&hmi, &ops, &host);
    bind_keys(&hmi, &host);
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 5000u, on_combo, &host));
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(0 == host.combo_count);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_TIMER]);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(1 == host.long_count[APL_HMI_KEY_SPEED]);
    EXPECT(1 == host.long_count[APL_HMI_KEY_TIMER]);
    EXPECT(0 == host.short_count[APL_HMI_KEY_SPEED]);
    EXPECT(0 == host.combo_count);
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_RELEASED);
    apl_hmi_tick(&hmi);
    EXPECT(0 == host.short_count[APL_HMI_KEY_TIMER]);

    clear_counts(&host);
    apl_hmi_init(&hmi, &ops, &host);
    bind_keys(&hmi, &host);
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    apl_hmi_set_child_lock(&hmi, APL_HMI_CHILD_LOCK_ON);
    set_level(&host, APL_HMI_KEY_TIMER, APL_HMI_KEY_PRESSED);
    set_level(&host, APL_HMI_KEY_SPEED, APL_HMI_KEY_PRESSED);
    hmi_ticks(&hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(0 == host.combo_count);
    EXPECT(1 == host.long_count[APL_HMI_KEY_TIMER]);
    EXPECT(0 == host.long_count[APL_HMI_KEY_SPEED]);

    pair[0] = APL_HMI_KEY_PURIFY;
    pair[1] = APL_HMI_KEY_HUMIDITY;
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    pair[0] = APL_HMI_KEY_LIGHT;
    pair[1] = APL_HMI_KEY_POWER;
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    pair[0] = APL_HMI_KEY_PURIFY;
    pair[1] = APL_HMI_KEY_POWER;
    EXPECT(0 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));
    pair[0] = APL_HMI_KEY_HUMIDITY;
    pair[1] = APL_HMI_KEY_LIGHT;
    EXPECT(-1 == apl_hmi_set_combo(&hmi, pair, 2u, 100u, on_combo, &host));

    apl_hmi_init(&hmi, NULL, NULL);
    apl_hmi_set_purify_enabled(&hmi, 1u);
    apl_hmi_set_key_lamp(&hmi, APL_HMI_KEY_PURIFY, 5u);
    apl_hmi_set_purify_enabled(&hmi, 0u);
    apl_hmi_tick(&hmi);
    EXPECT(0u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_PURIFY));

    ops.get_key_state = NULL;
    apl_hmi_init(&hmi, &ops, &host);
    apl_hmi_set_humidify_enabled(&hmi, 1u);
    apl_hmi_set_key_lamp(&hmi, APL_HMI_KEY_HUMIDITY, 6u);
    apl_hmi_set_humidify_enabled(&hmi, 0u);
    apl_hmi_tick(&hmi);
    EXPECT(0u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_HUMIDITY));
}

static void apl_ticks(struct apl *self, uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; index++)
    {
        apl_tick(self);
    }
}

static void test_compose(void)
{
    struct apl machine;
    struct core_host core_host;
    struct hmi_host hmi_host;
    struct apl_core_ops core_ops;
    struct apl_hmi_ops hmi_ops;

    core_host.water = APL_WATER_LEVEL_2;
    core_host.force_bad_water = 0;
    core_host.trigger = 0;
    core_host.recover = 0;
    core_host.power_off = 0;
    core_host.timer_fire = 0;
    core_host.uv_set_calls = 0;
    core_host.anion_set_calls = 0;
    core_host.pm_reads = 0;
    core_host.timer_at_fire = 99u;
    core_host.timer_at_off = 99u;
    core_host.core = &machine.core;

    memset(&core_ops, 0, sizeof(core_ops));
    core_ops.get_water_level = get_water_level;
    core_ops.get_pm25 = get_pm25;
    core_ops.get_humidity = get_humidity;
    core_ops.get_temperature = get_temperature;
    core_ops.water_on_trigger = water_on_trigger;
    core_ops.water_on_recover = water_on_recover;
    core_ops.set_uv = set_uv;
    core_ops.set_anion = set_anion;
    core_ops.on_power_off = on_power_off;

    memset(&hmi_ops, 0, sizeof(hmi_ops));
    hmi_ops.get_key_state = get_key_state;
    clear_counts(&hmi_host);
    release_all(&hmi_host);

    apl_init(NULL, &core_ops, &core_host, &hmi_ops, &hmi_host);
    apl_tick(NULL);

    apl_init(&machine, &core_ops, &core_host, &hmi_ops, &hmi_host);
    EXPECT(APL_POWER_OFF == apl_core_get_power(&machine.core));
    EXPECT(APL_FAN_GEAR_1 == apl_core_get_fan_gear(&machine.core));
    EXPECT(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&machine.hmi));
    EXPECT(0 == core_host.trigger);
    EXPECT(0 == core_host.pm_reads);

    apl_core_power_on(&machine.core);
    apl_core_set_timer(&machine.core, 1u, on_timer_off, &core_host);
    apl_ticks(&machine, APL_CORE_TICKS_PER_SECOND - 1u);
    EXPECT(1u == apl_core_get_timer(&machine.core));
    EXPECT(0 == core_host.timer_fire);
    apl_tick(&machine);
    EXPECT(1 == core_host.timer_fire);
    EXPECT(APL_POWER_OFF == apl_core_get_power(&machine.core));
    EXPECT(1 == core_host.power_off);
    EXPECT(0u == apl_core_get_timer(&machine.core));

    bind_keys(&machine.hmi, &hmi_host);
    set_level(&hmi_host, APL_HMI_KEY_POWER, APL_HMI_KEY_PRESSED);
    apl_tick(&machine);
    EXPECT(0 == hmi_host.short_count[APL_HMI_KEY_POWER]);
    set_level(&hmi_host, APL_HMI_KEY_POWER, APL_HMI_KEY_RELEASED);
    apl_tick(&machine);
    EXPECT(1 == hmi_host.short_count[APL_HMI_KEY_POWER]);

    core_host.water = APL_WATER_LEVEL_0;
    apl_tick(&machine);
    EXPECT(1 == core_host.trigger);
    EXPECT(0 == core_host.pm_reads);
    EXPECT(0 == core_host.uv_set_calls);
    EXPECT(0 == core_host.anion_set_calls);
}

static void test_powerup_lamps(void)
{
    struct apl_hmi hmi;
    struct hmi_host host;
    struct apl_hmi_ops ops;
    char digits[APL_HMI_DIGIT_COUNT];
    uint8_t index;

    memset(&ops, 0, sizeof(ops));
    ops.get_key_state = get_key_state;
    clear_counts(&host);
    release_all(&host);
    apl_hmi_init(&hmi, &ops, &host);

    EXPECT(1u == apl_hmi_get_powerup_lamp(&hmi));
    EXPECT(0u == apl_hmi_get_powerup_lamp(NULL));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_POWER));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_SPEED));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_ambient_brightness(&hmi));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_water_lamp(&hmi, 1u));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_water_lamp(&hmi, 4u));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_digits_brightness(&hmi));
    apl_hmi_get_digits_content(&hmi, digits);
    EXPECT(('8' == digits[0]) && ('8' == digits[1]) && ('8' == digits[2]));

    hmi_ticks(&hmi, APL_HMI_POWERUP_LAMP_TICKS - 1u);
    EXPECT(1u == apl_hmi_get_powerup_lamp(&hmi));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_POWER));

    apl_hmi_tick(&hmi);
    EXPECT(0u == apl_hmi_get_powerup_lamp(&hmi));
    EXPECT(APL_HMI_LAMP_HALF == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_POWER));
    EXPECT(0u == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_SPEED));
    EXPECT(0u == apl_hmi_get_ambient_brightness(&hmi));
    EXPECT(0u == apl_hmi_get_digits_brightness(&hmi));
    for (index = 1u; index <= APL_HMI_WATER_LAMP_COUNT; index++)
    {
        EXPECT(0u == apl_hmi_get_water_lamp(&hmi, index));
    }
    apl_hmi_get_digits_content(&hmi, digits);
    EXPECT((' ' == digits[0]) && (' ' == digits[1]) && (' ' == digits[2]));

    apl_hmi_tick(&hmi);
    EXPECT(APL_HMI_LAMP_HALF == apl_hmi_get_key_lamp(&hmi, APL_HMI_KEY_POWER));
}

int main(void)
{
    test_core();
    test_hmi();
    test_compose();
    test_powerup_lamps();
    if (0 != g_failed)
    {
        printf("%d failed\n", g_failed);
        return 1;
    }

    printf("ok\n");
    return 0;
}
