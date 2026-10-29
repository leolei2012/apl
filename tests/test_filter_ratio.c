/** Independent uint64_t oracle exercises the real core memory/snapshot API. */
#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct apl_core s_core;
static uint32_t s_cases;

static void check(uint32_t used)
{
    struct apl_core_memory memory;
    struct apl_link_snapshot snapshot;
    uint64_t life = (uint64_t)APL_CORE_FILTER_LIFE_H * 3600u;
    uint8_t expected;

    memset(&memory, 0, sizeof(memory));
    memory.filter_purify_s = used;
    memory.filter_humidify_s = used;
    apl_core_memory_restore(&s_core, &memory);
    apl_core_get_snapshot(&s_core, &snapshot);
    expected =
        used >= life ? 0u : (uint8_t)(((life - used) * 99u + life - 1u) / life);
#if APL_COMPILE_PURIFY
    assert(snapshot.filter_purify_left == expected);
#endif
#if APL_COMPILE_HUMIDIFY
    assert(snapshot.filter_humidify_left == expected);
#endif
    s_cases++;
}

int main(void)
{
    uint32_t life = (uint32_t)APL_CORE_FILTER_LIFE_H * 3600u;
    uint32_t random = 8673u;
    unsigned index;

    apl_core_init(&s_core, NULL, NULL);
    check(0u);
    check(1u);
    check(life - 1u);
    check(life);
    check(UINT32_MAX);
    for (index = 0u; index <= 99u; index++)
    {
        uint32_t edge = (uint32_t)((uint64_t)life * index / 99u);
        check(edge);
        if (edge != 0u)
        {
            check(edge - 1u);
        }
        if (edge != UINT32_MAX)
        {
            check(edge + 1u);
        }
    }
    for (index = 0u; index < 5000u; index++)
    {
        random = random * 1664525u + 1013904223u;
        check(random);
    }
    printf("PASS: core filter percentage, life=%lu hours, %lu independent "
           "oracle cases\n",
           (unsigned long)APL_CORE_FILTER_LIFE_H, (unsigned long)s_cases);
    return 0;
}
