import type { Definition } from '@apl-studio/model'
import { firmwareBuild } from '@apl-studio/model/firmware'
import { defaultKeyMapping, keyActionAvailable, keyMappingActions, keyMappingSlots, profileKeyMapping, setKeyMapping } from '@apl-studio/model/key-mapping'
import { ChevronDown } from 'lucide-react'

type Props = { definition: Definition; onChange: (next: Definition) => void }
const keyNumbers: Record<string, number> = {
  Power: 1, Speed: 2, Timer: 3, Purify: 4, Humidity: 5, Light: 6, Filter: 7,
}
const displaySlots = [...keyMappingSlots].sort((a, b) => keyNumbers[a.key] - keyNumbers[b.key])

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
    <div className="ps-card-heading"><span>04</span><div><h2 id="key-mapping-heading">按键动作映射</h2><p>为每个按键选择短按和长按时执行的动作。</p></div></div>
    {!hmi ? <p className="ps-summary-note">APL 按键与显示已裁剪，按键由宿主自行处理。若已启用自定义映射，请恢复标准映射后保存，或在固件裁剪中重新启用 APL 按键与显示。</p>
      : <>
          {!compiled ? <p className="ps-summary-note">自定义按键映射已裁剪，当前固件使用标准映射。已保存的配置暂不生效；若已启用自定义映射，请恢复标准映射后保存，或在固件裁剪中重新启用自定义按键映射。</p>
            : <><label className="ps-option"><input name="key_mapping.enabled" type="checkbox" checked={mapping.enabled} onChange={e => onChange(setKeyMapping(definition, { ...mapping, enabled: e.target.checked }))}/><span>启用自定义按键动作</span></label>
            <p className="ps-summary-note">{mapping.enabled ? '按下面的表执行动作。' : '当前使用标准映射；开启后可修改下面的动作。'}“无动作”不执行业务操作。组合键仍按原始输入组合识别。</p></>}
          <p className="ps-key-reference">按键统一使用 KEY1～KEY7 编号；功能以各按键下方的短按、长按动作为准。</p>
          <div className="ps-parameter-groups">{displaySlots.map(slot => {
            const available = !!build.defines[slot.macro], disabled = !mapping.enabled || !available, keyName = `KEY${keyNumbers[slot.key]}`
            return <details key={slot.key} className="ps-parameter-group" data-key-slot={slot.key} open={slot.key === 'Power'}>
              <summary><div className="ps-parameter-group-title"><strong>{keyName}</strong><p>{available ? `${standard ? '当前标准映射' : '当前自定义映射'} · 短按：${actionName(effectiveMapping.short[slot.id])}；长按：${actionName(effectiveMapping.long[slot.id])}` : '此输入槽位已从固件裁剪，保存的映射暂不执行。'}</p></div><ChevronDown size={17} aria-hidden="true"/></summary>
              {compiled && <div className="ps-fields-grid ps-parameter-group-fields">{(['short', 'long'] as const).map(gesture => {
                const selected = mapping[gesture][slot.id], active = keyActionAvailable(definition, selected)
                return <label className="ps-field" key={gesture}>{gesture === 'short' ? '短按动作' : '长按动作'}
                  <select name={`key_mapping.${gesture}.${slot.key}`} aria-label={`${keyName} ${gesture === 'short' ? '短按' : '长按'}动作`} value={selected} disabled={disabled} onChange={e => update(gesture, slot.id, Number(e.target.value))}>
                    {keyMappingActions.map(action => {
                      const enabled = keyActionAvailable(definition, action.id)
                      return <option key={action.id} value={action.id} disabled={!enabled && action.id !== selected} title={action.description}>{action.name}{enabled ? '' : '（当前未启用）'}</option>
                    })}
                  </select>
                  <small>{!active ? '此动作对应的功能未启用或已裁剪，当前不会执行；选择会保留。' : keyMappingActions.find(action => action.id === selected)?.description}</small>
                </label>
              })}</div>}
              {!compiled && <p className="ps-summary-note ps-parameter-group-fields">当前使用上面的标准动作；重新启用自定义按键映射后，可修改短按和长按动作。</p>}
            </details>
          })}</div>
          <p className="ps-summary-note">修改动作不会改变按键编号。保存配置后，可到“仿真体验”验证按键效果。</p>
        </>}
    <button className="ps-button" type="button" onClick={() => onChange(setKeyMapping(definition, defaultKeyMapping()))}>恢复标准映射</button>
  </section>
}
