/** One all-feature executable, multiple runtime product profiles. */
#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host {
    unsigned pm_reads; unsigned rh_reads; unsigned uv_reads; enum apl_switch pump;
    struct apl_core *core; uint16_t callback_rpm; unsigned fan_calls; uint8_t sensor_fault;
};
static void product_pid(void)
{
    struct apl_core core; struct apl_core before; struct apl_profile p; struct apl_profile copied;
    char pid[APL_PRODUCT_PID_MAX + 1u];
    apl_profile_defaults(&p);
    assert(p.pid[0] == '\0');
    assert(APL_PROFILE_OK == apl_core_init_profile(&core, NULL, NULL, &p));
    memset(pid, 'x', sizeof(pid)); apl_core_get_pid(&core, pid); assert(pid[0] == '\0');
    memcpy(p.pid, "aaerrxvja3m7lvhz", sizeof(p.pid));
    assert(APL_PROFILE_OK == apl_core_init_profile(&core, NULL, NULL, &p));
    p.pid[0] = 'Z'; /* Initialization owns a copy, not the caller's buffer. */
    apl_core_get_pid(&core, pid); assert(0 == strcmp(pid, "aaerrxvja3m7lvhz"));
    apl_core_get_profile(&core, &copied); assert(0 == strcmp(copied.pid, pid));
    memcpy(p.pid, "Ab09", 5u);
    assert(APL_PROFILE_OK == apl_profile_validate(&p));
    before = core;
    p.pid[0] = '-';
    assert(APL_PROFILE_INVALID_VALUE == apl_core_init_profile(&core, NULL, NULL, &p));
    assert(0 == memcmp(&before, &core, sizeof(core)));
    memset(p.pid, 'a', sizeof(p.pid));
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    p.pid[0] = (char)0xff;
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    memset(pid, 'x', sizeof(pid)); apl_core_get_pid(NULL, pid); assert(pid[0] == '\0');
    apl_core_get_pid(&core, NULL);
    assert(APL_PROFILE_OK == apl_core_init_profile(&core, NULL, NULL, NULL));
    apl_core_get_pid(&core, pid); assert(pid[0] == '\0');
}
static uint16_t pm(void *ctx) APL_CALL { ((struct host *)ctx)->pm_reads++; return 160u; }
static uint8_t rh(void *ctx) APL_CALL { ((struct host *)ctx)->rh_reads++; return 35u; }
static uint8_t uv(void *ctx) APL_CALL { ((struct host *)ctx)->uv_reads++; return 1u; }
static enum apl_water_level water(void *ctx) APL_CALL { APL_UNUSED_PARAM(ctx); return APL_WATER_LEVEL_3; }
static void pump(enum apl_switch on, void *ctx) APL_CALL { ((struct host *)ctx)->pump = on; }
static void ticks(struct apl *self, unsigned count) { while (count-- != 0u) { apl_tick(self); } }
static enum apl_link_result cmd(struct apl *self, enum apl_link_command_type type, uint8_t arg)
{ struct apl_link_command c; c.type = type; c.arg = arg; return apl_core_command(&self->core, &c); }
static void fan_rpm_capture(enum apl_switch on, enum apl_fan_gear gear, void *ctx) APL_CALL
{
    struct host *host = ctx; APL_UNUSED_PARAM(gear);
    host->fan_calls++; host->callback_rpm = apl_core_get_fan_target_rpm(host->core);
    if (on == APL_SWITCH_OFF) assert(host->callback_rpm == 0u);
}
static enum apl_sensor_status rpm_status(enum apl_sensor sensor, void *ctx) APL_CALL
{ APL_UNUSED_PARAM(sensor); return ((struct host *)ctx)->sensor_fault ? APL_SENSOR_FAULT : APL_SENSOR_VALID; }

