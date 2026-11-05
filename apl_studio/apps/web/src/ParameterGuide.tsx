import { useEffect, useId, useState } from 'react'
import { Lightbulb, Pause, Play, RotateCcw } from 'lucide-react'

type Props = { fieldKey: string; values: Record<string, number> }
type Scene = 'flow' | 'humidity' | 'fan' | 'pages' | 'steps' | 'info'
type Guide = { title: string; description: string; labels: [string, string, string]; scene: Scene; valid: boolean; animate?: boolean }
const gears = ['睡眠', '一档', '二档', '三档', '自动', '极速']
const rpmNames: Record<string, string> = { fan_sleep_rpm: '睡眠档', fan_gear1_rpm: '一档', fan_gear2_rpm: '二档', fan_gear3_rpm: '三档', fan_turbo_rpm: '极速档' }
const integer = (value: number, min: number, max: number) => Number.isSafeInteger(value) && value >= min && value <= max
const invalid = (title: string): Guide => ({ title, description: '先填写允许范围内的整数，再查看这项设置的效果。', labels: ['修改数值', '检查范围', '查看示意'], scene: 'info', valid: false })
const count = (value: number) => value.toLocaleString('zh-CN')

function describe(fieldKey: string, values: Record<string, number>): Guide | null {
  const n = values[fieldKey]
  if (fieldKey in rpmNames) {
    if (!integer(n, 0, 65535)) return invalid('各档目标转速')
    return { title: `${rpmNames[fieldKey]} · ${count(n)} rpm`, description: n === 0 ? '这一档的目标转速为 0，表示停转。其他档位保持各自的设置。' : `进入${rpmNames[fieldKey]}时，APL 给出的目标转速为 ${count(n)} rpm。图中转动只作示意，不代表实测转速或实际加速过程。`, labels: [rpmNames[fieldKey], `${count(n)} rpm`, n === 0 ? '目标停转' : '目标值'], scene: 'fan', valid: true, animate: n !== 0 }
  }
  switch (fieldKey) {
    case 'default_fan_gear':
    case 'sensor_fallback_gear': {
      if (!integer(n, 0, fieldKey === 'default_fan_gear' ? 4 : 3)) return invalid('风档设置')
      const fallback = fieldKey === 'sensor_fallback_gear'
      return { title: fallback ? `备用风档 · ${gears[n]}` : `首次开机风档 · ${gears[n]}`, description: fallback ? `自动或恒湿控制需要的传感器输入不可用时，使用${gears[n]}作为备用风档；预处理与其他运行策略仍按 APL 规则执行。` : `尚无运行记忆时，首次开机选择${gears[n]}。之后开机可能恢复已保存的风档，实际输出也会受自动、恒湿等策略影响。`, labels: [fallback ? '输入不可用' : '首次开机', gears[n], fallback ? '备用运行' : '按运行策略输出'], scene: 'flow', valid: true, animate: true }
    }
    case 'turbo_enabled':
      if (!integer(n, 0, 1)) return invalid('极速功能')
      return { title: n ? '允许选择极速档' : '极速功能关闭', description: n ? '允许通过已配置的极速动作进入或退出极速档；开启这一项不会让设备立即加速。' : '极速动作不会进入极速档，其他风档仍可正常选择。', labels: ['极速动作', n ? '允许进入' : '不进入极速', '其他风档保留'], scene: 'flow', valid: true, animate: !!n }
    case 'humidity_hysteresis': {
      if (!integer(n, 0, 100)) return invalid('恒湿回差')
      const lower = 60 - n
      return { title: `恒湿回差 · ${n} 个百分点`, description: lower < 0 ? '以目标 60%RH 为例：这个回差使恢复阈值低于 0%RH，在正常湿度范围内无法达到。可调小回差后比较效果；实际阈值还取决于运行时目标湿度。' : `示例目标为 60%RH：达到 60% 时暂停加湿，${n === 0 ? '低于 60%' : `降到 ${lower}% 及以下`}恢复加湿。回差越大，停泵后需要下降得越多才重新加湿。`, labels: [lower < 0 ? '恢复阈值低于 0%' : `${n === 0 ? '<' : '≤'} ${lower}% · 恢复`, '示例目标 60%RH', '≥ 60% · 暂停'], scene: 'humidity', valid: true, animate: lower >= 0 }
    }
    case 'dry_s':
    case 'dry_fan_gear': {
      if (!integer(values.dry_s, 0, 86400) || !integer(values.dry_fan_gear, 0, 3)) return invalid('关机风干')
      return { title: `关机风干 · ${count(values.dry_s)} 秒`, description: `需要风干的加湿运行结束后，先风干，再进入待机。风干档设为${gears[values.dry_fan_gear]}；纯加湿产品处于睡眠模式时仍沿用睡眠档。并非每次关机都会进入风干。`, labels: ['结束加湿运行', `${gears[values.dry_fan_gear]} · ${count(values.dry_s)} 秒`, '风干结束 → 待机'], scene: 'flow', valid: true, animate: true }
    }
    case 'shortage_run_s':
      if (!integer(n, 0, 86400)) return invalid('缺水恢复窗口')
      return { title: `缺水恢复窗口 · ${count(n)} 秒`, description: values.purify_enabled ? '一体机缺水后停止加湿并保留净化运行；这个窗口主要用于纯加湿产品，不会让一体机在倒计时后停止净化。' : `纯加湿产品缺水后保留恢复窗口；持续缺水达到 ${count(n)} 秒后，进入风干或按已编入的功能停机。`, labels: ['确认缺水', values.purify_enabled ? '停止加湿' : `等待恢复 · ${count(n)} 秒`, values.purify_enabled ? '保留净化' : '风干 / 停机'], scene: 'flow', valid: true, animate: true }
    case 'water_blink_s':
      if (!integer(n, 0, 86400)) return invalid('缺水提醒')
      return { title: `缺水闪烁提醒 · ${count(n)} 秒`, description: `确认缺水后，先按闪烁方式提醒；持续缺水达到 ${count(n)} 秒后转为常亮。这只设置提醒时长，不设置开泵条件。`, labels: ['确认缺水', `闪烁提醒 · ${count(n)} 秒`, '缺水灯常亮'], scene: 'flow', valid: true, animate: true }
    case 'display_rotate_s':
      if (!integer(n, 1, 3600)) return invalid('显示轮换')
      return { title: `显示轮换 · 每页 ${count(n)} 秒`, description: `净化与加湿均启用时，正常数值页按这个间隔轮换。图中 PM2.5、湿度仅为页签示例；预处理、故障和设置画面仍有自己的显示优先级。`, labels: ['PM2.5 数值页', `${count(n)} 秒后`, '湿度数值页'], scene: 'pages', valid: true, animate: true }
    case 'timer_step_h':
      if (!integer(n, 1, 24)) return invalid('定时步进')
      return { title: `定时步进 · 每次 ${n} 小时`, description: `每次定时步进增加 ${n} 小时，超过 24 小时回到 0，表示取消。停止操作约 3 秒后确认当前设置。`, labels: ['0 · 取消', `${n} 小时`, n * 2 > 24 ? '再按 → 0 · 取消' : `再按 → ${n * 2} 小时`], scene: 'steps', valid: true, animate: true }
    case 'filter_life_h':
      if (!integer(n, 1, 1000000)) return invalid('滤网寿命')
      return { title: `滤网寿命 · ${count(n)} 小时`, description: '滤网累计使用时间达到寿命后提醒维护。它是累计计时阈值，不是从本次开机开始的倒计时。', labels: ['累计使用', `${count(n)} 小时`, '到期提醒'], scene: 'flow', valid: true, animate: true }
    case 'filter_shared':
      if (!integer(n, 0, 1)) return invalid('滤网重置方式')
      return { title: n ? '两类滤网一起重置' : '两类滤网分别重置', description: n ? '一体机执行滤网重置时，净化滤芯与加湿滤网的累计计时一起清零。' : '两类滤网各自计时。一体机面板执行共用重置动作时，先清除累计时间较长的一项。', labels: ['滤网重置', n ? '两项一起清零' : '先重置较长的一项', '重新累计'], scene: 'flow', valid: true, animate: true }
    case 'wifi_pair_s':
      if (!integer(n, 1, 86400)) return invalid('配网等待时间')
      return { title: `配网等待 · 最长 ${count(n)} 秒`, description: `进入配网后，最多等待 ${count(n)} 秒；超时结束本次配网。此设置不会创建网络连接，联网仍由宿主通信模块完成。`, labels: ['请求配网', `等待 · ${count(n)} 秒`, '成功 / 超时结束'], scene: 'flow', valid: true, animate: true }
    default: return null
  }
}

