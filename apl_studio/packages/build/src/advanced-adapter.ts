import { readFileSync } from 'node:fs'
import type { SimulationAdapter } from './simulation-adapters'
import type { SimulationControl } from '@apl-studio/model/simulation'

/** v2 has a distinct native bridge; legacy and paused combination bridges stay unchanged. */
export function createAdvancedAdapter(base: SimulationAdapter): SimulationAdapter {
  return {
    supports: t => t.engine === 'apl-air-care-v1' && t.sourceVersion === '0.2.0' && t.workflow === 'air-care-standard-v2',
    sourceDirectories: ['src'], includeDirectories: ['include', 'config'],
    source: d => readFileSync(new URL('./runners/air-care-advanced.c', import.meta.url), 'utf8').replaceAll('SIM_PRODUCT', 'apl_' + d.code),
    inputCodes: { Purify: 0, Humidity: 1, Timer: 2, Light: 3, Speed: 4, Power: 5, Filter: 6,
      humidity: 100, pm25: 101, water_level: 102, uv_interlock: 103, combo_filter: 104, combo_factory: 105,
      uv_request: 110, anion_request: 111, wifi_connected: 112, uv_fault_input: 120, anion_fault_input: 121, wifi_fault_input: 122,
      pm25_status: 130, humidity_status: 131, pm10: 132, hcho: 133, tvoc: 134, pm10_status: 135, hcho_status: 136, tvoc_status: 137 },
    describe(d) {
      const has = (key: string) => d.functions.some(f => f.key === key), model = base.describe(d)
      // Controls are ordinary key/sensor requests, independent of the paused combination action API.
      Reflect.deleteProperty(model, 'combinations')
      model.id = 'apl-air-care-standard-v2'
      model.controls = model.controls.filter(c => !['Purify', 'pm25', 'humidity', 'uv_interlock'].includes(c.key))
      const controls: SimulationControl[] = model.controls
      const bool = (key: string, name: string, target: 'sensor' | 'value' = 'sensor', value = false) => controls.push({ key, name, target, kind: 'bool', default: value })
      const gesture = (key: string, name: string) => controls.push({ key, name, target: 'key', kind: 'gesture', default: 'short' })
      if (has('uv') || has('anion') || has('wifi')) gesture('Purify', 'UV/负离子复合键（长按配网）')
      if (has('purify') && !has('humidify')) gesture('Filter', '滤网')
      if (has('humidify')) gesture('combo_filter', '定时+风速（滤网维护）')
      gesture('combo_factory', '电源+定时（待机产测）')
      for (const [key, name, max, value] of [['pm25','PM2.5',65535,12],['humidity','环境湿度',100,45],['pm10','PM10',65535,10],['hcho','甲醛（μg/m³）',65535,10],['tvoc','TVOC（μg/m³）',65535,10]] as const) {
        if (!has(key === 'pm10' || key === 'hcho' || key === 'tvoc' ? 'sensor_' + key : key)) continue
        controls.push({ key, name, target: 'sensor', kind: 'int', default: value, min: 0, max, step: 1 })
        controls.push({ key: key + '_status', name: name + '状态', target: 'sensor', kind: 'enum', default: 2,
          options: ['未接入', '预处理', '有效', '故障'].map((label, value) => ({ label, value })) })
      }
      if (has('uv')) { bool('uv_interlock', 'UV安全开关闭合', 'sensor', true); bool('uv_request', 'UV独立请求', 'value'); bool('uv_fault_input', 'UV已确认故障') }
      if (has('anion')) { bool('anion_request', '负离子独立请求', 'value'); bool('anion_fault_input', '负离子已确认故障') }
      if (has('wifi')) { bool('wifi_connected', 'Wi-Fi已连接', 'value'); bool('wifi_fault_input', 'Wi-Fi已确认故障') }
      model.outputs = model.outputs.filter(s => !['uv','anion'].includes(s.key))
      for (const key of ['uv','anion']) if (has(key)) model.outputs.push({ key, name: key === 'uv' ? 'UV' : '负离子', boolean: true })
      model.internal = model.internal.filter(s => !['purify','humidify','humidity','pm25','filter_purify_left','filter_humidify_left'].includes(s.key))
      for (const [key, name] of [['sensor_ready','有效输入标记'],['sensor_warming','预处理输入标记'],['sensor_faults','输入故障标记'],['module_faults','模块故障标记'],['aq_ready','空气质量输入可用']]) model.internal.push({ key, name })
      return model
    },
  }
}
