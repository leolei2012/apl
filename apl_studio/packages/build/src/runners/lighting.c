#include "product.h"
static SIM_PRODUCT_t device;
static int actual_light, actual_brightness;
static void output(bool on, uint8_t brightness, void *ctx)
{
    (void)ctx;
    if (actual_light != (int)on) { actual_light = (int)on; sim_event("output", "light", actual_light); }
    if (actual_brightness != (int)brightness) { actual_brightness = (int)brightness; sim_event("output", "brightness", actual_brightness); }
}
static void sim_boot(void) { SIM_PRODUCT_init(&device, output, NULL); }
static void sim_drop(void) { output(false, 0u, NULL); }
static void sim_clear_memory(void) { }
static void sim_step(void) { SIM_PRODUCT_tick(&device); }
static void sim_input(int key, int value)
{
    if (!powered) return;
    if (key == 200) (void)SIM_PRODUCT_set_power(&device, value != 0);
    if (key == 201) (void)SIM_PRODUCT_set_brightness(&device, value);
}
static void sim_snapshot(void)
{
    printf("\"values\":{\"power\":%s,\"brightness\":%d},", powered && SIM_PRODUCT_get_power(&device) ? "true" : "false", (int)SIM_PRODUCT_get_brightness(&device));
    printf("\"outputs\":{\"light\":%d,\"brightness\":%d},\"display\":{},", actual_light, actual_brightness);
    printf("\"internal\":{\"power\":%d,\"brightness\":%d,\"elapsed_ms\":%u},\"memory\":{}", powered && SIM_PRODUCT_get_power(&device), (int)SIM_PRODUCT_get_brightness(&device), powered ? (unsigned)device.elapsed_ms : 0u);
}
