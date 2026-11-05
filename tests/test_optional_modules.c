#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct host
{
    uint8_t interlock;
    uint8_t faults;
    unsigned reads[5];
    unsigned fault_reads[3];
    enum apl_switch uv;
    enum apl_switch anion;
};
static enum apl_water_level water(void *ctx) { (void)ctx; return APL_WATER_LEVEL_3; }
static uint16_t pm25(void *ctx) { ((struct host *)ctx)->reads[0]++; return 10u; }
static uint16_t pm10(void *ctx) { ((struct host *)ctx)->reads[1]++; return 10u; }
static uint16_t hcho(void *ctx) { ((struct host *)ctx)->reads[2]++; return 10u; }
static uint16_t tvoc(void *ctx) { ((struct host *)ctx)->reads[3]++; return 10u; }
static uint8_t humidity(void *ctx) { ((struct host *)ctx)->reads[4]++; return 45u; }
static uint8_t interlock(void *ctx) { return ((struct host *)ctx)->interlock; }
static uint8_t fault(enum apl_module module, void *ctx)
{
    struct host *self = ctx; self->fault_reads[module]++; return (uint8_t)(self->faults & (1u << (unsigned)module));
}
static void uv(enum apl_switch value, void *ctx) { ((struct host *)ctx)->uv = value; }
static void anion(enum apl_switch value, void *ctx) { ((struct host *)ctx)->anion = value; }
static const struct apl_core_ops ops = {.get_water_level = water, .get_pm25 = pm25,
    .get_humidity = humidity, .get_pm10 = pm10, .get_hcho = hcho, .get_tvoc = tvoc,
    .get_uv_interlock = interlock, .set_uv = uv, .set_anion = anion, .get_module_fault = fault};
static void tick(struct apl_core *self, unsigned n) { while (n-- != 0u) { apl_core_tick(self); } }
static enum apl_link_result cmd(struct apl_core *self, enum apl_link_command_type type, uint8_t value)
{
    struct apl_link_command c = {type, value}; return apl_core_command(self, &c);
}
static struct apl_link_snapshot snapshot(struct apl_core *self)
{
    struct apl_link_snapshot s; apl_core_get_snapshot(self, &s); return s;
}
int main(void)
{
    struct apl_core self; struct host host; struct apl_core_memory memory;
    memset(&host, 0, sizeof(host)); host.interlock = 1u;
    apl_core_init(&self, &ops, &host); apl_core_power_on(&self); tick(&self, APL_CORE_SENSOR_WARMUP_MS / APL_CORE_TICK_MS);
    assert((APL_COMPILE_SENSOR_PM25 != 0) == (host.reads[0] > 0u));
    assert((APL_COMPILE_SENSOR_PM10 != 0) == (host.reads[1] > 0u));
    assert((APL_COMPILE_SENSOR_HCHO != 0) == (host.reads[2] > 0u));
    assert((APL_COMPILE_SENSOR_TVOC != 0) == (host.reads[3] > 0u));
    assert((APL_COMPILE_SENSOR_HUMIDITY != 0) == (host.reads[4] > 0u));
    assert((APL_COMPILE_UV != 0) == (host.fault_reads[0] > 0u));
    assert((APL_COMPILE_ANION != 0) == (host.fault_reads[1] > 0u));
    assert((APL_COMPILE_WIFI != 0) == (host.fault_reads[2] > 0u));
    assert((APL_COMPILE_UV ? APL_LINK_OK : APL_LINK_REJECTED) == cmd(&self, APL_LINK_CMD_UV_SET, 1u));
    tick(&self, 1u); assert((APL_COMPILE_UV ? APL_SWITCH_ON : APL_SWITCH_OFF) == host.uv);
    assert(APL_SWITCH_OFF == host.anion);
    assert((APL_COMPILE_ANION ? APL_LINK_OK : APL_LINK_REJECTED) == cmd(&self, APL_LINK_CMD_ANION_SET, 1u));
    tick(&self, 1u); assert((APL_COMPILE_ANION ? APL_SWITCH_ON : APL_SWITCH_OFF) == host.anion);
    assert(APL_FAN_GEAR_1 == snapshot(&self).fan_gear);
#if APL_COMPILE_UV
    host.interlock = 0u; tick(&self, 1u); assert(APL_SWITCH_OFF == host.uv);
    assert(1u == snapshot(&self).uv_fault);
    host.interlock = 1u; tick(&self, 1u); assert(APL_SWITCH_OFF == host.uv);
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_UV_SET, 1u)); tick(&self, 1u); assert(APL_SWITCH_ON == host.uv);
    host.faults = 1u; tick(&self, 1u); assert(APL_SWITCH_OFF == host.uv);
    host.faults = 0u; assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_UV_SET, 1u)); tick(&self, 1u); assert(APL_SWITCH_ON == host.uv);
#endif
#if APL_COMPILE_ANION
    host.faults = 2u; tick(&self, 1u); assert(APL_SWITCH_OFF == host.anion);
    host.faults = 0u; tick(&self, 1u); assert(APL_SWITCH_ON == host.anion);
    assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_ANION_SET, 0u)); tick(&self, 1u);
#endif
    assert((APL_COMPILE_WIFI ? APL_LINK_OK : APL_LINK_REJECTED) == cmd(&self, APL_LINK_CMD_WIFI_PAIR, 0u));
#if APL_COMPILE_WIFI
    apl_core_set_wifi_connected(&self, 1u); assert(APL_WIFI_CONNECTED == snapshot(&self).wifi);
#else
    assert(APL_WIFI_OFF == snapshot(&self).wifi);
#endif
    host.faults = 4u; tick(&self, 1u); assert(APL_WIFI_OFF == snapshot(&self).wifi); host.faults = 0u;
    /* Independent requests survive memory round trip; no raw-layout persistence is implied. */
    apl_core_get_memory(&self, &memory); assert(1u == memory.module_state_valid);
    apl_core_init(&self, &ops, &host); apl_core_memory_restore(&self, &memory); tick(&self, 1u);
    assert((APL_COMPILE_UV ? APL_SWITCH_ON : APL_SWITCH_OFF) == host.uv);
    assert(APL_SWITCH_OFF == host.anion);
#if APL_COMPILE_UV
    {
        struct apl_core_ops missing = ops;
        missing.get_uv_interlock = NULL;
        apl_core_init(&self, &missing, &host); apl_core_power_on(&self);
        assert(APL_LINK_OK == cmd(&self, APL_LINK_CMD_UV_SET, 1u)); tick(&self, 1u);
        assert(APL_SWITCH_OFF == snapshot(&self).uv);
    }
#endif
    {
        struct apl bundle;
        apl_init(&bundle, &ops, &host, NULL, NULL); apl_link_bind(&bundle);
        apl_core_power_on(&bundle.core);
        {
            unsigned n;
            for (n = 0u; n < 210u; n++) { apl_tick(&bundle); }
        }
#if !APL_COMPILE_UV && !APL_COMPILE_ANION
        assert(0u == apl_hmi_get_key_lamp(&bundle.hmi, APL_HMI_KEY_PURIFY));
#endif
#if !APL_COMPILE_SENSOR_HUMIDITY && !APL_COMPILE_PURIFY
        assert(0u == apl_hmi_get_key_lamp(&bundle.hmi, APL_HMI_KEY_HUMIDITY));
#endif
#if !APL_COMPILE_WIFI
        assert(0u == apl_hmi_get_icon(&bundle.hmi, APL_HMI_ICON_WIFI));
#endif
    }
    puts("PASS: independent module requests, compile gates, sensor reads, missing UV interlock, host faults and memory requests");
    return 0;
}
