import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, rmSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, sep, delimiter } from 'node:path'
import { spawnSync } from 'node:child_process'
import { unzipSync, strFromU8 } from 'fflate'
import { RuntimeProfileProvider, TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { SimulationService } from '@apl-studio/build'
import { newProfile, profilePid, setProfile, validateProfile } from '@apl-studio/model/profile'
import { BUILD_PARAMETER, buildPreset, c51InstanceBytes, defaultFirmwareBuild, firmwareBuild, firmwareIssues, setFirmwareBuild } from '@apl-studio/model/firmware'

const repository = resolve(import.meta.dirname, '../..')
const compiler = process.env.CC || 'gcc'
function command(program: string, args: string[]) {
  const r = spawnSync(program, args, { encoding: 'utf8', timeout: 60000,
    env: { ...process.env, PATH: dirname(compiler) + delimiter + (process.env.PATH ?? '') } })
  assert.equal(r.status, 0, `${r.error ?? ''}\n${r.stdout}\n${r.stderr}`)
  return r
}
function clean(directory: string) {
  assert.ok(resolve(directory).startsWith(resolve(tmpdir()) + sep))
  rmSync(directory, { recursive: true, force: true })
}
test('裁剪预设、资源参考与能力依赖校验，旧产品默认保持全功能', () => {
  const provider = new RuntimeProfileProvider(repository)
  const d = setProfile(newProfile(provider.catalog, '芯片预算', { purify: true, humidify: false }), 'pid', '000Ab09')
  assert.deepEqual(firmwareBuild(d), defaultFirmwareBuild())
  for (const [preset, size] of [['full', 516], ['product', 461], ['compact', 371], ['minimal', 336], ['core-only', 133]] as const) {
    const b = buildPreset(d, preset), next = setFirmwareBuild(d, b)
    assert.deepEqual(validateProfile(next, provider.catalog), [])
    assert.equal(c51InstanceBytes(b), size)
    assert.equal(profilePid(next), '000Ab09')
  }
  const bad = buildPreset(d, 'compact'); bad.defines.APL_COMPILE_PURIFY = 0
  assert.ok(firmwareIssues(setFirmwareBuild(d, bad)).some(s => s.includes('裁掉')))
  const malformed = setFirmwareBuild(d, defaultFirmwareBuild())
  malformed.parameters.find(p => p.key === BUILD_PARAMETER)!.value = '{"defines":{"INJECT":"bad"}}'
  assert.ok(validateProfile(malformed, provider.catalog).some(s => s.includes('裁剪配置无效')))
  for (const combo of [1, 5]) { const b = defaultFirmwareBuild(); b.defines.APL_HMI_COMBO_MAX = combo; assert.ok(firmwareIssues(setFirmwareBuild(d, b)).length) }
})

test('源码ZIP精确修订、来源与哈希；三机型裁剪包可由宿主消费并通过可选Keil编译', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-firmware-'))
  const provider = new RuntimeProfileProvider(repository), registry = new TemplateRegistry(); registry.register(provider)
  const products = new ProductStore(join(directory, 'studio.db')), app = createApi(products, registry)
  const sourceBefore = readFileSync(join(repository, 'apl_lib/config/apl_build_config.h'))
  try {
    for (const features of [{ purify: true, humidify: false }, { purify: false, humidify: true }, { purify: true, humidify: true }]) {
      for (const preset of ['compact', 'minimal', 'core-only'] as const) {
        let d = setProfile(newProfile(provider.catalog, '固件裁剪', features), 'pid', 'aaerrxvja3m7lvhz')
        const b = buildPreset(d, preset); b.target = 'c51'; b.chip = 'test-target'
        d = setFirmwareBuild(d, b)
        const create = await app.inject({ method: 'POST', url: '/api/products', payload: d })
        assert.equal(create.statusCode, 201, create.body)
        const p = create.json()
        assert.deepEqual(firmwareBuild(p.definition), b)
        const url = `/api/products/${p.id}/firmware.zip?revision=1`
        const exported = await app.inject({ method: 'GET', url }); assert.equal(exported.statusCode, 200, exported.body)
        assert.deepEqual((await app.inject({ method: 'GET', url })).rawPayload, exported.rawPayload, 'archive must be reproducible')
        const zip = unzipSync(exported.rawPayload), manifest = JSON.parse(strFromU8(zip['apl-firmware/manifest.json']))
        assert.equal(manifest.revision, 1); assert.equal(manifest.sourceFingerprint, provider.catalog.source.fingerprint)
        assert.equal(manifest.targetCapacityVerified, false); assert.equal(manifest.compiled, false)
        const { createHash } = await import('node:crypto')
        for (const [name, hash] of Object.entries(manifest.files)) assert.equal(createHash('sha256').update(zip['apl-firmware/' + name]).digest('hex'), hash)
        const root = join(directory, p.id); mkdirSync(root)
        for (const [name, bytes] of Object.entries(zip)) {
          const target = resolve(root, name.replace('apl-firmware/', '')); assert.ok(target.startsWith(root + sep))
          mkdirSync(dirname(target), { recursive: true }); writeFileSync(target, bytes)
        }
        const probe = join(root, 'consumer.c')
        writeFileSync(probe, '#include "apl_product_profile.h"\n#define CHECK(x) do { if (!(x)) return 1; } while (0)\nint main(void) { struct apl d; struct apl_profile p; char pid[17]; unsigned i;\napl_product_profile_defaults(&p); CHECK(APL_PROFILE_OK == apl_profile_validate(&p));\nCHECK(APL_PROFILE_OK == apl_init_profile(&d,0,0,0,0,&p)); apl_core_get_pid(&d.core,pid); CHECK(strcmp(pid,"aaerrxvja3m7lvhz")==0); for(i=0;i<10;i++) apl_tick_1ms(&d); return 0; }\n')
        const include = ['-I' + join(root, 'include'), '-I' + join(root, 'config')]
        const sources = ['apl.c', 'apl_core.c', 'apl_hmi.c', 'apl_wifi.c'].map(n => join(root, 'src', n))
        const exe = join(root, process.platform === 'win32' ? 'consumer.exe' : 'consumer')
        command(compiler, ['-std=c11', '-Wall', '-Wextra', '-Werror', ...include, probe, ...sources, '-o', exe]); command(exe, [])
        if (process.env.APL_STUDIO_C51) for (const [i, source] of [...sources, probe].entries())
          command(process.env.APL_STUDIO_C51, [source, 'LARGE', 'OBJECTEXTEND', 'OPTIMIZE(8,SIZE)', `INCDIR(${join(root, 'include')},${join(root, 'config')})`, `OBJECT(${join(root, 'probe' + i + '.obj')})`, `PRINT(${join(root, 'probe' + i + '.lst')})`])
        const mismatch = spawnSync(compiler, ['-std=c11', ...include, '-DAPL_COMPILE_HMI=' + (1 - b.defines.APL_COMPILE_HMI), '-c', probe, '-o', join(root, 'bad.o')], { encoding: 'utf8' })
        assert.notEqual(mismatch.status, 0); assert.match(mismatch.stderr, /macro mismatch/)
        assert.equal((await app.inject({ method: 'GET', url: `/api/products/${p.id}/firmware.zip?revision=2` })).statusCode, 409)
      }
    }
    assert.deepEqual(readFileSync(join(repository, 'apl_lib/config/apl_build_config.h')), sourceBefore)
  } finally { await app.close(); products.close(); clean(directory) }
})

