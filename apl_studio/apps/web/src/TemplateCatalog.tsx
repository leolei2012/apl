import { Boxes, ArrowRight, CheckCircle2, Info } from 'lucide-react'
import type { TemplateDescriptor } from '@apl-studio/model/template'

export function TemplateCatalog({ templates, busy, onCreate }: { templates: TemplateDescriptor[]; busy: boolean; onCreate: (template: TemplateDescriptor) => void }) {
  return <><div className="page-heading"><div><div className="eyebrow">SOURCE TEMPLATES</div><h1>从能力，到代码<span className="heading-dot">.</span></h1><p>选择源码模板，配置产品，再交付可移植的产品 SDK。</p></div><span className="badge purple">{templates.length} 个可用版本</span></div>
    <div className="stack">{templates.map(template => <section className="card" key={template.id + template.version}><div className="section-heading"><div><h2><Boxes size={22}/> {template.name}</h2><p>{template.description}</p></div><button className="button primary" disabled={busy} onClick={() => onCreate(template)}>用此模板创建产品<ArrowRight size={15}/></button></div><div className="template-meta"><code>{template.id}@{template.version}</code><span>源码 {template.sourceVersion}</span><span>定义格式 v{template.definitionSchemaVersion}</span><code title={template.fingerprint}>SHA-256 {template.fingerprint.slice(0, 16)}</code></div>
      <div className="template-feature-grid">{template.features.map(feature => <div className="feature-option" key={feature.key}><div className="control-heading"><strong>{feature.name}</strong>{feature.required && <span className="badge">必选</span>}</div><p>{feature.description}</p><code>{feature.key}</code></div>)}</div>
      <div className="table-footer"><CheckCircle2 size={15}/><span>{template.parameters.length} 项参数 · {template.ports.length} 项移植接口 · 生成后执行本机 C11 编译与基础消费验证</span></div></section>)}</div>
    {!templates.length && <section className="card empty-inline"><Boxes size={36}/><h2>还没有可用源码模板</h2><p>将版本化模板放入 apl_sdk_template 目录后重启服务。</p></section>}
    <div className="note-card"><Info size={18}/><div><strong>平台与模板分别演进</strong><p>界面从模板描述读取能力、参数和依赖。模板绑定精确版本；升级需要主动选择新版本。下载的 SDK 包含源码、配置、产品接口、移植入口与验证记录。</p></div></div>
  </>
}
