import { test } from 'node:test'
import assert from 'node:assert/strict'
import { defaultFirmwareBuild, type FirmwareBuild } from '@apl-studio/model/firmware'
import { defaultMemorySettings, estimateMemory, memorySettings, validMemorySettings, type MemorySettings } from '../packages/model/src/memory'

function build(overrides: Partial<MemorySettings> = {}, target: FirmwareBuild['target'] = 'c51') {
  return { ...defaultFirmwareBuild(), target, memory: { ...defaultMemorySettings(), ...overrides } }
}

test('C51估算分项与当前裁剪占位一致，移除HMI和Wi-Fi不重复计入', () => {
  const cases: { bytes: number; flags: Partial<FirmwareBuild['defines']> }[] = [
    { bytes: 516, flags: {} },
    { bytes: 498, flags: { APL_HMI_COMPILE_KEYMAP: 0 } },
    { bytes: 490, flags: { APL_COMPILE_WIFI: 0 } },
    { bytes: 425, flags: { APL_HMI_COMPILE_KEY_CALLBACKS: 0, APL_HMI_COMBO_MAX: 2, APL_CORE_COMPILE_FILTER_CACHE: 0 } },
    { bytes: 399, flags: { APL_COMPILE_WIFI: 0, APL_HMI_COMPILE_KEY_CALLBACKS: 0, APL_HMI_COMBO_MAX: 2, APL_CORE_COMPILE_FILTER_CACHE: 0 } },
    { bytes: 391, flags: { APL_HMI_COMPILE_KEY_CALLBACKS: 0, APL_HMI_COMBO_MAX: 0, APL_HMI_COMPILE_BUZZER: 0, APL_CORE_COMPILE_FILTER_CACHE: 0 } },
    { bytes: 176, flags: { APL_COMPILE_HMI: 0, APL_CORE_COMPILE_FILTER_CACHE: 0 } },
    { bytes: 150, flags: { APL_COMPILE_WIFI: 0, APL_COMPILE_HMI: 0, APL_CORE_COMPILE_FILTER_CACHE: 0 } },
  ]
  for (const { bytes, flags } of cases) {
    const b = build({ instanceBytes: 9999 }); Object.assign(b.defines, flags)
    const e = estimateMemory(b)
    assert.equal(e.basis, 'c51-reference')
    assert.equal(e.perInstanceBytes, bytes, 'a stale portable override must not replace C51 layout')
    assert.equal(e.aplBytes, bytes)
    assert.equal(e.coreBytes! + e.hmiBytes! + e.schedulerBytes!, bytes)
    if (!b.defines.APL_COMPILE_HMI) assert.equal(e.hmiBytes, 0)
  }
})

test('RAM合计包含全部实例、宿主、栈和安全余量，历史整机基线仍报告超限', () => {
  const e = estimateMemory(build({ hostStaticBytes: 479, safetyBytes: 0 }))
  assert.equal(e.aplBytes, 516)
  assert.equal(e.knownBytes, 1251)
  assert.equal(e.totalBytes, 1251)
  assert.equal(e.remainingBytes, -227)
  assert.equal(e.status, 'over')
  const multiple = estimateMemory(build({ instanceCount: 2, hostStaticBytes: 100, safetyBytes: 64 }))
  assert.equal(multiple.aplBytes, 1032)
  assert.equal(multiple.totalBytes, 1452)
  assert.equal(multiple.status, 'over')
})

test('未知输入与显式0区别处理，已知占用超过预算时不等待其他字段', () => {
  const incomplete = estimateMemory(build())
  assert.equal(incomplete.totalBytes, null)
  assert.equal(incomplete.remainingBytes, null)
  assert.equal(incomplete.knownBytes, 836)
  assert.equal(incomplete.knownRemainingBytes, 188)
  assert.equal(incomplete.status, 'unknown')
  assert.deepEqual(incomplete.missing, ['宿主及其他数据'])
  const zero = estimateMemory(build({ hostStaticBytes: 0, safetyBytes: 0 }))
  assert.equal(zero.totalBytes, 772)
  assert.equal(zero.status, 'within-budget')
  const alreadyOver = estimateMemory(build({ instanceCount: 2 }))
  assert.equal(alreadyOver.status, 'over')
  assert.equal(alreadyOver.totalBytes, null)
  assert.equal(alreadyOver.knownRemainingBytes, -328)
})