static void missing_profile(void)
{
    struct apl self; struct apl legacy; struct apl_profile expected; struct apl_profile actual;
    struct apl_core core; struct apl_core_ops ops; struct host host; struct apl_link_snapshot snap;
    unsigned i;
    const enum apl_fan_gear gears[] = {APL_FAN_GEAR_SLEEP, APL_FAN_GEAR_1, APL_FAN_GEAR_2, APL_FAN_GEAR_3, APL_FAN_GEAR_TURBO};
    const uint16_t rpms[] = {APL_DEFAULT_FAN_SLEEP_RPM, APL_DEFAULT_FAN_GEAR1_RPM,
        APL_DEFAULT_FAN_GEAR2_RPM, APL_DEFAULT_FAN_GEAR3_RPM, APL_DEFAULT_FAN_TURBO_RPM};
    memset(&ops, 0, sizeof(ops)); memset(&host, 0, sizeof(host));
    host.core = &self.core; ops.set_fan = fan_rpm_capture;
    apl_profile_defaults(&expected);
    assert(APL_PROFILE_OK == apl_init_profile(&self, &ops, &host, NULL, NULL, NULL));
    assert(host.fan_calls == 0u); /* Initialization never actuates physical loads. */
    apl_core_get_profile(&self.core, &actual);
    assert(0 == memcmp(&expected, &actual, sizeof(actual)));
    apl_init(&legacy, &ops, &host, NULL, NULL);
    /* HMI borrows each instance's own profile map, so its internal pointers differ. */
    assert(0 == memcmp(&self.core, &legacy.core, sizeof(self.core)));
    assert(self.tick_elapsed_ms == legacy.tick_elapsed_ms);
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&self.core));
    apl_link_bind(&self); ticks(&self, 201u);
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u)); ticks(&self, 1u);
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.run_state == APL_RUN_ON);
    assert(snap.fan_output == expected.default_fan_gear);
    assert(snap.display_rotate_s == expected.display_rotate_s);
    assert(snap.timer_step_h == expected.timer_step_h);
    assert(host.fan_calls != 0u);
    assert(expected.fan_gear1_rpm == apl_core_get_fan_target_rpm(&self.core));
    for (i = 0u; i < 5u; ++i)
    {
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_FAN_SET, (uint8_t)gears[i])); ticks(&self, 1u);
        apl_core_get_snapshot(&self.core, &snap);
        assert(snap.fan_target_rpm == rpms[i]);
        assert(apl_core_get_fan_target_rpm(&self.core) == rpms[i] && host.callback_rpm == rpms[i]);
    }
    assert(APL_PROFILE_OK == apl_core_init_profile(&core, NULL, NULL, NULL));
    apl_core_get_profile(&core, &actual);
    assert(0 == memcmp(&expected, &actual, sizeof(actual)));
    assert(APL_RUN_STANDBY == apl_core_get_run_state(&core));
    assert(APL_PROFILE_INVALID_ARGUMENT == apl_init_profile(NULL, NULL, NULL, NULL, NULL, NULL));
    assert(APL_PROFILE_INVALID_ARGUMENT == apl_core_init_profile(NULL, NULL, NULL, NULL));
    assert(APL_PROFILE_INVALID_ARGUMENT == apl_profile_validate(NULL));
}

