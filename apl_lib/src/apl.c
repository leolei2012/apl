/**
 * @file    apl.c
 * @brief   组合根。把主控和人机放进同一个对象，只做初始化和按拍推进。
 */

#include "apl.h"
#include "apl_internal.h"

#include <stddef.h>
#include <string.h>

APL_STATIC_ASSERT(APL_CORE_TICK_MS == APL_HMI_TICK_MS, "主控和人机使用同一拍长");
APL_STATIC_ASSERT(APL_VERSION_NUM == 0x000300u, "版本号与分段宏不一致");
#if APL_COMPILE_HMI
APL_STATIC_ASSERT(offsetof(struct apl, hmi) == sizeof(struct apl_core),
               "主控和人机之间不插入填充");
#endif
APL_STATIC_ASSERT(APL_TICK_MS == 1u, "推荐宿主入口每 1ms 调用");
APL_STATIC_ASSERT((APL_CORE_TICK_MS % APL_TICK_MS) == 0u
                  && APL_CORE_TICK_MS <= 255u, "分频周期可存入单字节");

static void apl_scheduler_reset(struct apl *self) APL_CALL
{
    /* Include native tail padding so both initialization APIs leave identical bytes. */
    memset((unsigned char *)self + offsetof(struct apl, tick_elapsed_ms), 0,
           sizeof(*self) - offsetof(struct apl, tick_elapsed_ms));
}

void apl_init(struct apl *self,
              const struct apl_core_ops *core_ops,
              void *core_ctx,
              const struct apl_hmi_ops *hmi_ops,
              void *hmi_ctx) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_core_init(&self->core, core_ops, core_ctx);
#if APL_COMPILE_HMI
    apl_hmi_init(&self->hmi, hmi_ops, hmi_ctx);
#if APL_HMI_COMPILE_KEYMAP
    apl_hmi_set_keymap(&self->hmi, apl_core_keymap_target(&self->core));
#endif
#else
    APL_UNUSED_PARAM(hmi_ops);
    APL_UNUSED_PARAM(hmi_ctx);
#endif
    apl_scheduler_reset(self);
}

/** 人机交出的操作原样交给主控。ctx 是主控对象。 */
#if APL_COMPILE_HMI
static enum apl_link_result apl_link_send(const struct apl_link_command *command, void *ctx) APL_CALL
{
    return apl_core_command((struct apl_core *)ctx, command);
}
#endif

void apl_link_bind(struct apl *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

#if APL_COMPILE_HMI
    apl_hmi_link_bind(&self->hmi, apl_link_send, &self->core);
#if APL_HMI_COMPILE_KEYMAP
    apl_hmi_set_keymap(&self->hmi, apl_core_keymap_target(&self->core));
#endif
#endif
}

void apl_tick(struct apl *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_core_tick(&self->core);
#if APL_COMPILE_HMI
    apl_core_get_snapshot(&self->core, apl_hmi_snapshot_target(&self->hmi));
    apl_hmi_snapshot_commit(&self->hmi);
    apl_hmi_tick(&self->hmi);
#endif
}

void apl_tick_1ms(struct apl *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }
    self->tick_elapsed_ms += APL_TICK_MS;
    if (self->tick_elapsed_ms >= APL_CORE_TICK_MS)
    {
        self->tick_elapsed_ms = 0u;
        apl_tick(self);
    }
}

enum apl_profile_result apl_init_profile(struct apl *self,
    const struct apl_core_ops *core_ops, void *core_ctx,
    const struct apl_hmi_ops *hmi_ops, void *hmi_ctx,
    const struct apl_profile *profile) APL_CALL
{
    enum apl_profile_result result;
    if (NULL == self) { return APL_PROFILE_INVALID_ARGUMENT; }
    result = apl_core_init_profile(&self->core, core_ops, core_ctx, profile);
    if (APL_PROFILE_OK != result) { return result; }
#if APL_COMPILE_HMI
    apl_hmi_init(&self->hmi, hmi_ops, hmi_ctx);
#if APL_HMI_COMPILE_KEYMAP
    apl_hmi_set_keymap(&self->hmi, apl_core_keymap_target(&self->core));
#endif
#else
    APL_UNUSED_PARAM(hmi_ops);
    APL_UNUSED_PARAM(hmi_ctx);
#endif
    apl_scheduler_reset(self);
    return APL_PROFILE_OK;
}
