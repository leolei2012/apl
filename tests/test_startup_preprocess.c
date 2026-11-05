#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host { unsigned pm_reads; unsigned rh_reads; enum apl_sensor_status status; };
static uint16_t pm(void *ctx) APL_CALL { ((struct host *)ctx)->pm_reads++; return 123u; }
static uint8_t rh(void *ctx) APL_CALL { ((struct host *)ctx)->rh_reads++; return 45u; }
static enum apl_water_level water(void *ctx) APL_CALL { APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static enum apl_sensor_status status(enum apl_sensor sensor, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(sensor); return ((struct host *)ctx)->status; }
static const struct apl_core_ops ops = { .get_pm25 = pm, .get_humidity = rh, .get_water_level = water, .get_sensor_status = status };
static void ticks(struct apl *self, unsigned n) { while (n-- != 0u) apl_tick(self); }
static struct apl_link_snapshot snapshot(struct apl *self) { struct apl_link_snapshot s; apl_core_get_snapshot(&self->core, &s); return s; }
static void bar(struct apl *self, char expected, uint8_t mask)
{
    char text[3]; unsigned i;
    apl_hmi_get_digits_content(&self->hmi, text);
    for (i = 0; i < 3u; i++) { assert(text[i] == expected); assert(apl_hmi_get_digit_segments(&self->hmi, (uint8_t)i) == mask); }
    assert(apl_hmi_get_digits_unit(&self->hmi) == APL_HMI_UNIT_NONE);
}
static void boot(struct apl *self, struct host *host)
{
    memset(host, 0, sizeof(*host)); host->status = APL_SENSOR_VALID;
    apl_init(self, &ops, host, NULL, NULL); apl_link_bind(self);
}
int main(void)
{
    struct apl self; struct host host; struct apl_core_memory memory;
    unsigned i; char text[3]; uint8_t expected;
    expected = (APL_COMPILE_SENSOR_PM25 ? APL_SENSOR_PM25_MASK : 0u)
               | (APL_COMPILE_SENSOR_HUMIDITY ? APL_SENSOR_HUMIDITY_MASK : 0u);
    boot(&self, &host);
    ticks(&self, 199u);
    assert(apl_hmi_get_powerup_lamp(&self.hmi));
    apl_hmi_get_digits_content(&self.hmi, text); assert(!memcmp(text, "888", 3));
    ticks(&self, 1u);
    assert(!apl_hmi_get_powerup_lamp(&self.hmi));
    assert(apl_hmi_get_key_lamp(&self.hmi, APL_HMI_KEY_POWER) != 0u);
    assert(apl_hmi_get_digits_brightness(&self.hmi) == 0u);
    /* Starting at a non-second uptime boundary must still take exactly 500 ticks. */
    ticks(&self, 137u);
    apl_core_power_on(&self.core);
    ticks(&self, 30u); bar(&self, '^', 0x01u);
    ticks(&self, 1u); bar(&self, '-', 0x40u);
    ticks(&self, 30u); bar(&self, '_', 0x08u);
    ticks(&self, 30u); bar(&self, '^', 0x01u);
    ticks(&self, 408u); /* elapsed4990ms */
    assert((snapshot(&self).sensors_warming & expected) == expected);
    assert((snapshot(&self).sensors_ready & expected) == 0u);
    assert(host.pm_reads == 0u && host.rh_reads == 0u);
    assert(apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_1) == APL_HMI_LAMP_FULL);
    ticks(&self, 1u);
    assert((snapshot(&self).sensors_warming & expected) == 0u);
    assert((snapshot(&self).sensors_ready & expected) == expected);
    assert(host.pm_reads == (APL_COMPILE_SENSOR_PM25 ? 1u : 0u));
    assert(host.rh_reads == (APL_COMPILE_SENSOR_HUMIDITY ? 1u : 0u));
    apl_hmi_get_digits_content(&self.hmi, text);
    assert(!memcmp(text, APL_COMPILE_PURIFY ? "123" : " 45", 3));
    /* Driver warmup beyond 5s remains unavailable; a fault never becomes valid by timeout. */
    apl_core_power_off(&self.core); host.status = APL_SENSOR_WARMUP;
    apl_core_power_on(&self.core); ticks(&self, 600u);
    assert((snapshot(&self).sensors_warming & expected) == expected);
    host.status = APL_SENSOR_FAULT; ticks(&self, 100u);
    assert((snapshot(&self).sensor_faults & expected) == expected);
    assert((snapshot(&self).sensors_ready & expected) == 0u);
    host.status = APL_SENSOR_VALID; ticks(&self, 100u);
    assert((snapshot(&self).sensors_ready & expected) == expected);
    /* Restored running mode appears at exactly2s, while the initial5s gate is still active. */
    apl_core_get_memory(&self.core, &memory);
    memory.running = 1u; memory.used = 1u; memory.fan_gear = APL_FAN_GEAR_2;
    boot(&self, &host); apl_core_memory_restore(&self.core, &memory);
    ticks(&self, 199u); assert(apl_hmi_get_powerup_lamp(&self.hmi));
    ticks(&self, 1u); assert(!apl_hmi_get_powerup_lamp(&self.hmi));
    assert(apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_2) == APL_HMI_LAMP_FULL);
    bar(&self, '^', 0x01u);
    ticks(&self, 299u); assert((snapshot(&self).sensors_ready & expected) == 0u);
    ticks(&self, 1u); assert((snapshot(&self).sensors_ready & expected) == expected);
    /* Standard glyph-to-segment output for physical drivers. */
    apl_hmi_set_digits_content(&self.hmi, "180");
    for (i = 0u; i < 3u; i++) assert(apl_hmi_get_digit_segments(&self.hmi, (uint8_t)i) == (i == 0u ? 0x06u : i == 1u ? 0x7Fu : 0x3Fu));
    assert(apl_hmi_get_digit_segments(NULL, 0u) == 0u);
    assert(apl_hmi_get_digit_segments(&self.hmi, 3u) == 0u);
    puts("PASS: exact2s self-test, restored mode, exact5s sensor readiness, synchronized three-digit top/middle/bottom bars, fault priority and segment getter");
    return 0;
}
