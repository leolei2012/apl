/**
 * @file test_output_trace.c
 * @brief Deterministic public-state and callback trace. See compare_output_trace.ps1.
 * Every callback reads the snapshot before returning; struct padding is not compared.
 */
#include "apl.h"
#include <stdio.h>
#include <string.h>

struct trace_host
{
    struct apl_core core;
    struct apl_core_memory saved;
    unsigned id;
    unsigned tick;
    uint16_t pm25;
    uint8_t humidity;
    uint8_t interlock;
    enum apl_water_level water;
};

static void trace(struct trace_host *self, const char *event, unsigned arg)
{
    struct apl_link_snapshot s;
    struct apl_core_memory m;
    apl_core_get_snapshot(&self->core, &s);
    apl_core_get_memory(&self->core, &m);
    printf("%u:%u:%s:%u:%u|", self->tick, self->id, event, arg,
           (unsigned)apl_core_get_power(&self->core));
#define FIELD(name) printf("%u,", (unsigned)s.name)
    FIELD(run_state); FIELD(fan_gear); FIELD(fan_output); FIELD(aq_level);
    FIELD(water_level); FIELD(water_alert); FIELD(wifi); FIELD(uv); FIELD(anion);
    FIELD(pump); FIELD(beep); FIELD(timer_left_s); FIELD(dry_left_s); FIELD(pm25);
    FIELD(beep_seq); FIELD(purify_enabled); FIELD(humidify_enabled); FIELD(purify_on);
    FIELD(humidify_on); FIELD(humidity); FIELD(humidity_target); FIELD(sleep);
    FIELD(filter_purify_left); FIELD(filter_humidify_left); FIELD(filter_reset_seq);
    FIELD(filter_reset_forced); FIELD(uv_fault); FIELD(factory_done);
#undef FIELD
    printf("|%u,%u,%u,%u,%u,%u,%u,%u\n", (unsigned)m.filter_purify_s,
           (unsigned)m.filter_humidify_s, (unsigned)m.fan_gear, (unsigned)m.running,
           (unsigned)m.used, (unsigned)m.purify_on, (unsigned)m.humidify_on,
           (unsigned)m.factory_pending);
}

static enum apl_water_level water(void *ctx)
{
    struct trace_host *self = ctx;
    trace(self, "read-water", (unsigned)self->water);
    return self->water;
}
static uint16_t pm25(void *ctx)
{
    struct trace_host *self = ctx;
    trace(self, "read-pm25", self->pm25);
    return self->pm25;
}
static uint8_t humidity(void *ctx)
{
    struct trace_host *self = ctx;
    trace(self, "read-rh", self->humidity);
    return self->humidity;
}
static uint8_t interlock(void *ctx)
{
    struct trace_host *self = ctx;
    trace(self, "read-interlock", self->interlock);
    return self->interlock;
}
static void fan(enum apl_switch on, enum apl_fan_gear gear, void *ctx)
{
    trace(ctx, "fan", (unsigned)on * 10u + (unsigned)gear);
}
static void pump(enum apl_switch on, void *ctx)
{
    trace(ctx, "pump", (unsigned)on);
}
static void uv(enum apl_switch on, void *ctx)
{
    trace(ctx, "uv", (unsigned)on);
}
static void anion(enum apl_switch on, void *ctx)
{
    trace(ctx, "anion", (unsigned)on);
}
static void trigger(struct apl_core *core, void *ctx)
{
    (void)core;
    trace(ctx, "water-trigger", 0u);
}
static void recover(struct apl_core *core, void *ctx)
{
    (void)core;
    trace(ctx, "water-recover", 0u);
}
static void power_off(struct apl_core *core, void *ctx)
{
    (void)core;
    trace(ctx, "power-off", 0u);
}
static void save(const struct apl_core_memory *m, void *ctx)
{
    struct trace_host *self = ctx;
    self->saved = *m;
    trace(self, "save", 0u);
}
static void timer(struct apl_core *core, void *ctx)
{
    trace(ctx, "timer", 0u);
    apl_core_power_off(core);
}
static const struct apl_core_ops ops = {
    .get_water_level = water, .get_pm25 = pm25, .get_humidity = humidity,
    .get_uv_interlock = interlock, .set_fan = fan, .set_pump = pump,
    .set_uv = uv, .set_anion = anion, .memory_save = save,
    .water_on_trigger = trigger, .water_on_recover = recover, .on_power_off = power_off
};

static void command(struct trace_host *self, enum apl_link_command_type type, uint8_t arg)
{
    struct apl_link_command c = { type, arg };
    int result = apl_core_command(&self->core, &c);
    printf("command:%u:%u:%u:%d\n", self->id, (unsigned)type, (unsigned)arg, result);
    trace(self, "command-result", 0u);
}

