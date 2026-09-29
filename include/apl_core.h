/**
 * @file    apl_core.h
 * @brief   主控对象。业务判断与执行器决定，库内不创建实例。
 *
 * 编译时把 `config` 加入头文件搜索路径，以便包含 `apl_config.h`。
 * 整机状态、开关、档位和水位段数在 `apl_type.h`。
 * 宿主与主控交换数据只走本头的 getter、setter，以及初始化时注入的 ops。
 * `struct apl_core` 里的字节只给使用方分配实例。
 */

#ifndef APL_CORE_H
#define APL_CORE_H

#include <stdint.h>

#include "apl_config.h"
#include "apl_type.h"

#define APL_CORE_VERSION_MAJOR  0
#define APL_CORE_VERSION_MINOR  1
#define APL_CORE_VERSION_PATCH  0
#define APL_CORE_VERSION_STRING "0.1.0"
#define APL_CORE_VERSION_NUM \
    ((APL_CORE_VERSION_MAJOR << 16) | (APL_CORE_VERSION_MINOR << 8) | APL_CORE_VERSION_PATCH)

/** 宿主调用 apl_core_tick 的周期。与人机拍长相同，两边不互相包含。 */
#define APL_CORE_TICK_MS            10u

/** 1 秒折合的拍数。剩余秒数每这么多拍减 1。 */
#define APL_CORE_TICKS_PER_SECOND   (1000u / APL_CORE_TICK_MS)

/** 使用方按这个大小持有对象。布局不对外，读写都走 getter 和 setter。 */
#define APL_CORE_OPAQUE_SIZE        128u

struct apl_core;

/**
 * 宿主注入的硬件访问。函数指针为空则不调用，也不报警。
 * 对象存活期间 ops 指向的内容必须一直有效。
 * UV、负离子何时开关尚未规定，因此库目前不调用 uv_set、anion_set，
 * 也不另提供读取它们的 getter。
 */
struct apl_core_ops
{
    enum apl_water_level (*water_level_get)(void *ctx);
    uint16_t (*pm25_get)(void *ctx);        /**< μg/m³，整数 */
    uint8_t (*humidity_get)(void *ctx);     /**< 相对湿度，整数百分比 */
    int16_t (*temperature_get)(void *ctx);  /**< 摄氏度，整数 */
    void (*water_on_trigger)(struct apl_core *self, void *ctx);
    void (*water_on_recover)(struct apl_core *self, void *ctx);
    void (*uv_set)(enum apl_switch state, void *ctx);
    void (*anion_set)(enum apl_switch state, void *ctx);
    void (*on_power_off)(struct apl_core *self, void *ctx);
};

/** 占位对象。使用方声明它来持有实例，不读写 storage。 */
struct apl_core
{
    union
    {
        void *align_ptr;
        void (*align_fn)(void);
        long long align_ll;
        double align_d;
        unsigned char bytes[APL_CORE_OPAQUE_SIZE];
    } storage;
};

/**
 * @brief 记下 ops 和 ctx，并建立第一份动态配置
 * @param self 主控对象。由使用方持有
 * @param ops 宿主函数表。可以为空，空则所有硬件访问都不调用
 * @param ctx 调用 ops 时原样传回
 */
void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx);

/**
 * @brief 推进一拍
 * @param self 主控对象
 *
 * 宿主每 APL_CORE_TICK_MS 调用一次。这一拍里定时按秒减少，并采样水位边沿。
 * 定时在关机时也走，到时回调里可以开机。传感器读数只在已规定的业务需要时才取，
 * 目前没有这种业务，因此这一拍不调用 pm25_get、humidity_get、temperature_get。
 */
void apl_core_tick(struct apl_core *self);

/**
 * @brief 进入开机
 * @param self 主控对象
 */
void apl_core_power_on(struct apl_core *self);

/**
 * @brief 进入关机
 * @param self 主控对象
 *
 * 从开机进入关机时，先清掉剩余定时，再调用 on_power_off 一次。
 * 已经关机时不再调用。断电来不及走这里。
 */
void apl_core_power_off(struct apl_core *self);

/**
 * @brief 读取整机状态
 * @param self 主控对象
 * @return APL_POWER_ON 或 APL_POWER_OFF。self 为空时返回 APL_POWER_OFF
 */
enum apl_power apl_core_power_get(const struct apl_core *self);

/**
 * @brief 按新的秒数和回调重新设定定时
 * @param self 主控对象
 * @param seconds 剩余秒数。0 表示当前没有定时，到时回调不触发
 * @param on_timer 到时调用一次。空指针则到时不调用
 * @param ctx 调用 on_timer 时原样传回，可以和 ops 的 ctx 不同
 *
 * 取消定时的正式方式尚未规定。秒数为 0 只是把剩余时间设成没有定时。
 * 到时回调由这里绑定，不另提供取出函数指针的 getter。
 */
void apl_core_timer_set(struct apl_core *self,
                        uint32_t seconds,
                        void (*on_timer)(struct apl_core *self, void *ctx),
                        void *ctx);

/**
 * @brief 读取剩余定时秒数
 * @param self 主控对象
 * @return 剩余秒数。没有定时或 self 为空时返回 0
 */
uint32_t apl_core_timer_get(const struct apl_core *self);

/**
 * @brief 设定风机档位
 * @param self 主控对象
 * @param gear 上表中的一个档位。其他值不改变当前档位
 *
 * 各档对应的输出尚未规定，这里只记下档位。
 */
void apl_core_fan_gear_set(struct apl_core *self, enum apl_fan_gear gear);

/**
 * @brief 读取风机档位
 * @param self 主控对象
 * @return 当前档位。self 为空时返回 APL_FAN_GEAR_2
 */
enum apl_fan_gear apl_core_fan_gear_get(const struct apl_core *self);

#if APL_COMPILE_PURIFY
/**
 * @brief 把净化滤芯累计计数清零
 * @param self 主控对象
 */
void apl_core_filter_purify_reset(struct apl_core *self);

/**
 * @brief 读取净化滤芯累计计数
 * @param self 主控对象
 * @return 当前计数。self 为空时返回 0
 *
 * 单位尚未规定。除重置外没有写入方法。
 */
uint32_t apl_core_filter_purify_get(const struct apl_core *self);

/**
 * @brief 读取空气净化是否启用
 * @param self 主控对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 *
 * 修改入口尚未规定，因此没有对应的 setter。初始值来自 APL_DEFAULT_PURIFY。
 */
uint8_t apl_core_purify_enabled_get(const struct apl_core *self);
#endif

#if APL_COMPILE_HUMIDIFY
/**
 * @brief 把加湿滤网累计计数清零
 * @param self 主控对象
 */
void apl_core_filter_humidify_reset(struct apl_core *self);

/**
 * @brief 读取加湿滤网累计计数
 * @param self 主控对象
 * @return 当前计数。self 为空时返回 0
 *
 * 单位尚未规定。除重置外没有写入方法。
 */
uint32_t apl_core_filter_humidify_get(const struct apl_core *self);

/**
 * @brief 读取加湿是否启用
 * @param self 主控对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 *
 * 修改入口尚未规定，因此没有对应的 setter。初始值来自 APL_DEFAULT_HUMIDIFY。
 */
uint8_t apl_core_humidify_enabled_get(const struct apl_core *self);
#endif

#endif /* APL_CORE_H */
