/**
 * @file    apl.c
 * @brief   组合根。把主控和人机放进同一个对象，只做初始化和按拍推进。
 */

#include "apl.h"

#include <stddef.h>

_Static_assert(APL_CORE_TICK_MS == APL_HMI_TICK_MS, "主控和人机使用同一拍长");
_Static_assert(APL_VERSION_NUM == 0x000100u, "版本号与分段宏不一致");
_Static_assert(sizeof(struct apl) == sizeof(struct apl_core) + sizeof(struct apl_hmi),
               "组合对象中间不插入填充");

void apl_init(struct apl *self,
              const struct apl_core_ops *core_ops,
              void *core_ctx,
              const struct apl_hmi_ops *hmi_ops,
              void *hmi_ctx)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_init(&self->core, core_ops, core_ctx);
    apl_hmi_init(&self->hmi, hmi_ops, hmi_ctx);
}

void apl_tick(struct apl *self)
{
    if (NULL == self)
    {
        return;
    }

    apl_core_tick(&self->core);
    apl_hmi_tick(&self->hmi);
}
