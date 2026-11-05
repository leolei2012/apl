#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host { struct apl_hmi_key_state keys; unsigned samples, key_events, buzz_writes; };
static void key_read(struct apl_hmi_key_state *keys, void *ctx) APL_CALL
{ struct host *host = ctx; host->samples++; *keys = host->keys; }
static void buzzer(uint8_t value, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(value); ((struct host *)ctx)->buzz_writes++; }
static enum apl_water_level water(void *ctx) APL_CALL
{ APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static void ticks(struct apl *self, unsigned n)
{ while (n-- != 0u) apl_tick(self); }
#if APL_COMPILE_HMI
#if APL_HMI_COMPILE_KEY_CALLBACKS || APL_HMI_COMBO_MAX > 0
static void key_event(struct apl_hmi *self, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(self); ((struct host *)ctx)->key_events++; }
#endif
static void press(struct apl *self, struct host *host, enum apl_hmi_key_level *key)
{
    APL_UNUSED_PARAM(host);
    *key = APL_HMI_KEY_PRESSED; ticks(self, 5u);
    *key = APL_HMI_KEY_RELEASED; ticks(self, 2u);
}
#endif
int main(void)
{
    struct apl self;
    struct host host;
    struct apl_core_ops core;
    struct apl_hmi_ops hmi;
    struct apl_core_memory memory;
    struct apl_link_snapshot snapshot;
    unsigned i;
    memset(&host, 0, sizeof(host)); memset(&core, 0, sizeof(core)); memset(&hmi, 0, sizeof(hmi));
    core.get_water_level = water; hmi.get_key_state = key_read; hmi.set_buzzer = buzzer;
    assert(APL_PROFILE_OK == apl_init_profile(&self, &core, &host, &hmi, &host, NULL));
    apl_link_bind(&self); ticks(&self, 210u);
#if APL_COMPILE_HMI
    assert(host.samples == 210u);
#if !APL_HMI_COMPILE_BUZZER
    assert(host.buzz_writes == 0u);
#endif
    /* The linked standard panel still works when arbitrary callbacks are removed. */
    press(&self, &host, &host.keys.power);
    assert(apl_core_get_run_state(&self.core) == APL_RUN_ON);
    press(&self, &host, &host.keys.speed);
    assert(apl_core_get_fan_gear(&self.core) == APL_FAN_GEAR_2);
    apl_init(&self, &core, &host, &hmi, &host); ticks(&self, 210u);
#if APL_HMI_COMPILE_KEY_CALLBACKS
    apl_hmi_set_key_callback(&self.hmi, APL_HMI_KEY_POWER, key_event, NULL, &host);
#endif
    press(&self, &host, &host.keys.power);
    assert(host.key_events == (APL_HMI_COMPILE_KEY_CALLBACKS ? 1u : 0u));
#if APL_HMI_COMBO_MAX > 0
    { const enum apl_hmi_key_id pairs[4][2] = {
        {APL_HMI_KEY_POWER,APL_HMI_KEY_SPEED}, {APL_HMI_KEY_POWER,APL_HMI_KEY_LIGHT},
        {APL_HMI_KEY_TIMER,APL_HMI_KEY_HUMIDITY}, {APL_HMI_KEY_PURIFY,APL_HMI_KEY_LIGHT} };
      for (i = 0u; i < 4u; ++i)
        assert(apl_hmi_set_combo(&self.hmi, pairs[i], 2u, 100u, key_event, &host)
            == (i < APL_HMI_COMBO_MAX ? 0 : -1));
    }
#endif
    { unsigned calls = host.buzz_writes;
#if APL_HMI_COMPILE_BUZZER
      apl_hmi_buzzer_play(&self.hmi, 1u, 100u, 100u);
#endif
      ticks(&self, 1u);
#if APL_HMI_COMPILE_BUZZER
      assert(apl_hmi_get_buzzer_busy(&self.hmi) == 1u);
#endif
      ticks(&self, 25u);
#if APL_HMI_COMPILE_BUZZER
      assert(!apl_hmi_get_buzzer_busy(&self.hmi));
#endif
      assert(APL_HMI_COMPILE_BUZZER ? host.buzz_writes > calls : host.buzz_writes == calls);
    }
#else
    assert(host.samples == 0u && host.buzz_writes == 0u);
#endif
    /* Both scheduler and memory/filter APIs remain available in core-only builds. */
    apl_init(&self, &core, &host, &hmi, &host); apl_core_power_on(&self.core);
    for (i = 0u; i < 9u; ++i) apl_tick_1ms(&self);
    assert(apl_core_get_fan_target_rpm(&self.core) == 0u);
    apl_tick_1ms(&self);
    assert(apl_core_get_fan_target_rpm(&self.core) == APL_DEFAULT_FAN_GEAR1_RPM);
    memset(&memory, 0, sizeof(memory)); memory.filter_purify_s = UINT32_MAX; memory.filter_humidify_s = UINT32_MAX;
    apl_core_memory_restore(&self.core, &memory); apl_core_get_snapshot(&self.core, &snapshot);
    assert(snapshot.filter_purify_left == 0u && snapshot.filter_humidify_left == 0u);
#if APL_COMPILE_PURIFY
    apl_core_filter_purify_reset(&self.core);
#endif
#if APL_COMPILE_HUMIDIFY
    apl_core_filter_humidify_reset(&self.core);
#endif
    apl_core_get_snapshot(&self.core, &snapshot);
    assert(snapshot.filter_purify_left == (APL_COMPILE_PURIFY ? 99u : 0u));
    assert(snapshot.filter_humidify_left == (APL_COMPILE_HUMIDIFY ? 99u : 0u));
    apl_tick(NULL); apl_tick_1ms(NULL); apl_link_bind(NULL);
    puts("PASS: build trimming, standard panel, disabled API behavior, combo capacity, core-only scheduler and memory");
    return 0;
}
