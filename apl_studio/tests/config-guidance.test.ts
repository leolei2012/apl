import { test } from 'node:test'
import assert from 'node:assert/strict'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import type { Definition } from '@apl-studio/model'
import { defaultKeyMapping, setKeyMapping } from '@apl-studio/model/key-mapping'
import { ParameterGuide } from '../apps/web/src/ParameterGuide'
import { ProfileKeyMapping } from '../apps/web/src/ProfileKeyMapping'

const parameter = (fieldKey: string, values: Record<string, number>) =>
  renderToStaticMarkup(createElement(ParameterGuide, { fieldKey, values }))
function product(): Definition {
  return { schemaVersion: 1, name: '配置示意测试', code: 'guide_test', category: '环境电器', description: '', sdkVersion: '0.1.0',
    template: { id: 'apl.runtime-profile', version: '0.3.0' }, functions: [],
    parameters: Object.entries({ purify_enabled: 1, humidify_enabled: 1, modules: 7, sensors: 17, turbo_enabled: 1 })
      .map(([key, value]) => ({ id: key, key, name: key, value })) }
}
function keyMarkup(definition: Definition): string {
  return renderToStaticMarkup(createElement(ProfileKeyMapping, { definition, onChange() { throw new Error('Rendering must not modify configuration') } }))
}
function keySummary(markup: string): string {
  const match = markup.match(/<details[^>]*data-key-slot="Power"[^>]*>\s*<summary>([\s\S]*?)<\/summary>/)
  assert.ok(match, 'KEY1 action summary must remain available')
  return match[1]
}

test('恒湿示意明确示例目标并区分零回差的恢复边界', () => {
  assert.match(parameter('humidity_hysteresis', { humidity_hysteresis: 3 }), /降到 57% 及以下/)
  assert.match(parameter('humidity_hysteresis', { humidity_hysteresis: 3 }), /示例目标为 60%RH/)
  assert.match(parameter('humidity_hysteresis', { humidity_hysteresis: 0 }), /低于 60%/)
})

test('非法数值和零目标转速不播放虚假的运行示意', () => {
  for (const value of [NaN, -1, 65536]) {
    const markup = parameter('fan_gear1_rpm', { fan_gear1_rpm: value })
    assert.match(markup, /先填写允许范围内的整数/)
    assert.doesNotMatch(markup, /<button/)
    assert.doesNotMatch(markup, /NaN|Infinity/)
  }
  const stopped = parameter('fan_gear1_rpm', { fan_gear1_rpm: 0 })
  assert.match(stopped, /表示停转/)
  assert.doesNotMatch(stopped, /<button/)
})

test('缺水窗口说明区分一体机保留净化和纯加湿恢复窗口', () => {
  assert.match(parameter('shortage_run_s', { shortage_run_s: 300, purify_enabled: 1 }), /不会让一体机在倒计时后停止净化/)
  assert.match(parameter('shortage_run_s', { shortage_run_s: 300, purify_enabled: 0 }), /持续缺水达到 300 秒后/)
})

test('关闭自定义映射时，摘要展示标准动作且不再显示操作示意', () => {
  const mapping = defaultKeyMapping(); mapping.short[5] = 6
  const markup = keyMarkup(setKeyMapping(product(), mapping)), summary = keySummary(markup)
  assert.match(summary, /短按：电源开关/)
  assert.doesNotMatch(summary, /Wi-Fi 配网/)
  assert.match(summary, /当前标准映射/)
  assert.doesNotMatch(markup, /操作示意|播放演示|演示手势/)
})

test('启用后的摘要跟随当前选择，保留配置和底层输入绑定', () => {
  const mapping = defaultKeyMapping(); mapping.enabled = true; mapping.short[5] = 6
  const definition = setKeyMapping(product(), mapping), before = JSON.stringify(definition)
  const markup = keyMarkup(definition), summary = keySummary(markup)
  assert.match(summary, /短按：Wi-Fi 配网/)
  assert.match(summary, /当前自定义映射/)
  assert.match(markup, /name="key_mapping\.short\.Power"/)
  assert.doesNotMatch(markup, /操作示意|播放演示|演示手势/)
  assert.equal(JSON.stringify(definition), before)
})
