import type { Definition } from '@apl-studio/model'
import { firmwareBuild } from '@apl-studio/model/firmware'
import { defaultKeyMapping, keyActionAvailable, keyMappingActions, keyMappingSlots, profileKeyMapping, setKeyMapping } from '@apl-studio/model/key-mapping'
import { ChevronDown } from 'lucide-react'
import { KeyGestureGuide, type GestureGuideAction } from './KeyGestureGuide'

type Props = { definition: Definition; onChange: (next: Definition) => void }
const boardKeys: Record<string, { number: number; name: string }> = {
  Power: { number: 1, name: '开关' }, Speed: { number: 2, name: '风速' }, Timer: { number: 3, name: '定时' },
  Purify: { number: 4, name: '净化' }, Humidity: { number: 5, name: '加湿' }, Light: { number: 6, name: '灯光' }, Filter: { number: 7, name: '滤芯' },
}
const displaySlots = [...keyMappingSlots].sort((a, b) => boardKeys[a.key].number - boardKeys[b.key].number)

export function ProfileKeyMapping({ definition, onChange }: Props) {
  const mapping = profileKeyMapping(definition), build = firmwareBuild(definition)
  const hmi = !!build.defines.APL_COMPILE_HMI, compiled = !!build.defines.APL_HMI_COMPILE_KEYMAP
  const standardMapping = defaultKeyMapping(), standard = !compiled || !mapping.enabled
  const effectiveMapping = standard ? standardMapping : mapping
  const actionName = (id: number) => keyMappingActions.find(action => action.id === id)?.name ?? '未知动作'
  const update = (gesture: 'short' | 'long', slot: number, action: number) => onChange(setKeyMapping(definition, {
    ...mapping, [gesture]: mapping[gesture].map((value, index) => index === slot ? action : value),
  }))
  return <section className="ps-card ps-key-mapping" aria-labelledby="key-mapping-heading">
    <div className="ps-card-heading"><span>04</span><div><h2 id="key-mapping-heading">按键动作映射</h2><p>保留宿主输入编号，配置 APL 收到短按、长按后执行的动作。</p></div></div>
    {!hmi ? <p className="ps-summary-note">APL 按键与显示已裁剪，按键由宿主自行处理。若已启用自定义映射，请恢复标准映射后保存，或在固件裁剪中重新启用 APL 按键与显示。</p>
      : <>
          {!compiled ? <p className="ps-summary-note">自定义按键映射已裁剪，当前固件使用标准映射。已保存的配置暂不生效；若已启用自定义映射，请恢复标准映射后保存，或在固件裁剪中重新启用自定义按键映射。下方按标准映射显示操作示意。</p>
            : <><label className="ps-option"><input name="key_mapping.enabled" type="checkbox" checked={mapping.enabled} onChange={e => onChange(setKeyMapping(definition, { ...mapping, enabled: e.target.checked }))}/><span>启用自定义按键动作</span></label>
            <p className="ps-summary-note">{mapping.enabled ? '按下面的表执行动作。' : '当前使用标准映射；开启后可修改下面的动作。'}“无动作”不执行业务操作。组合键仍按原始输入组合识别。</p></>}
          <p className="ps-key-reference">KEY1～KEY6 对应开关、风速、定时、净化、加湿、灯光；KEY7 为选配滤芯键。编号用于输入对应说明，图中排列不代表 PCB 位置。动作可以重新配置，输入编号保持不变。</p>
          <div className="ps-parameter-groups">{displaySlots.map(slot => {
            const available = !!build.defines[slot.macro], disabled = !mapping.enabled || !available, keyName = `KEY${boardKeys[slot.key].number}`
            const guideAction = (gesture: 'short' | 'long'): GestureGuideAction => {
              const id = effectiveMapping[gesture][slot.id], action = keyMappingActions.find(item => item.id === id)
              const active = available && id !== 0 && keyActionAvailable(definition, id)
              return { name: action?.name ?? '未知动作', description: action?.description ?? '当前没有可用的动作说明。', available: active,
                note: !available ? '此输入槽位已从固件裁剪，当前不会识别这一按键。' : id === 0 ? '此手势设为“无动作”，不执行业务操作或唤醒显示。'
                  : !active ? '此动作对应的功能未启用或已裁剪，当前不会执行；选择会保留。' : standard ? '当前使用标准映射；已保存的自定义选择在启用后才生效。' : undefined }
            }
            return <details key={slot.key} className="ps-parameter-group" data-key-slot={slot.key} open={slot.key === 'Power'}>
              <summary><div className="ps-parameter-group-title"><strong>{keyName} · {boardKeys[slot.key].name}</strong><span>{`${slot.key} · ${slot.id}`}</span><p>{available ? `${standard ? '当前标准映射' : '当前自定义映射'} · 短按：${actionName(effectiveMapping.short[slot.id])}；长按：${actionName(effectiveMapping.long[slot.id])}` : '此输入槽位已从固件裁剪，保存的映射暂不执行。'}</p></div><ChevronDown size={17} aria-hidden="true"/></summary>
              <div className={`ps-key-config-body ${!compiled ? 'is-standard' : ''}`}>
              {compiled && <div className="ps-fields-grid ps-parameter-group-fields">{(['short', 'long'] as const).map(gesture => {
                const selected = mapping[gesture][slot.id], active = keyActionAvailable(definition, selected)
                return <label className="ps-field" key={gesture}>{gesture === 'short' ? '短按动作' : '长按动作'}
                  <select name={`key_mapping.${gesture}.${slot.key}`} aria-label={`${slot.key} ${gesture === 'short' ? '短按' : '长按'}动作`} value={selected} disabled={disabled} onChange={e => update(gesture, slot.id, Number(e.target.value))}>
                    {keyMappingActions.map(action => {
                      const enabled = keyActionAvailable(definition, action.id)
                      return <option key={action.id} value={action.id} disabled={!enabled && action.id !== selected} title={action.description}>{action.name}{enabled ? '' : '（当前未启用）'}</option>
                    })}
                  </select>
                  <small>{!active ? '此动作对应的功能未启用或已裁剪，当前不会执行；选择会保留。' : keyMappingActions.find(action => action.id === selected)?.description}</small>
                </label>
              })}</div>}
              <KeyGestureGuide keyName={keyName} short={guideAction('short')} long={guideAction('long')} standard={standard}/>
              </div>
            </details>
          })}</div>
          <p className="ps-summary-note">输入名称和编号代表宿主传入的按键槽位。修改这里不会改变 GPIO、触摸通道或宿主与 APL 的按键绑定；仿真事件仍记录原始输入。</p>
        </>}
    <button className="ps-button" type="button" onClick={() => onChange(setKeyMapping(definition, defaultKeyMapping()))}>恢复标准映射</button>
  </section>
}
