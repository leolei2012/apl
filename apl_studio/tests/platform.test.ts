import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync, writeFileSync, existsSync, mkdirSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { unzipSync, strFromU8 } from 'fflate'
import { exampleDefinition, parseDefinition, interfaceHeader, validateDefinition, validateValue, type Definition } from '@apl-studio/model'
import { ProductStore } from '@apl-studio/product'
import { TemplateRegistry } from '@apl-studio/templates'
import { definitionPackage } from '@apl-studio/definition'
import { createApi } from '@apl-studio/api'
import { Context } from 'cordis'
import * as productPlugin from '@apl-studio/product'
import * as templatesPlugin from '@apl-studio/templates'
import * as apiPlugin from '@apl-studio/api'

function example() { return exampleDefinition(randomUUID) }
function temporary() { return mkdtempSync(join(tmpdir(), 'apl-studio-test-')) }
function cleanup(dir: string) {
  assert.equal(resolve(dirname(dir)), resolve(tmpdir()))
  assert.ok(basename(dir).startsWith('apl-studio-test-'))
  rmSync(dir, { recursive: true, force: true })
}

test('JSON 产品定义完整往返并拒绝未知格式', () => {
  const definition = example()
  assert.deepEqual(parseDefinition(JSON.parse(JSON.stringify(definition))), definition)
  assert.throws(() => parseDefinition(null))
  assert.throws(() => parseDefinition({ ...definition, schemaVersion: 2 }))
})

test('功能约束和公共命名规则覆盖四种数据类型', () => {
  const d = example()
  assert.equal(validateDefinition(d).length, 0)
  const copy = (change: (definition: Definition) => void) => { const changed = structuredClone(d); change(changed); return validateDefinition(changed) }
  assert.ok(copy(x => x.code = '../escape').length)
  assert.ok(copy(x => x.functions[1].key = x.functions[0].key).length)
  assert.ok(copy(x => x.functions[0].key = 'while').length)
  assert.ok(copy(x => x.functions[1].options[1].value = x.functions[1].options[0].value).length)
  assert.ok(copy(x => x.functions[3].default = 101).length)
  assert.ok(copy(x => x.functions[3].step = 0).length)
  assert.ok(copy(x => x.functions[0].default = 1).length)
  assert.ok(validateDefinition({ ...d, schemaVersion: 99 }).length)
  assert.ok(validateDefinition({ ...d, mystery: true }).length)
  assert.equal(validateValue(d.functions[3], 42), null)
  assert.ok(validateValue(d.functions[3], 4.5))
  const stringField = { ...d.functions[0], kind: 'string' as const, default: '', maxLength: 4 }
  assert.equal(validateValue(stringField, 'abcd'), null)
  assert.ok(validateValue(stringField, 'abcde'))
  assert.ok(validateValue(stringField, 'a\0b'))
})

test('SQLite 保存、重启恢复、版本冲突和删除隔离', () => {
  const dir = temporary(), path = join(dir, 'studio.db')
  let store = new ProductStore(path)
  try {
    const product = store.create(example())
    const changed = { ...product.definition, name: '新产品名称' }
    const saved = store.update(product.id, 1, changed)
    assert.equal(saved.revision, 2)
    assert.throws(() => store.update(product.id, 1, product.definition), { code: 'REVISION_CONFLICT' })
    assert.throws(() => store.remove(product.id, 1), { code: 'REVISION_CONFLICT' })
    assert.equal(store.get(product.id).definition.name, '新产品名称')
    assert.throws(() => store.create(example()), { code: 'DUPLICATE_CODE' })
    assert.equal(store.revisions(product.id).length, 2)
    store.close(); store = new ProductStore(path)
    assert.deepEqual(store.get(product.id), saved)
    const other = store.create({ ...example(), code: 'other' })
    store.remove(product.id, 2)
    assert.throws(() => store.get(product.id), { code: 'NOT_FOUND' })
    assert.equal(store.get(other.id).revision, 1)
  } finally { store.close(); cleanup(dir) }
})

test('定义包可重新导入，哈希正确，并明确不包含完整 SDK', () => {
  const dir = temporary(), store = new ProductStore(join(dir, 'studio.db'))
  try {
    const product = store.create(example()), pack = definitionPackage(product)
    const files = unzipSync(pack.bytes)
    assert.deepEqual(parseDefinition(JSON.parse(strFromU8(files['apl-definition/product.apl.json']))), product.definition)
    const manifest = JSON.parse(strFromU8(files['apl-definition/manifest.json']))
    assert.equal(manifest.sdkReady, false)
    assert.equal(manifest.revision, 1)
    for (const [path, hash] of Object.entries(manifest.files)) assert.equal(createHash('sha256').update(files['apl-definition/' + path]).digest('hex'), hash)
    assert.ok(strFromU8(files['apl-definition/README.md']).includes('不是完整 APL SDK'))
    assert.deepEqual(definitionPackage(product).bytes, pack.bytes)
    assert.ok(!interfaceHeader(product.definition).includes('set_humidity('))
    assert.ok(interfaceHeader(product.definition).includes('apl_air_care_get_humidity(void)'))
  } finally { store.close(); cleanup(dir) }
})

