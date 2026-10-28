import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { resolve, join, dirname, basename } from 'node:path'
import { randomUUID } from 'node:crypto'
import { type Product, emptyDefinition } from '@apl-studio/model'
import { definitionFromTemplate, toggleTemplateFeature } from '@apl-studio/model/template'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { TemplateRegistry } from '@apl-studio/templates'
import { SimulationService } from '@apl-studio/build'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { api as webApi } from '../apps/web/src/api'

const catalog = resolve(import.meta.dirname, '../../apl_sdk_template')
const temporary = () => mkdtempSync(join(tmpdir(), 'apl-simulation-test-'))
function cleanup(dir: string) {
  assert.equal(resolve(dirname(dir)), resolve(tmpdir()))
  assert.ok(basename(dir).startsWith('apl-simulation-test-'))
  rmSync(dir, { recursive: true, force: true })
}
function registry() { const r = new TemplateRegistry(); r.loadDirectory(catalog); return r }
function product(registry: TemplateRegistry, id = 'apl.air-care', version = '0.1.0'): Product {
  return { id: randomUUID(), revision: 1, createdAt: new Date().toISOString(), updatedAt: new Date().toISOString(), definition: definitionFromTemplate(registry.get(id, version)!.descriptor) }
}
async function ready(service: SimulationService, started: SimulationSession) {
  const deadline = Date.now() + 30000
  while (Date.now() < deadline) {
    const s = service.get(started.id)
    if (s.status !== 'building') { assert.equal(s.status, 'ready', s.diagnostics); return s }
    await new Promise(resolve => setTimeout(resolve, 30))
  }
  assert.fail('仿真启动超时')
}

test('灯控实际 C 运行：延迟边界、输出回调、请求/实际状态、掉电、重置及实例隔离', async () => {
  const dir = temporary(), r = registry(), service = new SimulationService(dir, r)
  try {
    const p = product(r, 'standard.lighting', '1.0.0')
    p.definition.parameters[0].value = 120
    p.definition.functions.find(f => f.key === 'brightness')!.default = 73
    let s = await ready(service, await service.create(p, false))
    const id = s.id, other = await ready(service, await service.create(p, false))
    p.definition.parameters[0].value = 0 // Active sessions keep their generated snapshot.
    s = await service.action(id, { type: 'input', key: 'power', value: true })
    assert.equal(s.snapshot!.internal.power, 1)
    assert.equal(s.snapshot!.outputs.light, 0)
    s = await service.action(id, { type: 'advance', ms: 110 })
    assert.equal(s.snapshot!.outputs.light, 0)
    s = await service.action(id, { type: 'advance', ms: 10 })
    assert.equal(s.snapshot!.outputs.light, 1)
    assert.equal(s.snapshot!.outputs.brightness, 73)
    assert.ok(s.events.some(e => e.kind === 'output' && e.message === '灯光输出 → 开' && e.ms === 120))
    assert.equal(service.get(other.id).snapshot!.outputs.light, 0)
    await service.action(id, { type: 'input', key: 'brightness', value: 22 })
    s = await service.action(id, { type: 'supply', on: false })
    assert.equal(s.snapshot!.outputs.light, 0)
    await assert.rejects(service.action(id, { type: 'input', key: 'power', value: true }), /掉电/)
    s = await service.action(id, { type: 'supply', on: true })
    assert.equal(s.snapshot!.values.brightness, 73)
    assert.equal(s.snapshot!.outputs.light, 0)
    s = await service.action(id, { type: 'reset' })
    assert.equal(s.snapshot!.ms, 0)
    assert.equal(s.snapshot!.values.brightness, 73)
    service.stop(id)
    await assert.rejects(service.action(id, { type: 'advance', ms: 10 }), /停止/)
    assert.equal(service.get(other.id).status, 'ready')
  } finally { await service.close(); cleanup(dir) }
})

