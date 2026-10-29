import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname } from 'node:path'
import { createHash } from 'node:crypto'
import { unzipSync, strFromU8, zipSync, strToU8 } from 'fflate'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { writeFileSync } from 'node:fs'
import { applyTemplate, definitionFromTemplate } from '@apl-studio/model/template'
import type { Product } from '@apl-studio/model'
import { TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { AcceptanceService, SdkBuildService, SimulationService, simulationAdapters } from '@apl-studio/build'
import { createApi } from '@apl-studio/api'
import { CreateProductFlow } from '../apps/web/src/CreateProductFlow'
import { PmWorkspace } from '../apps/web/src/PmWorkspace'

function fixture() {
  const directory = mkdtempSync(join(tmpdir(), 'apl-pm-test-')), registry = new TemplateRegistry()
  registry.loadDirectory(resolve(import.meta.dirname, '../../apl_sdk_template'))
  const store = new ProductStore(join(directory, 'studio.db'))
  const simulations = new SimulationService(join(directory, 'simulation'), registry)
  const acceptances = new AcceptanceService(join(directory, 'acceptance'), registry, simulations)
  const builds = new SdkBuildService(join(directory, 'sdk'), registry, undefined, undefined, acceptances)
  const app = createApi(store, registry, undefined, builds, simulations, acceptances)
  return { directory, registry, store, simulations, acceptances, builds, app, async close() {
    await app.close(); await simulations.close(); await acceptances.close(); await builds.close(); store.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir()))
    rmSync(directory, { recursive: true, force: true })
  } }
}
async function accepted(f: ReturnType<typeof fixture>, product: Product) {
  const response = await f.app.inject({ method: 'POST', url: '/api/acceptance/run', payload: { productId: product.id, revision: product.revision } })
  assert.equal(response.statusCode, 202, response.body)
  const deadline = Date.now() + 70000
  while (Date.now() < deadline) {
    const result = f.acceptances.latest(product)
    if (result.report?.status !== 'running') return result
    await new Promise(resolve => setTimeout(resolve, 30))
  }
  assert.fail('产品经理场景验收超时')
}
async function built(f: ReturnType<typeof fixture>, product: Product) {
  const response = await f.app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: product.id, revision: product.revision } })
  assert.equal(response.statusCode, 202, response.body)
  const id = response.json().id, deadline = Date.now() + 30000
  while (Date.now() < deadline) {
    const job = f.builds.get(id)
    if (!['queued', 'building'].includes(job.status)) { assert.equal(job.status, 'succeeded', job.diagnostics); return job }
    await new Promise(resolve => setTimeout(resolve, 30))
  }
  assert.fail('产品经理SDK构建超时')
}

test('产品经理闭环：服务端真实C验收、极速开关、过期门禁、中文交付和历史下载', async () => {
  const f = fixture()
  try {
    const template = f.registry.get('apl.air-care', '0.2.0')!.descriptor
    const d = definitionFromTemplate(template); d.name = '客厅空气净化方案'
    const legacyDraft = definitionFromTemplate(f.registry.get('apl.air-care', '0.1.0')!.descriptor)
    assert.deepEqual(applyTemplate(legacyDraft, template).functions.map(f => f.key), ['power', 'fan_speed', 'purify', 'pm25'])
    let p = f.store.create(d)
    assert.deepEqual(p.definition.functions.map(f => f.key), ['power', 'fan_speed', 'purify', 'pm25'])
    assert.equal((await f.app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: p.revision, passed: true } })).statusCode, 409)
    assert.equal((await f.app.inject({ method: 'POST', url: '/api/acceptance/run', payload: { productId: p.id, revision: p.revision, status: 'passed', checks: [] } })).statusCode, 400)
    assert.equal((await f.app.inject({ method: 'POST', url: '/api/acceptance/run', payload: { productId: p.id, revision: 999 } })).statusCode, 409)
    const first = await accepted(f, p)
    assert.equal(first.current, true); assert.equal(first.report!.status, 'passed', first.report!.diagnostics)
    assert.equal(first.report!.checks.length, 10)
    assert.ok(first.report!.checks.every(c => c.passed))
    assert.match(first.report!.sourceFingerprint, /^[a-f0-9]{64}$/)
    const old = await built(f, p)
    assert.match(old.diagnostics, /PM interaction and Turbo option verified/)
    const archive = unzipSync(f.builds.artifact(old.id).bytes)
    for (const path of ['docs/product-spec.md', 'docs/acceptance-report.md', 'docs/hardware-checklist.md', 'verification/acceptance.json']) assert.ok(archive['apl-sdk/' + path], path)
    assert.match(strFromU8(archive['apl-sdk/docs/product-spec.md']), /长按3秒：进入\/退出极速/)
    assert.match(strFromU8(archive['apl-sdk/docs/hardware-checklist.md']), /UV安全联锁/)
    const manifest = JSON.parse(strFromU8(archive['apl-sdk/manifest.json']))
    assert.equal(manifest.verification.acceptance.fingerprint, first.report!.fingerprint)
    for (const [path, digest] of Object.entries(manifest.files)) assert.equal(createHash('sha256').update(archive['apl-sdk/' + path]).digest('hex'), digest)
    const savedZip = f.builds.artifact(old.id).bytes
    manifest.verification.acceptance.fingerprint = '0'.repeat(64)
    archive['apl-sdk/manifest.json'] = strToU8(JSON.stringify(manifest))
    const archivePath = join(f.directory, 'sdk', old.id, 'apl-sdk.zip')
    writeFileSync(archivePath, zipSync(archive))
    assert.throws(() => f.builds.artifact(old.id), /归档校验/)
    writeFileSync(archivePath, savedZip)
    const provider = f.registry.get('apl.air-care', '0.2.0')!, originalGenerate = provider.generate.bind(provider)
    try {
      provider.generate = async d => { const files = await originalGenerate(d); files.set('src/apl_core.c', Buffer.concat([files.get('src/apl_core.c')!, Buffer.from('\n/* generator changed */\n')])); return files }
      assert.equal((await f.app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: p.revision } })).statusCode, 409)
    } finally { provider.generate = originalGenerate }
    const disabled = structuredClone(p.definition)
    disabled.parameters.find(p => p.key === 'turbo_enabled')!.value = false
    p = f.store.update(p.id, p.revision, disabled)
    assert.equal(f.acceptances.latest(p).current, false)
    assert.equal((await f.app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: p.revision } })).statusCode, 409)
    assert.equal((await f.app.inject(`/api/sdk/jobs/${old.id}/download`)).statusCode, 200)
    const second = await accepted(f, p)
    assert.equal(second.report!.status, 'passed', second.report!.diagnostics)
    assert.match(second.report!.checks[8].name, /关闭极速/)
    const next = await built(f, p)
    assert.match(next.diagnostics, /PM interaction and Turbo option verified/)
    const files = unzipSync(f.builds.artifact(next.id).bytes)
    assert.match(strFromU8(files['apl-sdk/config/apl_core_config.h']), /#define APL_CORE_HAS_TURBO 0u/)
    assert.match(strFromU8(files['apl-sdk/docs/product-spec.md']), /无档位切换动作/)
    const restored = new AcceptanceService(join(f.directory, 'acceptance'), f.registry, f.simulations)
    assert.equal(restored.require(p).fingerprint, second.report!.fingerprint)
    const adapter = simulationAdapters[0], original = adapter.source
    try {
      adapter.source = definition => '/* changed bridge */\n' + original(definition)
      assert.equal(restored.latest(p).current, false)
      assert.throws(() => restored.require(p), /当前产品修订/)
    } finally { adapter.source = original }
    const unavailable = template.features.find(feature => feature.key === 'humidify')!
    const invalid = structuredClone(p.definition); invalid.functions.push(structuredClone(unavailable.definition))
    assert.equal((await f.app.inject({ method: 'PUT', url: '/api/products/' + p.id, payload: { revision: p.revision, definition: invalid } })).statusCode, 400)
    assert.equal(f.registry.get('apl.air-care', '0.1.0')!.descriptor.workflow, undefined)
  } finally { await f.close() }
})

