import { useCallback, useEffect, useRef, useState } from 'react'
import { ArrowDownToLine, ArrowLeft, ArrowRight, ArrowUpRight, Bell, Box, Boxes, Check, CheckCircle2, ChevronRight, CircleDashed, Code2, Download, FileCode2, FileJson, FolderOpen, Layers, LayoutGrid, Loader2, MoreHorizontal, Plus, Search, Settings, ShieldCheck, Sparkles, Trash2, X, Info, ExternalLink, RefreshCw } from 'lucide-react'
import { STUDIO_VERSION, emptyDefinition, exampleDefinition, parseDefinition, validateDefinition, type Definition, type FunctionDefinition, type Parameter, type Product } from '@apl-studio/model'
import { api, ApiError, download } from './api'
import { Field, Modal, Select, Time } from './components'
import { FunctionEditor, ParameterEditor } from './editors'
import { Workspace, type Tab } from './Workspace'
import { definitionFromTemplate, applyTemplate, validateTemplateConfiguration, type TemplateDescriptor } from '@apl-studio/model/template'
import { TemplateCatalog } from './TemplateCatalog'

type View = 'products' | 'project' | 'templates' | 'settings'
type Confirmation = { title: string; text: string; action: () => void; destructive?: boolean; label: string }
const equal = (a: unknown, b: unknown) => JSON.stringify(a) === JSON.stringify(b)

