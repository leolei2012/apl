import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve, sep, delimiter } from 'node:path'
import { spawnSync } from 'node:child_process'
import { RuntimeProfileProvider, TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { newProfile, setProfile, validateProfile } from '@apl-studio/model/profile'
import { defaultFirmwareBuild, firmwareBuild, setFirmwareBuild, type FirmwareBuild } from '@apl-studio/model/firmware'

const repository = resolve(import.meta.dirname, '../..')
const compiler = process.env.CC || 'gcc'
const optional = ['APL_COMPILE_TIMER', 'APL_COMPILE_FILTER', 'APL_COMPILE_FACTORY', 'APL_COMPILE_MEMORY', 'APL_COMPILE_SLEEP', 'APL_COMPILE_DRY', 'APL_HMI_COMPILE_AMBIENT']
const disabledApis: Record<string, string[]> = {
  APL_COMPILE_TIMER: ['apl_core_set_timer', 'apl_core_get_timer'],
  APL_COMPILE_FILTER: ['apl_core_filter_purify_reset', 'apl_core_filter_humidify_reset', 'apl_core_get_filter_purify', 'apl_core_get_filter_humidify', 'apl_hmi_filter_query'],
  APL_COMPILE_MEMORY: ['apl_core_get_memory', 'apl_core_memory_restore'],
  APL_HMI_COMPILE_AMBIENT: ['apl_hmi_set_ambient_brightness', 'apl_hmi_get_ambient_brightness', 'apl_hmi_set_ambient_color', 'apl_hmi_get_ambient_color'],
  APL_HMI_COMPILE_BUZZER: ['apl_hmi_buzzer_play', 'apl_hmi_get_buzzer_busy'],
  APL_HMI_COMPILE_KEY_CALLBACKS: ['apl_hmi_set_key_callback'],
  APL_HMI_COMBO_MAX: ['apl_hmi_set_combo'],
}
const disabledHelpers: Record<string, string[]> = {
  APL_COMPILE_TIMER: ['apl_core_timer_tick', 'apl_core_panel_timer_expire', 'apl_hmi_panel_timer_key', 'apl_hmi_timer_repeat'],
  APL_COMPILE_FILTER: ['apl_core_filter_left', 'apl_core_filter_cache_update', 'apl_core_cmd_filter_reset', 'apl_hmi_panel_draw_filter'],
  APL_COMPILE_FACTORY: ['apl_core_factory_restore', 'apl_core_cmd_factory_enter'],
  APL_COMPILE_MEMORY: ['apl_core_memory_fill', 'apl_core_memory_notify'],
  APL_COMPILE_DRY: ['apl_core_dry_start'],
  APL_HMI_COMPILE_AMBIENT: ['apl_hmi_apply_ambient', 'apl_hmi_panel_draw_ambient'],
  APL_HMI_COMPILE_BUZZER: ['apl_hmi_buzzer_tick', 'apl_hmi_buzzer_write'],
}
function command(program: string, args: string[]) {
  const result = spawnSync(program, args, { encoding: 'utf8', timeout: 60000,
    env: { ...process.env, PATH: dirname(compiler) + delimiter + (process.env.PATH ?? '') } })
  assert.equal(result.status, 0, `${program}: ${result.error ?? ''}\n${result.stdout}\n${result.stderr}`)
  return result
}

test('旧裁剪记录仅补新增七项全功能默认，不接受缺失旧字段或未知开关', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-trim-migration-')), provider = new RuntimeProfileProvider(repository)
  const registry = new TemplateRegistry(); registry.register(provider)
  const products = new ProductStore(join(directory, 'studio.db')), app = createApi(products, registry)
  try {
    const current = defaultFirmwareBuild(); current.defines.APL_HMI_COMPILE_KEY_CALLBACKS = 0; current.defines.APL_HMI_COMBO_MAX = 2
    const old = { ...current, defines: Object.fromEntries(Object.entries(current.defines).filter(([key]) => !optional.includes(key))) } as FirmwareBuild
    const d = setFirmwareBuild(newProfile(provider.catalog, '旧配置升级'), old)
    assert.deepEqual(validateProfile(d, provider.catalog), [])
    const normalized = firmwareBuild(d)
    for (const key of optional) assert.equal(normalized.defines[key as keyof FirmwareBuild['defines']], 1, key)
    assert.equal(normalized.defines.APL_HMI_COMPILE_KEY_CALLBACKS, 0)
    assert.equal(normalized.defines.APL_HMI_COMBO_MAX, 2)
    const created = await app.inject({ method: 'POST', url: '/api/products', payload: d })
    assert.equal(created.statusCode, 201, created.body)
    const p = created.json()
    assert.deepEqual(firmwareBuild(p.definition), normalized)
    for (const invalid of [
      { ...old, defines: Object.fromEntries(Object.entries(old.defines).filter(([key]) => key !== 'APL_COMPILE_WIFI')) },
      { ...old, defines: { ...old.defines, APL_COMPILE_TYPO: 1 } },
      { ...old, defines: { ...old.defines, APL_COMPILE_TIMER: 2 } },
    ]) {
      const bad = setFirmwareBuild(d, invalid as FirmwareBuild)
      assert.ok(validateProfile(bad, provider.catalog).some(s => s.includes('裁剪配置无效')))
      const rejected = await app.inject({ method: 'PUT', url: '/api/products/' + p.id, payload: { revision: 1, definition: bad } })
      assert.equal(rejected.statusCode, 400, rejected.body)
    }
  } finally {
    await app.close(); products.close()
    assert.ok(resolve(directory).startsWith(resolve(tmpdir()) + sep)); rmSync(directory, { recursive: true, force: true })
  }
})

