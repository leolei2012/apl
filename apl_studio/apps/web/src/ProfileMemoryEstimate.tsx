import { useEffect, useState } from 'react'
import type { FirmwareBuild } from '@apl-studio/model/firmware'
import { estimateMemory, memorySettings, type MemorySettings, type MemoryStatus } from '@apl-studio/model/memory'

const statusNames: Record<MemoryStatus, string> = { unknown: '信息不完整', over: '超出预算', tight: '余量较小', 'within-budget': '预算内 · 估算' }
const bytes = (n: number | null) => n === null ? '未填写' : `${n.toLocaleString()} B`

function MemoryNumber({ label, value, onChange, min = 0, max = 1073741824, optional = false, hint }: {
  label: string; value: number | null; onChange: (n: number | null) => void; min?: number; max?: number; optional?: boolean; hint: string
}) {
  const [text, setText] = useState(value === null ? '' : String(value))
  useEffect(() => setText(value === null ? '' : String(value)), [value])
  return <label className="ps-field">{label}<input type="number" min={min} max={max} step={1} value={text} placeholder={optional ? '未填写' : undefined}
    onChange={e => { const raw = e.target.value; setText(raw); const n = Number(raw); if (raw === '' && optional) onChange(null); else if (raw !== '' && Number.isSafeInteger(n) && n >= min && n <= max) onChange(n) }}
    onBlur={() => setText(value === null ? '' : String(value))}/><small>{hint}</small></label>
}

export function ProfileMemoryEstimate({ build, onChange }: { build: FirmwareBuild; onChange: (b: FirmwareBuild) => void }) {
  const settings = memorySettings(build), estimate = estimateMemory(build), c51 = build.target === 'c51'
  const update = (key: keyof MemorySettings, n: number | null) => onChange({ ...build, memory: { ...settings, [key]: n } })
  const parts = [
    { name: 'APL 实例', value: estimate.aplBytes, key: 'apl' }, { name: '其他数据', value: estimate.hostStaticBytes, key: 'host' },
    { name: '软件栈', value: estimate.stackBytes, key: 'stack' }, { name: '安全余量', value: estimate.safetyBytes, key: 'reserve' },
  ]
  const barScale = Math.max(build.ramBytes, estimate.knownBytes)
  const remaining = estimate.remainingBytes
  return <section className="ps-memory-estimate" aria-labelledby="memory-estimate-heading">
    <div className="ps-memory-heading"><div><h3 id="memory-estimate-heading">内存估算</h3><p>裁剪方案的预算参考，随设置实时更新。</p></div><span className={'ps-memory-status ' + estimate.status}>{statusNames[estimate.status]}</span></div>
    <div className="ps-fields-grid ps-memory-inputs">
      <MemoryNumber label="APL 实例数量" value={settings.instanceCount} min={1} max={64} onChange={n => n !== null && update('instanceCount', n)} hint="同时保留的完整实例，允许 1～64 个。"/>
      <MemoryNumber label="宿主及其他数据（B）" value={settings.hostStaticBytes} optional onChange={n => update('hostStaticBytes', n)} hint="驱动、缓冲区、RAM中的回调表、共享表及其他预留；不含APL实例和栈。未知请留空。"/>
      <MemoryNumber label="安全余量（B）" value={settings.safetyBytes} onChange={n => n !== null && update('safetyBytes', n)} hint="额外保留的空间，默认 64 B；它是预算预留，不是已用内存。"/>
      {!c51 && <MemoryNumber label="目标单实例 sizeof（B）" value={settings.instanceBytes} min={1} optional onChange={n => update('instanceBytes', n)} hint="填写目标编译器测得的 sizeof(struct apl)；未填写时不使用C51数值替代。"/>}
    </div>
    <div className="ps-memory-totals" aria-live="polite">
      <div><span>{estimate.totalBytes === null ? '已知预算小计' : 'RAM 预算合计'}</span><strong>{bytes(estimate.knownBytes)}</strong><small>总预算 {bytes(build.ramBytes)}</small></div>
      <div><span>{remaining === null ? estimate.knownRemainingBytes < 0 ? '已知部分已超出' : '完整余量待确认' : remaining < 0 ? '超出预算' : '剩余预算'}</span>
        <strong>{remaining === null ? estimate.knownRemainingBytes < 0 ? bytes(-estimate.knownRemainingBytes) : '—' : bytes(Math.abs(remaining))}</strong>
        <small>{estimate.missing.length ? `缺少：${estimate.missing.join('、')}` : '包括栈预留与安全余量'}</small></div>
    </div>
    <div className="ps-memory-bar" role="img" aria-label={`RAM预算组成：${parts.map(p => `${p.name}${bytes(p.value)}`).join('，')}，总预算${bytes(build.ramBytes)}`}>
      {parts.map(p => p.value !== null && p.value > 0 && <span key={p.key} className={'ps-memory-segment ' + p.key} style={{ width: `${p.value / barScale * 100}%` }}/>) }
    </div>
    <dl className="ps-memory-breakdown">{parts.map(p => <div key={p.key}><dt><i className={'ps-memory-dot ' + p.key}/>{p.name}</dt><dd>{bytes(p.value)}</dd></div>)}</dl>
    <p className="ps-memory-caption">{c51
      ? `Keil C51 LARGE 参考：每实例主控 ${estimate.coreBytes} B + HMI ${estimate.hmiBytes} B + 调度 ${estimate.schedulerBytes} B = ${estimate.perInstanceBytes} B，× ${settings.instanceCount} 个。按当前头文件预留计算，未在此处重新链接测量。`
      : estimate.basis === 'manual' ? '单实例大小为手工输入，未自动核验其编译器、配置或来源。' : '通用平台缺少目标实例大小，无法给出完整合计。'}
      {c51 && ' 此处按同一 XDATA 预算池计算；IRAM、硬件栈和中断保存需另行核对。'}
    </p>
    {estimate.status === 'over' && <p className="ps-memory-warning" role="status">当前已知预算已超限。可调整裁剪、实例数量或宿主占用；不要仅根据入口栈帧缩减栈预留。</p>}
    <details className="ps-memory-flash"><summary>Flash 记录（可选）<span className={'ps-memory-status ' + estimate.flash.status}>{estimate.flash.usedBytes === null ? '未测量' : statusNames[estimate.flash.status]}</span></summary>
      <MemoryNumber label="目标链接报告 Flash（B）" value={settings.flashUsedBytes} optional onChange={n => update('flashUsedBytes', n)} hint="手工记录整个目标工程的链接占用。裁剪、编译器或源码变化后需要重新核对。"/>
      <p className="ps-memory-caption">预算 {bytes(estimate.flash.budgetBytes)}；{estimate.flash.usedBytes === null ? '尚无占用数据，不推算完整固件体积。' : `手填占用 ${bytes(estimate.flash.usedBytes)}，${estimate.flash.remainingBytes! < 0 ? '超出' : '剩余'} ${bytes(Math.abs(estimate.flash.remainingBytes!))}。工具未验证该输入。`}</p>
    </details>
    <p className="ps-memory-caption">切换平台、芯片或编译开关会清除手填的单实例大小与Flash记录；源码、编译器或其他配置变化后也需重新核对。估算不代表芯片验收通过。目标工程须完整链接，核对各内存空间、共享数据及最坏调用链栈；不要重复计算实例内已包含的配置和快照。</p>
  </section>
}
