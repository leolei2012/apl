import type { Definition } from './index'
import { validMemorySettings, type MemorySettings } from './memory-settings'

export const BUILD_PARAMETER = 'firmware_build'
const addedFlags = new Set(['APL_COMPILE_TIMER','APL_COMPILE_FILTER','APL_COMPILE_FACTORY','APL_COMPILE_MEMORY','APL_COMPILE_SLEEP','APL_COMPILE_DRY','APL_HMI_COMPILE_AMBIENT','APL_HMI_COMPILE_KEYMAP'])
export const buildOptions = [
  ['APL_COMPILE_PURIFY', '净化', '业务能力'], ['APL_COMPILE_HUMIDIFY', '加湿', '业务能力'],
  ['APL_COMPILE_UV', 'UV', '辅助模块'], ['APL_COMPILE_ANION', '负离子', '辅助模块'], ['APL_COMPILE_WIFI', 'Wi-Fi', '辅助模块'],
  ['APL_COMPILE_SENSOR_PM25', 'PM2.5', '传感器'], ['APL_COMPILE_SENSOR_PM10', 'PM10', '传感器'],
  ['APL_COMPILE_SENSOR_HCHO', '甲醛', '传感器'], ['APL_COMPILE_SENSOR_TVOC', 'TVOC', '传感器'], ['APL_COMPILE_SENSOR_HUMIDITY', '湿度', '传感器'],
  ['APL_COMPILE_TIMER', '定时', '业务能力'], ['APL_COMPILE_FILTER', '滤网维护', '业务能力'],
  ['APL_COMPILE_FACTORY', '产测', '业务能力'], ['APL_COMPILE_MEMORY', '掉电记忆', '业务能力'],
  ['APL_COMPILE_SLEEP', '睡眠模式', '业务能力'], ['APL_COMPILE_DRY', '关机风干', '业务能力'],
  ['APL_HMI_COMPILE_AMBIENT', 'RGB 氛围灯', '交互与资源'],
  ['APL_HMI_COMPILE_KEYMAP', '内部按键映射', '交互与资源'],
  ['APL_CORE_HAS_TURBO', '极速档', '业务能力'], ['APL_COMPILE_HMI', 'APL 按键与显示', '交互与资源'],
  ['APL_HMI_COMPILE_KEY_CALLBACKS', '每键自定义回调', '交互与资源'], ['APL_HMI_COMPILE_BUZZER', '蜂鸣器（含报警音）', '交互与资源'],
  ['APL_CORE_COMPILE_FILTER_CACHE', '滤网百分比缓存', '交互与资源'],
  ['APL_HMI_COMPILE_KEY_PURIFY', '净化键', '按键'], ['APL_HMI_COMPILE_KEY_HUMIDITY', '加湿键', '按键'],
  ['APL_HMI_COMPILE_KEY_TIMER', '定时键', '按键'], ['APL_HMI_COMPILE_KEY_LIGHT', '灯光键', '按键'],
  ['APL_HMI_COMPILE_KEY_SPEED', '风速键', '按键'], ['APL_HMI_COMPILE_KEY_POWER', '电源键', '按键'], ['APL_HMI_COMPILE_KEY_FILTER', '滤芯键', '按键'],
] as const
export type BuildMacro = typeof buildOptions[number][0] | 'APL_HMI_COMBO_MAX'
export interface FirmwareBuild {
  target: 'portable' | 'c51'; chip: string; flashKb: number; ramBytes: number; stackBytes: number
  defines: Record<BuildMacro, number>
  memory?: MemorySettings
}
export type BuildPreset = 'full' | 'product' | 'compact' | 'minimal' | 'core-only'
export function defaultFirmwareBuild(): FirmwareBuild {
  return { target: 'portable', chip: '', flashKb: 32, ramBytes: 1024, stackBytes: 256,
    defines: { ...Object.fromEntries(buildOptions.map(([key]) => [key, 1])), APL_HMI_COMBO_MAX: 4 } as FirmwareBuild['defines'] }
}
function rawBuild(d: Definition): unknown {
  const raw = d.parameters.find(p => p.key === BUILD_PARAMETER)?.value
  if (raw === undefined) return defaultFirmwareBuild()
  if (typeof raw !== 'string') return null
  try { return JSON.parse(raw) } catch { return null }
}
function shape(value: unknown): value is FirmwareBuild {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false
  const b = value as FirmwareBuild
  const keys = ['target', 'chip', 'flashKb', 'ramBytes', 'stackBytes', 'defines', 'memory']
  if (Object.keys(b).some(k => !keys.includes(k)) || Object.hasOwn(b, 'memory') && !validMemorySettings(b.memory)
    || !['portable', 'c51'].includes(b.target) || typeof b.chip !== 'string' || b.chip.length > 80 || /[\x00-\x1f]/.test(b.chip)
    || !Number.isSafeInteger(b.flashKb) || b.flashKb < 1 || b.flashKb > 1048576
    || !Number.isSafeInteger(b.ramBytes) || b.ramBytes < 128 || b.ramBytes > 1073741824
    || !Number.isSafeInteger(b.stackBytes) || b.stackBytes < 0 || b.stackBytes > 1073741824
    || !b.defines || typeof b.defines !== 'object' || Array.isArray(b.defines)) return false
  const names = [...buildOptions.map(([key]) => key), 'APL_HMI_COMBO_MAX']
  return Object.keys(b.defines).every(k => names.includes(k))
    && buildOptions.every(([key]) => b.defines[key] === 0 || b.defines[key] === 1 || addedFlags.has(key) && b.defines[key] === undefined)
    && [0, 2, 3, 4].includes(b.defines.APL_HMI_COMBO_MAX)
}
/** Invalid persisted input is reported by validation; the editor can still open it. */
export function firmwareBuild(d: Definition): FirmwareBuild { const raw = rawBuild(d); return shape(raw) ? { ...raw, defines: { ...defaultFirmwareBuild().defines, ...raw.defines } } : defaultFirmwareBuild() }
export function setFirmwareBuild(d: Definition, build: FirmwareBuild): Definition {
  const parameter = { id: BUILD_PARAMETER, key: BUILD_PARAMETER, name: '固件编译裁剪', value: JSON.stringify(build) }
  return { ...d, parameters: d.parameters.some(p => p.key === BUILD_PARAMETER)
    ? d.parameters.map(p => p.key === BUILD_PARAMETER ? { ...p, value: parameter.value } : p) : [...d.parameters, parameter] }
}
const values = (d: Definition) => Object.fromEntries(d.parameters.filter(p => typeof p.value === 'number').map(p => [p.key, Number(p.value)]))
export function buildPreset(d: Definition, preset: BuildPreset, previous = firmwareBuild(d)): FirmwareBuild {
  const b = { ...previous, defines: { ...defaultFirmwareBuild().defines } }, p = values(d)
  if (preset === 'full') return b
  b.defines.APL_COMPILE_PURIFY = Number(!!p.purify_enabled)
  b.defines.APL_COMPILE_HUMIDIFY = Number(!!p.humidify_enabled)
  for (const [i, key] of ['APL_COMPILE_UV', 'APL_COMPILE_ANION', 'APL_COMPILE_WIFI'].entries()) b.defines[key as BuildMacro] = Number(!!(p.modules & (1 << i)))
  for (const [i, key] of ['APL_COMPILE_SENSOR_PM25', 'APL_COMPILE_SENSOR_PM10', 'APL_COMPILE_SENSOR_HCHO', 'APL_COMPILE_SENSOR_TVOC', 'APL_COMPILE_SENSOR_HUMIDITY'].entries()) b.defines[key as BuildMacro] = Number(!!(p.sensors & (1 << i)))
  b.defines.APL_CORE_HAS_TURBO = Number(!!p.turbo_enabled)
  if (preset !== 'product') {
    b.defines.APL_HMI_COMPILE_KEY_CALLBACKS = 0
    b.defines.APL_HMI_COMBO_MAX = 2
    b.defines.APL_CORE_COMPILE_FILTER_CACHE = 0
  }
  if (preset === 'minimal') { b.defines.APL_HMI_COMBO_MAX = 0; b.defines.APL_HMI_COMPILE_BUZZER = 0 }
  if (preset === 'core-only') b.defines.APL_COMPILE_HMI = 0
  return b
}
export function c51MemoryLayout(b: FirmwareBuild): { coreBytes: number; hmiBytes: number; schedulerBytes: number } {
  const f = b.defines, aq = Number(!!(f.APL_COMPILE_SENSOR_PM25 || f.APL_COMPILE_SENSOR_PM10 || f.APL_COMPILE_SENSOR_HCHO || f.APL_COMPILE_SENSOR_TVOC))
  const rh = Number(!!(f.APL_COMPILE_HUMIDIFY && f.APL_COMPILE_SENSOR_HUMIDITY)), sensors = Number(!!(aq || rh))
  const coreBytes = 46 + 10 * Number(!!(f.APL_COMPILE_DRY && f.APL_COMPILE_HUMIDIFY)) + 7 * f.APL_COMPILE_FILTER
    + f.APL_COMPILE_FILTER * (f.APL_COMPILE_PURIFY + f.APL_COMPILE_HUMIDIFY) * (4 + 4 * f.APL_COMPILE_FACTORY + f.APL_CORE_COMPILE_FILTER_CACHE)
    + 13 * f.APL_COMPILE_TIMER + 4 * f.APL_CORE_HAS_TURBO + 3 * f.APL_COMPILE_SLEEP + 22 * f.APL_COMPILE_HUMIDIFY
    + 26 * f.APL_COMPILE_WIFI + 10 * f.APL_COMPILE_FACTORY + 3 * f.APL_COMPILE_UV + 2 * f.APL_COMPILE_ANION
    + 2 * aq + 3 * rh + 2 * f.APL_COMPILE_SENSOR_PM25 + 5 * sensors + f.APL_COMPILE_PURIFY
    + 15 * Number(!!(f.APL_COMPILE_HMI && f.APL_HMI_COMPILE_KEYMAP))
  const hmiBytes = f.APL_COMPILE_HMI ? 160 + 63 * f.APL_HMI_COMPILE_KEY_CALLBACKS + 13 * f.APL_HMI_COMBO_MAX
    + 8 * f.APL_HMI_COMPILE_AMBIENT + 8 * f.APL_HMI_COMPILE_BUZZER + 3 * f.APL_COMPILE_TIMER
    + 7 * f.APL_COMPILE_FILTER + f.APL_COMPILE_SLEEP + 7 * f.APL_COMPILE_HUMIDIFY
    + Number(!!(f.APL_HMI_COMPILE_BUZZER || f.APL_COMPILE_HUMIDIFY))
    + 4 * Number(!!(f.APL_COMPILE_SENSOR_PM25 && f.APL_COMPILE_SENSOR_HUMIDITY)) + 2 * sensors
    + 4 * Number(!!(f.APL_COMPILE_TIMER || f.APL_COMPILE_HUMIDIFY || f.APL_COMPILE_WIFI))
    + 3 * f.APL_HMI_COMPILE_KEYMAP : 0
  return { coreBytes, hmiBytes, schedulerBytes: 1 }
}
export function c51InstanceBytes(b: FirmwareBuild): number {
  const p = c51MemoryLayout(b); return p.coreBytes + p.hmiBytes + p.schedulerBytes
}
export function firmwareIssues(d: Definition): string[] {
  const raw = rawBuild(d)
  if (!shape(raw)) return ['固件裁剪配置无效，请重新选择裁剪预设并检查芯片资源预算']
  const f = { ...defaultFirmwareBuild().defines, ...raw.defines }, p = values(d), issues: string[] = []
  if (raw.stackBytes > raw.ramBytes) issues.push('软件栈预留不能超过芯片RAM预算')
  if (!f.APL_COMPILE_PURIFY && !f.APL_COMPILE_HUMIDIFY) issues.push('固件至少编入净化或加湿能力')
  const require = (on: number, key: BuildMacro, name: string) => { if (on && !f[key]) issues.push(`产品启用了${name}，但固件已裁掉；请重新按产品裁剪或关闭该产品功能`) }
  require(p.purify_enabled, 'APL_COMPILE_PURIFY', '净化'); require(p.humidify_enabled, 'APL_COMPILE_HUMIDIFY', '加湿')
  require(p.turbo_enabled, 'APL_CORE_HAS_TURBO', '极速')
  if (!f.APL_COMPILE_SLEEP && (p.default_fan_gear === 0 || p.sensor_fallback_gear === 0 || f.APL_COMPILE_HUMIDIFY && f.APL_COMPILE_DRY && p.dry_fan_gear === 0)) issues.push('已裁掉睡眠模式，请将开机、备用和风干档设置为非睡眠档')
  for (const [i, key, name] of [[0, 'APL_COMPILE_UV', 'UV'], [1, 'APL_COMPILE_ANION', '负离子'], [2, 'APL_COMPILE_WIFI', 'Wi-Fi']] as const) require(p.modules & (1 << i), key, name)
  for (const [i, key, name] of [[0, 'APL_COMPILE_SENSOR_PM25', 'PM2.5'], [1, 'APL_COMPILE_SENSOR_PM10', 'PM10'], [2, 'APL_COMPILE_SENSOR_HCHO', '甲醛'], [3, 'APL_COMPILE_SENSOR_TVOC', 'TVOC'], [4, 'APL_COMPILE_SENSOR_HUMIDITY', '湿度']] as const) {
    require(p.sensors & (1 << i), key, name)
    if (f[key] && !f[i === 4 ? 'APL_COMPILE_HUMIDIFY' : 'APL_COMPILE_PURIFY']) issues.push(`编入${name}传感器需要对应的${i === 4 ? '加湿' : '净化'}能力`)
  }
  return issues
}
/** All translation units include this before public layout macros are evaluated. */
export function targetConfigHeader(b: FirmwareBuild): string {
  const flags = { ...b.defines, APL_DEFAULT_PURIFY: b.defines.APL_COMPILE_PURIFY, APL_DEFAULT_HUMIDIFY: b.defines.APL_COMPILE_HUMIDIFY }
  return '/* Generated by APL Studio. Rebuild ALL library and host sources together. */\n#ifndef APL_TARGET_CONFIG_H\n#define APL_TARGET_CONFIG_H\n'
    + Object.entries(flags).map(([key, value]) => `#ifdef ${key}\n#if ${key} != ${value}u\n#error "APL Studio build macro mismatch: ${key}"\n#endif\n#else\n#define ${key} ${value}u\n#endif\n`).join('') + '#endif\n'
}

