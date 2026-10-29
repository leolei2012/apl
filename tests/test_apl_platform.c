/** Platform profile and config-constant branches; no behavioral specification
 * change. */
#include "apl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

APL_STATIC_ASSERT(sizeof(uint8_t) == 1u, "uint8");
APL_STATIC_ASSERT(sizeof(uint16_t) == 2u, "uint16");
APL_STATIC_ASSERT(sizeof(uint32_t) == 4u, "uint32");
APL_STATIC_ASSERT(APL_VERSION_NUM == 0x000103u, "development patch version");

static void key_sample(struct apl_hmi_key_state *state, void *ctx) APL_CALL
{
    APL_UNUSED_PARAM(ctx);
    memset(state, 0, sizeof(*state));
}

int main(void)
{
    struct apl instance;
    struct apl_hmi_ops hmi_ops;
    struct apl_core_memory memory;
    struct apl_link_command command;
    enum apl_link_result result;

    memset(&hmi_ops, 0, sizeof(hmi_ops));
    hmi_ops.get_key_state = key_sample;
    apl_init(&instance, NULL, NULL, &hmi_ops, NULL);
    apl_link_bind(&instance);
    memset(&memory, 0, sizeof(memory));
    memory.filter_purify_s = 200u;
    memory.filter_humidify_s = 100u;
    apl_core_memory_restore(&instance.core, &memory);
    command.type = APL_LINK_CMD_FILTER_RESET;
    command.arg = 0u;
    assert(apl_core_command(&instance.core, &command) == APL_LINK_OK);
    apl_core_get_memory(&instance.core, &memory);
#if APL_COMPILE_PURIFY && APL_COMPILE_HUMIDIFY && (APL_CORE_FILTER_SHARED == 0u)
    assert(memory.filter_purify_s == 0u && memory.filter_humidify_s == 100u);
#else
#if APL_COMPILE_PURIFY
    assert(memory.filter_purify_s == 0u);
#endif
#if APL_COMPILE_HUMIDIFY
    assert(memory.filter_humidify_s == 0u);
#endif
#endif
    apl_core_power_on(&instance.core);
    command.type = APL_LINK_CMD_TURBO_TOGGLE;
    result = apl_core_command(&instance.core, &command);
#if (APL_CORE_HAS_TURBO == 0u)
    assert(result == APL_LINK_REJECTED);
#else
    assert(result == APL_LINK_OK);
    assert(apl_core_get_fan_gear(&instance.core) == APL_FAN_GEAR_TURBO);
#endif
    puts("PASS: platform widths, callback ABI spelling, shared/independent "
         "reset and Turbo compilation profile");
    return 0;
}
