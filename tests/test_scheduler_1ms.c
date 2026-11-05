#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host {
    struct apl_hmi_key_state keys;
    unsigned samples, short_presses, long_presses, rgb_calls, timer_calls;
    uint8_t red, green, blue;
};
static void keys(struct apl_hmi_key_state *value, void *ctx) APL_CALL
{ struct host *host = ctx; *value = host->keys; host->samples++; }
static void rgb(uint8_t r, uint8_t g, uint8_t b, void *ctx) APL_CALL
{ struct host *host = ctx; host->red = r; host->green = g; host->blue = b; host->rgb_calls++; }
static void short_press(struct apl_hmi *self, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(self); ((struct host *)ctx)->short_presses++; }
static void long_press(struct apl_hmi *self, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(self); ((struct host *)ctx)->long_presses++; }
static void timer_done(struct apl_core *self, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(self); ((struct host *)ctx)->timer_calls++; }
static uint16_t pm(void *ctx) APL_CALL { APL_UNUSED_PARAM(ctx); return 12u; }
static uint8_t rh(void *ctx) APL_CALL { APL_UNUSED_PARAM(ctx); return 45u; }
static enum apl_water_level water(void *ctx) APL_CALL
{ APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static const struct apl_core_ops core_ops = {
    .get_pm25 = pm, .get_humidity = rh, .get_water_level = water
};
static const struct apl_hmi_ops hmi_ops = { .get_key_state = keys, .set_ambient_rgb = rgb };
static void ms(struct apl *self, unsigned count)
{ while (count-- != 0u) apl_tick_1ms(self); }
static void boot(struct apl *self, struct host *host)
{
    memset(host, 0, sizeof(*host));
    apl_init(self, &core_ops, host, &hmi_ops, host);
}
static struct apl_link_snapshot snapshot(struct apl *self)
{ struct apl_link_snapshot value; apl_core_get_snapshot(&self->core, &value); return value; }
static void command(struct apl *self, enum apl_link_command_type type)
{
    struct apl_link_command value = {type, 0u};
    assert(APL_LINK_OK == apl_core_command(&self->core, &value));
}
static void boundaries_and_instances(void)
{
    struct apl first, second, saved;
    struct host a, b;
    struct apl_profile profile;
    boot(&first, &a); boot(&second, &b);
    apl_tick_1ms(NULL);
    ms(&first, 9u); ms(&second, 4u);
    assert(a.samples == 0u && b.samples == 0u && a.rgb_calls == 0u);
    ms(&first, 1u);
    assert(a.samples == 1u && a.rgb_calls == 1u);
    assert(a.red == 255u && a.green == 255u && a.blue == 255u);
    ms(&second, 5u); assert(b.samples == 0u);
    ms(&second, 1u); assert(b.samples == 1u);
    ms(&first, 1989u); assert(apl_hmi_get_powerup_lamp(&first.hmi));
    ms(&first, 1u); assert(!apl_hmi_get_powerup_lamp(&first.hmi));
    assert(apl_hmi_get_powerup_lamp(&second.hmi));
    /* A failed profile load preserves the pending 9ms; successful init clears it. */
    ms(&second, 9u); saved = second;
    apl_profile_defaults(&profile); profile.version = 0u;
    assert(APL_PROFILE_UNSUPPORTED_VERSION == apl_init_profile(&second, &core_ops, &b, &hmi_ops, &b, &profile));
    assert(memcmp(&saved, &second, sizeof(saved)) == 0);
    ms(&second, 1u); assert(b.samples == 2u);
    ms(&second, 9u);
    assert(APL_PROFILE_OK == apl_init_profile(&second, &core_ops, &b, &hmi_ops, &b, NULL));
    ms(&second, 9u); assert(b.samples == 2u);
    ms(&second, 1u); assert(b.samples == 3u);
    ms(&second, 9u); apl_profile_defaults(&profile);
    assert(APL_PROFILE_OK == apl_init_profile(&second, &core_ops, &b, &hmi_ops, &b, &profile));
    ms(&second, 9u); assert(b.samples == 3u);
    ms(&second, 1u); assert(b.samples == 4u);
    ms(&second, 9u); apl_init(&second, &core_ops, &b, &hmi_ops, &b);
    ms(&second, 9u); assert(b.samples == 4u);
    ms(&second, 1u); assert(b.samples == 5u);
    /* Legacy callers still get a full 10ms business frame from each apl_tick. */
    apl_tick(&first); assert(a.samples == 201u);
}
static void gestures_and_seconds(void)
{
    struct apl self;
    struct host host;
    boot(&self, &host); ms(&self, 2000u);
    apl_hmi_set_key_callback(&self.hmi, APL_HMI_KEY_POWER, short_press, long_press, &host);
    host.keys.power = APL_HMI_KEY_PRESSED;
    ms(&self, 2999u); assert(host.long_presses == 0u);
    ms(&self, 1u); assert(host.long_presses == 1u);
    ms(&self, 100u); assert(host.long_presses == 1u);
    host.keys.power = APL_HMI_KEY_RELEASED; ms(&self, 10u);
    assert(host.short_presses == 0u);
    host.keys.power = APL_HMI_KEY_PRESSED; ms(&self, 50u);
    host.keys.power = APL_HMI_KEY_RELEASED; ms(&self, 9u);
    assert(host.short_presses == 0u);
    ms(&self, 1u); assert(host.short_presses == 1u);
    apl_core_set_timer(&self.core, 2u, timer_done, &host);
    ms(&self, 999u); assert(apl_core_get_timer(&self.core) == 2u);
    ms(&self, 1u); assert(apl_core_get_timer(&self.core) == 1u);
    ms(&self, 999u); assert(host.timer_calls == 0u);
    ms(&self, 1u); assert(host.timer_calls == 1u && apl_core_get_timer(&self.core) == 0u);
    ms(&self, 1000u); assert(host.timer_calls == 1u);
}
static void sensor_and_drying(void)
{
    struct apl self;
    struct host host;
    struct apl_profile profile;
    uint8_t expected = (APL_COMPILE_SENSOR_PM25 ? APL_SENSOR_PM25_MASK : 0u)
        | (APL_COMPILE_SENSOR_HUMIDITY ? APL_SENSOR_HUMIDITY_MASK : 0u);
    memset(&host, 0, sizeof(host)); apl_profile_defaults(&profile);
#if APL_COMPILE_HAS_DRY
    profile.dry_s = 2u;
#endif
    assert(APL_PROFILE_OK == apl_init_profile(&self, &core_ops, &host, &hmi_ops, &host, &profile));
    apl_link_bind(&self); ms(&self, 2000u);
    command(&self, APL_LINK_CMD_POWER); ms(&self, 4999u);
    assert((snapshot(&self).sensors_ready & expected) == 0u);
    ms(&self, 1u); assert((snapshot(&self).sensors_ready & expected) == expected);
    command(&self, APL_LINK_CMD_POWER);
#if APL_COMPILE_HUMIDIFY
    assert(apl_core_get_run_state(&self.core) == APL_RUN_DRY);
    ms(&self, 10u); assert(apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_DRY) == 255u);
    ms(&self, 1989u); assert(apl_core_get_run_state(&self.core) == APL_RUN_DRY);
    ms(&self, 1u); assert(apl_core_get_run_state(&self.core) == APL_RUN_STANDBY);
    assert(apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_DRY) == 0u);
