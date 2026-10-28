import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync, cpSync, readFileSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { createHash } from 'node:crypto'
import { unzipSync, strFromU8 } from 'fflate'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { definitionFromTemplate, type BuildJob } from '@apl-studio/model/template'
import { NativeCTemplate, TemplateRegistry } from '@apl-studio/templates'
import { SdkBuildService } from '@apl-studio/build'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { TemplateConfiguration } from '../apps/web/src/TemplateConfiguration'

const catalog = resolve(import.meta.dirname, '../../apl_sdk_template')
const lighting = join(catalog, 'lighting/1.0.0')
const temporary = () => mkdtempSync(join(tmpdir(), 'product-standard-test-'))
function cleanup(dir: string) {
  assert.equal(resolve(dirname(dir)), resolve(tmpdir()))
  assert.ok(basename(dir).startsWith('product-standard-test-'))
  rmSync(dir, { recursive: true, force: true })
}
async function finished(builds: SdkBuildService, id: string) {
  const end = Date.now() + 30000
  while (Date.now() < end) {
    const job = builds.get(id)
    if (!['queued', 'building'].includes(job.status)) return job
    await new Promise(resolve => setTimeout(resolve, 40))
  }
  assert.fail('构建超时')
}

test('同一目录发现 AP 与独立灯控；品类、默认标识、表单均来自模板', () => {
  const registry = new TemplateRegistry()
  registry.loadDirectory(catalog)
  assert.equal(registry.list().length, 2)
  const lamp = registry.get('standard.lighting', '1.0.0')!, air = registry.get('apl.air-care', '0.1.0')!
  assert.ok(lamp instanceof NativeCTemplate)
  const definition = definitionFromTemplate(lamp.descriptor, ['dimmable_light'])
  assert.equal(definition.code, 'dimmable_light_2')
  assert.equal(definition.category, '照明')
  assert.equal(definitionFromTemplate(air.descriptor).category, '环境电器')
  assert.deepEqual(definition.functions.map(f => f.key), ['power', 'brightness'])
  assert.deepEqual(lamp.validate(definition), [])
  const html = renderToStaticMarkup(createElement(TemplateConfiguration, { definition, templates: registry.list(), onChange() {}, onBind() {}, mode: 'features' }))
  assert.match(html, /亮度默认值/)
  assert.match(html, /type="number"/)
  assert.doesNotMatch(html, /风机档位/)
})

test('非 AP 产品 HTTP 闭环：独立目录、真实配置、默认值、消费执行、ZIP 哈希与版本记录', async () => {
  const dir = temporary(), registry = new TemplateRegistry()
  registry.loadDirectory(catalog)
  const store = new ProductStore(join(dir, 'studio.db')), builds = new SdkBuildService(join(dir, 'jobs'), registry), app = createApi(store, registry, undefined, builds)
  try {
    const provider = registry.get('standard.lighting', '1.0.0')!
    for (const delay of [0, 120, 5000]) {
      const d = definitionFromTemplate(provider.descriptor, store.list().map(p => p.definition.code))
      d.parameters[0].value = delay
      d.functions.find(f => f.key === 'brightness')!.default = 73
      d.functions.find(f => f.key === 'power')!.default = true
      const created = await app.inject({ method: 'POST', url: '/api/products', payload: d })
      assert.equal(created.statusCode, 201, created.body)
      const product = created.json()
      const preview = await app.inject({ method: 'POST', url: '/api/templates/preview', payload: { definition: d } })
      assert.equal(preview.statusCode, 200, preview.body)
      assert.deepEqual(Object.keys(preview.json().files), provider.descriptor.previewFiles)
      assert.match(preview.json().files['settings/product_config.h'], new RegExp('#define LIGHT_BOOT_DELAY_MS ' + delay + '(?:\\r?\\n)'))
      const response = await app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: product.id, revision: 1 } })
      assert.equal(response.statusCode, 202, response.body)
      const job = await finished(builds, response.json().id)
      assert.equal(job.status, 'succeeded', job.diagnostics)
      assert.deepEqual(job.checks, { compile: 'passed', consumer: 'passed' })
      assert.match(job.diagnostics, /Lighting defaults/)
      const download = await app.inject('/api/sdk/jobs/' + job.id + '/download')
      assert.equal(download.statusCode, 200)
      const files = unzipSync(download.rawPayload), manifest = JSON.parse(strFromU8(files['apl-sdk/manifest.json']))
      assert.equal(manifest.sourceVersion, '1.0.0')
      assert.equal(manifest.verification.scope, provider.descriptor.verification!.scope)
      assert.equal(manifest.sdkReady, true)
      assert.equal(manifest.template.fingerprint, provider.descriptor.fingerprint)
      assert.ok(!files['apl-sdk/include/apl.h'])
      assert.match(strFromU8(files['apl-sdk/api/product.h']), new RegExp('sdk_' + d.code + '_set_brightness'))
      assert.match(strFromU8(files['apl-sdk/settings/product_config.h']), /#define LIGHT_DEFAULT_BRIGHTNESS 73/)
      for (const [path, hash] of Object.entries(manifest.files)) assert.equal(createHash('sha256').update(files['apl-sdk/' + path]).digest('hex'), hash)
      const invalid = structuredClone(d)
      invalid.parameters[0].value = 115
      assert.equal((await app.inject({ method: 'PUT', url: '/api/products/' + product.id, payload: { revision: 1, definition: invalid } })).statusCode, 400)
    }
  } finally { await builds.close(); await app.close(); store.close(); cleanup(dir) }
})

test('模板消费程序失败时，编译通过也不能下载 SDK', async () => {
  const dir = temporary(), provider = new NativeCTemplate(lighting), registry = new TemplateRegistry()
  registry.register({ descriptor: provider.descriptor, validate: d => provider.validate(d), async generate(d) {
    const files = await provider.generate(d)
    files.set('checks/verify.c', Buffer.from('#include <stdio.h>\nint main(void) { puts("deliberate consumer failure"); return 7; }\n'))
    return files
  } })
  const store = new ProductStore(join(dir, 'studio.db')), builds = new SdkBuildService(join(dir, 'jobs'), registry)
  try {
    const product = store.create(definitionFromTemplate(provider.descriptor))
    const job = await finished(builds, (await builds.create(product)).id)
    assert.equal(job.status, 'failed')
    assert.deepEqual(job.checks, { compile: 'passed', consumer: 'failed' })
    assert.match(job.diagnostics, /deliberate consumer failure/)
    assert.throws(() => builds.artifact(job.id), /不能下载/)
  } finally { await builds.close(); store.close(); cleanup(dir) }
})

test('通用模板拒绝非法构建路径、重复宏和未知依赖', () => {
  const dir = temporary(), fixture = join(dir, 'template')
  cpSync(lighting, fixture, { recursive: true })
  const manifest = JSON.parse(readFileSync(join(fixture, 'template.json'), 'utf8'))
  const invalid = (change: (value: typeof manifest) => void, expected: RegExp) => {
    const changed = structuredClone(manifest)
    change(changed)
    writeFileSync(join(fixture, 'template.json'), JSON.stringify(changed))
    assert.throws(() => new NativeCTemplate(fixture), expected)
  }
  try {
    invalid(m => m.verification.consumer = '../escape.c', /路径不安全/)
    invalid(m => m.previewFiles = ['missing.h'], /预览文件不存在/)
    invalid(m => m.parameters[0].binding = m.features[0].defaultBinding, /重复绑定/)
    invalid(m => m.parameters[0].requires = ['compressor'], /未知功能/)
  } finally { cleanup(dir) }
})
