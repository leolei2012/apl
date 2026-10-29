/** Periodic memory notifications only; never writes physical NVM. */
#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned saves;
static struct apl_core_memory latest;
static void save(const struct apl_core_memory *memory, void *ctx) { (void)ctx; latest=*memory; saves++; }
static enum apl_water_level water(void *ctx) { (void)ctx; return APL_WATER_LEVEL_3; }
static const struct apl_core_ops ops={.memory_save=save,.get_water_level=water};
static void ticks(struct apl_core *self, unsigned long n) { while(n--) apl_core_tick(self); }
int main(void)
{
    struct apl_core self;
    unsigned long elapsed;
    apl_core_init(&self,&ops,NULL); apl_core_power_on(&self); ticks(&self,110u);
    saves=0u; ticks(&self,5900u);
    if(saves!=0u) { printf("FAIL: %u unexpected periodic saves before first hour\n",saves); return 1; }
    apl_core_get_memory(&self,&latest);
    elapsed=APL_COMPILE_PURIFY ? latest.filter_purify_s : latest.filter_humidify_s;
    assert(elapsed==60u);
    ticks(&self,(3600UL-elapsed)*100UL); assert(saves==1u);
    ticks(&self,100u); assert(saves==1u);
    assert((APL_COMPILE_PURIFY ? latest.filter_purify_s : latest.filter_humidify_s)==3600UL);
    puts("PASS: disabled counter zero cannot trigger saves; active hour boundary sends one notification");
    return 0;
}
