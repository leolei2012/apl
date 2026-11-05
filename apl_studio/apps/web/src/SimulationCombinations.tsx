import type { SimulationAction, SimulationCombination } from '@apl-studio/model/simulation'

export function SimulationCombinations({ combinations, disabled, onAction }: {
  combinations?: SimulationCombination[]; disabled: boolean; onAction: (action: SimulationAction) => void
}) {
  if (!combinations?.length) return null
  return <section className="card"><div className="section-heading"><div><h2>组合键</h2><p>同时按下、保持并释放；是否生效由 C 业务判断。</p></div></div>
    <div className="simulation-actions">{combinations.map(combo => <button className="button" key={combo.key} disabled={disabled}
      aria-label={`${combo.name}：${combo.keys.join(' + ')} 保持 ${combo.holdMs / 1000} 秒`}
      onClick={() => onAction({ type: 'combo', keys: [...combo.keys], holdMs: combo.holdMs })}>
      {combo.name} · {combo.keys.join(' + ')}（{combo.holdMs / 1000} 秒）
    </button>)}</div>
    <p className="simulation-hint">一次点击完成整组手势并推进虚拟时间，无需持续按住鼠标。执行期间按钮暂不可用；离开页面会停止会话。</p>
  </section>
}
