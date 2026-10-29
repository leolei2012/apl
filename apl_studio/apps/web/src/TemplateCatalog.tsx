import { Boxes, ArrowRight, CheckCircle2, Info } from 'lucide-react'
import type { TemplateDescriptor } from '@apl-studio/model/template'

export function TemplateCatalog({ templates, busy, onCreate }: { templates: TemplateDescriptor[]; busy: boolean; onCreate: (template: TemplateDescriptor) => void }) {
  const recommended = templates.find(t => t.workflow === 'purification-pm-v1')
  const cards = (items: TemplateDescriptor[]) => items.map(template => <section className="card" key={template.id + template.version}>
    <div className="section-heading"><div><h2><Boxes size={22}/> {template.name}</h2><p>{template.description}</p></div><button className="button primary" disabled={busy} onClick={() => onCreate(template)}>用此方案创建产品<ArrowRight size={15}/></button></div>
    <div className="template-feature-grid">{template.features.filter(f => f.available !== false).map(feature => <div className="feature-option" key={feature.key}><div className="control-heading"><strong>{feature.name}</strong>{feature.required && <span className="badge">已包含</span>}</div><p>{feature.description}</p></div>)}</div>
    <details className="pm-technical"><summary>版本与工程追溯信息</summary><div className="template-meta"><code>{template.id}@{template.version}</code><span>源码 {template.sourceVersion}</span><span>定义格式 v{template.definitionSchemaVersion}</span><code title={template.fingerprint}>SHA-256 {template.fingerprint.slice(0, 16)}</code></div><p>{template.parameters.length}项参数，{template.ports.length}项移植接口</p></details>
  </section>)
  return <><div className="page-heading"><div><div className="eyebrow">产品标准方案</div><h1>定义产品，验证交互，交付代码<span className="heading-dot">.</span></h1><p>使用已实现的标准能力，完成真实业务验收与工程师交付。</p></div></div>
    {recommended && <div className="stack">{cards([recommended])}</div>}
    {recommended ? <details className="card pm-other-templates"><summary>其他产品与历史工程模板</summary><div className="stack">{cards(templates.filter(t => t !== recommended))}</div></details> : <div className="stack">{cards(templates)}</div>}
    {!templates.length && <section className="card empty-inline"><Boxes size={36}/><h2>还没有可用标准方案</h2><p>请工程师接入经过验证的源码模板。</p></section>}
    <div className="note-card"><Info size={18}/><div><strong>标准能力与代码保持一致</strong><p>产品经理选择已支持的能力和交互。平台固定配置、验收与源码版本；工程师在交付包中查看接口和验证记录。</p><CheckCircle2 size={15}/></div></div>
  </>
}