test('产品经理入口和规则卡默认不要求技术字段', () => {
  const registry = new TemplateRegistry(); registry.loadDirectory(resolve(import.meta.dirname, '../../apl_sdk_template'))
  const template = registry.get('apl.air-care', '0.2.0')!.descriptor, definition = definitionFromTemplate(template)
  const create = renderToStaticMarkup(createElement(CreateProductFlow, { templates: registry.list(), products: [], busy: false, onClose() {}, async onCreate() {}, engineering: () => '工程配置' }))
  assert.match(create, /创建并定义方案/)
  assert.doesNotMatch(create, /产品标识|数据类型|SDK 目标版本/)
  const html = renderToStaticMarkup(createElement(PmWorkspace, { product: { id: 'preview', revision: 1, createdAt: '', updatedAt: '', definition }, draft: definition, setDraft() {}, tab: 'functions', setTab() {}, dirty: false, onSave() {}, onExport() {}, template }))
  assert.match(html, /风速键长按进入极速/)
  assert.match(html, /自动档的最低风速不等于睡眠模式/)
  assert.match(html, /<details class="pm-technical"><summary>查看技术追溯信息/)
  assert.doesNotMatch(html, /APL_CORE_HAS_TURBO|数据类型|功能标识/)
})

test('所有界面提供的开机档位均可通过真实C场景，未提供的极速开机配置被拒绝', async () => {
  const f = fixture()
  try {
    const template = f.registry.get('apl.air-care', '0.2.0')!.descriptor
    for (const gear of [0, 1, 2, 3, 4]) {
      const d = definitionFromTemplate(template, f.store.list().map(p => p.definition.code))
      d.functions.find(f => f.key === 'fan_speed')!.default = gear
      const p = f.store.create(d), result = await accepted(f, p)
      assert.equal(result.report!.status, 'passed', `开机档位${gear}: ${result.report!.diagnostics}`)
    }
    const invalid = definitionFromTemplate(template)
    invalid.functions.find(f => f.key === 'fan_speed')!.default = 5
    assert.equal((await f.app.inject({ method: 'POST', url: '/api/products', payload: invalid })).statusCode, 400)
  } finally { await f.close() }
})

test('未通过场景不能交付；旧模板仍使用原编译消费流程', async () => {
  const f = fixture()
  try {
    const provider = f.registry.get('apl.air-care', '0.2.0')!, original = provider.generate.bind(provider)
    provider.generate = async definition => {
      const files = await original(definition)
      const path = 'src/apl_core.c'
      files.set(path, Buffer.from(Buffer.from(files.get(path)!).toString().replace('if (0u == APL_CORE_HAS_TURBO)', 'if (1u)')))
      return files
    }
    const p = f.store.create(definitionFromTemplate(provider.descriptor))
    const result = await accepted(f, p)
    assert.equal(result.report!.status, 'failed')
    assert.ok(result.report!.checks.some(c => !c.passed))
    assert.throws(() => f.acceptances.require(p), /当前产品修订/)
    const legacy = f.store.create(definitionFromTemplate(f.registry.get('apl.air-care', '0.1.0')!.descriptor))
    const job = await built(f, legacy)
    assert.equal(job.acceptance, undefined)
    assert.equal(f.builds.artifact(job.id).bytes[0], 0x50)
    assert.ok(readFileSync(join(f.directory, 'studio.db')).length)
  } finally { await f.close() }
})
