import type { Context } from 'cordis'
import { readdirSync, readFileSync } from 'node:fs'
import { resolve, relative, posix } from 'node:path'
import { createHash } from 'node:crypto'
import { parseDefinition, identifier, ValidationError, type Definition } from '@apl-studio/model'
import { applyTemplate, activeFor, validateTemplateConfiguration, type TemplateDescriptor, type ConfigBinding } from '@apl-studio/model/template'
export type { TemplateDescriptor } from '@apl-studio/model/template'

export interface TemplateProvider {
  descriptor: TemplateDescriptor
  validate(definition: Definition): string[]
  generate(definition: Definition): Promise<Map<string, Uint8Array>>
}
interface Manifest extends Omit<TemplateDescriptor, 'fingerprint'> { formatVersion: 1; directories: string[]; staticFiles: string[]; adapter: string }
interface Adapter { get?: string; set?: string; initialise?: string }
export { safeRelative } from './paths'
import { safeRelative } from './paths'
import { NativeCTemplate } from './native'
export { NativeCTemplate } from './native'

const text = (bytes: Uint8Array) => Buffer.from(bytes).toString('utf8')
const bytes = (value: string) => Buffer.from(value, 'utf8')
const comment = (value: string) => value.replace(/\*\//g, '* /').replace(/[\r\n]/g, ' ')

export class CSourceTemplate implements TemplateProvider {
  readonly descriptor: TemplateDescriptor
  private files = new Map<string, Uint8Array>()
  private adapters: Record<string, Adapter>
  constructor(directory: string) {
    const raw = readFileSync(resolve(directory, 'template.json'))
    const manifest = JSON.parse(raw.toString('utf8')) as Manifest
    if (manifest.formatVersion !== 1 || manifest.definitionSchemaVersion !== 1 || !/^[a-z][a-z0-9.-]+$/.test(manifest.id)
      || !/^\d+\.\d+\.\d+$/.test(manifest.version) || !Array.isArray(manifest.features) || !Array.isArray(manifest.parameters) || !Array.isArray(manifest.ports) || !Array.isArray(manifest.constraints)) throw new Error('不支持的源码模板描述：' + directory)
    const adapterBytes = readFileSync(resolve(directory, safeRelative(manifest.adapter)))
    this.adapters = JSON.parse(adapterBytes.toString('utf8'))
    const walk = (folder: string) => {
      for (const entry of readdirSync(resolve(directory, folder), { withFileTypes: true })) {
        const path = safeRelative(posix.join(folder, entry.name))
        if (entry.isSymbolicLink()) throw new Error('模板不能包含符号链接：' + path)
        if (entry.isDirectory()) walk(path)
        else if (entry.isFile()) this.files.set(path, readFileSync(resolve(directory, path)))
      }
    }
    for (const folder of manifest.directories) walk(safeRelative(folder))
    for (const file of manifest.staticFiles) this.files.set(safeRelative(file), readFileSync(resolve(directory, file)))
    for (const required of ['include/apl.h', 'src/apl.c', 'src/apl_core.c', 'src/apl_hmi.c', 'config/apl_core_config.h', 'config/apl_hmi_config.h', 'port/apl_port.c']) if (!this.files.has(required)) throw new Error('模板缺少文件：' + required)
    const hash = createHash('sha256').update(raw).update(adapterBytes)
    for (const [path, data] of [...this.files].sort(([a], [b]) => a < b ? -1 : 1)) hash.update(path).update('\0').update(data).update('\0')
    const { directories, staticFiles, adapter, formatVersion, ...description } = manifest
    this.descriptor = { engine: 'apl-air-care-v1', productDefaults: { code: 'air_care', category: '环境电器' },
      previewFiles: ['include/apl_product.h', 'config/apl_core_config.h', 'config/apl_hmi_config.h', 'README.md'],
      verification: { sourceDirectories: ['src', 'port', 'examples'], includeDirectories: ['include', 'config', 'port'], consumer: 'verification/smoke.c', scope: 'APL 配置、开关机、风速与风干恢复' },
      ...description, fingerprint: hash.digest('hex') }
    const featureKeys = new Set<string>(), parameterKeys = new Set<string>(), bindings = new Set<string>()
    const checkBinding = (binding: ConfigBinding | undefined) => {
      if (!binding) return
      if (!/^config\/[a-zA-Z0-9_.-]+\.h$/.test(binding.path) || !/^[A-Z][A-Z0-9_]+$/.test(binding.macro)) throw new Error('配置绑定无效')
      const key = binding.path + '/' + binding.macro
      if (bindings.has(key)) throw new Error('配置宏被重复绑定：' + key)
      bindings.add(key)
      this.replaceMacro(new Map(this.files), binding, 1)
    }
    for (const feature of this.descriptor.features) {
      if (!identifier(feature.key) || featureKeys.has(feature.key) || feature.definition.key !== feature.key) throw new Error('模板功能标识无效或重复')
      featureKeys.add(feature.key)
      const binding = this.adapters[feature.key]
      if (!binding || (feature.definition.access !== 'write' && typeof binding.get !== 'string') || (feature.definition.access !== 'read' && typeof binding.set !== 'string')) throw new Error('功能缺少 C 适配实现：' + feature.key)
      for (const enabled of feature.enabledBindings ?? []) checkBinding(enabled)
      checkBinding(feature.defaultBinding)
    }
    for (const parameter of this.descriptor.parameters) {
      if (!identifier(parameter.key) || parameterKeys.has(parameter.key)) throw new Error('模板参数标识无效或重复')
      parameterKeys.add(parameter.key); checkBinding(parameter.binding)
    }
    for (const item of [...this.descriptor.features, ...this.descriptor.parameters, ...this.descriptor.ports]) for (const key of item.requires ?? []) if (!featureKeys.has(key)) throw new Error('依赖未知功能：' + key)
    const sourceVersion = text(this.files.get('include/apl.h')!).match(/#define\s+APL_VERSION_STRING\s+"([^"]+)"/)?.[1]
    if (sourceVersion !== this.descriptor.sourceVersion) throw new Error('模板声明的 APL 版本与实际源码不一致')
    const model = applyTemplate({ schemaVersion: 1, name: '模板自检', code: 'template_check', category: '自检', description: '', sdkVersion: '0.1.0', functions: [], parameters: [], template: null }, this.descriptor)
    const issues = this.validate(model)
    if (issues.length) throw new Error('模板默认配置无效：' + issues.join('；'))
  }
  validate(definition: Definition): string[] { return validateTemplateConfiguration(definition, this.descriptor) }
  private replaceMacro(files: Map<string, Uint8Array>, binding: ConfigBinding, value: number | boolean) {
    const original = files.get(binding.path)
    if (!original) throw new Error('配置头缺失：' + binding.path)
    const source = text(original), pattern = new RegExp('^[ \\t]*#\\s*define[ \\t]+' + binding.macro + '[ \\t]+[^\\r\\n]*', 'gm')
    if ([...source.matchAll(pattern)].length !== 1) throw new Error('配置宏缺失或重复：' + binding.macro)
    files.set(binding.path, bytes(source.replace(pattern, `#define ${binding.macro} ${Number(value)}u`)))
  }
  async generate(input: Definition): Promise<Map<string, Uint8Array>> {
    const definition = parseDefinition(input), errors = this.validate(definition)
    if (errors.length) throw new ValidationError(errors.map(message => ({ path: 'template', message })))
    const files = new Map([...this.files].map(([path, value]) => [path, Uint8Array.from(value)]))
    const enabled = new Set(definition.functions.map(f => f.key))
    for (const feature of this.descriptor.features) {
      for (const binding of feature.enabledBindings ?? []) this.replaceMacro(files, binding, enabled.has(feature.key))
      const selected = definition.functions.find(f => f.key === feature.key)
      if (feature.defaultBinding && selected) this.replaceMacro(files, feature.defaultBinding, Number(selected.default))
    }
    for (const parameter of this.descriptor.parameters) if (parameter.binding && activeFor(parameter.requires, enabled)) this.replaceMacro(files, parameter.binding, definition.parameters.find(p => p.key === parameter.key)!.value as number | boolean)
    const prefix = 'apl_' + definition.code, type = prefix + '_t', guard = prefix.toUpperCase() + '_PRODUCT_H'
    const header = [`/* ${comment(definition.name)} · 由 APL Studio 与源码模板生成。 */`, `#ifndef ${guard}`, `#define ${guard}`, '#include "apl.h"', '#include <stdbool.h>', '#include <stddef.h>', '#ifdef __cplusplus', 'extern "C" {', '#endif', `typedef struct apl ${type};`, `void ${prefix}_init(${type} *self, const struct apl_core_ops *core_ops, void *core_ctx, const struct apl_hmi_ops *hmi_ops, void *hmi_ctx);`, `void ${prefix}_tick(${type} *self);`, '']
    const source = ['#include "apl_product.h"', '#include <string.h>', 'static bool send_command(struct apl *self, enum apl_link_command_type type, uint8_t arg) {', '    struct apl_link_command command = { type, arg };', '    return APL_LINK_OK == apl_core_command(&self->core, &command);', '}', '']
    const initialisers: string[] = []
    for (const field of definition.functions) {
      const adapter = this.adapters[field.key], ctype = field.kind === 'bool' ? 'bool' : field.kind === 'enum' ? `enum ${prefix}_${field.key}` : 'int32_t'
      if (field.kind === 'enum') header.push(`enum ${prefix}_${field.key} {`, ...field.options.map(o => `    ${prefix.toUpperCase()}_${field.key.toUpperCase()}_${o.value < 0 ? 'NEG_' + Math.abs(o.value) : o.value} = ${o.value},`), '};')
      const expand = (code: string) => code.replaceAll('{{PREFIX}}', prefix).replaceAll('{{TYPE}}', ctype).replaceAll('{{DEFAULT}}', typeof field.default === 'boolean' ? String(field.default) : String(Number(field.default)))
      header.push(`/* ${comment(field.name)}: ${comment(field.description)} */`)
      if (field.access !== 'write') {
        const signature = field.kind === 'string' ? `size_t ${prefix}_get_${field.key}(const ${type} *self, char *buffer, size_t capacity)` : `${ctype} ${prefix}_get_${field.key}(const ${type} *self)`
        header.push(signature + ';')
        source.push(signature + ' {', '    if (!self) return 0;', '    ' + expand(adapter.get!), '}', '')
      }
      if (field.access !== 'read') {
        const signature = `bool ${prefix}_set_${field.key}(${type} *self, ${field.kind === 'string' ? 'const char *' : ctype + ' '}value)`
        header.push(signature + ';')
        source.push(signature + ' {', '    if (!self) return false;')
        if (field.kind === 'enum') source.push(`    if (${field.options.map(o => `value != ${o.value}`).join(' && ')}) return false;`)
        if (field.kind === 'int') source.push(`    if (value < ${field.min} || value > ${field.max} || ((int64_t)value - (${field.min})) % ${field.step}) return false;`)
        if (field.kind === 'string') source.push(`    if (!value || strlen(value) > ${field.maxLength}) return false;`)
        source.push('    ' + expand(adapter.set!), '}', '')
      }
      if (adapter.initialise) initialisers.push('    ' + expand(adapter.initialise))
    }
    header.push('#ifdef __cplusplus', '}', '#endif', '#endif', '')
    source.push(`void ${prefix}_init(${type} *self, const struct apl_core_ops *core_ops, void *core_ctx, const struct apl_hmi_ops *hmi_ops, void *hmi_ctx) {`, '    if (!self) return;', '    apl_init(self, core_ops, core_ctx, hmi_ops, hmi_ctx);', '    apl_link_bind(self);', ...initialisers, '}', `void ${prefix}_tick(${type} *self) { apl_tick(self); }`, '')
    files.set('include/apl_product.h', bytes(header.join('\n')))
    files.set('src/apl_product.c', bytes(source.join('\n')))
    const port = text(files.get('port/apl_port.c')!).replace('    apl_init(self, &core_ops, 0, &hmi_ops, 0);', `    ${prefix}_init(self, &core_ops, 0, &hmi_ops, 0);`).replace('    apl_link_bind(self);', '')
    files.set('port/apl_port.c', bytes(port))
    files.set('port/apl_port.h', bytes(text(files.get('port/apl_port.h')!).replace('#include "apl.h"', '#include "apl_product.h"')))
    files.set('product.apl.json', bytes(JSON.stringify(definition, null, 2) + '\n'))
    files.set('metadata/template.json', bytes(JSON.stringify(this.descriptor, null, 2) + '\n'))
    files.set('verification/smoke.c', bytes(this.smoke(definition)))
    files.set('README.md', bytes(this.readme(definition)))
    files.set('CMakeLists.txt', bytes(text(files.get('CMakeLists.txt')!) + '\ninclude(CTest)\nif(BUILD_TESTING)\n  add_executable(apl_sdk_smoke verification/smoke.c)\n  target_link_libraries(apl_sdk_smoke PRIVATE apl)\n  add_test(NAME apl_sdk_smoke COMMAND apl_sdk_smoke)\nendif()\n'))
    return files
  }
  private smoke(definition: Definition): string {
    const prefix = 'apl_' + definition.code, enabled = new Set(definition.functions.map(f => f.key))
    const assertions: string[] = []
    for (const feature of this.descriptor.features) {
      for (const binding of feature.enabledBindings ?? []) assertions.push(`_Static_assert(${binding.macro} == ${Number(enabled.has(feature.key))}u, "feature ${feature.key}");`)
      const field = definition.functions.find(f => f.key === feature.key)
      if (feature.defaultBinding && field) assertions.push(`_Static_assert(${feature.defaultBinding.macro} == ${Number(field.default)}u, "default ${feature.key}");`)
    }
    for (const p of this.descriptor.parameters) if (p.binding && activeFor(p.requires, enabled)) assertions.push(`_Static_assert(${p.binding.macro} == ${Number(definition.parameters.find(value => value.key === p.key)!.value)}u, "parameter ${p.key}");`)
    const fan = definition.functions.find(f => f.key === 'fan_speed')!.default
    return `#include "apl_product.h"\n#include <assert.h>\n#include <stdio.h>\n${assertions.join('\n')}\nstatic int fan_on;\nstatic enum apl_water_level water(void *ctx) { (void)ctx; return APL_WATER_LEVEL_3; }\nstatic uint16_t pm(void *ctx) { (void)ctx; return 12u; }\nstatic uint8_t humidity(void *ctx) { (void)ctx; return 45u; }\nstatic uint8_t interlock(void *ctx) { (void)ctx; return 1u; }\nstatic void fan(enum apl_switch on, enum apl_fan_gear gear, void *ctx) { (void)gear; (void)ctx; fan_on = on == APL_SWITCH_ON; }\nint main(void) {\n  ${prefix}_t self;\n  static struct apl_core_ops ops;\n  struct apl_link_snapshot s;\n  ops.get_water_level=water; ops.get_pm25=pm; ops.get_humidity=humidity; ops.get_uv_interlock=interlock; ops.set_fan=fan;\n  ${prefix}_init(&self,&ops,0,0,0);\n  assert(${prefix}_set_power(&self,true));\n  for (unsigned i=0;i<201u;i++) ${prefix}_tick(&self);\n  apl_core_get_snapshot(&self.core,&s);\n  assert(s.run_state == APL_RUN_ON && fan_on);\n  assert(s.purify_enabled == ${Number(enabled.has('purify'))} && s.humidify_enabled == ${Number(enabled.has('humidify'))});\n  assert((int)${prefix}_get_fan_speed(&self) == ${Number(fan)});\n  ${enabled.has('pm25') ? `assert(${prefix}_get_pm25(&self) == 12);` : ''}\n  ${enabled.has('humidity') ? `assert(${prefix}_get_humidity(&self) == 45);` : ''}\n  assert(${prefix}_set_power(&self,false));\n  ${prefix}_tick(&self);\n  apl_core_get_snapshot(&self.core,&s);\n  ${enabled.has('humidify') ? `assert(s.run_state == APL_RUN_DRY && s.dry_left_s > 0u);\n  assert(${prefix}_set_power(&self,true));\n  ${prefix}_tick(&self);\n  apl_core_get_snapshot(&self.core,&s);\n  assert(s.run_state == APL_RUN_ON && s.dry_left_s == 0u);` : 'assert(s.run_state == APL_RUN_STANDBY);'}\n  puts("APL SDK configuration, power, fan and dry recovery verified");\n  return 0;\n}\n`
  }
  private readme(definition: Definition): string {
    const prefix = 'apl_' + definition.code, enabled = new Set(definition.functions.map(f => f.key))
    const ports = this.descriptor.ports.filter(p => activeFor(p.requires, enabled))
    return `# ${comment(definition.name)} · APL C11 SDK\n\n产品目标版本 ${definition.sdkVersion}；模板 ${this.descriptor.id}@${this.descriptor.version}；APL ${this.descriptor.sourceVersion}。\n\n## 接入\n\n1. 将 src 下全部 C 文件加入工程，包含目录加入 include、config。\n2. 复制 port 到自己的应用目录，完成 TODO 硬件实现；升级时保留自己的移植代码。占位回调不是真实驱动。\n3. 使用方持有 ${prefix}_t 对象，调用 apl_port_init 或 ${prefix}_init；ops 和 ctx 必须在实例存活期间有效。\n4. 每 10 ms 串行调用 ${prefix}_tick；读写接口见 include/apl_product.h。所有操作不能重入。\n5. 读取 HMI getter 刷新实际面板。power getter 表示正常运行；风干时返回 false，实际运行态从 apl_core_get_snapshot 读取。\n6. 需要记忆时实现 memory_save，并在初始化后、第一拍前显式恢复带版本与校验的记录。\n\n## 本产品移植接口\n\n${ports.map(p => `- ${p.key}（${p.required ? '必需' : '可选'}）：${p.description}`).join('\n')}\n\n## 配置\n\n${definition.parameters.map(p => `- ${p.key} = ${String(p.value)}`).join('\n')}\n\n只读传感器功能的 default 仅用于 Studio 数据模型预览，实际值始终来自硬件回调。UI 的通用接口契约预览不是本 SDK 的 ABI；本包 include/apl_product.h 是带显式实例参数、已编译验证的实际接口。\n\n## 验证与升级\n\nmanifest.json 记录产品修订、模板和源码指纹、逐文件 SHA-256 及实际编译/消费验证结果。\n通用构建：cmake -S . -B build，然后 cmake --build build，再 ctest --test-dir build。交叉编译可关闭 BUILD_TESTING。\n验证仅覆盖本机 C11 配置与基础业务消费，不等于已完成目标 MCU、外设电气或完整产品验收。\n重新生成只产生新归档，不修改用户嵌入式工程。\n`
  }
}
export class TemplateRegistry {
  private providers = new Map<string, TemplateProvider>()
  register(provider: TemplateProvider): () => void {
    const key = `${provider.descriptor.id}@${provider.descriptor.version}`
    if (this.providers.has(key)) throw new Error('源码模板版本已注册：' + key)
    this.providers.set(key, provider)
    return () => { this.providers.delete(key) }
  }
  list(): TemplateDescriptor[] { return [...this.providers.values()].map(p => structuredClone(p.descriptor)) }
  get(id: string, version: string): TemplateProvider | undefined { return this.providers.get(`${id}@${version}`) }
  loadDirectory(root: string): void {
    for (const product of readdirSync(root, { withFileTypes: true })) {
      if (!product.isDirectory() || product.isSymbolicLink()) continue
      for (const version of readdirSync(resolve(root, product.name), { withFileTypes: true })) {
        if (!version.isDirectory() || version.isSymbolicLink() || !/^\d+\.\d+\.\d+$/.test(version.name)) continue
        const directory = resolve(root, product.name, version.name)
        const manifest = JSON.parse(readFileSync(resolve(directory, 'template.json'), 'utf8'))
        if (manifest.version !== version.name) throw new Error('模板目录与版本不一致：' + directory)
        if (manifest.engine === 'native-c-v1') this.register(new NativeCTemplate(directory))
        else if (!manifest.engine || manifest.engine === 'apl-air-care-v1') this.register(new CSourceTemplate(directory))
        else throw new Error('未知模板引擎：' + manifest.engine)
      }
    }
  }
}
export const name = 'templates'
declare module 'cordis' { interface Context { templates: TemplateRegistry } }
export function apply(ctx: Context) { ctx.provide('templates', new TemplateRegistry()) }
