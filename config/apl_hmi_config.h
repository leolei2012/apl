/**
 * @file    apl_hmi_config.h
 * @brief   人机的编译控制与默认值。面板在编译期选定，运行中不改。
 */

#ifndef APL_HMI_CONFIG_H
#define APL_HMI_CONFIG_H

/**
 * 编译控制：选定一块按键/显示面板，只编译该面板的实现。
 * 对外仍是一个 struct apl_hmi。净化、加湿是否启用看主控快照，不在本文件。
 * 可用构建命令行 -DAPL_HMI_PANEL=... 覆盖。
 * 新增面板时在此登记标识；未登记的值编译报错。
 *
 * 人机本地行为的默认值确定后也写在本文件。目标湿度、档位等业务默认值属于主控配置。
 */

/** 尚未指定面板 */
#define APL_HMI_PANEL_UNSPECIFIED    0u

#ifndef APL_HMI_PANEL
#define APL_HMI_PANEL    APL_HMI_PANEL_UNSPECIFIED
#endif

#if (APL_HMI_PANEL != APL_HMI_PANEL_UNSPECIFIED)
#error "APL_HMI_PANEL 不是已登记的面板"
#endif

/**
 * 各按键是否编入。1 编入，0 不编入。可用构建命令行 -D 覆盖。
 * 未编入的键不认短按、长按和组合，键灯读写视为非法。
 * 键编号保持不变。采样结构仍有六颗键，未编入的电平被丢弃。
 * 与净化、加湿的运行时启用无关，也与 APL_HMI_PANEL 无关。
 */

/** 1 = 编译净化键，0 = 不编译 */
#ifndef APL_HMI_COMPILE_KEY_PURIFY
#define APL_HMI_COMPILE_KEY_PURIFY       1u
#endif

/** 1 = 编译加湿键，0 = 不编译 */
#ifndef APL_HMI_COMPILE_KEY_HUMIDITY
#define APL_HMI_COMPILE_KEY_HUMIDITY     1u
#endif

/** 1 = 编译定时键，0 = 不编译 */
#ifndef APL_HMI_COMPILE_KEY_TIMER
#define APL_HMI_COMPILE_KEY_TIMER        1u
#endif

/** 1 = 编译灯光键，0 = 不编译 */
#ifndef APL_HMI_COMPILE_KEY_LIGHT
#define APL_HMI_COMPILE_KEY_LIGHT        1u
#endif

/** 1 = 编译风速键，0 = 不编译 */
#ifndef APL_HMI_COMPILE_KEY_SPEED
#define APL_HMI_COMPILE_KEY_SPEED        1u
#endif

/** 1 = 编译电源键，0 = 不编译 */
#ifndef APL_HMI_COMPILE_KEY_POWER
#define APL_HMI_COMPILE_KEY_POWER        1u
#endif

#if ((APL_HMI_COMPILE_KEY_PURIFY != 0u) && (APL_HMI_COMPILE_KEY_PURIFY != 1u))
#error "APL_HMI_COMPILE_KEY_PURIFY 只能是 0 或 1"
#endif

#if ((APL_HMI_COMPILE_KEY_HUMIDITY != 0u) && (APL_HMI_COMPILE_KEY_HUMIDITY != 1u))
#error "APL_HMI_COMPILE_KEY_HUMIDITY 只能是 0 或 1"
#endif

#if ((APL_HMI_COMPILE_KEY_TIMER != 0u) && (APL_HMI_COMPILE_KEY_TIMER != 1u))
#error "APL_HMI_COMPILE_KEY_TIMER 只能是 0 或 1"
#endif

#if ((APL_HMI_COMPILE_KEY_LIGHT != 0u) && (APL_HMI_COMPILE_KEY_LIGHT != 1u))
#error "APL_HMI_COMPILE_KEY_LIGHT 只能是 0 或 1"
#endif

#if ((APL_HMI_COMPILE_KEY_SPEED != 0u) && (APL_HMI_COMPILE_KEY_SPEED != 1u))
#error "APL_HMI_COMPILE_KEY_SPEED 只能是 0 或 1"
#endif

#if ((APL_HMI_COMPILE_KEY_POWER != 0u) && (APL_HMI_COMPILE_KEY_POWER != 1u))
#error "APL_HMI_COMPILE_KEY_POWER 只能是 0 或 1"
#endif

#endif /* APL_HMI_CONFIG_H */
