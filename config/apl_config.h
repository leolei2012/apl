/**
 * @file    apl_config.h
 * @brief   编译控制与默认值。运行中改过的配置以动态配置为准，不回读本文件。
 */

#ifndef APL_CONFIG_H
#define APL_CONFIG_H

/*
 * 编译控制：决定对应代码是否进入程序。未编译的功能，动态配置不能打开。
 * 默认值：尚无动态配置时的初始值。已有动态配置时，以动态配置为准。
 * 两项均可由构建命令行 -D 覆盖。
 */

/** 1 = 编译空气净化，0 = 不编译 */
#ifndef APL_COMPILE_PURIFY
#define APL_COMPILE_PURIFY      1u
#endif

/** 1 = 编译加湿，0 = 不编译 */
#ifndef APL_COMPILE_HUMIDIFY
#define APL_COMPILE_HUMIDIFY    1u
#endif

/** 动态配置缺省时是否启用空气净化。仅在已编译时可为 1 */
#ifndef APL_DEFAULT_PURIFY
#define APL_DEFAULT_PURIFY      1u
#endif

/** 动态配置缺省时是否启用加湿。仅在已编译时可为 1 */
#ifndef APL_DEFAULT_HUMIDIFY
#define APL_DEFAULT_HUMIDIFY    1u
#endif

#if ((APL_COMPILE_PURIFY != 0u) && (APL_COMPILE_PURIFY != 1u))
#error "APL_COMPILE_PURIFY 只能是 0 或 1"
#endif

#if ((APL_COMPILE_HUMIDIFY != 0u) && (APL_COMPILE_HUMIDIFY != 1u))
#error "APL_COMPILE_HUMIDIFY 只能是 0 或 1"
#endif

#if ((APL_COMPILE_PURIFY == 0u) && (APL_COMPILE_HUMIDIFY == 0u))
#error "净化与加湿至少编译一项"
#endif

#if ((APL_DEFAULT_PURIFY != 0u) && (APL_DEFAULT_PURIFY != 1u))
#error "APL_DEFAULT_PURIFY 只能是 0 或 1"
#endif

#if ((APL_DEFAULT_HUMIDIFY != 0u) && (APL_DEFAULT_HUMIDIFY != 1u))
#error "APL_DEFAULT_HUMIDIFY 只能是 0 或 1"
#endif

#if ((APL_COMPILE_PURIFY == 0u) && (APL_DEFAULT_PURIFY != 0u))
#error "未编译空气净化时，APL_DEFAULT_PURIFY 必须为 0"
#endif

#if ((APL_COMPILE_HUMIDIFY == 0u) && (APL_DEFAULT_HUMIDIFY != 0u))
#error "未编译加湿时，APL_DEFAULT_HUMIDIFY 必须为 0"
#endif

#endif /* APL_CONFIG_H */
