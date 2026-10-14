/**
 * @file    test_link.c
 * @brief   宿主侧检查主控业务、link_command / link_snapshot 和人机面板逻辑。
 *
 * gcc -std=c11 -Wall -Wextra -Werror -I include -I config
 *     tests/test_link.c src/apl.c src/apl_core.c src/apl_hmi.c
 * 净化单开、加湿单开、两项都开三种编译都要通过。
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

#define HAS_PURIFY    (0u != APL_COMPILE_PURIFY)
#define HAS_HUMIDIFY  (0u != APL_COMPILE_HUMIDIFY)
#define TICKS_PER_S   APL_CORE_TICKS_PER_SECOND

struct host
{
    enum apl_water_level water;
    uint16_t pm25;
    uint16_t pm10;
    uint16_t hcho;
    uint8_t humidity;
    uint8_t interlock;
    enum apl_switch fan;
    enum apl_fan_gear fan_gear;
    enum apl_switch pump;
    enum apl_switch uv;
    enum apl_switch anion;
    int power_off;
    int memory_saves;
    struct apl_core_memory memory;
    struct apl_hmi_key_state keys;
    int buzzer_on_edges;
    uint8_t buzzer;
};

static enum apl_water_level water_level_get(void *ctx)
{
    return ((struct host *)ctx)->water;
}

static uint16_t pm25_get(void *ctx)
{
    return ((struct host *)ctx)->pm25;
}

static uint16_t pm10_get(void *ctx)
{
    return ((struct host *)ctx)->pm10;
}

static uint16_t hcho_get(void *ctx)
{
    return ((struct host *)ctx)->hcho;
}

static uint8_t humidity_get(void *ctx)
{
    return ((struct host *)ctx)->humidity;
}

static uint8_t interlock_get(void *ctx)
{
    return ((struct host *)ctx)->interlock;
}

static void fan_set(enum apl_switch state, enum apl_fan_gear gear, void *ctx)
{
    ((struct host *)ctx)->fan = state;
    ((struct host *)ctx)->fan_gear = gear;
}

static void pump_set(enum apl_switch state, void *ctx)
{
    ((struct host *)ctx)->pump = state;
}

static void uv_set(enum apl_switch state, void *ctx)
{
    ((struct host *)ctx)->uv = state;
}

static void anion_set(enum apl_switch state, void *ctx)
{
    ((struct host *)ctx)->anion = state;
}

static void on_power_off(struct apl_core *self, void *ctx)
{
    (void)self;
    ((struct host *)ctx)->power_off++;
}

static void memory_save(const struct apl_core_memory *memory, void *ctx)
{
    ((struct host *)ctx)->memory = *memory;
    ((struct host *)ctx)->memory_saves++;
}

static void key_state_get(struct apl_hmi_key_state *state, void *ctx)
{
    *state = ((struct host *)ctx)->keys;
}

static void buzzer_set(uint8_t on, void *ctx)
{
    struct host *host;

    host = ctx;
    if ((0u != on) && (0u == host->buzzer))
    {
        host->buzzer_on_edges++;
    }
    host->buzzer = on;
}

static struct apl_core_ops g_core_ops;
static struct apl_hmi_ops g_hmi_ops;

static void host_reset(struct host *host)
{
    memset(host, 0, sizeof(*host));
    host->water = APL_WATER_LEVEL_3;
    host->pm25 = 20u;
    host->humidity = 50u;
    host->interlock = 1u;

    memset(&g_core_ops, 0, sizeof(g_core_ops));
    g_core_ops.water_level_get = water_level_get;
    g_core_ops.pm25_get = pm25_get;
    g_core_ops.humidity_get = humidity_get;
    g_core_ops.uv_set = uv_set;
    g_core_ops.anion_set = anion_set;
    g_core_ops.on_power_off = on_power_off;
    g_core_ops.uv_interlock_get = interlock_get;
    g_core_ops.fan_set = fan_set;
    g_core_ops.pump_set = pump_set;
    g_core_ops.memory_save = memory_save;

    memset(&g_hmi_ops, 0, sizeof(g_hmi_ops));
    g_hmi_ops.key_state_get = key_state_get;
    g_hmi_ops.buzzer_set = buzzer_set;
}

static enum apl_link_result cmd(struct apl_core *core, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command command;

    command.type = type;
    command.arg = arg;
    return apl_core_command(core, &command);
}

static void core_seconds(struct apl_core *core, uint32_t seconds)
{
    uint32_t index;

    for (index = 0u; index < seconds * TICKS_PER_S; index++)
    {
        apl_core_tick(core);
    }
}

static struct apl_link_snapshot snap_of(const struct apl_core *core)
{
    struct apl_link_snapshot snap;

    apl_core_snapshot_get(core, &snap);
    return snap;
}

static void core_start(struct apl_core *core, struct host *host)
{
    host_reset(host);
    apl_core_init(core, &g_core_ops, host);
    apl_core_tick(core);
}

static void test_reject(void)
{
    struct apl_core core;
    struct host host;
    struct apl_link_snapshot snap;

    core_start(&core, &host);
    EXPECT(APL_LINK_REJECTED == apl_core_command(&core, NULL));
    EXPECT(APL_LINK_REJECTED == apl_core_command(NULL, NULL));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_NONE, 0u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_FAN_CYCLE, 0u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_TIMER_SET, 1u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_DRY_STOP, 0u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_FACTORY_EXIT, 0u));

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_POWER, 0u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_TIMER_SET, 25u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_FAN_SET, 9u));

    /* 功能未编入时主控拒绝，快照保持原状态。 */
    EXPECT((HAS_PURIFY ? APL_LINK_OK : APL_LINK_REJECTED)
           == cmd(&core, APL_LINK_CMD_PURIFY_TOGGLE, 0u));
    EXPECT((HAS_HUMIDIFY ? APL_LINK_OK : APL_LINK_REJECTED)
           == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 60u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 62u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 85u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 35u));
    snap = snap_of(&core);
    EXPECT(snap.purify_enabled == (HAS_PURIFY ? 1u : 0u));
    EXPECT(snap.humidify_enabled == (HAS_HUMIDIFY ? 1u : 0u));
    EXPECT(snap.purify_on == (HAS_PURIFY ? 1u : 0u));
    EXPECT(snap.humidity_target == (HAS_HUMIDIFY ? 60u : 0u));
    if (!HAS_PURIFY)
    {
        EXPECT(0u == snap.filter_purify_left);
        EXPECT(0u == snap.pm25);
    }
    if (!HAS_HUMIDIFY)
    {
        EXPECT(APL_WATER_LEVEL_0 == snap.water_level);
        EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u));
    }
}

