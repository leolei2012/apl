/** General feature-trim behavior. Compile with all current library sources. */
#include "apl.h"
#include <assert.h>
#include <string.h>

struct host
{
    unsigned memory_calls;
    unsigned timer_calls;
    unsigned buzzer_calls;
    unsigned ambient_calls;
    unsigned pm_reads;
    unsigned humidity_reads;
    enum apl_switch pump;
};
static enum apl_water_level water(void *ctx) APL_CALL
{ APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static uint16_t pm25(void *ctx) APL_CALL
{ ((struct host *)ctx)->pm_reads++; return 12u; }
static uint8_t humidity(void *ctx) APL_CALL
{ ((struct host *)ctx)->humidity_reads++; return 40u; }
static void pump(enum apl_switch state, void *ctx) APL_CALL
{ ((struct host *)ctx)->pump = state; }
static void memory_save(const struct apl_core_memory *memory, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(memory); ((struct host *)ctx)->memory_calls++; }
static void buzzer(uint8_t on, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(on); ((struct host *)ctx)->buzzer_calls++; }
static void rgb(uint8_t r, uint8_t g, uint8_t b, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(r); APL_UNUSED_PARAM(g); APL_UNUSED_PARAM(b); ((struct host *)ctx)->ambient_calls++; }
#if APL_COMPILE_TIMER
static void timer_done(struct apl_core *self, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(self); ((struct host *)ctx)->timer_calls++; }
#endif
static void ticks(struct apl *self, unsigned count)
{ while (count-- != 0u) apl_tick(self); }
static enum apl_link_result command(struct apl *self, enum apl_link_command_type type, uint8_t arg)
{ struct apl_link_command c; c.type = type; c.arg = arg; return apl_core_command(&self->core, &c); }
static void boot(struct apl *self, struct host *host)
{
    static struct apl_core_ops core;
    static struct apl_hmi_ops hmi;
    memset(host, 0, sizeof(*host)); memset(&core, 0, sizeof(core)); memset(&hmi, 0, sizeof(hmi));
    core.get_water_level = water; core.get_pm25 = pm25; core.get_humidity = humidity;
    core.set_pump = pump; core.memory_save = memory_save;
    hmi.set_buzzer = buzzer; hmi.set_ambient_rgb = rgb;
    assert(APL_PROFILE_OK == apl_init_profile(self, &core, host, &hmi, host, NULL));
    apl_link_bind(self);
    ticks(self, 210u);
}

int main(void)
{
    struct apl self;
    struct host host;
    struct apl_link_snapshot snap;
    unsigned i;
    boot(&self, &host);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_POWER, 0u));
    ticks(&self, 1u);
    assert(APL_RUN_ON == apl_core_get_run_state(&self.core));
    assert(apl_core_get_fan_target_rpm(&self.core) > 0u);
    ticks(&self, 600u);
    assert((host.pm_reads != 0u) == (APL_COMPILE_SENSOR_PM25 != 0u));
    assert((host.humidity_reads != 0u) == (APL_COMPILE_SENSOR_HUMIDITY != 0u));
#if APL_COMPILE_UV
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_UV_SET, 1u));
    ticks(&self, 1u); apl_core_get_snapshot(&self.core, &snap);
    /* Cutting other features must not weaken the UV missing-interlock safety. */
    assert(snap.uv == APL_SWITCH_OFF);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_UV_SET, 0u));
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_UV_SET, 1u));
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.uv == APL_SWITCH_OFF && snap.uv_enabled == 0u && snap.uv_requested == 0u);
#endif
#if APL_COMPILE_ANION
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_ANION_SET, 1u));
    ticks(&self, 1u); apl_core_get_snapshot(&self.core, &snap); assert(snap.anion == APL_SWITCH_ON);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_ANION_SET, 0u));
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_ANION_SET, 1u));
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.anion == APL_SWITCH_OFF && snap.anion_enabled == 0u && snap.anion_requested == 0u);
#endif
#if APL_CORE_HAS_TURBO
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_TURBO_TOGGLE, 0u));
    assert(apl_core_get_fan_gear(&self.core) == APL_FAN_GEAR_TURBO);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_TURBO_TOGGLE, 0u));
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_TURBO_TOGGLE, 0u));
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_TURBO));
#endif

