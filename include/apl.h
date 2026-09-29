/**
 * @file    apl.h
 * @brief   组合对象。使用方持有一份，库内不创建实例。
 *
 * `struct apl_core` 与 `struct apl_hmi` 互不包含，也不互相包含头文件。
 * 把两边放进同一个对象、完成初始化和按拍推进，只发生在 apl.c。
 * 编译时把头文件搜索路径指到 `include` 和 `config`。
 */

#ifndef APL_H
#define APL_H

#include "apl_type.h"
#include "apl_core.h"
#include "apl_hmi.h"

#define APL_VERSION_MAJOR   0
#define APL_VERSION_MINOR   1
#define APL_VERSION_PATCH   0
#define APL_VERSION_STRING  "0.1.0"
#define APL_VERSION_NUM \
    ((APL_VERSION_MAJOR << 16) | (APL_VERSION_MINOR << 8) | APL_VERSION_PATCH)

/** 使用方声明它来同时持有主控和人机。库内没有 static 实例。 */
struct apl
{
    struct apl_core core;
    struct apl_hmi hmi;
};

/**
 * @brief 初始化主控和人机
 * @param self 组合对象。由使用方持有。空指针则什么都不做
 * @param core_ops 主控硬件表。可以为空
 * @param core_ctx 调用主控 ops 时原样传回
 * @param hmi_ops 人机按键采样表。可以为空
 * @param hmi_ctx 调用按键采样时原样传回
 */
void apl_init(struct apl *self,
              const struct apl_core_ops *core_ops,
              void *core_ctx,
              const struct apl_hmi_ops *hmi_ops,
              void *hmi_ctx);

/**
 * @brief 推进一拍
 * @param self 组合对象。空指针则什么都不做
 *
 * 使用方每 APL_CORE_TICK_MS 调用一次。顺序是 apl_core_tick，然后 apl_hmi_tick。
 * 显示映射仍由使用方在这一拍之后用 getter 和 setter 完成。
 */
void apl_tick(struct apl *self);

#endif /* APL_H */
