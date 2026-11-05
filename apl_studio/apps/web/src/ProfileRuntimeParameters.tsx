import { ChevronDown } from 'lucide-react'
import { useState } from 'react'
import type { ProfileField } from '@apl-studio/model/profile'
import { ParameterGuide } from './ParameterGuide'

const gears = ['睡眠', '一档', '二档', '三档', '自动', '极速']
const groups = [
  { id: 'fan', name: '风机运行', description: '开机风档、传感器异常时的备用风档和极速功能。', keys: ['default_fan_gear', 'sensor_fallback_gear', 'turbo_enabled'] },
  { id: 'rpm', name: '各档转速', description: '睡眠到极速的目标转速，单位 rpm；0 表示停转。', keys: ['fan_sleep_rpm', 'fan_gear1_rpm', 'fan_gear2_rpm', 'fan_gear3_rpm', 'fan_turbo_rpm'] },
  { id: 'humidify', name: '加湿与风干', description: '恒湿控制、关机风干和缺水后的处理。', keys: ['humidity_hysteresis', 'dry_fan_gear', 'dry_s', 'shortage_run_s', 'water_blink_s'] },
  { id: 'display', name: '显示与定时', description: '传感器数值切换频率和定时按键步进。', keys: ['display_rotate_s', 'timer_step_h'] },
  { id: 'filter', name: '滤网维护', description: '滤网使用寿命和重置方式。', keys: ['filter_life_h', 'filter_shared'] },
  { id: 'wifi', name: 'Wi-Fi 联网', description: '联网产品 PID 和等待配网的最长时间。', keys: ['wifi_pair_s'] },
]
const groupedKeys = new Set(groups.flatMap(group => group.keys))
const basicKeys = new Set(['version', 'purify_enabled', 'humidify_enabled', 'modules', 'sensors'])
const humidifyKeys = new Set(['dry_fan_gear', 'humidity_hysteresis', 'dry_s', 'shortage_run_s', 'water_blink_s'])

type Props = { fields: ProfileField[]; values: Record<string, number>; onChange: (key: string, value: number) => void
  pid: string; pidMaxLength: number; onPidChange: (value: string) => void }

export function ProfileRuntimeParameters({ fields, values, onChange, pid, pidMaxLength, onPidChange }: Props) {
  const [focusedFields, setFocusedFields] = useState<Record<string, string>>({})
  const visible = fields.filter(field => !basicKeys.has(field.key)
    && (!humidifyKeys.has(field.key) || !!values.humidify_enabled)
    && (!['filter_shared', 'display_rotate_s'].includes(field.key) || !!(values.purify_enabled && values.humidify_enabled))
    && (field.key !== 'wifi_pair_s' || !!(values.modules & 4)))
  const byKey = new Map(visible.map(field => [field.key, field]))
  const sections = groups.map(group => ({ ...group, fields: group.keys.flatMap(key => byKey.has(key) ? [byKey.get(key)!] : []) }))
  // Future APL fields must remain editable even before a dedicated UI category is added.
  const other = visible.filter(field => !groupedKeys.has(field.key))
  if (other.length) sections.push({ id: 'other', name: '其他参数', description: '当前 APL 版本提供的其他运行设置。', keys: other.map(field => field.key), fields: other })

  return <section className="ps-card ps-runtime-parameters" aria-labelledby="runtime-parameters-heading">
    <div className="ps-card-heading"><span>03</span><div><h2 id="runtime-parameters-heading">运行参数</h2><p>展开一组设置，点选参数查看解释和动作示意；修改数值后，示意会同步更新。</p></div></div>
    <div className="ps-parameter-groups">{sections.filter(group => group.fields.length).map(group => {
      const focused = group.fields.find(field => field.key === focusedFields[group.id]) ?? group.fields[0]
      const guideId = `parameter-guide-${group.id}`
      return <details className="ps-parameter-group" key={group.id} open={group.id === 'fan'}>
        <summary><div className="ps-parameter-group-title"><strong>{group.name}</strong><span>{group.fields.length + Number(group.id === 'wifi')} 项</span><p>{group.description}</p></div><ChevronDown size={17} aria-hidden="true"/></summary>
        <div className="ps-fields-grid ps-parameter-group-fields">
          {group.id === 'wifi' && <label className="ps-field">产品 PID<input name="pid" type="text" maxLength={pidMaxLength} placeholder="例如：aaerrxvja3m7lvhz" autoComplete="off" spellCheck={false} value={pid} onChange={e => onPidChange(e.target.value)}/><small>Wi-Fi 产品标识字符串，最多 {pidMaxLength} 位英文字母或数字，保留大小写和前导零；可留空。</small></label>}
          {group.fields.map(field =>
          <label className="ps-field" data-guided={focused.key === field.key} key={field.key} onFocus={() => setFocusedFields(previous => ({ ...previous, [group.id]: field.key }))}>{field.name}<div className="ps-input-unit">
            {field.max === 1 ? <select name={field.key} aria-describedby={`${field.key}-help ${guideId}`} value={values[field.key]} onChange={e => onChange(field.key, Number(e.target.value))}><option value={1}>开启</option><option value={0}>关闭</option></select>
              : field.key.endsWith('gear') ? <select name={field.key} aria-describedby={`${field.key}-help ${guideId}`} value={values[field.key]} onChange={e => onChange(field.key, Number(e.target.value))}>{gears.slice(0, field.max + 1).map((gear, i) => <option value={i} key={gear}>{gear}</option>)}</select>
                : <input name={field.key} aria-describedby={`${field.key}-help ${guideId}`} type="number" min={field.min} max={field.max} step={1} value={Number.isSafeInteger(values[field.key]) ? values[field.key] : ''} onChange={e => onChange(field.key, e.target.value === '' ? NaN : Number(e.target.value))}/>}
            <span>{field.unit === '位掩码' ? '' : field.unit}</span>
          </div><small id={`${field.key}-help`}>{field.description}</small></label>
        )}</div>
        <div id={guideId} className="ps-parameter-guide-wrap"><ParameterGuide key={focused.key} fieldKey={focused.key} values={values}/></div>
      </details>
    })}</div>
  </section>
}
