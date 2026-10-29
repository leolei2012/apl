import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { createHash } from 'node:crypto'
import { unzipSync, strFromU8 } from 'fflate'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { definitionFromTemplate, toggleTemplateFeature, type BuildJob } from '@apl-studio/model/template'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { SdkBuildService, SimulationService } from '@apl-studio/build'
import { createApi } from '@apl-studio/api'
import { TemplateCatalog } from '../apps/web/src/TemplateCatalog'
import { api as webApi } from '../apps/web/src/api'

const catalog = resolve(import.meta.dirname, '../../apl_sdk_template')
const hash = (bytes: Uint8Array) => createHash('sha256').update(bytes).digest('hex')
function fixture() {
  const directory = mkdtempSync(join(tmpdir(), 'apl-release-test-')), registry = new TemplateRegistry()
  registry.loadDirectory(catalog)
  const store = new ProductStore(join(directory, 'studio.db'))
  const builds = new SdkBuildService(join(directory, 'sdk'), registry)
  const simulations = new SimulationService(join(directory, 'simulation'), registry)
  const app = createApi(store, registry, undefined, builds, simulations)
  return { registry, store, builds, simulations, app, async close() {
    await simulations.close(); await builds.close(); await app.close(); store.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir()))
    assert.ok(basename(directory).startsWith('apl-release-test-'))
    rmSync(directory, { recursive: true, force: true })
  } }
}
function product(f: ReturnType<typeof fixture>, version: string) {
  const template = f.registry.get('apl.air-care', version)!.descriptor
  let definition = definitionFromTemplate(template)
  definition.code = 'release_' + version.replaceAll('.', '_')
  definition = toggleTemplateFeature(definition, template, 'humidify', false)
  definition.functions.find(field => field.key === 'power')!.default = true
  definition.functions.find(field => field.key === 'fan_speed')!.default = 4
  return f.store.create(definition)
}
async function ready(service: SimulationService, started: SimulationSession) {
  const deadline = Date.now() + 30000
  while (Date.now() < deadline) {
    const current = service.get(started.id)
    if (current.status !== 'building') { assert.equal(current.status, 'ready', current.diagnostics); return current }
    await new Promise(resolve => setTimeout(resolve, 30))
  }
  assert.fail('版本仿真启动超时')
}
async function finished(builds: SdkBuildService, job: BuildJob) {
  const deadline = Date.now() + 30000
  while (Date.now() < deadline) {
    const current = builds.get(job.id)
    if (!['queued', 'building'].includes(current.status)) return current
    await new Promise(resolve => setTimeout(resolve, 40))
  }
  assert.fail('版本SDK构建超时')
}

test('AP 新旧版本可区分，0.1.1快照有根库来源哈希，旧产品保持精确绑定', async () => {
  const f = fixture()
  try {
    const old = f.registry.get('apl.air-care', '0.1.0')!, latest = f.registry.get('apl.air-care', '0.1.1')!
    const versions = (await f.app.inject('/api/templates')).json().templates
    assert.deepEqual(versions.filter((t: { id: string }) => t.id === 'apl.air-care').map((t: { version: string }) => t.version).sort(), ['0.1.0', '0.1.1', '0.2.0', '0.3.0', '0.4.0', '0.4.1', '0.4.2'])
    assert.equal(old.descriptor.sourceVersion, '0.1.0')
    assert.equal(latest.descriptor.sourceVersion, '0.1.1')
    assert.notEqual(old.descriptor.fingerprint, latest.descriptor.fingerprint)
    const html = renderToStaticMarkup(createElement(TemplateCatalog, { templates: versions, busy: false, onCreate() {} }))
    assert.match(html, /apl\.air-care@0\.1\.0/)
    assert.match(html, /apl\.air-care@0\.1\.1/)
    const snapshotRoot = join(catalog, 'air-care/0.1.1')
    const provenance = JSON.parse(readFileSync(join(snapshotRoot, 'source-snapshot.json'), 'utf8'))
    assert.equal(provenance.sourceVersion, '0.1.1')
    assert.equal(Object.keys(provenance.rootFiles).length, 9)
    for (const [path, digest] of Object.entries(provenance.rootFiles)) assert.equal(hash(readFileSync(join(snapshotRoot, path))), digest)
    assert.equal(hash(readFileSync(join(snapshotRoot, 'verification/consumer.c'))), provenance.validationSource.sha256)
    const legacy = product(f, '0.1.0'), upgraded = product(f, '0.1.1')
    assert.deepEqual((await f.app.inject('/api/products/' + legacy.id)).json().definition.template, { id: 'apl.air-care', version: '0.1.0' })
    assert.deepEqual(upgraded.definition.template, { id: 'apl.air-care', version: '0.1.1' })
    assert.equal(f.store.get(legacy.id).revision, 1)
  } finally { await f.close() }
})

