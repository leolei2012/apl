import { test } from 'node:test'
import assert from 'node:assert/strict'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import type { Product } from '@apl-studio/model'
import { definitionFromTemplate } from '@apl-studio/model/template'
import { TemplateRegistry } from '@apl-studio/templates'
import { SimulationService } from '@apl-studio/build'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { AirCarePanel } from '../apps/web/src/AirCarePanel'

test('圆形面板使用实际C显示快照：上电自检、待机、运行、童锁、熄屏与掉电', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'apl-face-test-')), registry = new TemplateRegistry()
  registry.loadDirectory(resolve(import.meta.dirname, '../../apl_sdk_template'))
  const template = registry.get('apl.air-care', '0.4.2')!.descriptor
  const product: Product = { id: 'round-panel-test', revision: 1, createdAt: '', updatedAt: '', definition: definitionFromTemplate(template) }
  const service = new SimulationService(dir, registry)
  const html = (s: SimulationSession) => renderToStaticMarkup(createElement(AirCarePanel, { snapshot: s.snapshot, controls: s.descriptor.controls, disabled: false, async onGesture() {} }))
  try {
    let s = await service.create(product, false)
    const deadline = Date.now() + 30000
    while (s.status === 'building' && Date.now() < deadline) { await new Promise(resolve => setTimeout(resolve, 30)); s = service.get(s.id) }
    assert.equal(s.status, 'ready', s.diagnostics)
    assert.equal(s.snapshot!.display.digits, '888')
    assert.match(html(s), /aria-label="数码管 888/)
    s = await service.action(s.id, { type: 'advance', ms: 2100 })
    assert.equal(s.snapshot!.internal.run_state, 0)
    assert.match(html(s), /aria-label="数码管已熄灭"/)
    s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
    assert.equal(s.snapshot!.internal.run_state, 1)
    assert.ok(Number(s.snapshot!.display.brightness) > 0)
    assert.ok(html(s).includes(`data-value="${String(s.snapshot!.display.digits).replaceAll('\0', ' ')}"`))
    s = await service.action(s.id, { type: 'input', key: 'Timer', value: 'long' })
    assert.equal(s.snapshot!.display.child_lock, 1)
    assert.match(html(s), /aria-label="童锁灯亮"/)
    const gear = s.snapshot!.internal.fan_speed
    s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'short' })
    assert.equal(s.snapshot!.internal.fan_speed, gear)
    s = await service.action(s.id, { type: 'input', key: 'Timer', value: 'long' })
    assert.equal(s.snapshot!.display.child_lock, 0)
    for (let i = 0; i < 6 && s.snapshot!.internal.fan_speed !== 0; i++) s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'short' })
    assert.equal(s.snapshot!.internal.fan_speed, 0)
    s = await service.action(s.id, { type: 'advance', ms: 5100 })
    assert.equal(s.snapshot!.display.brightness, 0)
    assert.match(html(s), /aria-label="数码管已熄灭"/)
    assert.match(html(s), /aria-label="睡眠灯亮"/)
    s = await service.action(s.id, { type: 'supply', on: false })
    const off = html(s)
    assert.match(off, /aria-label="数码管已熄灭"/)
    assert.match(off, /aria-label="童锁灯灭"/)
    assert.match(off, /disabled="" aria-label="电源短按"/)
    const idle = renderToStaticMarkup(createElement(AirCarePanel, { snapshot: null, controls: [], disabled: true, async onGesture() {} }))
    assert.match(idle, /点击“开始体验”，点亮你的产品/)
    assert.doesNotMatch(idle, /数码管 888/)
  } finally {
    await service.close()
    assert.equal(resolve(dirname(dir)), resolve(tmpdir()))
    rmSync(dir, { recursive: true, force: true })
  }
})
