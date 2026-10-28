#include "light_port.h"

static {{API_PREFIX}}_t light;
void product_app_init(void)
{
    {{API_PREFIX}}_board_init(&light);
}
/* Call once every LIGHT_TICK_MS (10 ms), from the application task. */
void product_app_tick(void)
{
    {{API_PREFIX}}_tick(&light);
}
