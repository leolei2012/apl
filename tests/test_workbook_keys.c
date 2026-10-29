#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host
{
    struct apl_hmi_key_state keys;
};
static enum apl_water_level water(void *ctx)
{
    (void)ctx;
    return APL_WATER_LEVEL_3;
}
static uint8_t humidity(void *ctx)
{
    (void)ctx;
    return 45u;
}
static void keys(struct apl_hmi_key_state *state, void *ctx)
{
    *state = ((struct host *)ctx)->keys;
}
static const struct apl_core_ops core_ops = {.get_water_level = water, .get_humidity = humidity};
static const struct apl_hmi_ops hmi_ops = {.get_key_state = keys};
static void ticks(struct apl *self, unsigned n)
{
    while (n-- != 0u) { apl_tick(self); }
}
static void command(struct apl *self, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command c = {type, arg};
    assert(APL_LINK_OK == apl_core_command(&self->core, &c));
}
int main(void)
{
    struct apl self;
    struct host host;
    struct apl_link_snapshot before, after;
    memset(&host, 0, sizeof(host));
    apl_init(&self, &core_ops, &host, &hmi_ops, &host);
    apl_link_bind(&self);
    ticks(&self, 210u);
    command(&self, APL_LINK_CMD_POWER, 0u);
    ticks(&self, 110u);
    /* 按键说明E11：长按不能改变风速、灯光或加湿。 */
    apl_core_get_snapshot(&self.core, &before);
    host.keys.light = APL_HMI_KEY_PRESSED;
    ticks(&self, 310u);
    host.keys.light = APL_HMI_KEY_RELEASED;
    ticks(&self, 5u);
    apl_core_get_snapshot(&self.core, &after);
    assert(before.fan_gear == after.fan_gear);
    assert(1u == apl_hmi_get_light(&self.hmi));
    assert(before.humidify_on == after.humidify_on);
    /* 更改记录D23：非电源键先唤醒；电源键在再次熄屏后立即执行。 */
    command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_SLEEP);
    ticks(&self, 600u);
    host.keys.speed = APL_HMI_KEY_PRESSED;
    ticks(&self, 5u);
    host.keys.speed = APL_HMI_KEY_RELEASED;
    ticks(&self, 5u);
    assert(APL_FAN_GEAR_SLEEP == apl_core_get_fan_gear(&self.core));
    ticks(&self, 600u);
    host.keys.power = APL_HMI_KEY_PRESSED;
    ticks(&self, 5u);
    host.keys.power = APL_HMI_KEY_RELEASED;
    ticks(&self, 5u);
    assert(APL_RUN_ON != apl_core_get_run_state(&self.core));
#if APL_COMPILE_PURIFY && APL_COMPILE_HUMIDIFY && APL_CORE_HAS_TURBO
    apl_core_power_off(&self.core);
    command(&self, APL_LINK_CMD_POWER, 0u);
    ticks(&self, 110u);
    command(&self, APL_LINK_CMD_HUMIDITY_TARGET, 60u);
    command(&self, APL_LINK_CMD_TURBO_TOGGLE, 0u);
    ticks(&self, 1u);
    apl_core_get_snapshot(&self.core, &before);
    assert(APL_FAN_GEAR_TURBO == before.fan_gear);
    assert(APL_SWITCH_OFF == before.pump);
    command(&self, APL_LINK_CMD_HUMIDIFY_TOGGLE, 0u);
    ticks(&self, 1u);
    apl_core_get_snapshot(&self.core, &after);
    assert(APL_FAN_GEAR_AUTO == after.fan_gear);
    assert(before.humidify_on != after.humidify_on);
    assert(before.humidity_target == after.humidity_target);
#endif
    puts("PASS: workbook D23 power exception, E11 light long no-op and combined Turbo humidity key");
    return 0;
}