#else
    assert(apl_core_get_run_state(&self.core) == APL_RUN_STANDBY);
#endif
}
static void compare_observable(struct apl *a, struct apl *b, struct host *ha, struct host *hb)
{
    struct apl_link_snapshot sa = snapshot(a), sb = snapshot(b);
    char da[3], db[3];
    unsigned i;
    assert(memcmp(&sa, &sb, sizeof(sa)) == 0);
    assert(memcmp(ha, hb, sizeof(*ha)) == 0);
    apl_hmi_get_digits_content(&a->hmi, da); apl_hmi_get_digits_content(&b->hmi, db);
    assert(memcmp(da, db, sizeof(da)) == 0);
    assert(apl_hmi_get_digits_brightness(&a->hmi) == apl_hmi_get_digits_brightness(&b->hmi));
    assert(apl_hmi_get_digits_unit(&a->hmi) == apl_hmi_get_digits_unit(&b->hmi));
    for (i = 0; i < APL_HMI_KEY_COUNT; ++i)
        assert(apl_hmi_get_key_lamp(&a->hmi, (enum apl_hmi_key_id)i) == apl_hmi_get_key_lamp(&b->hmi, (enum apl_hmi_key_id)i));
    for (i = 0; i < APL_HMI_ICON_COUNT; ++i)
        assert(apl_hmi_get_icon(&a->hmi, (enum apl_hmi_icon)i) == apl_hmi_get_icon(&b->hmi, (enum apl_hmi_icon)i));
    for (i = 1; i <= APL_HMI_WATER_LAMP_COUNT; ++i)
        assert(apl_hmi_get_water_lamp(&a->hmi, (uint8_t)i) == apl_hmi_get_water_lamp(&b->hmi, (uint8_t)i));
}
static void legacy_equivalence(void)
{
    struct apl legacy, fine;
    struct host a, b;
    unsigned elapsed;
    boot(&legacy, &a); boot(&fine, &b); apl_link_bind(&legacy); apl_link_bind(&fine);
    for (elapsed = 0u; elapsed < 14000u; elapsed += 10u)
    {
        a.keys.power = ((elapsed >= 2100u && elapsed < 2150u) || (elapsed >= 12000u && elapsed < 12050u))
            ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
        a.keys.speed = (elapsed >= 8000u && elapsed < 11050u) ? APL_HMI_KEY_PRESSED : APL_HMI_KEY_RELEASED;
        b.keys = a.keys;
        ms(&fine, 9u); compare_observable(&legacy, &fine, &a, &b);
        apl_tick(&legacy); ms(&fine, 1u); compare_observable(&legacy, &fine, &a, &b);
    }
}
int main(void)
{
    assert(APL_TICK_MS == 1u && APL_CORE_TICK_MS == 10u && APL_HMI_TICK_MS == 10u);
    boundaries_and_instances(); gestures_and_seconds(); sensor_and_drying(); legacy_equivalence();
    puts("PASS: 1ms entry, independent instances, atomic/reset initialization, exact timing, short/long keys, RGB and legacy behavior equivalence");
    return 0;
}
