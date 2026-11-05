#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host {
    struct apl_hmi_key_state keys;
    uint16_t pm;
    enum apl_sensor_status status;
    uint8_t red, green, blue;
    unsigned calls;
};
static void rgb(uint8_t red, uint8_t green, uint8_t blue, void *ctx) APL_CALL
{
    struct host *host = ctx;
    host->red = red; host->green = green; host->blue = blue; host->calls++;
}
static void keys(struct apl_hmi_key_state *value, void *ctx) APL_CALL { *value = ((struct host *)ctx)->keys; }
static uint16_t pm(void *ctx) APL_CALL { return ((struct host *)ctx)->pm; }
static uint8_t rh(void *ctx) APL_CALL { APL_UNUSED_PARAM(ctx); return 45u; }
static enum apl_water_level water(void *ctx) APL_CALL { APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static enum apl_sensor_status status(enum apl_sensor sensor, void *ctx) APL_CALL { APL_UNUSED_PARAM(sensor); return ((struct host *)ctx)->status; }
static void ticks(struct apl *self, unsigned n) { while (n-- != 0u) apl_tick(self); }
static void expect(const struct host *host, uint8_t red, uint8_t green, uint8_t blue)
{ assert(host->red == red && host->green == green && host->blue == blue); }
int main(void)
{
    struct apl self;
    struct apl_hmi hmi;
    struct host host;
    struct apl_core_ops core_ops;
    struct apl_hmi_ops hmi_ops;
    unsigned calls, i;
    const uint16_t inputs[] = {35u, 36u, 75u, 76u, 150u, 151u, 12u};
    const uint8_t colors[][3] = {{0,0,255}, {0,255,0}, {0,255,0}, {255,255,0}, {255,255,0}, {255,0,0}, {0,0,255}};
    memset(&host, 0, sizeof(host)); host.pm = 12u; host.status = APL_SENSOR_VALID;
    memset(&core_ops, 0, sizeof(core_ops)); core_ops.get_pm25 = pm; core_ops.get_humidity = rh;
    core_ops.get_water_level = water; core_ops.get_sensor_status = status;
    memset(&hmi_ops, 0, sizeof(hmi_ops)); hmi_ops.get_key_state = keys; hmi_ops.set_ambient_rgb = rgb;
    apl_init(&self, &core_ops, &host, &hmi_ops, &host); apl_link_bind(&self);
    assert(host.calls == 0u); /* Init leaves physical output untouched; first tick synchronizes. */
    ticks(&self, 1u); expect(&host, 255u, 255u, 255u);
    calls = host.calls; ticks(&self, 198u); assert(host.calls == calls);
    ticks(&self, 1u); expect(&host, 0u, 0u, 0u);
    apl_core_power_on(&self.core); ticks(&self, 499u); expect(&host, 0u, 0u, 0u);
    ticks(&self, 1u);
    if (APL_COMPILE_PURIFY) expect(&host, 0u, 0u, 255u); else expect(&host, 0u, 0u, 0u);
    for (i = 0u; i < sizeof(inputs) / sizeof(inputs[0]); ++i)
    {
        host.pm = inputs[i]; ticks(&self, 100u);
        if (APL_COMPILE_PURIFY) expect(&host, colors[i][0], colors[i][1], colors[i][2]);
        else expect(&host, 0u, 0u, 0u);
        calls = host.calls; ticks(&self, 100u); assert(host.calls == calls);
    }
    host.status = APL_SENSOR_FAULT; ticks(&self, 100u); expect(&host, 0u, 0u, 0u);
    host.status = APL_SENSOR_VALID; ticks(&self, 100u);
    host.keys.light = APL_HMI_KEY_PRESSED; ticks(&self, 2u);
    host.keys.light = APL_HMI_KEY_RELEASED; ticks(&self, 2u); expect(&host, 0u, 0u, 0u);
    host.keys.speed = APL_HMI_KEY_PRESSED; ticks(&self, 2u);
    host.keys.speed = APL_HMI_KEY_RELEASED; ticks(&self, 2u);
    if (APL_COMPILE_PURIFY) expect(&host, 0u, 0u, 255u);
    ticks(&self, 301u); expect(&host, 0u, 0u, 0u);
    apl_core_power_off(&self.core); ticks(&self, 1u); expect(&host, 0u, 0u, 0u);
    /* Without the standard panel, a host may combine independent RGB and brightness setters. */
    apl_hmi_init(&hmi, &hmi_ops, &host);
    for (i = 0u; i < 200u; ++i) apl_hmi_tick(&hmi);
    apl_hmi_set_ambient_color(&hmi, 12u, 128u, 255u);
    apl_hmi_set_ambient_brightness(&hmi, 128u); apl_hmi_tick(&hmi); expect(&host, 6u, 64u, 128u);
    calls = host.calls; apl_hmi_tick(&hmi); assert(host.calls == calls);
    apl_hmi_set_ambient_brightness(&hmi, 0u); apl_hmi_tick(&hmi); expect(&host, 0u, 0u, 0u);
    apl_hmi_init(&hmi, NULL, NULL); apl_hmi_tick(&hmi); /* Optional callback remains optional. */
    puts("PASS: RGB host callback, first-tick sync, white self-test, four AQ colors, validity, light/wake/off, brightness scaling and change-only notification");
    return 0;
}
