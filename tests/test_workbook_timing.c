#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host { struct apl_hmi_key_state keys; };
static void keys(struct apl_hmi_key_state *value, void *ctx) { *value = ((struct host *)ctx)->keys; }
static enum apl_water_level water(void *ctx) { (void)ctx; return APL_WATER_LEVEL_3; }
static uint8_t humidity(void *ctx) { (void)ctx; return 45u; }
static const struct apl_core_ops core = {.get_water_level = water, .get_humidity = humidity};
static const struct apl_hmi_ops hmi = {.get_key_state = keys};
static void ticks(struct apl *self, unsigned long n) { while (n-- != 0u) { apl_tick(self); } }
static void command(struct apl *self, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command c = {type, arg};
    assert(APL_LINK_OK == apl_core_command(&self->core, &c));
}
static void start(struct apl *self, struct host *host)
{
    memset(host, 0, sizeof(*host)); apl_init(self, &core, host, &hmi, host); apl_link_bind(self);
    ticks(self, 210u); command(self, APL_LINK_CMD_POWER, 0u); ticks(self, 100u);
}
static void short_timer(struct apl *self, struct host *host)
{
    host->keys.timer = APL_HMI_KEY_PRESSED; ticks(self, 5u);
    host->keys.timer = APL_HMI_KEY_RELEASED; ticks(self, 1u);
}
static unsigned digits(struct apl *self)
{
    char text[APL_HMI_DIGIT_COUNT]; unsigned result = 0u; unsigned i;
    apl_hmi_get_digits_content(&self->hmi, text);
    for (i = 0u; i < APL_HMI_DIGIT_COUNT; i++) { if (text[i] != ' ') { assert(text[i] >= '0' && text[i] <= '9'); result = result * 10u + (unsigned)(text[i] - '0'); } }
    return result;
}
int main(void)
{
    struct apl self; struct host host;
    start(&self, &host);
    command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_SLEEP);
    ticks(&self, 499u); assert(255u == apl_hmi_get_digits_brightness(&self.hmi));
    ticks(&self, 1u); assert(0u == apl_hmi_get_digits_brightness(&self.hmi));
    assert(255u == apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_SLEEP));
    host.keys.speed = APL_HMI_KEY_PRESSED; ticks(&self, 5u);
    host.keys.speed = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    assert(255u == apl_hmi_get_digits_brightness(&self.hmi));
    ticks(&self, 300u); assert(255u == apl_hmi_get_digits_brightness(&self.hmi));
    ticks(&self, 199u); assert(0u == apl_hmi_get_digits_brightness(&self.hmi));
    start(&self, &host);
    short_timer(&self, &host); assert(1u == digits(&self));
    host.keys.timer = APL_HMI_KEY_PRESSED;
    ticks(&self, 149u); assert(1u == digits(&self));
    ticks(&self, 1u); assert(2u == digits(&self));
    ticks(&self, 49u); assert(2u == digits(&self));
    ticks(&self, 1u); assert(3u == digits(&self));
    ticks(&self, 150u); assert(6u == digits(&self));
    assert(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&self.hmi));
    assert(0u == apl_core_get_timer(&self.core));
    host.keys.timer = APL_HMI_KEY_RELEASED; ticks(&self, 1u); assert(6u == digits(&self));
    ticks(&self, 301u); assert(21600u == apl_core_get_timer(&self.core));
    command(&self, APL_LINK_CMD_TIMER_SET, 24u); ticks(&self, 1u);
    short_timer(&self, &host); assert(24u == digits(&self));
    host.keys.timer = APL_HMI_KEY_PRESSED;
    ticks(&self, 150u); assert(0u == digits(&self));
    ticks(&self, 199u); assert(0u == digits(&self));
    ticks(&self, 1u); assert(1u == digits(&self));
    host.keys.timer = APL_HMI_KEY_RELEASED; ticks(&self, 301u); assert(3600u == apl_core_get_timer(&self.core));
    host.keys.timer = APL_HMI_KEY_PRESSED; ticks(&self, 300u);
    host.keys.timer = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    assert(APL_HMI_CHILD_LOCK_ON == apl_hmi_get_child_lock(&self.hmi));
    host.keys.timer = APL_HMI_KEY_PRESSED; ticks(&self, 300u);
    host.keys.timer = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    assert(APL_HMI_CHILD_LOCK_OFF == apl_hmi_get_child_lock(&self.hmi));
    start(&self, &host); command(&self, APL_LINK_CMD_TIMER_SET, 1u);
    ticks(&self, 360000UL);
    assert((APL_COMPILE_HUMIDIFY ? APL_RUN_DRY : APL_RUN_STANDBY) == apl_core_get_run_state(&self.core));
    puts("PASS: 499/500 sleep boundary, 149/150 and 49/50 timer repeat, zero pause, release, child lock and normal expiry");
    return 0;
}
