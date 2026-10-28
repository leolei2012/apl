import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync, cpSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { unzipSync, strFromU8 } from 'fflate'
import { emptyDefinition, type Definition, type Product } from '@apl-studio/model'
import { applyTemplate, toggleTemplateFeature, validateTemplateConfiguration, type BuildJob } from '@apl-studio/model/template'
import { CSourceTemplate, TemplateRegistry } from '@apl-studio/templates'
import { SdkBuildService } from '@apl-studio/build'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { TemplateConfiguration } from '../apps/web/src/TemplateConfiguration'

const templateDir = resolve(import.meta.dirname, '../../apl_sdk_template/air-care/0.1.0')
const temporary = () => mkdtempSync(join(tmpdir(), 'apl-sdk-test-'))
function cleanup(dir: string) {
  assert.equal(resolve(dirname(dir)), resolve(tmpdir()))
  assert.ok(basename(dir).startsWith('apl-sdk-test-'))
  rmSync(dir, { recursive: true, force: true })
}
function definition(provider: CSourceTemplate) {
  return applyTemplate({ ...emptyDefinition(), name: 'SDK 测试', code: 'sdk_test' }, provider.descriptor)
}
function product(d: Definition): Product {
  return { id: randomUUID(), revision: 1, createdAt: new Date().toISOString(), updatedAt: new Date().toISOString(), definition: d }
}
async function finished(service: SdkBuildService, job: BuildJob) {
  const end = Date.now() + 30000
  while (Date.now() < end) {
    const current = service.get(job.id)
    if (!['building', 'queued'].includes(current.status)) return current
    await new Promise(resolve => setTimeout(resolve, 40))
  }
  assert.fail('SDK 构建超时')
}

