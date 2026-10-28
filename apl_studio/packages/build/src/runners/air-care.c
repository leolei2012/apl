#include "apl_product.h"
static SIM_PRODUCT_t device;
static struct apl_hmi_key_state keys;
static int input_humidity = 45, input_pm25 = 12, input_water = 3, input_interlock = 1;
static int actual_fan, actual_gear, actual_pump, actual_uv, actual_anion, actual_buzzer;
static struct apl_core_memory memory;
static int memory_valid, restoring;
static enum apl_water_level water(void *ctx) { (void)ctx; return (enum apl_water_level)input_water; }
static uint16_t pm25(void *ctx) { (void)ctx; return (uint16_t)input_pm25; }
static uint8_t humidity(void *ctx) { (void)ctx; return (uint8_t)input_humidity; }
static uint8_t interlock(void *ctx) { (void)ctx; return (uint8_t)input_interlock; }
static void key_state(struct apl_hmi_key_state *state, void *ctx) { (void)ctx; *state = keys; }
static void key_event(enum apl_hmi_key_id key, uint8_t is_long, void *ctx)
{
    static const char *names[] = { "Purify", "Humidity", "Timer", "Light", "Speed", "Power" };
    (void)ctx;
    if ((unsigned)key < 6u) sim_event("input", names[key], is_long);
}
static void fan(enum apl_switch on, enum apl_fan_gear gear, void *ctx)
{
    (void)ctx;
    if (actual_fan != (int)on) { actual_fan = (int)on; sim_event("output", "fan", actual_fan); }
    if (actual_gear != (int)gear) { actual_gear = (int)gear; sim_event("output", "fan_gear", actual_gear); }
}
#define OUTPUT_CALLBACK(name) \
static void name(enum apl_switch on, void *ctx) { \
    (void)ctx; if (actual_##name != (int)on) { actual_##name = (int)on; sim_event("output", #name, actual_##name); } \
}
OUTPUT_CALLBACK(pump)
OUTPUT_CALLBACK(uv)
OUTPUT_CALLBACK(anion)
static void buzzer(uint8_t on, void *ctx)
{
    (void)ctx;
    if (actual_buzzer != (int)on) { actual_buzzer = (int)on; sim_event("output", "buzzer", actual_buzzer); }
}
static void save_memory(const struct apl_core_memory *value, void *ctx)
{
    (void)ctx;
    if (restoring) return;
    memory = *value; memory_valid = 1;
}
static void sim_boot(void)
{
    static const struct apl_core_ops core = { .get_water_level = water, .get_pm25 = pm25, .get_humidity = humidity,
        .get_uv_interlock = interlock, .set_fan = fan, .set_pump = pump, .set_uv = uv, .set_anion = anion, .memory_save = save_memory };
    static const struct apl_hmi_ops hmi = { .get_key_state = key_state, .set_buzzer = buzzer, .key_event = key_event };
    memset(&keys, 0, sizeof(keys));
    restoring = 1;
    SIM_PRODUCT_init(&device, &core, NULL, &hmi, NULL);
    if (memory_valid) apl_core_memory_restore(&device.core, &memory);
    restoring = 0;
}
static void sim_drop(void)
{
    memset(&keys, 0, sizeof(keys));
    fan(APL_SWITCH_OFF, APL_FAN_GEAR_SLEEP, NULL); pump(APL_SWITCH_OFF, NULL);
    uv(APL_SWITCH_OFF, NULL); anion(APL_SWITCH_OFF, NULL); buzzer(0u, NULL);
}
static void sim_clear_memory(void) { memset(&memory, 0, sizeof(memory)); memory_valid = 0; }
static void sim_step(void) { SIM_PRODUCT_tick(&device); }
static void sim_input(int key, int value)
{
    enum apl_hmi_key_level *level = NULL;
    if (key == 100) input_humidity = value;
    if (key == 101) input_pm25 = value;
    if (key == 102) input_water = value;
    if (key == 103) input_interlock = value;
    if (!powered) return;
    switch (key) {
    case 0: level = &keys.purify; break;
    case 1: level = &keys.humidity; break;
    case 2: level = &keys.timer; break;
    case 3: level = &keys.light; break;
    case 4: level = &keys.speed; break;
    case 5: level = &keys.power; break;
    default: break;
    }
    if (level != NULL) {
        *level = APL_HMI_KEY_PRESSED;
        run_ticks(value ? 305 : 5);
        *level = APL_HMI_KEY_RELEASED;
        run_ticks(2);
    }
}
static void sim_snapshot(void)
{
    struct apl_link_snapshot s = {0};
    char digits[4] = {0};
    if (powered) { apl_core_get_snapshot(&device.core, &s); apl_hmi_get_digits_content(&device.hmi, digits); }
    /* Digits are data from C; escape every byte instead of interpolating arbitrary text. */
    printf("\"values\":{\"humidity\":%d,\"pm25\":%d,\"water_level\":%d,\"uv_interlock\":%s},", input_humidity, input_pm25, input_water, input_interlock ? "true" : "false");
    printf("\"outputs\":{\"fan\":%d,\"fan_gear\":%d,\"pump\":%d,\"uv\":%d,\"anion\":%d,\"buzzer\":%d},", actual_fan, actual_gear, actual_pump, actual_uv, actual_anion, actual_buzzer);
    printf("\"display\":{\"digits\":\"\\u%04x\\u%04x\\u%04x\",\"unit\":\"%s\",", (unsigned char)digits[0], (unsigned char)digits[1], (unsigned char)digits[2], powered ? (const char *[]) { "", "H", "%", "PM2.5" }[apl_hmi_get_digits_unit(&device.hmi)] : "");
    printf("\"brightness\":%u,\"light\":%u,\"child_lock\":%u,\"ambient\":%u,\"powerup\":%u,", powered ? apl_hmi_get_digits_brightness(&device.hmi) : 0u, powered ? apl_hmi_get_light(&device.hmi) : 0u, powered ? apl_hmi_get_child_lock(&device.hmi) : 0u, powered ? apl_hmi_get_ambient_brightness(&device.hmi) : 0u, powered ? apl_hmi_get_powerup_lamp(&device.hmi) : 0u);
    unsigned water_lamps = 0;
    for (unsigned i = 1; i <= 4; ++i) if (powered && apl_hmi_get_water_lamp(&device.hmi, (uint8_t)i)) water_lamps++;
    printf("\"water_level\":%u", water_lamps);
    for (unsigned i = 0; i < 6; ++i) printf(",\"key_%u\":%u", i, powered ? apl_hmi_get_key_lamp(&device.hmi, (enum apl_hmi_key_id)i) : 0u);
    for (unsigned i = 0; i < APL_HMI_ICON_COUNT; ++i) printf(",\"icon_%u\":%u", i, powered ? apl_hmi_get_icon(&device.hmi, (enum apl_hmi_icon)i) : 0u);
    printf("},\"internal\":{\"run_state\":%d,\"fan_speed\":%d,\"purify\":%u,\"humidify\":%u,\"humidity\":%u,\"pm25\":%u,\"humidity_target\":%u,\"timer_left_s\":%u,\"dry_left_s\":%u,\"sleep\":%u,\"uv_fault\":%u,\"wifi\":%d,\"filter_purify_left\":%u,\"filter_humidify_left\":%u},", s.run_state, s.fan_gear, s.purify_on, s.humidify_on, s.humidity, s.pm25, s.humidity_target, (unsigned)s.timer_left_s, (unsigned)s.dry_left_s, s.sleep, s.uv_fault, s.wifi, s.filter_purify_left, s.filter_humidify_left);
    printf("\"memory\":{\"valid\":%d,\"running\":%u,\"fan_gear\":%d,\"purify_on\":%u,\"humidify_on\":%u,\"filter_purify_s\":%u,\"filter_humidify_s\":%u}", memory_valid, memory.running, memory.fan_gear, memory.purify_on, memory.humidify_on, (unsigned)memory.filter_purify_s, (unsigned)memory.filter_humidify_s);
}