test('通用平台要求输入目标编译器实例大小，不套用C51数字', () => {
  const unknown = estimateMemory(build({}, 'portable'))
  assert.equal(unknown.basis, 'unknown')
  assert.equal(unknown.aplBytes, null)
  assert.equal(unknown.perInstanceBytes, null)
  assert.equal(unknown.coreBytes, null)
  assert.equal(unknown.hmiBytes, null)
  assert.equal(unknown.schedulerBytes, null)
  assert.deepEqual(unknown.missing, ['APL 单实例大小', '宿主及其他数据'])
  const manual = estimateMemory(build({ instanceBytes: 128, instanceCount: 3, hostStaticBytes: 24, safetyBytes: 16 }, 'portable'))
  assert.equal(manual.basis, 'manual')
  assert.equal(manual.aplBytes, 384)
  assert.equal(manual.totalBytes, 680)
  assert.equal(manual.status, 'within-budget')
})

test('预算边界保留紧张状态，Flash只使用人工填写的链接数据', () => {
  const exact = estimateMemory(build({ hostStaticBytes: 252, safetyBytes: 0 }))
  assert.equal(exact.totalBytes, 1024)
  assert.equal(exact.remainingBytes, 0)
  assert.equal(exact.status, 'tight')
  const tenPercent = build({ hostStaticBytes: 0, safetyBytes: 0 }, 'portable')
  tenPercent.ramBytes = 1000; tenPercent.stackBytes = 0; tenPercent.memory.instanceBytes = 900
  assert.equal(estimateMemory(tenPercent).status, 'tight')
  tenPercent.memory.instanceBytes = 899
  assert.equal(estimateMemory(tenPercent).status, 'within-budget')
  const unknown = estimateMemory(build())
  assert.deepEqual(unknown.flash, { budgetBytes: 32768, usedBytes: null, remainingBytes: null, status: 'unknown' })
  const zero = estimateMemory(build({ flashUsedBytes: 0 }))
  assert.equal(zero.flash.remainingBytes, 32768)
  assert.equal(zero.flash.status, 'within-budget')
  const over = estimateMemory(build({ flashUsedBytes: 32833 }))
  assert.equal(over.flash.status, 'over')
  assert.equal(over.flash.remainingBytes, -65)
  const atLimit = estimateMemory(build({ flashUsedBytes: 32768 }))
  assert.equal(atLimit.flash.status, 'tight')
})

test('内存设置严格校验且旧配置补默认，不接受隐式类型转换或额外字段', () => {
  assert.deepEqual(memorySettings(defaultFirmwareBuild()), defaultMemorySettings())
  assert.ok(validMemorySettings(defaultMemorySettings()))
  assert.ok(validMemorySettings({ instanceCount: 64, hostStaticBytes: 1073741824, safetyBytes: 0, instanceBytes: 1, flashUsedBytes: 0 }))
  const defaults = defaultMemorySettings()
  for (const value of [null, [], {}, { ...defaults, unknown: 1 }, { ...defaults, hostStaticBytes: undefined },
    { ...defaults, instanceCount: 0 }, { ...defaults, instanceCount: 65 }, { ...defaults, instanceCount: 1.5 },
    { ...defaults, hostStaticBytes: -1 }, { ...defaults, hostStaticBytes: '0' }, { ...defaults, hostStaticBytes: 1073741825 },
    { ...defaults, safetyBytes: null }, { ...defaults, safetyBytes: NaN }, { ...defaults, safetyBytes: Infinity },
    { ...defaults, instanceBytes: 0 }, { ...defaults, instanceBytes: 1073741825 }, { ...defaults, flashUsedBytes: -1 },
    { ...defaults, flashUsedBytes: 1.1 }]) assert.equal(validMemorySettings(value), false, JSON.stringify(value))
  const b = build({ hostStaticBytes: 123 })
  const copy = memorySettings(b); copy.hostStaticBytes = 999
  assert.equal(b.memory.hostStaticBytes, 123)
  const first = defaultMemorySettings(); first.safetyBytes = 0
  assert.equal(defaultMemorySettings().safetyBytes, 64)
})