test('生成的接口契约可被真实 C11 编译器消费', t => {
  const compiler = process.env.CC ?? (process.platform === 'win32' ? 'C:/Qt/Tools/mingw1310_64/bin/gcc.exe' : 'cc')
  if (process.platform === 'win32' && !existsSync(compiler)) { t.skip('未找到本机 C 编译器，可通过 CC 指定'); return }
  const dir = temporary()
  try {
    const definition = example()
    definition.functions.push({ ...definition.functions[0], id: randomUUID(), key: 'label', name: '文本 */ 注释', kind: 'string', default: '' })
    writeFileSync(join(dir, 'contract.h'), interfaceHeader(definition))
    writeFileSync(join(dir, 'consumer.c'), '#include "contract.h"\nvoid consume(void) { bool (*read_power)(void) = apl_air_care_get_power; (void)read_power; }\n')
    const result = spawnSync(compiler, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-finput-charset=UTF-8', '-c', join(dir, 'consumer.c'), '-o', join(dir, 'consumer.o')], { encoding: 'utf8' })
    assert.equal(result.status, 0, result.stderr || result.error?.message)
  } finally { cleanup(dir) }
})

test('HTTP 端到端：创建、校验、更新冲突、导出和模板边界', async () => {
  const dir = temporary(), store = new ProductStore(join(dir, 'studio.db')), app = createApi(store, new TemplateRegistry())
  try {
    const create = await app.inject({ method: 'POST', url: '/api/products', payload: example() })
    assert.equal(create.statusCode, 201, create.body)
    const product = create.json()
    assert.equal((await app.inject({ url: '/api/products' })).json().length, 1)
    assert.equal((await app.inject({ method: 'PUT', url: '/api/products/' + product.id, payload: { revision: 1, definition: { ...product.definition, name: 'HTTP 产品' } } })).statusCode, 200)
    const conflict = await app.inject({ method: 'PUT', url: '/api/products/' + product.id, payload: { revision: 1, definition: product.definition } })
    assert.equal(conflict.statusCode, 409)
    assert.equal(conflict.json().code, 'REVISION_CONFLICT')
    assert.equal((await app.inject({ method: 'POST', url: '/api/products', payload: { ...example(), code: '../bad' } })).statusCode, 400)
    const exported = await app.inject({ url: '/api/products/' + product.id + '/export' })
    assert.equal(exported.statusCode, 200)
    assert.equal(exported.headers['content-type'], 'application/zip')
    assert.equal(JSON.parse(strFromU8(unzipSync(exported.rawPayload)['apl-definition/manifest.json'])).revision, 2)
    const sdk = await app.inject({ method: 'POST', url: '/api/sdk/generate', payload: {} })
    assert.equal(sdk.statusCode, 409)
    assert.equal(sdk.json().code, 'SOURCE_TEMPLATE_REQUIRED')
    assert.deepEqual((await app.inject({ url: '/api/templates' })).json().templates, [])
    assert.equal((await app.inject({ method: 'POST', url: '/api/products', headers: { origin: 'https://other.invalid' }, payload: example() })).statusCode, 403)
    assert.equal((await app.inject({ url: '/api/products', headers: { host: 'other.invalid' } })).statusCode, 403)
  } finally { await app.close(); store.close(); cleanup(dir) }
})

test('Cordis 实际启动、网页托管、WebSocket 通知与资源退出', async () => {
  const dir = temporary(), ctx = new Context()
  let socket: WebSocket | undefined
  try {
    const web = join(dir, 'web')
    mkdirSync(web)
    writeFileSync(join(web, 'index.html'), '<!doctype html><title>APL Studio fixture</title>')
    await ctx.plugin(productPlugin, { dbPath: join(dir, 'studio.db') })
    await ctx.plugin(templatesPlugin)
    await ctx.plugin(apiPlugin, { host: '127.0.0.1', port: 0, staticDir: web })
    const address = await ctx.api.listen({ host: '127.0.0.1', port: 0 })
    assert.match(await (await fetch(address)).text(), /APL Studio fixture/)
    assert.equal((await fetch(address + '/api/missing')).status, 404)
    socket = new WebSocket(address.replace('http:', 'ws:') + '/ws')
    await new Promise<void>((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('WebSocket 连接超时')), 5000)
      socket!.onopen = () => { clearTimeout(timeout); resolve() }
      socket!.onerror = () => { clearTimeout(timeout); reject(new Error('WebSocket 连接失败')) }
    })
    const notified = new Promise<{ id: string; revision: number }>((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('缺少产品变更通知')), 5000)
      socket!.onmessage = event => {
        const body = JSON.parse(String(event.data))
        if (body.type === 'product/changed') { clearTimeout(timeout); resolve(body) }
      }
    })
    const [response, event] = await Promise.all([
      fetch(address + '/api/products', { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(example()) }), notified,
    ])
    assert.equal(response.status, 201)
    const created = await response.json() as { id: string }
    assert.equal(event.id, created.id)
    assert.equal(event.revision, 1)
  } finally {
    socket?.close()
    if (ctx.api) await ctx.api.close()
    await ctx.fiber.dispose()
    cleanup(dir)
  }
})
