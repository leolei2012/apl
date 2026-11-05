import { useEffect, useRef, useState, type ComponentType, type SVGProps } from 'react'
import { Clock3, Droplet, Fan, Layers2, LockKeyhole, Power, Sun, Wifi } from 'lucide-react'
import type { SimulationControl, SimulationSnapshot } from '@apl-studio/model/simulation'
import type { ProductFeatures } from '@apl-studio/model/profile'
import { keyMappingActions, type ProfileKeyMapping } from '@apl-studio/model/key-mapping'

type Gesture = 'short' | 'long'
type Props = { startLabel?: string; features?: ProductFeatures; keyMapping?: ProfileKeyMapping; snapshot: SimulationSnapshot | null | undefined; controls: SimulationControl[]; disabled: boolean; busy?: boolean; onGesture: (key: string, gesture: Gesture) => Promise<void> }
type PanelKey = { key: string; name: string; x: number; y: number; icon: ComponentType<SVGProps<SVGSVGElement>>; legend: string; index: number }
const keys: PanelKey[] = [
  { key: 'Purify', name: '净化', x: 50, y: 10, icon: Layers2, legend: 'Wifi (3s)', index: 0 },
  { key: 'Humidity', name: '加湿', x: 15, y: 31, icon: Droplet, legend: 'Humidistat (3s)', index: 1 },
  { key: 'Timer', name: '定时', x: 85, y: 31, icon: Clock3, legend: 'Lock (3s)', index: 2 },
  { key: 'Light', name: '灯光', x: 15, y: 70, icon: Sun, legend: '', index: 3 },
  { key: 'Speed', name: '风速', x: 85, y: 70, icon: Fan, legend: 'Turbo (3s)', index: 4 },
  { key: 'Power', name: '电源', x: 50, y: 91, icon: Power, legend: '', index: 5 },
]
function FilterSymbol(props: SVGProps<SVGSVGElement>) {
  return <svg viewBox="0 0 30 30" {...props}><ellipse cx="13" cy="7" rx="8" ry="3"/><path d="M5 7v13c0 4 16 4 16 0V7M10 10v10m6-10v10"/><circle cx="23" cy="23" r="5"/><path d="m20 23 2 2 4-4"/></svg>
}
function FanGearSymbol({ gear }: { gear: 1 | 2 | 3 }) {
  return <svg className="ac-gear-waves" viewBox="0 0 32 32" aria-hidden="true">
    {Array.from({ length: gear }, (_, index) => {
      const y = 16 + (index - (gear - 1) / 2) * 8
      return <path key={index} d={`M3 ${y} C7 ${y - 4} 11 ${y - 4} 16 ${y} S25 ${y + 4} 29 ${y}`}/>
    })}
  </svg>
}
const segments: Record<string, string> = {
  '0': 'abcdef', '1': 'bc', '2': 'abdeg', '3': 'abcdg', '4': 'bcfg', '5': 'acdfg', '6': 'acdefg', '7': 'abc', '8': 'abcdefg', '9': 'abcdfg',
  A: 'abcefg', b: 'cdefg', C: 'adef', c: 'deg', d: 'bcdeg', E: 'adefg', F: 'aefg', H: 'bcefg', h: 'cefg', L: 'def', o: 'cdeg', O: 'abcdef', P: 'abefg', U: 'bcdef', u: 'cde', '-': 'g', '_': 'd', '^': 'a', ' ': '',
}
const paths: Record<string, string> = {
  a: '8,2 38,2 43,6 38,10 8,10 3,6', b: '40,12 44,8 48,13 48,40 44,44 40,40',
  c: '40,50 44,46 48,50 48,77 44,82 40,78', d: '8,80 38,80 43,84 38,88 8,88 3,84',
  e: '0,50 4,46 8,50 8,78 4,82 0,77', f: '0,13 4,8 8,12 8,40 4,44 0,40',
  g: '8,41 38,41 43,45 38,49 8,49 3,45',
}

