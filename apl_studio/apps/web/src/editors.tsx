import { useState } from 'react'
import { Plus, Trash2 } from 'lucide-react'
import { newFunction, validateDefinition, kindNames, accessNames, type Definition, type FunctionDefinition, type Kind, type Access, type Parameter } from '@apl-studio/model'
import { Field, Modal, Select, Switch } from './components'

export function FunctionEditor({ definition, original, onSave, onClose }: { definition: Definition; original: FunctionDefinition | null; onSave: (field: FunctionDefinition) => void; onClose: () => void }) {
  const [field, setField] = useState<FunctionDefinition>(() => original ? structuredClone(original) : { ...newFunction(crypto.randomUUID()), key: `function_${definition.functions.length + 1}` })
  const [error, setError] = useState('')
  const update = <K extends keyof FunctionDefinition>(key: K, value: FunctionDefinition[K]) => setField(f => ({ ...f, [key]: value }))
  function changeKind(kind: Kind) {
    setField(f => ({ ...f, kind, default: kind === 'bool' ? false : kind === 'string' ? '' : kind === 'enum' ? 0 : f.min,
      ...(kind !== 'int' ? { min: 0, max: 100, step: 1 } : {}),
      options: kind === 'enum' ? (f.options.length ? f.options : [{ label: '关闭', value: 0 }, { label: '开启', value: 1 }]) : [] }))
  }
  function submit(event: React.FormEvent) {
    event.preventDefault()
    const all = [...definition.functions.filter(f => f.id !== field.id), field]
    const index = all.length - 1
    const issue = validateDefinition({ ...definition, functions: all }).find(i => i.path.startsWith(`functions.${index}`))
    if (issue) { setError(issue.message); return }
    onSave(field)
  }
  return <Modal title={original ? '编辑功能' : '添加产品功能'} subtitle="定义数据接口；具体业务行为将在 产品源码模板中实现。" onClose={onClose} wide>
    <form onSubmit={submit}><div className="modal-body">
      <div className="form-grid"><Field label="功能名称"><input autoFocus value={field.name} onChange={e => update('name', e.target.value)} maxLength={80}/></Field><Field label="功能标识" hint="用于生成 C 接口，使用小写字母、数字及下划线。"><input className="mono" value={field.key} onChange={e => update('key', e.target.value)} maxLength={48}/></Field></div>
      <div className="form-grid"><Field label="数据类型"><Select value={field.kind} onChange={v => changeKind(v as Kind)}>{Object.entries(kindNames).map(([key, label]) => <option key={key} value={key}>{label}</option>)}</Select></Field><Field label="访问权限"><Select value={field.access} onChange={v => update('access', v as Access)}>{Object.entries(accessNames).map(([key, label]) => <option key={key} value={key}>{label}</option>)}</Select></Field></div>
      {field.kind === 'int' && <div className="form-grid three"><Field label="最小值"><input type="number" step="1" value={field.min} onChange={e => update('min', Number(e.target.value))}/></Field><Field label="最大值"><input type="number" step="1" value={field.max} onChange={e => update('max', Number(e.target.value))}/></Field><Field label="步进"><input type="number" min="1" step="1" value={field.step} onChange={e => update('step', Number(e.target.value))}/></Field></div>}
      {field.kind === 'enum' && <div className="field"><span>枚举选项</span><div className="enum-list">{field.options.map((option, index) => <div className="enum-row" key={index}><input aria-label={`枚举 ${index + 1} 名称`} value={option.label} onChange={e => update('options', field.options.map((o, i) => i === index ? { ...o, label: e.target.value } : o))}/><input aria-label={`枚举 ${index + 1} 数值`} type="number" step="1" value={option.value} onChange={e => update('options', field.options.map((o, i) => i === index ? { ...o, value: Number(e.target.value) } : o))}/><button type="button" className="icon-button" aria-label={`移除枚举 ${index + 1}`} onClick={() => update('options', field.options.filter((_, i) => i !== index))}><Trash2 size={15}/></button></div>)}<button type="button" className="text-button" onClick={() => update('options', [...field.options, { label: '新选项', value: Math.max(-1, ...field.options.map(o => o.value)) + 1 }])}><Plus size={14}/>添加选项</button></div></div>}
      {field.kind === 'string' && <Field label="最大字符数"><input type="number" min="1" max="4096" value={field.maxLength} onChange={e => update('maxLength', Number(e.target.value))}/></Field>}
      <div className="form-grid"><Field label="默认值">{field.kind === 'bool' ? <div className="boolean-field"><Switch checked={Boolean(field.default)} onChange={v => update('default', v)} label="默认值"/><span>{field.default ? '开启 / true' : '关闭 / false'}</span></div> : field.kind === 'enum' ? <Select value={String(field.default)} onChange={v => update('default', Number(v))}>{field.options.map((o, i) => <option key={i} value={o.value}>{o.label} ({o.value})</option>)}</Select> : <input type={field.kind === 'int' ? 'number' : 'text'} value={String(field.default)} onChange={e => update('default', field.kind === 'int' ? Number(e.target.value) : e.target.value)}/>}</Field><Field label="单位（可选）"><input placeholder="例如 %、rpm、℃" value={field.unit} maxLength={20} onChange={e => update('unit', e.target.value)}/></Field></div>
      <Field label="功能说明（可选）"><textarea rows={2} value={field.description} maxLength={1000} onChange={e => update('description', e.target.value)} placeholder="描述这个功能的用途"/></Field>
      {error && <p className="form-error" role="alert">{error}</p>}
    </div><div className="modal-footer"><button type="button" className="button" onClick={onClose}>取消</button><button className="button primary" type="submit">{original ? '保存功能' : '添加功能'}</button></div></form>
  </Modal>
}

