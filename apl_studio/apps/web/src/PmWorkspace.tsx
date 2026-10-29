import { StandardOptions } from './StandardOptions'
import { useEffect, useRef, useState } from 'react'
import { CheckCircle2, Play, Download, Info, Loader2, ArrowRight, Power } from 'lucide-react'
import type { Definition, Product, Value } from '@apl-studio/model'
import type { AcceptanceReport, BuildJob, TemplateDescriptor } from '@apl-studio/model/template'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { validateTemplateConfiguration } from '@apl-studio/model/template'
import { Field, Select, Switch } from './components'
import { api, download } from './api'
import { SdkDelivery } from './SdkDelivery'
import type { Tab } from './Workspace'

const gears = ['睡眠风速', '一档', '二档', '三档', '自动', '极速']
const panelNames: Record<string, string> = { Purify: '净化键', Humidity: '湿度键', Timer: '定时键', Light: '灯光键' }
const steps: { id: Tab; name: string }[] = [
  { id: 'overview', name: '产品方案' }, { id: 'functions', name: '交互规则' },
  { id: 'simulation', name: '体验与验收' }, { id: 'delivery', name: '交付工程师' },
]
function valuesText(values: Record<string, Value>) {
  return Object.entries(values).map(([key, value]) => {
    if (key === 'output') return '实际风速' + gears[Number(value)]
    if (key === 'requested') return '所选模式' + gears[Number(value)]
    if (key === 'pm') return `PM2.5 ${value}`
    if (key === 'sleep') return value ? '睡眠模式' : '非睡眠模式'
    if (key === 'autoLamp') return '自动灯' + (value ? '亮' : '灭')
    if (key === 'sleepLamp') return '睡眠灯' + (value ? '亮' : '灭')
    if (key === 'fan') return '风机' + (value ? '运行' : '停止')
    if (key === 'brightness') return '屏幕亮度' + (Number(value) === 255 ? '正常' : value)
    return String(value)
  }).join('，')
}