test('AP 实际面板：三种组合、短按/长按、灯光事件、风干、缺水、掉电存储恢复', async () => {
  const dir = temporary(), r = registry(), service = new SimulationService(dir, r)
  try {
    for (const disabled of [null, 'purify', 'humidify']) {
      const p = product(r)
      if (disabled) p.definition = toggleTemplateFeature(p.definition, r.get('apl.air-care', '0.1.0')!.descriptor, disabled, false)
      let s = await ready(service, await service.create(p, false)), id = s.id
      assert.equal(s.snapshot!.display.digits, '888')
      assert.equal(s.snapshot!.outputs.fan, 0)
      if (disabled) assert.ok(!s.descriptor.controls.some(c => c.key === (disabled === 'purify' ? 'Purify' : 'Humidity')))
      await service.action(id, { type: 'advance', ms: 2100 })
      s = await service.action(id, { type: 'input', key: 'Power', value: 'short' })
      assert.equal(s.snapshot!.internal.run_state, 1)
      assert.equal(s.snapshot!.outputs.fan, 1)
      assert.ok(s.events.some(e => e.kind === 'input' && e.message === 'Power短按'))
      const beforeLight = s.events.at(-1)!.seq
      s = await service.action(id, { type: 'input', key: 'Light', value: 'short' })
      const lightInput = s.events.find(e => e.seq > beforeLight && e.message === 'Light短按')!
      const lightState = s.events.find(e => e.seq > beforeLight && e.message.startsWith('面板灯光 →'))!
      assert.ok(lightInput)
      assert.ok(lightState)
      assert.ok(lightInput.seq < lightState.seq)
      s = await service.action(id, { type: 'input', key: 'Speed', value: 'short' })
      const selectedGear = s.snapshot!.internal.fan_speed
      s = await service.action(id, { type: 'supply', on: false })
      assert.equal(s.snapshot!.outputs.fan, 0)
      assert.equal(s.snapshot!.memory.valid, 1)
      assert.equal(s.snapshot!.memory.running, 1)
      s = await service.action(id, { type: 'supply', on: true })
      assert.equal(s.snapshot!.internal.run_state, 1)
      s = await service.action(id, { type: 'advance', ms: 2100 })
      assert.equal(s.snapshot!.internal.fan_speed, selectedGear)
      assert.equal(s.snapshot!.outputs.fan, 1)
      if (disabled !== 'humidify') {
        await service.action(id, { type: 'input', key: 'water_level', value: 0 })
        s = await service.action(id, { type: 'advance', ms: 1000 })
        assert.equal(s.snapshot!.outputs.pump, 0)
      }
      s = await service.action(id, { type: 'input', key: 'Power', value: 'short' })
      assert.equal(s.snapshot!.internal.run_state, disabled === 'humidify' ? 0 : 2)
      if (disabled !== 'humidify') {
        s = await service.action(id, { type: 'input', key: 'Power', value: 'long' })
        assert.ok(s.events.some(e => e.message === 'Power长按'))
        assert.equal(s.snapshot!.internal.run_state, 0)
      }
      s = await service.action(id, { type: 'reset' })
      assert.equal(s.snapshot!.memory.valid, 0)
      assert.equal(s.snapshot!.internal.run_state, 0)
      assert.equal(s.snapshot!.display.digits, '888')
      service.stop(id)
    }
  } finally { await service.close(); cleanup(dir) }
})

