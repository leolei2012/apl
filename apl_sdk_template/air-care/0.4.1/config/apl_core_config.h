/**
 * @file    apl_core_config.h
 * @brief   编译控制与默认值。运行中改过的配置以动态配置为准，不回读本文件。
 */

#ifndef APL_CORE_CONFIG_H
#define APL_CORE_CONFIG_H

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


/*
 * 运行参数默认值。来源见 docs/规格/apl后端规格书.md 第 5 节。
 * 标「待确认」的取值在规格里有冲突或只写了「按项目」，这里选一个，可用 -D 覆盖。
 */

/** 首次开机的风机档位，取 enum apl_fan_gear 的值。1 = 一档（待确认：汇总表写二档） */
#ifndef APL_DEFAULT_FAN_GEAR
#define APL_DEFAULT_FAN_GEAR            1u
#endif

/** 关机风干时长，秒。原表 1 小时，「时间待定」 */
#ifndef APL_CORE_DRY_S
#define APL_CORE_DRY_S                  3600u
#endif

/** 风干转速对应的档位，取 enum apl_fan_gear 的值。待确认 */
#ifndef APL_CORE_DRY_FAN_GEAR
#define APL_CORE_DRY_FAN_GEAR           1u
#endif

/** 滤网寿命，小时。原表默认 2200 小时或按项目设定 */
#ifndef APL_CORE_FILTER_LIFE_H
#define APL_CORE_FILTER_LIFE_H          2200u
#endif

/** 一体机净化滤芯与加湿滤网共用维护计时：1 一起清零，0 各自清零 */
#ifndef APL_CORE_FILTER_SHARED
#define APL_CORE_FILTER_SHARED          1u
#endif

/** 纯加湿缺水恢复窗口，秒。默认5分钟后风干；一体机始终保持净化。 */
#ifndef APL_CORE_WATER_SHORTAGE_RUN_S
#define APL_CORE_WATER_SHORTAGE_RUN_S   300u
#endif

/** 缺水橙灯闪烁时间，秒。之后常亮 */
#ifndef APL_CORE_WATER_BLINK_S
#define APL_CORE_WATER_BLINK_S          300u
#endif

/** 恒湿回差，百分点。达到目标停泵，低于目标这么多再开泵。原表「按项目回差」，待确认 */
#ifndef APL_CORE_HUMIDITY_HYSTERESIS
#define APL_CORE_HUMIDITY_HYSTERESIS    3u
#endif

/** 上电后允许进入产测的时间，秒 */
#ifndef APL_CORE_FACTORY_WINDOW_S
#define APL_CORE_FACTORY_WINDOW_S       30u
#endif

/** 产测把滤网计时置为离寿命终点还剩这么多秒 */
#ifndef APL_CORE_FACTORY_FILTER_LEFT_S
#define APL_CORE_FACTORY_FILTER_LEFT_S  60u
#endif

/** 配网超时，秒。原表只写「超时」，待确认 */
#ifndef APL_CORE_WIFI_PAIR_S
#define APL_CORE_WIFI_PAIR_S            180u
#endif

/** 恢复出厂设置后「CL」保留的时间，秒 */
#ifndef APL_CORE_FACTORY_CL_S
#define APL_CORE_FACTORY_CL_S           2u
#endif

/** 是否配置极速。原表风速键长按为选配 */
#ifndef APL_CORE_HAS_TURBO
#define APL_CORE_HAS_TURBO              1u
#endif

#if (APL_DEFAULT_FAN_GEAR > 5u)
#error "APL_DEFAULT_FAN_GEAR 超出 enum apl_fan_gear"
#endif

#if (APL_CORE_DRY_FAN_GEAR > 5u)
#error "APL_CORE_DRY_FAN_GEAR 超出 enum apl_fan_gear"
#endif

#if ((APL_CORE_FILTER_LIFE_H == 0u) || (APL_CORE_FILTER_LIFE_H > 1000000u))
#error "APL_CORE_FILTER_LIFE_H 必须在 1 到 1000000 之间"
#endif

/** 独立模块/传感器裁剪；默认保持原有能力范围，新模板显式绑定选配。 */
#ifndef APL_COMPILE_UV
#define APL_COMPILE_UV APL_COMPILE_PURIFY
#endif
#if ((APL_COMPILE_UV != 0u) && (APL_COMPILE_UV != 1u))
#error "APL_COMPILE_UV must be 0 or 1"
#endif

