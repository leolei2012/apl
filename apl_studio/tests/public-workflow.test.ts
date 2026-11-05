import { test } from 'node:test'
import assert from 'node:assert/strict'
import { request } from 'node:http'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { unzipSync, strFromU8 } from 'fflate'
import { startStudio } from '../apps/server/src/startup'
import { newProfile, type ProfileCatalog } from '@apl-studio/model/profile'
import type { Product } from '@apl-studio/model'
import type { SimulationSession } from '@apl-studio/model/simulation'

test('公网同源认证贯穿新建配置、真实C仿真和Excel/源码导出', { timeout: 30000 }, async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-public-workflow-'))
  const authorization = 'Basic ' + Buffer.from('engineer:isolated-workflow-secret').toString('base64')
  const studio = await startStudio({ host: '127.0.0.1', port: 0, publicUrl: 'https://apl.example.com',
    username: 'engineer', password: 'isolated-workflow-secret', dataDirectory: directory })
  async function send(path: string, method = 'GET', body?: unknown, authenticated = true) {
    const bytes = body === undefined ? undefined : Buffer.from(JSON.stringify(body))
    return await new Promise<{ status: number; bytes: Buffer; json: () => any }>((resolve, reject) => {
      const outgoing = request(new URL(path, studio.url), { method, agent: false, headers: {
        host: 'apl.example.com', origin: 'https://apl.example.com', ...(authenticated ? { authorization } : {}),
        ...(bytes ? { 'content-type': 'application/json', 'content-length': bytes.length } : {}),
      } }, incoming => {
        const chunks: Buffer[] = []
        incoming.on('data', chunk => chunks.push(Buffer.from(chunk)))
        incoming.once('end', () => { const bytes = Buffer.concat(chunks); resolve({ status: incoming.statusCode!, bytes, json: () => JSON.parse(bytes.toString()) }) })
        incoming.once('error', reject)
      })
      outgoing.setTimeout(10000, () => outgoing.destroy(new Error('Public workflow request timed out')))
      outgoing.once('error', reject); outgoing.end(bytes)
    })
  }
  try {
    const catalog = await send('/api/profile/catalog'); assert.equal(catalog.status, 200)
    const definition = newProfile(catalog.json() as ProfileCatalog, '公网产品闭环', { purify: true, humidify: false })
    assert.equal((await send('/api/products', 'POST', definition, false)).status, 401)
    assert.deepEqual((await send('/api/products')).json(), [], 'an unauthorized write must have no effect')
    const created = await send('/api/products', 'POST', definition); assert.equal(created.status, 201)
    const product = created.json() as Product
    const started = await send('/api/simulation/sessions', 'POST', { productId: product.id, revision: 1, running: false })
    assert.equal(started.status, 202)
    let session = started.json() as SimulationSession
    const deadline = Date.now() + 15000
    while (session.status === 'building' && Date.now() < deadline) {
      await new Promise(resolve => setTimeout(resolve, 30))
      session = (await send('/api/simulation/sessions/' + session.id)).json() as SimulationSession
    }
    assert.equal(session.status, 'ready', session.diagnostics)
    assert.equal((await send(`/api/simulation/sessions/${session.id}/actions`, 'POST', { type: 'advance', ms: 2000 })).status, 200)
    const action = await send(`/api/simulation/sessions/${session.id}/actions`, 'POST', { type: 'input', key: 'Power', value: 'short' })
    assert.equal(action.status, 200)
    session = action.json() as SimulationSession
    assert.equal(session.snapshot!.internal.run_state, 1)
    assert.equal(session.snapshot!.outputs.fan, 1)
    const workbook = await send(`/api/products/${product.id}/parameters.xlsx?revision=1`)
    assert.equal(workbook.status, 200); assert.ok(unzipSync(workbook.bytes)['xl/worksheets/sheet4.xml'])
    const firmware = await send(`/api/products/${product.id}/firmware.zip?revision=1`)
    assert.equal(firmware.status, 200)
    const files = unzipSync(firmware.bytes)
    assert.equal(JSON.parse(strFromU8(files['apl-firmware/manifest.json'])).revision, 1)
    assert.equal((await send(`/api/products/${product.id}/firmware.zip?revision=1`, 'GET', undefined, false)).status, 401)
    assert.equal((await send('/api/simulation/sessions/' + session.id, 'DELETE')).status, 204)
  } finally {
    await studio.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir())); assert.ok(basename(directory).startsWith('apl-public-workflow-'))
    rmSync(directory, { recursive: true, force: true })
  }
})