export function PanelDigits({ value, brightness }: { value: string; brightness: number }) {
  return <svg viewBox="0 0 170 90" aria-hidden="true" className="ac-digits-svg">{value.slice(0, 3).padEnd(3, ' ').split('').map((char, index) =>
    <g key={index} transform={`translate(${index * 60},0)`}>{Object.entries(paths).map(([segment, points]) => <polygon key={segment} points={points} fill="currentColor" opacity={(segments[char] ?? segments[char.toUpperCase()] ?? '').includes(segment) ? brightness : .045}/>)}</g>
  )}</svg>
}

/** Face artwork is independent of policy: every illuminated element comes from a C display snapshot. */
export function AirCarePanel({ snapshot, controls, disabled, busy = false, features, keyMapping, onGesture, startLabel = '开始体验' }: Props) {
  const [holding, setHolding] = useState(''), [sending, setSending] = useState(false)
  const timer = useRef<ReturnType<typeof setTimeout>>()
  const press = useRef<{ key: string; pointerId: number; started: number; fired: boolean } | null>(null)
  const pending = useRef(false)
  const unavailable = disabled || !snapshot?.powered
  const blocked = unavailable || busy || sending
  const display = snapshot?.display ?? {}
  const channel = (value: unknown) => snapshot?.powered && typeof value === 'number' && Number.isFinite(value) ? Math.round(Math.max(0, Math.min(255, value))) : 0
  // RGB already includes brightness in the native host output; never classify air quality here.
  const ambientRgb = [channel(display.ambient_red), channel(display.ambient_green), channel(display.ambient_blue)]
  const ambientOn = !!snapshot?.powered && Number(display.ambient) > 0 && ambientRgb.some(v => v > 0)
  const brightness = (value: unknown) => snapshot?.powered && typeof value === 'number' && Number.isFinite(value) ? Math.max(0, Math.min(1, value / 255)) : 0
  const lamp = (index: number) => brightness(display['icon_' + index])
  const waterLamp = (index: number) => typeof display['water_' + index] === 'number'
    ? brightness(display['water_' + index]) : snapshot?.powered && Number(display.water_level ?? 0) >= index ? 1 : 0
  const digitBrightness = brightness(display.brightness), digits = String(display.digits ?? '').replaceAll('\0', ' ')
  const unit = String(display.unit ?? '')
  // Older SDK workspaces keep their existing face unless capabilities are supplied.
  const purify = features?.purify ?? true, humidify = features?.humidify ?? true
  const custom = keyMapping?.enabled ? keyMapping : null
  const panelKeys = keys.map(k => !custom && k.key === 'Humidity' && purify && !humidify
    ? { ...k, key: 'Filter', name: '滤芯复位', icon: FilterSymbol, legend: 'Reset (3s)', index: 6 }
    : !custom && k.key === 'Purify' && !purify ? { ...k, name: '联网' } : k)
    .filter(k => !!custom || k.key !== 'Humidity' || humidify)
    .map(k => custom ? { ...k, name: keyMappingActions[custom.short[k.index]]?.name ?? '无动作',
      legend: custom.long[k.index] ? `${keyMappingActions[custom.long[k.index]]?.name ?? '无动作'} (3s)` : '' } : k)
  function cancel() { clearTimeout(timer.current); press.current = null; setHolding('') }
  useEffect(() => {
    const clear = () => { clearTimeout(timer.current); press.current = null; setHolding('') }
    const visibility = () => { if (document.hidden) clear() }
    window.addEventListener('blur', clear); window.addEventListener('pagehide', clear); document.addEventListener('visibilitychange', visibility)
    return () => { clearTimeout(timer.current); window.removeEventListener('blur', clear); window.removeEventListener('pagehide', clear); document.removeEventListener('visibilitychange', visibility) }
  }, [])
  useEffect(() => { if (blocked) cancel() }, [blocked])
  async function send(key: string, gesture: Gesture) {
    if (blocked || pending.current) return
    pending.current = true; setSending(true)
    try { await onGesture(key, gesture) } finally { pending.current = false; setSending(false) }
  }
  return <div className="ac-panel-layout">
    <div className="ac-panel-stage">
      <div className="ac-panel" data-testid="air-care-panel" data-purify={purify} data-humidify={humidify} data-key-mapping={!!custom} aria-label="圆形设备交互面板" aria-busy={busy || sending}>
        {purify && <div className="ac-ambient" data-lit={ambientOn} data-rgb={ambientRgb.join(',')} aria-label={`氛围灯${ambientOn ? '亮' : '灭'}`} style={{ color: `rgb(${ambientRgb.join(', ')})` }}>{Array.from({ length: 30 }, (_, i) => <i key={i}/>)}</div>}
        <div className="ac-display" data-testid="panel-digits" data-value={digits} aria-label={digitBrightness ? (digits === '^^^' ? '预处理：三位横杠在上段' : digits === '---' ? '预处理：三位横杠在中段' : digits === '___' ? '预处理：三位横杠在下段' : `数码管 ${digits} ${unit}`) : '数码管已熄灭'}>
          <PanelDigits value={digits} brightness={digitBrightness}/>
        </div>
        <div className="ac-units" aria-label="显示单位">{[{ label: 'PM2.5', value: 'PM2.5', visible: purify }, { label: '%RH', value: '%', visible: humidify }, { label: 'H', value: 'H', visible: true }].filter(u => u.visible).map(u => <span key={u.value} data-unit={u.value} style={{ opacity: unit === u.value ? .08 + .92 * digitBrightness : .08 }}>{u.label}</span>)}</div>
        <div className="ac-indicators ac-indicators-left">
          {purify && <span aria-label={`负离子灯${lamp(4) ? '亮' : '灭'}`} style={{ gridRow: 1, opacity: .08 + .92 * lamp(4) }} className="ac-anion">⊖<small>○</small></span>}
          <Wifi aria-label={`Wi-Fi灯${lamp(0) ? '亮' : '灭'}`} style={{ gridRow: 2, opacity: .08 + .92 * lamp(0) }}/>
          <FilterSymbol aria-label={`滤网灯${lamp(2) ? '到期' : lamp(1) ? '正常' : '灭'}`} style={{ gridRow: 3, opacity: .08 + .92 * Math.max(lamp(1), lamp(2)), color: lamp(2) ? '#ff355b' : 'inherit' }}/>
          <LockKeyhole aria-label={`童锁灯${lamp(3) ? '亮' : '灭'}`} style={{ gridRow: 4, opacity: .08 + .92 * lamp(3) }}/>
        </div>
        {purify && <span className="ac-uv" aria-label={`UV灯${lamp(5) ? '亮' : '灭'}`} style={{ opacity: .08 + .92 * lamp(5) }}>UV</span>}
        <div className="ac-gears" aria-label="风速指示灯">{['睡眠', '一档', '二档', '三档', '自动'].map((name, i) => <span key={name} aria-label={`${name}灯${lamp(8 + i) ? '亮' : '灭'}`} style={{ opacity: .08 + .92 * lamp(8 + i) }}>{i === 0 ? 'Sleep' : i === 4 ? 'Auto' : <FanGearSymbol gear={i as 1 | 2 | 3}/>}</span>)}</div>
        {humidify && <div className="ac-water-row"><svg viewBox="0 0 30 30" style={{ opacity: .08 + .92 * lamp(6) }} aria-label={`恒湿灯${lamp(6) ? '亮' : '灭'}`}><path d="M15 2C10 9 5 14 5 20a10 10 0 0 0 20 0c0-6-5-11-10-18Z M11 24l8-11"/><circle cx="11" cy="15" r="1.5"/><circle cx="19" cy="23" r="1.5"/></svg><div className="ac-water" aria-label={`水位 ${Number(display.water_level ?? 0)}格`}>{[1, 2, 3, 4].map(i => <i key={i} className={waterLamp(i) > 0 ? 'is-lit' : ''} style={{ opacity: waterLamp(i) > 0 ? waterLamp(i) : 1 }}/>)}</div><svg viewBox="0 0 30 30" aria-label={`风干灯${lamp(7) ? '亮' : '灭'}`} style={{ opacity: .08 + .92 * lamp(7) }}><ellipse cx="11" cy="6" rx="7" ry="3"/><path d="M4 6v17c0 4 14 4 14 0V6m1 6c5-4 6 4 10 0m-10 6c5-4 6 4 10 0"/></svg></div>}
        {panelKeys.map(k => {
          const control = controls.find(c => c.key === k.key && c.kind === 'gesture'), Icon = k.icon
          return <div className={'ac-key ac-key-' + k.key + (unavailable || !control ? ' is-unavailable' : '') + (holding === k.key ? ' is-holding' : '')} style={{ left: k.x + '%', top: k.y + '%' }} key={k.key}>
            <button type="button" className="ac-touch-key" disabled={blocked || !control} aria-label={`${k.name}短按`} title={!snapshot ? `请先${startLabel}` : control ? `${k.key} 输入：${k.name}；点击短按，按住3秒长按` : `${k.name}未配置`} onContextMenu={e => e.preventDefault()}
              onPointerDown={e => { if (e.button !== 0 || blocked || press.current) return; e.currentTarget.setPointerCapture(e.pointerId); press.current = { key: k.key, pointerId: e.pointerId, started: Date.now(), fired: false }; setHolding(k.key); timer.current = setTimeout(() => { if (press.current?.key === k.key) { press.current.fired = true; void send(k.key, 'long') } }, 3000) }}
              onPointerMove={e => { if (press.current?.key !== k.key) return; const r = e.currentTarget.getBoundingClientRect(); if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) cancel() }}
              onPointerUp={e => { const p = press.current; if (!p || p.pointerId !== e.pointerId || p.key !== k.key) return; const gesture = Date.now() - p.started >= 3000 ? 'long' : 'short'; cancel(); if (!p.fired) void send(k.key, gesture) }}
              onPointerCancel={cancel} onLostPointerCapture={cancel} onBlur={cancel}
              onKeyDown={e => { if (e.repeat && ['Enter', ' '].includes(e.key)) e.preventDefault() }}
              onClick={e => { if (e.detail === 0 && !press.current) void send(k.key, 'short') }}>
              <Icon aria-hidden="true" strokeWidth={1.45} style={{ opacity: .22 + .78 * brightness(display['key_' + k.index]) }}/>
              <span className="ac-key-caption">{k.name}</span>
            </button>
            {k.legend && <button type="button" className="ac-long-key" disabled={blocked || !control} aria-label={`${k.name}长按3秒`} onClick={() => void send(k.key, 'long')}>{k.legend}</button>}
          </div>
        })}
      </div>
    </div>
    <p className="ac-panel-instructions">点击图标短按，按住3秒长按；也可点击图标下方的3s文字。Tab与回车可操作按键。</p>
    {!snapshot && <p className="ac-panel-waiting">点击“{startLabel}”，点亮你的产品</p>}
    <details className="ac-panel-accessible"><summary>全部按键与长按操作</summary><div>{controls.filter(c => c.kind === 'gesture').map(c => <div key={c.key}><span>{panelKeys.find(k => k.key === c.key)?.name ?? c.name}</span><button type="button" className="button small" disabled={blocked} onClick={() => void send(c.key, 'short')}>短按</button><button type="button" className="button small" disabled={blocked} onClick={() => void send(c.key, 'long')}>长按3秒</button></div>)}</div></details>
  </div>
}
