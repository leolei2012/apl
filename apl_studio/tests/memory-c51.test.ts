import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, sep } from 'node:path'
import { spawnSync } from 'node:child_process'
import { RuntimeProfileProvider } from '@apl-studio/templates'
import { defaultFirmwareBuild, setFirmwareBuild, type FirmwareBuild } from '@apl-studio/model/firmware'
import { newProfile, profileValues, setProfile } from '@apl-studio/model/profile'
import { estimateMemory } from '../packages/model/src/memory'

const compiler = process.env.APL_STUDIO_C51
test('内存参考与新编译Keil C51 LARGE sizeof一致：多种功能裁剪组合', {
  skip: !compiler ? 'Set APL_STUDIO_C51 to an installed Keil C51 compiler to verify fresh target sizeof constants.' : false,
}, t => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-memory-c51-'))
  const provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  const compact = { APL_HMI_COMPILE_KEY_CALLBACKS: 0, APL_HMI_COMBO_MAX: 2, APL_CORE_COMPILE_FILTER_CACHE: 0 }
  const cases: { name: string; flags: Partial<FirmwareBuild['defines']> }[] = [
    { name: 'full', flags: {} },
    { name: 'no-wifi', flags: { APL_COMPILE_WIFI: 0 } },
    { name: 'compact', flags: compact },
    { name: 'compact-no-wifi', flags: { ...compact, APL_COMPILE_WIFI: 0 } },
    { name: 'minimal', flags: { ...compact, APL_HMI_COMBO_MAX: 0, APL_HMI_COMPILE_BUZZER: 0 } },
    { name: 'core', flags: { APL_COMPILE_HMI: 0, APL_CORE_COMPILE_FILTER_CACHE: 0 } },
    { name: 'core-no-wifi', flags: { APL_COMPILE_HMI: 0, APL_CORE_COMPILE_FILTER_CACHE: 0, APL_COMPILE_WIFI: 0 } },
    { name: 'custom-combo3', flags: { APL_HMI_COMBO_MAX: 3 } },
    ...['APL_COMPILE_TIMER','APL_COMPILE_FILTER','APL_COMPILE_FACTORY','APL_COMPILE_MEMORY','APL_COMPILE_SLEEP','APL_COMPILE_DRY','APL_HMI_COMPILE_AMBIENT','APL_HMI_COMPILE_KEYMAP','APL_COMPILE_UV','APL_COMPILE_ANION','APL_CORE_HAS_TURBO','APL_COMPILE_SENSOR_PM25','APL_COMPILE_SENSOR_HUMIDITY'].map(key => ({ name: 'without-' + key, flags: { [key]: 0 } })),
    { name: 'purify-family', flags: { APL_COMPILE_HUMIDIFY: 0, APL_COMPILE_SENSOR_HUMIDITY: 0 } },
    { name: 'humidify-family', flags: { APL_COMPILE_PURIFY: 0, APL_COMPILE_SENSOR_PM25: 0, APL_COMPILE_SENSOR_PM10: 0, APL_COMPILE_SENSOR_HCHO: 0, APL_COMPILE_SENSOR_TVOC: 0 } },

  ]
  try {
    for (const item of cases) {
      const build = { ...defaultFirmwareBuild(), target: 'c51' as const }
      Object.assign(build.defines, item.flags)
      let definition = newProfile(provider.catalog, '目标尺寸核对', { purify: true, humidify: true })
      const values = profileValues(definition)
      definition = setProfile(setProfile(definition, 'purify_enabled', build.defines.APL_COMPILE_PURIFY), 'humidify_enabled', build.defines.APL_COMPILE_HUMIDIFY)
      const moduleMask = build.defines.APL_COMPILE_UV + 2 * build.defines.APL_COMPILE_ANION + 4 * build.defines.APL_COMPILE_WIFI
      const sensorMask = build.defines.APL_COMPILE_SENSOR_PM25 + 2 * build.defines.APL_COMPILE_SENSOR_PM10 + 4 * build.defines.APL_COMPILE_SENSOR_HCHO + 8 * build.defines.APL_COMPILE_SENSOR_TVOC + 16 * build.defines.APL_COMPILE_SENSOR_HUMIDITY
      definition = setProfile(setProfile(definition, 'modules', values.modules & moduleMask), 'sensors', values.sensors & sensorMask)
      if (!build.defines.APL_CORE_HAS_TURBO) definition = setProfile(definition, 'turbo_enabled', 0)
      definition = setFirmwareBuild(definition, build)
      const root = join(directory, item.name)
      for (const [name, bytes] of provider.firmwareFiles(definition)) {
        const target = resolve(root, name)
        assert.ok(target.startsWith(root + sep))
        mkdirSync(dirname(target), { recursive: true })
        writeFileSync(target, bytes)
      }
      const source = join(root, 'metrics.c'), assembly = join(root, 'metrics.a51')
      writeFileSync(source, '#include "apl.h"\nunsigned int code memory_estimate_metrics[] = { sizeof(struct apl_core), APL_COMPILE_HMI ? sizeof(struct apl_hmi) : 0u, sizeof(struct apl) };\n')
      const compiled = spawnSync(compiler!, [source, 'LARGE', 'OBJECTEXTEND', 'OPTIMIZE(8,SIZE)',
        `INCDIR(${join(root, 'include')},${join(root, 'config')})`, `SRC(${assembly})`, `PRINT(${join(root, 'metrics.lst')})`],
      { encoding: 'utf8', timeout: 60000 })
      assert.equal(compiled.status, 0, `${item.name}: ${compiled.error ?? ''}\n${compiled.stdout}\n${compiled.stderr}`)
      assert.match(compiled.stdout, /0 WARNING\(S\),\s+0 ERROR\(S\)/)
      const asm = readFileSync(assembly, 'utf8'), label = asm.indexOf('memory_estimate_metrics:')
      assert.ok(label >= 0, 'target compiler did not emit the metrics label')
      const metrics = [...asm.slice(label).matchAll(/^\s*DW\s+([0-9A-F]+)H\s*$/gmi)].slice(0, 3).map(match => parseInt(match[1], 16))
      assert.equal(metrics.length, 3)
      const estimate = estimateMemory(build)
      assert.deepEqual(metrics, [estimate.coreBytes, estimate.hmiBytes, estimate.perInstanceBytes], item.name)
      t.diagnostic(`${item.name}: core=${metrics[0]} B, hmi=${metrics[1]} B, instance=${metrics[2]} B; compiler sizeof only, no whole-firmware link or stack claim`)
    }
  } finally {
    assert.ok(resolve(directory).startsWith(resolve(tmpdir()) + sep))
    rmSync(directory, { recursive: true, force: true })
  }
})