static void test_power_and_dry(void)
{
    struct apl_core core;
    struct host host;
    struct apl_link_snapshot snap;

    core_start(&core, &host);
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_POWER, 0u));
    apl_core_tick(&core);
    EXPECT(APL_RUN_ON == apl_core_run_state_get(&core));
    EXPECT(APL_FAN_GEAR_1 == apl_core_fan_gear_get(&core));
    EXPECT(APL_SWITCH_ON == host.fan);
    EXPECT(APL_FAN_GEAR_1 == host.fan_gear);
    EXPECT(1u == host.memory.running);
    EXPECT(1u == host.memory.used);

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FAN_CYCLE, 0u));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FAN_CYCLE, 0u));
    apl_core_tick(&core);
    EXPECT(APL_FAN_GEAR_3 == host.fan_gear);
    EXPECT(APL_FAN_GEAR_3 == host.memory.fan_gear);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FAN_CYCLE, 0u));
    EXPECT(APL_FAN_GEAR_AUTO == apl_core_fan_gear_get(&core));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FAN_CYCLE, 0u));
    EXPECT(APL_FAN_GEAR_SLEEP == apl_core_fan_gear_get(&core));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FAN_CYCLE, 0u));
    EXPECT(APL_FAN_GEAR_1 == apl_core_fan_gear_get(&core));

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_TIMER_SET, 3u));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_POWER, 0u));
    apl_core_tick(&core);
    snap = snap_of(&core);
    EXPECT(0u == snap.timer_left_s);
    if (HAS_HUMIDIFY)
    {
        /* 加湿启用：先风干，停泵，风机按风干档。 */
        EXPECT(APL_RUN_DRY == snap.run_state);
        EXPECT(APL_CORE_DRY_S == snap.dry_left_s);
        EXPECT(APL_SWITCH_OFF == host.pump);
        EXPECT(APL_SWITCH_ON == host.fan);
        EXPECT(0u == host.memory.running);
        EXPECT(0 == host.power_off);
        EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_POWER, 0u));
        core_seconds(&core, APL_CORE_DRY_S - 1u);
        EXPECT(APL_RUN_DRY == apl_core_run_state_get(&core));
        core_seconds(&core, 1u);
        EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));
        EXPECT(1 == host.power_off);
        EXPECT(APL_SWITCH_OFF == host.fan);

        /* 风干中长按电源键立即结束。 */
        EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_POWER, 0u));
        EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_POWER, 0u));
        EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_DRY_STOP, 0u));
        EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));
        EXPECT(2 == host.power_off);
    }
    else
    {
        EXPECT(APL_RUN_STANDBY == snap.run_state);
        EXPECT(1 == host.power_off);
        EXPECT(APL_SWITCH_OFF == host.fan);
    }

    /* 非首次开机恢复记忆档位。 */
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_POWER, 0u));
    EXPECT(APL_FAN_GEAR_1 == apl_core_fan_gear_get(&core));
}

static void test_memory(void)
{
    struct apl_core core;
    struct host host;
    struct apl_core_memory memory;

    core_start(&core, &host);
    memset(&memory, 0, sizeof(memory));
    memory.running = 1u;
    memory.used = 1u;
    memory.fan_gear = APL_FAN_GEAR_3;
    memory.purify_on = 1u;
    memory.humidify_on = 1u;
    memory.filter_purify_s = 1234u;
    apl_core_memory_restore(&core, &memory);
    EXPECT(APL_RUN_ON == apl_core_run_state_get(&core));
    EXPECT(APL_FAN_GEAR_3 == apl_core_fan_gear_get(&core));
    apl_core_memory_get(&core, &memory);
    EXPECT(1234u == memory.filter_purify_s);
    EXPECT(1u == memory.running);

    /* 正常关机后再上电保持待机。 */
    core_start(&core, &host);
    memory.running = 0u;
    apl_core_memory_restore(&core, &memory);
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));

    /* 运行中掉电：用过的机器自动开机；首次没有手动开过机则不开。 */
    core_start(&core, &host);
    memory.running = 1u;
    memory.used = 0u;
    apl_core_memory_restore(&core, &memory);
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));

    apl_core_memory_get(NULL, &memory);
    EXPECT(0u == memory.running);
    apl_core_memory_restore(&core, NULL);
}