test('面板仿真实际使用裁剪开关；纯主控早期拒绝，蜂鸣器和按键裁剪生效', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-trim-sim-')), registry = new TemplateRegistry(), provider = new RuntimeProfileProvider(repository)
  registry.register(provider)
  const products = new ProductStore(join(directory, 'studio.db')), service = new SimulationService(join(directory, 'sim'), registry)
  try {
    const d = newProfile(provider.catalog, '裁剪仿真')
    await assert.rejects(service.create(products.create(setFirmwareBuild(d, buildPreset(d, 'core-only')))), /纯主控/)
    for (const preset of ['compact', 'minimal'] as const) {
      const b = buildPreset(d, preset); b.defines.APL_HMI_COMPILE_KEY_LIGHT = 0
      const product = products.create(setFirmwareBuild({ ...d, code: d.code + '_' + preset }, b))
      let s = await service.create(product, false), deadline = Date.now() + 30000
      while (s.status === 'building' && Date.now() < deadline) { await new Promise(r => setTimeout(r, 30)); s = service.get(s.id) }
      assert.equal(s.status, 'ready', s.diagnostics)
      assert.ok(!s.descriptor.controls.some(c => c.key === 'Light'))
      s = await service.action(s.id, { type: 'advance', ms: 2100 })
      s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
      assert.equal(s.snapshot!.internal.run_state, 1)
      if (preset === 'minimal') assert.ok(!s.events.some(e => e.kind === 'output' && /蜂鸣器.*开/.test(e.message)))
      service.stop(s.id)
    }
  } finally { await service.close(); products.close(); clean(directory) }
})

