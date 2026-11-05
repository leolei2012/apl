/** Build-wide footprint options. All APL/host translation units must agree.
 * Runtime profiles cannot re-enable code removed at build time. Defaults retain
 * the full API and standard panel. See docs/规范/APL编译裁剪.md. */
#ifndef APL_BUILD_CONFIG_H
#define APL_BUILD_CONFIG_H

/* Build-wide capabilities: shared by core and HMI, all default to full support. */
#ifndef APL_COMPILE_PURIFY
#define APL_COMPILE_PURIFY 1u
#endif
#ifndef APL_COMPILE_HUMIDIFY
#define APL_COMPILE_HUMIDIFY 1u
#endif
#ifndef APL_COMPILE_UV
#define APL_COMPILE_UV APL_COMPILE_PURIFY
#endif
#ifndef APL_COMPILE_ANION
#define APL_COMPILE_ANION APL_COMPILE_PURIFY
#endif
#ifndef APL_CORE_HAS_TURBO
#define APL_CORE_HAS_TURBO 1u
#endif
#ifndef APL_COMPILE_SENSOR_PM25
#define APL_COMPILE_SENSOR_PM25 APL_COMPILE_PURIFY
#endif
#ifndef APL_COMPILE_SENSOR_PM10
#define APL_COMPILE_SENSOR_PM10 APL_COMPILE_PURIFY
#endif
#ifndef APL_COMPILE_SENSOR_HCHO
#define APL_COMPILE_SENSOR_HCHO APL_COMPILE_PURIFY
#endif
#ifndef APL_COMPILE_SENSOR_TVOC
#define APL_COMPILE_SENSOR_TVOC APL_COMPILE_PURIFY
#endif
#ifndef APL_COMPILE_SENSOR_HUMIDITY
#define APL_COMPILE_SENSOR_HUMIDITY APL_COMPILE_HUMIDIFY
#endif
#ifndef APL_COMPILE_TIMER
#define APL_COMPILE_TIMER 1u
#endif
#ifndef APL_COMPILE_FILTER
#define APL_COMPILE_FILTER 1u
#endif
#ifndef APL_COMPILE_FACTORY
#define APL_COMPILE_FACTORY 1u
#endif
#ifndef APL_COMPILE_MEMORY
#define APL_COMPILE_MEMORY 1u
#endif
#ifndef APL_COMPILE_SLEEP
#define APL_COMPILE_SLEEP 1u
#endif
#ifndef APL_COMPILE_DRY
#define APL_COMPILE_DRY 1u
#endif
#ifndef APL_HMI_COMPILE_AMBIENT
#define APL_HMI_COMPILE_AMBIENT 1u
#endif
#ifndef APL_HMI_COMPILE_KEYMAP
#define APL_HMI_COMPILE_KEYMAP 1u
#endif
#define APL_COMPILE_HAS_AQ (APL_COMPILE_SENSOR_PM25 || APL_COMPILE_SENSOR_PM10 || APL_COMPILE_SENSOR_HCHO || APL_COMPILE_SENSOR_TVOC)
#define APL_COMPILE_HAS_SENSORS (APL_COMPILE_HAS_AQ || APL_COMPILE_SENSOR_HUMIDITY)
#define APL_COMPILE_HAS_DRY (APL_COMPILE_DRY && APL_COMPILE_HUMIDIFY)

#if ((APL_HMI_COMPILE_KEYMAP != 0u) && (APL_HMI_COMPILE_KEYMAP != 1u)) \
 || ((APL_COMPILE_TIMER != 0u) && (APL_COMPILE_TIMER != 1u)) \
 || ((APL_COMPILE_FILTER != 0u) && (APL_COMPILE_FILTER != 1u)) \
 || ((APL_COMPILE_FACTORY != 0u) && (APL_COMPILE_FACTORY != 1u)) \
 || ((APL_COMPILE_MEMORY != 0u) && (APL_COMPILE_MEMORY != 1u)) \
 || ((APL_COMPILE_SLEEP != 0u) && (APL_COMPILE_SLEEP != 1u)) \
 || ((APL_COMPILE_DRY != 0u) && (APL_COMPILE_DRY != 1u)) \
 || ((APL_HMI_COMPILE_AMBIENT != 0u) && (APL_HMI_COMPILE_AMBIENT != 1u))
#error "APL optional feature switches must be 0 or 1"
#endif

/** Optional module shared by the core and HMI; 0 removes its implementation/API. */
#ifndef APL_COMPILE_WIFI
#define APL_COMPILE_WIFI 1u
#endif
#if (APL_COMPILE_WIFI != 0u) && (APL_COMPILE_WIFI != 1u)
#error "APL_COMPILE_WIFI must be 0 or 1"
#endif

/** 0: core-only struct apl; host owns input/display policy. No HMI API linked. */
#ifndef APL_COMPILE_HMI
#define APL_COMPILE_HMI 1u
#endif
/** 0: remove arbitrary per-key callbacks; linked standard panel keys still work. */
#ifndef APL_HMI_COMPILE_KEY_CALLBACKS
#define APL_HMI_COMPILE_KEY_CALLBACKS 1u
#endif
/** 0: no combinations, 2..4: number of generic slots. Standard panel uses two. */
#ifndef APL_HMI_COMBO_MAX
#define APL_HMI_COMBO_MAX 4u
#endif
/** 0: remove buzzer state/player/output and dedicated public APIs. */
#ifndef APL_HMI_COMPILE_BUZZER
#define APL_HMI_COMPILE_BUZZER 1u
#endif
/** 0: calculate percentages on snapshot reads, trading CPU for smaller CODE. */
#ifndef APL_CORE_COMPILE_FILTER_CACHE
#define APL_CORE_COMPILE_FILTER_CACHE 1u
#endif

#if ((APL_COMPILE_HMI != 0u) && (APL_COMPILE_HMI != 1u)) \
 || ((APL_HMI_COMPILE_KEY_CALLBACKS != 0u) && (APL_HMI_COMPILE_KEY_CALLBACKS != 1u)) \
 || ((APL_HMI_COMPILE_BUZZER != 0u) && (APL_HMI_COMPILE_BUZZER != 1u)) \
 || ((APL_CORE_COMPILE_FILTER_CACHE != 0u) && (APL_CORE_COMPILE_FILTER_CACHE != 1u))
#error "APL build switches must be 0 or 1"
#endif
#if (APL_HMI_COMBO_MAX != 0u) && ((APL_HMI_COMBO_MAX < 2u) || (APL_HMI_COMBO_MAX > 4u))
#error "APL_HMI_COMBO_MAX must be 0 or 2..4; the standard panel needs two slots"
#endif
#endif