static void test_timer_turbo_sleep(void)
{
    struct apl_core core;
    struct host host;
    struct apl_link_snapshot snap;

    core_start(&core, &host);
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_TIMER_SET, 1u));
    EXPECT(3600u == apl_core_timer_get(&core));
    core_seconds(&core, 3599u);
    EXPECT(APL_RUN_ON == apl_core_run_state_get(&core));
    core_seconds(&core, 1u);
    /* 定时到期整机关机进入待机，不走风干。 */
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));

    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_TIMER_SET, 2u));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_TIMER_SET, 0u));
    EXPECT(0u == apl_core_timer_get(&core));

    (void)cmd(&core, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_2);
    apl_core_tick(&core);
    EXPECT((HAS_HUMIDIFY ? APL_SWITCH_ON : APL_SWITCH_OFF) == host.pump);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_TURBO_TOGGLE, 0u));
    apl_core_tick(&core);
    EXPECT(APL_FAN_GEAR_TURBO == host.fan_gear);
    EXPECT(APL_SWITCH_OFF == host.pump);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_TURBO_TOGGLE, 0u));
    apl_core_tick(&core);
    EXPECT(APL_FAN_GEAR_2 == host.fan_gear);
    EXPECT((HAS_HUMIDIFY ? APL_SWITCH_ON : APL_SWITCH_OFF) == host.pump);

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_SLEEP_TOGGLE, 0u));
    snap = snap_of(&core);
    EXPECT(1u == snap.sleep);
    EXPECT(APL_FAN_GEAR_SLEEP == snap.fan_gear);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_SLEEP_TOGGLE, 0u));
    snap = snap_of(&core);
    EXPECT(0u == snap.sleep);
    EXPECT(APL_FAN_GEAR_2 == snap.fan_gear);
}

static void test_humidity(void)
{
    struct apl_core core;
    struct host host;
    struct apl_link_snapshot snap;

    if (!HAS_HUMIDIFY)
    {
        return;
    }

    core_start(&core, &host);
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    (void)cmd(&core, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_3);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 60u));

    host.humidity = 45u;
    core_seconds(&core, 1u);
    EXPECT(APL_FAN_GEAR_2 == host.fan_gear);   /* 差 15 */
    EXPECT(APL_SWITCH_ON == host.pump);
    host.humidity = 40u;
    core_seconds(&core, 1u);
    EXPECT(APL_FAN_GEAR_3 == host.fan_gear);   /* 差 20 */
    host.humidity = 51u;
    core_seconds(&core, 1u);
    EXPECT(APL_FAN_GEAR_1 == host.fan_gear);   /* 差 9 */
    host.humidity = 60u;
    core_seconds(&core, 1u);
    EXPECT(APL_FAN_GEAR_1 == host.fan_gear);   /* 达到仍一档 */
    EXPECT(APL_SWITCH_OFF == host.pump);       /* 达到停泵 */
    host.humidity = 58u;
    core_seconds(&core, 1u);
    EXPECT(APL_SWITCH_OFF == host.pump);       /* 回差内 */
    host.humidity = (uint8_t)(60u - APL_CORE_HUMIDITY_HYSTERESIS);
    core_seconds(&core, 1u);
    EXPECT(APL_SWITCH_ON == host.pump);

    /* OF：不恒湿，按用户档位，持续加湿。 */
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, APL_HUMIDITY_TARGET_OFF));
    core_seconds(&core, 1u);
    EXPECT(APL_FAN_GEAR_3 == host.fan_gear);
    EXPECT(APL_SWITCH_ON == host.pump);

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u));
    apl_core_tick(&core);
    EXPECT(APL_SWITCH_OFF == host.pump);
    snap = snap_of(&core);
    EXPECT(0u == snap.humidify_on);

    /* 水位无效时不能打开加湿、不能设恒湿。 */
    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u));
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 50u));
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, APL_HUMIDITY_TARGET_OFF));

    /* 关机清除恒湿。 */
    host.water = APL_WATER_LEVEL_2;
    apl_core_tick(&core);
    (void)cmd(&core, APL_LINK_CMD_HUMIDITY_TARGET, 70u);
    apl_core_power_off(&core);
    EXPECT(APL_HUMIDITY_TARGET_OFF == snap_of(&core).humidity_target);
}

