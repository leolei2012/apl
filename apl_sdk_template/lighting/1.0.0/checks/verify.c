#include "product.h"
#include "product_config.h"
#include <assert.h>
#include <stdio.h>

struct load { bool on; uint8_t brightness; unsigned calls; };
static void output(bool on, uint8_t brightness, void *ctx)
{
    struct load *load = ctx;
    load->on = on;
    load->brightness = brightness;
    load->calls++;
}
int main(void)
{
    {{API_PREFIX}}_t self, other;
    struct load load = {0}, other_load = {0};
    {{API_PREFIX}}_init(&self, output, &load);
    {{API_PREFIX}}_init(&other, output, &other_load);
    assert({{API_PREFIX}}_get_power(&self) == (LIGHT_DEFAULT_POWER != 0));
    assert({{API_PREFIX}}_get_brightness(&self) == LIGHT_DEFAULT_BRIGHTNESS);
    assert({{API_PREFIX}}_set_power(&self, false));
    assert(!load.on && load.brightness == 0);
    assert({{API_PREFIX}}_set_brightness(&self, 61));
    assert(!{{API_PREFIX}}_set_brightness(&self, -1));
    assert(!{{API_PREFIX}}_set_brightness(&self, 101));
    assert({{API_PREFIX}}_get_brightness(&self) == 61);
    assert({{API_PREFIX}}_get_brightness(&other) == LIGHT_DEFAULT_BRIGHTNESS);
    assert({{API_PREFIX}}_set_power(&self, true));
    unsigned delay = (unsigned)LIGHT_BOOT_DELAY_MS / LIGHT_TICK_MS;
    for (unsigned i = 0; i < delay; i++) {
        assert(!load.on);
        /* An idempotent setter must not restart the delay. */
        assert({{API_PREFIX}}_set_power(&self, true));
        {{API_PREFIX}}_tick(&self);
    }
    assert(load.on && load.brightness == 61);
    assert({{API_PREFIX}}_set_power(&self, false));
    assert(!load.on && load.brightness == 0);
    assert(load.calls > 0);
    assert(!{{API_PREFIX}}_set_power(NULL, true));
    puts("Lighting defaults, delay, brightness bounds, output and instance isolation verified");
    return 0;
}