#ifndef APL_COMPILE_ANION
#define APL_COMPILE_ANION APL_COMPILE_PURIFY
#endif
#if ((APL_COMPILE_ANION != 0u) && (APL_COMPILE_ANION != 1u))
#error "APL_COMPILE_ANION must be 0 or 1"
#endif

#ifndef APL_COMPILE_WIFI
#define APL_COMPILE_WIFI 1u
#endif
#if ((APL_COMPILE_WIFI != 0u) && (APL_COMPILE_WIFI != 1u))
#error "APL_COMPILE_WIFI must be 0 or 1"
#endif

#ifndef APL_COMPILE_SENSOR_PM25
#define APL_COMPILE_SENSOR_PM25 APL_COMPILE_PURIFY
#endif
#if ((APL_COMPILE_SENSOR_PM25 != 0u) && (APL_COMPILE_SENSOR_PM25 != 1u))
#error "APL_COMPILE_SENSOR_PM25 must be 0 or 1"
#endif
#if (APL_COMPILE_SENSOR_PM25 && !APL_COMPILE_PURIFY)
#error "APL_COMPILE_SENSOR_PM25 requires APL_COMPILE_PURIFY"
#endif

#ifndef APL_COMPILE_SENSOR_PM10
#define APL_COMPILE_SENSOR_PM10 APL_COMPILE_PURIFY
#endif
#if ((APL_COMPILE_SENSOR_PM10 != 0u) && (APL_COMPILE_SENSOR_PM10 != 1u))
#error "APL_COMPILE_SENSOR_PM10 must be 0 or 1"
#endif
#if (APL_COMPILE_SENSOR_PM10 && !APL_COMPILE_PURIFY)
#error "APL_COMPILE_SENSOR_PM10 requires APL_COMPILE_PURIFY"
#endif

#ifndef APL_COMPILE_SENSOR_HCHO
#define APL_COMPILE_SENSOR_HCHO APL_COMPILE_PURIFY
#endif
#if ((APL_COMPILE_SENSOR_HCHO != 0u) && (APL_COMPILE_SENSOR_HCHO != 1u))
#error "APL_COMPILE_SENSOR_HCHO must be 0 or 1"
#endif
#if (APL_COMPILE_SENSOR_HCHO && !APL_COMPILE_PURIFY)
#error "APL_COMPILE_SENSOR_HCHO requires APL_COMPILE_PURIFY"
#endif

#ifndef APL_COMPILE_SENSOR_TVOC
#define APL_COMPILE_SENSOR_TVOC APL_COMPILE_PURIFY
#endif
#if ((APL_COMPILE_SENSOR_TVOC != 0u) && (APL_COMPILE_SENSOR_TVOC != 1u))
#error "APL_COMPILE_SENSOR_TVOC must be 0 or 1"
#endif
#if (APL_COMPILE_SENSOR_TVOC && !APL_COMPILE_PURIFY)
#error "APL_COMPILE_SENSOR_TVOC requires APL_COMPILE_PURIFY"
#endif

#ifndef APL_COMPILE_SENSOR_HUMIDITY
#define APL_COMPILE_SENSOR_HUMIDITY APL_COMPILE_HUMIDIFY
#endif
#if ((APL_COMPILE_SENSOR_HUMIDITY != 0u) && (APL_COMPILE_SENSOR_HUMIDITY != 1u))
#error "APL_COMPILE_SENSOR_HUMIDITY must be 0 or 1"
#endif
#if (APL_COMPILE_SENSOR_HUMIDITY && !APL_COMPILE_HUMIDIFY)
#error "APL_COMPILE_SENSOR_HUMIDITY requires APL_COMPILE_HUMIDIFY"
#endif

/** 无有效闭环输入的备用实际档位。缺省沿用旧纯加湿备用二档，项目须确认。 */
#ifndef APL_CORE_SENSOR_FALLBACK_GEAR
#define APL_CORE_SENSOR_FALLBACK_GEAR 2u
#endif
#if (APL_CORE_SENSOR_FALLBACK_GEAR > 3u)
#error "APL_CORE_SENSOR_FALLBACK_GEAR must be a physical sleep/1/2/3 gear"
#endif

#endif /* APL_CORE_CONFIG_H */