static void test_water(void)
{
    struct apl_core core;
    struct host host;
    struct apl_link_snapshot snap;
    uint8_t seq;

    core_start(&core, &host);
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    apl_core_tick(&core);
    seq = snap_of(&core).beep_seq;

    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    snap = snap_of(&core);
    if (!HAS_HUMIDIFY)
    {
        EXPECT(APL_WATER_ALERT_NONE == snap.water_alert);
        EXPECT(seq == snap.beep_seq);
        return;
    }

    EXPECT(APL_WATER_ALERT_BLINK == snap.water_alert);
    EXPECT(APL_BEEP_WATER_SHORTAGE == snap.beep);
    EXPECT((uint8_t)(seq + 1u) == snap.beep_seq);
    EXPECT(APL_SWITCH_OFF == host.pump);
    EXPECT(APL_SWITCH_ON == host.fan);

    core_seconds(&core, APL_CORE_WATER_BLINK_S);
    EXPECT(APL_WATER_ALERT_STEADY == snap_of(&core).water_alert);

    /* 加水恢复。 */
    host.water = APL_WATER_LEVEL_2;
    apl_core_tick(&core);
    EXPECT(APL_WATER_ALERT_NONE == snap_of(&core).water_alert);
    EXPECT(APL_SWITCH_ON == host.pump);

    /* 加满提示。 */
    host.water = APL_WATER_LEVEL_4;
    apl_core_tick(&core);
    EXPECT(APL_BEEP_WATER_FULL == snap_of(&core).beep);

    /* 缺水 30 分钟未加水进入风干。 */
    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    core_seconds(&core, APL_CORE_WATER_SHORTAGE_RUN_S - 1u);
    EXPECT(APL_RUN_ON == apl_core_run_state_get(&core));
    core_seconds(&core, 1u);
    EXPECT(APL_RUN_DRY == apl_core_run_state_get(&core));

    /* 睡眠中缺水不响。 */
    core_start(&core, &host);
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    (void)cmd(&core, APL_LINK_CMD_SLEEP_TOGGLE, 0u);
    seq = snap_of(&core).beep_seq;
    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    snap = snap_of(&core);
    EXPECT(seq == snap.beep_seq);
    EXPECT(APL_WATER_ALERT_BLINK == snap.water_alert);

    /* 开机时已缺水，直接按缺水处理。 */
    core_start(&core, &host);
    host.water = APL_WATER_LEVEL_0;
    apl_core_tick(&core);
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    EXPECT(APL_WATER_ALERT_BLINK == snap_of(&core).water_alert);
}

static void test_filter(void)
{
    struct apl_core core;
    struct host host;
    struct apl_core_memory memory;
    struct apl_link_snapshot snap;
    const uint32_t life = APL_CORE_FILTER_LIFE_H * 3600u;

    core_start(&core, &host);
    snap = snap_of(&core);
    EXPECT(snap.filter_purify_left == (HAS_PURIFY ? 99u : 0u));
    EXPECT(snap.filter_humidify_left == (HAS_HUMIDIFY ? 99u : 0u));

    memset(&memory, 0, sizeof(memory));
    memory.filter_purify_s = life - 1u;
    memory.filter_humidify_s = life - 1u;
    apl_core_memory_restore(&core, &memory);
    snap = snap_of(&core);
    EXPECT(snap.filter_purify_left == (HAS_PURIFY ? 1u : 0u));

    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    core_seconds(&core, 1u);
    snap = snap_of(&core);
    EXPECT(0u == snap.filter_purify_left);
    EXPECT(0u == snap.filter_humidify_left);

    /* 到期重置：500 ms 提示音，不算强制。 */
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FILTER_RESET, 0u));
    snap = snap_of(&core);
    EXPECT(APL_BEEP_FILTER_RESET == snap.beep);
    EXPECT(0u == snap.filter_reset_forced);
    EXPECT(1u == snap.filter_reset_seq);
    EXPECT(snap.filter_purify_left == (HAS_PURIFY ? 99u : 0u));
    EXPECT(snap.filter_humidify_left == (HAS_HUMIDIFY ? 99u : 0u));

    /* 未到期强制重置。待机也可以。 */
    apl_core_power_off(&core);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FILTER_RESET, 0u));
    snap = snap_of(&core);
    EXPECT(APL_BEEP_FILTER_FORCED == snap.beep);
    EXPECT(1u == snap.filter_reset_forced);
    EXPECT(2u == snap.filter_reset_seq);

    /* 只在运行中累计。 */
    core_seconds(&core, 2u);
#if APL_COMPILE_PURIFY
    EXPECT(0u == apl_core_filter_purify_get(&core));
#endif
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    core_seconds(&core, 2u);
#if APL_COMPILE_PURIFY
    EXPECT(2u == apl_core_filter_purify_get(&core));
#endif
#if APL_COMPILE_HUMIDIFY
    EXPECT(2u == apl_core_filter_humidify_get(&core));
#endif
}

