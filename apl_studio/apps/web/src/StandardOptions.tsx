import type { Definition } from '@apl-studio/model'
import { applyTemplate, type TemplateDescriptor } from '@apl-studio/model/template'
import { Field, Select } from './components'

export function StandardOptions({ draft, template, setDraft }: { draft: Definition; template: TemplateDescriptor; setDraft: (draft: Definition) => void }) {
  const has = (key: string) => draft.functions.some(f => f.key === key)
  const profile = has('purify') ? has('humidify') ? 'combined' : 'purify' : 'humidify'
  function change(value: string) {
    const keys = ['power', 'fan_speed', ...(value !== 'humidify' ? ['purify', 'pm25'] : []), ...(value !== 'purify' ? ['humidify', 'humidity', 'drying'] : [])]
    setDraft(applyTemplate(draft, template, new Set(keys)))
  }
  return <section className="card"><h2>产品能力与项目参数</h2><div className="form-grid">
    <Select label="产品类型" value={profile} onChange={change}><option value="purify">空气净化器</option><option value="humidify">加湿器</option><option value="combined">净化加湿一体机</option></Select>
    {template.parameters.filter(p => !['tick_ms', 'filter_life_h', 'turbo_enabled'].includes(p.key) && (!p.requires || p.requires.every(has))).map(p => {
      const value = draft.parameters.find(v => v.key === p.key)!.value
      const update = (value: number) => setDraft({ ...draft, parameters: draft.parameters.map(v => v.key === p.key ? { ...v, value } : v) })
      return p.kind === 'enum' ? <Select key={p.key} label={p.name} value={String(value)} onChange={value => update(Number(value))}>{p.options?.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}</Select>
        : <Field key={p.key} label={p.name + (p.unit ? `（${p.unit}）` : '')}><input type="number" min={p.min} max={p.max} step={p.step} value={Number(value)} onChange={e => update(Number(e.target.value))}/></Field>
    })}
  </div><p className="pm-scope">风干按本次运行的泵输出记录决定；纯加湿缺水5分钟后风干，一体机缺水持续净化。风干时间和转速由项目确认。RH&gt;50优先级、Auto是否退出恒湿、湿差19%的档位仍待确认，当前版本保留原规则；UV/负离子暂为复合接口。</p></section>
}
