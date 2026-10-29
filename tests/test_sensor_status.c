#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host
{
    enum apl_sensor_status status[5];
    unsigned pm_reads;
    unsigned rh_reads;
    uint8_t humidity;
};
static enum apl_water_level water(void *ctx) { (void)ctx; return APL_WATER_LEVEL_3; }
static uint16_t pm25(void *ctx) { ((struct host *)ctx)->pm_reads++; return 10u; }
static uint8_t humidity(void *ctx) { struct host *s = ctx; s->rh_reads++; return s->humidity; }
static enum apl_sensor_status status(enum apl_sensor sensor, void *ctx) { return ((struct host *)ctx)->status[sensor]; }
static const struct apl_core_ops ops = {.get_water_level = water, .get_pm25 = pm25, .get_humidity = humidity, .get_sensor_status = status};
static void ticks(struct apl *self, unsigned n) { while (n-- != 0u) { apl_tick(self); } }
static void command(struct apl *self, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command c = {type, arg}; assert(APL_LINK_OK == apl_core_command(&self->core, &c));
}
static struct apl_link_snapshot snap(struct apl *self) { struct apl_link_snapshot s; apl_core_get_snapshot(&self->core, &s); return s; }
int main(void)
{
    struct apl self; struct host host; unsigned i, pm_reads, rh_reads; uint8_t seq;
    char text[APL_HMI_DIGIT_COUNT]; struct apl_link_snapshot s;
    memset(&host, 0, sizeof(host)); host.humidity = 45u;
    for (i = 0u; i < 5u; i++) { host.status[i] = APL_SENSOR_WARMUP; }
    apl_init(&self, &ops, &host, NULL, NULL); apl_link_bind(&self); ticks(&self, 210u);
    command(&self, APL_LINK_CMD_POWER, 0u); command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO);
#if APL_COMPILE_HUMIDIFY
    command(&self, APL_LINK_CMD_HUMIDITY_TARGET, 60u);
#endif
    ticks(&self, 100u); s = snap(&self);
    assert((APL_DEFAULT_FAN_GEAR <= 3 ? APL_DEFAULT_FAN_GEAR : APL_CORE_SENSOR_FALLBACK_GEAR) == s.fan_output);
    assert(0u == host.pm_reads && 0u == host.rh_reads);
    apl_hmi_get_digits_content(&self.hmi, text); assert(0 == memcmp(text, " --", 3));
#if APL_COMPILE_HUMIDIFY
    assert(APL_SWITCH_OFF == s.pump);
#endif
    for (i = 0u; i < 5u; i++) { host.status[i] = APL_SENSOR_VALID; }
    ticks(&self, 100u); s = snap(&self);
    assert((APL_COMPILE_PURIFY ? 1u : 0u) == s.aq_ready);
    assert((APL_COMPILE_HUMIDIFY ? 2u : 0u) == s.fan_output);
    pm_reads = host.pm_reads; rh_reads = host.rh_reads;
    host.status[APL_SENSOR_PM25] = APL_SENSOR_FAULT; host.status[APL_SENSOR_HUMIDITY] = APL_SENSOR_FAULT;
    ticks(&self, 100u); s = snap(&self);
    assert(APL_CORE_SENSOR_FALLBACK_GEAR == s.fan_output);
    assert(pm_reads == host.pm_reads && rh_reads == host.rh_reads);
    assert((APL_COMPILE_PURIFY ? APL_SENSOR_PM25_MASK : 0u) + (APL_COMPILE_HUMIDIFY ? APL_SENSOR_HUMIDITY_MASK : 0u) == s.sensor_faults);
    apl_hmi_get_digits_content(&self.hmi, text);
    assert(0 == memcmp(text, APL_COMPILE_PURIFY ? " E9" : " --", 3));
    seq = s.beep_seq; ticks(&self, 200u); assert(seq == snap(&self).beep_seq);
    command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_3); ticks(&self, 1u); assert(APL_FAN_GEAR_3 == snap(&self).fan_output);
#if APL_COMPILE_HUMIDIFY
    assert(APL_SWITCH_OFF == snap(&self).pump);
    assert(60u == snap(&self).humidity_target);
#endif
    for (i = 0u; i < 5u; i++) { host.status[i] = APL_SENSOR_VALID; }
    command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO); ticks(&self, 100u);
    assert(0u == snap(&self).sensor_faults);
    assert((APL_COMPILE_HUMIDIFY ? 2u : 0u) == snap(&self).fan_output);
    assert(seq == snap(&self).beep_seq);
    command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_SLEEP); ticks(&self, 1u); seq = snap(&self).beep_seq;
    host.status[APL_SENSOR_PM25] = (enum apl_sensor_status)99;
    host.status[APL_SENSOR_HUMIDITY] = APL_SENSOR_FAULT; ticks(&self, 100u);
    assert(seq == snap(&self).beep_seq);
    assert(0u != snap(&self).sensor_faults);
#if APL_COMPILE_HUMIDIFY
    host.status[APL_SENSOR_HUMIDITY] = APL_SENSOR_VALID; host.humidity = 101u; ticks(&self, 100u);
    assert(0u != (snap(&self).sensor_faults & APL_SENSOR_HUMIDITY_MASK));
#endif
    apl_core_power_off(&self.core);
    for (i = 0u; i < 5u; i++) { host.status[i] = APL_SENSOR_WARMUP; }
    command(&self, APL_LINK_CMD_POWER, 0u);
    ticks(&self, 1u); assert(0u == snap(&self).sensors_ready);
    assert(0u == snap(&self).sensor_faults && 0u != snap(&self).sensors_warming);
    apl_hmi_get_digits_content(&self.hmi, text); assert(0 == memcmp(text, " --", 3));
    puts("PASS: preprocessing, no reads while invalid, configurable fallback, E9, no fake humidity, recovery and sleep silence");
    return 0;
}
