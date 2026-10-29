#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host
{
    enum apl_water_level water;
    enum apl_switch pump;
};
static enum apl_water_level water(void *ctx) { return ((struct host *)ctx)->water; }
static uint8_t humidity(void *ctx) { (void)ctx; return 45u; }
static void pump(enum apl_switch value, void *ctx) { ((struct host *)ctx)->pump = value; }
static const struct apl_core_ops ops = {.get_water_level = water, .get_humidity = humidity, .set_pump = pump};
static void ticks(struct apl_core *self, uint32_t n) { while (n-- != 0u) { apl_core_tick(self); } }
static void command(struct apl_core *self, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command c = {type, arg};
    assert(APL_LINK_OK == apl_core_command(self, &c));
}
static void start(struct apl_core *self, struct host *host, enum apl_water_level level)
{
    memset(host, 0, sizeof(*host));
    host->water = level;
    apl_core_init(self, &ops, host);
    ticks(self, 100u); /* 建立有效水位，再接收开机操作。 */
    command(self, APL_LINK_CMD_POWER, 0u);
    ticks(self, 100u);
}
int main(void)
{
    struct apl_core self;
    struct host host;
#if APL_COMPILE_HUMIDIFY
    struct apl_core other;
    struct host other_host;
    struct apl_core_memory memory;
    struct apl_link_snapshot snap;
    /* 无泵输出历史：两个机型均应直接待机。 */
    start(&self, &host, APL_WATER_LEVEL_0);
    assert(APL_SWITCH_OFF == host.pump);
    command(&self, APL_LINK_CMD_POWER, 0u);
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&self));
    /* 湿周期：泵已开启，随后关闭加湿也不能丢失风干需求。 */
    start(&self, &host, APL_WATER_LEVEL_3);
    assert(APL_SWITCH_ON == host.pump);
    command(&self, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u);
    ticks(&self, 1u);
    command(&self, APL_LINK_CMD_POWER, 0u);
    ticks(&self, 1u);
    assert(APL_RUN_DRY == apl_core_get_run_state(&self));
    assert(APL_SWITCH_OFF == host.pump);
    /* 风干中断掉电：恢复存储不恢复风干，需求不泄漏到新实例。 */
    apl_core_get_memory(&self, &memory);
    apl_core_init(&other, &ops, &host);
    apl_core_memory_restore(&other, &memory);
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&other));
    command(&self, APL_LINK_CMD_DRY_STOP, 0u);
    host.water = APL_WATER_LEVEL_0;
    command(&self, APL_LINK_CMD_POWER, 0u);
    ticks(&self, 100u);
    command(&self, APL_LINK_CMD_POWER, 0u);
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&self));
    /* 另一个实例从未运行泵，不得继承第一个实例的需求。 */
    start(&other, &other_host, APL_WATER_LEVEL_0);
    command(&other, APL_LINK_CMD_POWER, 0u);
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&other));
    /* 恢复窗口最后一秒仍运行；有效水位恢复后立即解除缺水。 */
    start(&self, &host, APL_WATER_LEVEL_3);
    host.water = APL_WATER_LEVEL_0;
    ticks(&self, 1u);
    ticks(&self, (uint32_t)(APL_CORE_WATER_SHORTAGE_RUN_S - 1u) * 100u);
    assert(APL_RUN_ON == apl_core_get_run_state(&self));
    host.water = APL_WATER_LEVEL_3;
    ticks(&self, 1u);
    assert(APL_SWITCH_ON == host.pump);
    apl_core_get_snapshot(&self, &snap);
    assert(APL_WATER_ALERT_NONE == snap.water_alert);
    /* 独立整秒边界：恢复窗口到点纯加湿转风干，一体保持净化。 */
    start(&self, &host, APL_WATER_LEVEL_3);
    command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_SLEEP);
    host.water = APL_WATER_LEVEL_0;
    ticks(&self, (uint32_t)APL_CORE_WATER_SHORTAGE_RUN_S * 100u);
    apl_core_get_snapshot(&self, &snap);
#if APL_COMPILE_PURIFY
    assert(APL_RUN_ON == snap.run_state);
    assert(APL_WATER_ALERT_STEADY == snap.water_alert);
    ticks(&self, 180000UL);
    assert(APL_RUN_ON == apl_core_get_run_state(&self));
#else
    assert(APL_RUN_DRY == snap.run_state);
    assert(APL_FAN_GEAR_SLEEP == snap.fan_output);
#endif
    assert(APL_SWITCH_OFF == host.pump);
    puts("PASS: workbook water recovery boundary, capability policy, per-run drying latch, clear/restart and instance isolation");
#else
    start(&self, &host, APL_WATER_LEVEL_0);
    ticks(&self, 180000UL);
    assert(APL_RUN_ON == apl_core_get_run_state(&self));
    command(&self, APL_LINK_CMD_POWER, 0u);
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&self));
    puts("PASS: pure purification ignores humidifier drying and water timeout");
#endif
    return 0;
}
