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
const runner = (name: string, prefix: string) => readFileSync(new URL('./runners/' + name + '.c', import.meta.url), 'utf8').replaceAll('SIM_PRODUCT', prefix)
const slot = (key: string, name: string, boolean = false, unit?: string): SimulationSlot => ({ key, name, boolean, unit })
const gears = ['睡眠', '一档', '二档', '三档', '自动', '极速'].map((label, value) => ({ value, label }))
const gear = (key: string, name: string): SimulationSlot => ({ key, name, options: gears })
export const airCareSimulation: SimulationAdapter = {
  supports: t => t.engine === 'apl-air-care-v1' && t.sourceVersion === '0.1.0',
  sourceDirectories: ['src'], includeDirectories: ['include', 'config'],
  inputCodes: { Purify: 0, Humidity: 1, Timer: 2, Light: 3, Speed: 4, Power: 5, humidity: 100, pm25: 101, water_level: 102, uv_interlock: 103 },
  source: d => runner('air-care', 'apl_' + d.code),
  describe(d) {
    const has = (key: string) => d.functions.some(f => f.key === key)
    return { id: 'apl-air-care-v1', name: 'APL 面板与实际负载', tickMs: 10, panel: 'round', memory: true,
      controls: [
        ...['Purify', 'Humidity', 'Timer', 'Light', 'Speed', 'Power'].filter(key => key !== 'Purify' || has('purify')).filter(key => key !== 'Humidity' || has('humidify')).map((name) => ({ key: name, name, kind: 'gesture' as const, target: 'key' as const, default: 'short', position: ({ Purify: 'top', Humidity: 'left', Timer: 'right', Light: 'lower-left', Speed: 'lower-right', Power: 'bottom' } as Record<string, string>)[name] })),
        ...(has('humidify') ? [{ key: 'humidity', name: '环境湿度', kind: 'int' as const, target: 'sensor' as const, default: 45, min: 0, max: 100, step: 1, unit: '%' },
          { key: 'water_level', name: '水位', kind: 'enum' as const, target: 'sensor' as const, default: 3, options: ['缺水', '一格', '两格', '三格', '水满'].map((label, value) => ({ label, value })) }] : []),
        ...(has('purify') ? [{ key: 'pm25', name: 'PM2.5', kind: 'int' as const, target: 'sensor' as const, default: 12, min: 0, max: 65535, step: 1, unit: 'μg/m³' },
          { key: 'uv_interlock', name: 'UV 安全开关闭合', kind: 'bool' as const, target: 'sensor' as const, default: true }] : []),
      ],
      outputs: [slot('fan', '风机', true), gear('fan_gear', '实际风机档位'), ...(has('humidify') ? [slot('pump', '加湿负载', true)] : []), ...(has('purify') ? [slot('uv', 'UV', true), slot('anion', '负离子', true)] : []), slot('buzzer', '蜂鸣器', true)],
      internal: [{ key: 'run_state', name: '运行状态', options: ['待机', '运行', '关机风干', '产测'].map((label, value) => ({ label, value })) }, gear('fan_speed', '请求档位'), slot('purify', '净化请求', true), slot('humidify', '加湿请求', true), slot('humidity', '采样湿度', false, '%'), slot('pm25', '采样 PM2.5', false, 'μg/m³'), slot('humidity_target', '目标湿度', false, '%'), slot('timer_left_s', '定时剩余', false, 's'), slot('dry_left_s', '风干剩余', false, 's'), slot('sleep', '睡眠', true), slot('uv_fault', 'UV 故障', true), slot('wifi', 'Wi-Fi 状态'), slot('filter_purify_left', '净化滤芯寿命', false, '%'), slot('filter_humidify_left', '加湿滤芯寿命', false, '%')],
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
export const simulationAdapters: SimulationAdapter[] = [airCareSimulation, lightingSimulation]