test('仿真 HTTP：修订门禁、参数检查、自动运行/暂停、会话停止与未支持模板', async () => {
  const dir = temporary(), r = registry(), store = new ProductStore(join(dir, 'studio.db')), service = new SimulationService(join(dir, 'simulation'), r)
  const app = createApi(store, r, undefined, undefined, service)
  try {
    const p = store.create(product(r, 'standard.lighting', '1.0.0').definition)
    const create = (revision: number) => app.inject({ method: 'POST', url: '/api/simulation/sessions', payload: { productId: p.id, revision } })
    assert.equal((await create(2)).statusCode, 409)
    const start = await create(1)
    assert.equal(start.statusCode, 202, start.body)
    const s = await ready(service, start.json())
    await new Promise(resolve => setTimeout(resolve, 250))
    const send = (payload: unknown) => app.inject({ method: 'POST', url: `/api/simulation/sessions/${s.id}/actions`, payload: JSON.stringify(payload), headers: { 'content-type': 'application/json' } })
    const paused = await send({ type: 'running', value: false })
    assert.ok(paused.json().snapshot.ms >= 200)
    const ms = paused.json().snapshot.ms
    await new Promise(resolve => setTimeout(resolve, 150))
    assert.equal(service.get(s.id).snapshot!.ms, ms)
    for (const input of [{ type: 'input', key: 'brightness', value: 101 }, { type: 'input', key: 'brightness', value: '3' }, { type: 'input', key: '__proto__', value: 0 }, { type: 'input', key: 'power', value: 1 }, { type: 'advance', ms: 1000000 }, { type: 'advance', ms: 1 }, { type: 'supply', on: 'yes' }, null]) assert.equal((await send(input)).statusCode, 400)
    assert.equal((await app.inject({ method: 'DELETE', url: `/api/simulation/sessions/${s.id}` })).statusCode, 204)
    assert.equal((await send({ type: 'advance', ms: 10 })).statusCode, 409)
    const blank = store.create({ ...emptyDefinition(), code: 'blank' })
    assert.equal((await app.inject('/api/products/' + blank.id + '/simulation')).json().descriptor, null)
    assert.equal((await app.inject({ method: 'POST', url: '/api/simulation/sessions', payload: { productId: blank.id, revision: 1 } })).statusCode, 409)
    assert.equal((await app.inject('/api/system')).json().capabilities.aplExecution, true)
  } finally { await service.close(); await app.close(); store.close(); cleanup(dir) }
})

test('编译失败显示诊断，停止后的会话不能继续操作', async () => {
  const dir = temporary(), r = registry(), original = r.get('standard.lighting', '1.0.0')!, broken = new TemplateRegistry()
  broken.register({ descriptor: original.descriptor, validate: d => original.validate(d), async generate(d) { const files = await original.generate(d); files.set('lib/broken.c', Buffer.from('#error simulation_compile_failure\n')); return files } })
  const service = new SimulationService(dir, broken)
  try {
    const started = await service.create(product(r, 'standard.lighting', '1.0.0'), false)
    const deadline = Date.now() + 30000
    let s = service.get(started.id)
    while (s.status === 'building' && Date.now() < deadline) { await new Promise(resolve => setTimeout(resolve, 30)); s = service.get(started.id) }
    assert.equal(s.status, 'failed')
    assert.match(s.diagnostics, /simulation_compile_failure/)
    assert.equal(s.snapshot, null)
    await assert.rejects(service.action(s.id, { type: 'advance', ms: 10 }), /停止/)
  } finally { await service.close(); cleanup(dir) }
})

test('网页客户端可通过真实 HTTP 停止会话，空请求不会被 JSON 解析器拒绝', async () => {
  const dir = temporary(), r = registry(), store = new ProductStore(join(dir, 'studio.db')), service = new SimulationService(join(dir, 'simulation'), r)
  const app = createApi(store, r, undefined, undefined, service), originalFetch = globalThis.fetch
  try {
    const origin = await app.listen({ host: '127.0.0.1', port: 0 })
    // Supply the origin that browsers resolve automatically, retaining the actual client headers/body.
    globalThis.fetch = (input, init) => originalFetch(new URL(String(input), origin), init)
    const p = store.create(product(r, 'standard.lighting', '1.0.0').definition)
    const started = await ready(service, await webApi.startSimulation(p))
    await webApi.stopSimulation(started.id)
    assert.equal(service.get(started.id).status, 'stopped')
    await webApi.stopSimulation(started.id) // Unmount after explicit stop is harmless.
  } finally { globalThis.fetch = originalFetch; await service.close(); await app.close(); store.close(); cleanup(dir) }
})
