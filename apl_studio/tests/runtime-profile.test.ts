import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { resolve, join } from 'node:path'
import { unzipSync, strFromU8 } from 'fflate'
import { RuntimeProfileProvider, TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { SimulationService } from '@apl-studio/build'
import { bindCurrentProfile, featureSummary, newProfile, productFeatures, profilePid, setProductFeatures, setProfile, validateProfile } from '@apl-studio/model/profile'
import { createApi } from '@apl-studio/api'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { AirCarePanel, PanelDigits } from '../apps/web/src/AirCarePanel'

test('产品 PID 是独立的可选文本，校验、旧产品补默认和生成 C 初始化一致', async () => {
  const provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  const d = newProfile(provider.catalog, 'PID 配置')
  assert.equal(profilePid(d), '')
  for (const pid of ['', 'aaerrxvja3m7lvhz', 'Ab09', '000123']) {
    const configured = setProfile(d, 'pid', pid)
    assert.deepEqual(validateProfile(configured, provider.catalog), [])
    const files = await provider.generate(configured)
    const header = Buffer.from(files.get('include/apl_product.h')!).toString()
    assert.ok(header.includes(`memcpy(p.pid, ${JSON.stringify(pid)}, ${pid.length + 1}u);`))
  }
  for (const invalid of ['a'.repeat(17), 'bad-id', '空净', 'a b', 'abc\n', 'abc\0', '"', 123])
    assert.ok(validateProfile(setProfile(d, 'pid', invalid), provider.catalog).some(s => s.includes('产品 PID')))
  const old = structuredClone(d)
  old.parameters = old.parameters.filter(p => p.key !== 'pid')
  const upgraded = bindCurrentProfile(old, provider.catalog)
  assert.equal(profilePid(upgraded), '')
  assert.deepEqual(validateProfile(upgraded, provider.catalog), [])
  assert.equal(profilePid(bindCurrentProfile(setProfile(d, 'pid', 'Ab09'), provider.catalog)), 'Ab09')
})

test('产品功能按两个开关组合，切换功能保留其他已选传感器和运行参数', () => {
  const provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  let d = newProfile(provider.catalog, '功能组合', { purify: true, humidify: false })
  assert.deepEqual(productFeatures(d), { purify: true, humidify: false })
  assert.equal(featureSummary(d), '净化')
  const legacy = structuredClone(d)
  legacy.parameters = legacy.parameters.filter(p => !['display_rotate_s', 'timer_step_h'].includes(p.key) && !p.key.endsWith('_rpm'))
  legacy.parameters.find(p => p.key === 'version')!.value = 1
  legacy.parameters.find(p => p.key === 'source_fingerprint')!.value = 'previous-source'
  const rebound = bindCurrentProfile(legacy, provider.catalog)
  assert.deepEqual(validateProfile(rebound, provider.catalog), [])
  assert.equal(rebound.parameters.find(p => p.key === 'display_rotate_s')!.value, 20)
  assert.equal(rebound.parameters.find(p => p.key === 'timer_step_h')!.value, 1)
  assert.equal(rebound.parameters.find(p => p.key === 'version')!.value, provider.catalog.schema.schemaVersion)
  assert.deepEqual(productFeatures(rebound), productFeatures(legacy))
  for (const field of legacy.parameters.filter(p => !['version', 'source_fingerprint'].includes(p.key)))
    assert.deepEqual(rebound.parameters.find(p => p.key === field.key), field)
  const previousV2 = setProfile(setProfile(d, 'version', 2), 'display_rotate_s', 7)
  previousV2.parameters = previousV2.parameters.filter(p => p.key !== 'timer_step_h' && !p.key.endsWith('_rpm'))
  const upgradedV2 = bindCurrentProfile(previousV2, provider.catalog)
  assert.deepEqual(validateProfile(upgradedV2, provider.catalog), [])
  assert.equal(upgradedV2.parameters.find(p => p.key === 'display_rotate_s')!.value, 7)
  assert.equal(upgradedV2.parameters.find(p => p.key === 'timer_step_h')!.value, 1)
  const rpmFields = ['fan_sleep_rpm', 'fan_gear1_rpm', 'fan_gear2_rpm', 'fan_gear3_rpm', 'fan_turbo_rpm']
  const previousV3 = setProfile(setProfile(setProfile(d, 'version', 3), 'display_rotate_s', 7), 'timer_step_h', 2)
  previousV3.parameters = previousV3.parameters.filter(p => !rpmFields.includes(p.key))
  const upgradedV3 = bindCurrentProfile(previousV3, provider.catalog)
  assert.deepEqual(validateProfile(upgradedV3, provider.catalog), [])
  for (const [index, key] of rpmFields.entries()) assert.equal(upgradedV3.parameters.find(p => p.key === key)!.value, [400, 600, 900, 1200, 1500][index])
  const previousV4 = setProfile(setProfile(d, 'fan_sleep_rpm', 0), 'fan_gear1_rpm', 777)
  const reboundV4 = bindCurrentProfile(previousV4, provider.catalog)
  assert.equal(reboundV4.parameters.find(p => p.key === 'fan_sleep_rpm')!.value, 0, 'explicit stop must survive changed defaults')
  assert.equal(reboundV4.parameters.find(p => p.key === 'fan_gear1_rpm')!.value, 777, 'existing product RPM must survive changed defaults')
  for (const [index, key] of rpmFields.entries()) assert.equal(d.parameters.find(p => p.key === key)!.value, [400, 600, 900, 1200, 1500][index])
  assert.equal(upgradedV3.parameters.find(p => p.key === 'display_rotate_s')!.value, 7)
  assert.equal(upgradedV3.parameters.find(p => p.key === 'timer_step_h')!.value, 2)
  d = setProfile(setProfile(d, 'sensors', 2 | 8), 'default_fan_gear', 3)
  d = setProductFeatures(d, { purify: true, humidify: true })
  assert.equal(d.parameters.find(p => p.key === 'sensors')!.value, 2 | 8 | 16)
  assert.equal(d.parameters.find(p => p.key === 'default_fan_gear')!.value, 3)
  assert.equal(featureSummary(d), '净化 + 加湿')
  d = setProductFeatures(d, { purify: false, humidify: true })
  assert.equal(d.parameters.find(p => p.key === 'sensors')!.value, 16)
  assert.deepEqual(validateProfile(d, provider.catalog), [])
  d = setProductFeatures(d, { purify: false, humidify: false })
  assert.equal(featureSummary(d), '未选择功能')
  assert.ok(validateProfile(d, provider.catalog).includes('至少启用净化或加湿'))
})

function horizontalFrame(session: SimulationSession, glyph: string, topY: number) {
  const display = session.snapshot!.display
  assert.equal(display.digits, glyph.repeat(3))
  const html = renderToStaticMarkup(createElement(PanelDigits, { value: String(display.digits), brightness: Number(display.brightness) / 255 }))
  const lit = [...html.matchAll(/<polygon\b[^>]*opacity="1"[^>]*>/g)].map(m => m[0])
  assert.equal(lit.length, 3, 'all three digits must light exactly one horizontal segment')
  for (const segment of lit) assert.ok(segment.includes(`points="8,${topY} `), 'wrong horizontal segment rendered: ' + segment)
}

function allLightsOff(session: SimulationSession) {
  const display = session.snapshot!.display
  assert.equal(display.light, 0)
  assert.equal(display.brightness, 0)
  assert.equal(display.ambient, 0)
  for (const channel of ['red', 'green', 'blue']) assert.equal(display['ambient_' + channel], 0)
  assert.equal(display.unit, '')
  assert.equal(display.water_level, 0, 'display must report illuminated water lamps, not the tank level')
  for (const [key, value] of Object.entries(display)) if (/^(key_|icon_|water_[1-4]$)/.test(key)) assert.equal(value, 0, key)
  for (let i = 1; i <= 4; i++) assert.equal(display['water_' + i], 0)
  const face = renderToStaticMarkup(createElement(AirCarePanel, { snapshot: session.snapshot, controls: session.descriptor.controls, disabled: false, async onGesture() {} }))
  assert.doesNotMatch(face, /class="is-lit"/)
}

function ambientOutput(session: SimulationSession, rgb: number[], purify: boolean) {
  assert.deepEqual(['red', 'green', 'blue'].map(channel => session.snapshot!.display['ambient_' + channel]), rgb)
  const face = renderToStaticMarkup(createElement(AirCarePanel, { features: { purify, humidify: true }, snapshot: session.snapshot, controls: session.descriptor.controls, disabled: false, async onGesture() {} }))
  if (purify) {
    assert.ok(face.includes(`data-rgb="${rgb.join(',')}"`))
    assert.ok(face.includes(`color:rgb(${rgb.join(', ')})`))
    assert.ok(face.includes(`aria-label="氛围灯${rgb.some(v => v > 0) ? '亮' : '灭'}"`))
  } else assert.ok(!face.includes('class="ac-ambient"'))
}

test('当前 APL 按 1ms 推进，业务每 10ms 执行，自检、手势和自动时间保持真实毫秒', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'apl-profile-tick-'))
  const registry = new TemplateRegistry(), provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  registry.register(provider)
  const products = new ProductStore(join(dir, 'studio.db')), service = new SimulationService(join(dir, 'sim'), registry)
  try {
    const product = products.create(newProfile(provider.catalog, '1ms 调度', { purify: true, humidify: false }))
    let s = await service.create(product, false)
    const deadline = Date.now() + 30000
    while (s.status === 'building' && Date.now() < deadline) { await new Promise(r => setTimeout(r, 30)); s = service.get(s.id) }
    assert.equal(s.status, 'ready', s.diagnostics)
    assert.equal(s.descriptor.tickMs, 1)
    s = await service.action(s.id, { type: 'advance', ms: 1 })
    assert.equal(s.snapshot!.ms, 1)
    ambientOutput(s, [0, 0, 0], true)
    s = await service.action(s.id, { type: 'advance', ms: 8 })
    assert.equal(s.snapshot!.ms, 9)
    ambientOutput(s, [0, 0, 0], true)
    s = await service.action(s.id, { type: 'advance', ms: 1 })
    assert.equal(s.snapshot!.ms, 10)
    ambientOutput(s, [255, 255, 255], true)
    s = await service.action(s.id, { type: 'advance', ms: 1989 })
    assert.equal(s.snapshot!.ms, 1999)
    assert.equal(s.snapshot!.display.digits, '888')
    assert.equal(s.snapshot!.display.brightness, 255)
    s = await service.action(s.id, { type: 'advance', ms: 1 })
    assert.equal(s.snapshot!.ms, 2000)
    assert.equal(s.snapshot!.display.brightness, 0)
    assert.equal(s.snapshot!.display.key_5, 128)
    ambientOutput(s, [0, 0, 0], true)
    s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
    assert.equal(s.snapshot!.ms, 2070, 'short press remains 50ms down plus 20ms released')
    assert.equal(s.snapshot!.internal.run_state, 1)
    s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'long' })
    assert.equal(s.snapshot!.ms, 5140, 'long press remains 3050ms down plus 20ms released')
    assert.equal(s.snapshot!.internal.fan_speed, 5)
    assert.ok(s.events.some(e => e.kind === 'input' && e.message === 'Speed长按' && e.ms === 5070))
    const before = s.snapshot!.ms
    s = await service.action(s.id, { type: 'advance', ms: 60000 })
    assert.equal(s.snapshot!.ms, before + 60000, 'one-minute requests allow 60000 native 1ms steps')
    for (const ms of [0, 0.5, 60001]) await assert.rejects(service.action(s.id, { type: 'advance', ms }), /无效/)
    await service.action(s.id, { type: 'running', value: true })
    const autoStart = s.snapshot!.ms, autoDeadline = Date.now() + 2000
    do { await new Promise(r => setTimeout(r, 10)); s = service.get(s.id) } while (s.snapshot!.ms === autoStart && Date.now() < autoDeadline)
    s = await service.action(s.id, { type: 'running', value: false })
    assert.ok(s.snapshot!.ms > autoStart)
    assert.equal((s.snapshot!.ms - autoStart) % 100, 0, 'each realtime pulse advances 100ms, not 10ms')
  } finally { await service.close(); products.close(); rmSync(dir, { recursive: true, force: true }) }
})