export function PmWorkspace({ product, draft, setDraft, tab, setTab, dirty, onSave, onExport, template }: {
  product: Product; draft: Definition; setDraft: (definition: Definition) => void; tab: Tab; setTab: (tab: Tab) => void
  dirty: boolean; onSave: () => void; onExport: (json?: boolean) => void; template: TemplateDescriptor
}) {
  const [report, setReport] = useState<AcceptanceReport | null>(null), [current, setCurrent] = useState(false)
  const [jobs, setJobs] = useState<BuildJob[]>([]), [busy, setBusy] = useState(false), [error, setError] = useState('')
  const issues = validateTemplateConfiguration(draft, template)
  const selected = steps.some(step => step.id === tab) ? tab : 'overview'
  const standard = ['air-care-standard-v1', 'air-care-standard-v2'].includes(template.workflow ?? '')
  const turbo = draft.parameters.find(p => p.key === 'turbo_enabled')?.value === true
  const initial = draft.functions.find(f => f.key === 'fan_speed')!
  const parameter = (key: string, value: Value) => setDraft({ ...draft, parameters: draft.parameters.map(p => p.key === key ? { ...p, value } : p) })
  useEffect(() => {
    let stopped = false, timer: ReturnType<typeof setTimeout>
    const refresh = async () => {
      try {
        const [result, all] = await Promise.all([api.acceptance(product.id), api.jobs(product.id)])
        if (stopped) return
        setReport(result.report); setCurrent(result.current); setJobs(all)
        timer = setTimeout(refresh, result.report?.status === 'running' || all.some(j => ['queued', 'building'].includes(j.status)) ? 700 : 2500)
      } catch (e) { if (!stopped) { setError((e as Error).message); timer = setTimeout(refresh, 3000) } }
    }
    void refresh()
    return () => { stopped = true; clearTimeout(timer) }
  }, [product.id, product.revision])
  const bound = current && report?.productId === product.id && report.revision === product.revision && report.template.fingerprint === template.fingerprint
  const passed = !dirty && bound && report?.status === 'passed'
  const latest = jobs.find(j => j.revision === product.revision && j.template.fingerprint === template.fingerprint && j.acceptance?.fingerprint === report?.fingerprint)
  const generating = latest && ['queued', 'building'].includes(latest.status)
  async function run() {
    setBusy(true); setError('')
    try { const result = await api.runAcceptance(product); setReport(result); setCurrent(true) }
    catch (e) { setError((e as Error).message) }
    finally { setBusy(false) }
  }
  async function generate() {
    setBusy(true); setError('')
    try { const job = await api.generate(product); setJobs(all => [job, ...all.filter(j => j.id !== job.id)]) }
    catch (e) { setError((e as Error).message) }
    finally { setBusy(false) }
  }
  async function downloadJob(job: BuildJob) {
    try { await download('/api/sdk/jobs/' + job.id + '/download', job.filename ?? '工程师交付包.zip') }
    catch (e) { setError((e as Error).message) }
  }
  return <div className="pm-workspace">
    <div className="project-heading"><div><div className="eyebrow">产品经理工作台</div><h1>{draft.name || '我的空气净化产品'}</h1><p>用标准方案定义产品，在真实业务程序中验证，再交付给工程师。</p></div><span className={'badge ' + (passed ? 'green' : 'purple')}>{dirty ? '有未保存修改' : passed ? '本次方案已验收' : '待完成验收'}</span></div>
    <nav className="project-tabs pm-steps" aria-label="产品经理步骤">{steps.map((step, i) => <button key={step.id} className={selected === step.id ? 'active' : ''} onClick={() => setTab(step.id)}><span className="step-index">{i + 1}</span>{step.name}</button>)}</nav>
    {dirty && <div className="inline-alert warning"><Info size={16}/><span>方案已修改，旧验收不代表当前草稿。保存后重新运行验收。</span><button className="button small" disabled={!!issues.length} onClick={onSave}>保存方案</button></div>}
    {(error || issues.length > 0) && <div className="inline-alert warning" role="alert">{error || issues[0]}</div>}
    {selected === 'overview' && standard && <StandardOptions draft={draft} template={template} setDraft={setDraft}/>}
    {selected === 'overview' && <div className="overview-grid"><section className="card"><h2>我的产品方案</h2><div className="form-grid"><Field label="产品名称"><input value={draft.name} maxLength={80} onChange={e => setDraft({ ...draft, name: e.target.value })}/></Field><Select label="开机档位" value={String(initial.default)} onChange={v => setDraft({ ...draft, functions: draft.functions.map(f => f.key === 'fan_speed' ? { ...f, default: Number(v) } : f) })}>{gears.slice(0, 5).map((name, i) => <option key={i} value={i}>{name}</option>)}</Select><Field label="滤芯维护提醒（累计运行小时）"><input type="number" min="1" max="1000000" value={Number(draft.parameters.find(p => p.key === 'filter_life_h')?.value)} onChange={e => parameter('filter_life_h', Number(e.target.value))}/></Field></div><Field label="产品说明"><textarea rows={3} value={draft.description} onChange={e => setDraft({ ...draft, description: e.target.value })}/></Field></section><section className="card"><h2><CheckCircle2 size={18}/> 已选标准能力集</h2><ul className="pm-capabilities">{(standard ? template.features.filter(f => draft.functions.some(v => v.key === f.key)).map(f => f.name) : ["风机手动档、自动档与睡眠模式", "PM2.5采样与标准自动调速", "电源、定时、灯光及模板标准显示", "滤芯维护提醒"]).map(name => <li key={name}>{name}</li>)}</ul><p className="pm-scope">{standard ? '三种产品按能力裁剪。UV/负离子保持复合接口，工程师需确认硬件、安全联锁及报告中列出的待确认规则。' : '净化模板保留UV/负离子复合接口，当前不提供两者的独立选配。此试点不含加湿，工程师需按清单确认硬件与安全联锁。'}</p><button className="button primary full" onClick={() => setTab('functions')}>继续定义交互<ArrowRight size={15}/></button></section></div>}
    {selected === 'functions' && <div className="stack"><section className="card"><div className="section-heading"><div><h2>点选风速键的长按动作</h2><p>短按始终采用标准档位循环；长按动作可在现有实现范围内选择。</p></div><Switch label="风速键长按进入极速" checked={turbo} onChange={value => parameter('turbo_enabled', value)}/></div><div className="pm-panel-map"><div className="pm-panel-key">风速键</div><div><strong>短按</strong><p>睡眠 → 一档 → 二档 → 三档 → 自动</p></div><div><strong>长按3秒</strong><p>{turbo ? '进入/退出极速' : '不改变当前档位'}</p></div></div></section>{standard ? <section className="card"><h2>标准规则与验收范围</h2><p>净化、加湿和一体机使用各自能力组合；按键、缺水恢复窗口、风干需求和自动调速由同一C库运行。</p><p>灯光长按无附加动作；睡眠熄屏电源键直接执行。一体机Turbo加湿键切至Auto；缺水保持净化，纯加湿缺水5分钟后风干。</p><p className="pm-scope">报告逐项列出实际检查；三项湿度冲突及其他项目待办不计为已验收。</p></section> : <section className="card"><h2>标准规则：空气质量自动调速</h2><p>选择自动模式后按下表运行。规则采用已确认标准，本轮以选择规则方案的方式使用。</p><table className="pm-rule-table"><thead><tr><th>PM2.5（μg/m³）</th><th>实际风速</th><th>保持状态</th></tr></thead><tbody>{[['≤35', '睡眠风速'], ['36～75', '一档'], ['76～150', '二档'], ['>150', '三档']].map(([range, gear]) => <tr key={range}><td>{range}</td><td>{gear}</td><td>自动模式</td></tr>)}</tbody></table><p className="pm-scope">自动档的最低风速不等于睡眠模式：自动灯保持亮起，屏幕不因自动降速熄灭。显示布局及其余动作沿用模板标准；此轮未提供任意按键映射或自由规则编排。</p></section>}<button className="button primary" onClick={() => setTab('simulation')}>前往体验与验收<ArrowRight size={15}/></button></div>}
    {selected === 'simulation' && <div className="stack"><PmSimulator key={product.id} product={product} dirty={dirty}/><section className="card"><div className="section-heading"><div><h2>一键运行标准验收场景</h2><p>{standard ? '服务器运行真实C，按当前能力检查按键、缺水/风干、自动调速及联锁。' : '服务器运行真实C，自动检查PM边界、自动/睡眠语义和当前长按配置。'}</p></div><button className="button primary" disabled={dirty || !!issues.length || busy || report?.status === 'running'} onClick={() => void run()}>{busy || report?.status === 'running' ? <Loader2 className="spin" size={16}/> : <Play size={16}/>}运行场景验收</button></div>
      {report && <><div className={'pm-verdict ' + (passed ? 'passed' : '')}>{!bound || dirty ? '此报告对应旧方案，请保存并重新验收' : report.status === 'running' ? '正在运行真实C场景…' : report.status === 'passed' ? '当前方案：全部场景通过' : '验收未通过，请查看差异'}</div>{report.diagnostics && <p role="alert">{report.diagnostics}</p>}<div className="table-scroll"><table className="pm-rule-table"><thead><tr><th>检查项</th><th>预期</th><th>实际</th><th>结果</th></tr></thead><tbody>{report.checks.map((c, i) => <tr key={i}><td>{c.name}</td><td>{valuesText(c.expected)}</td><td>{valuesText(c.actual)}</td><td>{c.passed ? '通过' : '未通过'}</td></tr>)}</tbody></table></div></>}
      {!report && <p className="empty-events">保存方案后运行一次。报告将与本次方案绑定；修改方案后需要重新验收。</p>}<p className="pm-scope">场景覆盖范围明确列在报告中，尚不替代目标MCU和真实硬件验收。</p></section>{passed && <button className="button primary" onClick={() => setTab('delivery')}>验收已通过，准备交付<ArrowRight size={15}/></button>}</div>}
    {selected === 'delivery' && <div className="stack"><section className="card"><div className="section-heading"><div><h2>生成工程师交付包</h2><p>包含代码库、配置、中文功能交互说明、验收结果和硬件适配清单。</p></div><button className="button primary" disabled={!passed || busy || !!generating} onClick={() => void generate()}>{generating ? <Loader2 className="spin" size={16}/> : <Play size={16}/>}生成并验证交付包</button></div><div className="pm-verdict">{!passed ? '先保存并通过当前方案的场景验收' : generating ? '正在编译与执行交付包消费验证…' : latest?.status === 'succeeded' ? '场景验收、编译与消费验证均已通过，可以交付' : latest?.status === 'failed' ? '交付验证未通过，请展开工程信息查看诊断' : '当前方案已验收，等待生成交付包'}</div>{passed && latest?.status === 'succeeded' && <button className="button primary full" onClick={() => void downloadJob(latest)}><Download size={17}/>下载工程师交付包</button>}<ul className="pm-capabilities"><li>标准C源码、产品接口与配置</li><li>中文功能与交互说明</li><li>本次修订的真实C场景验收报告</li><li>硬件适配待办、示例及验证程序</li><li>可追溯版本、来源及文件校验</li></ul><p>工程师实现实际硬件接线、驱动标定和目标环境验收，业务规则继续由生成库负责。</p></section>{jobs.some(j => j.status === 'succeeded') && <section className="card"><h2>历史交付包</h2>{jobs.filter(j => j.status === 'succeeded').map(j => <div className="build-row" key={j.id}><span>已验证方案 · 修订{j.revision}</span><button className="button small" onClick={() => void downloadJob(j)}>下载历史包</button></div>)}</section>}<details className="card"><summary>工程文件、构建诊断与历史详情</summary><SdkDelivery product={product} definition={draft} template={template} dirty={dirty} onSave={onSave} onExport={onExport}/></details></div>}
    <details className="pm-technical"><summary>查看技术追溯信息</summary><p>产品标识：{draft.code}；已保存修订：{product.revision}；模板：{template.id}@{template.version}；源码：{template.sourceVersion}</p><p>模板指纹：{template.fingerprint}</p>{report && <p>验收：{report.id}；驱动指纹：{report.driverFingerprint}</p>}</details>
    {dirty && <div className="pm-save-bar"><span>方案有修改</span><button className="button primary" disabled={!!issues.length} onClick={onSave}>保存方案</button></div>}
  </div>
}

