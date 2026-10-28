import { parseDefinition, validateDefinition, validateValue, type Definition, type FunctionDefinition, type Value } from './index'

export interface ConfigBinding { path: string; macro: string }
export interface TemplateFeature {
  key: string; name: string; description: string; required?: boolean; enabledByDefault?: boolean; requires?: string[]
  definition: FunctionDefinition
  enabledBindings?: ConfigBinding[]; defaultBinding?: ConfigBinding
}
export interface TemplateParameter {
  key: string; name: string; description: string; kind: 'int' | 'bool' | 'enum'; default: number | boolean
  min?: number; max?: number; step?: number; options?: { label: string; value: number }[]; unit?: string; requires?: string[]; binding?: ConfigBinding
}
export interface TemplatePort { key: string; name: string; signature: string; description: string; required: boolean; requires?: string[] }
export interface NativeVerification {
  sourceDirectories: string[]; includeDirectories: string[]; consumer: string; scope: string
}
export interface TemplateDescriptor {
  engine?: 'apl-air-care-v1' | 'native-c-v1'
  productDefaults?: { code: string; category: string }
  previewFiles?: string[]
  verification?: NativeVerification
  id: string; version: string; name: string; description: string; definitionSchemaVersion: 1; sourceVersion: string; fingerprint: string
  features: TemplateFeature[]; parameters: TemplateParameter[]; ports: TemplatePort[]
  constraints: { kind: 'atLeastOne'; features: string[]; message: string }[]
}
export interface BuildJob {
  id: string; productId: string; revision: number; productName: string; productCode: string; sdkVersion: string
  template: { id: string; version: string; fingerprint: string }; createdAt: string; updatedAt: string
  sourceVersion?: string; verificationScope?: string
  status: 'queued' | 'building' | 'succeeded' | 'failed' | 'cancelled'
  phase: string; diagnostics: string; compilerVersion: string; fingerprint?: string; filename?: string
  checks: { compile: 'pending' | 'passed' | 'failed'; consumer: 'pending' | 'passed' | 'failed' }
}
export function activeFor(requires: string[] | undefined, enabled: Set<string>): boolean { return !requires || requires.every(key => enabled.has(key)) }
export function parameterIssue(parameter: TemplateParameter, value: unknown): string | null {
  if (parameter.kind === 'bool') return typeof value === 'boolean' ? null : '需要布尔值'
  if (typeof value !== 'number' || !Number.isInteger(value)) return '需要整数'
  if (parameter.kind === 'enum') return parameter.options?.some(o => o.value === value) ? null : '不在模板支持的选项中'
  const min = parameter.min ?? 0, max = parameter.max ?? 2147483647, step = parameter.step ?? 1
  return value >= min && value <= max && (value - min) % step === 0 ? null : `需要 ${min}–${max} 内、步进为 ${step} 的整数`
}
export function validateTemplateConfiguration(definition: Definition, template: TemplateDescriptor): string[] {
  const errors = validateDefinition(definition).map(issue => issue.message)
  if (definition.template?.id !== template.id || definition.template.version !== template.version) errors.push('产品没有绑定此模板的精确版本')
  const enabled = new Set(definition.functions.map(f => f.key))
  for (const feature of template.features) {
    const field = definition.functions.find(f => f.key === feature.key)
    if (!field) { if (feature.required) errors.push(`缺少必选功能：${feature.name}`); continue }
    if (!activeFor(feature.requires, enabled)) errors.push(`${feature.name}依赖：${feature.requires?.join('、')}`)
    const canonical = feature.definition
    if (field.kind !== canonical.kind || field.access !== canonical.access) errors.push(`${feature.name}的数据类型或访问方式与模板不一致`)
    if (field.min !== canonical.min || field.max !== canonical.max || field.step !== canonical.step || field.maxLength !== canonical.maxLength
      || JSON.stringify(field.options.map(o => o.value)) !== JSON.stringify(canonical.options.map(o => o.value))) errors.push(`${feature.name}的取值约束与模板不一致，请重新应用模板`)
    if (validateValue(canonical, field.default)) errors.push(`${feature.name}的默认值不在模板支持范围内`)
  }
  for (const field of definition.functions) if (!template.features.some(f => f.key === field.key)) errors.push(`模板不支持功能：${field.key}`)
  for (const constraint of template.constraints) if (!constraint.features.some(key => enabled.has(key))) errors.push(constraint.message)
  const active = template.parameters.filter(p => activeFor(p.requires, enabled))
  for (const parameter of active) {
    const value = definition.parameters.find(p => p.key === parameter.key)
    const issue = value ? parameterIssue(parameter, value.value) : '尚未配置'
    if (issue) errors.push(`${parameter.name}：${issue}`)
  }
  for (const parameter of definition.parameters) if (!active.some(p => p.key === parameter.key)) errors.push(`模板不支持或当前功能不使用参数：${parameter.key}`)
  return errors
}
/** Explicitly applying a template normalizes the model; ordinary save never discards fields. */
export function applyTemplate(definition: Definition, template: TemplateDescriptor, selection?: Set<string>): Definition {
  const enabled = selection ? new Set(selection) : new Set(definition.functions.length
    ? definition.functions.filter(f => template.features.some(t => t.key === f.key)).map(f => f.key)
    : template.features.filter(f => f.enabledByDefault).map(f => f.key))
  for (const feature of template.features) if (feature.required) enabled.add(feature.key)
  if (!selection) {
    let changed = true
    while (changed) { changed = false; for (const feature of template.features) if (enabled.has(feature.key)) for (const dependency of feature.requires ?? []) if (!enabled.has(dependency)) { enabled.add(dependency); changed = true } }
  }
  return parseDefinition({ ...definition, template: { id: template.id, version: template.version },
    functions: template.features.filter(f => enabled.has(f.key)).map(feature => {
      const previous = definition.functions.find(f => f.key === feature.key), canonical = structuredClone(feature.definition)
      return { ...canonical, id: previous?.id ?? `template_${feature.key}`, name: previous?.name ?? canonical.name,
        default: previous && !validateValue(canonical, previous.default) ? previous.default : canonical.default }
    }),
    parameters: template.parameters.filter(p => activeFor(p.requires, enabled)).map(parameter => {
      const previous = definition.parameters.find(p => p.key === parameter.key)
      return { id: previous?.id ?? `parameter_${parameter.key}`, key: parameter.key, name: parameter.name,
        value: previous && !parameterIssue(parameter, previous.value) ? previous.value : parameter.default }
    }),
  })
}
export function toggleTemplateFeature(definition: Definition, template: TemplateDescriptor, key: string, on: boolean): Definition {
  const enabled = new Set(definition.functions.map(f => f.key))
  const feature = template.features.find(f => f.key === key)
  if (!feature || feature.required) return definition
  if (on) {
    enabled.add(key)
    let changed = true
    while (changed) { changed = false; for (const f of template.features) if (enabled.has(f.key)) for (const dependency of f.requires ?? []) if (!enabled.has(dependency)) { enabled.add(dependency); changed = true } }
  } else {
    enabled.delete(key)
    let changed = true
    while (changed) { changed = false; for (const f of template.features) if (enabled.has(f.key) && !activeFor(f.requires, enabled)) { enabled.delete(f.key); changed = true } }
  }
  return applyTemplate(definition, template, enabled)
}

/** The template owns product identity defaults; no product family is built into the UI. */
export function definitionFromTemplate(template: TemplateDescriptor, existingCodes: string[] = []): Definition {
  const base = template.productDefaults?.code ?? 'product'
  let code = base, number = 2
  while (existingCodes.includes(code)) code = base.slice(0, 38) + '_' + number++
  return applyTemplate({ schemaVersion: 1, name: template.name + '产品', code,
    category: template.productDefaults?.category ?? '自定义产品', description: '', sdkVersion: '0.1.0',
    functions: [], parameters: [], template: null }, template)
}
