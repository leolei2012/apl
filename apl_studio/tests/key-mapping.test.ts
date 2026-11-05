import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, readFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { resolve, join, sep } from 'node:path'
import { randomUUID } from 'node:crypto'
import { unzipSync, strFromU8 } from 'fflate'
import type { Definition, Product } from '@apl-studio/model'
import { firmwareBuild, setFirmwareBuild } from '@apl-studio/model/firmware'
import { KEY_MAPPING_PARAMETER, defaultKeyMapping, keyActionAvailable, keyMappingActions, keyMappingSlots, profileKeyMapping, setKeyMapping, validKeyMapping } from '@apl-studio/model/key-mapping'
import { bindCurrentProfile, newProfile, setProfile, validateProfile } from '@apl-studio/model/profile'
import { RuntimeProfileProvider, TemplateRegistry } from '@apl-studio/templates'
import { profileWorkbook, firmwarePackage } from '@apl-studio/definition'
import { SimulationService } from '@apl-studio/build'

const repository = resolve(import.meta.dirname, '../..')
const provider = new RuntimeProfileProvider(repository)
const asProduct = (definition: Definition): Product => ({ id: randomUUID(), revision: 3, createdAt: '', updatedAt: '', definition })
function rawMapping(definition: Definition, value: string | number | boolean): Definition {
  return { ...definition, parameters: [...definition.parameters.filter(p => p.key !== KEY_MAPPING_PARAMETER), { id: KEY_MAPPING_PARAMETER, key: KEY_MAPPING_PARAMETER, name: '按键映射', value }] }
}

test('按键映射严格校验完整结构、七个动作范围和持久化格式，错误记录不能默默使用默认值', () => {
  const definition = newProfile(provider.catalog, '映射校验'), defaults = defaultKeyMapping()
  const invalid: unknown[] = [null, [], {}, { ...defaults, enabled: 1 }, { ...defaults, extra: true },
    { ...defaults, short: undefined }, { ...defaults, long: undefined },
    { ...defaults, short: defaults.short.slice(1) }, { ...defaults, long: [...defaults.long, 0] },
    { ...defaults, short: Array(7) },
    ...[-1, 16, 0.5, '1', null, NaN, Infinity, false].map(id => ({ ...defaults, short: [id, ...defaults.short.slice(1)] })),
  ]
  for (const value of invalid) {
    assert.equal(validKeyMapping(value), false, JSON.stringify(value))
    const encoded = JSON.stringify(value)
    const errors = validateProfile(rawMapping(definition, encoded), provider.catalog)
    assert.ok(errors.some(error => error.includes('按键映射')), `${encoded}: ${errors.join('；')}`)
  }
  for (const value of ['{', '', 'null', '[]', 1, false]) {
    const bad = rawMapping(definition, value)
    assert.deepEqual(profileKeyMapping(bad), defaults, 'editor may recover a readable default')
    assert.ok(validateProfile(bad, provider.catalog).some(error => error.includes('按键映射')), 'saving must reject malformed persisted input')
  }
  const duplicates = rawMapping(definition, JSON.stringify(defaults))
  duplicates.parameters.push({ ...duplicates.parameters.find(p => p.key === KEY_MAPPING_PARAMETER)! })
  assert.ok(validateProfile(duplicates, provider.catalog).length > 0)
})

test('缺失映射的旧产品默认兼容，schema、Studio 和 C 枚举使用同一输入及动作编号', () => {
  const definition = newProfile(provider.catalog, '旧产品映射'), expected = defaultKeyMapping()
  assert.equal(definition.parameters.some(p => p.key === KEY_MAPPING_PARAMETER), false)
  assert.deepEqual(profileKeyMapping(definition), expected)
  assert.deepEqual(validateProfile(definition, provider.catalog), [])
  const returned = profileKeyMapping(definition); returned.short[0] = 0; returned.long[0] = 0
  assert.deepEqual(profileKeyMapping(definition), expected, 'default arrays must not be shared')
  const old = setProfile(definition, 'version', 5)
  assert.deepEqual(profileKeyMapping(bindCurrentProfile(old, provider.catalog)), expected)
  const schema = JSON.parse(readFileSync(resolve(repository, 'apl_lib/parameters/profile.schema.json'), 'utf8'))
  assert.equal(schema.schemaVersion, 6)
  assert.equal(schema.keyMapping.parameter, KEY_MAPPING_PARAMETER)
  assert.deepEqual(schema.keyMapping.default, expected)
  assert.deepEqual(schema.keyMapping.slots.map((slot: { id: number; name: string }) => [slot.id, slot.name]), keyMappingSlots.map(slot => [slot.id, slot.key]))
  assert.deepEqual(schema.keyMapping.actions.map((action: { id: number }) => action.id), keyMappingActions.map(action => action.id))
  const header = readFileSync(resolve(repository, 'apl_lib/include/apl_type.h'), 'utf8')
  const actions = header.match(/enum apl_key_action\s*\{([\s\S]*?)\};/)![1]
  const names = [...actions.matchAll(/APL_KEY_ACTION_([A-Z_]+)/g)].map(match => match[1]).filter(name => name !== 'COUNT')
  assert.deepEqual(schema.keyMapping.actions.map((action: { key: string }) => action.key), names)
})

