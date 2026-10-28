#include "product.h"
#include "product_config.h"
#include <stddef.h>

static void render({{API_PREFIX}}_t *self)
{
    bool on = self->power;
#if LIGHT_BOOT_DELAY_MS > 0
    on = on && self->elapsed_ms >= (uint32_t)LIGHT_BOOT_DELAY_MS;
#endif
    if (self->output != NULL) self->output(on, on ? self->brightness : 0u, self->ctx);
}

void {{API_PREFIX}}_init({{API_PREFIX}}_t *self, {{API_PREFIX}}_output output, void *ctx)
{
    if (self == NULL) return;
    *self = ({{API_PREFIX}}_t){0};
    self->power = LIGHT_DEFAULT_POWER != 0;
    self->brightness = LIGHT_DEFAULT_BRIGHTNESS;
    self->output = output;
    self->ctx = ctx;
    render(self);
}

void {{API_PREFIX}}_tick({{API_PREFIX}}_t *self)
{
    if (self == NULL) return;
#if LIGHT_BOOT_DELAY_MS > 0
    if (self->power && self->elapsed_ms < (uint32_t)LIGHT_BOOT_DELAY_MS)
        self->elapsed_ms += LIGHT_TICK_MS;
#endif
    render(self);
}

bool {{API_PREFIX}}_get_power(const {{API_PREFIX}}_t *self)
{
    return self != NULL && self->power;
}

bool {{API_PREFIX}}_set_power({{API_PREFIX}}_t *self, bool value)
{
    if (self == NULL) return false;
    if (self->power != value) self->elapsed_ms = 0u;
    self->power = value;
    render(self);
    return true;
}

int32_t {{API_PREFIX}}_get_brightness(const {{API_PREFIX}}_t *self)
{
    return self == NULL ? 0 : self->brightness;
}

bool {{API_PREFIX}}_set_brightness({{API_PREFIX}}_t *self, int32_t value)
{
    if (self == NULL || value < 0 || value > 100) return false;
    self->brightness = (uint8_t)value;
    render(self);
    return true;
}