test('所有可选功能独立及组合裁剪：真实C行为、专用API/实现消失与生成包冷启动', { timeout: 180000 }, t => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-feature-trim-')), provider = new RuntimeProfileProvider(repository)
  const allOff = Object.fromEntries([...optional, 'APL_HMI_COMPILE_BUZZER', 'APL_HMI_COMPILE_KEY_CALLBACKS', 'APL_HMI_COMBO_MAX',
    'APL_COMPILE_WIFI', 'APL_COMPILE_UV', 'APL_COMPILE_ANION', 'APL_CORE_HAS_TURBO',
    'APL_COMPILE_SENSOR_PM25', 'APL_COMPILE_SENSOR_PM10', 'APL_COMPILE_SENSOR_HCHO', 'APL_COMPILE_SENSOR_TVOC', 'APL_COMPILE_SENSOR_HUMIDITY'].map(key => [key, 0]))
  const cases: { name: string; flags: Record<string, number>; purify?: boolean; humidify?: boolean }[] = [
    ...[...optional, 'APL_HMI_COMPILE_BUZZER', 'APL_HMI_COMPILE_KEY_CALLBACKS', 'APL_HMI_COMBO_MAX',
      'APL_COMPILE_UV', 'APL_COMPILE_ANION', 'APL_COMPILE_SENSOR_PM25', 'APL_COMPILE_SENSOR_HUMIDITY', 'APL_CORE_HAS_TURBO'].map(key => ({ name: key, flags: { [key]: 0 } })),
    { name: 'all-optional-off', flags: allOff },
    { name: 'purify-minimum', flags: { ...allOff, APL_COMPILE_HUMIDIFY: 0 }, humidify: false },
    { name: 'humidify-minimum', flags: { ...allOff, APL_COMPILE_PURIFY: 0 }, purify: false },
    { name: 'core-minimum', flags: { ...allOff, APL_COMPILE_HUMIDIFY: 0, APL_COMPILE_HMI: 0 }, humidify: false },
  ]
  try {
    for (const item of cases) {
      const build: FirmwareBuild = defaultFirmwareBuild(); Object.assign(build.defines, item.flags)
      let d = newProfile(provider.catalog, item.name, { purify: item.purify ?? true, humidify: item.humidify ?? true })
      d = setProfile(setProfile(d, 'modules', 0), 'sensors', 0)
      if (!build.defines.APL_CORE_HAS_TURBO) d = setProfile(d, 'turbo_enabled', 0)
      d = setFirmwareBuild(d, build)
      assert.deepEqual(validateProfile(d, provider.catalog), [], item.name)
      const root = join(directory, item.name), files = provider.firmwareFiles(d)
      for (const [name, bytes] of files) {
        const target = resolve(root, name); assert.ok(target.startsWith(root + sep))
        mkdirSync(dirname(target), { recursive: true }); writeFileSync(target, bytes)
      }
      const includes = ['-I' + join(root, 'include'), '-I' + join(root, 'config')]
      const sources = [...files.keys()].filter(name => name.startsWith('src/') && name.endsWith('.c')).map(name => join(root, name))
      const behavior = join(root, process.platform === 'win32' ? 'behavior.exe' : 'behavior')
      command(compiler, ['-std=c11', '-Wall', '-Wextra', '-Werror', ...includes, join(repository, 'tests/test_feature_trimming.c'), ...sources, '-o', behavior])
      command(behavior, [])
      const consumer = join(root, 'consumer.c'), exe = join(root, process.platform === 'win32' ? 'consumer.exe' : 'consumer')
      writeFileSync(consumer, '#include "apl_product_profile.h"\nint main(void) { struct apl d; struct apl_profile p; unsigned i; apl_product_profile_defaults(&p); if(APL_PROFILE_OK != apl_init_profile(&d,0,0,0,0,&p)) return 1; apl_core_power_on(&d.core); for(i=0;i<10;i++) apl_tick_1ms(&d); return apl_core_get_run_state(&d.core) != APL_RUN_ON; }\n')
      command(compiler, ['-std=c11', '-Wall', '-Wextra', '-Werror', ...includes, consumer, ...sources, '-o', exe]); command(exe, [])
      const apiNames = Object.entries(disabledApis).filter(([flag]) => item.flags[flag] === 0).flatMap(([, names]) => names)
      const implementationNames = [...apiNames, ...Object.entries(disabledHelpers).filter(([flag]) => item.flags[flag] === 0).flatMap(([, names]) => names)]
      for (const unit of ['apl_core', 'apl_hmi']) {
        const assembly = join(root, unit + '.s')
        command(compiler, ['-std=c11', ...includes, '-S', join(root, 'src', unit + '.c'), '-o', assembly])
        for (const symbol of implementationNames) assert.ok(!readFileSync(assembly, 'utf8').includes(symbol), `${item.name}: ${symbol} implementation is still emitted`)
      }
      const api = join(root, 'disabled-api.c')
      for (const name of apiNames) {
        writeFileSync(api, `#include "apl.h"\nint main(void) { (void)&${name}; return 0; }\n`)
        const rejected = spawnSync(compiler, ['-std=c11', ...includes, '-c', api, '-o', join(root, 'disabled-api.o')], { encoding: 'utf8' })
        assert.notEqual(rejected.status, 0, `${item.name}: disabled API still declared: ${name}`)
        assert.ok(rejected.stderr.includes(name), rejected.stderr)
      }
      if (process.env.APL_STUDIO_C51) for (const [i, source] of [...sources, consumer].entries()) {
        const assembly = join(root, 'target' + i + '.a51')
        const result = command(process.env.APL_STUDIO_C51, [source, 'LARGE', 'OBJECTEXTEND', 'OPTIMIZE(8,SIZE)',
          `INCDIR(${join(root, 'include')},${join(root, 'config')})`, `SRC(${assembly})`, `PRINT(${join(root, 'target' + i + '.lst')})`])
        assert.match(result.stdout, /0 WARNING\(S\),\s+0 ERROR\(S\)/)
        const asm = readFileSync(assembly, 'utf8')
        for (const symbol of implementationNames) assert.doesNotMatch(asm, new RegExp('(?:PUBLIC|EXTRN|LCALL)[^\\r\\n]*' + symbol, 'i'), `${item.name}: ${symbol} target symbol remains`)
      }
      t.diagnostic(item.name + ': behavior and source consumer passed' + (process.env.APL_STUDIO_C51 ? ', Keil compiled' : ''))
    }
  } finally {
    assert.ok(resolve(directory).startsWith(resolve(tmpdir()) + sep))
    rmSync(directory, { recursive: true, force: true })
  }
})
