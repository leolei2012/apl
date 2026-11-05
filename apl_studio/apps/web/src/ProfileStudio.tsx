import { firmwareBuild, profileFieldCompiled } from '@apl-studio/model/firmware'
import { ProfileFirmwareBuild } from './ProfileFirmwareBuild'
import { ProfileKeyMapping } from './ProfileKeyMapping'
import { profileKeyMapping } from '@apl-studio/model/key-mapping'
import { useEffect, useRef, useState } from 'react'
import { ArrowLeft, ArrowRight, Check, ChevronDown, Download, Fan, FileSpreadsheet, GitBranch, Layers, Loader2, Play, Plus, Settings2, SlidersHorizontal, Wind, X } from 'lucide-react'
import type { Definition, Product } from '@apl-studio/model'
import { PROFILE_ID, bindCurrentProfile, featureSummary, newProfile, productFeatures, profilePid, profileValues, setProductFeatures, setProfile, sourceFingerprint, validateProfile, type ProfileCatalog, type ProductFeatures } from '@apl-studio/model/profile'
import type { SimulationSession } from '@apl-studio/model/simulation'
import { api, download } from './api'
import { AirCarePanel } from './AirCarePanel'
import { ProfileRuntimeParameters } from './ProfileRuntimeParameters'
import './ProfileStudio.css'
import './ParameterGuide.css'
import './KeyGestureGuide.css'

const initialFeatures: ProductFeatures = { purify: true, humidify: false }
const gears = ['睡眠', '一档', '二档', '三档', '自动', '极速']
const steps = [{ name: '配置产品', description: '选择功能与参数', icon: SlidersHorizontal }, { name: '仿真体验', description: '试一试设备如何工作', icon: Play }, { name: '导出交付', description: '配置表与固件源码', icon: FileSpreadsheet }]