export function ParameterEditor({ definition, original, onSave, onClose }: { definition: Definition; original: Parameter | null; onSave: (value: Parameter) => void; onClose: () => void }) {
  const [field, setField] = useState<Parameter>(() => original ? { ...original } : { id: crypto.randomUUID(), key: `parameter_${definition.parameters.length + 1}`, name: '新参数', value: 10 })
  const [error, setError] = useState('')
  function submit(event: React.FormEvent) {
    event.preventDefault()
    const parameters = [...definition.parameters.filter(p => p.id !== field.id), field]
    const issue = validateDefinition({ ...definition, parameters }).find(i => i.path.startsWith(`parameters.${parameters.length - 1}`))
    if (issue) { setError(issue.message); return }
    onSave(field)
  }
  return <Modal title={original ? '编辑产品参数' : '添加产品参数'} subtitle="参数随产品定义交付；其含义由后续源码模板约定。" onClose={onClose}><form onSubmit={submit}><div className="modal-body">
    <Field label="参数名称"><input autoFocus value={field.name} maxLength={80} onChange={e => setField({ ...field, name: e.target.value })}/></Field>
    <Field label="参数标识"><input className="mono" value={field.key} maxLength={48} onChange={e => setField({ ...field, key: e.target.value })}/></Field>
    <Field label="数据类型"><Select value={typeof field.value} onChange={v => setField({ ...field, value: v === 'boolean' ? false : v === 'number' ? 0 : '' })}><option value="number">整数</option><option value="boolean">布尔</option><option value="string">字符串</option></Select></Field>
    <Field label="参数值">{typeof field.value === 'boolean' ? <Switch checked={field.value} label="参数值" onChange={value => setField({ ...field, value })}/> : <input type={typeof field.value === 'number' ? 'number' : 'text'} value={field.value} onChange={e => setField({ ...field, value: typeof field.value === 'number' ? Number(e.target.value) : e.target.value })}/>}</Field>
    {error && <p className="form-error" role="alert">{error}</p>}
  </div><div className="modal-footer"><button type="button" className="button" onClick={onClose}>取消</button><button className="button primary" type="submit">保存参数</button></div></form></Modal>
}
