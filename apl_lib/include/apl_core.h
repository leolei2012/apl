/**
 * @file    apl_core.h
 * @brief   主控对象。业务判断与执行器决定，库内不创建实例。
 *
 * 编译时把 `config` 加入头文件搜索路径，以便包含 `apl_core_config.h`。
 * 整机状态、开关、档位和水位段数在 `apl_type.h`。
 * 宿主与主控交换数据只走本头的 getter、setter，以及初始化时注入的 ops。
 * `struct apl_core` 里的字节只给使用方分配实例。
 */

#ifndef APL_CORE_H
#define APL_CORE_H

#include "apl_platform.h"

#include "apl_core_config.h"
#include "apl_type.h"

#define APL_CORE_VERSION_MAJOR  0
#define APL_CORE_VERSION_MINOR  3
#define APL_CORE_VERSION_PATCH  0
#define APL_CORE_VERSION_STRING "0.3.0"
#define APL_CORE_VERSION_NUM \
    ((APL_CORE_VERSION_MAJOR << 16) | (APL_CORE_VERSION_MINOR << 8) | APL_CORE_VERSION_PATCH)

/** 宿主调用 apl_core_tick 的周期。与人机拍长相同，两边不互相包含。 */
#define APL_CORE_TICK_MS            10u

/** PM2.5/湿度：每次进入运行后至少预处理5秒，再检查驱动有效状态。 */
#define APL_CORE_SENSOR_WARMUP_MS   5000u

/** 1 秒折合的拍数。剩余秒数每这么多拍减 1。 */
#define APL_CORE_TICKS_PER_SECOND   (1000u / APL_CORE_TICK_MS)

/** 使用方按这个大小持有对象。布局不对外，读写都走 getter 和 setter。 */
#if defined(__C51__)
/* Exact guarded C51 state plus 11-byte retained capacity margin. */
#define APL_CORE_OPAQUE_SIZE (46u \
 + 10u * APL_COMPILE_HAS_DRY + 7u * APL_COMPILE_FILTER \
 + APL_COMPILE_FILTER * (APL_COMPILE_PURIFY + APL_COMPILE_HUMIDIFY) * (4u + 4u * APL_COMPILE_FACTORY + APL_CORE_COMPILE_FILTER_CACHE) \
 + 13u * APL_COMPILE_TIMER + 4u * APL_CORE_HAS_TURBO + 3u * APL_COMPILE_SLEEP \
 + 22u * APL_COMPILE_HUMIDIFY + 26u * APL_COMPILE_WIFI + 10u * APL_COMPILE_FACTORY \
 + 3u * APL_COMPILE_UV + 2u * APL_COMPILE_ANION + 2u * APL_COMPILE_HAS_AQ \
 + 3u * (APL_COMPILE_HUMIDIFY && APL_COMPILE_SENSOR_HUMIDITY) \
 + 2u * APL_COMPILE_SENSOR_PM25 + 5u * APL_COMPILE_HAS_SENSORS + APL_COMPILE_PURIFY \
 + 15u * (APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP))
#else
#define APL_CORE_OPAQUE_SIZE        320u
#endif

/** 运行时方案版本。内存结构不是串口/Flash格式，禁止直接传输sizeof结构体。 */
#define APL_PROFILE_VERSION 6u
#define APL_PRODUCT_PID_MAX 16u
#define APL_PROFILE_MODULE_UV 1u
#define APL_PROFILE_MODULE_ANION 2u
#define APL_PROFILE_MODULE_WIFI 4u

