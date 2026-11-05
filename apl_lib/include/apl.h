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
#define APL_VERSION_MINOR   3
#define APL_VERSION_PATCH   0
#define APL_VERSION_STRING  "0.3.0"
#define APL_VERSION_NUM \
    ((APL_VERSION_MAJOR << 16) | (APL_VERSION_MINOR << 8) | APL_VERSION_PATCH)

/** 推荐宿主入口 apl_tick_1ms 的调用周期；业务仍按 10ms 推进。 */
#define APL_TICK_MS 1u

/** 使用方声明它来同时持有主控和人机。库内没有 static 实例。 */
struct apl
{
    struct apl_core core;
#if APL_COMPILE_HMI
    struct apl_hmi hmi;
#endif
    uint8_t tick_elapsed_ms; /**< 库内调度余数，宿主不得读写。 */
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
              void *hmi_ctx) APL_CALL;

/**
 * @brief 推进一拍
 * @param self 组合对象。空指针则什么都不做
 *
 * 使用方每 APL_CORE_TICK_MS 调用一次。顺序是 apl_core_tick，取主控快照交给人机，
 * 然后 apl_hmi_tick。未调用 apl_link_bind 时，显示映射仍由使用方用 getter 和 setter 完成。
 */
void apl_tick(struct apl *self) APL_CALL;

/**
 * @brief 推荐入口：宿主每 1ms 调用一次
 *
 * 每个实例独立累计，满 10ms 后推进一次 apl_tick；前 9 次不采样或调用硬件。
 * 初始化清零余数，自检、按键、传感器和定时仍使用原有 10ms 业务节拍。
 * 与 apl_tick / apl_core_tick / apl_hmi_tick 任选一种调度方式，不得混调。
 * 在前台串行调用，不从中断或回调重入；空指针不做任何操作。
 */
void apl_tick_1ms(struct apl *self) APL_CALL;

/**
 * @brief 把人机的操作接到主控，并让人机按快照绘制
 * @param self 组合对象。空指针则什么都不做
 *
 * 接上后六颗键不再调用使用方的键回调，见 apl_hmi_link_bind。
 */
void apl_link_bind(struct apl *self) APL_CALL;

/** 冷启动加载整表；profile为空时使用固件缺省配置并返回OK。
 * 非空表校验失败时self保持原样。成功后仍需apl_link_bind。
 * 调用前停止tick并确保物理负载已关闭；不处理传输、持久化或在线热切换。 */
enum apl_profile_result apl_init_profile(struct apl *self,
    const struct apl_core_ops *core_ops, void *core_ctx,
    const struct apl_hmi_ops *hmi_ops, void *hmi_ctx,
    const struct apl_profile *profile) APL_CALL;

#endif /* APL_H */
