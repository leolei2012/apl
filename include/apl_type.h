/**
 * @file    apl_type.h
 * @brief   主控和人机共用的枚举。两边都不包含对方的头文件。
 *
 * 人机自己的按键、单位和童锁仍在 apl_hmi.h。
 * 本头不读取编译配置。
 */

#ifndef APL_TYPE_H
#define APL_TYPE_H

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

#endif /* APL_TYPE_H */
