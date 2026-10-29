import { useState, type ReactNode } from 'react'
import { ArrowRight, CheckCircle2 } from 'lucide-react'
import type { Definition, Product } from '@apl-studio/model'
import { definitionFromTemplate, type TemplateDescriptor } from '@apl-studio/model/template'
import { Field, Modal } from './components'

export function CreateProductFlow({ templates, products, busy, onClose, onCreate, engineering }: {
  templates: TemplateDescriptor[]; products: Product[]; busy: boolean; onClose: () => void
  onCreate: (definition: Definition) => Promise<void>; engineering: () => ReactNode
}) {
  const [advanced, setAdvanced] = useState(false), [name, setName] = useState('我的空气净化产品'), [error, setError] = useState('')
  const standard = templates.find(t => t.workflow === 'purification-pm-v1')
  if (!standard || advanced) return engineering()
  async function create(event: React.FormEvent) {
    event.preventDefault(); setError('')
    const definition = definitionFromTemplate(standard!, products.map(p => p.definition.code))
    definition.name = name.trim()
    if (!definition.name) { setError('请为产品起一个名字'); return }
    try { await onCreate(definition) } catch (e) { setError((e as Error).message) }
  }
  return <Modal title="创建空气净化产品" subtitle="选择标准方案，从产品定义走到工程师交付。" onClose={onClose}><form onSubmit={create}><div className="modal-body"><Field label="产品名称"><input autoFocus maxLength={80} value={name} onChange={e => setName(e.target.value)}/></Field><div className="pm-start-card"><CheckCircle2 size={22}/><div><strong>空气净化标准方案</strong><p>风机档位、空气质量自动调速、风速键交互、真实C场景验收与中文交付包。</p><p className="pm-scope">含现有UV/负离子复合接口，暂不独立选配；不含加湿。工程师按清单完成硬件接入。</p></div></div>{error && <p role="alert">{error}</p>}<button className="text-button" type="button" onClick={() => setAdvanced(true)}>其他产品与工程配置</button></div><div className="modal-footer"><button type="button" className="button" disabled={busy} onClick={onClose}>取消</button><button className="button primary" disabled={busy} type="submit">创建并定义方案<ArrowRight size={15}/></button></div></form></Modal>
}