/** Logical parameters remain in Studio; target structures exclude absent features. */
export function profileCompileGuard(key: string): string | undefined {
  const guards: Record<string, string> = {
    wifi_pair_s: 'APL_COMPILE_WIFI', pid: 'APL_COMPILE_WIFI',
    dry_fan_gear: 'APL_COMPILE_HAS_DRY', dry_s: 'APL_COMPILE_HAS_DRY',
    filter_shared: 'APL_COMPILE_FILTER', filter_life_h: 'APL_COMPILE_FILTER',
    timer_step_h: 'APL_COMPILE_TIMER', fan_sleep_rpm: 'APL_COMPILE_SLEEP',
    turbo_enabled: 'APL_CORE_HAS_TURBO', fan_turbo_rpm: 'APL_CORE_HAS_TURBO',
    humidity_hysteresis: 'APL_COMPILE_HUMIDIFY', shortage_run_s: 'APL_COMPILE_HUMIDIFY', water_blink_s: 'APL_COMPILE_HUMIDIFY',
  }
  return guards[key]
}

export function profileFieldCompiled(build: FirmwareBuild, key: string): boolean {
  // Keep capability toggles editable so a user can resolve parameter/build conflicts.
  if (key === 'turbo_enabled') return true
  const guard = profileCompileGuard(key)
  if (!guard) return true
  if (guard === 'APL_COMPILE_HAS_DRY') return !!(build.defines.APL_COMPILE_DRY && build.defines.APL_COMPILE_HUMIDIFY)
  return !!build.defines[guard as BuildMacro]
}