export function ParameterGuide({ fieldKey, values }: Props) {
  const guide = describe(fieldKey, values)
  if (!guide) return null
  // A changed setting starts a fresh, idle illustration; it never sends a device event.
  return <GuideCard key={fieldKey + JSON.stringify(values)} fieldKey={fieldKey} guide={guide}/>
}

function GuideCard({ fieldKey, guide }: { fieldKey: string; guide: Guide }) {
  const titleId = useId(), [status, setStatus] = useState<'idle' | 'playing' | 'paused' | 'done'>('idle'), [run, setRun] = useState(0)
  const [reducedMotion, setReducedMotion] = useState(false)
  useEffect(() => {
    const query = window.matchMedia('(prefers-reduced-motion: reduce)')
    const update = () => { setReducedMotion(query.matches); if (query.matches) setStatus('idle') }
    update(); query.addEventListener('change', update)
    return () => query.removeEventListener('change', update)
  }, [])
  const canPlay = guide.valid && guide.animate && !reducedMotion
  const play = () => { if (status === 'playing') setStatus('paused'); else if (status === 'paused') setStatus('playing'); else { setRun(n => n + 1); setStatus('playing') } }
  return <aside className="ps-parameter-guide" data-parameter-guide={fieldKey} aria-labelledby={titleId}>
    <div className="pg-heading"><Lightbulb size={16} aria-hidden="true"/><span>看看这个设置的效果</span><small>配置示意</small></div>
    <strong id={titleId}>{guide.title}</strong><p>{guide.description}</p>
    <div key={run} className={'pg-motion ' + (status !== 'idle' ? 'pg-motion-started' : '')} data-state={status} data-scene={guide.scene} onAnimationEnd={e => { if (e.animationName === 'pg-progress') setStatus('done') }}>
      <svg className="pg-scene" viewBox="0 0 420 112" role="img" aria-label={guide.labels.join(' → ')}>
        {guide.scene === 'fan' ? <>
          <circle cx="210" cy="43" r="33" className="pg-fan-rim"/>
          <g className="pg-fan-blades"><path d="M210 43c-24-1-26-27-7-27 12 0 12 13 7 27zM210 43c11-22 35-11 26 5-7 11-17 6-26-5zM210 43c12 21-10 36-20 19-6-11 4-18 20-19z"/></g>
          <circle cx="210" cy="43" r="5" className="pg-fan-center"/>
          <text x="72" y="48">{guide.labels[0]}</text><text x="348" y="48">{guide.labels[1]}</text>
          <text x="210" y="99" className="pg-svg-subtext">{guide.labels[2]}</text>
        </> : guide.scene === 'humidity' ? <>
          <path d="M38 58H382" className="pg-track"/><path d="M125 58H295" className="pg-band"/>
          <circle cx="125" cy="58" r="7" className="pg-node"/><circle cx="295" cy="58" r="7" className="pg-node"/>
          <circle cx="125" cy="58" r="5" className="pg-humidity-dot"/>
          <text x="210" y="22">{guide.labels[1]}</text><text x="109" y="99" className="pg-svg-subtext">{guide.labels[0]}</text><text x="310" y="99" className="pg-svg-subtext">{guide.labels[2]}</text>
        </> : <>
          <path d="M74 43H346" className="pg-track"/>
          {[0, 1, 2].map(index => <g key={index} className={`pg-step pg-step-${index}`}><circle cx={70 + index * 140} cy="43" r="18" className="pg-node"/><text x={70 + index * 140} y="48" className="pg-step-number">{index + 1}</text><text x={70 + index * 140} y="88" className="pg-svg-subtext">{guide.labels[index]}</text></g>)}
        </>}
      </svg>
      <div className="pg-progress-track" aria-hidden="true"><span className="pg-progress-fill"/></div>
    </div>
    <div className="pg-footer">{canPlay && <button type="button" className="pg-play" onClick={play} aria-label={`${status === 'playing' ? '暂停' : status === 'paused' ? '继续' : status === 'done' ? '重播' : '播放'}${guide.title}示意`}>{status === 'playing' ? <Pause size={13}/> : status === 'done' ? <RotateCcw size={13}/> : <Play size={13}/>} {status === 'playing' ? '暂停' : status === 'paused' ? '继续' : status === 'done' ? '重播示意' : '播放示意'}</button>}<span>{!guide.valid ? '有效数值填写后可查看示意' : reducedMotion ? '已按系统偏好显示静态示意' : '过程已压缩展示，不运行设备仿真'}</span></div>
  </aside>
}
