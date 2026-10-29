/** APL 0.1.1 SDK consumer. Frozen with the release from the root regression source. */
#include "apl.h"
#include <assert.h>
#include <stdio.h>

_Static_assert(APL_VERSION_NUM == 0x000101u, "APL release version");
_Static_assert(APL_CORE_VERSION_NUM == APL_VERSION_NUM, "core release version");
_Static_assert(APL_HMI_VERSION_NUM == APL_VERSION_NUM, "HMI release version");

struct host
{
    uint16_t pm25;
    enum apl_switch fan;
    enum apl_fan_gear gear;
};

static uint16_t pm25(void *ctx) { return ((struct host *)ctx)->pm25; }
static uint8_t humidity(void *ctx) { (void)ctx; return 45u; }
static enum apl_water_level water(void *ctx) { (void)ctx; return APL_WATER_LEVEL_3; }
static void fan(enum apl_switch on, enum apl_fan_gear gear, void *ctx)
{
    ((struct host *)ctx)->fan = on;
    ((struct host *)ctx)->gear = gear;
}
static const struct apl_core_ops ops = {
    .get_pm25 = pm25, .get_humidity = humidity, .get_water_level = water, .set_fan = fan
};
static void ticks(struct apl *self, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) apl_tick(self);
}
static void set_gear(struct apl *self, enum apl_fan_gear gear)
{
    const struct apl_link_command command = { APL_LINK_CMD_FAN_SET, (uint8_t)gear };
    assert(apl_core_command(&self->core, &command) == APL_LINK_OK);
}
static int verify_library(void)
{
    struct apl self;
    struct host host = { 12u, APL_SWITCH_OFF, APL_FAN_GEAR_1 };
    struct apl_link_snapshot snapshot;
    apl_init(&self, &ops, &host, NULL, NULL);
    apl_link_bind(&self);
    apl_core_power_on(&self.core);
    ticks(&self, 201u);
    assert(host.fan == APL_SWITCH_ON);

#if APL_COMPILE_PURIFY && !APL_COMPILE_HUMIDIFY
    static const uint16_t values[] = { 0u, 35u, 36u, 75u, 76u, 150u, 151u, UINT16_MAX, 35u };
    static const enum apl_fan_gear gears[] = {
        APL_FAN_GEAR_SLEEP, APL_FAN_GEAR_SLEEP, APL_FAN_GEAR_1, APL_FAN_GEAR_1,
        APL_FAN_GEAR_2, APL_FAN_GEAR_2, APL_FAN_GEAR_3, APL_FAN_GEAR_3, APL_FAN_GEAR_SLEEP
    };
    set_gear(&self, APL_FAN_GEAR_AUTO);
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        host.pm25 = values[i]; ticks(&self, 101u);
        apl_core_get_snapshot(&self.core, &snapshot);
        assert(host.gear == gears[i] && snapshot.fan_output == gears[i]);
        assert(snapshot.fan_gear == APL_FAN_GEAR_AUTO && snapshot.sleep == 0u);
        assert(host.fan == APL_SWITCH_ON);
        assert(apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_AUTO) == APL_HMI_LAMP_FULL);
        assert(apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_SLEEP) == 0u);
    }
    ticks(&self, APL_HMI_IDLE_TICKS + 1u);
    assert(apl_hmi_get_digits_brightness(&self.hmi) == APL_HMI_LAMP_FULL);
    puts("APL 0.1.1 pure auto boundaries and Auto/sleep semantics verified");
#endif

    set_gear(&self, APL_FAN_GEAR_2);
    host.pm25 = 0u; ticks(&self, 101u); assert(host.gear == APL_FAN_GEAR_2);
    host.pm25 = UINT16_MAX; ticks(&self, 101u); assert(host.gear == APL_FAN_GEAR_2);
    apl_core_get_snapshot(&self.core, &snapshot);
    assert(snapshot.sleep == 0u);
    puts("APL 0.1.1 C11 SDK consumer verified");
    return 0;
}

/** Product-manager profile: actual HMI long/short input, same configuration as SDK. */
#include <string.h>
_Static_assert(APL_COMPILE_PURIFY == 1u && APL_COMPILE_HUMIDIFY == 0u, "PM purification profile");
static void pm_keys(struct apl_hmi_key_state *state, void *ctx)
{
    *state = *(const struct apl_hmi_key_state *)ctx;
}
static void pm_ticks(struct apl *self, unsigned count)
{
    for (unsigned i = 0; i < count; i++) { apl_tick(self); }
}
int main(void)
{
    struct apl self;
    struct host host = {0};
    struct apl_hmi_key_state keys;
    const struct apl_core_ops core = { .get_pm25=pm25, .get_water_level=water, .set_fan=fan };
    const struct apl_hmi_ops hmi = { .get_key_state=pm_keys };
    assert(verify_library() == 0);
    memset(&keys, 0, sizeof(keys));
    apl_init(&self, &core, &host, &hmi, &keys);
    apl_link_bind(&self);
    pm_ticks(&self, 210u);
    apl_core_power_on(&self.core);
    apl_core_set_fan_gear(&self.core, APL_FAN_GEAR_1);
    pm_ticks(&self, 1u);
    keys.speed = APL_HMI_KEY_PRESSED;
    pm_ticks(&self, 310u);
    keys.speed = APL_HMI_KEY_RELEASED;
    pm_ticks(&self, 2u);
    assert(host.gear == (APL_CORE_HAS_TURBO ? APL_FAN_GEAR_TURBO : APL_FAN_GEAR_1));
    keys.speed = APL_HMI_KEY_PRESSED;
    pm_ticks(&self, 5u);
    keys.speed = APL_HMI_KEY_RELEASED;
    pm_ticks(&self, 2u);
    assert(host.gear == APL_FAN_GEAR_2);
    puts("PM interaction and Turbo option verified");
    return 0;
}
