export const STUDIO_VERSION = '0.1.0'
export type Value = boolean | number | string
export type Kind = 'bool' | 'int' | 'enum' | 'string'
export type Access = 'read' | 'write' | 'readwrite'
export interface EnumOption { label: string; value: number }
export interface FunctionDefinition {
  id: string; key: string; name: string; description: string; kind: Kind; access: Access
  default: Value; unit: string; min: number; max: number; step: number; maxLength: number; options: EnumOption[]
}
export interface Parameter { id: string; key: string; name: string; value: Value }
export interface Definition {
  schemaVersion: 1; name: string; code: string; description: string; category: string
  sdkVersion: string; functions: FunctionDefinition[]; parameters: Parameter[]
  template: { id: string; version: string } | null
}
export interface Product {
  id: string; revision: number; createdAt: string; updatedAt: string; definition: Definition
}
export interface Revision { revision: number; createdAt: string }
export interface Issue { path: string; message: string }
export class ValidationError extends Error {
  statusCode = 400
  constructor(public issues: Issue[]) { super(issues[0]?.message ?? '产品定义无效') }
}

export const kindNames: Record<Kind, string> = { bool: '布尔', int: '整数', enum: '枚举', string: '字符串' }
export const accessNames: Record<Access, string> = { read: '只读', write: '只写', readwrite: '读写' }
export const cKeywords = new Set('auto break case char const continue default do double else enum extern float for goto if inline int long register restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Alignas _Alignof _Atomic _Bool _Complex _Generic _Imaginary _Noreturn _Static_assert _Thread_local'.split(' '))
export function identifier(value: unknown): value is string {
  return typeof value === 'string' && /^[a-z][a-z0-9_]{0,47}$/.test(value) && !cKeywords.has(value)
}
function plain(value: unknown): value is Record<string, unknown> { return !!value && typeof value === 'object' && !Array.isArray(value) }
function integer(value: unknown): value is number { return typeof value === 'number' && Number.isInteger(value) && value >= -2147483648 && value <= 2147483647 }
function text(value: unknown, max: number, required = false): value is string {
  return typeof value === 'string' && value.length <= max && (!required || value.trim().length > 0) && !value.includes('\0')
}
export function validateValue(field: FunctionDefinition, value: unknown): string | null {
  switch (field.kind) {
    case 'bool': return typeof value === 'boolean' ? null : '应为布尔值'
    case 'int': return integer(value) && value >= field.min && value <= field.max && (value - field.min) % field.step === 0 ? null : `应为 ${field.min}–${field.max} 内、步进为 ${field.step} 的整数`
    case 'enum': return integer(value) && field.options.some(item => item.value === value) ? null : '请选择已定义的枚举值'
    case 'string': return text(value, field.maxLength) ? null : `文本不能超过 ${field.maxLength} 个字符，也不能包含空字符`
  }
}
export function validateDefinition(input: unknown): Issue[] {
  const issues: Issue[] = []
  const add = (path: string, message: string) => issues.push({ path, message })
  if (!plain(input)) return [{ path: '', message: '产品定义必须是对象' }]
  const keys = ['schemaVersion', 'name', 'code', 'description', 'category', 'sdkVersion', 'functions', 'parameters', 'template']
  for (const key of Object.keys(input)) if (!keys.includes(key)) add(key, `未知产品字段：${key}`)
  if (input.schemaVersion !== 1) add('schemaVersion', '不支持的产品定义版本，需要 schemaVersion = 1')
  if (!text(input.name, 80, true) || /[\r\n]/.test(String(input.name))) add('name', '产品名称需要 1–80 个字符，不能换行')
  if (!identifier(input.code)) add('code', '产品标识需以小写字母开头，使用小写字母、数字和下划线，最多 48 位')
  if (!text(input.description, 2000)) add('description', '产品说明不能超过 2000 字')
  if (!text(input.category, 40, true)) add('category', '请填写产品品类，最多 40 字')
  if (typeof input.sdkVersion !== 'string' || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(input.sdkVersion) || input.sdkVersion.length > 32) add('sdkVersion', 'SDK 版本格式为主版本.次版本.修订号，例如 0.1.0')
  if (input.template !== null && (!plain(input.template) || !text(input.template.id, 80, true) || !text(input.template.version, 32, true) || Object.keys(input.template).some(k => !['id', 'version'].includes(k)))) add('template', '源码模板引用无效')
  if (!Array.isArray(input.functions)) add('functions', '功能列表无效')
  else {
    const names = new Set<string>(), ids = new Set<string>()
    input.functions.forEach((raw, index) => {
      const path = `functions.${index}`
      if (!plain(raw)) { add(path, '功能定义必须是对象'); return }
      const allowed = ['id', 'key', 'name', 'description', 'kind', 'access', 'default', 'unit', 'min', 'max', 'step', 'maxLength', 'options']
      for (const key of Object.keys(raw)) if (!allowed.includes(key)) add(path, `未知功能字段：${key}`)
      if (!text(raw.id, 80, true) || ids.has(String(raw.id))) add(path + '.id', '功能内部 ID 缺失或重复')
      ids.add(String(raw.id))
      if (!identifier(raw.key) || names.has(String(raw.key))) add(path + '.key', '功能标识无效、属于 C 保留字或已存在')
      names.add(String(raw.key))
      if (!text(raw.name, 80, true)) add(path + '.name', '功能名称需要 1–80 字')
      if (!text(raw.description, 1000) || !text(raw.unit, 20)) add(path, '功能说明或单位过长')
      if (!['bool', 'int', 'enum', 'string'].includes(String(raw.kind))) add(path + '.kind', '功能数据类型无效')
      if (!['read', 'write', 'readwrite'].includes(String(raw.access))) add(path + '.access', '功能访问权限无效')
      if (!integer(raw.min) || !integer(raw.max) || raw.min > raw.max || !integer(raw.step) || raw.step <= 0) add(path, '整数范围或步进无效')
      if (!integer(raw.maxLength) || raw.maxLength < 1 || raw.maxLength > 4096) add(path + '.maxLength', '字符串长度应为 1–4096')
      if (!Array.isArray(raw.options)) add(path + '.options', '枚举列表无效')
      else {
        const values = new Set<number>()
        raw.options.forEach(option => {
          if (!plain(option) || Object.keys(option).some(key => !['label', 'value'].includes(key)) || !text(option.label, 80, true) || !integer(option.value) || values.has(option.value)) add(path + '.options', '枚举名称或数值无效，存在未知字段，或数值重复')
          else values.add(option.value)
        })
        if (raw.kind === 'enum' && raw.options.length === 0) add(path + '.options', '枚举至少需要一个选项')
      }
      if (!issues.some(issue => issue.path === path || issue.path.startsWith(path + '.'))) {
        const error = validateValue(raw as unknown as FunctionDefinition, raw.default)
        if (error) add(path + '.default', '默认值：' + error)
      }
    })
  }
  if (!Array.isArray(input.parameters)) add('parameters', '参数列表无效')
  else {
    const names = new Set<string>(), ids = new Set<string>()
    input.parameters.forEach((raw, index) => {
      const path = `parameters.${index}`
      if (!plain(raw)) { add(path, '参数必须是对象'); return }
      if (Object.keys(raw).some(k => !['id', 'key', 'name', 'value'].includes(k))) add(path, '存在未知参数字段')
      if (!text(raw.id, 80, true) || ids.has(String(raw.id))) add(path, '参数内部 ID 无效或重复')
      ids.add(String(raw.id))
      if (!identifier(raw.key) || names.has(String(raw.key))) add(path + '.key', '参数标识无效、属于 C 保留字或已存在')
      names.add(String(raw.key))
      if (!text(raw.name, 80, true)) add(path + '.name', '参数名称需要 1–80 字')
      if (!(typeof raw.value === 'boolean' || integer(raw.value) || text(raw.value, 4096))) add(path + '.value', '参数仅支持布尔、32 位整数或文本')
    })
  }
  return issues
}
export function parseDefinition(input: unknown): Definition {
  const issues = validateDefinition(input)
  if (issues.length) throw new ValidationError(issues)
  return structuredClone(input as Definition)
}
export function newFunction(id: string, kind: Kind = 'bool'): FunctionDefinition {
  return { id, key: 'new_function', name: '新功能', description: '', kind, access: 'readwrite', default: kind === 'bool' ? false : kind === 'string' ? '' : 0, unit: '', min: 0, max: 100, step: 1, maxLength: 64, options: kind === 'enum' ? [{ label: '关闭', value: 0 }, { label: '开启', value: 1 }] : [] }
}
export function emptyDefinition(): Definition {
  return { schemaVersion: 1, name: '未命名产品', code: 'my_product', description: '', category: '自定义产品', sdkVersion: '0.1.0', functions: [], parameters: [], template: null }
}
export function exampleDefinition(id: () => string): Definition {
  return { ...emptyDefinition(), name: '空气净化加湿一体机', code: 'air_care', category: '环境电器', description: '从产品功能开始，定义净化、加湿与环境数据。生成统一的产品定义，供后续 APL 源码模板使用。',
    functions: [
      { ...newFunction(id()), key: 'power', name: '电源', description: '整机电源开关' },
      { ...newFunction(id(), 'enum'), key: 'fan_speed', name: '风机档位', default: 1, options: [{ label: '睡眠', value: 0 }, { label: '一档', value: 1 }, { label: '二档', value: 2 }, { label: '三档', value: 3 }, { label: '自动', value: 4 }] },
      { ...newFunction(id()), key: 'humidify', name: '加湿', default: true, description: '加湿功能开关' },
      { ...newFunction(id(), 'int'), key: 'humidity', name: '环境湿度', access: 'read', default: 55, unit: '%' },
      { ...newFunction(id(), 'int'), key: 'pm25', name: 'PM2.5', access: 'read', default: 28, max: 1000, unit: 'μg/m³' },
    ], parameters: [{ id: id(), key: 'tick_ms', name: '业务推进周期（ms）', value: 10 }, { id: id(), key: 'filter_life_h', name: '滤芯寿命（小时）', value: 4320 }],
  }
}
function comment(value: string): string { return value.replace(/\*\//g, '* /').replace(/[\r\n]/g, ' ') }
export function interfaceHeader(definition: Definition): string {
  const d = parseDefinition(definition), prefix = `apl_${d.code}`, guard = `${prefix}_contract_h`.toUpperCase()
  const lines = [`/* ${comment(d.name)} — 产品接口契约预览。仅声明接口，不包含 APL 业务实现。 */`, `#ifndef ${guard}`, `#define ${guard}`, '', '#include <stdint.h>', '#include <stdbool.h>', '#include <stddef.h>', '', '#ifdef __cplusplus', 'extern "C" {', '#endif', '']
  for (const f of d.functions) {
    lines.push(`/* ${comment(f.name)}${f.unit ? ' (' + comment(f.unit) + ')' : ''} · ${accessNames[f.access]}${f.description ? ' · ' + comment(f.description) : ''} */`)
    if (f.kind === 'enum') {
      lines.push(`enum ${prefix}_${f.key} {`, ...f.options.map(o => `    ${prefix.toUpperCase()}_${f.key.toUpperCase()}_${o.value < 0 ? 'NEG_' + Math.abs(o.value) : o.value} = ${o.value}, /* ${comment(o.label)} */`), '};')
    }
    const type = f.kind === 'bool' ? 'bool' : f.kind === 'enum' ? `enum ${prefix}_${f.key}` : 'int32_t'
    if (f.access !== 'write') lines.push(f.kind === 'string' ? `size_t ${prefix}_get_${f.key}(char *buffer, size_t capacity);` : `${type} ${prefix}_get_${f.key}(void);`)
    if (f.access !== 'read') lines.push(f.kind === 'string' ? `bool ${prefix}_set_${f.key}(const char *value);` : `bool ${prefix}_set_${f.key}(${type} value);`)
    lines.push('')
  }
  lines.push('#ifdef __cplusplus', '}', '#endif', '', `#endif /* ${guard} */`, '')
  return lines.join('\n')
}
