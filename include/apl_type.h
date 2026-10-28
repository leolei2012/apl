/**
 * @file    apl_type.h
 * @brief   主控和人机共用的枚举。两边都不包含对方的头文件。
 *
 * 人机自己的按键、单位和童锁仍在 apl_hmi.h。
 * 本头不读取编译配置。
 */

#ifndef APL_TYPE_H
#define APL_TYPE_H

#include <stdint.h>

/** 整机状态。同一时刻只有一个。 */
enum apl_power
{
    APL_POWER_OFF = 0,
    APL_POWER_ON
};

/** UV、负离子的开关。没有档位。 */
enum apl_switch
{
    APL_SWITCH_OFF = 0,
    APL_SWITCH_ON
};

/** 风机档位。同一时刻只有一个。 */
enum apl_fan_gear
{
    APL_FAN_GEAR_SLEEP = 0,
    APL_FAN_GEAR_1,
    APL_FAN_GEAR_2,
    APL_FAN_GEAR_3,
    APL_FAN_GEAR_AUTO,
    APL_FAN_GEAR_TURBO
};

/**
 * 水位点亮段数。0 全灭，1 到 4 从最低段往上点亮。
 * 库不提供读取接口；宿主每次被询问时返回当前段数。
 */
enum apl_water_level
{
    APL_WATER_LEVEL_0 = 0,
    APL_WATER_LEVEL_1,
    APL_WATER_LEVEL_2,
    APL_WATER_LEVEL_3,
    APL_WATER_LEVEL_4
};


/**
 * 运行状态。比 apl_power 细：关机分成待机，开机之外还有关机风干和产测。
 * apl_core_get_power 仍只回答开或关；风干和产测算开机。
 */
enum apl_run_state
{
    APL_RUN_STANDBY = 0,   /**< 待机（关机） */
    APL_RUN_ON,            /**< 运行 */
    APL_RUN_DRY,           /**< 关机风干。只在加湿启用时出现 */
    APL_RUN_FACTORY        /**< 产测 */
};

/** 空气质量等级。取各项传感器中的最差一项。 */
enum apl_aq_level
{
    APL_AQ_EXCELLENT = 0,  /**< 优，蓝 */
    APL_AQ_GOOD,           /**< 良，绿 */
    APL_AQ_MODERATE,       /**< 中度污染，黄 */
    APL_AQ_SEVERE          /**< 重度污染，红 */
};

/** 缺水提醒。 */
enum apl_water_alert
{
    APL_WATER_ALERT_NONE = 0,
    APL_WATER_ALERT_BLINK,   /**< 缺水后 5 分钟内，最左一格橙灯闪 */
    APL_WATER_ALERT_STEADY   /**< 5 分钟后橙灯常亮 */
};

/** Wi-Fi 状态。 */
enum apl_wifi_state
{
    APL_WIFI_OFF = 0,
    APL_WIFI_PAIRING,        /**< 配网中，图标闪烁 */
    APL_WIFI_CONNECTED       /**< 已联网，图标常亮 */
};

/** 主控要求人机发出的提示音。人机按序号变化播放一次。 */
enum apl_beep
{
    APL_BEEP_NONE = 0,
    APL_BEEP_SHORT,          /**< 一声，按键或上电 */
    APL_BEEP_FILTER_RESET,   /**< 到期重置，500 ms */
    APL_BEEP_FILTER_FORCED,  /**< 未到期强制重置，300 ms */
    APL_BEEP_WATER_85,       /**< 水量过 85%，一声 300 ms */
    APL_BEEP_WATER_FULL,     /**< 水满，四声 100 ms 响 100 ms 停 */
    APL_BEEP_WATER_SHORTAGE, /**< 缺水，三声 100 ms 响 100 ms 停 */
    APL_BEEP_FACTORY,        /**< 进入产测，500 ms */
    APL_BEEP_UV_ALARM        /**< UV 联锁断开报警 */
};