test('功能裁剪允许保留未执行动作，启用自定义映射必须编入映射和 HMI', () => {
  const definition = newProfile(provider.catalog, '映射裁剪'), mapping = defaultKeyMapping()
  mapping.enabled = true; mapping.short[3] = 6
  const custom = setKeyMapping(setProfile(definition, 'modules', 0), mapping), build = firmwareBuild(custom)
  const noWifi = setFirmwareBuild(custom, { ...build, defines: { ...build.defines, APL_COMPILE_WIFI: 0 } })
  assert.deepEqual(validateProfile(noWifi, provider.catalog), [], 'a currently unavailable action remains a valid saved choice')
  for (const flag of ['APL_HMI_COMPILE_KEYMAP', 'APL_COMPILE_HMI'] as const) {
    const trimmed = setFirmwareBuild(custom, { ...build, defines: { ...build.defines, [flag]: 0 } })
    assert.ok(validateProfile(trimmed, provider.catalog).some(error => error.includes('自定义按键映射需要编入')))
    assert.deepEqual(validateProfile(setKeyMapping(trimmed, defaultKeyMapping()), provider.catalog), [])
  }
  const noLight = setFirmwareBuild(custom, { ...build, defines: { ...build.defines, APL_HMI_COMPILE_KEY_LIGHT: 0 } })
  assert.deepEqual(validateProfile(noLight, provider.catalog), [])
  assert.equal(profileKeyMapping(noLight).short[3], 6)
  const humidifier = setProfile(newProfile(provider.catalog, '加湿机独立杀菌', { purify: false, humidify: true }), 'modules', 1)
  assert.equal(keyActionAvailable(humidifier, 5), true, 'UV/anion compound toggle does not require the purification product family')
  const humidifierBuild = firmwareBuild(humidifier)
  assert.equal(keyActionAvailable(setFirmwareBuild(humidifier, { ...humidifierBuild, defines: { ...humidifierBuild.defines, APL_COMPILE_UV: 0 } }), 5), false)
})

test('仿真初始化、固件源码包和 Excel 第四表保存同一份自定义映射', async () => {
  const mapping = defaultKeyMapping(); mapping.enabled = true
  mapping.short[3] = 1; mapping.short[5] = 0; mapping.long[3] = 8; mapping.long[5] = 4
  const definition = setKeyMapping(newProfile(provider.catalog, '映射<导出>&'), mapping), product = asProduct(definition)
  const generated = await provider.generate(definition), firmware = provider.firmwareFiles(definition)
  const simulationHeader = Buffer.from(generated.get('include/apl_product.h')!).toString()
  const firmwareHeader = Buffer.from(firmware.get('include/apl_product_profile.h')!).toString()
  for (const [header, target] of [[simulationHeader, 'p.keymap'], [firmwareHeader, 'p->keymap']] as const) {
    assert.ok(header.includes('#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP'))
    assert.ok(header.includes(`${target}.enabled = 1u;`))
    for (let slot = 0; slot < 7; slot++) {
      assert.ok(header.includes(`${target}.short_action[${slot}] = ${mapping.short[slot]}u;`))
      assert.ok(header.includes(`${target}.long_action[${slot}] = ${mapping.long[slot]}u;`))
    }
  }
  const archive = unzipSync(firmwarePackage(product, firmware, provider.catalog.source.fingerprint).bytes)
  assert.equal(strFromU8(archive['apl-firmware/include/apl_product_profile.h']), firmwareHeader)
  const exported = JSON.parse(strFromU8(archive['apl-firmware/product.apl.json']))
  assert.deepEqual(profileKeyMapping(exported), mapping)
  const workbook = unzipSync(profileWorkbook(provider.workbook, product, provider.catalog))
  assert.ok(workbook['xl/worksheets/sheet4.xml'])
  const xml = strFromU8(workbook['xl/worksheets/sheet4.xml'])
  const cell = (address: string) => {
    const match = xml.match(new RegExp(`<(?:[\\w.-]+:)?c\\b[^>]*\\br="${address}"[^>]*>[\\s\\S]*?<\\/(?:[\\w.-]+:)?c>`))
    assert.ok(match, `missing ${address}`); return match[0]
  }
  const number = (address: string) => Number(cell(address).match(/<(?:[\w.-]+:)?v>([^<]+)<\/(?:[\w.-]+:)?v>/)![1])
  assert.equal(number('B3'), 1)
  for (let slot = 0; slot < 7; slot++) {
    assert.equal(number('C' + (slot + 7)), mapping.short[slot])
    assert.equal(number('E' + (slot + 7)), mapping.long[slot])
    assert.ok(cell('D' + (slot + 7)).includes(keyMappingActions[mapping.short[slot]].name))
    assert.ok(cell('F' + (slot + 7)).includes(keyMappingActions[mapping.long[slot]].name))
  }
})

