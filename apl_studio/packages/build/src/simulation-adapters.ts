import { profileValues, PROFILE_ID } from '@apl-studio/model/profile'
import { firmwareBuild } from '@apl-studio/model/firmware'
import { profileKeyMapping, keyMappingSlots } from '@apl-studio/model/key-mapping'
import { createAdvancedAdapter } from './advanced-adapter'
import { readFileSync } from 'node:fs'
import type { Definition } from '@apl-studio/model'
import type { TemplateDescriptor } from '@apl-studio/model/template'
import type { SimulationDescriptor, SimulationSlot } from '@apl-studio/model/simulation'

/** A product family owns its native bridge; the service and web UI only use this contract. */
export interface SimulationAdapter {
  supports(template: TemplateDescriptor): boolean
  describe(definition: Definition): SimulationDescriptor
  source(definition: Definition): string
  sourceDirectories: string[]; includeDirectories: string[]
  inputCodes: Record<string, number>
}
/** Execution and acceptance must fingerprint the identical host, including its scheduler period. */
export function simulationSource(adapter: SimulationAdapter, definition: Definition, tickMs = adapter.describe(definition).tickMs): string {
  if (!Number.isSafeInteger(tickMs) || tickMs < 1 || 10 % tickMs) throw new Error('仿真调度周期必须为 10ms 的正整数约数')
  const common = readFileSync(new URL('./runners/common.c', import.meta.url), 'utf8')
  return `#define SIM_TICK_MS ${tickMs}\n` + common.replace('/* ADAPTER */', adapter.source(definition))
}
const runner = (name: string, prefix: string) => readFileSync(new URL('./runners/' + name + '.c', import.meta.url), 'utf8').replaceAll('SIM_PRODUCT', prefix)
const slot = (key: string, name: string, boolean = false, unit?: string): SimulationSlot => ({ key, name, boolean, unit })
const gears = ['睡眠', '一档', '二档', '三档', '自动', '极速'].map((label, value) => ({ value, label }))
const gear = (key: string, name: string): SimulationSlot => ({ key, name, options: gears })
export const airCareSimulation: SimulationAdapter = {
  supports: t => t.engine === 'apl-air-care-v1' && ['0.1.0', '0.1.1', '0.1.3'].includes(t.sourceVersion),
  sourceDirectories: ['src'], includeDirectories: ['include', 'config'],
  inputCodes: { Purify: 0, Humidity: 1, Timer: 2, Light: 3, Speed: 4, Power: 5, humidity: 100, pm25: 101, water_level: 102, uv_interlock: 103, combo_filter: 104, combo_factory: 105 },
  source: d => runner('air-care', 'apl_' + d.code),
  describe(d) {
    const has = (key: string) => d.functions.some(f => f.key === key)
    return { id: 'apl-air-care-v1', name: 'APL 面板与实际负载', tickMs: 10, panel: 'round', memory: true,
      combinations: [
        { key: 'combo_filter', name: '重置滤网', keys: ['Timer', 'Speed'], holdMs: 3000 },
        { key: 'combo_factory', name: '进入产测', keys: ['Power', 'Timer'], holdMs: 3000 },
      ],
      controls: [
        ...['Purify', 'Humidity', 'Timer', 'Light', 'Speed', 'Power'].filter(key => key !== 'Purify' || has('purify')).filter(key => key !== 'Humidity' || has('humidify')).map((name) => ({ key: name, name, kind: 'gesture' as const, target: 'key' as const, default: 'short', position: ({ Purify: 'top', Humidity: 'left', Timer: 'right', Light: 'lower-left', Speed: 'lower-right', Power: 'bottom' } as Record<string, string>)[name] })),
        ...(has('humidify') ? [{ key: 'humidity', name: '环境湿度', kind: 'int' as const, target: 'sensor' as const, default: 45, min: 0, max: 100, step: 1, unit: '%' },
          { key: 'water_level', name: '水位', kind: 'enum' as const, target: 'sensor' as const, default: 3, options: ['缺水', '一格', '两格', '三格', '水满'].map((label, value) => ({ label, value })) }] : []),
        ...(has('purify') ? [{ key: 'pm25', name: 'PM2.5', kind: 'int' as const, target: 'sensor' as const, default: 12, min: 0, max: 65535, step: 1, unit: 'μg/m³' },
          { key: 'uv_interlock', name: 'UV 安全开关闭合', kind: 'bool' as const, target: 'sensor' as const, default: true }] : []),
      ],
      outputs: [slot('fan', '风机', true), gear('fan_gear', '实际风机档位'), ...(has('humidify') ? [slot('pump', '加湿负载', true)] : []), ...(has('purify') ? [slot('uv', 'UV', true), slot('anion', '负离子', true)] : []), slot('buzzer', '蜂鸣器', true)],
      internal: [{ key: 'run_state', name: '运行状态', options: ['待机', '运行', '关机风干', '产测'].map((label, value) => ({ label, value })) }, gear('fan_speed', '请求档位'), slot('purify', '净化请求', true), slot('humidify', '加湿请求', true), slot('humidity', '采样湿度', false, '%'), slot('pm25', '采样 PM2.5', false, 'μg/m³'), slot('humidity_target', '目标湿度', false, '%'), slot('timer_left_s', '定时剩余', false, 's'), slot('dry_left_s', '风干剩余', false, 's'), slot('sleep', '睡眠', true), slot('uv_fault', 'UV 故障', true), slot('wifi', 'Wi-Fi 状态'), slot('filter_purify_left', '净化滤芯寿命', false, '%'), slot('filter_humidify_left', '加湿滤芯寿命', false, '%'), slot('filter_reset_seq', '滤网重置序号')],
      display: [slot('light', '面板灯光', true), slot('child_lock', '童锁', true), slot('digits', '数码管'), slot('unit', '单位'), slot('brightness', '数码管亮度'), slot('ambient', '氛围灯亮度'), slot('water_level', '水位亮灯数'), slot('powerup', '上电全亮', true)],
    }
  },
}
export const lightingSimulation: SimulationAdapter = {
  supports: t => t.id === 'standard.lighting' && t.version === '1.0.0' && t.sourceVersion === '1.0.0',
  sourceDirectories: ['lib'], includeDirectories: ['api', 'settings'], inputCodes: { power: 200, brightness: 201 },
  source: d => runner('lighting', 'sdk_' + d.code),
  describe: d => ({ id: 'lighting-v1', name: '灯控实际输出', tickMs: 10, panel: 'controls', memory: false,
    controls: d.functions.filter(f => f.access !== 'read').map(f => ({ key: f.key, name: f.name, kind: f.kind as 'bool' | 'int', target: 'value', default: f.default, min: f.min, max: f.max, step: f.step, unit: f.unit })),
    outputs: [slot('light', '灯光输出', true), slot('brightness', '实际亮度', false, '%')],
    internal: [slot('power', '开灯请求', true), slot('brightness', '请求亮度', false, '%'), slot('elapsed_ms', '启动累计', false, 'ms')], display: [] }),
}
const advanced = createAdvancedAdapter(airCareSimulation)
export const runtimeProfileSimulation: SimulationAdapter = {
  ...advanced,
  source: d => '#define APL_STUDIO_FAN_RPM 1\n#define APL_STUDIO_AMBIENT_RGB 1\n' + advanced.source(d),
  supports: t => t.id === PROFILE_ID && t.sourceVersion === '0.3.0',
  describe(d) {
    const p = profileValues(d), keys = new Set<string>()
    if (p.purify_enabled) keys.add('purify')
    if (p.humidify_enabled) keys.add('humidify')
    for (const [bit, key] of [[1, 'uv'], [2, 'anion'], [4, 'wifi']] as const) if (p.modules & bit) keys.add(key)
    for (const [bit, key] of [[1, 'pm25'], [2, 'sensor_pm10'], [4, 'sensor_hcho'], [8, 'sensor_tvoc'], [16, 'humidity']] as const) if (p.sensors & bit) keys.add(key)
    const model = advanced.describe({ ...d, functions: [...keys].map(key => ({ key })) as Definition['functions'] })
    model.id = 'apl-runtime-profile-v1'
    model.name = '固定 APL 库 · 运行时参数'
    const build = firmwareBuild(d), keyMacros: Record<string, keyof typeof build.defines> = {
      Purify: 'APL_HMI_COMPILE_KEY_PURIFY', Humidity: 'APL_HMI_COMPILE_KEY_HUMIDITY', Timer: 'APL_HMI_COMPILE_KEY_TIMER',
      Light: 'APL_HMI_COMPILE_KEY_LIGHT', Speed: 'APL_HMI_COMPILE_KEY_SPEED', Power: 'APL_HMI_COMPILE_KEY_POWER', Filter: 'APL_HMI_COMPILE_KEY_FILTER' }
    const custom = profileKeyMapping(d).enabled && !!build.defines.APL_HMI_COMPILE_KEYMAP
    if (custom) for (const slot of keyMappingSlots) if (!model.controls.some(c => c.key === slot.key))
      model.controls.push({ key: slot.key, name: slot.name, kind: 'gesture', target: 'key', default: 'short' })
    model.controls = model.controls.filter(c => !keyMacros[c.key] || !!build.defines[keyMacros[c.key]])
    model.controls = model.controls.filter(c => !c.key.startsWith('combo_') || build.defines.APL_HMI_COMBO_MAX > 0
      && !!build.defines.APL_HMI_COMPILE_KEY_TIMER
      && !!build.defines[c.key === 'combo_filter' ? 'APL_HMI_COMPILE_KEY_SPEED' : 'APL_HMI_COMPILE_KEY_POWER'])
    model.memory = !!build.defines.APL_COMPILE_MEMORY
    model.controls = model.controls.filter(c => c.key !== 'Filter' || custom || !!build.defines.APL_COMPILE_FILTER)
    model.controls = model.controls.filter(c => c.key !== 'combo_filter' || !!build.defines.APL_COMPILE_FILTER)
    model.controls = model.controls.filter(c => c.key !== 'combo_factory' || !!build.defines.APL_COMPILE_FACTORY)
    model.tickMs = 1
    model.outputs.push(slot('fan_target_rpm', '风机目标转速', false, 'rpm'))
    model.display.push(slot('ambient_red', '氛围灯红通道'), slot('ambient_green', '氛围灯绿通道'), slot('ambient_blue', '氛围灯蓝通道'))
    return model
  },
}
export const simulationAdapters: SimulationAdapter[] = [runtimeProfileSimulation, advanced, airCareSimulation, lightingSimulation]