test('Wi-Fi彻底裁剪：专用符号和配置存储消失，离线源码与面板仍可运行', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-no-wifi-')), registry = new TemplateRegistry(), provider = new RuntimeProfileProvider(repository)
  registry.register(provider)
  const products = new ProductStore(join(directory, 'studio.db')), service = new SimulationService(join(directory, 'sim'), registry)
  try {
    let d = setProfile(newProfile(provider.catalog, '无Wi-Fi'), 'modules', 0)
    // Retain a configured identity in Studio; the offline C profile excludes it.
    d = setProfile(d, 'pid', '000Ab09')
    const build = buildPreset(d, 'compact'); d = setFirmwareBuild(d, build)
    assert.equal(c51InstanceBytes(build), 340)
    const files = provider.firmwareFiles(d)
    assert.ok(!files.has('src/apl_wifi.c'), 'offline delivery must omit the optional implementation file')
    for (const [name, bytes] of files) { const target = join(directory, name); mkdirSync(dirname(target), { recursive: true }); writeFileSync(target, bytes) }
    const include = ['-I' + join(directory, 'include'), '-I' + join(directory, 'config')]
    for (const name of ['apl_core', 'apl_hmi', 'apl_wifi']) {
      const assembly = join(directory, name + '.s')
      const source = name === 'apl_wifi' ? join(repository, 'apl_lib/src/apl_wifi.c') : join(directory, 'src', name + '.c')
      command(compiler, ['-std=c11', ...include, '-S', source, '-o', assembly])
      assert.doesNotMatch(readFileSync(assembly, 'utf8'), /apl_core_get_pid|apl_core_set_wifi_connected|apl_wifi_(pair|connected|second|cancel_pairing|reset)/)
    }
    const probe = join(directory, 'consumer.c'), exe = join(directory, process.platform === 'win32' ? 'offline.exe' : 'offline')
    writeFileSync(probe, '#include "apl_product_profile.h"\n#if APL_COMPILE_WIFI\n#error "expected offline build"\n#endif\nint main(void) { struct apl d; struct apl_profile p; apl_product_profile_defaults(&p); return apl_init_profile(&d,0,0,0,0,&p); }\n')
    const sources = ['apl', 'apl_core', 'apl_hmi'].map(n => join(directory, 'src', n + '.c'))
    command(compiler, ['-std=c11', '-Wall', '-Wextra', '-Werror', ...include, probe, ...sources, '-o', exe]); command(exe, [])
    if (process.env.APL_STUDIO_C51) for (const [i, source] of [...sources, probe, join(repository, 'apl_lib/src/apl_wifi.c')].entries()) {
      const assembly = join(directory, 'target' + i + '.a51')
      command(process.env.APL_STUDIO_C51, [source, 'LARGE', 'OBJECTEXTEND', 'OPTIMIZE(8,SIZE)', `INCDIR(${join(directory, 'include')},${join(directory, 'config')})`, `SRC(${assembly})`, `PRINT(${join(directory, 'target' + i + '.lst')})`])
      assert.doesNotMatch(readFileSync(assembly, 'utf8'), /(?:PUBLIC|EXTRN|LCALL)[^\r\n]*(?:apl_core_get_pid|apl_core_set_wifi_connected|apl_wifi_(?:pair|connected|second|cancel_pairing|reset))/i)
    }
    const bad = join(directory, 'bad-api.c')
    writeFileSync(bad, '#include "apl.h"\nvoid (*p)(struct apl_core *, char *) = apl_core_get_pid;\n')
    assert.notEqual(spawnSync(compiler, ['-std=c11', ...include, '-c', bad, '-o', join(directory, 'bad-api.o')]).status, 0)
    let s = await service.create(products.create(d), false), deadline = Date.now() + 30000
    while (s.status === 'building' && Date.now() < deadline) { await new Promise(r => setTimeout(r, 30)); s = service.get(s.id) }
    assert.equal(s.status, 'ready', s.diagnostics)
    assert.ok(!s.descriptor.controls.some(c => c.key === 'wifi_connected'))
    s = await service.action(s.id, { type: 'advance', ms: 2100 })
    s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
    assert.equal(s.snapshot!.internal.run_state, 1)
  } finally { await service.close(); products.close(); clean(directory) }
})