test('真实 APL 模板：三种能力组合生成、C11 编译、消费执行、ZIP 校验', async () => {
  const dir = temporary(), provider = new CSourceTemplate(templateDir), registry = new TemplateRegistry()
  registry.register(provider)
  const service = new SdkBuildService(dir, registry)
  try {
    for (const disabled of [null, 'purify', 'humidify']) {
      let d = definition(provider)
      if (disabled) d = toggleTemplateFeature(d, provider.descriptor, disabled, false)
      d.functions.find(f => f.key === 'fan_speed')!.default = 3
      for (const p of d.parameters) { if (p.key === 'dry_seconds') p.value = 7; if (p.key === 'filter_life_h') p.value = 4320 }
      assert.deepEqual(provider.validate(d), [])
      const saved = product(d), started = await service.create(saved)
      saved.definition.parameters.find(p => p.key === 'filter_life_h')!.value = 99
      saved.revision = 2
      const job = await finished(service, started)
      assert.equal(job.status, 'succeeded', job.diagnostics)
      assert.deepEqual(job.checks, { compile: 'passed', consumer: 'passed' })
      const files = unzipSync(service.artifact(job.id).bytes)
      const manifest = JSON.parse(strFromU8(files['apl-sdk/manifest.json']))
      assert.equal(manifest.sdkReady, true)
      assert.equal(manifest.product.revision, 1)
      assert.equal(manifest.template.fingerprint, provider.descriptor.fingerprint)
      for (const [path, digest] of Object.entries(manifest.files)) assert.equal(createHash('sha256').update(files['apl-sdk/' + path]).digest('hex'), digest)
      const config = strFromU8(files['apl-sdk/config/apl_core_config.h'])
      assert.match(config, /#define APL_CORE_FILTER_LIFE_H 4320u/)
      if (disabled !== 'humidify') assert.match(config, /#define APL_CORE_DRY_S 7u/)
      const header = strFromU8(files['apl-sdk/include/apl_product.h'])
      assert.match(header, /apl_sdk_test_set_power\(apl_sdk_test_t \*self, bool value\)/)
      assert.doesNotMatch(header, /set_pm25/)
      assert.match(job.diagnostics, /APL SDK configuration/)
    }
  } finally { await service.close(); cleanup(dir) }
})

test('模板约束拒绝未知能力、缺失依赖和非法参数；切换能力同步依赖', () => {
  const provider = new CSourceTemplate(templateDir), d = definition(provider), metadata = provider.descriptor
  const invalid = (change: (d: Definition) => void) => { const copy = structuredClone(d); change(copy); return validateTemplateConfiguration(copy, metadata) }
  assert.ok(invalid(d => d.parameters.find(p => p.key === 'dry_seconds')!.value = 0).length)
  assert.ok(invalid(d => d.functions.push({ ...d.functions[0], id: 'unknown', key: 'compressor' })).length)
  assert.ok(invalid(d => d.parameters.push({ id: 'unknown', key: 'unknown', name: '未知', value: 1 })).length)
  assert.ok(invalid(d => d.functions = d.functions.filter(f => f.key !== 'humidify')).length)
  assert.ok(invalid(d => d.functions = d.functions.filter(f => f.key !== 'power')).length)
  let changed = toggleTemplateFeature(d, metadata, 'humidify', false)
  assert.ok(!changed.functions.some(f => ['humidify', 'humidity', 'drying'].includes(f.key)))
  assert.ok(!changed.parameters.some(p => p.key === 'dry_seconds'))
  changed = toggleTemplateFeature(changed, metadata, 'humidity', true)
  assert.ok(changed.functions.some(f => f.key === 'humidify'))
  assert.ok(changed.parameters.some(p => p.key === 'dry_seconds'))
  assert.deepEqual(provider.validate(changed), [])
  const neither = toggleTemplateFeature(toggleTemplateFeature(d, metadata, 'purify', false), metadata, 'humidify', false)
  assert.ok(provider.validate(neither).some(message => message.includes('至少')))
})

test('新版本增加参数：同一 React 表单自动显示、值写入实际 C 配置并通过消费验证', async () => {
  const dir = temporary(), versionDir = join(dir, 'template')
  cpSync(templateDir, versionDir, { recursive: true })
  const manifest = JSON.parse(readFileSync(join(versionDir, 'template.json'), 'utf8'))
  manifest.version = '0.1.1'
  manifest.parameters.push({ key: 'wifi_timeout_seconds', name: 'Wi-Fi 配网超时', description: '模板新增参数，无需修改 React', kind: 'int', default: 120, min: 1, max: 600, step: 1, unit: 's', binding: { path: 'config/apl_core_config.h', macro: 'APL_CORE_WIFI_PAIR_S' } })
  writeFileSync(join(versionDir, 'template.json'), JSON.stringify(manifest))
  const provider = new CSourceTemplate(versionDir), registry = new TemplateRegistry()
  registry.register(provider)
  const service = new SdkBuildService(join(dir, 'jobs'), registry)
  try {
    const d = definition(provider)
    d.parameters.find(p => p.key === 'wifi_timeout_seconds')!.value = 123
    d.functions.find(f => f.key === 'power')!.default = true
    d.functions.find(f => f.key === 'purify')!.default = true
    d.functions.find(f => f.key === 'humidify')!.default = false
    const html = renderToStaticMarkup(createElement(TemplateConfiguration, { definition: d, templates: [provider.descriptor], onChange() {}, onBind() {}, mode: 'parameters' }))
    assert.match(html, /Wi-Fi 配网超时/)
    assert.match(html, /value="123"/)
    const job = await finished(service, await service.create(product(d)))
    assert.equal(job.status, 'succeeded', job.diagnostics)
    const archive = unzipSync(service.artifact(job.id).bytes)
    assert.match(strFromU8(archive['apl-sdk/config/apl_core_config.h']), /#define APL_CORE_WIFI_PAIR_S 123u/)
    assert.notEqual(provider.descriptor.fingerprint, new CSourceTemplate(templateDir).descriptor.fingerprint)
  } finally { await service.close(); cleanup(dir) }
})

test('构建失败保留诊断和旧产物；重启可恢复已验证的交付记录', async () => {
  const dir = temporary(), provider = new CSourceTemplate(templateDir), registry = new TemplateRegistry()
  const unregister = registry.register(provider)
  let service = new SdkBuildService(dir, registry)
  try {
    const p = product(definition(provider)), good = await finished(service, await service.create(p))
    assert.equal(good.status, 'succeeded', good.diagnostics)
    const oldArtifact = service.artifact(good.id).bytes
    unregister()
    registry.register({ descriptor: provider.descriptor, validate: d => provider.validate(d), async generate(d) { const files = await provider.generate(d); files.set('src/broken.c', Buffer.from('#error deliberate_compile_failure\n')); return files } })
    const bad = await finished(service, await service.create(p))
    assert.equal(bad.status, 'failed')
    assert.equal(bad.checks.compile, 'failed')
    assert.match(bad.diagnostics, /deliberate_compile_failure/)
    assert.throws(() => service.artifact(bad.id), /不能下载/)
    assert.deepEqual(service.artifact(good.id).bytes, oldArtifact)
    await service.close()
    service = new SdkBuildService(dir, registry)
    assert.equal(service.get(good.id).status, 'succeeded')
    assert.equal(service.get(bad.id).status, 'failed')
    assert.deepEqual(service.artifact(good.id).bytes, oldArtifact)
  } finally { await service.close(); cleanup(dir) }
})

test('缺失编译器不能产生可下载 SDK，越界文件路径被拒绝', async () => {
  const dir = temporary(), provider = new CSourceTemplate(templateDir), registry = new TemplateRegistry()
  const unregister = registry.register(provider)
  const service = new SdkBuildService(dir, registry, join(dir, 'no-compiler.exe'))
  try {
    const p = product(definition(provider)), job = await finished(service, await service.create(p))
    assert.equal(job.status, 'failed')
    assert.match(job.diagnostics, /无法启动/)
    assert.throws(() => service.artifact(job.id))
    unregister()
    registry.register({ descriptor: provider.descriptor, validate: () => [], async generate() { return new Map([['../escape.c', Buffer.from('bad')]]) } })
    await assert.rejects(service.create(p), /路径不安全/)
    assert.equal(service.list(p.id).length, 1)
  } finally { await service.close(); cleanup(dir) }
})

test('HTTP 闭环：发现模板、显式绑定、校验保存、预览、构建、下载；拒绝过期修订', async () => {
  const dir = temporary(), provider = new CSourceTemplate(templateDir), registry = new TemplateRegistry()
  registry.register(provider)
  const store = new ProductStore(join(dir, 'studio.db')), service = new SdkBuildService(join(dir, 'sdk'), registry), app = createApi(store, registry, undefined, service)
  try {
    assert.equal((await app.inject('/api/templates')).json().templates.length, 1)
    assert.equal((await app.inject('/api/system')).json().capabilities.sdkGeneration, true)
    const original = { ...emptyDefinition(), name: 'HTTP 产品', code: 'http_product' }
    const configured = await app.inject({ method: 'POST', url: '/api/templates/apl.air-care/0.1.0/configure', payload: { definition: original } })
    assert.equal(configured.statusCode, 200)
    assert.equal(store.list().length, 0)
    const d = configured.json().definition as Definition
    d.parameters.find(p => p.key === 'dry_seconds')!.value = 14
    const created = await app.inject({ method: 'POST', url: '/api/products', payload: d })
    assert.equal(created.statusCode, 201, created.body)
    const p = created.json() as Product
    const preview = await app.inject({ method: 'POST', url: '/api/templates/preview', payload: { definition: d } })
    assert.equal(preview.statusCode, 200, preview.body)
    assert.match(preview.json().files['config/apl_core_config.h'], /#define APL_CORE_DRY_S 14u/)
    const invalid = structuredClone(d)
    invalid.parameters.find(p => p.key === 'dry_seconds')!.value = 0
    assert.equal((await app.inject({ method: 'PUT', url: '/api/products/' + p.id, payload: { revision: 1, definition: invalid } })).statusCode, 400)
    const response = await app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: 1 } })
    assert.equal(response.statusCode, 202, response.body)
    store.update(p.id, 1, { ...d, name: '构建过程中产品更新' })
    const job = await finished(service, response.json())
    assert.equal(job.status, 'succeeded', job.diagnostics)
    const artifact = await app.inject('/api/sdk/jobs/' + job.id + '/download')
    assert.equal(artifact.statusCode, 200)
    assert.equal(artifact.headers['content-type'], 'application/zip')
    const files = unzipSync(artifact.rawPayload)
    assert.equal(JSON.parse(strFromU8(files['apl-sdk/manifest.json'])).product.revision, 1)
    assert.equal((await app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: 1 } })).statusCode, 409)
    assert.equal((await app.inject('/api/products/' + p.id + '/sdk/jobs')).json().length, 1)
  } finally { await service.close(); await app.close(); store.close(); cleanup(dir) }
})