static void test_purify(void)
{
    struct apl_core core;
    struct host host;
    struct apl_link_snapshot snap;

    if (!HAS_PURIFY)
    {
        return;
    }

    core_start(&core, &host);
    (void)cmd(&core, APL_LINK_CMD_PURIFY_TOGGLE, 0u);   /* 待机中也可切换 */
    apl_core_tick(&core);
    EXPECT(APL_SWITCH_OFF == host.uv);
    (void)cmd(&core, APL_LINK_CMD_POWER, 0u);
    apl_core_tick(&core);
    EXPECT(APL_SWITCH_ON == host.uv);
    EXPECT(APL_SWITCH_ON == host.anion);

    /* 联锁断开：立即关 UV 并报警，负离子不受影响。 */
    host.interlock = 0u;
    apl_core_tick(&core);
    snap = snap_of(&core);
    EXPECT(APL_SWITCH_OFF == host.uv);
    EXPECT(APL_SWITCH_ON == host.anion);
    EXPECT(1u == snap.uv_fault);
    EXPECT(APL_BEEP_UV_ALARM == snap.beep);
    host.interlock = 1u;
    apl_core_tick(&core);
    EXPECT(APL_SWITCH_OFF == host.uv);

    (void)cmd(&core, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
    (void)cmd(&core, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
    apl_core_tick(&core);
    EXPECT(APL_SWITCH_ON == host.uv);
    EXPECT(0u == snap_of(&core).uv_fault);

    /* 空气质量取最差一项，自动档跟随。 */
    g_core_ops.pm10_get = pm10_get;
    g_core_ops.hcho_get = hcho_get;
    (void)cmd(&core, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO);
    host.pm25 = 35u;
    core_seconds(&core, 1u);
    EXPECT(APL_AQ_EXCELLENT == snap_of(&core).aq_level);
    host.pm25 = 36u;
    core_seconds(&core, 1u);
    EXPECT(APL_AQ_GOOD == snap_of(&core).aq_level);
    EXPECT(36u == snap_of(&core).pm25);
    if (!HAS_HUMIDIFY)
    {
        EXPECT(APL_FAN_GEAR_2 == host.fan_gear);
    }
    host.hcho = 51u;
    core_seconds(&core, 1u);
    EXPECT(APL_AQ_MODERATE == snap_of(&core).aq_level);
    host.pm10 = 151u;
    core_seconds(&core, 1u);
    EXPECT(APL_AQ_SEVERE == snap_of(&core).aq_level);
    EXPECT(APL_FAN_GEAR_3 == host.fan_gear);

    apl_core_power_off(&core);
    apl_core_tick(&core);
    EXPECT(APL_SWITCH_OFF == host.uv);
    EXPECT(APL_SWITCH_OFF == host.anion);
}

static void test_factory_wifi(void)
{
    struct apl_core core;
    struct host host;
    struct apl_core_memory memory;
    struct apl_link_snapshot snap;

    core_start(&core, &host);
    memset(&memory, 0, sizeof(memory));
    memory.filter_purify_s = 100u;
    memory.filter_humidify_s = 100u;
    memory.used = 1u;
    memory.fan_gear = APL_FAN_GEAR_3;
    apl_core_memory_restore(&core, &memory);

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FACTORY_ENTER, 0u));
    snap = snap_of(&core);
    EXPECT(APL_RUN_FACTORY == snap.run_state);
    EXPECT(APL_BEEP_FACTORY == snap.beep);
    EXPECT(1u == host.memory.factory_pending);
    EXPECT(100u == host.memory.filter_purify_s);   /* 保存的是真实值 */
    core_seconds(&core, APL_CORE_FACTORY_FILTER_LEFT_S);
    snap = snap_of(&core);
    EXPECT(0u == snap.filter_purify_left);
    EXPECT(0u == snap.filter_humidify_left);

    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_FACTORY_EXIT, 0u));
    snap = snap_of(&core);
    EXPECT(APL_RUN_STANDBY == snap.run_state);
    EXPECT(1u == snap.factory_done);
    EXPECT(0u == host.memory.factory_pending);
    EXPECT(0u == host.memory.used);
    EXPECT(APL_FAN_GEAR_1 == snap.fan_gear);
    EXPECT(snap.filter_purify_left == (HAS_PURIFY ? 99u : 0u));
#if APL_COMPILE_PURIFY
    EXPECT(100u == apl_core_filter_purify_get(&core));
#endif
    core_seconds(&core, APL_CORE_FACTORY_CL_S);
    EXPECT(0u == snap_of(&core).factory_done);

    /* 上电 30 秒后不能进产测。 */
    core_start(&core, &host);
    core_seconds(&core, APL_CORE_FACTORY_WINDOW_S);
    EXPECT(APL_LINK_REJECTED == cmd(&core, APL_LINK_CMD_FACTORY_ENTER, 0u));

    /* 异常断电：上电先完成恢复出厂设置。 */
    core_start(&core, &host);
    memory.factory_pending = 1u;
    memory.running = 1u;
    apl_core_memory_restore(&core, &memory);
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&core));
    EXPECT(1u == snap_of(&core).factory_done);
    EXPECT(0u == host.memory.factory_pending);

    /* 配网。 */
    core_start(&core, &host);
    EXPECT(APL_LINK_OK == cmd(&core, APL_LINK_CMD_WIFI_PAIR, 0u));
    EXPECT(APL_WIFI_PAIRING == snap_of(&core).wifi);
    core_seconds(&core, APL_CORE_WIFI_PAIR_S);
    EXPECT(APL_WIFI_OFF == snap_of(&core).wifi);
    (void)cmd(&core, APL_LINK_CMD_WIFI_PAIR, 0u);
    apl_core_wifi_connected_set(&core, 1u);
    EXPECT(APL_WIFI_CONNECTED == snap_of(&core).wifi);
    apl_core_wifi_connected_set(&core, 7u);
    EXPECT(APL_WIFI_CONNECTED == snap_of(&core).wifi);
}

/* ---------- 面板 ---------- */

static void machine_ticks(struct apl *machine, uint32_t count)
{
    uint32_t index;

    for (index = 0u; index < count; index++)
    {
        apl_tick(machine);
    }
}

