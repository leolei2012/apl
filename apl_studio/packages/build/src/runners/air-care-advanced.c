#include "apl_product.h"
static SIM_PRODUCT_t device;
static struct apl_hmi_key_state keys;
static int input_humidity = 45, input_pm25 = 12, input_water = 3, input_interlock = 1;
static int input_pm10 = 10, input_hcho = 10, input_tvoc = 10;
static int sensor_statuses[5] = {2, 2, 2, 2, 2};
static unsigned module_faults;
static uint16_t pm10(void *ctx) APL_CALL { (void)ctx; return (uint16_t)input_pm10; }
static uint16_t hcho(void *ctx) APL_CALL { (void)ctx; return (uint16_t)input_hcho; }
static uint16_t tvoc(void *ctx) APL_CALL { (void)ctx; return (uint16_t)input_tvoc; }
static enum apl_sensor_status sensor_status(enum apl_sensor sensor, void *ctx) APL_CALL { (void)ctx; return (enum apl_sensor_status)sensor_statuses[sensor]; }
static uint8_t module_fault(enum apl_module module, void *ctx) APL_CALL { (void)ctx; return (uint8_t)(module_faults & (1u << (unsigned)module)); }
static int actual_fan, actual_gear, actual_pump, actual_uv, actual_anion, actual_buzzer;
#if defined(APL_STUDIO_AMBIENT_RGB)
static uint8_t actual_ambient_red, actual_ambient_green, actual_ambient_blue;
static void ambient_rgb(uint8_t red, uint8_t green, uint8_t blue, void *ctx) APL_CALL
{
    (void)ctx;
    actual_ambient_red = red; actual_ambient_green = green; actual_ambient_blue = blue;
}
#endif
#if defined(APL_STUDIO_FAN_RPM)
static unsigned actual_fan_target_rpm;
#endif
static struct apl_core_memory memory;
static int memory_valid, restoring;
static enum apl_water_level water(void *ctx) APL_CALL { (void)ctx; return (enum apl_water_level)input_water; }
static uint16_t pm25(void *ctx) APL_CALL { (void)ctx; return (uint16_t)input_pm25; }
static uint8_t humidity(void *ctx) APL_CALL { (void)ctx; return (uint8_t)input_humidity; }
static uint8_t interlock(void *ctx) APL_CALL { (void)ctx; return (uint8_t)input_interlock; }
static void key_state(struct apl_hmi_key_state *state, void *ctx) APL_CALL { (void)ctx; *state = keys; }
static void key_event(enum apl_hmi_key_id key, uint8_t is_long, void *ctx) APL_CALL
{
    static const char *names[] = { "Purify", "Humidity", "Timer", "Light", "Speed", "Power", "Filter" };
    (void)ctx;
    if ((unsigned)key < 7u) sim_event("input", names[key], is_long);
}
static void fan(enum apl_switch on, enum apl_fan_gear gear, void *ctx) APL_CALL
{
    (void)ctx;
    if (actual_fan != (int)on) { actual_fan = (int)on; sim_event("output", "fan", actual_fan); }
    if (actual_gear != (int)gear) { actual_gear = (int)gear; sim_event("output", "fan_gear", actual_gear); }
#if defined(APL_STUDIO_FAN_RPM)
    {
        unsigned rpm = (powered && on == APL_SWITCH_ON) ? apl_core_get_fan_target_rpm(&device.core) : 0u;
        if (actual_fan_target_rpm != rpm) { actual_fan_target_rpm = rpm; sim_event("output", "fan_target_rpm", (int)rpm); }
    }
#endif
}
#define OUTPUT_CALLBACK(name) \
static void name(enum apl_switch on, void *ctx) APL_CALL { \
    (void)ctx; if (actual_##name != (int)on) { actual_##name = (int)on; sim_event("output", #name, actual_##name); } \
}
OUTPUT_CALLBACK(pump)
OUTPUT_CALLBACK(uv)
OUTPUT_CALLBACK(anion)
static void buzzer(uint8_t on, void *ctx) APL_CALL
{
    (void)ctx;
    if (actual_buzzer != (int)on) { actual_buzzer = (int)on; sim_event("output", "buzzer", actual_buzzer); }
}
static void save_memory(const struct apl_core_memory *value, void *ctx) APL_CALL
{
    (void)ctx;
    if (restoring) return;
    memory = *value; memory_valid = 1;
}
static void sim_boot(void) APL_CALL
{
    static const struct apl_core_ops core = { .get_water_level = water, .get_pm25 = pm25, .get_humidity = humidity,
        .get_uv_interlock = interlock, .set_fan = fan, .set_pump = pump, .set_uv = uv, .set_anion = anion, .memory_save = save_memory, .get_pm10 = pm10, .get_hcho = hcho, .get_tvoc = tvoc, .get_module_fault = module_fault, .get_sensor_status = sensor_status };
    static const struct apl_hmi_ops hmi = { .get_key_state = key_state, .set_buzzer = buzzer, .key_event = key_event
#if defined(APL_STUDIO_AMBIENT_RGB)
        , .set_ambient_rgb = ambient_rgb
#endif
    };
    memset(&keys, 0, sizeof(keys));
    restoring = 1;
    SIM_PRODUCT_init(&device, &core, NULL, &hmi, NULL);
#if !defined(APL_COMPILE_MEMORY) || APL_COMPILE_MEMORY
    if (memory_valid) apl_core_memory_restore(&device.core, &memory);
#endif
    restoring = 0;
}
static void sim_drop(void) APL_CALL
{
    memset(&keys, 0, sizeof(keys));
    fan(APL_SWITCH_OFF, APL_FAN_GEAR_SLEEP, NULL); pump(APL_SWITCH_OFF, NULL);
    uv(APL_SWITCH_OFF, NULL); anion(APL_SWITCH_OFF, NULL); buzzer(0u, NULL);
#if defined(APL_STUDIO_AMBIENT_RGB)
    ambient_rgb(0u, 0u, 0u, NULL);
#endif
}
static void sim_clear_memory(void) APL_CALL { memset(&memory, 0, sizeof(memory)); memory_valid = 0; }
static void sim_step(void) APL_CALL { SIM_PRODUCT_tick(&device); }
static void sim_input(int key, int value) APL_CALL
{
    enum apl_hmi_key_level *level = NULL;
    if (key == 100) input_humidity = value;
    if (key == 101) input_pm25 = value;
    if (key == 102) input_water = value;
    if (key == 103) input_interlock = value;
    if (key == 130) sensor_statuses[APL_SENSOR_PM25] = value;
    if (key == 131) sensor_statuses[APL_SENSOR_HUMIDITY] = value;
    if (key == 132) input_pm10 = value;
    if (key == 133) input_hcho = value;
    if (key == 134) input_tvoc = value;
    if (key == 135) sensor_statuses[APL_SENSOR_PM10] = value;
    if (key == 136) sensor_statuses[APL_SENSOR_HCHO] = value;
    if (key == 137) sensor_statuses[APL_SENSOR_TVOC] = value;
    if (key >= 120 && key <= 122) {
        unsigned mask = 1u << (unsigned)(key - 120);
        if (value) module_faults |= mask; else module_faults &= ~mask;
    }
    if (!powered) return;
#if APL_COMPILE_UV
    if (key == 110) (void)SIM_PRODUCT_set_uv(&device, value != 0);
#endif
#if APL_COMPILE_ANION
    if (key == 111) (void)SIM_PRODUCT_set_anion(&device, value != 0);
#endif
#if !defined(APL_COMPILE_WIFI) || APL_COMPILE_WIFI
    if (key == 112) apl_core_set_wifi_connected(&device.core, (uint8_t)(value != 0));
#endif
    if (key == 104 || key == 105) {
        keys.timer = APL_HMI_KEY_PRESSED;
        if (key == 104) keys.speed = APL_HMI_KEY_PRESSED; else keys.power = APL_HMI_KEY_PRESSED;
        run_ticks((value ? 3050 : 50) / SIM_TICK_MS); memset(&keys, 0, sizeof(keys)); run_ticks(20 / SIM_TICK_MS); return;
    }
    switch (key) {
    case 0: level = &keys.purify; break;
    case 1: level = &keys.humidity; break;
    case 2: level = &keys.timer; break;
    case 3: level = &keys.light; break;
    case 4: level = &keys.speed; break;
    case 5: level = &keys.power; break;
    case 6: level = &keys.filter; break;
    default: break;
    }
    if (level != NULL) {
        *level = APL_HMI_KEY_PRESSED;
        run_ticks((value ? 3050 : 50) / SIM_TICK_MS);
        *level = APL_HMI_KEY_RELEASED;
        run_ticks(20 / SIM_TICK_MS);
    }
}
static unsigned sim_ambient_brightness(void) {
#if !defined(APL_HMI_COMPILE_AMBIENT) || APL_HMI_COMPILE_AMBIENT
    return apl_hmi_get_ambient_brightness(&device.hmi);
#else
    return 0u;
#endif
}
static void sim_snapshot(void) APL_CALL
{
    struct apl_link_snapshot s = {0};
    char digits[4] = {0};
    unsigned water_lamps = 0u;
    uint8_t red = 0u, green = 0u, blue = 0u;
    if (powered) { apl_core_get_snapshot(&device.core, &s); apl_hmi_get_digits_content(&device.hmi, digits); }
#if defined(APL_STUDIO_AMBIENT_RGB)
    if (powered) { red = actual_ambient_red; green = actual_ambient_green; blue = actual_ambient_blue; }
#else
    if (powered) {
        unsigned brightness = sim_ambient_brightness();
#if !defined(APL_HMI_COMPILE_AMBIENT) || APL_HMI_COMPILE_AMBIENT
        apl_hmi_get_ambient_color(&device.hmi, &red, &green, &blue);
#endif
        red = (uint8_t)((red * brightness + 127u) / 255u);
        green = (uint8_t)((green * brightness + 127u) / 255u);
        blue = (uint8_t)((blue * brightness + 127u) / 255u);
    }
#endif
    printf("\"values\":{\"humidity\":%d,\"pm25\":%d,\"water_level\":%d,\"uv_interlock\":%s,", input_humidity, input_pm25, input_water, input_interlock ? "true" : "false");
    printf("\"pm10\":%d,\"hcho\":%d,\"tvoc\":%d,\"pm25_status\":%d,\"humidity_status\":%d,\"pm10_status\":%d,\"hcho_status\":%d,\"tvoc_status\":%d,", input_pm10, input_hcho, input_tvoc, sensor_statuses[0], sensor_statuses[4], sensor_statuses[1], sensor_statuses[2], sensor_statuses[3]);
    printf("\"uv_fault_input\":%s,\"anion_fault_input\":%s,\"wifi_fault_input\":%s,\"wifi_connected\":%s,\"uv_request\":%s,\"anion_request\":%s},", (module_faults & 1u) ? "true" : "false", (module_faults & 2u) ? "true" : "false", (module_faults & 4u) ? "true" : "false", s.wifi == APL_WIFI_CONNECTED ? "true" : "false", s.uv_requested ? "true" : "false", s.anion_requested ? "true" : "false");
    printf("\"outputs\":{\"fan\":%d,\"fan_gear\":%d,\"pump\":%d,\"uv\":%d,\"anion\":%d,\"buzzer\":%d", actual_fan, actual_gear, actual_pump, actual_uv, actual_anion, actual_buzzer);
#if defined(APL_STUDIO_FAN_RPM)
    printf(",\"fan_target_rpm\":%u", actual_fan_target_rpm);
#endif
    printf("},");
    printf("\"display\":{\"digits\":\"\\u%04x\\u%04x\\u%04x\",\"unit\":\"%s\",", (unsigned)digits[0], (unsigned)digits[1], (unsigned)digits[2], powered ? (const char *[]) { "", "H", "%", "PM2.5" }[apl_hmi_get_digits_unit(&device.hmi)] : "");
    printf("\"brightness\":%u,\"light\":%u,\"child_lock\":%u,\"ambient\":%u,", powered ? apl_hmi_get_digits_brightness(&device.hmi) : 0u, powered ? apl_hmi_get_light(&device.hmi) : 0u, powered ? apl_hmi_get_child_lock(&device.hmi) : 0u, powered ? sim_ambient_brightness() : 0u);
    printf("\"ambient_red\":%u,\"ambient_green\":%u,\"ambient_blue\":%u,", (unsigned)red, (unsigned)green, (unsigned)blue);
    for (unsigned i = 0; i < APL_HMI_KEY_COUNT; ++i) printf("\"key_%u\":%u,", i, powered ? apl_hmi_get_key_lamp(&device.hmi, (enum apl_hmi_key_id)i) : 0u);
    for (unsigned i = 0; i < APL_HMI_ICON_COUNT; ++i) printf("\"icon_%u\":%u,", i, powered ? apl_hmi_get_icon(&device.hmi, (enum apl_hmi_icon)i) : 0u);
    for (unsigned i = 1u; i <= APL_HMI_WATER_LAMP_COUNT; ++i) {
        unsigned value = powered ? apl_hmi_get_water_lamp(&device.hmi, (uint8_t)i) : 0u;
        if (value) ++water_lamps;
        printf("\"water_%u\":%u,", i, value);
    }
    printf("\"water_level\":%u},", water_lamps);
    printf("\"internal\":{\"run_state\":%d,\"fan_speed\":%d,\"humidity_target\":%u,\"timer_left_s\":%u,\"dry_left_s\":%u,\"sleep\":%u,\"filter_reset_seq\":%u,\"sensor_ready\":%u,\"sensor_warming\":%u,\"sensor_faults\":%u,\"module_faults\":%u,\"uv_fault\":%u,\"aq_ready\":%u,\"wifi\":%d},", s.run_state, s.fan_gear, (unsigned)s.humidity_target, (unsigned)s.timer_left_s, (unsigned)s.dry_left_s, (unsigned)s.sleep, (unsigned)s.filter_reset_seq, (unsigned)s.sensors_ready, (unsigned)s.sensors_warming, (unsigned)s.sensor_faults, (unsigned)s.module_faults, (unsigned)s.uv_fault, (unsigned)s.aq_ready, s.wifi);
    printf("\"memory\":{\"valid\":%d,\"running\":%u,\"fan_gear\":%d,\"uv_requested\":%u,\"anion_requested\":%u}", memory_valid, (unsigned)memory.running, memory.fan_gear, (unsigned)memory.uv_requested, (unsigned)memory.anion_requested);
}
