import type { Definition } from '@apl-studio/model'
import { buildOptions, buildPreset, firmwareBuild, setFirmwareBuild, type BuildPreset, type FirmwareBuild } from '@apl-studio/model/firmware'
import { ProfileMemoryEstimate } from './ProfileMemoryEstimate'
import { memorySettings } from '@apl-studio/model/memory'

const presets: { id: BuildPreset; name: string; detail: string }[] = [
  { id: 'full', name: '全功能', detail: '保留全部能力，后续通过参数切换产品。' },
  { id: 'product', name: '按产品裁剪', detail: '只编入当前选用的业务、模块和传感器。' },
  { id: 'compact', name: '紧凑面板', detail: '按产品裁剪；去掉每键自定义回调与滤网缓存，保留两组组合键和蜂鸣器。' },
  { id: 'minimal', name: '最小面板', detail: '进一步去掉组合键和全部蜂鸣提示，包含报警音。' },
  { id: 'core-only', name: '纯主控', detail: '去掉APL按键与显示，宿主自行实现交互；当前面板不能仿真。' },
]
export function ProfileFirmwareBuild({ definition, onChange }: { definition: Definition; onChange: (d: Definition) => void }) {
  const build = firmwareBuild(definition)
  const preset = presets.find(p => JSON.stringify(buildPreset(definition, p.id).defines) === JSON.stringify(build.defines))
  const update = (next: FirmwareBuild) => {
    const changed = next.target !== build.target || next.chip !== build.chip || JSON.stringify(next.defines) !== JSON.stringify(build.defines)
    if (changed && next.memory) next = { ...next, memory: { ...memorySettings(next), instanceBytes: null, flashUsedBytes: null } }
    onChange(setFirmwareBuild(definition, next))
  }
  const budget = (key: 'flashKb' | 'ramBytes' | 'stackBytes', value: string) => {
    const number = Number(value)
    if (Number.isSafeInteger(number) && number >= (key === 'stackBytes' ? 0 : key === 'ramBytes' ? 128 : 1)) update({ ...build, [key]: number })
  }
  return <section className="ps-card ps-firmware-build">
    <div className="ps-card-heading"><span>05</span><div><h2>芯片与固件裁剪</h2><p>决定哪些代码编入固件。修改后需要在目标芯片工程重新编译。</p></div></div>
    <div className="ps-fields-grid">
      <label className="ps-field">芯片平台<select value={build.target} onChange={e => update({ ...build, target: e.target.value as FirmwareBuild['target'] })}><option value="portable">通用 C 平台</option><option value="c51">C51 / 8051（Keil LARGE 参考）</option></select></label>
      <label className="ps-field">芯片型号<input maxLength={80} placeholder="填写实际芯片型号（可选）" value={build.chip} onChange={e => update({ ...build, chip: e.target.value })}/></label>
      <label className="ps-field">Flash 预算（KiB）<input type="number" min={1} max={1048576} value={build.flashKb} onChange={e => budget('flashKb', e.target.value)}/></label>
      <label className="ps-field">{build.target === 'c51' ? 'XDATA 预算（B）' : 'RAM 预算（B）'}<input type="number" min={128} max={1073741824} value={build.ramBytes} onChange={e => budget('ramBytes', e.target.value)}/></label>
      <label className="ps-field">软件栈预留（B）<input type="number" min={0} max={build.ramBytes} value={build.stackBytes} onChange={e => budget('stackBytes', e.target.value)}/><small>单独预留，不根据入口栈帧自动缩减。</small></label>
      <label className="ps-field">裁剪预设<select value={preset?.id ?? 'custom'} onChange={e => e.target.value !== 'custom' && update(buildPreset(definition, e.target.value as BuildPreset))}>{!preset && <option value="custom">自定义</option>}{presets.map(p => <option key={p.id} value={p.id}>{p.name}</option>)}</select><small>{preset?.detail ?? '已手动调整编译开关。'}</small></label>
    </div>
    <ProfileMemoryEstimate build={build} onChange={update}/>
    <details className="ps-build-options"><summary>自定义编译开关</summary>
      {[...new Set(buildOptions.map(o => o[2]))].map(group => <div key={group}><div className="ps-subheading">{group}</div><div className="ps-option-grid">{buildOptions.filter(o => o[2] === group).map(([key, name]) =>
        <label className="ps-option" key={key} title={key}><input type="checkbox" checked={!!build.defines[key]} onChange={e => update({ ...build, defines: { ...build.defines, [key]: Number(e.target.checked) } })}/><span>{name}</span></label>
      )}</div></div>)}
      <label className="ps-field">组合键槽位<select value={build.defines.APL_HMI_COMBO_MAX} onChange={e => update({ ...build, defines: { ...build.defines, APL_HMI_COMBO_MAX: Number(e.target.value) } })}>{[0, 2, 3, 4].map(n => <option key={n} value={n}>{n === 0 ? '关闭组合键' : `${n} 组`}</option>)}</select><small>标准面板需要两组。关闭后不能通过组合键复位滤网或进入产测；主控命令仍保留。</small></label>
      <p className="ps-summary-note">关闭蜂鸣器会移除全部提示音和报警音；关闭自定义回调不影响标准面板。定时、滤网、产测、记忆、睡眠、风干及氛围灯可独立裁剪，关闭后对应专用接口不再提供。通用按键/图标槽位保留编号，RAM变化以估算分项为准。裁掉HMI后不得调用apl_hmi_*。</p>
    </details>
  </section>
}