function PmSimulator({ product, dirty }: { product: Product; dirty: boolean }) {
  const [session, setSession] = useState<SimulationSession | null>(null), [busy, setBusy] = useState(false), [error, setError] = useState('')
  const id = useRef('')
  const alive = useRef(true)
  useEffect(() => {
    alive.current = true
    const stop = () => { if (id.current) { void fetch('/api/simulation/sessions/' + id.current, { method: 'DELETE', keepalive: true }).catch(() => {}); id.current = '' } }
    window.addEventListener('pagehide', stop)
    return () => { alive.current = false; window.removeEventListener('pagehide', stop); stop() }
  }, [])
  useEffect(() => {
    if (session?.status !== 'building') return
    let stopped = false, checking = false
    const timer = setInterval(() => {
      if (checking) return
      checking = true
      void (async () => {
        let next = await api.simulation(session.id)
        if (next.status === 'ready' && next.snapshot?.ms === 0) next = await api.simulate(session.id, { type: 'advance', ms: 2100 })
        if (!stopped) setSession(next)
      })().catch(e => { if (!stopped) setError(e.message) }).finally(() => { checking = false })
    }, 500)
    return () => { stopped = true; clearInterval(timer) }
  }, [session?.id, session?.status])
  const ready = session?.status === 'ready', snapshot = session?.snapshot
  const disabled = busy || !ready
  async function start() {
    setBusy(true); setError('')
    try { if (id.current) await api.stopSimulation(id.current); const next = await api.startSimulation(product, false); if (!alive.current) { await api.stopSimulation(next.id); return }; id.current = next.id; setSession(next) }
    catch (e) { setError((e as Error).message) }
    finally { setBusy(false) }
  }
  async function input(key: string, value: Value) {
    if (!session) return
    setBusy(true); setError('')
    try { await api.simulate(session.id, { type: 'input', key, value }); setSession(await api.simulate(session.id, { type: 'advance', ms: key === 'pm25' ? 1000 : 10 })) }
    catch (e) { setError((e as Error).message) }
    finally { setBusy(false) }
  }
  return <section className="card"><div className="section-heading"><div><h2>体验我的产品</h2><p>按键和环境输入驱动实际C程序，下面显示真实输出。</p></div><button className="button" disabled={busy || dirty || session?.status === 'building'} onClick={() => void start()}>{session?.status === 'building' ? '正在准备…' : session ? '重新开始体验' : '开始体验'}</button></div>{(dirty || session && session.revision !== product.revision) && <p className="inline-alert warning">当前体验仍使用旧方案；保存后重新开始体验，才会应用新配置。</p>}{error && <p role="alert">{error}</p>}{session?.status === 'failed' && <p role="alert">{session.diagnostics}</p>}{session && <><div className="pm-preview"><div className="pm-device-display"><strong>{['待机', '运行中', '关机风干', '产测'][Number(snapshot?.internal.run_state)] ?? '等待输入'}</strong><span>实际风速：{snapshot && !snapshot.outputs.fan ? '风机已停止' : gears[Number(snapshot?.outputs.fan_gear)] ?? '等待输入'}</span><span>所选模式：{gears[Number(snapshot?.internal.fan_speed)] ?? '等待输入'}</span></div><div className="pm-device-buttons"><button className="button" disabled={disabled} onClick={() => void input('Power', 'short')}><Power size={15}/>电源</button><button className="button" disabled={disabled || snapshot?.internal.run_state !== 1} onClick={() => void input('Speed', 'short')}>风速短按</button><button className="button" disabled={disabled || snapshot?.internal.run_state !== 1} onClick={() => void input('Speed', 'long')}>风速长按3秒</button></div></div>{product.definition.functions.some(f => f.key === "pm25") && <div className="pm-environment"><strong>改变空气质量</strong>{[35, 36, 75, 76, 150, 151].map(pm => <button className="button small" key={pm} disabled={disabled} onClick={() => void input('pm25', pm)}>PM2.5 {pm}</button>)}</div>}{["0.3.0", "0.4.0"].includes(product.definition.template?.version ?? "") && <div className="form-grid">{session.descriptor.controls.filter(c => !["Power", "Speed", "pm25"].includes(c.key)).map(c => <Field key={c.key} label={panelNames[c.key] ?? c.name}>{c.kind === "gesture" ? <><button className="button small" disabled={disabled} aria-label={(panelNames[c.key] ?? c.name) + "短按"} onClick={() => void input(c.key, "short")}>短按</button><button className="button small" disabled={disabled} aria-label={(panelNames[c.key] ?? c.name) + "长按"} onClick={() => void input(c.key, "long")}>长按3秒</button></> : c.kind === "enum" ? <select disabled={disabled} value={String(snapshot?.values[c.key] ?? c.default)} onChange={e => void input(c.key, Number(e.target.value))}>{c.options?.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}</select> : c.kind === "bool" ? <input type="checkbox" disabled={disabled} checked={!!(snapshot?.values[c.key] ?? c.default)} onChange={e => void input(c.key, e.target.checked)}/> : <input type="number" disabled={disabled} min={c.min} max={c.max} step={c.step} value={Number(snapshot?.values[c.key] ?? c.default)} onChange={e => void input(c.key, Number(e.target.value))}/>}</Field>)}</div>}{["0.3.0", "0.4.0"].includes(product.definition.template?.version ?? "") && snapshot && <div className="table-scroll"><table className="pm-rule-table"><caption>实际C输出与状态</caption><tbody>{session.descriptor.outputs.map(o => <tr key={o.key}><th>{o.name}</th><td>{o.boolean ? snapshot.outputs[o.key] ? "开启" : "关闭" : o.options?.find(v => v.value === snapshot.outputs[o.key])?.label ?? String(snapshot.outputs[o.key])}</td></tr>)}<tr><th>目标湿度</th><td>{snapshot.internal.humidity_target || "OF"}</td></tr><tr><th>风干剩余秒数</th><td>{snapshot.internal.dry_left_s}</td></tr></tbody></table></div>}<p className="pm-scope">体验使用已保存的方案。若配置已保存更新，需要重新开始体验；未选择自动档时，改变空气质量不会改变手动风速。</p></>}</section>
}