export default function ProfileStudio() {
  const [catalog, setCatalog] = useState<ProfileCatalog | null>(null), [products, setProducts] = useState<Product[]>([])
  const [product, setProduct] = useState<Product | null>(null), [draft, setDraft] = useState<Definition | null>(null)
  const [step, setStep] = useState(0), [creating, setCreating] = useState(false), [name, setName] = useState(''), [features, setFeatures] = useState<ProductFeatures>(initialFeatures)
  const [busy, setBusy] = useState(false), [error, setError] = useState(''), [loading, setLoading] = useState(true)
  const lock = useRef(false), currentDraft = useRef(draft), currentProduct = useRef(product)
  currentDraft.current = draft; currentProduct.current = product
  const dirty = !!product && JSON.stringify(product.definition) !== JSON.stringify(draft)
  const issues = draft && catalog ? validateProfile(draft, catalog) : []
  const values = draft ? profileValues(draft) : {}
  async function refresh() { setProducts((await api.list()).filter(p => p.definition.template?.id === PROFILE_ID)) }
  useEffect(() => {
    let alive = true
    Promise.all([fetch('/api/profile/catalog').then(async r => { const data = await r.json(); if (!r.ok) throw new Error(data.error); return data as ProfileCatalog }), api.list()])
      .then(([c, all]) => { if (alive) { setCatalog(c); setProducts(all.filter(p => p.definition.template?.id === PROFILE_ID)); const match = location.hash.match(/^#device\/([\w-]+)\/([0-2])$/); const selected = match && all.find(p => p.id === match[1] && p.definition.template?.id === PROFILE_ID); if (selected) { setProduct(selected); setDraft(structuredClone(selected.definition)); setStep(Number(match![2])) } } })
      .catch(e => alive && setError(e.message)).finally(() => alive && setLoading(false))
    return () => { alive = false }
  }, [])
  useEffect(() => {
    const unload = (e: BeforeUnloadEvent) => { if (currentProduct.current && JSON.stringify(currentProduct.current.definition) !== JSON.stringify(currentDraft.current)) { e.preventDefault(); e.returnValue = '' } }
    window.addEventListener('beforeunload', unload); return () => window.removeEventListener('beforeunload', unload)
  }, [])
  async function operation<T,>(fn: () => Promise<T>): Promise<T | undefined> {
    if (lock.current) return
    lock.current = true; setBusy(true); setError('')
    try { return await fn() } catch (e) { setError((e as Error).message) } finally { lock.current = false; setBusy(false) }
  }
  async function save(): Promise<Product | undefined> {
    if (!product || !draft || !catalog) return
    if (issues.length) { setError(issues[0]); return }
    if (!dirty) return product
    return operation(async () => { const saved = await api.update(product, draft); setProduct(saved); setDraft(structuredClone(saved.definition)); await refresh(); return saved })
  }
  async function go(next: number) { if (busy) return; if (dirty && !await save()) return; setError(''); setStep(next); if (product) history.replaceState(null, '', `#device/${product.id}/${next}`) }
  async function open(p: Product | null) {
    if (busy) return
    if (dirty && !await save()) return
    setProduct(p); setDraft(p ? structuredClone(p.definition) : null); setStep(0); setError(''); history.replaceState(null, '', p ? `#device/${p.id}/0` : '#products')
  }
  async function create() {
    if (!catalog || !name.trim() || !features.purify && !features.humidify) return
    await operation(async () => { const p = await api.create(newProfile(catalog, name.trim(), features)); await refresh(); setProduct(p); setDraft(structuredClone(p.definition)); setStep(0); setCreating(false); setName(''); history.replaceState(null, '', `#device/${p.id}/0`) })
  }
  const update = (key: string, value: number) => setDraft(d => d && setProfile(d, key, value))
  const toggle = (field: string, bit: number, on: boolean) => update(field, on ? values[field] | bit : values[field] & ~bit)
  return <div className="profile-studio">
    <aside className="ps-sidebar"><a className="ps-brand" href="/" onClick={e => { e.preventDefault(); void open(null) }}><span><Layers size={24}/></span><div>APL Studio<small>产品参数工作台</small></div></a>
      <button className={'ps-nav ' + (!product ? 'selected' : '')} disabled={busy} onClick={() => void open(null)}><Wind size={18}/>我的产品<span>{products.length}</span></button>
      <div className="ps-nav-label">最近编辑</div><div className="ps-recents">{products.slice(0, 8).map(p => <button disabled={busy} key={p.id} className={product?.id === p.id ? 'selected' : ''} onClick={() => void open(p)}><span className="ps-product-dot"/>{p.definition.name}</button>)}</div>
      <div className="ps-sidebar-bottom"><div className="ps-how"><strong>把产品想法，变成参数。</strong><p>配置功能，体验效果，<br/>导出一份设备参数表。</p></div><a href="/engineering"><Settings2 size={16}/>旧版工程工具</a><small>本地工作区 · 参数配置预览版</small></div>
    </aside>
    <div className="ps-body"><header className="ps-topbar"><button disabled={busy} onClick={() => void open(null)}>我的产品</button>{product && <><span>/</span><strong>{draft?.name}</strong></>}<div className="ps-save-state">{product && <><span className={dirty ? 'pending' : ''}>{dirty ? '● 有未保存更改' : <><Check size={14}/>已保存</>}</span><button className="ps-button" disabled={busy || !dirty || !!issues.length} onClick={() => void save()}>{busy ? '正在保存…' : '保存配置'}</button></>}</div></header>
      <main className="ps-main">{error && <div className="ps-error" role="alert">{error}<button aria-label="关闭提示" onClick={() => setError('')}><X size={16}/></button></div>}
      {loading ? <div className="ps-empty"><Loader2 className="spin"/>正在打开工作区…</div> : !product ? <>
        <div className="ps-heading"><div className="ps-eyebrow">从一个产品开始</div><h1>你的下一款产品。</h1><p>选好功能和参数，在面板上试用，再导出配置表。</p><button className="ps-button primary" disabled={!catalog} onClick={() => { setCreating(true); setName(''); setFeatures(initialFeatures) }}><Plus size={18}/>新建产品</button></div>
        <div className="ps-intro">{steps.map((s, i) => <div key={s.name}><s.icon size={23}/><span>0{i + 1}</span><h3>{s.name}</h3><p>{s.description}</p></div>)}</div>
        <div className="ps-section-title"><h2>我的产品</h2><span>{products.length} 个产品</span></div>
        <div className="ps-products">{products.map(p => <button className="ps-product-card" key={p.id} onClick={() => void open(p)}><span className="ps-product-icon"><Fan size={27}/></span><small>{featureSummary(p.definition)}</small><h3>{p.definition.name}</h3><p>APL {p.definition.template?.version} · 配置 r{p.revision}</p><span className="ps-card-link">打开配置<ArrowRight size={16}/></span></button>)}<button className="ps-product-card new" disabled={!catalog} onClick={() => { setCreating(true); setName(''); setFeatures(initialFeatures) }}><Plus size={28}/><strong>创建一个新产品</strong><span>从标准配置开始</span></button></div>
      </> : draft && catalog && <>
        <div className="ps-project-heading"><div><div className="ps-eyebrow">{featureSummary(draft)}</div><h1>{draft.name}</h1></div><span className="ps-version-pill"><GitBranch size={15}/>APL {draft.template?.version}<span>本地开发版</span></span></div>
        <nav className="ps-steps" aria-label="产品配置步骤">{steps.map((s, i) => <button key={s.name} disabled={busy} aria-current={step === i ? 'step' : undefined} className={step === i ? 'active' : ''} onClick={() => void go(i)}><span className="ps-step-number">{i + 1}</span><div><strong>{s.name}</strong><small>{s.description}</small></div><s.icon size={19}/></button>)}</nav>
        {step === 0 && <div className="ps-config-layout"><fieldset className="ps-form-stack" disabled={busy}>
          <section className="ps-card"><div className="ps-card-heading"><span>01</span><div><h2>基本设置</h2><p>给产品一个名字，选择它提供的能力。</p></div></div><label className="ps-field">产品名称<input maxLength={80} value={draft.name} onChange={e => setDraft({ ...draft, name: e.target.value })}/></label><ProductFeatureOptions value={productFeatures(draft)} onChange={f => setDraft(d => d && setProductFeatures(d, f))}/><label className="ps-field">APL 版本<select value={draft.template?.version} onChange={() => {}}><option value={draft.template?.version}>APL {draft.template?.version} · 本地开发版</option></select><small>当前已接入本地版本；历史 Git 版本选择尚未开放。</small></label></section>
          <section className="ps-card"><div className="ps-card-heading"><span>02</span><div><h2>功能选配</h2><p>开启设备需要的模块和传感器。</p></div></div><div className="ps-option-grid">{[[1, 'UV 杀菌'], [2, '负离子'], [4, 'Wi-Fi 联网']].map(([bit, label]) => <label className="ps-option" key={String(label)}><input type="checkbox" checked={!!(values.modules & Number(bit))} onChange={e => toggle('modules', Number(bit), e.target.checked)}/><span>{label}</span></label>)}</div><div className="ps-subheading">环境传感器</div><div className="ps-option-grid">{[[1, 'PM2.5'], [2, 'PM10'], [4, '甲醛'], [8, 'TVOC'], [16, '湿度']].map(([bit, label]) => <label className="ps-option" key={String(label)}><input type="checkbox" disabled={Number(bit) === 16 ? !values.humidify_enabled : !values.purify_enabled} checked={!!(values.sensors & Number(bit))} onChange={e => toggle('sensors', Number(bit), e.target.checked)}/><span>{label}</span></label>)}</div></section>
          <ProfileRuntimeParameters fields={catalog.schema.parameters.filter(f => profileFieldCompiled(firmwareBuild(draft), f.key))} values={values} onChange={update} pid={profilePid(draft)} pidMaxLength={catalog.schema.identity.pid.maxLength} onPidChange={pid => setDraft(d => d && setProfile(d, 'pid', pid))}/>
          <ProfileKeyMapping definition={draft} onChange={setDraft}/>
          <ProfileFirmwareBuild definition={draft} onChange={setDraft}/>
        </fieldset><aside className="ps-config-summary"><div className="ps-card"><Wind size={28}/><h3>你的产品方案</h3><p>{featureSummary(draft)}</p><dl><dt>开机风档</dt><dd>{gears[values.default_fan_gear]}</dd><dt>滤网寿命</dt><dd>{firmwareBuild(draft).defines.APL_COMPILE_FILTER ? `${values.filter_life_h} 小时` : '未编入'}</dd><dt>辅助模块</dt><dd>{['UV', '负离子', 'Wi-Fi'].filter((_, i) => values.modules & 1 << i).join('、') || '未启用'}</dd></dl><div className="ps-summary-note">接下来，在仿真面板上按下电源键，试用这份配置。</div><button className="ps-button primary" disabled={busy || !!issues.length} onClick={() => void go(1)}>保存并进入仿真<ArrowRight size={16}/></button>{issues.length > 0 && <p className="ps-error-text">{issues[0]}</p>}</div>
          <details className="ps-source"><summary><GitBranch size={14}/>版本与来源<ChevronDown size={14}/></summary><p>{catalog.source.repository}</p><p>参数格式 v{catalog.schema.schemaVersion}</p><p>{catalog.source.dirty ? '工作区含未提交改动，以源码指纹固定本次快照。' : '源码来自当前 Git 提交。'}</p><code>{catalog.source.fingerprint}</code>{sourceFingerprint(draft) !== catalog.source.fingerprint && <button className="ps-button" onClick={() => setDraft(bindCurrentProfile(draft, catalog))}>确认绑定当前源码</button>}</details></aside></div>}
        {step === 1 && <ProfileSimulation key={`${product.id}:${product.revision}`} product={product} onNext={() => void go(2)}/>}
        {step === 2 && <section className="ps-export ps-card"><span className="ps-export-icon"><FileSpreadsheet size={42}/></span><h2>把这份配置，交给设备。</h2><p>参数表用于运行配置，源码包用于按当前裁剪重新编译固件。</p><div className="ps-export-facts"><span>产品<strong>{draft.name}</strong></span><span>Wi-Fi 产品 PID<strong>{profilePid(draft) || '未设置'}</strong></span><span>APL 版本<strong>{catalog.schema.aplVersion}</strong></span><span>配置修订<strong>r{product.revision}</strong></span></div><button className="ps-button primary" disabled={busy || dirty || !!issues.length} onClick={() => void operation(async () => { await download(`/api/products/${product.id}/parameters.xlsx?revision=${product.revision}`, 'APL参数表.xlsx') })}><Download size={18}/>导出 Excel 参数表</button><button className="ps-button" disabled={busy || dirty || !!issues.length} onClick={() => void operation(async () => { await download(`/api/products/${product.id}/firmware.zip?revision=${product.revision}`, 'APL固件源码.zip') })}><Download size={18}/>导出裁剪后的 C 源码包</button><button className="ps-button" disabled={busy || dirty || !!issues.length} onClick={() => void operation(async () => { await download(`/api/products/${product.id}/memory-estimate.json?revision=${product.revision}`, 'APL内存估算.json') })}><Download size={18}/>导出内存估算记录</button><small>包含参数值、允许范围、产品身份和版本来源。Modbus 地址与写入工具尚未接入。</small><button className="ps-back-link" onClick={() => void go(0)}><ArrowLeft size={14}/>返回修改配置</button></section>}
      </>}
      </main>
    </div>
    {creating && catalog && <div className="ps-modal-backdrop" onClick={() => !busy && setCreating(false)}><form className="ps-modal" onClick={e => e.stopPropagation()} onSubmit={e => { e.preventDefault(); void create() }}><button type="button" className="ps-modal-close" aria-label="关闭新建产品" onClick={() => setCreating(false)}><X size={20}/></button><div className="ps-eyebrow">第一步，只需一个名字</div><h2>新建产品</h2><label className="ps-field">产品名称<input autoFocus required maxLength={80} placeholder="例如：卧室空气净化器" value={name} onChange={e => setName(e.target.value)}/></label><ProductFeatureOptions value={features} onChange={setFeatures}/><label className="ps-field">APL 版本<select value={catalog.schema.aplVersion} onChange={() => {}}><option>{catalog.schema.aplVersion}</option></select><small>本地开发版 · 参数格式 v{catalog.schema.schemaVersion}</small></label><button className="ps-button primary" disabled={busy || !name.trim() || !features.purify && !features.humidify}>{busy ? '正在创建…' : '创建并配置'}<ArrowRight size={16}/></button></form></div>}
  </div>
}

function ProductFeatureOptions({ value, onChange }: { value: ProductFeatures; onChange: (value: ProductFeatures) => void }) {
  return <fieldset className="ps-feature-options"><legend>产品功能</legend><div className="ps-option-grid">
    <label className="ps-option"><input type="checkbox" checked={value.purify} onChange={e => onChange({ ...value, purify: e.target.checked })}/><span>净化功能</span></label>
    <label className="ps-option"><input type="checkbox" checked={value.humidify} onChange={e => onChange({ ...value, humidify: e.target.checked })}/><span>加湿功能</span></label>
  </div><p>至少勾选一项，可以同时启用。面板按功能自动适配。</p>{!value.purify && !value.humidify && <p role="alert" className="ps-error-text">请至少选择净化功能或加湿功能。</p>}</fieldset>
}

function ProfileSimulation({ product, onNext }: { product: Product; onNext: () => void }) {
  const [session, setSession] = useState<SimulationSession | null>(null), [error, setError] = useState(''), [busy, setBusy] = useState(false)
  const owned = useRef(''), active = useRef(true), lock = useRef(false), epoch = useRef(0)
  useEffect(() => { active.current = true; return () => { active.current = false; if (owned.current) void api.stopSimulation(owned.current).catch(() => {}) } }, [])
  useEffect(() => {
    if (!session || !['building', 'ready'].includes(session.status)) return
    let stopped = false, pending = false
    const timer = setInterval(() => {
      if (pending || lock.current) return
      pending = true
      const version = epoch.current
      void api.simulation(session.id).then(s => { if (!stopped && !lock.current && version === epoch.current) setSession(s) }).catch(e => !stopped && setError(e.message)).finally(() => { pending = false })
    // C publishes every 100ms; sampling slower than the 300ms bar frame skips segments.
    }, 100)
    return () => { stopped = true; clearInterval(timer) }
  }, [session?.id, session?.status])
  async function start() {
    if (lock.current) return; lock.current = true; ++epoch.current; setBusy(true); setError('')
    try { if (owned.current) await api.stopSimulation(owned.current); const s = await api.startSimulation(product, true); if (!active.current) { await api.stopSimulation(s.id); return }; owned.current = s.id; setSession(s) }
    catch (e) { setError((e as Error).message) } finally { lock.current = false; setBusy(false) }
  }
  async function action(value: Parameters<typeof api.simulate>[1]) {
    if (!session || lock.current || session.status !== 'ready') return
    lock.current = true; ++epoch.current; setBusy(true); setError('')
    try { const s = await api.simulate(session.id, value); if (active.current) setSession(s) } catch (e) { setError((e as Error).message) } finally { lock.current = false; setBusy(false) }
  }
  if (!firmwareBuild(product.definition).defines.APL_COMPILE_HMI) return <section className="ps-card"><h2>纯主控模式</h2><p className="ps-summary-note">此固件已裁掉 APL 按键与显示，当前面板仿真不可用。导出源码包后，由宿主接入主控命令和输出 getter。</p><button className="ps-button primary" onClick={onNext}>继续导出<ArrowRight size={16}/></button></section>
  const ready = session?.status === 'ready', snap = session?.snapshot
  return <div className="ps-simulation-layout" aria-busy={busy} data-action-pending={busy && ready}><section className="ps-card ps-device-card"><div className="ps-simulation-heading"><div><h2>试用你的产品</h2><p>{session ? '按下面板按键，观察设备的真实变化。' : '先启动仿真，再按面板电源键开机。'}</p></div><button className="ps-button primary" disabled={busy || session?.status === 'building'} onClick={() => void start()}>{session?.status === 'building' ? <><Loader2 size={16} className="spin"/>正在准备</> : <><Play size={15}/>{session ? '重新开始' : '启动仿真'}</>}</button></div>{error && <p role="alert" className="ps-error">{error}</p>}{session?.status === 'failed' && <pre className="ps-error">{session.diagnostics}</pre>}<AirCarePanel startLabel="启动仿真" features={productFeatures(product.definition)} keyMapping={profileKeyMapping(product.definition)} snapshot={snap} controls={session?.descriptor.controls ?? []} disabled={!ready} busy={busy} onGesture={async (key, value) => action({ type: 'input', key, value })}/><div className="ps-live-state"><span className={snap?.outputs.fan ? 'on' : ''}/>{['待机', '运行中', '关机风干', '产测'][Number(snap?.internal.run_state)] ?? '尚未启动'}<span>输出档位：{snap?.outputs.fan ? gears[Number(snap.outputs.fan_gear)] : '停止'}</span><span>目标转速：{typeof snap?.outputs.fan_target_rpm === 'number' ? snap.outputs.fan_target_rpm : '—'} rpm</span></div></section>
    <aside className="ps-simulation-tools"><section className="ps-card"><h3>改变使用环境</h3><p>观察自动模式和保护逻辑的反应。</p>{session?.descriptor.controls.filter(c => ['pm25', 'humidity', 'water_level', 'uv_interlock'].includes(c.key)).map(c => <label className="ps-field" key={c.key}>{c.name}{c.kind === 'enum' ? <select disabled={!ready || busy} value={Number(snap?.values[c.key] ?? c.default)} onChange={e => void action({ type: 'input', key: c.key, value: Number(e.target.value) })}>{c.options?.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}</select> : c.kind === 'bool' ? <input type="checkbox" disabled={!ready || busy} checked={!!(snap?.values[c.key] ?? c.default)} onChange={e => void action({ type: 'input', key: c.key, value: e.target.checked })}/> : <input type="number" min={c.min} max={c.max} disabled={!ready || busy} defaultValue={Number(c.default)} onBlur={e => { const n = Number(e.target.value); if (e.target.value !== '' && Number.isInteger(n)) void action({ type: 'input', key: c.key, value: n }) }}/>}</label>)}{!session && <div className="ps-tool-empty">启动后可调整空气质量、湿度或水位。</div>}<div className="ps-time-controls"><button className="ps-button" disabled={!ready || busy} onClick={() => void action({ type: 'advance', ms: 5000 })}>快进 5 秒</button><button className="ps-button" disabled={!ready || busy} onClick={() => void action({ type: 'supply', on: !snap?.powered })}>{snap?.powered === false ? '模拟上电' : '模拟掉电'}</button></div></section><section className="ps-card"><h3>实际负载</h3>{session?.descriptor.outputs.map(o => <div className="ps-load" key={o.key}><span>{o.name}</span><strong>{!snap ? '—' : o.boolean ? snap.outputs[o.key] ? '运行' : '停止' : o.options?.find(v => v.value === snap.outputs[o.key])?.label ?? String(snap.outputs[o.key])}{snap && o.unit ? ' ' + o.unit : ''}</strong></div>)}{!session && <p>设备启动后显示实际输出。</p>}</section><button className="ps-button primary" onClick={onNext}>体验完成，导出参数<ArrowRight size={16}/></button></aside>
    <details className="ps-card ps-diagnostics"><summary>调试详情与事件记录</summary><p>来自真实 C 库的输入、输出和状态变化；仿真使用当前已保存的参数。</p><div className="ps-events">{session?.events.slice(-40).reverse().map(e => <p key={e.seq}>{e.message}</p>)}</div></details></div>
}
