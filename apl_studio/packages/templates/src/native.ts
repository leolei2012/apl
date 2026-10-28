import { readFileSync, readdirSync, lstatSync } from 'node:fs'
import { resolve, posix } from 'node:path'
import { createHash } from 'node:crypto'
import { parseDefinition, identifier, ValidationError, type Definition } from '@apl-studio/model'
import { applyTemplate, activeFor, parameterIssue, validateTemplateConfiguration, type ConfigBinding, type TemplateDescriptor } from '@apl-studio/model/template'
import type { TemplateProvider } from './index'
import { safeRelative } from './paths'

interface Manifest extends Omit<TemplateDescriptor, 'fingerprint'> {
  formatVersion: 1; engine: 'native-c-v1'; directories: string[]; staticFiles: string[]
}
const text = (value: Uint8Array) => Buffer.from(value).toString('utf8')
/** Generic C11 templates own their API, source tree, hardware ports and consumer. */
export class NativeCTemplate implements TemplateProvider {
  readonly descriptor: TemplateDescriptor
  private files = new Map<string, Uint8Array>()
  constructor(directory: string) {
    const raw = readFileSync(resolve(directory, 'template.json'))
    const manifest = JSON.parse(text(raw)) as Manifest
    if (manifest.formatVersion !== 1 || manifest.engine !== 'native-c-v1' || manifest.definitionSchemaVersion !== 1
      || !/^[a-z][a-z0-9.-]+$/.test(manifest.id) || !/^\d+\.\d+\.\d+$/.test(manifest.version)
      || !manifest.sourceVersion || !Array.isArray(manifest.features) || !Array.isArray(manifest.parameters)
      || !Array.isArray(manifest.ports) || !Array.isArray(manifest.constraints)) throw new Error('通用 C 模板描述无效')
    if (manifest.productDefaults && (!identifier(manifest.productDefaults.code) || !manifest.productDefaults.category?.trim())) throw new Error('产品默认标识或品类无效')
    const add = (path: string) => {
      safeRelative(path)
      const target = resolve(directory, path)
      if (!lstatSync(target).isFile() || lstatSync(target).isSymbolicLink()) throw new Error('模板只允许普通文件：' + path)
      if ([...this.files.keys()].some(key => key.toLowerCase() === path.toLowerCase())) throw new Error('模板文件重复：' + path)
      if (['manifest.json', 'product.apl.json', 'metadata/template.json'].includes(path.toLowerCase())) throw new Error('模板占用了保留文件：' + path)
      this.files.set(path, readFileSync(target))
    }
    const walk = (folder: string) => {
      for (const item of readdirSync(resolve(directory, safeRelative(folder)), { withFileTypes: true })) {
        const path = safeRelative(posix.join(folder, item.name))
        if (item.isSymbolicLink()) throw new Error('模板不能包含符号链接：' + path)
        if (item.isDirectory()) walk(path)
        else if (item.isFile()) add(path)
      }
    }
    for (const folder of manifest.directories) {
      const parts = safeRelative(folder).split('/')
      for (let i=1;i<=parts.length;i++) if (lstatSync(resolve(directory, parts.slice(0,i).join('/'))).isSymbolicLink()) throw new Error('模板不能包含符号链接')
      walk(folder)
    }
    for (const path of manifest.staticFiles) {
      const parts = safeRelative(path).split('/')
      for (let i=1;i<parts.length;i++) if (lstatSync(resolve(directory, parts.slice(0,i).join('/'))).isSymbolicLink()) throw new Error('模板不能包含符号链接')
      add(path)
    }
    const verification = manifest.verification
    if (!verification || !verification.scope?.trim() || !Array.isArray(verification.sourceDirectories) || !verification.sourceDirectories.length || !Array.isArray(verification.includeDirectories)) throw new Error('模板缺少本机 C 验证声明')
    for (const path of [...verification.sourceDirectories, ...verification.includeDirectories, verification.consumer]) safeRelative(path)
    if (!verification.consumer.endsWith('.c') || !this.files.has(verification.consumer)) throw new Error('模板缺少消费验证程序')
    if (!manifest.previewFiles?.length || manifest.previewFiles.some(path => !this.files.has(safeRelative(path)))) throw new Error('模板预览文件不存在')
    const features = new Set<string>(), parameters = new Set<string>(), bindings = new Set<string>()
    const checkBinding = (binding?: ConfigBinding) => {
      if (!binding) return
      safeRelative(binding.path)
      if (!/^[A-Z][A-Z0-9_]*$/.test(binding.macro) || !binding.path.endsWith('.h')) throw new Error('模板配置绑定无效')
      const key = binding.path + ':' + binding.macro
      if (bindings.has(key)) throw new Error('模板配置宏重复绑定：' + key)
      bindings.add(key); this.replaceMacro(new Map(this.files), binding, 1)
    }
    for (const feature of manifest.features) {
      if (!identifier(feature.key) || features.has(feature.key) || feature.definition.key !== feature.key) throw new Error('模板功能标识无效或重复')
      features.add(feature.key)
      for (const binding of feature.enabledBindings ?? []) checkBinding(binding)
      checkBinding(feature.defaultBinding)
    }
    for (const parameter of manifest.parameters) {
      if (!identifier(parameter.key) || parameters.has(parameter.key) || parameterIssue(parameter, parameter.default)) throw new Error('模板参数定义无效')
      parameters.add(parameter.key); checkBinding(parameter.binding)
    }
    for (const item of [...manifest.features, ...manifest.parameters, ...manifest.ports]) for (const key of item.requires ?? []) if (!features.has(key)) throw new Error('依赖未知功能：' + key)
    for (const constraint of manifest.constraints) if (constraint.kind !== 'atLeastOne' || !constraint.features.length || constraint.features.some(key => !features.has(key))) throw new Error('模板约束无效')
    const hash = createHash('sha256').update(raw)
    for (const [path, data] of [...this.files].sort(([a], [b]) => a.localeCompare(b))) hash.update(path).update('\0').update(data).update('\0')
    const { directories, staticFiles, formatVersion, ...descriptor } = manifest
    this.descriptor = { ...descriptor, fingerprint: hash.digest('hex') }
    const sample = applyTemplate({ schemaVersion: 1, name: '模板自检', code: 'template_check', category: '自检', sdkVersion: '0.1.0', description: '', functions: [], parameters: [], template: null }, this.descriptor)
    const issues = this.validate(sample)
    if (issues.length) throw new Error('模板默认配置无效：' + issues.join('；'))
  }
  validate(definition: Definition) { return validateTemplateConfiguration(definition, this.descriptor) }
  private replaceMacro(files: Map<string, Uint8Array>, binding: ConfigBinding, value: number | boolean) {
    const data = files.get(binding.path)
    if (!data) throw new Error('配置头缺失：' + binding.path)
    const pattern = new RegExp('^[ \t]*#\\s*define[ \t]+' + binding.macro + '[ \t]+[^\\r\\n]*', 'gm')
    const source = text(data)
    if ([...source.matchAll(pattern)].length !== 1) throw new Error('配置宏缺失或重复：' + binding.macro)
    files.set(binding.path, Buffer.from(source.replace(pattern, '#define ' + binding.macro + ' ' + Number(value))))
  }
  async generate(input: Definition) {
    const definition = parseDefinition(input), issues = this.validate(definition)
    if (issues.length) throw new ValidationError(issues.map(message => ({ path: 'template', message })))
    const enabled = new Set(definition.functions.map(f => f.key))
    const substitutions: Record<string, string> = { PRODUCT_CODE: definition.code, API_PREFIX: 'sdk_' + definition.code, SDK_VERSION: definition.sdkVersion }
    const files = new Map([...this.files].map(([path, data]) => [path, Buffer.from(text(data).replace(/\{\{([A-Z_]+)\}\}/g, (_, key: string) => {
      if (!(key in substitutions)) throw new Error('未知源码占位符：' + key)
      return substitutions[key]
    }))]))
    for (const feature of this.descriptor.features) {
      for (const binding of feature.enabledBindings ?? []) this.replaceMacro(files, binding, enabled.has(feature.key))
      const selected = definition.functions.find(f => f.key === feature.key)
      if (selected && feature.defaultBinding) this.replaceMacro(files, feature.defaultBinding, selected.default as number | boolean)
    }
    for (const parameter of this.descriptor.parameters) if (parameter.binding && activeFor(parameter.requires, enabled)) this.replaceMacro(files, parameter.binding, definition.parameters.find(p => p.key === parameter.key)!.value as number | boolean)
    files.set('product.apl.json', Buffer.from(JSON.stringify(definition, null, 2) + '\n'))
    files.set('metadata/template.json', Buffer.from(JSON.stringify(this.descriptor, null, 2) + '\n'))
    return files
  }
}