/** 人机发给主控的操作种类。 */
enum apl_link_command_type
{
    APL_LINK_CMD_NONE = 0,
    APL_LINK_CMD_POWER,            /**< 电源键短按：待机/风干→开机；运行→关机（加湿启用时先风干） */
    APL_LINK_CMD_DRY_STOP,         /**< 风干中长按电源键：立即结束风干 */
    APL_LINK_CMD_FAN_CYCLE,        /**< 风速键短按：睡眠→一→二→三→自动循环 */
    APL_LINK_CMD_FAN_SET,          /**< 直接设档，arg 为 enum apl_fan_gear */
    APL_LINK_CMD_TURBO_TOGGLE,     /**< 风速键长按：进入或退出极速 */
    APL_LINK_CMD_SLEEP_TOGGLE,     /**< 灯光键长按：进入或退出睡眠 */
    APL_LINK_CMD_TIMER_SET,        /**< 定时确认，arg 为 0～24 小时，0 取消 */
    APL_LINK_CMD_PURIFY_TOGGLE,    /**< 净化键短按：UV、负离子等净化复合功能开关 */
    APL_LINK_CMD_HUMIDIFY_TOGGLE,  /**< 湿度键短按（一体机）：加湿开关 */
    APL_LINK_CMD_HUMIDITY_TARGET,  /**< 恒湿确认，arg 为 40～80（5 步进）或 0 表示 OF */
    APL_LINK_CMD_FILTER_RESET,     /**< 滤网重置 */
    APL_LINK_CMD_WIFI_PAIR,        /**< 净化键长按：清除网络并配网 */
    APL_LINK_CMD_FACTORY_ENTER,    /**< 产测组合键 */
    APL_LINK_CMD_FACTORY_EXIT      /**< 产测中按电源键：恢复出厂设置 */
};

/** apl_core_command 的结果。 */
enum apl_link_result
{
    APL_LINK_OK = 0,
    APL_LINK_REJECTED = -1   /**< 功能未编译或未启用、状态不允许，或参数非法 */
};

/** 人机发给主控的一条操作。 */
struct apl_link_command
{
    enum apl_link_command_type type;
    uint8_t arg;   /**< 含义见 enum apl_link_command_type；不用时为 0 */
};

/** 恒湿目标为 OF（不恒湿）。 */
#define APL_HUMIDITY_TARGET_OFF   0u

/** 剩余寿命百分比。0 表示已到期。 */
#define APL_FILTER_EXPIRED        0u

/**
 * 主控发给人机的状态快照。人机只按这一份绘制。
 * 未编译或未启用的功能，对应字段为 0，人机不画。
 */
struct apl_link_snapshot
{
    enum apl_run_state run_state;
    enum apl_fan_gear fan_gear;        /**< 用户选择的档位 */
    enum apl_fan_gear fan_output;      /**< 实际输出档位（自动、恒湿、风干折算后） */
    enum apl_aq_level aq_level;
    enum apl_water_level water_level;
    enum apl_water_alert water_alert;
    enum apl_wifi_state wifi;
    enum apl_switch uv;
    enum apl_switch anion;
    enum apl_switch pump;
    enum apl_beep beep;                /**< 最近一次提示音 */
    uint32_t timer_left_s;             /**< 剩余定时秒数，0 没有定时 */
    uint32_t dry_left_s;               /**< 剩余风干秒数 */
    uint16_t pm25;                     /**< μg/m³ */
    uint8_t beep_seq;                  /**< 每要求一次提示音加 1 */
    uint8_t purify_enabled;            /**< 动态配置：净化启用 */
    uint8_t humidify_enabled;          /**< 动态配置：加湿启用 */
    uint8_t purify_on;                 /**< 净化复合功能开着 */
    uint8_t humidify_on;               /**< 加湿开着 */
    uint8_t humidity;                  /**< 环境湿度，% */
    uint8_t humidity_target;           /**< 恒湿目标，APL_HUMIDITY_TARGET_OFF 为不恒湿 */
    uint8_t sleep;                     /**< 睡眠中：静音、熄屏 */
    uint8_t filter_purify_left;        /**< 净化滤芯剩余 1～99，0 到期 */
    uint8_t filter_humidify_left;      /**< 加湿滤网剩余 1～99，0 到期 */
    uint8_t filter_reset_seq;          /**< 每重置一次加 1 */
    uint8_t filter_reset_forced;       /**< 最近一次重置是否未到期强制重置 */
    uint8_t uv_fault;                  /**< UV 联锁断开过，直到再次开启净化 */
    uint8_t factory_done;              /**< 刚完成恢复出厂设置，显示 CL */
};

/**
 * 主控需要宿主掉电保存的内容。宿主在变化时保存，上电后交还。
 * 定时、童锁、恒湿不保存。
 */
struct apl_core_memory
{
    uint32_t filter_purify_s;     /**< 净化滤芯累计运行秒数 */
    uint32_t filter_humidify_s;   /**< 加湿滤网累计运行秒数 */
    enum apl_fan_gear fan_gear;
    uint8_t running;              /**< 掉电时在运行（风干、产测不算） */
    uint8_t used;                 /**< 已经手动开过机。0 表示首次开机 */
    uint8_t purify_on;
    uint8_t humidify_on;
    uint8_t factory_pending;      /**< 产测未完成，上电先恢复出厂设置 */
};

#endif /* APL_TYPE_H */