static void advance(struct trace_host *a, struct trace_host *b, unsigned ticks)
{
    unsigned index;
    for (index = 0u; index < ticks; index++)
    {
        a->tick++;
        b->tick++;
        /* Swap execution order each tick to expose accidental shared state. */
        if (0u != (a->tick % 2u))
        {
            apl_core_tick(&a->core); trace(a, "tick", 0u);
            apl_core_tick(&b->core); trace(b, "tick", 0u);
        }
        else
        {
            apl_core_tick(&b->core); trace(b, "tick", 0u);
            apl_core_tick(&a->core); trace(a, "tick", 0u);
        }
    }
}

int main(void)
{
    struct trace_host a;
    struct trace_host b;
    struct apl_core_memory memory;
    struct apl_core_ops sparse = ops;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.id = 1u; b.id = 2u;
    a.water = b.water = APL_WATER_LEVEL_3;
    a.interlock = b.interlock = 1u;
    a.humidity = 25u; b.humidity = 65u;
    a.pm25 = 12u; b.pm25 = 170u;
    printf("abi:%zu,%zu,%zu,%zu,%zu,%zu\n", sizeof(struct apl), sizeof(struct apl_core),
           sizeof(struct apl_hmi), sizeof(struct apl_core_ops),
           sizeof(struct apl_link_snapshot), sizeof(struct apl_core_memory));
    apl_core_init(&a.core, &ops, &a);
    apl_core_init(&b.core, &ops, &b);
    advance(&a, &b, 3u); /* First standby tick must not call fan-off. */
    command(&a, APL_LINK_CMD_POWER, 0u);
    command(&b, APL_LINK_CMD_POWER, 0u);
    command(&a, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
    command(&b, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
    command(&a, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO);
    command(&b, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_2);
    advance(&a, &b, 101u);
    a.humidity = 60u; a.pm25 = 80u; b.humidity = 25u; b.pm25 = 0u;
    advance(&a, &b, 101u);
    command(&a, APL_LINK_CMD_SLEEP_TOGGLE, 0u);
    command(&b, APL_LINK_CMD_TURBO_TOGGLE, 0u);
    advance(&a, &b, 10u);
    command(&a, APL_LINK_CMD_SLEEP_TOGGLE, 0u);
    command(&b, APL_LINK_CMD_TURBO_TOGGLE, 0u);
    command(&b, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO);
    command(&a, APL_LINK_CMD_HUMIDITY_TARGET, 60u);
    command(&b, APL_LINK_CMD_HUMIDITY_TARGET, 70u);
    a.humidity = 45u; b.humidity = 25u;
    advance(&a, &b, 101u);
    a.humidity = 60u; b.humidity = 70u; advance(&a, &b, 101u);
    a.humidity = 58u; b.humidity = 68u; advance(&a, &b, 101u);
    a.humidity = 57u; b.humidity = 67u; advance(&a, &b, 101u);
    a.interlock = 0u; b.water = APL_WATER_LEVEL_0;
    advance(&a, &b, 101u);
    b.water = APL_WATER_LEVEL_2; advance(&a, &b, 1u);
    a.interlock = 1u;
    command(&a, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
    command(&a, APL_LINK_CMD_PURIFY_TOGGLE, 0u);
    a.water = APL_WATER_LEVEL_0;
    advance(&a, &b, 401u); /* Shortened test configuration: shortage -> dry. */
    advance(&a, &b, 301u); /* Dry -> standby. */
    a.water = APL_WATER_LEVEL_3;
    command(&a, APL_LINK_CMD_POWER, 0u);
    command(&a, APL_LINK_CMD_HUMIDITY_TARGET, APL_HUMIDITY_TARGET_OFF);
    advance(&a, &b, 101u);
    apl_core_get_memory(&a.core, &memory);
    trace(&a, "power-cut", 0u);
    apl_core_init(&a.core, &ops, &a);
    apl_core_memory_restore(&a.core, &memory);
    advance(&a, &b, 101u);
    apl_core_set_timer(&b.core, 2u, timer, &b);
    advance(&a, &b, 201u);
    command(&a, APL_LINK_CMD_POWER, 0u); /* Normal shutdown, possibly dry. */
    advance(&a, &b, 1u);
    command(&a, APL_LINK_CMD_DRY_STOP, 0u);
    advance(&a, &b, 2u);
    /* Optional output callbacks and sensor callbacks may be absent. */
    sparse.set_fan = NULL; sparse.set_pump = NULL; sparse.set_uv = NULL;
    sparse.set_anion = NULL; sparse.get_humidity = NULL; sparse.get_uv_interlock = NULL;
    apl_core_init(&b.core, &sparse, &b);
    command(&b, APL_LINK_CMD_POWER, 0u);
    command(&b, APL_LINK_CMD_FAN_SET, APL_FAN_GEAR_AUTO);
    advance(&a, &b, 101u);
    apl_core_init(&a.core, NULL, &a);
    command(&a, APL_LINK_CMD_POWER, 0u);
    advance(&a, &b, 101u);
    return ferror(stdout) ? 1 : 0;
}