test('运行时参数闭环：真实 C 三机型、固定源码、修订保存、Excel 值与来源、过期和非法输入门禁', async () => {
  const dir = mkdtempSync(join(tmpdir(), 'apl-profile-studio-'))
  const registry = new TemplateRegistry(), provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  registry.register(provider)
  const products = new ProductStore(join(dir, 'studio.db')), service = new SimulationService(join(dir, 'sim'), registry)
  const app = createApi(products, registry, undefined, undefined, service)
  const request = async (method: 'POST' | 'GET' | 'PUT', url: string, payload?: unknown) => app.inject({ method, url, payload: payload as never })
  try {
    const catalog = await request('GET', '/api/profile/catalog')
    assert.equal(catalog.statusCode, 200)
    assert.equal(catalog.json().schema.aplVersion, '0.3.0')
    const sourceSets: Map<string, Uint8Array>[] = []
    for (const kind of ['purifier', 'humidifier', 'combined'] as const) {
      let definition = newProfile(provider.catalog, '运行时<' + kind + '>&配置', { purify: kind !== 'humidifier', humidify: kind !== 'purifier' })
      definition = setProfile(definition, 'pid', 'aaerrxvja3m7lvhz')
      definition = setProfile(definition, 'default_fan_gear', 2)
      definition = setProfile(definition, 'dry_s', 2)
      definition = setProfile(definition, 'turbo_enabled', 0)
      definition = setProfile(definition, 'modules', 0)
      definition = setProfile(definition, 'display_rotate_s', 7)
      definition = setProfile(definition, 'timer_step_h', 2)
      for (const [key, value] of [['fan_sleep_rpm', 111], ['fan_gear1_rpm', 401], ['fan_gear2_rpm', 902], ['fan_gear3_rpm', 1303], ['fan_turbo_rpm', 1605]] as const) definition = setProfile(definition, key, value)
      assert.deepEqual(validateProfile(definition, provider.catalog), [])
      const create = await request('POST', '/api/products', definition)
      assert.equal(create.statusCode, 201, create.body)
      const product = create.json()
      assert.equal(profilePid(product.definition), 'aaerrxvja3m7lvhz')
      sourceSets.push(await provider.generate(definition))
      let s = await service.create(product, false)
      const deadline = Date.now() + 30000
      while (s.status === 'building' && Date.now() < deadline) { await new Promise(r => setTimeout(r, 30)); s = service.get(s.id) }
      assert.equal(s.status, 'ready', s.diagnostics)
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 0)
      assert.equal(s.descriptor.outputs.find(o => o.key === 'fan_target_rpm')?.unit, 'rpm')
      const face = renderToStaticMarkup(createElement(AirCarePanel, { features: productFeatures(definition), snapshot: s.snapshot, controls: s.descriptor.controls, disabled: false, async onGesture() {} }))
      assert.equal(face.includes('aria-label="滤芯复位短按"'), kind === 'purifier')
      assert.equal(face.includes('aria-label="加湿短按"'), kind !== 'purifier')
      assert.equal(face.includes('data-unit="PM2.5"'), kind !== 'humidifier')
      assert.equal(face.includes('data-unit="%"'), kind !== 'purifier')
      assert.equal(face.includes('class="ac-water-row"'), kind !== 'purifier')
      assert.equal(face.includes('class="ac-ambient"'), kind !== 'humidifier')
      assert.equal(face.includes('class="ac-uv"'), kind !== 'humidifier')
      assert.equal(s.descriptor.controls.some(c => c.key === 'Filter'), kind === 'purifier')
      s = await service.action(s.id, { type: 'advance', ms: 10 })
      ambientOutput(s, [255, 255, 255], kind !== 'humidifier')
      await service.action(s.id, { type: 'advance', ms: 2100 })
      s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
      ambientOutput(s, [0, 0, 0], kind !== 'humidifier')
      horizontalFrame(s, '^', 2)
      // Match the page's 100ms sampling: never skip top -> middle -> bottom -> top.
      for (let sample = 1; sample <= 9; sample++) {
        s = await service.action(s.id, { type: 'advance', ms: 100 })
        const phase = Math.floor(sample / 3) % 3
        horizontalFrame(s, ['^', '-', '_'][phase], [2, 41, 80][phase])
      }
      s = await service.action(s.id, { type: 'advance', ms: 4000 })
      assert.ok(['^^^', '---', '___'].includes(String(s.snapshot!.display.digits)))
      s = await service.action(s.id, { type: 'advance', ms: 100 })
      assert.equal(s.snapshot!.display.digits, kind === 'humidifier' ? ' 45' : ' 12')
      assert.equal(s.snapshot!.outputs.fan, 1)
      assert.equal(s.snapshot!.outputs.fan_gear, 2)
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 902)
      assert.ok(s.events.some(e => e.kind === 'output' && e.message === '风机目标转速 → 902 rpm'))
      assert.equal(s.snapshot!.outputs.pump, kind === 'purifier' ? 0 : 1)
      ambientOutput(s, kind === 'humidifier' ? [0, 0, 0] : [0, 0, 255], kind !== 'humidifier')
      if (kind === 'combined') {
        s = await service.action(s.id, { type: 'advance', ms: 2000 })
        assert.equal(s.snapshot!.display.unit, '%', 'configured 7s interval must replace the default 20s')
        s = await service.action(s.id, { type: 'advance', ms: 7000 })
        assert.equal(s.snapshot!.display.unit, 'PM2.5')
      }
      if (kind !== 'humidifier') {
        for (const [pm25, rgb] of [[35, [0, 0, 255]], [36, [0, 255, 0]], [75, [0, 255, 0]], [76, [255, 255, 0]], [150, [255, 255, 0]], [151, [255, 0, 0]], [12, [0, 0, 255]]] as const) {
          await service.action(s.id, { type: 'input', key: 'pm25', value: pm25 })
          s = await service.action(s.id, { type: 'advance', ms: 1000 })
          ambientOutput(s, [...rgb], true)
        }
        await service.action(s.id, { type: 'input', key: 'pm25_status', value: 3 })
        s = await service.action(s.id, { type: 'advance', ms: 1000 }); ambientOutput(s, [0, 0, 0], true)
        await service.action(s.id, { type: 'input', key: 'pm25_status', value: 2 })
        s = await service.action(s.id, { type: 'advance', ms: 1000 }); ambientOutput(s, [0, 0, 255], true)
        assert.ok(s.events.some(e => e.kind === 'state' && e.message === '氛围灯红通道 → 255'))
      }
      s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'long' })
      assert.equal(s.snapshot!.internal.fan_speed, 2, 'disabled Turbo must not change gear')
      const outputs = { ...s.snapshot!.outputs }
      s = await service.action(s.id, { type: 'input', key: 'Light', value: 'short' })
      allLightsOff(s)
      assert.equal(s.snapshot!.internal.run_state, 1)
      for (const key of ['fan', 'fan_gear', 'fan_target_rpm', 'pump']) assert.equal(s.snapshot!.outputs[key], outputs[key], key + ' changed when turning lights off')
      assert.equal(s.snapshot!.values.water_level, 3, 'tank level remains unchanged')
      s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'short' })
      assert.equal(s.snapshot!.internal.fan_speed, 2, 'first dark-screen touch only wakes the display')
      assert.equal(s.snapshot!.display.key_5, 255)
      assert.equal(s.snapshot!.display.key_3, 255, 'Light key backlight must wake with the rest of the panel')
      assert.equal(s.snapshot!.display.light, 0, 'temporary wake must preserve the lights-off setting')
      assert.ok(Number(s.snapshot!.display.brightness) > 0)
      assert.equal(s.snapshot!.display.water_level, kind === 'purifier' ? 0 : 3)
      s = await service.action(s.id, { type: 'advance', ms: 3100 })
      allLightsOff(s)
      s = await service.action(s.id, { type: 'input', key: 'Light', value: 'short' })
      assert.equal(s.snapshot!.display.light, 0, 'waking does not toggle the light setting')
      assert.equal(s.snapshot!.display.key_3, 255)
      s = await service.action(s.id, { type: 'input', key: 'Light', value: 'short' })
      assert.equal(s.snapshot!.display.light, 1)
      assert.equal(s.snapshot!.display.key_5, 255)
      // Manual lights-off also hides Sleep; automatic sleep policy is otherwise unchanged.
      for (let i = 0; i < 3; i++) s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'short' })
      assert.equal(s.snapshot!.internal.fan_speed, 0)
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 111)
      s = await service.action(s.id, { type: 'input', key: 'Light', value: 'short' })
      allLightsOff(s)
      s = await service.action(s.id, { type: 'input', key: 'Light', value: 'short' })
      s = await service.action(s.id, { type: 'input', key: 'Light', value: 'short' })
      s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'short' })
      assert.equal(s.snapshot!.internal.fan_speed, 0, 'automatic sleep still requires a wake-up touch')
      for (let i = 0; i < 2; i++) s = await service.action(s.id, { type: 'input', key: 'Speed', value: 'short' })
      assert.equal(s.snapshot!.internal.fan_speed, 2)
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 902)
      if (kind === 'purifier') {
        s = await service.action(s.id, { type: 'input', key: 'Filter', value: 'short' })
        assert.equal(s.snapshot!.display.unit, '')
        const resets = Number(s.snapshot!.internal.filter_reset_seq)
        s = await service.action(s.id, { type: 'input', key: 'Filter', value: 'long' })
        assert.equal(s.snapshot!.internal.filter_reset_seq, resets + 1, 'face reset key must drive the native Filter input')
      }
      s = await service.action(s.id, { type: 'input', key: 'Timer', value: 'short' })
      assert.equal(s.snapshot!.display.digits, '  2')
      s = await service.action(s.id, { type: 'input', key: 'Timer', value: 'short' })
      assert.equal(s.snapshot!.display.digits, '  4')
      assert.equal(s.snapshot!.internal.timer_left_s, 0)
      s = await service.action(s.id, { type: 'advance', ms: 3100 })
      assert.ok(Number(s.snapshot!.internal.timer_left_s) >= 14399 && Number(s.snapshot!.internal.timer_left_s) <= 14400)
      s = await service.action(s.id, { type: 'input', key: 'Timer', value: 'short' })
      assert.equal(s.snapshot!.display.digits, '  4', 'first press queries the running timer')
      s = await service.action(s.id, { type: 'input', key: 'Timer', value: 'short' })
      assert.equal(s.snapshot!.display.digits, '  6')
      s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
      assert.equal(s.snapshot!.internal.run_state, kind === 'purifier' ? 0 : 2)
      assert.equal(s.snapshot!.outputs.fan_target_rpm, kind === 'purifier' ? 0 : 401)
      if (kind !== 'purifier') { s = await service.action(s.id, { type: 'advance', ms: 2100 }); assert.equal(s.snapshot!.internal.run_state, 0) }
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 0)
      s = await service.action(s.id, { type: 'input', key: 'Power', value: 'short' })
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 902)
      s = await service.action(s.id, { type: 'supply', on: false })
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 0)
      ambientOutput(s, [0, 0, 0], kind !== 'humidifier')
      s = await service.action(s.id, { type: 'supply', on: true })
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 0, 'restored outputs are applied on the first tick')
      s = await service.action(s.id, { type: 'advance', ms: 10 })
      assert.equal(s.snapshot!.outputs.fan_target_rpm, 902, 'restored gear uses configured RPM')
      service.stop(s.id)
      assert.equal(service.get(s.id).status, 'stopped')

      const exported = await request('GET', `/api/products/${product.id}/parameters.xlsx?revision=1`)
      assert.equal(exported.statusCode, 200, exported.body.slice(0, 200))
      assert.match(exported.headers['content-type']!, /spreadsheetml/)
      const zip = unzipSync(exported.rawPayload), xml = strFromU8(zip['xl/worksheets/sheet1.xml'])
      assert.match(xml, /r="D11"[^>]*><(?:\w+:)?v>2<\/(?:\w+:)?v>/)
      assert.match(xml, /r="D17"[^>]*><(?:\w+:)?v>2<\/(?:\w+:)?v>/)
      assert.match(xml, /r="D22"[^>]*><(?:\w+:)?v>7<\/(?:\w+:)?v>/)
      assert.match(xml, /r="D23"[^>]*><(?:\w+:)?v>2<\/(?:\w+:)?v>/)
      for (const [row, value] of [[24, 111], [25, 401], [26, 902], [27, 1303], [28, 1605]])
        assert.match(xml, new RegExp('r="D' + row + '"[^>]*><(?:\\w+:)?v>' + value + '<\\/(?:\\w+:)?v>'))
      const metadata = strFromU8(zip['xl/worksheets/sheet2.xml'])
      assert.ok(metadata.includes('运行时&lt;' + kind + '&gt;&amp;配置'))
      assert.ok(metadata.includes(provider.catalog.source.fingerprint))
      assert.match(metadata, /r="B22"[^>]*t="inlineStr"[^>]*>[\s\S]*?aaerrxvja3m7lvhz/)
      const update = await request('PUT', '/api/products/' + product.id, { revision: 1, definition: setProfile(definition, 'dry_s', 3) })
      assert.equal(update.statusCode, 200, update.body)
      assert.equal((await request('GET', `/api/products/${product.id}/parameters.xlsx?revision=1`)).statusCode, 409)
      const invalid = setProfile(definition, 'filter_life_h', 0)
      assert.equal((await request('PUT', '/api/products/' + product.id, { revision: 2, definition: invalid })).statusCode, 400)
      const stale = structuredClone(definition)
      stale.parameters.find(p => p.key === 'source_fingerprint')!.value = 'outdated'
      assert.equal((await request('POST', '/api/products', stale)).statusCode, 400)
    }
    for (const [file, bytes] of sourceSets[0]) if (file !== 'include/apl_product.h') {
      assert.deepEqual(sourceSets[1].get(file), bytes, 'product choice cannot change compiled library: ' + file)
      assert.deepEqual(sourceSets[2].get(file), bytes)
    }
  } finally { await app.close(); await service.close(); products.close(); rmSync(dir, { recursive: true, force: true }) }
})
