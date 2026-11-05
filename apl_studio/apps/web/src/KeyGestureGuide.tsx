import { useEffect, useRef, useState } from 'react'
import { Pause, Play, RotateCcw } from 'lucide-react'

export type GestureGuideAction = { name: string; description: string; available: boolean; note?: string }
type Props = { keyName: string; short: GestureGuideAction; long: GestureGuideAction; standard: boolean }

/** Configuration help only. This component never calls the product or simulation API. */
export function KeyGestureGuide({ keyName, short, long, standard }: Props) {
  const guideRef = useRef<HTMLElement>(null)
  const [gesture, setGesture] = useState<'short' | 'long'>('short')
  const [elapsed, setElapsed] = useState(0), elapsedRef = useRef(0)
  const [started, setStarted] = useState(false), [playing, setPlaying] = useState(false)
  const [reducedMotion, setReducedMotion] = useState(false)
  const action = gesture === 'short' ? short : long
  const longPress = gesture === 'long', triggerAt = longPress ? 3000 : 450
  const duration = longPress ? 3500 : 1050

  useEffect(() => {
    const media = window.matchMedia('(prefers-reduced-motion: reduce)')
    setReducedMotion(media.matches)
    const change = () => { setReducedMotion(media.matches); setPlaying(false) }
    media.addEventListener('change', change)
    return () => media.removeEventListener('change', change)
  }, [])
  useEffect(() => {
    const details = guideRef.current?.closest('details')
    const change = () => { if (details && !details.open) setPlaying(false) }
    details?.addEventListener('toggle', change)
    return () => details?.removeEventListener('toggle', change)
  }, [])
  useEffect(() => {
    setPlaying(false); setStarted(false); setElapsed(0); elapsedRef.current = 0
  }, [gesture, keyName, action.name, action.available, action.note, standard])
  useEffect(() => {
    if (!playing || reducedMotion) return
    let frame = 0, previous = performance.now()
    const tick = (now: number) => {
      elapsedRef.current = Math.min(duration, elapsedRef.current + now - previous)
      previous = now; setElapsed(elapsedRef.current)
      if (elapsedRef.current < duration) frame = requestAnimationFrame(tick)
      else setPlaying(false)
    }
    frame = requestAnimationFrame(tick)
    return () => cancelAnimationFrame(frame)
  }, [playing, duration, reducedMotion])

  const restart = () => {
    setStarted(true); elapsedRef.current = reducedMotion ? duration : 0
    setElapsed(elapsedRef.current); setPlaying(!reducedMotion)
  }
  const toggle = () => {
    if (!started || elapsed >= duration || reducedMotion) restart()
    else setPlaying(value => !value)
  }
  const pressed = started && elapsed < (longPress ? 3200 : 390)
  const triggered = started && elapsed >= triggerAt
  const stage = !action.available ? 'inactive' : !started ? 'ready' : triggered ? 'done' : longPress ? 'hold' : pressed ? 'press' : 'release'
  const step = stage === 'ready' || stage === 'inactive' ? 0 : triggered ? 3 : pressed ? (longPress ? 2 : 1) : 2
  const status = !action.available ? action.note || '当前动作不会执行'
    : !started ? '点击演示，查看这一手势的操作过程。'
      : triggered ? `示意结果：${action.name}。${playing ? '' : '可重播查看操作。'}`
        : !playing ? '演示已暂停，点击继续即可接着播放。'
          : longPress ? '保持按住，约 3 秒后识别为长按。'
            : pressed ? '轻触按下，然后松开。' : '松开后识别为短按。'

  return <aside ref={guideRef} className="kg-guide" aria-label={`${keyName} 操作示意`} data-gesture={gesture} data-playing={playing} data-stage={stage}>
    <div className="kg-heading"><strong>操作示意</strong><span>{standard ? '按当前标准映射' : '按当前自定义映射'}</span></div>
    <div className="kg-tabs" role="group" aria-label={`${keyName} 演示手势`}>
      <button type="button" aria-pressed={!longPress} onClick={() => setGesture('short')}>短按</button>
      <button type="button" aria-pressed={longPress} onClick={() => setGesture('long')}>长按 · 约 3 秒</button>
    </div>
    <svg className="kg-scene" viewBox="0 0 360 164" aria-hidden="true">
      <circle className="kg-key-outline" cx="79" cy="77" r="46"/>
      <circle className="kg-key-progress" cx="79" cy="77" r="46" pathLength="100" strokeDasharray="100" strokeDashoffset={started && action.available ? 100 - Math.min(elapsed / triggerAt, 1) * 100 : 100} transform="rotate(-90 79 77)"/>
      <g className={`kg-key ${pressed ? 'is-pressed' : ''}`}>
        <circle cx="79" cy="77" r="35"/>
        <text x="79" y="83" textAnchor="middle">{keyName}</text>
      </g>
      <g className={`kg-touch ${pressed ? 'is-touching' : ''}`}><path d="M79 12v19m-7-7 7 7 7-7"/><circle cx="79" cy="37" r="4"/></g>
      <path className={`kg-arrow ${triggered ? 'is-triggered' : ''}`} d="M141 77h44m-8-8 8 8-8 8"/>
      <g className={`kg-result ${triggered && action.available ? 'is-triggered' : ''}`}>
        <rect x="201" y="44" width="149" height="66" rx="12"/>
        <text x="275.5" y="70" textAnchor="middle">{action.available ? action.name : '当前不执行'}</text>
        <text className="kg-result-sub" x="275.5" y="94" textAnchor="middle">{triggered ? '示意触发' : longPress ? '保持约 3 秒后' : '松开后'}</text>
      </g>
      <text className="kg-timing" x="79" y="148" textAnchor="middle">{longPress ? `${started ? Math.min(elapsed / 1000, 3).toFixed(1) : '0.0'} / 3.0 秒` : '轻触 → 松开'}</text>
    </svg>
    <ol className="kg-steps" aria-label="操作步骤">
      <li data-active={step === 1}><span>1</span>按下 {keyName}</li>
      <li data-active={step === 2}><span>2</span>{longPress ? '保持约 3 秒' : '松开按键'}</li>
      <li data-active={step === 3}><span>3</span>{action.available ? action.name : '当前不执行'}</li>
    </ol>
    <p className="kg-description"><strong>{action.name}</strong> · {action.description}</p>
    {action.note && <p className="kg-note">{action.note}</p>}
    <p className="kg-status" role="status" aria-live="polite">{status}</p>
    <div className="kg-controls">
      <button type="button" disabled={!action.available} onClick={toggle} aria-label={`${keyName} ${playing ? '暂停' : started && elapsed < duration ? '继续' : '播放'}${longPress ? '长按' : '短按'}演示`}>
        {playing ? <Pause size={14}/> : <Play size={14}/>}{reducedMotion ? '查看步骤' : playing ? '暂停' : started && elapsed < duration ? '继续' : '播放演示'}
      </button>
      <button type="button" disabled={!action.available || !started} onClick={restart} aria-label={`重播 ${keyName} ${longPress ? '长按' : '短按'}演示`}><RotateCcw size={14}/>重播</button>
    </div>
    <small className="kg-disclaimer">{reducedMotion ? '已按系统偏好关闭动画，显示完整步骤。' : '仅演示操作方法；'}不会启动仿真或控制设备。</small>
  </aside>
}