#if APL_COMPILE_TIMER
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_TIMER_SET, 1u));
    assert(apl_core_get_timer(&self.core) > 0u);
    apl_core_set_timer(&self.core, 1u, timer_done, &host);
    ticks(&self, 100u);
    assert(host.timer_calls == 1u && apl_core_get_timer(&self.core) == 0u);
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_TIMER_SET, 1u));
    ticks(&self, 100u);
    apl_core_get_snapshot(&self.core, &snap); assert(snap.timer_left_s == 0u);
#endif

#if APL_COMPILE_FILTER
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_FILTER_RESET, 0u));
    apl_core_get_snapshot(&self.core, &snap); assert(snap.filter_reset_seq != 0u);
#if APL_COMPILE_PURIFY
    apl_core_filter_purify_reset(&self.core); assert(apl_core_get_filter_purify(&self.core) == 0u);
#endif
#if APL_COMPILE_HUMIDIFY
    apl_core_filter_humidify_reset(&self.core); assert(apl_core_get_filter_humidify(&self.core) == 0u);
#endif
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_FILTER_RESET, 0u));
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.filter_purify_left == 0u && snap.filter_humidify_left == 0u && snap.filter_reset_seq == 0u);
#endif

#if APL_COMPILE_MEMORY
    { struct apl_core_memory saved; apl_core_get_memory(&self.core, &saved); assert(host.memory_calls > 0u); }
#else
    assert(host.memory_calls == 0u);
#endif

#if APL_COMPILE_SLEEP
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_SLEEP_TOGGLE, 0u));
    apl_core_get_snapshot(&self.core, &snap); assert(snap.sleep != 0u);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_SLEEP_TOGGLE, 0u));
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_SLEEP_TOGGLE, 0u));
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_SLEEP));
#endif
    for (i = 0u; i < 12u; ++i)
    {
        assert(APL_LINK_OK == command(&self, APL_LINK_CMD_FAN_CYCLE, 0u));
#if !APL_COMPILE_SLEEP
        assert(apl_core_get_fan_gear(&self.core) != APL_FAN_GEAR_SLEEP);
#endif
    }

    /* Factory entrance and CL completion are absent as a feature, not a stub. */
    boot(&self, &host);
#if APL_COMPILE_FACTORY
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_FACTORY_ENTER, 0u));
    assert(APL_RUN_FACTORY == apl_core_get_run_state(&self.core));
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_FACTORY_EXIT, 0u));
#else
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_FACTORY_ENTER, 0u));
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_FACTORY_EXIT, 0u));
    apl_core_get_snapshot(&self.core, &snap); assert(snap.factory_done == 0u);
#endif

    boot(&self, &host);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_POWER, 0u)); ticks(&self, 1u);
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_POWER, 0u));
#if APL_COMPILE_DRY && APL_COMPILE_HUMIDIFY
    assert(APL_RUN_DRY == apl_core_get_run_state(&self.core));
    assert(APL_LINK_OK == command(&self, APL_LINK_CMD_DRY_STOP, 0u));
#else
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&self.core));
    assert(APL_LINK_REJECTED == command(&self, APL_LINK_CMD_DRY_STOP, 0u));
#endif
    ticks(&self, 1u);
    assert(apl_core_get_fan_target_rpm(&self.core) == 0u);
    apl_core_get_snapshot(&self.core, &snap);
#if !APL_COMPILE_DRY
    assert(snap.dry_left_s == 0u);
#endif
#if !APL_COMPILE_HMI || !APL_HMI_COMPILE_AMBIENT
    assert(host.ambient_calls == 0u);
#endif
#if !APL_COMPILE_HMI || !APL_HMI_COMPILE_BUZZER
    assert(host.buzzer_calls == 0u);
#endif
    return 0;
}