static enum apl_hmi_key_level *key_of(struct host *host, enum apl_hmi_key_id key)
{
    switch (key)
    {
    case APL_HMI_KEY_PURIFY:
        return &host->keys.purify;
    case APL_HMI_KEY_HUMIDITY:
        return &host->keys.humidity;
    case APL_HMI_KEY_TIMER:
        return &host->keys.timer;
    case APL_HMI_KEY_LIGHT:
        return &host->keys.light;
    case APL_HMI_KEY_SPEED:
        return &host->keys.speed;
    default:
        return &host->keys.power;
    }
}

static void press(struct apl *machine, struct host *host, enum apl_hmi_key_id key, uint32_t ticks)
{
    *key_of(host, key) = APL_HMI_KEY_PRESSED;
    machine_ticks(machine, ticks);
    *key_of(host, key) = APL_HMI_KEY_RELEASED;
    machine_ticks(machine, 1u);
}

#define SHORT_PRESS  5u
#define LONG_PRESS   (APL_HMI_LONG_PRESS_TICKS + 1u)

static void machine_start(struct apl *machine, struct host *host)
{
    host_reset(host);
    apl_init(machine, &g_core_ops, host, &g_hmi_ops, host);
    apl_link_bind(machine);
    machine_ticks(machine, APL_HMI_POWERUP_LAMP_TICKS + 1u);
}