test('真实 C 仿真按映射执行并保留输入事件：Light 开机、Humidity 调速和 FILTER0 的 Filter 输入', { timeout: 60000 }, async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-key-mapping-')), registry = new TemplateRegistry()
  registry.register(provider)
  const service = new SimulationService(directory, registry)
  const mapping = defaultKeyMapping(); mapping.enabled = true; mapping.short.fill(0); mapping.long.fill(0)
  mapping.short[3] = 1; mapping.long[3] = 8
  mapping.short[1] = 3; mapping.short[6] = 3; mapping.long[5] = 4
  let definition = setKeyMapping(newProfile(provider.catalog, '纯净化输入复用', { purify: true, humidify: false }), mapping)
  definition = setProfile(definition, 'modules', 0)
  const build = firmwareBuild(definition)
  definition = setFirmwareBuild(definition, { ...build, defines: { ...build.defines, APL_COMPILE_FILTER: 0 } })
  assert.deepEqual(validateProfile(definition, provider.catalog), [])
  try {
    let session = await service.create(asProduct(definition), false)
    const deadline = Date.now() + 30000
    while (session.status === 'building' && Date.now() < deadline) { await new Promise(resolve => setTimeout(resolve, 30)); session = service.get(session.id) }
    assert.equal(session.status, 'ready', session.diagnostics)
    for (const key of ['Humidity', 'Filter', 'Light', 'Power']) assert.ok(session.descriptor.controls.some(control => control.key === key && control.kind === 'gesture'), key)
    session = await service.action(session.id, { type: 'advance', ms: 2100 })
    assert.equal(session.snapshot!.internal.run_state, 0)
    assert.equal(session.snapshot!.display.key_3, 128, 'standby power lamp follows the mapped power action')
    assert.equal(session.snapshot!.display.key_5, 0)
    session = await service.action(session.id, { type: 'input', key: 'Power', value: 'short' })
    assert.equal(session.snapshot!.internal.run_state, 0, 'original Power short is NONE')
    assert.equal(session.snapshot!.display.brightness, 0, 'NONE does not wake the display')
    assert.ok(session.events.some(event => event.kind === 'input' && event.message === 'Power短按'))
    session = await service.action(session.id, { type: 'input', key: 'Light', value: 'short' })
    assert.equal(session.snapshot!.internal.run_state, 1)
    assert.equal(session.snapshot!.internal.fan_speed, 1)
    assert.ok(session.events.some(event => event.kind === 'input' && event.message === 'Light短按'))
    session = await service.action(session.id, { type: 'input', key: 'Humidity', value: 'short' })
    assert.equal(session.snapshot!.internal.fan_speed, 2, 'pure purifier may use the Humidity input for a fan action')
    assert.ok(session.events.some(event => event.kind === 'input' && event.message === 'Humidity短按'))
    session = await service.action(session.id, { type: 'input', key: 'Filter', value: 'short' })
    assert.equal(session.snapshot!.internal.fan_speed, 3, 'FILTER0 removes maintenance logic, not a compiled input slot used by another action')
    assert.ok(session.events.some(event => event.kind === 'input' && event.message === 'Filter短按'))
    session = await service.action(session.id, { type: 'input', key: 'Power', value: 'long' })
    assert.equal(session.snapshot!.internal.fan_speed, 5, 'long action is independent of the same input short NONE')
    assert.equal(session.snapshot!.internal.run_state, 1)
    assert.ok(session.events.some(event => event.kind === 'input' && event.message === 'Power长按'))
    session = await service.action(session.id, { type: 'input', key: 'Light', value: 'long' })
    assert.equal(session.snapshot!.display.child_lock, 1)
    assert.equal(session.snapshot!.internal.run_state, 1, 'Light long must not execute its short power action')
    session = await service.action(session.id, { type: 'input', key: 'Humidity', value: 'short' })
    assert.equal(session.snapshot!.internal.fan_speed, 5, 'child lock still blocks other mapped actions')
    session = await service.action(session.id, { type: 'input', key: 'Light', value: 'long' })
    assert.equal(session.snapshot!.display.child_lock, 0, 'mapped child-lock action can unlock outside the original Timer input')
    session = await service.action(session.id, { type: 'input', key: 'Light', value: 'short' })
    assert.equal(session.snapshot!.internal.run_state, 0)
  } finally {
    await service.close()
    assert.ok(resolve(directory).startsWith(resolve(tmpdir()) + sep))
    rmSync(directory, { recursive: true, force: true })
  }
})
