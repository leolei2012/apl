import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { resolve, join, dirname, basename } from 'node:path'
import { definitionFromTemplate, toggleTemplateFeature } from '@apl-studio/model/template'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { TemplateRegistry } from '@apl-studio/templates'
import { SimulationService } from '@apl-studio/build'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { api as webApi } from '../apps/web/src/api'

function fixture() {
  const directory = mkdtempSync(join(tmpdir(), 'apl-combo-test-')), registry = new TemplateRegistry()
  registry.loadDirectory(resolve(import.meta.dirname, '../../apl_sdk_template'))
  const store = new ProductStore(join(directory, 'studio.db'))
  const service = new SimulationService(join(directory, 'simulation'), registry)
  const app = createApi(store, registry, undefined, undefined, service)
  return { registry, store, service, app, async close() {
    await service.close(); await app.close(); store.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir()))
    assert.ok(basename(directory).startsWith('apl-combo-test-'))
    rmSync(directory, { recursive: true, force: true })
  } }
}
async function ready(service: SimulationService, started: SimulationSession) {
  const deadline = Date.now() + 30000
  while (Date.now() < deadline) {
    const current = service.get(started.id)
    if (current.status !== 'building') { assert.equal(current.status, 'ready', current.diagnostics); return current }
    await new Promise(resolve => setTimeout(resolve, 30))
  }
  assert.fail('组合键 C 仿真启动超时')
}
function createProduct(f: ReturnType<typeof fixture>, disabled?: 'purify' | 'humidify') {
  const template = f.registry.get('apl.air-care', '0.1.0')!.descriptor
  let definition = definitionFromTemplate(template, f.store.list().map(p => p.definition.code))
  if (disabled) definition = toggleTemplateFeature(definition, template, disabled, false)
  return f.store.create(definition)
}

test('Web 客户端经真实 HTTP 驱动三种 AP 的 C 组合键：顺序无关、一次触发及释放后单键', async () => {
  const f = fixture(), originalFetch = globalThis.fetch
  try {
    const origin = await f.app.listen({ host: '127.0.0.1', port: 0 })
    globalThis.fetch = (input, init) => originalFetch(new URL(String(input), origin), init)
    for (const disabled of [undefined, 'purify', 'humidify'] as const) {
      const p = createProduct(f, disabled)
      let s = await ready(f.service, await webApi.startSimulation(p)), id = s.id
      await webApi.simulate(id, { type: 'running', value: false })
      s = await webApi.simulate(id, { type: 'advance', ms: 2100 })
      const ms = s.snapshot!.ms
      s = await webApi.simulate(id, { type: 'combo', keys: ['Timer', 'Power'], holdMs: 3000 })
      assert.equal(s.snapshot!.internal.run_state, 3)
      assert.equal(s.snapshot!.ms - ms, 3020)
      assert.equal(s.snapshot!.display.child_lock, 0)
      assert.ok(s.events.some(e => e.kind === 'input' && e.message.includes('组合输入') && e.message.includes('进入产测')))
      assert.ok(!s.events.some(e => e.message === 'Timer长按' || e.message === 'Power长按'))
      // C consumes both keys: release cannot emit a power short press that immediately exits factory mode.
      s = await webApi.simulate(id, { type: 'input', key: 'Power', value: 'short' })
      assert.equal(s.snapshot!.internal.run_state, 0)
      const beforeReset = Number(s.snapshot!.internal.filter_reset_seq)
      s = await webApi.simulate(id, { type: 'combo', keys: ['Speed', 'Timer'], holdMs: 6000 })
      assert.equal(s.snapshot!.internal.filter_reset_seq, beforeReset + 1)
      assert.equal(s.snapshot!.display.child_lock, 0)
      assert.equal(s.snapshot!.internal.run_state, 0)
      s = await webApi.simulate(id, { type: 'advance', ms: 1000 })
      assert.equal(s.snapshot!.internal.filter_reset_seq, beforeReset + 1)
      s = await webApi.simulate(id, { type: 'combo', keys: ['Timer', 'Speed'], holdMs: 3000 })
      assert.equal(s.snapshot!.internal.filter_reset_seq, beforeReset + 2)
      await webApi.stopSimulation(id)
    }
  } finally { globalThis.fetch = originalFetch; await f.close() }
})

