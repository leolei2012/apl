#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host { struct apl_hmi_key_state keys; };
static void keys(struct apl_hmi_key_state *value, void *ctx) { *value = ((struct host *)ctx)->keys; }
static const struct apl_hmi_ops hmi = {.get_key_state = keys};
static void ticks(struct apl *self, unsigned n) { while (n-- != 0u) { apl_tick(self); } }
static unsigned resets(struct apl *self)
{
    struct apl_link_snapshot s; apl_core_get_snapshot(&self->core, &s); return s.filter_reset_seq;
}
static void command(struct apl *self, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command c = {type, arg}; assert(APL_LINK_OK == apl_core_command(&self->core, &c));
}
int main(void)
{
    struct apl self; struct host host; char digits[APL_HMI_DIGIT_COUNT];
    memset(&host, 0, sizeof(host)); apl_init(&self, NULL, &host, &hmi, &host); apl_link_bind(&self); ticks(&self, 210u);
    /* 按键说明J9：纯净化寿命键在待机可查询，其他机型输入无效。 */
    host.keys.filter = APL_HMI_KEY_PRESSED; ticks(&self, 5u);
    host.keys.filter = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    apl_hmi_get_digits_content(&self.hmi, digits);
#if APL_COMPILE_PURIFY && !APL_COMPILE_HUMIDIFY && APL_HMI_COMPILE_KEY_FILTER
    assert(0 == memcmp(digits, " 99", 3));
    assert(255u == apl_hmi_get_digits_brightness(&self.hmi));
#else
    assert(0u == apl_hmi_get_digits_brightness(&self.hmi));
#endif
    host.keys.filter = APL_HMI_KEY_PRESSED; ticks(&self, 300u);
    host.keys.filter = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    assert((APL_COMPILE_PURIFY && !APL_COMPILE_HUMIDIFY && APL_HMI_COMPILE_KEY_FILTER ? 1u : 0u) == resets(&self));
    /* J12：组合键只在加湿/一体机重置。 */
    host.keys.timer = APL_HMI_KEY_PRESSED; host.keys.speed = APL_HMI_KEY_PRESSED; ticks(&self, 300u);
    host.keys.timer = APL_HMI_KEY_RELEASED; host.keys.speed = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    assert((APL_COMPILE_HUMIDIFY || APL_HMI_COMPILE_KEY_FILTER ? 1u : 0u) == resets(&self));
    /* 童锁下独立寿命键和组合键均不得再次重置。 */
    apl_hmi_set_child_lock(&self.hmi, APL_HMI_CHILD_LOCK_ON);
    host.keys.filter = APL_HMI_KEY_PRESSED; ticks(&self, 305u); host.keys.filter = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    host.keys.timer = APL_HMI_KEY_PRESSED; host.keys.speed = APL_HMI_KEY_PRESSED; ticks(&self, 305u);
    host.keys.timer = APL_HMI_KEY_RELEASED; host.keys.speed = APL_HMI_KEY_RELEASED; ticks(&self, 1u);
    assert((APL_COMPILE_HUMIDIFY || APL_HMI_COMPILE_KEY_FILTER ? 1u : 0u) == resets(&self));
    apl_hmi_set_child_lock(&self.hmi, APL_HMI_CHILD_LOCK_OFF);
    command(&self, APL_LINK_CMD_POWER, 0u); ticks(&self, 1u);
#if APL_CORE_HAS_TURBO
    command(&self, APL_LINK_CMD_TURBO_TOGGLE, 0u); ticks(&self, 1u);
    assert(255u == apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_1));
    assert(255u == apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_2));
    assert(255u == apl_hmi_get_icon(&self.hmi, APL_HMI_ICON_GEAR_3));
#endif
    puts("PASS: standby filter query, per-model reset key, child lock and three Turbo indicators; physical key mapping remains external");
    return 0;
}
