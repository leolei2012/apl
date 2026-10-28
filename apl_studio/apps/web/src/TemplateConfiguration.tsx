import { Boxes, CheckCircle2, Info, Settings2, Plug, Plus } from 'lucide-react'
import type { Definition } from '@apl-studio/model'
import { activeFor, toggleTemplateFeature, validateTemplateConfiguration, type TemplateDescriptor } from '@apl-studio/model/template'
import { Field, Select, Switch } from './components'

export function TemplateConfiguration({ definition, templates, onChange, onBind, mode = 'binding' }: {
  definition: Definition; templates: TemplateDescriptor[]; onChange: (definition: Definition) => void
  onBind: (template: TemplateDescriptor | null) => void; mode?: 'binding' | 'features' | 'parameters' | 'ports'
}) {
  const reference = definition.template
  const template = templates.find(t => t.id === reference?.id && t.version === reference.version)
  const enabled = new Set(definition.functions.map(f => f.key))
  const errors = template ? validateTemplateConfiguration(definition, template) : []
  if (mode === 'binding') return <section className="card template-binding"><div className="section-heading"><div><h2><Boxes size={17}/> 源码模板</h2><p>绑定精确版本，平台按模板描述配置功能、参数与移植接口。</p></div>{template && <span className="badge purple">v{template.version}</span>}</div>
    <div className="binding-options">{templates.map(item => <button className={'template-choice ' + (template?.fingerprint === item.fingerprint ? 'selected' : '')} key={item.id + item.version} onClick={() => onBind(item)}><Boxes size={23}/><span><strong>{item.name}</strong><small>{item.id}@{item.version} · 源码 {item.sourceVersion}</small></span>{template?.fingerprint === item.fingerprint ? <CheckCircle2 size={16}/> : <Plus size={16}/>}</button>)}</div>
    {!templates.length && <p className="subtle">暂无可用源码模板，请检查模板目录。</p>}
    {template && <div className="binding-summary"><span>{template.description}</span><button className="text-button" onClick={() => onBind(null)}>解除绑定</button></div>}
    {reference && !template && <p className="form-error">绑定版本 {reference.id}@{reference.version} 当前不可用。请加载该版本或解除绑定。</p>}
  </section>
  if (!template) return null
  return <>
    {(mode === 'features') && <section className="card template-features"><div className="section-heading"><div><h2>模板支持的产品能力</h2><p>启用功能时自动补齐依赖，关闭时同步移除依赖它的能力。</p></div><span className="badge purple">模板驱动</span></div><div className="template-feature-grid">{template.features.map(feature => {
      const selected = definition.functions.find(f => f.key === feature.key)
      return <article className={'feature-option ' + (selected ? 'enabled' : '')} key={feature.key}><div className="control-heading"><strong>{feature.name}</strong><Switch label={`启用${feature.name}`} checked={!!selected} disabled={feature.required} onChange={on => onChange(toggleTemplateFeature(definition, template, feature.key, on))}/></div><p>{feature.description}</p><div className="feature-details"><code>{feature.key}</code><span>{feature.required ? '必选' : '可选'}{feature.requires?.length ? ` · 依赖 ${feature.requires.join(' / ')}` : ''}</span></div>
        {selected?.access !== 'read' && selected && <Field label="默认值">{selected.kind === 'bool' ? <Switch label={`${feature.name}默认值`} checked={Boolean(selected.default)} onChange={value => onChange({ ...definition, functions: definition.functions.map(f => f.key === feature.key ? { ...f, default: value } : f) })}/> : selected.kind === 'enum' ? <Select label={`${feature.name}默认值`} value={String(selected.default)} onChange={value => onChange({ ...definition, functions: definition.functions.map(f => f.key === feature.key ? { ...f, default: Number(value) } : f) })}>{selected.options.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}</Select> : <input aria-label={`${feature.name}默认值`} type={selected.kind === 'int' ? 'number' : 'text'} min={selected.min} max={selected.max} step={selected.step} maxLength={selected.maxLength} value={String(selected.default)} onChange={event => onChange({ ...definition, functions: definition.functions.map(f => f.key === feature.key ? { ...f, default: selected.kind === 'int' ? Number(event.target.value) : event.target.value } : f) })}/>}</Field>}
      </article>
    })}</div>{!!errors.length && <div className="inline-alert warning"><Info size={15}/><span>{errors[0]}</span></div>}</section>}
    {(mode === 'parameters') && <section className="card"><div className="section-heading"><div><h2><Settings2 size={17}/> 模板参数</h2><p>这些表单直接来自模板描述；保存的数值将写入生成 SDK。</p></div><span className="badge purple">自动生成表单</span></div><div className="form-grid">{template.parameters.filter(p => activeFor(p.requires, enabled)).map(parameter => {
      const value = definition.parameters.find(p => p.key === parameter.key)
      const update = (next: number | boolean) => onChange({ ...definition, parameters: value ? definition.parameters.map(p => p.key === parameter.key ? { ...p, value: next } : p) : [...definition.parameters, { id: 'parameter_' + parameter.key, key: parameter.key, name: parameter.name, value: next }] })
      return <Field key={parameter.key} label={parameter.name + (parameter.unit ? `（${parameter.unit}）` : '')} hint={parameter.description}>{parameter.kind === 'bool' ? <Switch label={parameter.name} checked={Boolean(value?.value)} onChange={update}/> : parameter.kind === 'enum' ? <Select label={parameter.name} value={String(value?.value ?? parameter.default)} onChange={v => update(Number(v))}>{parameter.options?.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}</Select> : <input aria-label={parameter.name} type="number" min={parameter.min} max={parameter.max} step={parameter.step ?? 1} disabled={parameter.min === parameter.max} value={Number(value?.value ?? parameter.default)} onChange={event => update(Number(event.target.value))}/>}</Field>
    })}</div>{!!errors.length && <p className="form-error" role="alert">{errors[0]}</p>}</section>}
    {(mode === 'ports') && <section className="card port-list"><div className="section-heading"><div><h2><Plug size={17}/> 本产品需要移植的接口</h2><p>SDK 提供占位移植文件，工程师实现以下硬件访问。占位代码不代表已接通真实硬件。</p></div></div>{template.ports.filter(p => activeFor(p.requires, enabled)).map(port => <div className="port-entry" key={port.key}><div><strong>{port.name}</strong><span className="badge">{port.required ? '必需' : '可选'}</span></div><code>{port.signature}</code><p>{port.description}</p></div>)}</section>}
  </>
}
