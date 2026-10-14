/**
 * @file    test_hmi_key_optional.c
 * @brief   灯光键未编入时，手势和键灯都不起作用，其余键仍可用。
 *
 * cl /nologo /std:c11 /utf-8 /W4 /WX /I include /I config
 *    /DAPL_HMI_COMPILE_KEY_LIGHT=0
 *    tests\test_hmi_key_optional.c src\apl.c src\apl_core.c src\apl_hmi.c
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

struct key_host
{
    struct apl_hmi_key_state level;
    int light_short;
    int light_long;
    int speed_short;
};

static void key_state_get(struct apl_hmi_key_state *state, void *ctx)
{
    struct key_host *host;

    host = ctx;
    *state = host->level;
}

static void on_light_short(struct apl_hmi *hmi, void *ctx)
{
    struct key_host *host;

    (void)hmi;
    host = ctx;
    host->light_short++;
}

static void on_light_long(struct apl_hmi *hmi, void *ctx)
{
    struct key_host *host;

    (void)hmi;
    host = ctx;
    host->light_long++;
}

static void on_speed_short(struct apl_hmi *hmi, void *ctx)
{
    struct key_host *host;

    (void)hmi;
    host = ctx;
    host->speed_short++;
}

static void on_combo(struct apl_hmi *hmi, void *ctx)
{
    (void)hmi;
    (void)ctx;
}

static void ticks(struct apl_hmi *hmi, uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; index++)
    {
        apl_hmi_tick(hmi);
    }
}

int main(void)
{
    struct apl machine;
    struct key_host host;
    struct apl_hmi_ops ops;
    enum apl_hmi_key_id pair[2];

    host.level.purify = APL_HMI_KEY_RELEASED;
    host.level.humidity = APL_HMI_KEY_RELEASED;
    host.level.timer = APL_HMI_KEY_RELEASED;
    host.level.light = APL_HMI_KEY_RELEASED;
    host.level.speed = APL_HMI_KEY_RELEASED;
    host.level.power = APL_HMI_KEY_RELEASED;
    host.light_short = 0;
    host.light_long = 0;
    host.speed_short = 0;

    memset(&ops, 0, sizeof(ops));
    ops.key_state_get = key_state_get;
    apl_init(&machine, NULL, NULL, &ops, &host);
    apl_hmi_key_callback_set(&machine.hmi, APL_HMI_KEY_LIGHT, on_light_short, on_light_long, &host);
    apl_hmi_key_callback_set(&machine.hmi, APL_HMI_KEY_SPEED, on_speed_short, NULL, &host);

    apl_hmi_key_lamp_set(&machine.hmi, APL_HMI_KEY_LIGHT, 5u);
    EXPECT(0u == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_LIGHT));
    apl_hmi_key_lamp_set(&machine.hmi, APL_HMI_KEY_SPEED, 3u);
    EXPECT(3u == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_SPEED));

    host.level.light = APL_HMI_KEY_PRESSED;
    ticks(&machine.hmi, APL_HMI_LONG_PRESS_TICKS);
    EXPECT(0 == host.light_long);
    host.level.light = APL_HMI_KEY_RELEASED;
    apl_hmi_tick(&machine.hmi);
    EXPECT(0 == host.light_short);
    EXPECT(0 == host.light_long);

    host.level.speed = APL_HMI_KEY_PRESSED;
    apl_hmi_tick(&machine.hmi);
    host.level.speed = APL_HMI_KEY_RELEASED;
    apl_hmi_tick(&machine.hmi);
    EXPECT(1 == host.speed_short);

    pair[0] = APL_HMI_KEY_LIGHT;
    pair[1] = APL_HMI_KEY_POWER;
    EXPECT(-1 == apl_hmi_combo_set(&machine.hmi, pair, 2u, 100u, on_combo, &host));

    if (0 != g_failed)
    {
        printf("%d failed\n", g_failed);
        return 1;
    }

    printf("ok\n");
    return 0;
}
