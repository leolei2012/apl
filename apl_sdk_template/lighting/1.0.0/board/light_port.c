#include "light_port.h"
#include <stddef.h>

static void set_light(bool on, uint8_t brightness, void *ctx)
{
    /* TODO: map 0..100 brightness to PWM duty and on to the output enable. */
    (void)on;
    (void)brightness;
    (void)ctx;
}

void {{API_PREFIX}}_board_init({{API_PREFIX}}_t *self)
{
    {{API_PREFIX}}_init(self, set_light, NULL);
}