test('组合键保持 2990/3000 ms 边界及产测时间窗口由真实 C 判断', async () => {
  const f = fixture()
  try {
    const s = await ready(f.service, await f.service.create(createProduct(f), false)), id = s.id
    await f.service.action(id, { type: 'advance', ms: 2100 })
    let current = await f.service.action(id, { type: 'combo', keys: ['Power', 'Timer'], holdMs: 2990 })
    assert.notEqual(current.snapshot!.internal.run_state, 3)
    assert.equal(current.snapshot!.internal.run_state, 1) // Early release falls back to the existing C short-key rules.
    await f.service.action(id, { type: 'reset' })
    await f.service.action(id, { type: 'advance', ms: 2100 })
    current = await f.service.action(id, { type: 'combo', keys: ['Power', 'Timer'], holdMs: 3000 })
    assert.equal(current.snapshot!.internal.run_state, 3)
    await f.service.action(id, { type: 'reset' })
    await f.service.action(id, { type: 'advance', ms: 31000 })
    current = await f.service.action(id, { type: 'combo', keys: ['Timer', 'Power'], holdMs: 3000 })
    assert.equal(current.snapshot!.internal.run_state, 0) // The bridge cannot bypass the core's factory window.
  } finally { await f.close() }
})

test('组合键 HTTP 拒绝重复键、未声明组合、非法时长和掉电操作，拒绝时不推进 C 时钟', async () => {
  const f = fixture()
  try {
    const s = await ready(f.service, await f.service.create(createProduct(f), false)), id = s.id
    const send = (payload: unknown) => f.app.inject({ method: 'POST', url: `/api/simulation/sessions/${id}/actions`, payload: JSON.stringify(payload), headers: { 'content-type': 'application/json' } })
    const ms = s.snapshot!.ms
    for (const payload of [
      { type: 'combo', keys: ['Timer', 'Timer'], holdMs: 3000 },
      { type: 'combo', keys: ['Power', 'Speed'], holdMs: 3000 },
      { type: 'combo', keys: ['Power', 'Timer', 'Speed'], holdMs: 3000 },
      { type: 'combo', keys: ['Power', '__proto__'], holdMs: 3000 },
      { type: 'combo', keys: ['Power', 'Timer'], holdMs: 0 },
      { type: 'combo', keys: ['Power', 'Timer'], holdMs: 3001 },
      { type: 'combo', keys: ['Power', 'Timer'], holdMs: 60010 },
      { type: 'combo', keys: ['Power', 'Timer'], holdMs: '3000' },
      { type: 'combo', keys: 'Power,Timer', holdMs: 3000 },
      { type: 'combo', keys: null, holdMs: 3000 },
    ]) {
      const response = await send(payload)
      assert.equal(response.statusCode, 400, response.body)
      assert.equal(response.json().code, 'INVALID_SIMULATION_INPUT')
      assert.equal(f.service.get(id).snapshot!.ms, ms)
    }
    await f.service.action(id, { type: 'supply', on: false })
    assert.equal((await send({ type: 'combo', keys: ['Power', 'Timer'], holdMs: 3000 })).statusCode, 409)
    assert.equal(f.service.get(id).snapshot!.ms, ms)
  } finally { await f.close() }
})

test('会话停止会取消排队组合，重新读取或新建会话没有残留按下状态', async () => {
  const f = fixture()
  try {
    const product = createProduct(f)
    let current = await ready(f.service, await f.service.create(product, false))
    await f.service.action(current.id, { type: 'advance', ms: 2100 })
    const pending = f.service.action(current.id, { type: 'combo', keys: ['Power', 'Timer'], holdMs: 3000 })
    f.service.stop(current.id)
    await assert.rejects(pending, /停止/)
    current = await ready(f.service, await f.service.create(product, false))
    await f.service.action(current.id, { type: 'advance', ms: 2100 })
    await f.service.action(current.id, { type: 'combo', keys: ['Timer', 'Speed'], holdMs: 3000 })
    const reread = f.service.get(current.id)
    assert.equal(reread.snapshot!.internal.filter_reset_seq, 1)
    current = await f.service.action(current.id, { type: 'advance', ms: 5000 })
    assert.equal(current.snapshot!.internal.run_state, 0)
    assert.equal(current.snapshot!.internal.filter_reset_seq, 1)
    current = await f.service.action(current.id, { type: 'input', key: 'Power', value: 'short' })
    assert.equal(current.snapshot!.internal.run_state, 1)
  } finally { await f.close() }
})