static void fan_targets(void)
{
    struct apl self; struct apl_profile p; struct apl_profile copied; struct apl_core_ops ops;
    struct apl_link_snapshot snap; struct apl_core_memory memory; struct host host; unsigned mode, i;
    const enum apl_fan_gear gears[] = {APL_FAN_GEAR_SLEEP, APL_FAN_GEAR_1, APL_FAN_GEAR_2, APL_FAN_GEAR_3, APL_FAN_GEAR_TURBO};
    const uint16_t rpms[] = {111u, 401u, 902u, 1303u, 65535u};
    memset(&ops, 0, sizeof(ops)); ops.get_pm25 = pm; ops.get_humidity = rh;
    ops.get_water_level = water; ops.get_sensor_status = rpm_status; ops.set_fan = fan_rpm_capture;
    assert(0u == apl_core_get_fan_target_rpm(NULL));
    for (mode = 1u; mode <= 3u; ++mode)
    {
        memset(&host, 0, sizeof(host)); host.core = &self.core; apl_profile_defaults(&p);
        p.purify_enabled = (uint8_t)(mode & 1u); p.humidify_enabled = (uint8_t)((mode >> 1) & 1u);
        p.sensors = (uint8_t)((p.purify_enabled ? APL_SENSOR_PM25_MASK : 0u) | (p.humidify_enabled ? APL_SENSOR_HUMIDITY_MASK : 0u));
        p.modules = 0u; p.default_fan_gear = APL_FAN_GEAR_2; p.sensor_fallback_gear = APL_FAN_GEAR_1;
        p.dry_s = 2u; p.dry_fan_gear = APL_FAN_GEAR_3;
        p.fan_sleep_rpm = rpms[0]; p.fan_gear1_rpm = rpms[1]; p.fan_gear2_rpm = rpms[2];
        p.fan_gear3_rpm = rpms[3]; p.fan_turbo_rpm = rpms[4];
        assert(APL_PROFILE_OK == apl_init_profile(&self, &ops, &host, NULL, NULL, &p));
        apl_link_bind(&self); assert(0u == apl_core_get_fan_target_rpm(&self.core));
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u)); ticks(&self, 1u);
        assert(rpms[2] == apl_core_get_fan_target_rpm(&self.core));
        assert(rpms[2] == host.callback_rpm && host.fan_calls != 0u);
        p.fan_gear2_rpm = 1234u; /* Runtime config is a copy, not the caller's buffer. */
        apl_core_get_profile(&self.core, &copied); assert(copied.fan_gear2_rpm == rpms[2]);
        for (i = 0u; i < 5u; ++i)
        {
            assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_FAN_SET, (uint8_t)gears[i])); ticks(&self, 1u);
            apl_core_get_snapshot(&self.core, &snap);
            assert(snap.fan_output == gears[i] && snap.fan_target_rpm == rpms[i]);
            assert(rpms[i] == apl_core_get_fan_target_rpm(&self.core) && rpms[i] == host.callback_rpm);
        }
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO)); ticks(&self, 500u);
        assert((mode == 2u ? rpms[2] : rpms[3]) == apl_core_get_fan_target_rpm(&self.core));
        host.sensor_fault = 1u; ticks(&self, 100u);
        assert(APL_FAN_GEAR_AUTO == apl_core_get_fan_gear(&self.core));
        assert(rpms[1] == apl_core_get_fan_target_rpm(&self.core));
        host.sensor_fault = 0u; ticks(&self, 100u);
        if (mode != 1u)
        {
            assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_1));
            assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_HUMIDITY_TARGET, 65u)); ticks(&self, 1u);
            assert(APL_FAN_GEAR_1 == apl_core_get_fan_gear(&self.core));
            assert(rpms[3] == apl_core_get_fan_target_rpm(&self.core));
        }
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u)); ticks(&self, 1u);
        if (mode != 1u) { assert(rpms[3] == apl_core_get_fan_target_rpm(&self.core)); ticks(&self, 200u); }
        assert(0u == apl_core_get_fan_target_rpm(&self.core));
        assert(0u == host.callback_rpm);
        memset(&memory, 0, sizeof(memory)); memory.running = 1u; memory.used = 1u; memory.fan_gear = APL_FAN_GEAR_2;
        assert(APL_PROFILE_OK == apl_init_profile(&self, &ops, &host, NULL, NULL, &p));
        apl_link_bind(&self); apl_core_memory_restore(&self.core, &memory); ticks(&self, 1u);
        assert(1234u == apl_core_get_fan_target_rpm(&self.core));
    }
    /* A polling-only host needs no set_fan callback or measured-speed input. */
    apl_profile_defaults(&p); p.fan_gear1_rpm = 4321u; p.fan_sleep_rpm = 0u;
    assert(APL_PROFILE_OK == apl_init_profile(&self, NULL, NULL, NULL, NULL, &p));
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u)); ticks(&self, 1u);
    assert(4321u == apl_core_get_fan_target_rpm(&self.core));
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_SLEEP)); ticks(&self, 1u);
    assert(APL_RUN_ON == apl_core_get_run_state(&self.core));
    assert(0u == apl_core_get_fan_target_rpm(&self.core));
}

static void display_rotation(void)
{
    struct apl self;
    struct apl_profile p;
    struct apl_core_ops ops;
    struct apl_link_snapshot snap;
    struct host host;
    const uint16_t intervals[] = {1u, 7u, 3600u};
    unsigned i;
    memset(&ops, 0, sizeof(ops)); ops.get_pm25 = pm; ops.get_humidity = rh; ops.get_water_level = water;
    for (i = 0u; i < 3u; ++i)
    {
        uint32_t page_ticks = (uint32_t)intervals[i] * 100u;
        memset(&host, 0, sizeof(host)); apl_profile_defaults(&p);
        p.display_rotate_s = intervals[i];
        assert(APL_PROFILE_OK == apl_init_profile(&self, &ops, &host, NULL, NULL, &p));
        apl_link_bind(&self); ticks(&self, 201u);
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u));
        ticks(&self, 500u);
        apl_core_get_snapshot(&self.core, &snap); assert(snap.display_rotate_s == intervals[i]);
        if (intervals[i] == 1u)
        {
            /* Warmup does not reset the page clock: 5s is the humidity page. */
            assert(APL_HMI_UNIT_PERCENT == apl_hmi_get_digits_unit(&self.hmi));
            ticks(&self, 99u); assert(APL_HMI_UNIT_PERCENT == apl_hmi_get_digits_unit(&self.hmi));
            ticks(&self, 1u); assert(APL_HMI_UNIT_PM25 == apl_hmi_get_digits_unit(&self.hmi));
        }
        else
        {
            ticks(&self, page_ticks - 501u); assert(APL_HMI_UNIT_PM25 == apl_hmi_get_digits_unit(&self.hmi));
            ticks(&self, 1u); assert(APL_HMI_UNIT_PERCENT == apl_hmi_get_digits_unit(&self.hmi));
            ticks(&self, page_ticks - 1u); assert(APL_HMI_UNIT_PERCENT == apl_hmi_get_digits_unit(&self.hmi));
            ticks(&self, 1u); assert(APL_HMI_UNIT_PM25 == apl_hmi_get_digits_unit(&self.hmi));
        }
    }
}

