import { test } from 'node:test'
import assert from 'node:assert/strict'
import { webcrypto } from 'node:crypto'
import { resolve } from 'node:path'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { newId, exampleDefinition, interfaceHeader, emptyDefinition, validateDefinition, type IdCrypto } from '@apl-studio/model'
import { newProfile, validateProfile } from '@apl-studio/model/profile'
import { RuntimeProfileProvider } from '@apl-studio/templates'
import { FunctionEditor, ParameterEditor } from '../apps/web/src/editors'

const uuidV4 = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/

test('标识优先使用原生randomUUID，并保留WebCrypto方法的this绑定', () => {
  let calls = 0
  const source: IdCrypto = {
    randomUUID() { assert.equal(this, source); calls++; return '12345678-1234-4321-8123-123456789abc' },
    getRandomValues() { assert.fail('native randomUUID is available') },
  }
  assert.equal(newId(source), '12345678-1234-4321-8123-123456789abc')
  assert.equal(calls, 1)
})

test('HTTP可用的安全随机数后备生成标准UUIDv4，固定版本位和variant并保留其余随机位', () => {
  let calls = 0
  const source: IdCrypto = { getRandomValues(bytes) {
    assert.equal(this, source)
    assert.equal(bytes.length, 16)
    calls++
    bytes.fill(255)
  } }
  assert.equal(newId(source), 'ffffffff-ffff-4fff-bfff-ffffffffffff')
  assert.equal(calls, 1)
  assert.equal(newId({ getRandomValues(bytes) { bytes.fill(0) } }), '00000000-0000-4000-8000-000000000000')
  const random = { getRandomValues: webcrypto.getRandomValues.bind(webcrypto) }
  const ids = Array.from({ length: 32 }, () => newId(random))
  for (const id of ids) assert.match(id, uuidV4)
  assert.equal(new Set(ids).size, ids.length)
})

test('非安全上下文缺少randomUUID时仍可新建产品、生成接口头并打开功能与参数编辑器', { concurrency: false }, () => {
  const provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  const descriptor = Object.getOwnPropertyDescriptor(globalThis, 'crypto')
  try {
    Object.defineProperty(globalThis, 'crypto', { configurable: true, value: {
      getRandomValues: webcrypto.getRandomValues.bind(webcrypto),
    } })
    assert.equal(globalThis.crypto.randomUUID, undefined)
    const first = newProfile(provider.catalog, 'HTTP公网产品'), second = newProfile(provider.catalog, '另一产品')
    assert.deepEqual(validateProfile(first, provider.catalog), [])
    assert.match(first.code, /^device_[0-9a-f]{12}$/)
    assert.notEqual(first.code, second.code)
    const definition = exampleDefinition(newId)
    assert.deepEqual(validateDefinition(definition), [])
    for (const field of [...definition.functions, ...definition.parameters]) assert.match(field.id, uuidV4)
    const header = interfaceHeader(definition)
    assert.match(header, /#ifndef APL_AIR_CARE_CONTRACT_H/)
    assert.match(header, /bool apl_air_care_get_power\(void\);/)
    const shared = { definition: emptyDefinition(), original: null, onClose() {}, onSave() {} }
    assert.match(renderToStaticMarkup(createElement(FunctionEditor, shared)), /添加功能/)
    assert.match(renderToStaticMarkup(createElement(ParameterEditor, shared)), /保存参数/)

    Object.defineProperty(globalThis, 'crypto', { configurable: true, value: undefined })
    assert.throws(() => newId(), /安全随机数.*无法生成标识/)
  } finally {
    if (descriptor) Object.defineProperty(globalThis, 'crypto', descriptor)
    else Reflect.deleteProperty(globalThis, 'crypto')
  }
})
