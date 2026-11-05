import { readFileSync, readdirSync } from 'node:fs'
import { resolve, relative } from 'node:path'
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import type { Definition } from '@apl-studio/model'
import { PROFILE_ID, profilePid, profileValues, validateProfile, type ProfileCatalog, type ProfileSchema } from '@apl-studio/model/profile'
import type { TemplateProvider } from './index'
import { defaultFirmwareBuild, firmwareBuild, profileCompileGuard, targetConfigHeader } from '@apl-studio/model/firmware'
import { keyMappingC } from '@apl-studio/model/key-mapping'

/** Captures one immutable in-memory development snapshot for this server lifetime. */
export class RuntimeProfileProvider implements TemplateProvider {
  readonly catalog: ProfileCatalog
  readonly descriptor
  private readonly files = new Map<string, Uint8Array>()
  readonly workbook: Uint8Array
  constructor(repository: string) {
    const library = resolve(repository, 'apl_lib')
    const raw = readFileSync(resolve(library, 'parameters/profile.schema.json'))
    const schema = JSON.parse(raw.toString('utf8')) as ProfileSchema
    for (const folder of ['src', 'include', 'config']) for (const name of readdirSync(resolve(library, folder)).sort()) {
      if (/\.(c|h)$/.test(name)) this.files.set(`${folder}/${name}`, readFileSync(resolve(library, folder, name)))
    }
    const sourceVersion = Buffer.from(this.files.get('include/apl.h')!).toString().match(/APL_VERSION_STRING\s+"([^"]+)"/)?.[1]
    if (schema.schemaId !== PROFILE_ID || sourceVersion !== schema.aplVersion) throw new Error('APL 参数定义与源码版本不一致')
    this.workbook = readFileSync(resolve(library, 'parameters/APL参数表.xlsx'))
    const hash = createHash('sha256').update(raw).update(this.workbook)
    for (const [name, data] of this.files) hash.update(name).update(data)
    const fingerprint = hash.digest('hex')
    let baseCommit = '', dirty = true
    try {
      baseCommit = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: repository, encoding: 'utf8', timeout: 5000, windowsHide: true }).trim()
      dirty = !!execFileSync('git', ['status', '--porcelain', '--', relative(repository, library)], { cwd: repository, encoding: 'utf8', timeout: 5000, windowsHide: true }).trim()
    } catch { /* Export still identifies the captured source by its fingerprint. */ }
    this.descriptor = { id: PROFILE_ID, version: schema.aplVersion, name: 'APL 运行时参数', description: '固定全功能 C 库，加载产品参数', definitionSchemaVersion: 1 as const,
      sourceVersion: schema.aplVersion, fingerprint, engine: 'apl-air-care-v1' as const, features: [], ports: [], constraints: [],
      parameters: schema.parameters.map(p => ({ key: p.key, name: p.name, description: p.description, kind: 'int' as const, default: p.default, min: p.min, max: p.max, unit: p.unit })) }
    this.catalog = { schema, template: this.descriptor, source: { repository: schema.source.repository, baseCommit, dirty, fingerprint } }
  }
  validate(d: Definition) { return validateProfile(d, this.catalog) }
  private configuredFiles(d: Definition, force = false) {
    const files = new Map(this.files), build = firmwareBuild(d)
    if (force || JSON.stringify(build.defines) !== JSON.stringify(defaultFirmwareBuild().defines)) {
      files.set('config/apl_target_config.h', Buffer.from(targetConfigHeader(build)))
      files.set('config/apl_build_config.h', Buffer.concat([Buffer.from('#include "apl_target_config.h"\n'), Buffer.from(this.files.get('config/apl_build_config.h')!)]))
    }
    if (!build.defines.APL_COMPILE_WIFI) files.delete('src/apl_wifi.c')
    return files
  }
  /** Portable source delivery; target compiler/linker and host drivers remain external. */
  firmwareFiles(d: Definition) {
    const errors = this.validate(d)
    if (errors.length) throw new Error(errors.join('；'))
    const files = this.configuredFiles(d, true), p = profileValues(d)
    files.set('include/apl_product_profile.h', Buffer.from('#ifndef APL_PRODUCT_PROFILE_H\n#define APL_PRODUCT_PROFILE_H\n#include "apl.h"\n#include <string.h>\n'
      + 'static void apl_product_profile_defaults(struct apl_profile *p) {\nif (p == NULL) return;\napl_profile_defaults(p);\n'
      + this.catalog.schema.parameters.map(f => profileCompileGuard(f.key) ? `#if ${profileCompileGuard(f.key)}\np->${f.key} = ${p[f.key]}UL;\n#endif` : `p->${f.key} = ${p[f.key]}UL;`).join('\n')
      + '\n' + keyMappingC(d, 'p->keymap')
      + `\n#if APL_COMPILE_WIFI\nmemcpy(p->pid, ${JSON.stringify(profilePid(d))}, ${profilePid(d).length + 1}u);\n#endif\n}\n#endif\n`))
    return files
  }
  async generate(d: Definition) {
    const errors = this.validate(d)
    if (errors.length) throw new Error(errors.join('；'))
    if (!firmwareBuild(d).defines.APL_COMPILE_HMI) throw new Error('纯主控裁剪不支持当前面板仿真，请导出源码包并由宿主接入按键和显示')
    const prefix = 'apl_' + d.code, p = profileValues(d), files = this.configuredFiles(d)
    // The simulation and exported target use the same compile switches and profile.
    const header = `#include "apl.h"\n#include <stdlib.h>\n#include <string.h>\ntypedef struct apl ${prefix}_t;\nstatic void ${prefix}_init(${prefix}_t *self, const struct apl_core_ops *c, void *cx, const struct apl_hmi_ops *h, void *hx) {\nstruct apl_profile p; apl_profile_defaults(&p);\n${this.catalog.schema.parameters.map(f => profileCompileGuard(f.key) ? `#if ${profileCompileGuard(f.key)}\np.${f.key} = ${p[f.key]}UL;\n#endif` : `p.${f.key} = ${p[f.key]}UL;`).join('\n')}\n${keyMappingC(d, 'p.keymap')}#if APL_COMPILE_WIFI\nmemcpy(p.pid, ${JSON.stringify(profilePid(d))}, ${profilePid(d).length + 1}u);\n#endif\nif (APL_PROFILE_OK != apl_init_profile(self,c,cx,h,hx,&p)) exit(2);\napl_link_bind(self);\n}\nstatic void ${prefix}_tick(${prefix}_t *self) { apl_tick_1ms(self); }\n#if APL_COMPILE_UV\nstatic int ${prefix}_set_uv(${prefix}_t *self, int value) { struct apl_link_command c = { APL_LINK_CMD_UV_SET, (uint8_t)value }; return APL_LINK_OK == apl_core_command(&self->core,&c); }\n#endif\n#if APL_COMPILE_ANION\nstatic int ${prefix}_set_anion(${prefix}_t *self, int value) { struct apl_link_command c = { APL_LINK_CMD_ANION_SET, (uint8_t)value }; return APL_LINK_OK == apl_core_command(&self->core,&c); }\n#endif\n`
    files.set('include/apl_product.h', Buffer.from(header))
    return files
  }
}
