import { useEffect, useRef, useState } from 'react'
import { Play, Pause, Square, Power, RotateCcw, FastForward, Activity, Info, Loader2, Download, Cpu } from 'lucide-react'
import type { Product, Value } from '@apl-studio/model'
import type { SimulationAction, SimulationControl, SimulationDescriptor, SimulationSession, SimulationSlot } from '@apl-studio/model/simulation'
import { api, downloadBlob } from './api'
import { Field, Switch } from './components'

const time = (ms: number) => `${(ms / 1000).toFixed(2)} s`
function valueText(slot: SimulationSlot | undefined, value: Value | undefined) {
  if (value === undefined) return '—'
  if (slot?.boolean) return value ? '开' : '关'
  return (slot?.options?.find(o => o.value === value)?.label ?? String(value).replaceAll('\0', '')) + (slot?.unit ? ' ' + slot.unit : '')
}
type PanelStyle = 'device' | 'bench'
function savedPanelStyle(productId: string): PanelStyle {
  try { return localStorage.getItem('apl-studio:simulation-style:' + productId) === 'bench' ? 'bench' : 'device' } catch { return 'device' }
}
const eventNames = { input: '输入', output: '负载', state: '状态', system: '系统', memory: '存储' }

export function SimulationWorkbench({ product, dirty, valid, onSave, onOverview }: { product: Product; dirty: boolean; valid: boolean; onSave: () => void; onOverview: () => void }) {
  const [panelStyle, setPanelStyle] = useState<PanelStyle>(() => savedPanelStyle(product.id))
  function changePanelStyle(style: PanelStyle) {
    setPanelStyle(style)
    try { localStorage.setItem('apl-studio:simulation-style:' + product.id, style) } catch { /* Display preferences are optional when browser storage is unavailable. */ }
  }
  const [info, setInfo] = useState<SimulationDescriptor | null>(null)
  const [session, setSession] = useState<SimulationSession | null>(null)
  const [loading, setLoading] = useState(true), [busy, setBusy] = useState(false), [error, setError] = useState('')
  const [clearedAt, setClearedAt] = useState(0), [filter, setFilter] = useState('all')
  const owned = useRef<string | null>(null), alive = useRef(true), requestToken = useRef(0), busyRef = useRef(false)
  useEffect(() => { alive.current = true; return () => { alive.current = false; if (owned.current) void api.stopSimulation(owned.current).catch(() => {}) } }, [])
  useEffect(() => {
    let cancelled = false
    setLoading(true)
    api.simulationInfo(product.id).then(result => { if (!cancelled) { setInfo(result.descriptor); setError('') } }).catch(e => { if (!cancelled) setError(e.message) }).finally(() => { if (!cancelled) setLoading(false) })
    return () => { cancelled = true }
  }, [product.id, product.revision])
  useEffect(() => {
    if (!session || !['building', 'ready'].includes(session.status)) return
    let stopped = false, timer: ReturnType<typeof setTimeout>
    async function poll() {
      if (stopped) return
      if (!busyRef.current) {
        const token = ++requestToken.current
        try { const next = await api.simulation(session!.id); if (!stopped && token === requestToken.current) { setSession(next); setError('') } }
        catch (e) { if (!stopped && token === requestToken.current) setError((e as Error).message) }
      }
      if (!stopped) timer = setTimeout(() => void poll(), 250)
    }
    timer = setTimeout(() => void poll(), 250)
    return () => { stopped = true; clearTimeout(timer) }
  }, [session?.id, session?.status])
  async function run(operation: () => Promise<SimulationSession | null>) {
    if (busyRef.current) return
    busyRef.current = true; setBusy(true); setError(''); ++requestToken.current
    try {
      const next = await operation()
      if (!alive.current) { if (next) void api.stopSimulation(next.id).catch(() => {}); return }
      owned.current = next?.id ?? null; setSession(next)
    } catch (e) { if (alive.current) setError((e as Error).message) }
    finally { busyRef.current = false; if (alive.current) setBusy(false) }
  }
  function start() { void run(async () => { if (owned.current) await api.stopSimulation(owned.current); const next = await api.startSimulation(product); setClearedAt(0); return next }) }
  function act(action: SimulationAction) { if (session) void run(() => api.simulate(session.id, action)) }
  function stop() { if (session) void run(async () => { await api.stopSimulation(session.id); return api.simulation(session.id) }) }
  const descriptor = session?.descriptor ?? info, snapshot = session?.snapshot
  const ready = session?.status === 'ready', active = ready || session?.status === 'building'
  const controls = descriptor?.controls ?? [], keys = controls.filter(c => c.kind === 'gesture')
  const events = session?.events.filter(e => e.seq > clearedAt && (filter === 'all' || e.kind === filter)) ?? []
  const disabled = !ready || busy
  const newer = session && session.revision !== product.revision
  return <div className={'simulation-workbench simulation-style-' + panelStyle}>
    <div className="simulation-toolbar card"><div className="simulation-title"><span className="simulation-symbol"><Cpu size={22}/></span><div><h2>产品仿真台</h2><p>{session ? `产品修订 r${session.revision} · ${session.template.id}@${session.template.version}` : '使用已保存的配置，运行实际 C 业务程序'}</p></div></div><div className="simulation-actions">
      <span className={'badge ' + (ready ? 'green' : '')}>{session?.status === 'building' ? <><Loader2 size={13} className="spin"/>正在编译</> : ready ? session.running ? '运行中' : '已暂停' : session?.status === 'failed' ? '启动失败' : session?.status === 'stopped' ? '已停止' : '未启动'}</span>
      <button className="button primary" disabled={busy || dirty || !valid || !info || session?.status === 'building'} onClick={start}><Play size={15}/>{active ? '重新启动' : '启动仿真'}</button>
      {active && <button className="button" disabled={busy} onClick={stop}><Square size={14}/>停止</button>}
    </div></div>
    {dirty && <div className="inline-alert warning"><Info size={16}/><span>配置有未保存的更改。{session ? `当前会话仍使用修订 r${session.revision}。` : '保存后才能启动仿真。'}</span><button className="text-button" disabled={!valid} onClick={onSave}>保存更改</button></div>}
    {newer && <div className="inline-alert warning"><Info size={16}/><span>产品已保存为 r{product.revision}，重新启动后应用新配置。</span></div>}
    {session && ['stopped', 'failed'].includes(session.status) && snapshot && <div className="inline-alert"><Info size={16}/><span>会话已结束，下面保留停止前的最后快照。重新启动后可继续操作。</span></div>}
    {error && <div className="inline-alert warning" role="alert">{error}</div>}
    {!loading && !info && !session && <section className="card empty-inline"><Cpu size={30}/><h3>{product.definition.template ? '此模板尚未接入仿真' : '先为产品选择源码模板'}</h3><p>当前支持 AP 净化加湿模板与独立灯控模板。</p><button className="button" onClick={onOverview}>前往产品概览</button></section>}
    {loading && !descriptor && <div className="empty-inline"><Loader2 className="spin"/>正在读取仿真能力</div>}
    {descriptor && <>
      <div className="simulation-style-picker"><div><strong>面板样式</strong><span>切换样式保留运行状态与事件</span></div><label><span className="sr-only">选择仿真面板样式</span><select aria-label="选择仿真面板样式" value={panelStyle} onChange={e => changePanelStyle(e.target.value as PanelStyle)}><option value="device">{descriptor.panel === 'round' ? '圆形面板' : '卡片面板'}</option><option value="bench">{keys.length ? '按键台' : '紧凑控制台'}</option></select></label></div>
      <div className="simulation-clock"><div><Activity size={15}/><strong>虚拟时间 <output>{time(snapshot?.ms ?? 0)}</output></strong><span>{snapshot ? snapshot.powered ? '设备已上电' : '设备已掉电' : '等待启动'} · {descriptor.tickMs} ms / 拍</span></div><div>
        <button className="button small" disabled={disabled} onClick={() => act({ type: 'running', value: !session?.running })}>{session?.running ? <Pause size={13}/> : <Play size={13}/>} {session?.running ? '暂停' : '继续'}</button>
        <button className="button small" disabled={disabled} onClick={() => act({ type: 'advance', ms: 10 })}>单步 10 ms</button>
        <button className="button small" disabled={disabled} onClick={() => act({ type: 'advance', ms: 1000 })}>+1 秒</button>
        <button className="button small" disabled={disabled} onClick={() => act({ type: 'advance', ms: 60000 })}><FastForward size={13}/>+1 分钟</button>
      </div></div>
      <div className="simulation-grid"><div className="stack"><section className="card simulation-panel-card"><div className="section-heading"><div><h2>{descriptor.panel === 'round' ? '设备面板' : '设备控制'}</h2><p>{descriptor.name}</p></div><button className={'button small ' + (snapshot?.powered ? '' : 'primary')} disabled={disabled} onClick={() => act({ type: 'supply', on: !snapshot?.powered })}><Power size={14}/>{snapshot?.powered ? '模拟掉电' : '模拟上电'}</button></div>
        {panelStyle === 'device' && descriptor.panel === 'round' ? <div className={'round-device ' + (!snapshot?.powered ? 'unpowered' : '')}><div className="round-display"><div className="round-status"><span className={snapshot?.display.child_lock ? 'lit' : ''}>LOCK</span><span className={snapshot?.outputs.uv ? 'lit' : ''}>UV</span><span className={snapshot?.outputs.anion ? 'lit' : ''}>Anion</span></div><output className="round-digits" style={{ opacity: Math.max(.15, Number(snapshot?.display.brightness ?? 0) / 255) }}>{String(snapshot?.display.digits ?? '---').replaceAll('\0', ' ') || '---'}</output><span className="round-unit">{String(snapshot?.display.unit ?? '')}</span><div className="round-water">{[1, 2, 3, 4].map(i => <i key={i} className={Number(snapshot?.display.water_level ?? 0) >= i ? 'filled' : ''}/>)}</div></div>
          {keys.map(c => <div className={'round-key round-' + c.position} key={c.key}><button className="panel-key" disabled={disabled || !snapshot?.powered} onClick={() => act({ type: 'input', key: c.key, value: 'short' })} aria-label={`${c.name}短按`}>{c.name}</button><button className="panel-long" disabled={disabled || !snapshot?.powered} onClick={() => act({ type: 'input', key: c.key, value: 'long' })} aria-label={`${c.name}长按`}>长按 3 秒</button></div>)}
        </div> : <div className={panelStyle === 'bench' ? 'bench-device' : ''}>
          {keys.length > 0 && <><div className="bench-display"><div><span>面板显示</span><output style={{ opacity: Math.max(.25, Number(snapshot?.display.brightness ?? 0) / 255) }}>{String(snapshot?.display.digits ?? '---').replaceAll('\0', ' ') || '---'}</output><small>{String(snapshot?.display.unit ?? '')}</small></div><div className="bench-indicators"><span className={snapshot?.display.light ? 'lit' : ''}>灯光 {snapshot?.display.light ? '开' : '关'}</span><span className={snapshot?.display.child_lock ? 'lit' : ''}>童锁 {snapshot?.display.child_lock ? '开' : '关'}</span></div></div><div className="bench-keys">{keys.map(c => <div className="bench-key" key={c.key}><strong>{c.name}</strong><div><button className="button" disabled={disabled || !snapshot?.powered} onClick={() => act({ type: 'input', key: c.key, value: 'short' })} aria-label={`${c.name}短按`}>短按</button><button className="button" disabled={disabled || !snapshot?.powered} onClick={() => act({ type: 'input', key: c.key, value: 'long' })} aria-label={`${c.name}长按`}>长按 3 秒</button></div></div>)}</div></>}
          <div className="simulation-controls">{controls.filter(c => c.target === 'value').map(c => <SimulationInput key={c.key} control={c} value={snapshot?.values[c.key] ?? c.default} disabled={disabled || !snapshot?.powered} onChange={value => act({ type: 'input', key: c.key, value })}/>)}</div>
        </div>}
        <p className="simulation-hint">{descriptor.panel === 'round' ? (panelStyle === 'bench' ? '使用每个按键的短按 / 长按按钮；长按自动推进约 3 秒。Tab 聚焦后可用 Enter / 空格操作。' : '点击按键短按，点击下方文字长按；长按自动推进约 3 秒。Tab 聚焦后可用 Enter / 空格操作。上电全亮持续 2 秒。') : '请求状态与实际输出分别显示。暂停后单步推进，可检查开灯延迟。'}</p>
      </section>
      {controls.some(c => c.target === 'sensor') && <section className="card"><div className="section-heading"><div><h2>环境输入</h2><p>注入硬件读数，业务代码按自己的采样周期读取。</p></div></div><div className="simulation-controls">{controls.filter(c => c.target === 'sensor').map(c => <SimulationInput key={c.key} control={c} value={snapshot?.values[c.key] ?? c.default} disabled={disabled} onChange={value => act({ type: 'input', key: c.key, value })}/>)}</div></section>}
      <section className="card"><div className="section-heading"><div><h2>实际负载</h2><p>来自 C 输出回调；掉电时切断模拟负载。</p></div></div><div className="simulation-loads">{descriptor.outputs.map(slot => <div key={slot.key} className={'simulation-load ' + (slot.boolean && snapshot?.outputs[slot.key] ? 'on' : '')}><span className="load-indicator"/><div><span>{slot.name}</span><strong>{valueText(slot, snapshot?.outputs[slot.key])}</strong></div></div>)}</div></section>
      </div><div className="stack"><section className="card simulation-events"><div className="section-heading"><div><h2>事件记录</h2><p>操作、负载和业务状态分别记录；保留最近 300 条。</p></div><button className="text-button" disabled={!session?.events.length} onClick={() => setClearedAt(session?.events.at(-1)?.seq ?? 0)}>清空</button></div><div className="simulation-event-tools"><select aria-label="筛选仿真事件" value={filter} onChange={e => setFilter(e.target.value)}><option value="all">全部事件</option>{Object.entries(eventNames).map(([value, name]) => <option key={value} value={value}>{name}</option>)}</select><button className="icon-button" disabled={!events.length} aria-label="导出仿真事件" onClick={() => downloadBlob(new Blob([JSON.stringify({ productId: product.id, revision: session?.revision, template: session?.template, driverFingerprint: session?.driverFingerprint, events }, null, 2)], { type: 'application/json' }), product.definition.code + '-simulation.json')}><Download size={16}/></button></div>
        <ol aria-label="仿真事件记录">{[...events].reverse().map(e => <li key={e.seq}><time>{time(e.ms)}</time><span className={'event-kind event-' + e.kind}>{eventNames[e.kind]}</span><span>{e.message}</span></li>)}</ol>{!events.length && <p className="empty-events">启动并操作设备后，在这里查看事件。</p>}
      </section>
      <section className="card"><div className="section-heading"><div><h2>运行信息槽位</h2><p>字段由仿真桥接提供，界面自动显示。</p></div></div><SimulationSlots slots={descriptor.internal} values={snapshot?.internal}/>{descriptor.display.length > 0 && <details className="simulation-details"><summary>面板显示状态</summary><SimulationSlots slots={descriptor.display} values={snapshot?.display}/></details>}<details className="simulation-details"><summary>完整快照与桥接信息</summary><pre>{JSON.stringify({ snapshot, template: session?.template, driverFingerprint: session?.driverFingerprint }, null, 2)}</pre></details></section>
      <section className="card simulation-memory"><div className="section-heading"><div><h2>模拟存储器</h2><p>{descriptor.memory ? '由 APL 保存回调写入，上电后交回恢复。' : '此灯控模板没有掉电记忆，重新上电使用配置默认值。'}</p></div></div>{descriptor.memory && <pre>{snapshot ? JSON.stringify(snapshot.memory, null, 2) : '等待业务程序写入'}</pre>}<button className="button full" disabled={disabled} onClick={() => act({ type: 'reset' })}><RotateCcw size={14}/>重置设备与存储</button><p className="simulation-hint">存储仅在当前仿真会话内保留。离开仿真台或停止会话后释放。</p></section>
      </div></div>
    </>}
    {session?.diagnostics && <details className="card simulation-details" open={session.status === 'failed'}><summary>编译与运行诊断</summary><pre>{session.diagnostics}</pre></details>}
  </div>
}
function SimulationSlots({ slots, values }: { slots: SimulationSlot[]; values?: Record<string, Value> }) {
  const all = [...slots, ...Object.keys(values ?? {}).filter(key => !slots.some(s => s.key === key)).map(key => ({ key, name: key }))]
  return <dl className="simulation-slots">{all.map(slot => <div key={slot.key}><dt>{slot.name}</dt><dd>{valueText(slot, values?.[slot.key])}</dd></div>)}</dl>
}
function SimulationInput({ control: c, value, disabled, onChange }: { control: SimulationControl; value: Value; disabled: boolean; onChange: (v: Value) => void }) {
  const [local, setLocal] = useState(String(value))
  useEffect(() => setLocal(String(value)), [value])
  return <Field label={c.name} hint={c.unit}>{c.kind === 'bool' ? <Switch label={c.name} checked={!!value} disabled={disabled} onChange={onChange}/> : c.kind === 'enum' ? <select aria-label={c.name} disabled={disabled} value={Number(value)} onChange={e => onChange(Number(e.target.value))}>{c.options?.map(o => <option key={o.value} value={o.value}>{o.label}</option>)}</select> : <form onSubmit={e => { e.preventDefault(); onChange(Number(local)) }} className="simulation-number"><input aria-label={c.name} type="number" required disabled={disabled} min={c.min} max={c.max} step={c.step} value={local} onChange={e => setLocal(e.target.value)}/><button type="submit" className="button small" disabled={disabled || local === String(value)}>应用</button></form>}</Field>
}
