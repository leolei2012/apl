/**
 * @file    apl.c
 * @brief   组合根。把主控和人机放进同一个对象，只做初始化和按拍推进。
 */

#include "apl.h"

#include <stddef.h>

APL_STATIC_ASSERT(APL_CORE_TICK_MS == APL_HMI_TICK_MS, "主控和人机使用同一拍长");
APL_STATIC_ASSERT(APL_VERSION_NUM == 0x000202u, "版本号与分段宏不一致");
APL_STATIC_ASSERT(sizeof(struct apl) == sizeof(struct apl_core) + sizeof(struct apl_hmi),
               "组合对象中间不插入填充");

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
    apl_hmi_init(&self->hmi, hmi_ops, hmi_ctx);
}

/** 人机交出的操作原样交给主控。ctx 是主控对象。 */
static enum apl_link_result apl_link_send(const struct apl_link_command *command, void *ctx) APL_CALL
{
    return apl_core_command((struct apl_core *)ctx, command);
}

void apl_link_bind(struct apl *self) APL_CALL
{
    if (NULL == self)
    {
        return;
    }

    apl_hmi_link_bind(&self->hmi, apl_link_send, &self->core);
}

void apl_tick(struct apl *self) APL_CALL
{
    struct apl_link_snapshot snapshot;

    if (NULL == self)
    {
        return;
    }

    apl_core_tick(&self->core);
    apl_core_get_snapshot(&self->core, &snapshot);
    apl_hmi_set_snapshot(&self->hmi, &snapshot);
    apl_hmi_tick(&self->hmi);
}