int main(void)
{
    struct apl self;
    struct apl saved;
    struct apl_profile p;
    struct apl_profile readback;
    struct apl_core_ops ops;
    struct apl_link_snapshot snap;
    struct apl_core_memory memory;
    struct host host;
    unsigned mode;
    missing_profile();
    memset(&ops, 0, sizeof(ops));
    ops.get_pm25 = pm; ops.get_humidity = rh; ops.get_uv_interlock = uv;
    ops.get_water_level = water; ops.set_pump = pump;
    apl_profile_defaults(&p);
    assert(APL_PROFILE_OK == apl_profile_validate(&p));
    assert(p.display_rotate_s == 20u);
    assert(p.timer_step_h == 1u);
    assert(p.fan_sleep_rpm == APL_DEFAULT_FAN_SLEEP_RPM && p.fan_gear1_rpm == APL_DEFAULT_FAN_GEAR1_RPM);
    assert(p.fan_gear2_rpm == APL_DEFAULT_FAN_GEAR2_RPM && p.fan_gear3_rpm == APL_DEFAULT_FAN_GEAR3_RPM);
    assert(p.fan_turbo_rpm == APL_DEFAULT_FAN_TURBO_RPM);
    p.timer_step_h = 0u; assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    p.timer_step_h = 25u; assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    p.display_rotate_s = 0u; assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    p.display_rotate_s = 3601u; assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    assert(APL_PROFILE_INVALID_ARGUMENT == apl_profile_validate(NULL));
    apl_init(&self, NULL, NULL, NULL, NULL);
    saved = self;
    p.version++;
    assert(APL_PROFILE_UNSUPPORTED_VERSION == apl_init_profile(&self, &ops, &host, NULL, NULL, &p));
    assert(0 == memcmp(&self, &saved, sizeof(self)));
    apl_profile_defaults(&p);
    p.filter_life_h = 0u;
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    p.modules = 128u;
    assert(APL_PROFILE_UNSUPPORTED_CAPABILITY == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    p.purify_enabled = 0u; p.humidify_enabled = 0u; p.sensors = 0u;
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    p.purify_enabled = 0u; p.sensors = APL_SENSOR_PM25_MASK;
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    p.dry_s = 86401UL;
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    apl_profile_defaults(&p);
    p.filter_life_h = 1000001UL;
    assert(APL_PROFILE_INVALID_VALUE == apl_profile_validate(&p));
    for (mode = 1u; mode <= 3u; mode++)
    {
        memset(&host, 0, sizeof(host));
        apl_profile_defaults(&p);
        p.purify_enabled = (uint8_t)(mode & 1u);
        p.humidify_enabled = (uint8_t)((mode >> 1) & 1u);
        p.modules = 0u;
        p.sensors = (uint8_t)((p.purify_enabled ? APL_SENSOR_PM25_MASK : 0u)
                    | (p.humidify_enabled ? APL_SENSOR_HUMIDITY_MASK : 0u));
        p.turbo_enabled = 0u;
        p.default_fan_gear = APL_FAN_GEAR_2;
        p.dry_s = 2u; p.dry_fan_gear = APL_FAN_GEAR_3;
        p.filter_life_h = 1u; p.shortage_run_s = 2u;
        assert(APL_PROFILE_OK == apl_init_profile(&self, &ops, &host, NULL, NULL, &p));
        apl_link_bind(&self);
        apl_core_get_profile(&self.core, &readback);
        assert(readback.dry_s == 2u && readback.modules == 0u);
        p.dry_s = 999u; /* Caller may immediately reuse the source buffer. */
        apl_core_get_profile(&self.core, &readback);
        assert(readback.dry_s == 2u);
        ticks(&self, 201u);
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u));
        ticks(&self, APL_CORE_SENSOR_WARMUP_MS / APL_CORE_TICK_MS);
        apl_core_get_snapshot(&self.core, &snap);
        assert(snap.fan_output == APL_FAN_GEAR_2);
        assert(snap.purify_enabled == (mode & 1u));
        assert(snap.humidify_enabled == ((mode >> 1) & 1u));
        assert(snap.uv_enabled == 0u && snap.anion_enabled == 0u && snap.wifi_enabled == 0u);
        /* Runtime-disabled modules remain dark under the running backlight policy. */
        assert(0u == apl_hmi_get_key_lamp(&self.hmi, APL_HMI_KEY_PURIFY));
        assert((mode != 1u ? 255u : 0u) == apl_hmi_get_key_lamp(&self.hmi, APL_HMI_KEY_HUMIDITY));
        assert(255u == apl_hmi_get_key_lamp(&self.hmi, APL_HMI_KEY_TIMER));
        assert(host.uv_reads == 0u);
        assert((host.pm_reads != 0u) == (mode != 2u));
        assert((host.rh_reads != 0u) == (mode != 1u));
        assert(host.pump == (mode != 1u ? APL_SWITCH_ON : APL_SWITCH_OFF));
        assert(APL_LINK_REJECTED == cmd(&self, APL_LINK_CMD_UV_SET, 1u));
        assert(APL_LINK_REJECTED == cmd(&self, APL_LINK_CMD_ANION_SET, 1u));
        assert(APL_LINK_REJECTED == cmd(&self, APL_LINK_CMD_WIFI_PAIR, 0u));
        assert(APL_LINK_REJECTED == cmd(&self, APL_LINK_CMD_TURBO_TOGGLE, 0u));
        apl_core_set_fan_gear(&self.core, APL_FAN_GEAR_TURBO);
        assert(APL_FAN_GEAR_2 == apl_core_get_fan_gear(&self.core));
        assert(APL_LINK_REJECTED == cmd(&self, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_TURBO));
        memset(&memory, 0, sizeof(memory)); memory.filter_purify_s = 3600u; memory.filter_humidify_s = 3600u;
        memory.fan_gear = APL_FAN_GEAR_2; memory.module_state_valid = 1u; memory.uv_requested = 1u; memory.anion_requested = 1u;
        apl_core_memory_restore(&self.core, &memory);
        ticks(&self, 1u);
        apl_core_get_snapshot(&self.core, &snap);
        assert(snap.uv == APL_SWITCH_OFF && snap.anion == APL_SWITCH_OFF);
        if (mode != 2u) { assert(snap.filter_purify_left == 0u); }
        if (mode != 1u) { assert(snap.filter_humidify_left == 0u); }
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u));
        if (mode == 1u) { assert(APL_RUN_STANDBY == apl_core_get_run_state(&self.core)); }
        else
        {
            ticks(&self, 1u);
            apl_core_get_snapshot(&self.core, &snap);
            assert(snap.dry_left_s == 2u && snap.fan_output == APL_FAN_GEAR_3);
            ticks(&self, 200u);
            assert(APL_RUN_STANDBY == apl_core_get_run_state(&self.core));
        }
    }
    /* Re-enable modules using the same executable; check real callbacks and timeout. */
    memset(&host, 0, sizeof(host));
    apl_profile_defaults(&p);
    p.wifi_pair_s = 2u;
    assert(APL_PROFILE_OK == apl_init_profile(&self, &ops, &host, NULL, NULL, &p));
    apl_link_bind(&self);
    ticks(&self, 201u);
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_POWER, 0u));
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_UV_SET, 1u));
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_ANION_SET, 1u));
    ticks(&self, 1u);
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.uv == APL_SWITCH_ON && snap.anion == APL_SWITCH_ON && host.uv_reads != 0u);
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_WIFI_PAIR, 0u));
    ticks(&self, 100u);
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.wifi == APL_WIFI_PAIRING);
    ticks(&self, 100u);
    apl_core_get_snapshot(&self.core, &snap);
    assert(snap.wifi == APL_WIFI_OFF);
    display_rotation();
    product_pid();
    fan_targets();
    puts("PASS: missing profile defaults, runtime profiles, atomic validation, memory, timing, fan target RPM mapping, callback/poll getter, automatic/humidity/fallback/drying and restored RPM");
    return 0;
}
