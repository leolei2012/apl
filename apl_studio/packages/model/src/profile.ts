import { BUILD_PARAMETER, firmwareIssues } from './firmware'
import { KEY_MAPPING_PARAMETER, keyMappingIssues } from './key-mapping'
import type { ProfileKeyMapping } from './key-mapping'
import type { Definition } from './index'
import { validateDefinition } from './index'
import type { TemplateDescriptor } from './template'

export const PROFILE_ID = 'apl.runtime-profile'
export interface ProfileField { key: string; name: string; cType: string; default: number; min: number; max: number; unit: string; group: string; description: string }
export interface ProfileSchema {
  schemaId: string; schemaVersion: number; schemaRevision: number; aplVersion: string; releaseStatus: string
  source: { repository: string; tag: string | null; commit: string | null }
  parameters: ProfileField[]
  identity: { pid: { default: string; maxLength: number; pattern: string; description: string } }
  keyMapping: { default: ProfileKeyMapping; slots: { id: number; key: string; name: string }[]; actions: { id: number; key: string; name: string; description: string }[] }
}
export interface ProfileCatalog {
  schema: ProfileSchema; template: TemplateDescriptor
  source: { repository: string; baseCommit: string; dirty: boolean; fingerprint: string }
}
export interface ProductFeatures { purify: boolean; humidify: boolean }
export function profileValues(d: Definition): Record<string, number> { return Object.fromEntries(d.parameters.filter(p => typeof p.value === 'number').map(p => [p.key, Number(p.value)])) }
export function productFeatures(d: Definition): ProductFeatures { const p = profileValues(d); return { purify: !!p.purify_enabled, humidify: !!p.humidify_enabled } }
export function featureSummary(d: Definition): string { const f = productFeatures(d); return [f.purify && '净化', f.humidify && '加湿'].filter(Boolean).join(' + ') || '未选择功能' }
export function sourceFingerprint(d: Definition): string { return String(d.parameters.find(p => p.key === 'source_fingerprint')?.value ?? '') }
export function profilePid(d: Definition): string { return String(d.parameters.find(p => p.key === 'pid')?.value ?? '') }
/** Explicit source rebind: preserve product choices, update format and add new defaults. */
export function bindCurrentProfile(d: Definition, catalog: ProfileCatalog): Definition {
  const parameters = d.parameters.map(p => p.key === 'version' ? { ...p, value: catalog.schema.schemaVersion }
    : p.key === 'source_fingerprint' ? { ...p, value: catalog.source.fingerprint } : { ...p })
  for (const field of catalog.schema.parameters) if (!parameters.some(p => p.key === field.key))
    parameters.push({ id: field.key, key: field.key, name: field.name, value: field.default })
  if (!parameters.some(p => p.key === 'source_fingerprint')) parameters.push({ id: 'source_fingerprint', key: 'source_fingerprint', name: 'APL源码与参数定义指纹', value: catalog.source.fingerprint })
  if (!parameters.some(p => p.key === 'pid')) parameters.push({ id: 'pid', key: 'pid', name: '产品 PID', value: catalog.schema.identity.pid.default })
  return { ...d, template: { id: PROFILE_ID, version: catalog.schema.aplVersion }, parameters }
}
export function newProfile(catalog: ProfileCatalog, name: string, features: ProductFeatures = { purify: true, humidify: false }): Definition {
  const d: Definition = { schemaVersion: 1, name, code: 'device_' + crypto.randomUUID().replaceAll('-', '').slice(0, 12), category: '环境电器', description: '', sdkVersion: '0.1.0', functions: [],
    template: { id: PROFILE_ID, version: catalog.schema.aplVersion }, parameters: catalog.schema.parameters.map(p => ({ id: p.key, key: p.key, name: p.name, value: p.default })) }
  d.parameters.push({ id: 'source_fingerprint', key: 'source_fingerprint', name: 'APL源码与参数定义指纹', value: catalog.source.fingerprint })
  d.parameters.push({ id: 'pid', key: 'pid', name: '产品 PID', value: catalog.schema.identity.pid.default })
  // The schema defaults describe a combined device. Start without either family
  // so enabling a feature below selects its default sensor exactly once.
  return setProductFeatures(setProfile(setProfile(setProfile(d, 'purify_enabled', 0), 'humidify_enabled', 0), 'sensors', 0), features)
}
export function setProfile(d: Definition, key: string, value: number | string): Definition { return { ...d, parameters: d.parameters.map(p => p.key === key ? { ...p, value } : p) } }
export function setProductFeatures(d: Definition, { purify, humidify }: ProductFeatures): Definition {
  const previous = productFeatures(d), p = profileValues(d)
  let next = setProfile(setProfile(d, 'purify_enabled', Number(purify)), 'humidify_enabled', Number(humidify))
  let sensors = p.sensors
  if (!purify) sensors &= ~15
  else if (!previous.purify) sensors |= 1
  if (!humidify) sensors &= ~16
  else if (!previous.humidify) sensors |= 16
  next = setProfile(next, 'sensors', sensors)
  return { ...next, category: '环境电器' }
}
export function validateProfile(d: Definition, catalog: ProfileCatalog): string[] {
  const issues = validateDefinition(d).map(i => i.message)
  if (d.template?.id !== PROFILE_ID || d.template.version !== catalog.schema.aplVersion) issues.push('此产品绑定的 APL 版本不可用')
  if (sourceFingerprint(d) !== catalog.source.fingerprint) issues.push('APL 源码或参数定义已变化，请确认并更新版本绑定')
  const known = new Set([...catalog.schema.parameters.map(p => p.key), 'source_fingerprint', 'pid', BUILD_PARAMETER, KEY_MAPPING_PARAMETER])
  const pid = d.parameters.find(p => p.key === 'pid')?.value
  const identity = catalog.schema.identity.pid
  if (typeof pid !== 'string' || pid.length > identity.maxLength || new RegExp(identity.pattern).exec(pid)?.[0] !== pid) issues.push(`产品 PID：请输入最多 ${identity.maxLength} 位英文字母或数字，可留空`)
  if (d.parameters.some(p => !known.has(p.key)) || d.functions.length) issues.push('包含当前 APL 不支持的配置项')
  for (const field of catalog.schema.parameters) {
    const value = d.parameters.find(p => p.key === field.key)?.value
    if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < field.min || value > field.max) issues.push(`${field.name}：请输入 ${field.min}～${field.max} 的整数`)
  }
  issues.push(...firmwareIssues(d))
  issues.push(...keyMappingIssues(d))
  const p = profileValues(d)
  if (p.version !== catalog.schema.schemaVersion) issues.push('参数格式版本不匹配')
  if (!p.purify_enabled && !p.humidify_enabled) issues.push('至少启用净化或加湿')
  if (!p.purify_enabled && (p.sensors & 15)) issues.push('空气质量传感器需要净化功能')
  if (!p.humidify_enabled && (p.sensors & 16)) issues.push('湿度传感器需要加湿功能')
  return [...new Set(issues)]
}