struct apl_profile
{
    uint16_t version;
#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
    struct apl_keymap keymap;
#endif
    uint8_t purify_enabled;
    uint8_t humidify_enabled;
    uint8_t modules; /**< APL_PROFILE_MODULE_* 位掩码，独立于净化能力 */
    uint8_t sensors; /**< APL_SENSOR_*_MASK；空气传感器依赖净化，湿度依赖加湿 */
    uint8_t default_fan_gear;
#if APL_COMPILE_HAS_DRY
    uint8_t dry_fan_gear;
#endif
    uint8_t sensor_fallback_gear;
#if APL_CORE_HAS_TURBO
    uint8_t turbo_enabled;
#endif
#if APL_COMPILE_FILTER
    uint8_t filter_shared;
#endif
#if APL_COMPILE_HUMIDIFY
    uint8_t humidity_hysteresis;
#endif
#if APL_COMPILE_HAS_DRY
    uint32_t dry_s;
#endif
#if APL_COMPILE_FILTER
    uint32_t filter_life_h;
#endif
#if APL_COMPILE_HUMIDIFY
    uint32_t shortage_run_s;
    uint32_t water_blink_s;
#endif
#if APL_COMPILE_WIFI
    uint32_t wifi_pair_s;
#endif
    uint16_t display_rotate_s; /**< 净化+加湿数显轮换间隔，1～3600秒，默认20秒。 */
#if APL_COMPILE_TIMER
    uint8_t timer_step_h; /**< 定时键每次增加1～24小时，默认1；超过24回到0取消。 */
#endif
#if APL_COMPILE_SLEEP
    uint16_t fan_sleep_rpm;
#endif
    uint16_t fan_gear1_rpm;
    uint16_t fan_gear2_rpm;
    uint16_t fan_gear3_rpm;
#if APL_CORE_HAS_TURBO
    uint16_t fan_turbo_rpm; /**< 各档目标rpm；默认400/600/900/1200/1500，0表示停转。 */
#endif
#if APL_COMPILE_WIFI
    char pid[APL_PRODUCT_PID_MAX + 1u]; /**< Wi-Fi产品ID：0～16位ASCII字母/数字，以NUL结束；默认空。 */
#endif
};

enum apl_profile_result
{
    APL_PROFILE_OK = 0,
    APL_PROFILE_INVALID_ARGUMENT,
    APL_PROFILE_UNSUPPORTED_VERSION,
    APL_PROFILE_UNSUPPORTED_CAPABILITY,
    APL_PROFILE_INVALID_VALUE
};

/** 按当前固件编译能力生成默认表；输出由调用方持有。 */
void apl_profile_defaults(struct apl_profile *profile) APL_CALL;
/** 整表校验；无副作用，不要求硬件已连接。 */
enum apl_profile_result apl_profile_validate(const struct apl_profile *profile) APL_CALL;

struct apl_core;

/**
 * 宿主注入的硬件访问。函数指针为空则不调用，也不报警。
 * 对象存活期间 ops 指向的内容必须一直有效。
 * 后面的成员是后加的：宿主应先把整张表清零再填写，未用的保持为空。
 */
struct apl_core_ops
{
    enum apl_water_level (*get_water_level)(void *ctx) APL_CALL;
    uint16_t (*get_pm25)(void *ctx) APL_CALL;        /**< μg/m³，整数 */
    uint8_t (*get_humidity)(void *ctx) APL_CALL;     /**< 相对湿度，整数百分比 */
    int16_t (*get_temperature)(void *ctx) APL_CALL;  /**< 摄氏度，整数。目前没有业务使用 */
    void (*water_on_trigger)(struct apl_core *self, void *ctx) APL_CALL;
    void (*water_on_recover)(struct apl_core *self, void *ctx) APL_CALL;
    void (*set_uv)(enum apl_switch state, void *ctx) APL_CALL;
    void (*set_anion)(enum apl_switch state, void *ctx) APL_CALL;
    void (*on_power_off)(struct apl_core *self, void *ctx) APL_CALL;
    uint16_t (*get_pm10)(void *ctx) APL_CALL;        /**< μg/m³。选配，空则不参与空气质量判定 */
    uint16_t (*get_hcho)(void *ctx) APL_CALL;        /**< 甲醛，μg/m³（mg/m³ × 1000）。选配 */
    uint16_t (*get_tvoc)(void *ctx) APL_CALL;        /**< TVOC，μg/m³（mg/m³ × 1000）。选配 */
    uint8_t (*get_uv_interlock)(void *ctx) APL_CALL; /**< UV 安全微动开关，仅1闭合；缺失输入按断开处理 */
    void (*set_fan)(enum apl_switch state, enum apl_fan_gear gear, void *ctx) APL_CALL;
    void (*set_pump)(enum apl_switch state, void *ctx) APL_CALL;  /**< 水泵与雾化等加湿负载 */
    void (*memory_save)(const struct apl_core_memory *memory, void *ctx) APL_CALL;
    uint8_t (*get_module_fault)(enum apl_module module, void *ctx) APL_CALL;
    /**< 宿主已判定故障：0正常，非0故障。NULL表示无反馈，不能据此宣称已检测。 */
    enum apl_sensor_status (*get_sensor_status)(enum apl_sensor sensor, void *ctx) APL_CALL;
    /**< NULL兼容旧宿主：已有数值回调视为有效，缺失回调为ABSENT；RH>100仍无效。
     * 新宿主应显式提供状态，预处理/通信故障阈值在驱动中判定。 */
};

