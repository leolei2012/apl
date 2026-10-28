/* Private host protocol, compiled with the same generated product sources as the SDK. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static int powered = 1;
static unsigned long long clock_ms;
static void sim_step(void);
static void sim_boot(void);
static void sim_drop(void);
static void sim_clear_memory(void);
static void sim_input(int key, int value);
static void sim_snapshot(void);
static void sim_event(const char *kind, const char *key, int value)
{
    printf("{\"event\":{\"kind\":\"%s\",\"key\":\"%s\",\"value\":%d,\"ms\":%llu}}\n", kind, key, value, clock_ms);
}
static void run_ticks(int count)
{
    for (int i = 0; i < count; ++i) {
        clock_ms += 10u;
        if (powered) sim_step();
    }
}
static void reply(unsigned id)
{
    printf("{\"id\":%u,\"snapshot\":{\"powered\":%s,\"ms\":%llu,", id, powered ? "true" : "false", clock_ms);
    sim_snapshot();
    puts("}}");
    fflush(stdout);
}
/* ADAPTER */
int main(void)
{
    char line[256];
    setvbuf(stdout, NULL, _IOLBF, 0);
    sim_boot();
    reply(0);
    while (fgets(line, sizeof(line), stdin)) {
        unsigned id; int op, a, b;
        if (sscanf(line, "%u %d %d %d", &id, &op, &a, &b) != 4) return 2;
        if (op == 0 && a >= 0 && a <= 6000) run_ticks(a);
        else if (op == 1) {
            if (a && !powered) { powered = 1; sim_boot(); }
            else if (!a && powered) { powered = 0; sim_drop(); }
        } else if (op == 2) {
            sim_drop(); sim_clear_memory(); clock_ms = 0u; powered = 1; sim_boot();
        } else if (op == 3) sim_input(a, b);
        else return 3;
        reply(id);
    }
    return 0;
}
