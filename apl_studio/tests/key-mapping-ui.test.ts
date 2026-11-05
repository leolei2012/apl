import { test } from 'node:test'
import assert from 'node:assert/strict'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import type { Definition } from '@apl-studio/model'
import { firmwareBuild, setFirmwareBuild } from '@apl-studio/model/firmware'
import { defaultKeyMapping, setKeyMapping } from '@apl-studio/model/key-mapping'
import { ProfileKeyMapping } from '../apps/web/src/ProfileKeyMapping'

function product(): Definition {
  const values = { purify_enabled: 1, humidify_enabled: 1, modules: 7, sensors: 17, turbo_enabled: 1 }
  return { schemaVersion: 1, name: '按键映射测试', code: 'keymap_ui', category: '环境电器', description: '', sdkVersion: '0.1.0', template: { id: 'apl.runtime-profile', version: '0.3.0' }, functions: [],
    parameters: Object.entries(values).map(([key, value]) => ({ id: key, key, name: key, value })) }
}
const html = (definition: Definition) => renderToStaticMarkup(createElement(ProfileKeyMapping, { definition, onChange() {} }))
function select(markup: string, gesture: 'short' | 'long', key: string): { tag: string; options: string } {
  const match = markup.match(new RegExp(`(<select[^>]*name="key_mapping\\.${gesture}\\.${key}"[^>]*>)([\\s\\S]*?)<\\/select>`))
  assert.ok(match, `missing ${key} ${gesture} select`)
  return { tag: match[1], options: match[2] }
}

test('按键映射编辑器保留七个宿主输入槽位，旧配置默认使用标准映射', () => {
  const markup = html(product())
  assert.equal((markup.match(/data-key-slot=/g) ?? []).length, 7)
  assert.equal((markup.match(/<select /g) ?? []).length, 14)
  assert.doesNotMatch(markup, /name="key_mapping.enabled"[^>]*checked/)
  for (const key of ['Purify', 'Humidity', 'Timer', 'Light', 'Speed', 'Power', 'Filter']) {
    assert.match(select(markup, 'short', key).tag, /disabled=""/)
    assert.match(select(markup, 'long', key).tag, /disabled=""/)
  }
  assert.match(markup, /修改动作不会改变按键编号/)
  assert.match(markup, /组合键仍按原始输入组合识别/)
})

test('改配动作后只展示KEY编号与实际动作，仍保留原始输入绑定', () => {
  const mapping = defaultKeyMapping()
  mapping.enabled = true; mapping.short[3] = 1; mapping.short[5] = 3; mapping.long[4] = 9
  const markup = html(setKeyMapping(product(), mapping))
  assert.match(markup, /name="key_mapping.enabled"[^>]*checked=""/)
  assert.match(markup, /data-key-slot="Light"/)
  const summaries = [...markup.matchAll(/<summary>([\s\S]*?)<\/summary>/g)].map(match => match[1])
  assert.deepEqual(summaries.map(summary => summary.match(/<strong>(.*?)<\/strong>/)?.[1]), ['KEY1', 'KEY2', 'KEY3', 'KEY4', 'KEY5', 'KEY6', 'KEY7'])
  assert.doesNotMatch(summaries.join(''), /(?:Power|Speed|Timer|Purify|Humidity|Light|Filter) · \d/)
  assert.match(summaries[5], /短按：电源开关/)
  assert.match(select(markup, 'short', 'Light').tag, /aria-label="KEY6 短按动作"/)
  assert.match(select(markup, 'short', 'Light').options, /<option value="1"[^>]*selected=""/)
  assert.match(select(markup, 'short', 'Power').options, /<option value="3"[^>]*selected=""/)
  assert.match(select(markup, 'long', 'Speed').options, /<option value="9"[^>]*selected=""/)
  assert.doesNotMatch(select(markup, 'short', 'Light').tag, /disabled/)
})

test('裁掉单个输入仅禁用对应槽位；裁掉 HMI 或映射功能时说明实际行为', () => {
  const mapping = { ...defaultKeyMapping(), enabled: true }, definition = setKeyMapping(product(), mapping)
  const build = firmwareBuild(definition)
  const trimmedKey = setFirmwareBuild(definition, { ...build, defines: { ...build.defines, APL_HMI_COMPILE_KEY_LIGHT: 0 } })
  const markup = html(trimmedKey)
  assert.match(select(markup, 'short', 'Light').tag, /disabled=""/)
  assert.match(select(markup, 'long', 'Light').tag, /disabled=""/)
  assert.doesNotMatch(select(markup, 'short', 'Power').tag, /disabled/)
  assert.match(markup, /此输入槽位已从固件裁剪/)
  const noHmi = html(setFirmwareBuild(definition, { ...build, defines: { ...build.defines, APL_COMPILE_HMI: 0 } }))
  assert.match(noHmi, /按键由宿主自行处理/)
  assert.match(noHmi, /<button[^>]*>恢复标准映射<\/button>/)
  assert.doesNotMatch(noHmi, /<select /)
  const noMapping = html(setFirmwareBuild(definition, { ...build, defines: { ...build.defines, APL_HMI_COMPILE_KEYMAP: 0 } }))
  assert.match(noMapping, /当前固件使用标准映射/)
  assert.match(noMapping, /已保存的配置暂不生效/)
  assert.match(noMapping, /<button[^>]*>恢复标准映射<\/button>/)
  assert.doesNotMatch(noMapping, /<select /)
})

test('功能关闭后仍展示已经选择的动作，并解释它当前不执行', () => {
  const definition = product(), build = firmwareBuild(definition)
  const mapping = defaultKeyMapping(); mapping.enabled = true; mapping.short[3] = 6
  const trimmed = setFirmwareBuild(setKeyMapping(definition, mapping), { ...build, defines: { ...build.defines, APL_COMPILE_WIFI: 0 } })
  const markup = html(trimmed), chosen = select(markup, 'short', 'Light')
  assert.match(chosen.options, /<option value="6"[^>]*selected=""[^>]*>[^<]*（当前未启用）<\/option>/)
  assert.match(markup, /此动作对应的功能未启用或已裁剪，当前不会执行；选择会保留/)
  assert.match(chosen.options, /<option value="0"/)
})