static void test_panel(void)
{
    struct apl machine;
    struct host host;
    char digits[APL_HMI_DIGIT_COUNT];
    struct apl_link_snapshot snap;

    machine_start(&machine, &host);
    EXPECT(1 == host.buzzer_on_edges);   /* 上电一声 */
    EXPECT(APL_HMI_LAMP_HALF == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_POWER));
    EXPECT(0u == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_SPEED));

    press(&machine, &host, APL_HMI_KEY_POWER, SHORT_PRESS);
    EXPECT(APL_RUN_ON == apl_core_run_state_get(&machine.core));
    machine_ticks(&machine, TICKS_PER_S);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_POWER));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_GEAR_1));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_FILTER_WHITE));
    apl_hmi_digits_content_get(&machine.hmi, digits);
    if (HAS_PURIFY)
    {
        EXPECT(0 == memcmp(digits, " 20", 3));
        EXPECT(APL_HMI_UNIT_PM25 == apl_hmi_digits_unit_get(&machine.hmi));
        EXPECT(APL_HMI_LAMP_FULL == apl_hmi_ambient_brightness_get(&machine.hmi));
    }
    else
    {
        EXPECT(0 == memcmp(digits, " 50", 3));
        EXPECT(APL_HMI_UNIT_PERCENT == apl_hmi_digits_unit_get(&machine.hmi));
    }
    if (HAS_HUMIDIFY)
    {
        EXPECT(APL_HMI_LAMP_FULL == apl_hmi_water_lamp_get(&machine.hmi, 3u));
        EXPECT(0u == apl_hmi_water_lamp_get(&machine.hmi, 4u));
        EXPECT(APL_HMI_WATER_GREEN == apl_hmi_water_color_get(&machine.hmi));
    }
    else
    {
        EXPECT(0u == apl_hmi_water_lamp_get(&machine.hmi, 1u));
    }

    press(&machine, &host, APL_HMI_KEY_SPEED, SHORT_PRESS);
    EXPECT(APL_FAN_GEAR_2 == apl_core_fan_gear_get(&machine.core));
    press(&machine, &host, APL_HMI_KEY_SPEED, LONG_PRESS);
    EXPECT(APL_FAN_GEAR_TURBO == apl_core_fan_gear_get(&machine.core));
    press(&machine, &host, APL_HMI_KEY_SPEED, LONG_PRESS);
    EXPECT(APL_FAN_GEAR_2 == apl_core_fan_gear_get(&machine.core));

    /* 定时：按两下显示 2，H 闪，3 秒确认。 */
    press(&machine, &host, APL_HMI_KEY_TIMER, SHORT_PRESS);
    press(&machine, &host, APL_HMI_KEY_TIMER, SHORT_PRESS);
    apl_hmi_digits_content_get(&machine.hmi, digits);
    EXPECT(0 == memcmp(digits, "  2", 3));
    EXPECT(0u == apl_core_timer_get(&machine.core));
    machine_ticks(&machine, APL_HMI_IDLE_TICKS);
    EXPECT(7200u == apl_core_timer_get(&machine.core));
    machine_ticks(&machine, 1u);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_TIMER));

    /* 定时中再按：先显示剩余小时数。 */
    press(&machine, &host, APL_HMI_KEY_TIMER, SHORT_PRESS);
    apl_hmi_digits_content_get(&machine.hmi, digits);
    EXPECT(0 == memcmp(digits, "  2", 3));
    machine_ticks(&machine, APL_HMI_IDLE_TICKS + 1u);

    /* 童锁：定时键长按。锁住时其他键无效，定时键长按解锁。 */
    press(&machine, &host, APL_HMI_KEY_TIMER, LONG_PRESS);
    EXPECT(APL_HMI_CHILD_LOCK_ON == apl_hmi_child_lock_get(&machine.hmi));
    machine_ticks(&machine, 1u);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_LOCK));
    press(&machine, &host, APL_HMI_KEY_SPEED, SHORT_PRESS);
    EXPECT(APL_FAN_GEAR_2 == apl_core_fan_gear_get(&machine.core));
    press(&machine, &host, APL_HMI_KEY_TIMER, LONG_PRESS);
    EXPECT(APL_HMI_CHILD_LOCK_OFF == apl_hmi_child_lock_get(&machine.hmi));

    /* 灯光关：状态显示熄灭，键灯不受影响；第一次触摸只唤醒。 */
    press(&machine, &host, APL_HMI_KEY_LIGHT, SHORT_PRESS);
    EXPECT(0u == apl_hmi_light_get(&machine.hmi));
    machine_ticks(&machine, 1u);
    EXPECT(0u == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_GEAR_2));
    EXPECT(0u == apl_hmi_digits_brightness_get(&machine.hmi));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_POWER));
    press(&machine, &host, APL_HMI_KEY_SPEED, SHORT_PRESS);
    EXPECT(APL_FAN_GEAR_2 == apl_core_fan_gear_get(&machine.core));
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_GEAR_2));
    press(&machine, &host, APL_HMI_KEY_SPEED, SHORT_PRESS);
    EXPECT(APL_FAN_GEAR_3 == apl_core_fan_gear_get(&machine.core));
    machine_ticks(&machine, APL_HMI_IDLE_TICKS + 1u);
    EXPECT(0u == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_GEAR_3));
    press(&machine, &host, APL_HMI_KEY_LIGHT, SHORT_PRESS);   /* 唤醒 */
    press(&machine, &host, APL_HMI_KEY_LIGHT, SHORT_PRESS);   /* 开灯 */
    EXPECT(1u == apl_hmi_light_get(&machine.hmi));

    /* 睡眠：灯光键长按；3 秒无操作熄屏。 */
    press(&machine, &host, APL_HMI_KEY_LIGHT, LONG_PRESS);
    EXPECT(APL_FAN_GEAR_SLEEP == apl_core_fan_gear_get(&machine.core));
    machine_ticks(&machine, 2u);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_GEAR_SLEEP));
    machine_ticks(&machine, APL_HMI_IDLE_TICKS);
    EXPECT(0u == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_GEAR_SLEEP));
    press(&machine, &host, APL_HMI_KEY_LIGHT, LONG_PRESS);    /* 唤醒 */
    EXPECT(APL_FAN_GEAR_SLEEP == apl_core_fan_gear_get(&machine.core));
    press(&machine, &host, APL_HMI_KEY_LIGHT, LONG_PRESS);    /* 退出睡眠 */
    EXPECT(APL_FAN_GEAR_3 == apl_core_fan_gear_get(&machine.core));

    /* 滤网重置组合键：定时 + 风速 3 秒。 */
    host.keys.timer = APL_HMI_KEY_PRESSED;
    host.keys.speed = APL_HMI_KEY_PRESSED;
    machine_ticks(&machine, LONG_PRESS);
    host.keys.timer = APL_HMI_KEY_RELEASED;
    host.keys.speed = APL_HMI_KEY_RELEASED;
    machine_ticks(&machine, 2u);
    apl_core_snapshot_get(&machine.core, &snap);
    EXPECT(1u == snap.filter_reset_seq);
    EXPECT(1u == snap.filter_reset_forced);
    EXPECT(APL_FAN_GEAR_3 == snap.fan_gear);
    EXPECT(0u == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_FILTER_WHITE));
    machine_ticks(&machine, 60u);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_FILTER_WHITE));

    /* 加湿相关：一体机湿度键短按开关加湿，长按设恒湿；加湿器短按设恒湿。 */
    if (HAS_HUMIDIFY && HAS_PURIFY)
    {
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        EXPECT(0u == snap_of(&machine.core).humidify_on);
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        EXPECT(1u == snap_of(&machine.core).humidify_on);
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, LONG_PRESS);
        apl_hmi_digits_content_get(&machine.hmi, digits);
        EXPECT(0 == memcmp(digits, " OF", 3));
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        machine_ticks(&machine, APL_HMI_IDLE_TICKS);
        EXPECT(45u == snap_of(&machine.core).humidity_target);
        machine_ticks(&machine, 1u);
        EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_HUMIDITY));
    }
    else if (HAS_HUMIDIFY)
    {
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        apl_hmi_digits_content_get(&machine.hmi, digits);
        EXPECT(0 == memcmp(digits, " 45", 3));
        machine_ticks(&machine, APL_HMI_IDLE_TICKS);
        EXPECT(45u == snap_of(&machine.core).humidity_target);
    }
    else
    {
        /* 加湿未启用：湿度键短按不发出。 */
        press(&machine, &host, APL_HMI_KEY_HUMIDITY, SHORT_PRESS);
        EXPECT(0u == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_HUMIDITY));
    }

    /* 缺水显示：最左一格橙灯闪。 */
    if (HAS_HUMIDIFY)
    {
        host.water = APL_WATER_LEVEL_0;
        machine_ticks(&machine, 2u);
        EXPECT(APL_HMI_WATER_ORANGE == apl_hmi_water_color_get(&machine.hmi));
        EXPECT(APL_HMI_LAMP_FULL == apl_hmi_water_lamp_get(&machine.hmi, 1u));
        EXPECT(0u == apl_hmi_water_lamp_get(&machine.hmi, 2u));
        host.water = APL_WATER_LEVEL_3;
        machine_ticks(&machine, 2u);
    }

    /* 滤芯查询 3 秒。 */
    apl_hmi_filter_query(&machine.hmi);
    machine_ticks(&machine, 1u);
    apl_hmi_digits_content_get(&machine.hmi, digits);
    EXPECT(0 == memcmp(digits, " 99", 3));

    /* 关机：加湿启用先风干，只亮电源键和风干图标；长按电源键结束。 */
    press(&machine, &host, APL_HMI_KEY_POWER, SHORT_PRESS);
    machine_ticks(&machine, 1u);
    if (HAS_HUMIDIFY)
    {
        EXPECT(APL_RUN_DRY == apl_core_run_state_get(&machine.core));
        EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_DRY));
        EXPECT(0u == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_SPEED));
        EXPECT(0u == apl_hmi_digits_brightness_get(&machine.hmi));
        press(&machine, &host, APL_HMI_KEY_POWER, LONG_PRESS);
    }
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&machine.core));
    machine_ticks(&machine, 1u);
    EXPECT(APL_HMI_LAMP_HALF == apl_hmi_key_lamp_get(&machine.hmi, APL_HMI_KEY_POWER));
}

