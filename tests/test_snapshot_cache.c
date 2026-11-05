/** Public-API checks for cache freshness and direct-vs-copy snapshot delivery. */
#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host { struct apl_core *core; unsigned saves; uint8_t purify, humidify; };
static enum apl_water_level water(void *ctx) APL_CALL
{ APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static void memory_save(const struct apl_core_memory *memory, void *ctx) APL_CALL
{
    struct host *host = ctx;
    struct apl_link_snapshot snapshot;
    APL_UNUSED_PARAM(memory);
    apl_core_get_snapshot(host->core, &snapshot);
    host->saves++; host->purify = snapshot.filter_purify_left; host->humidify = snapshot.filter_humidify_left;
}
static uint8_t ratio(uint32_t used)
{ return used >= 3600u ? 0u : (uint8_t)(((3600u - (uint64_t)used) * 99u + 3599u) / 3600u); }
static void check(struct apl_core *self, uint32_t purify, uint32_t humidify)
{
    struct apl_link_snapshot s;
    struct apl_core saved = *self;
    unsigned i;
    for (i = 0; i < 100u; ++i) {
        apl_core_get_snapshot(self, &s);
        assert(s.filter_purify_left == (APL_COMPILE_PURIFY ? ratio(purify) : 0u));
        assert(s.filter_humidify_left == (APL_COMPILE_HUMIDIFY ? ratio(humidify) : 0u));
    }
    assert(memcmp(self, &saved, sizeof(saved)) == 0); /* Const reads must not mutate caches. */
}
static void command(struct apl_core *self, enum apl_link_command_type type)
{
    struct apl_link_command c = {type, 0u};
    assert(APL_LINK_OK == apl_core_command(self, &c));
}
static void freshness(void)
{
    struct apl_core core;
    struct apl_profile profile;
    struct apl_core_memory memory;
    struct apl_core_ops ops;
    struct host host;
    unsigned i;
    memset(&host, 0, sizeof(host)); host.core = &core;
    memset(&ops, 0, sizeof(ops)); ops.get_water_level = water; ops.memory_save = memory_save;
    apl_profile_defaults(&profile); profile.filter_life_h = 1u;
    assert(APL_PROFILE_OK == apl_core_init_profile(&core, &ops, &host, &profile));
    check(&core, 0u, 0u);
    memset(&memory, 0, sizeof(memory)); memory.used = 1u; memory.fan_gear = APL_FAN_GEAR_1;
    memory.filter_purify_s = 3599u; memory.filter_humidify_s = 3599u;
    apl_core_memory_restore(&core, &memory); check(&core, 3599u, 3599u);
    command(&core, APL_LINK_CMD_POWER);
    for (i = 0; i < 99u; ++i) apl_core_tick(&core);
    check(&core, 3599u, 3599u);
    apl_core_tick(&core); check(&core, 3600u, 3600u);
    assert(host.saves != 0u && host.purify == 0u && host.humidify == 0u);
#if APL_COMPILE_PURIFY
    apl_core_filter_purify_reset(&core); check(&core, 0u, 3600u);
#endif
#if APL_COMPILE_HUMIDIFY
    apl_core_filter_humidify_reset(&core); check(&core, 0u, 0u);
#endif
    memory.filter_purify_s = 3600u; memory.filter_humidify_s = 3600u;
    apl_core_memory_restore(&core, &memory);
    command(&core, APL_LINK_CMD_FILTER_RESET); check(&core, 0u, 0u);
    assert(host.purify == (APL_COMPILE_PURIFY ? 99u : 0u));
    assert(host.humidify == (APL_COMPILE_HUMIDIFY ? 99u : 0u));
    apl_core_power_off(&core);
    memory.filter_purify_s = 1900u; memory.filter_humidify_s = 1900u;
    apl_core_memory_restore(&core, &memory); check(&core, 1900u, 1900u);
    command(&core, APL_LINK_CMD_FACTORY_ENTER);
    check(&core, 3600u - APL_CORE_FACTORY_FILTER_LEFT_S, 3600u - APL_CORE_FACTORY_FILTER_LEFT_S);
    command(&core, APL_LINK_CMD_FACTORY_EXIT); check(&core, 1900u, 1900u);
    memory.factory_pending = 1u; memory.filter_purify_s = 2500u; memory.filter_humidify_s = 2500u;
    apl_core_memory_restore(&core, &memory); check(&core, 2500u, 2500u);
    memory.factory_pending = 0u; memory.filter_purify_s = UINT32_MAX; memory.filter_humidify_s = UINT32_MAX;
    apl_core_memory_restore(&core, &memory); check(&core, UINT32_MAX, UINT32_MAX);
    /* Changing the lifespan by reinitialization must not reuse previous cached values. */
    profile.filter_life_h = 2u;
    assert(APL_PROFILE_OK == apl_core_init_profile(&core, NULL, NULL, &profile));
    memory.filter_purify_s = 3600u; memory.filter_humidify_s = 3600u;
    apl_core_memory_restore(&core, &memory);
    { struct apl_link_snapshot s; apl_core_get_snapshot(&core, &s);
      assert(s.filter_purify_left == (APL_COMPILE_PURIFY ? 50u : 0u));
      assert(s.filter_humidify_left == (APL_COMPILE_HUMIDIFY ? 50u : 0u)); }
}
static void compare_display(struct apl *a, struct apl *b)
{
    struct apl_link_snapshot sa, sb;
    char da[3], db[3]; unsigned i;
    apl_core_get_snapshot(&a->core, &sa); apl_core_get_snapshot(&b->core, &sb);
    assert(memcmp(&sa, &sb, sizeof(sa)) == 0);
    apl_hmi_get_digits_content(&a->hmi, da); apl_hmi_get_digits_content(&b->hmi, db);
    assert(memcmp(da, db, sizeof(da)) == 0);
    assert(apl_hmi_get_digits_brightness(&a->hmi) == apl_hmi_get_digits_brightness(&b->hmi));
    assert(apl_hmi_get_digits_unit(&a->hmi) == apl_hmi_get_digits_unit(&b->hmi));
    assert(apl_hmi_get_buzzer_busy(&a->hmi) == apl_hmi_get_buzzer_busy(&b->hmi));
    assert(apl_hmi_get_ambient_brightness(&a->hmi) == apl_hmi_get_ambient_brightness(&b->hmi));
    for (i = 0; i < APL_HMI_ICON_COUNT; ++i)
        assert(apl_hmi_get_icon(&a->hmi, (enum apl_hmi_icon)i) == apl_hmi_get_icon(&b->hmi, (enum apl_hmi_icon)i));
    for (i = 0; i < APL_HMI_KEY_COUNT; ++i)
        assert(apl_hmi_get_key_lamp(&a->hmi, (enum apl_hmi_key_id)i) == apl_hmi_get_key_lamp(&b->hmi, (enum apl_hmi_key_id)i));
    for (i = 1; i <= APL_HMI_WATER_LAMP_COUNT; ++i)
        assert(apl_hmi_get_water_lamp(&a->hmi, (uint8_t)i) == apl_hmi_get_water_lamp(&b->hmi, (uint8_t)i));
}
static void pipeline(void)
{
    struct apl direct, copied;
    struct apl_link_snapshot snapshot;
    unsigned frame;
    apl_init(&direct, NULL, NULL, NULL, NULL); apl_init(&copied, NULL, NULL, NULL, NULL);
    apl_link_bind(&direct); apl_link_bind(&copied);
    for (frame = 0u; frame < 2000u; ++frame) {
        if (frame == 250u || frame == 1250u) {
            command(&direct.core, APL_LINK_CMD_POWER); command(&copied.core, APL_LINK_CMD_POWER);
        }
        if (frame == 850u) {
            apl_core_set_fan_gear(&direct.core, APL_FAN_GEAR_SLEEP);
            apl_core_set_fan_gear(&copied.core, APL_FAN_GEAR_SLEEP);
        }
        if (frame == 950u) {
            apl_core_set_fan_gear(&direct.core, APL_FAN_GEAR_2);
            apl_core_set_fan_gear(&copied.core, APL_FAN_GEAR_2);
        }
        apl_tick(&direct);
        apl_core_tick(&copied.core); apl_core_get_snapshot(&copied.core, &snapshot);
        apl_hmi_set_snapshot(&copied.hmi, &snapshot); apl_hmi_tick(&copied.hmi);
        compare_display(&direct, &copied);
    }
    /* Independent HMI snapshot delivery continues to accept host-owned snapshots. */
    apl_hmi_init(&copied.hmi, NULL, NULL); apl_hmi_set_snapshot(&copied.hmi, &snapshot);
    memset(&snapshot, 0, sizeof(snapshot)); apl_hmi_tick(&copied.hmi);
    apl_hmi_set_snapshot(NULL, &snapshot); apl_hmi_set_snapshot(&copied.hmi, NULL);
}
int main(void)
{
    freshness(); pipeline();
    puts("PASS: cached filter freshness, callback reads, factory/restore/reset/lifespan boundaries and direct/copy display equivalence");
    return 0;
}