export default function App() {
  const [view, setView] = useState<View>('products')
  const [templates, setTemplates] = useState<TemplateDescriptor[]>([])
  const [products, setProducts] = useState<Product[]>([])
  const [product, setProduct] = useState<Product | null>(null)
  const [draft, setDraft] = useState<Definition | null>(null)
  const [tab, setTab] = useState<Tab>('functions')
  const [loading, setLoading] = useState(true)
  const [busy, setBusy] = useState(false)
  const [connected, setConnected] = useState(false)
  const [failure, setFailure] = useState('')
  const [toast, setToast] = useState<{ text: string; error?: boolean } | null>(null)
  const [creating, setCreating] = useState(false)
  const [functionEditor, setFunctionEditor] = useState<{ field: FunctionDefinition | null } | null>(null)
  const [parameterEditor, setParameterEditor] = useState<{ field: Parameter | null } | null>(null)
  const [confirmation, setConfirmation] = useState<Confirmation | null>(null)
  const [remoteChanged, setRemoteChanged] = useState(false)
  const [search, setSearch] = useState('')
  const [sort, setSort] = useState('updated')
  const productRef = useRef(product); productRef.current = product
  const importRef = useRef<HTMLInputElement>(null)
  const loadToken = useRef(0)
  const dirty = !!product && !!draft && !equal(product.definition, draft)
  const dirtyRef = useRef(dirty); dirtyRef.current = dirty
  const notify = useCallback((text: string, error = false) => setToast({ text, error }), [])
  const refresh = useCallback(async () => { const all = await api.list(); setProducts(all); setFailure(''); return all }, [])

  const openProduct = useCallback(async (id: string, nextTab: Tab = 'functions') => {
    const token = ++loadToken.current
    setLoading(true)
    try {
      const selected = await api.get(id)
      if (loadToken.current !== token) return
      setProduct(selected); setDraft(structuredClone(selected.definition)); setView('project'); setTab(nextTab); setRemoteChanged(false); setFailure('')
      history.replaceState(null, '', `#product/${id}/${nextTab}`)
    } catch (error) { notify((error as Error).message, true) }
    finally { if (loadToken.current === token) setLoading(false) }
  }, [notify])
  useEffect(() => {
    let alive = true
    Promise.all([refresh(), api.templates().then(result => alive && setTemplates(result.templates))]).then(() => {
      const match = location.hash.match(/^#product\/([\w-]+)\/(overview|functions|parameters|simulation|preview|delivery)$/)
      if (match && alive) void openProduct(match[1], match[2] as Tab)
      else if (alive && location.hash === '#templates') setView('templates')
      else if (alive && location.hash === '#settings') setView('settings')
    }).catch(error => alive && setFailure(error.message)).finally(() => alive && setLoading(false))
    return () => { alive = false }
  }, [refresh, openProduct])
  useEffect(() => {
    let stopped = false, socket: WebSocket | null = null, retry: ReturnType<typeof setTimeout> | undefined
    let checks = new Set<ReturnType<typeof setTimeout>>()
    function connect() {
      if (stopped) return
      socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`)
      socket.onopen = () => { setConnected(true); void refresh().catch(() => {}) }
      socket.onmessage = event => {
        let message: { type: string; id?: string; revision?: number | null }
        try { message = JSON.parse(event.data) } catch { return }
        if (message.type !== 'product/changed') return
        void refresh().catch(error => setFailure(error.message))
        const check = setTimeout(() => {
          checks.delete(check)
          const current = productRef.current
          if (current && current.id === message.id && (message.revision === null || Number(message.revision) > current.revision)) setRemoteChanged(true)
        }, 250)
        checks.add(check)
      }
      socket.onclose = () => { setConnected(false); if (!stopped) retry = setTimeout(connect, 2000) }
      socket.onerror = () => socket?.close()
    }
    connect()
    return () => { stopped = true; clearTimeout(retry); for (const timer of checks) clearTimeout(timer); socket?.close() }
  }, [refresh])
  useEffect(() => { if (!toast) return; const timer = setTimeout(() => setToast(null), toast.error ? 7000 : 3500); return () => clearTimeout(timer) }, [toast])
  useEffect(() => {
    function beforeUnload(event: BeforeUnloadEvent) { if (dirtyRef.current) { event.preventDefault(); event.returnValue = '' } }
    window.addEventListener('beforeunload', beforeUnload)
    return () => window.removeEventListener('beforeunload', beforeUnload)
  }, [])
  function configurationIssues(value: Definition | null): { message: string }[] {
    if (!value) return [{ message: '缺少产品定义' }]
    const issues = validateDefinition(value)
    if (issues.length || !value.template) return issues
    const template = templates.find(t => t.id === value.template?.id && t.version === value.template.version)
    return template ? validateTemplateConfiguration(value, template).map(message => ({ message })) : [{ message: '绑定的模板版本不可用' }]
  }
  function bindTemplate(template: TemplateDescriptor | null) {
    if (!draft) return
    if (!template) { setDraft({ ...draft, template: null }); return }
    const candidate = applyTemplate(draft, template)
    const removed = draft.functions.filter(f => !candidate.functions.some(c => c.key === f.key)).map(f => f.name)
    const removedParameters = draft.parameters.filter(p => !candidate.parameters.some(c => c.key === p.key)).map(p => p.name)
    setConfirmation({ title: '应用源码模板？', label: '应用到草稿', text: `将绑定 ${template.name} v${template.version}，按模板补齐能力、参数与取值约束。兼容的值会保留。${removed.length || removedParameters.length ? `不支持的项目将从草稿移除：${[...removed, ...removedParameters].join('、')}。` : ''}应用后仍需保存，才会影响后续生成。`, action: () => setDraft(candidate) })
  }
  async function createFromTemplate(template: TemplateDescriptor) {
    const definition = definitionFromTemplate(template, products.map(p => p.definition.code))
    try { await create(definition) } catch { /* visible toast */ }
  }
  async function save() {
    if (!product || !draft || busy) return
    const issues = configurationIssues(draft)
    if (issues.length) { notify(issues[0].message, true); return }
    const submitted = structuredClone(draft)
    setBusy(true)
    try {
      const saved = await api.update(product, submitted)
      setProduct(saved); setDraft(current => equal(current, submitted) ? structuredClone(saved.definition) : current); setRemoteChanged(false)
      await refresh(); notify(`产品已保存 · 修订 r${saved.revision}`)
    } catch (error) { if (error instanceof ApiError && error.code === 'REVISION_CONFLICT') setRemoteChanged(true); notify((error as Error).message, true) }
    finally { setBusy(false) }
  }
  const saveRef = useRef(save); saveRef.current = save
  useEffect(() => {
    const key = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 's') { event.preventDefault(); if (dirtyRef.current) void saveRef.current() }
    }
    window.addEventListener('keydown', key); return () => window.removeEventListener('keydown', key)
  }, [])
  function guarded(action: () => void) {
    if (busy) { notify('正在保存，请稍候'); return }
    if (dirty) setConfirmation({ title: '离开前保留更改？', text: '当前产品有尚未保存的更改。你可以取消返回保存，或放弃草稿继续。', label: '放弃草稿并继续', action, destructive: true })
    else action()
  }
  function navigate(next: View) {
    if (next === view) return
    guarded(() => { ++loadToken.current; setProduct(null); setDraft(null); setView(next); setLoading(false); history.replaceState(null, '', '#'+next) })
  }
  async function exportProduct(json = false) {
    if (!product || dirty) { notify('请先保存产品更改', true); return }
    try { await download(`/api/products/${product.id}/${json ? 'definition' : 'export'}`, 'product-definition.zip'); notify(json ? '产品 JSON 已导出' : '产品定义包已导出；源码 SDK 请在交付中心生成') }
    catch (error) { notify((error as Error).message, true) }
  }
  async function create(definition: Definition) {
    if (busy) return
    setBusy(true)
    try { const created = await api.create(definition); await refresh(); setCreating(false); await openProduct(created.id); notify('产品已创建，从功能定义开始') }
    catch (error) { notify((error as Error).message, true); throw error }
    finally { setBusy(false) }
  }
  async function createExample() {
    const definition = exampleDefinition(() => crypto.randomUUID())
    const base = definition.code
    let number = 2
    while (products.some(p => p.definition.code === definition.code)) definition.code = base + '_' + number++
    try { await create(definition) } catch { /* visible toast */ }
  }
  async function importProduct(file: File) {
    if (file.size > 1024 * 1024) { notify('产品定义文件不能超过 1 MiB', true); return }
    try { const input = JSON.parse(await file.text()); await create(parseDefinition(input)) }
    catch (error) { notify((error as Error).message, true) }
  }
  function removePart(kind: 'function' | 'parameter', id: string) {
    if (!draft) return
    const item = kind === 'function' ? draft.functions.find(f => f.id === id) : draft.parameters.find(p => p.id === id)
    setConfirmation({ title: `移除${kind === 'function' ? '功能' : '参数'}？`, text: `“${item?.name}”将从当前草稿移除，保存后生效。`, label: '移除', destructive: true, action: () => setDraft(current => current && ({ ...current, ...(kind === 'function' ? { functions: current.functions.filter(f => f.id !== id) } : { parameters: current.parameters.filter(p => p.id !== id) }) })) })
  }
  function removeProduct(target: Product) {
    setConfirmation({ title: '删除这个产品？', text: `“${target.definition.name}”及其已保存的修订将被删除。建议先导出产品定义。`, label: '删除产品', destructive: true, action: () => {
      setBusy(true)
      api.remove(target).then(() => { if (product?.id === target.id) { setProduct(null); setDraft(null); setView('products'); history.replaceState(null, '', '#products') }; return refresh() }).then(() => notify('产品已删除')).catch(error => notify(error.message, true)).finally(() => setBusy(false))
    } })
  }
  const visibleProducts = products.filter(p => (p.definition.name + p.definition.code + p.definition.category).toLowerCase().includes(search.toLowerCase())).sort((a, b) => sort === 'name' ? a.definition.name.localeCompare(b.definition.name, 'zh-CN') : b.updatedAt.localeCompare(a.updatedAt))
  return <div className="app-shell">
    <aside className="sidebar"><a className="brand" href="#products" onClick={event => { event.preventDefault(); navigate('products') }}><span className="brand-mark"><Layers size={22} strokeWidth={1.6}/></span><span>APL Studio<small>产品开发工作台</small></span></a>
      <div className="workspace-switch"><span className="workspace-avatar">A</span><div><strong>我的工作空间</strong><small>本地工作区</small></div><span className="badge tiny">LOCAL</span></div>
      <div className="nav-label">工作空间</div><nav><button className={view === 'products' || view === 'project' ? 'active' : ''} onClick={() => navigate('products')}><LayoutGrid size={18}/><span>产品库</span><span className="nav-count">{products.length}</span></button><button className={view === 'templates' ? 'active' : ''} onClick={() => navigate('templates')}><Boxes size={18}/><span>源码模板</span><span className="nav-count">{templates.length}</span></button></nav>
      {!!products.length && <><div className="nav-label recent-label">最近产品</div><div className="recent-products">{products.slice(0, 5).map(p => <button key={p.id} className={view === 'project' && product?.id === p.id ? 'selected' : ''} onClick={() => guarded(() => void openProduct(p.id))}><span className="product-dot"/><span>{p.definition.name}</span></button>)}</div></>}
      <div className="sidebar-bottom"><div className="stage-card"><span className="stage-icon"><Sparkles size={16}/></span><strong>从定义，到交付</strong><p>一份产品模型，连接配置、验证与 SDK。</p><button className="text-button" onClick={() => navigate('templates')}>了解模板边界<ArrowUpRight size={13}/></button></div><button className={'settings-nav ' + (view === 'settings' ? 'selected' : '')} onClick={() => navigate('settings')}><Settings size={17}/>平台设置<span>v{STUDIO_VERSION}</span></button><div className="connection-status"><span className={'status-dot ' + (connected ? 'online' : '')}/>{connected ? '工作区已连接' : '正在连接工作区…'}</div></div>
    </aside>
    <div className="main-shell"><header className="topbar"><div className="breadcrumbs"><span>工作空间</span><ChevronRight size={13}/><button onClick={() => navigate('products')}>产品库</button>{view === 'project' && <><ChevronRight size={13}/><strong>{draft?.name || '产品'}</strong></>}{view === 'templates' && <><ChevronRight size={13}/><strong>源码模板</strong></>}{view === 'settings' && <><ChevronRight size={13}/><strong>平台设置</strong></>}</div><div className="top-actions">{view === 'project' && <><span className={'save-state ' + (dirty ? 'unsaved' : '')}>{busy ? <Loader2 className="spin" size={13}/> : dirty ? <span className="unsaved-dot"/> : <Check size={13}/>} {busy ? '正在保存' : dirty ? '有未保存的更改' : '已保存'}</span><button className="button small" disabled={busy || !dirty || !!configurationIssues(draft).length} onClick={() => void save()}>保存更改<span className="shortcut">Ctrl S</span></button><button className="button primary small" disabled={busy || dirty} onClick={() => { setTab('delivery'); history.replaceState(null, '', `#product/${product?.id}/delivery`) }}><ArrowUpRight size={14}/>交付中心</button></>}<span className="user-avatar" title="本地工作空间，无账户系统">A</span></div></header>
      <main className={'main-content ' + (view === 'project' ? 'project-content' : '')}>
        {failure && <div className="inline-alert warning" role="alert"><Info size={17}/><span>{failure}</span><button className="text-button" onClick={() => { setLoading(true); refresh().catch(e => setFailure(e.message)).finally(() => setLoading(false)) }}><RefreshCw size={13}/>重新连接</button></div>}
        {view === 'products' && <><div className="page-heading"><div><div className="eyebrow">PRODUCT WORKSPACE</div><h1>产品工作台<span className="heading-dot">.</span></h1><p>定义你的产品，让每一个功能都有清晰的接口。</p></div><div className="heading-actions"><button className="button" onClick={() => importRef.current?.click()}><ArrowDownToLine size={15}/>导入定义</button><button className="button primary" onClick={() => setCreating(true)}><Plus size={16}/>新建产品</button></div></div>
          <input type="file" ref={importRef} hidden accept=".json,.apl.json,application/json" aria-label="导入产品定义文件" onChange={e => { const file = e.target.files?.[0]; e.target.value = ''; if (file) void importProduct(file) }}/>
          <div className="workspace-stats"><div><span className="stat-icon"><Box size={17}/></span><span><strong>{products.length}</strong><small>个产品</small></span></div><div><span className="stat-icon"><Code2 size={17}/></span><span><strong>{products.reduce((sum, p) => sum + p.definition.functions.length, 0)}</strong><small>个功能定义</small></span></div><div><span className="stat-icon"><ShieldCheck size={17}/></span><span><strong>JSON v1</strong><small>统一产品模型</small></span></div><span className="badge neutral"><CircleDashed size={12}/>平台预览版</span></div>
          <section className="welcome-card"><div className="welcome-copy"><span className="eyebrow">BUILD YOUR NEXT PRODUCT</span><h2>配置你的产品，<br/>交付真实代码。</h2><p>选择产品模板、配置能力与参数。<br/>编译验证通过后，下载可移植的 C 源码 SDK。</p><div><button className="button primary" disabled={busy} onClick={() => templates.length ? navigate('templates') : void createExample()}><Sparkles size={15}/>从模板开始<ArrowRight size={15}/></button><button className="text-button" onClick={() => navigate('templates')}>了解开发流程<ArrowUpRight size={14}/></button></div></div><div className="flow-illustration" aria-hidden="true"><div className="flow-line"/><div className="floating-code"><Code2 size={21}/><span>product.apl.json</span><small>一份定义 · 多个交付入口</small><div className="fake-code"><i/><i/><i/><i/></div></div><div className="flow-node node-a"><SlidersIcon/><span>功能定义</span></div><div className="flow-node node-b"><FileCode2 size={18}/><span>接口契约</span><CheckCircle2 size={13}/></div><div className="flow-node node-c"><Box size={17}/><span>产品 SDK</span><small>编译并验证</small></div></div></section>
          <div className="library-heading"><h2>我的产品 <span className="number-label">{products.length}</span></h2><div><div className="search-input"><Search size={15}/><input aria-label="搜索产品" placeholder="搜索产品名称、标识或品类" value={search} onChange={e => setSearch(e.target.value)}/></div><Select label="产品排序" value={sort} onChange={setSort}><option value="updated">最近更新</option><option value="name">产品名称</option></Select></div></div>
          {loading ? <div className="loading-state"><Loader2 className="spin" size={22}/>正在读取产品…</div> : <div className="product-grid">{visibleProducts.map(p => <article className="product-card" key={p.id}><div className="product-card-top"><span className="card-product-icon"><Box size={24} strokeWidth={1.6}/></span><span className="badge neutral">{p.definition.category}</span><button className="icon-button danger-hover" aria-label={`删除产品${p.definition.name}`} onClick={() => removeProduct(p)}><Trash2 size={15}/></button></div><button className="product-card-link" onClick={() => void openProduct(p.id)}><h3>{p.definition.name}</h3><code>{p.definition.code}</code><p>{p.definition.description || '还没有产品说明，进入工作台继续完善。'}</p><div className="product-card-counts"><span><Code2 size={13}/>{p.definition.functions.length} 个功能</span><span><Settings size={13}/>{p.definition.parameters.length} 个参数</span></div><div className="product-card-footer"><span>r{p.revision}<span className="dot-separator">·</span><Time value={p.updatedAt}/> 更新</span><span>进入开发<ArrowRight size={14}/></span></div></button></article>)}<button className="new-product-card" onClick={() => setCreating(true)}><span><Plus size={24}/></span><strong>创建一个新产品</strong><small>从一个清晰的定义开始</small></button></div>}
          {!loading && !!search && !visibleProducts.length && <p className="search-empty">没有匹配的产品，试试其他关键词。</p>}
          <footer className="workspace-footer"><span><Layers size={13}/>APL Studio · 嵌入式产品标准化与 SDK 交付</span><span>本地保存 · 源码模板独立接入</span></footer>
        </>}
        {view === 'project' && product && draft && <>{remoteChanged && <div className="inline-alert warning"><Info size={16}/><span>这个产品在其他窗口有更新，或已被删除。当前草稿已保留。</span><button className="text-button" onClick={() => guarded(() => void openProduct(product.id, tab))}>读取最新版本</button></div>}<Workspace templates={templates} onBind={bindTemplate} product={product} draft={draft} setDraft={setDraft} tab={tab} setTab={next => { setTab(next); history.replaceState(null, '', `#product/${product.id}/${next}`) }} dirty={dirty} onFunction={field => setFunctionEditor({ field })} onParameter={field => setParameterEditor({ field })} onRemove={removePart} onExport={json => void exportProduct(json)} onSave={() => void save()}/></>}
        {view === 'templates' && <TemplateCatalog templates={templates} busy={busy} onCreate={template => void createFromTemplate(template)}/>}
        {view === 'settings' && <SettingsPage connected={connected} templates={templates}/>}
      </main>
    </div>
    {toast && <div className={'toast ' + (toast.error ? 'toast-error' : '')} role={toast.error ? 'alert' : 'status'}>{toast.error ? <Info size={17}/> : <CheckCircle2 size={17}/>}<span>{toast.text}</span><button aria-label="关闭提示" onClick={() => setToast(null)}><X size={15}/></button></div>}
    {creating && <CreateProduct products={products} busy={busy} onClose={() => { if (!busy) setCreating(false) }} onCreate={create}/>}
    {functionEditor && draft && <FunctionEditor definition={draft} original={functionEditor.field} onClose={() => setFunctionEditor(null)} onSave={field => { setDraft(current => current && ({ ...current, functions: current.functions.some(f => f.id === field.id) ? current.functions.map(f => f.id === field.id ? field : f) : [...current.functions, field] })); setFunctionEditor(null) }}/>}
    {parameterEditor && draft && <ParameterEditor definition={draft} original={parameterEditor.field} onClose={() => setParameterEditor(null)} onSave={field => { setDraft(current => current && ({ ...current, parameters: current.parameters.some(p => p.id === field.id) ? current.parameters.map(p => p.id === field.id ? field : p) : [...current.parameters, field] })); setParameterEditor(null) }}/>}
    {confirmation && <Modal title={confirmation.title} onClose={() => setConfirmation(null)}><div className="modal-body"><p className="confirm-text">{confirmation.text}</p></div><div className="modal-footer"><button className="button" onClick={() => setConfirmation(null)}>取消</button><button className={'button ' + (confirmation.destructive ? 'danger' : 'primary')} onClick={() => { const action = confirmation.action; setConfirmation(null); action() }}>{confirmation.label}</button></div></Modal>}
  </div>
}
function SlidersIcon() { return <Settings size={19}/> }

function CreateProduct({ products, busy, onClose, onCreate }: { products: Product[]; busy: boolean; onClose: () => void; onCreate: (definition: Definition) => Promise<void> }) {
  const [preset, setPreset] = useState('blank')
  const [name, setName] = useState(''), [code, setCode] = useState(() => { let n = 1; while (products.some(p => p.definition.code === `product_${n}`)) n++; return `product_${n}` })
  const [category, setCategory] = useState('自定义产品'), [error, setError] = useState('')
  async function submit(event: React.FormEvent) {
    event.preventDefault()
    const d = { ...(preset === 'example' ? exampleDefinition(() => crypto.randomUUID()) : emptyDefinition()), name, code, category }
    const issues = validateDefinition(d)
    if (issues.length) { setError(issues[0].message); return }
    try { await onCreate(d) } catch (e) { setError((e as Error).message) }
  }
  return <Modal title="创建新产品" subtitle="为你的下一个嵌入式产品，建立一份定义。" onClose={onClose}><form onSubmit={submit}><div className="modal-body"><div className="preset-options"><button type="button" className={preset === 'blank' ? 'selected' : ''} onClick={() => setPreset('blank')}><Box size={21}/><strong>空白产品</strong><small>从零定义产品功能</small></button><button type="button" className={preset === 'example' ? 'selected' : ''} onClick={() => { setPreset('example'); if (!name) setName('空气净化加湿一体机'); setCategory('环境电器') }}><Sparkles size={21}/><strong>空气治理示例</strong><small>预置 5 个功能、2 个参数</small></button></div><Field label="产品名称"><input autoFocus placeholder="例如 桌面空气净化器" value={name} maxLength={80} onChange={e => setName(e.target.value)}/></Field><Field label="产品标识" hint="小写字母开头，使用小写字母、数字和下划线；标识在工作区中唯一。"><input className="mono" value={code} maxLength={48} onChange={e => setCode(e.target.value)}/></Field><Field label="产品品类"><input value={category} maxLength={40} onChange={e => setCategory(e.target.value)}/></Field>{error && <p className="form-error" role="alert">{error}</p>}</div><div className="modal-footer"><button type="button" className="button" disabled={busy} onClick={onClose}>取消</button><button className="button primary" type="submit" disabled={busy}>{busy && <Loader2 size={15} className="spin"/>}创建产品<ArrowRight size={15}/></button></div></form></Modal>
}
function SettingsPage({ connected, templates }: { connected: boolean; templates: TemplateDescriptor[] }) {
  return <><div className="page-heading"><div><div className="eyebrow">WORKSPACE SETTINGS</div><h1>平台设置<span className="heading-dot">.</span></h1><p>当前工作区的运行信息与能力边界。</p></div></div><section className="card settings-card"><h2>工作区信息</h2><dl><div><dt>平台版本</dt><dd>APL Studio {STUDIO_VERSION}</dd></div><div><dt>运行方式</dt><dd>本地 Node.js 服务 + 浏览器</dd></div><div><dt>数据存储</dt><dd>SQLite · apl_studio/.data/studio.db（默认）</dd></div><div><dt>实时连接</dt><dd><span className={'status-dot ' + (connected ? 'online' : '')}/>{connected ? '已连接' : '等待重新连接'}</dd></div><div><dt>产品定义格式</dt><dd>JSON · schemaVersion 1</dd></div><div><dt>源码模板</dt><dd>{templates.length} 个版本 · 启动时加载独立目录</dd></div><div><dt>SDK 生成</dt><dd>真实 C 源码 · 本机编译 · 基础消费验证 · ZIP 交付</dd></div><div><dt>仿真台</dt><dd>AP / 灯控实际 C 程序 · 环境注入 · 输出回调 · 事件记录</dd></div><div><dt>Agent / MCP</dt><dd>后续接入同一业务服务</dd></div></dl></section><div className="note-card settings-note"><ShieldCheck size={20}/><div><strong>配置、生成、验证、交付已接通</strong><p>产品创建后持久保存。仿真台运行生成的 C 业务代码，模型预览检查数据类型，SDK 生成会编译并运行 C 验证程序；目标 MCU 与硬件移植仍需工程师验证。当前没有账户和多租户系统。</p></div></div></>
}