static void test_panel_factory(void)
{
    struct apl machine;
    struct host host;
    char digits[APL_HMI_DIGIT_COUNT];
    int edges;

    machine_start(&machine, &host);
    edges = host.buzzer_on_edges;
    host.keys.power = APL_HMI_KEY_PRESSED;
    host.keys.timer = APL_HMI_KEY_PRESSED;
    machine_ticks(&machine, LONG_PRESS);
    host.keys.power = APL_HMI_KEY_RELEASED;
    host.keys.timer = APL_HMI_KEY_RELEASED;
    machine_ticks(&machine, 2u);
    EXPECT(APL_RUN_FACTORY == apl_core_run_state_get(&machine.core));
    EXPECT(APL_HMI_CHILD_LOCK_OFF == apl_hmi_child_lock_get(&machine.hmi));
    EXPECT(edges + 1 == host.buzzer_on_edges);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&machine.hmi, APL_HMI_ICON_UV));

    press(&machine, &host, APL_HMI_KEY_POWER, SHORT_PRESS);
    EXPECT(APL_RUN_STANDBY == apl_core_run_state_get(&machine.core));
    machine_ticks(&machine, 1u);
    apl_hmi_digits_content_get(&machine.hmi, digits);
    EXPECT(0 == memcmp(digits, " CL", 3));
    machine_ticks(&machine, APL_CORE_FACTORY_CL_S * TICKS_PER_S + 1u);
    apl_hmi_digits_content_get(&machine.hmi, digits);
    EXPECT(0 == memcmp(digits, "   ", 3));
}

static void test_hmi_resources(void)
{
    struct apl_hmi hmi;
    struct host host;

    host_reset(&host);
    apl_hmi_init(&hmi, &g_hmi_ops, &host);
    EXPECT(APL_HMI_LAMP_FULL == apl_hmi_icon_get(&hmi, APL_HMI_ICON_WIFI));   /* 上电全亮 */
    apl_hmi_icon_set(&hmi, APL_HMI_ICON_COUNT, 9u);
    EXPECT(0u == apl_hmi_icon_get(&hmi, APL_HMI_ICON_COUNT));
    EXPECT(0u == apl_hmi_icon_get(NULL, APL_HMI_ICON_WIFI));
    apl_hmi_water_color_set(&hmi, APL_HMI_WATER_ORANGE);
    EXPECT(APL_HMI_WATER_ORANGE == apl_hmi_water_color_get(&hmi));
    apl_hmi_water_color_set(&hmi, (enum apl_hmi_water_color)7);
    EXPECT(APL_HMI_WATER_ORANGE == apl_hmi_water_color_get(&hmi));

    /* 提示音：3 声，100 ms 响 100 ms 停。 */
    apl_hmi_buzzer_play(&hmi, 3u, 100u, 100u);
    host.buzzer_on_edges = 0;
    while (0u != apl_hmi_buzzer_busy_get(&hmi))
    {
        apl_hmi_tick(&hmi);
    }
    EXPECT(3 == host.buzzer_on_edges);
    EXPECT(0u == host.buzzer);
    apl_hmi_buzzer_play(&hmi, 0u, 100u, 0u);
    EXPECT(0u == apl_hmi_buzzer_busy_get(&hmi));

    /* 未接面板逻辑时，快照不改启用标志，也不绘制。 */
    {
        struct apl_link_snapshot snap;

        memset(&snap, 0, sizeof(snap));
        apl_hmi_snapshot_set(&hmi, &snap);
        EXPECT(1u == apl_hmi_purify_enabled_get(&hmi));
    }
}

int main(void)
{
    test_reject();
    test_power_and_dry();
    test_memory();
    test_timer_turbo_sleep();
    test_humidity();
    test_water();
    test_filter();
    test_purify();
    test_factory_wifi();
    test_panel();
    test_panel_factory();
    test_hmi_resources();

    if (0 != g_failed)
    {
        printf("%d failed\n", g_failed);
        return 1;
    }

    printf("ok\n");
    return 0;
}