test('Web 显式选择0.1.1运行真实C纯净化新规则，0.1.0仍运行旧规则与Auto语义', async () => {
  const f = fixture(), originalFetch = globalThis.fetch
  try {
    const origin = await f.app.listen({ host: '127.0.0.1', port: 0 })
    globalThis.fetch = (input, init) => originalFetch(new URL(String(input), origin), init)
    for (const version of ['0.1.0', '0.1.1']) {
      const p = product(f, version)
      let s = await ready(f.simulations, await webApi.startSimulation(p)), id = s.id
      await webApi.simulate(id, { type: 'running', value: false })
      await webApi.simulate(id, { type: 'advance', ms: 2100 })
      const values = [0, 35, 36, 75, 76, 150, 151, 65535, 35]
      const expected = version === '0.1.1' ? [0, 0, 1, 1, 2, 2, 3, 3, 0] : [1, 1, 2, 2, 3, 3, 3, 3, 1]
      for (let i = 0; i < values.length; i++) {
        await webApi.simulate(id, { type: 'input', key: 'pm25', value: values[i] })
        s = await webApi.simulate(id, { type: 'advance', ms: 1000 })
        assert.equal(s.template.version, version)
        assert.equal(s.snapshot!.outputs.fan_gear, expected[i])
        assert.equal(s.snapshot!.outputs.fan, 1)
        assert.equal(s.snapshot!.internal.fan_speed, 4)
        assert.equal(s.snapshot!.internal.sleep, 0)
        assert.equal(s.snapshot!.display.icon_12, 255) // Auto
        assert.equal(s.snapshot!.display.icon_8, 0) // Sleep
      }
      s = await webApi.simulate(id, { type: 'advance', ms: 4000 })
      assert.equal(s.snapshot!.display.brightness, 255)
      await webApi.stopSimulation(id)
      assert.equal(f.store.get(p.id).definition.template!.version, version)
      assert.equal(f.store.get(p.id).revision, 1)
    }
  } finally { globalThis.fetch = originalFetch; await f.close() }
})

test('新旧绑定生成并消费各自SDK，版本/指纹/下载校验一致，新包验证真实自动档边界', async () => {
  const f = fixture()
  try {
    const fingerprints = []
    for (const version of ['0.1.0', '0.1.1']) {
      const p = product(f, version)
      const response = await f.app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: p.revision } })
      assert.equal(response.statusCode, 202, response.body)
      const job = await finished(f.builds, response.json())
      assert.equal(job.status, 'succeeded', job.diagnostics)
      assert.deepEqual(job.checks, { compile: 'passed', consumer: 'passed' })
      const download = await f.app.inject('/api/sdk/jobs/' + job.id + '/download')
      assert.equal(download.statusCode, 200, download.body)
      const files = unzipSync(download.rawPayload), manifest = JSON.parse(strFromU8(files['apl-sdk/manifest.json']))
      assert.equal(manifest.sourceVersion, version)
      assert.equal(manifest.template.version, version)
      assert.equal(manifest.template.fingerprint, f.registry.get('apl.air-care', version)!.descriptor.fingerprint)
      assert.equal(manifest.product.id, p.id)
      assert.equal(manifest.product.revision, 1)
      assert.equal(manifest.fingerprint, job.fingerprint)
      assert.equal(manifest.sdkReady, true)
      for (const [path, digest] of Object.entries(manifest.files)) assert.equal(hash(files['apl-sdk/' + path]), digest)
      if (version === '0.1.1') {
        assert.match(job.diagnostics, /pure auto boundaries and Auto\/sleep semantics verified/)
        assert.match(strFromU8(files['apl-sdk/CMakeLists.txt']), /add_executable\(apl_sdk_smoke verification\/consumer.c\)/)
        assert.ok(files['apl-sdk/source-snapshot.json'])
        assert.match(strFromU8(files['apl-sdk/include/apl.h']), /APL_VERSION_STRING\s+"0\.1\.1"/)
      } else {
        assert.match(job.diagnostics, /APL SDK configuration, power, fan and dry recovery verified/)
        assert.match(strFromU8(files['apl-sdk/CMakeLists.txt']), /add_executable\(apl_sdk_smoke verification\/smoke.c\)/)
        assert.ok(!files['apl-sdk/source-snapshot.json'])
      }
      fingerprints.push(manifest.fingerprint)
    }
    assert.notEqual(fingerprints[0], fingerprints[1])
    const provider = f.registry.get('apl.air-care', '0.1.1')!, broken = new TemplateRegistry()
    broken.register({ descriptor: provider.descriptor, validate: d => provider.validate(d), async generate(d) {
      const files = await provider.generate(d)
      const path = 'config/apl_core_config.h'
      files.set(path, Buffer.from(strFromU8(files.get(path)!).replace(/#define APL_DEFAULT_FAN_GEAR 4u/, '#define APL_DEFAULT_FAN_GEAR 3u')))
      return files
    } })
    const negative = new SdkBuildService(join(f.builds.directory, 'negative'), broken)
    try {
      const p = f.store.list().find(p => p.definition.template?.version === '0.1.1')!
      const failed = await finished(negative, await negative.create(p))
      assert.equal(failed.status, 'failed')
      assert.equal(failed.checks.compile, 'failed')
      assert.match(failed.diagnostics, /default fan_speed/)
      assert.throws(() => negative.artifact(failed.id), /不能下载/)
    } finally { await negative.close() }
  } finally { await f.close() }
})