/** 占位对象。使用方声明它来持有实例，不读写 storage。 */
struct apl_core
{
    union
    {
        void *align_ptr;
        void (*align_fn)(void) APL_CALL;
        APL_ALIGN_INTEGER align_ll;
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
void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx) APL_CALL;

/** 冷初始化并复制整张配置；profile为空表示尚未导入，使用固件内置默认表并返回OK。
 * 非空配置校验失败时不修改对象、不调用硬件；仅用于停止tick后的启动。
 * 不可对正在驱动负载的对象调用；在线更换方案须由宿主先停机并完成输出关闭。
 * 成功后再恢复兼容的运行记忆，最后开始tick。配置不通过memory_save保存。 */
enum apl_profile_result apl_core_init_profile(struct apl_core *self,
    const struct apl_core_ops *ops, void *ctx, const struct apl_profile *profile) APL_CALL;
/** 读回已经复制的方案；self为空时返回当前固件默认表。 */
void apl_core_get_profile(const struct apl_core *self, struct apl_profile *profile) APL_CALL;
/** 复制PID（包含结尾NUL）；宿主提供至少17字节。self为空返回空串，pid为空无操作。 */
#if APL_COMPILE_WIFI
void apl_core_get_pid(const struct apl_core *self, char *pid) APL_CALL;
#endif


/**
 * @brief 推进一拍
 * @param self 主控对象
 *
 * 宿主每 APL_CORE_TICK_MS 调用一次。这一拍里定时按秒减少，采样水位边沿，
 * 并按当前状态更新风机、水泵、UV、负离子输出（有变化才调用 ops）。
 * 运行中每秒读一次传感器：净化启用时读 pm25/pm10/hcho/tvoc，加湿启用时读湿度。
 * 风干倒计时、缺水计时、滤网计时、配网超时也在这里按秒推进。
 */
void apl_core_tick(struct apl_core *self) APL_CALL;

/**
 * @brief 进入开机
 * @param self 主控对象
 */
void apl_core_power_on(struct apl_core *self) APL_CALL;

/**
 * @brief 进入关机
 * @param self 主控对象
 *
 * 从开机进入关机时，先清掉剩余定时，再调用 on_power_off 一次。
 * 已经关机时不再调用。断电来不及走这里。
 */
void apl_core_power_off(struct apl_core *self) APL_CALL;

/**
 * @brief 读取整机状态
 * @param self 主控对象
 * @return APL_POWER_ON 或 APL_POWER_OFF。self 为空时返回 APL_POWER_OFF
 */
enum apl_power apl_core_get_power(const struct apl_core *self) APL_CALL;

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
#if APL_COMPILE_TIMER
void apl_core_set_timer(struct apl_core *self,
                        uint32_t seconds,
                        void (*on_timer)(struct apl_core *self, void *ctx) APL_CALL,
                        void *ctx) APL_CALL;
#endif

/**
 * @brief 读取剩余定时秒数
 * @param self 主控对象
 * @return 剩余秒数。没有定时或 self 为空时返回 0
 */
#if APL_COMPILE_TIMER
uint32_t apl_core_get_timer(const struct apl_core *self) APL_CALL;
#endif

/**
 * @brief 设定风机档位
 * @param self 主控对象
 * @param gear 上表中的一个档位。其他值不改变当前档位
 *
 * 记下用户档位。实际输出见快照的 fan_output 和 ops 的 set_fan。
 */
void apl_core_set_fan_gear(struct apl_core *self, enum apl_fan_gear gear) APL_CALL;

/**
 * @brief 读取风机档位
 * @param self 主控对象
 * @return 当前档位。self 为空时返回 APL_DEFAULT_FAN_GEAR
 */
enum apl_fan_gear apl_core_get_fan_gear(const struct apl_core *self) APL_CALL;

/** 已提交输出档位对应的目标rpm，非实测值；风机关闭/self为空返回0。
 * 在tick后读取；set_fan回调内部也可读取，回调前输出档位已更新。
 * 自动/恒湿/风干/输入异常使用最终输出档位的配置；AUTO本身没有固定rpm。 */
uint16_t apl_core_get_fan_target_rpm(const struct apl_core *self) APL_CALL;


/**
 * @brief 执行一条人机操作
 * @param self 主控对象
 * @param command 操作。空指针视为非法
 * @return APL_LINK_OK 已执行；APL_LINK_REJECTED 未执行，状态不变
 *
 * 功能未编译或动态配置未启用、当前状态不允许、参数非法时拒绝。
 * 每种操作的条件见后端规格书第 3.2 节。
 */
enum apl_link_result apl_core_command(struct apl_core *self,
                                      const struct apl_link_command *command) APL_CALL;

/**
 * @brief 取当前状态快照
 * @param self 主控对象。为空时把 snapshot 清零
 * @param snapshot 输出。空指针则不写
 */
void apl_core_get_snapshot(const struct apl_core *self, struct apl_link_snapshot *snapshot) APL_CALL;

/**
 * @brief 读取运行状态
 * @param self 主控对象
 * @return 当前运行状态。self 为空时返回 APL_RUN_STANDBY
 */
enum apl_run_state apl_core_get_run_state(const struct apl_core *self) APL_CALL;

/**
 * @brief 取需要掉电保存的内容
 * @param self 主控对象。为空时把 memory 清零
 * @param memory 输出。产测期间给出的是真实滤网计时，不是产测注入值
 */
#if APL_COMPILE_MEMORY
void apl_core_get_memory(const struct apl_core *self, struct apl_core_memory *memory) APL_CALL;
#endif

/**
 * @brief 上电后交还掉电保存的内容
 * @param self 主控对象
 * @param memory 上次保存的内容。空指针则不改
 *
 * 在 apl_core_init 之后、第一拍之前调用。产测未完成时先恢复出厂设置；
 * 掉电前在运行则自动开机，按掉电前档位运行。风干中掉电不算运行，上电保持待机。
 */
#if APL_COMPILE_MEMORY
void apl_core_memory_restore(struct apl_core *self, const struct apl_core_memory *memory) APL_CALL;
#endif

/**
 * @brief 宿主报告 Wi-Fi 是否已联网
 * @param self 主控对象
 * @param connected 1 已联网，0 未联网。其他值不改
 */
#if APL_COMPILE_WIFI
void apl_core_set_wifi_connected(struct apl_core *self, uint8_t connected) APL_CALL;
#endif

#if APL_COMPILE_PURIFY
/**
 * @brief 把净化滤芯累计计数清零
 * @param self 主控对象
 */
#if APL_COMPILE_FILTER
void apl_core_filter_purify_reset(struct apl_core *self) APL_CALL;
#endif

/**
 * @brief 读取净化滤芯累计计数
 * @param self 主控对象
 * @return 当前计数。self 为空时返回 0
 *
 * 单位为秒，只在运行中累计。除重置外没有写入方法。
 */
#if APL_COMPILE_FILTER
uint32_t apl_core_get_filter_purify(const struct apl_core *self) APL_CALL;
#endif

/**
 * @brief 读取空气净化是否启用
 * @param self 主控对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 *
 * 通过 apl_core_init_profile 冷启动加载；旧初始化入口取 APL_DEFAULT_PURIFY。
 */
uint8_t apl_core_get_purify_enabled(const struct apl_core *self) APL_CALL;
#endif

#if APL_COMPILE_HUMIDIFY
/**
 * @brief 把加湿滤网累计计数清零
 * @param self 主控对象
 */
#if APL_COMPILE_FILTER
void apl_core_filter_humidify_reset(struct apl_core *self) APL_CALL;
#endif

/**
 * @brief 读取加湿滤网累计计数
 * @param self 主控对象
 * @return 当前计数。self 为空时返回 0
 *
 * 单位为秒，只在运行中累计。除重置外没有写入方法。
 */
#if APL_COMPILE_FILTER
uint32_t apl_core_get_filter_humidify(const struct apl_core *self) APL_CALL;
#endif

/**
 * @brief 读取加湿是否启用
 * @param self 主控对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 *
 * 通过 apl_core_init_profile 冷启动加载；旧初始化入口取 APL_DEFAULT_HUMIDIFY。
 */
uint8_t apl_core_get_humidify_enabled(const struct apl_core *self) APL_CALL;
#endif

#endif /* APL_CORE_H */
