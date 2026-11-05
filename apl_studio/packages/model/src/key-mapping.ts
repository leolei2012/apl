import type { Definition } from './index'
import { firmwareBuild, type BuildMacro } from './firmware'

export const KEY_MAPPING_PARAMETER = 'key_mapping'
export interface ProfileKeyMapping { enabled: boolean; short: number[]; long: number[] }
export const keyMappingSlots: { id: number; key: string; name: string; macro: BuildMacro }[] = [
  { id: 0, key: 'Purify', name: '净化输入', macro: 'APL_HMI_COMPILE_KEY_PURIFY' },
  { id: 1, key: 'Humidity', name: '加湿输入', macro: 'APL_HMI_COMPILE_KEY_HUMIDITY' },
  { id: 2, key: 'Timer', name: '定时输入', macro: 'APL_HMI_COMPILE_KEY_TIMER' },
  { id: 3, key: 'Light', name: '灯光输入', macro: 'APL_HMI_COMPILE_KEY_LIGHT' },
  { id: 4, key: 'Speed', name: '风速输入', macro: 'APL_HMI_COMPILE_KEY_SPEED' },
  { id: 5, key: 'Power', name: '电源输入', macro: 'APL_HMI_COMPILE_KEY_POWER' },
  { id: 6, key: 'Filter', name: '滤芯输入', macro: 'APL_HMI_COMPILE_KEY_FILTER' },
]
export const keyMappingActions = [
  { id: 0, name: '无动作', description: '记录输入，不执行功能或唤醒显示' },
  { id: 1, name: '电源开关', description: '开关机；产测中恢复出厂设置' },
  { id: 2, name: '停止风干', description: '关机风干中立即结束' },
  { id: 3, name: '切换风档', description: '按标准风档顺序循环' },
  { id: 4, name: '极速开关', description: '进入或退出极速档' },
  { id: 5, name: '净化开关', description: '切换净化复合功能' },
  { id: 6, name: 'Wi-Fi 配网', description: '清除网络并进入配网' },
  { id: 7, name: '设置定时', description: '按配置步进编辑定时，短按保持支持连增' },
  { id: 8, name: '童锁开关', description: '开启或解除童锁；锁定时仍可执行此动作' },
  { id: 9, name: '灯光开关', description: '运行时切换整块面板灯光' },
  { id: 10, name: '加湿 / 恒湿', description: '一体机切换加湿，纯加湿机设置目标湿度' },
  { id: 11, name: '设置恒湿', description: '一体机编辑目标湿度，保持标准湿度长按语义' },
  { id: 12, name: '查询滤芯', description: '纯净化机显示滤芯寿命' },
  { id: 13, name: '复位并查询滤芯', description: '纯净化机重置滤芯并显示寿命' },
  { id: 14, name: '睡眠开关', description: '进入或退出睡眠模式' },
  { id: 15, name: '加湿开关', description: '直接切换加湿，不进入恒湿编辑' },
]
export function defaultKeyMapping(): ProfileKeyMapping {
  return { enabled: false, short: [5, 10, 7, 9, 3, 1, 12], long: [6, 11, 8, 0, 4, 2, 13] }
}
export function validKeyMapping(value: unknown): value is ProfileKeyMapping {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false
  const m = value as ProfileKeyMapping
  return Object.keys(m).every(k => ['enabled', 'short', 'long'].includes(k)) && typeof m.enabled === 'boolean'
    && [m.short, m.long].every(a => Array.isArray(a) && a.length === 7 && Array.from(a).every(id => Number.isInteger(id) && id >= 0 && id < keyMappingActions.length))
}
function rawKeyMapping(d: Definition): unknown {
  const p = d.parameters.find(p => p.key === KEY_MAPPING_PARAMETER)
  if (!p) return defaultKeyMapping()
  if (typeof p.value !== 'string') return null
  try { return JSON.parse(p.value) } catch { return null }
}
export function profileKeyMapping(d: Definition): ProfileKeyMapping {
  const m = rawKeyMapping(d)
  return validKeyMapping(m) ? { ...m, short: [...m.short], long: [...m.long] } : defaultKeyMapping()
}
export function setKeyMapping(d: Definition, m: ProfileKeyMapping): Definition {
  const parameter = { id: KEY_MAPPING_PARAMETER, key: KEY_MAPPING_PARAMETER, name: 'APL 内部按键映射', value: JSON.stringify(m) }
  return { ...d, parameters: d.parameters.some(p => p.key === KEY_MAPPING_PARAMETER)
    ? d.parameters.map(p => p.key === KEY_MAPPING_PARAMETER ? { ...p, value: parameter.value } : p) : [...d.parameters, parameter] }
}
export function keyMappingIssues(d: Definition): string[] {
  const m = rawKeyMapping(d), f = firmwareBuild(d).defines
  if (!validKeyMapping(m)) return ['按键映射格式无效：需要七个输入各自的短按、长按动作编号']
  if (m.enabled && (!f.APL_COMPILE_HMI || !f.APL_HMI_COMPILE_KEYMAP)) return ['自定义按键映射需要编入 APL 按键与显示、内部按键映射功能']
  return []
}
/** Product/build availability, independent of transient running/lock/dark state. */
export function keyActionAvailable(d: Definition, id: number): boolean {
  const f = firmwareBuild(d).defines
  const p = Object.fromEntries(d.parameters.filter(p => typeof p.value === 'number').map(p => [p.key, Number(p.value)]))
  switch (id) {
    case 0: case 1: case 3: case 8: case 9: return true
    case 2: return !!(f.APL_COMPILE_DRY && f.APL_COMPILE_HUMIDIFY && p.humidify_enabled)
    case 4: return !!(f.APL_CORE_HAS_TURBO && p.turbo_enabled)
    case 5: return !!(f.APL_COMPILE_UV && (p.modules & 1) || f.APL_COMPILE_ANION && (p.modules & 2))
    case 6: return !!(f.APL_COMPILE_WIFI && p.modules & 4)
    case 7: return !!f.APL_COMPILE_TIMER
    case 10: return !!(f.APL_COMPILE_HUMIDIFY && p.humidify_enabled && (p.purify_enabled || (p.sensors & 16)))
    case 15: return !!(f.APL_COMPILE_HUMIDIFY && p.humidify_enabled)
    case 11: return !!(f.APL_COMPILE_HUMIDIFY && p.humidify_enabled && p.purify_enabled && p.sensors & 16)
    case 12: case 13: return !!(f.APL_COMPILE_FILTER && p.purify_enabled && !p.humidify_enabled)
    case 14: return !!f.APL_COMPILE_SLEEP
    default: return false
  }
}
/** Generates assignments only: the caller must initialize defaults first. */
export function keyMappingC(d: Definition, target: string): string {
  const m = profileKeyMapping(d)
  return '#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP\n'
    + `${target}.enabled = ${Number(m.enabled)}u;\n`
    + m.short.map((id, i) => `${target}.short_action[${i}] = ${id}u;`).join('\n') + '\n'
    + m.long.map((id, i) => `${target}.long_action[${i}] = ${id}u;`).join('\n') + '\n#endif\n'
}
