import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, sep } from 'node:path'
import { unzipSync, strFromU8 } from 'fflate'
import { createHash } from 'node:crypto'
import { RuntimeProfileProvider, TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'
import { newProfile } from '@apl-studio/model/profile'
import { BUILD_PARAMETER, defaultFirmwareBuild, firmwareBuild, setFirmwareBuild } from '@apl-studio/model/firmware'
import { defaultMemorySettings, estimateMemory } from '@apl-studio/model/memory'

test('内存设置随修订保存，JSON与ZIP报告一致，预算超限不伪造验收结果', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-memory-report-'))
  const products = new ProductStore(join(directory, 'studio.db')), provider = new RuntimeProfileProvider(resolve(import.meta.dirname, '../..'))
  const registry = new TemplateRegistry(); registry.register(provider)
  const app = createApi(products, registry)
  try {
    const d = newProfile(provider.catalog, '1KB预算'), oldBuild = defaultFirmwareBuild()
    // Existing saved builds without the optional extension remain valid.
    const legacy = await app.inject({ method: 'POST', url: '/api/products', payload: setFirmwareBuild(d, oldBuild) })
    assert.equal(legacy.statusCode, 201, legacy.body)
    const product = legacy.json()
    let response = await app.inject({ method: 'GET', url: `/api/products/${product.id}/memory-estimate.json?revision=1` })
    assert.equal(response.statusCode, 200)
    assert.equal(response.json().estimate.totalBytes, null)
    assert.equal(response.json().model, 'manual-target-sizeof')
    assert.equal(response.json().settings.hostStaticBytes, null)
    const build = { ...oldBuild, target: 'c51' as const, memory: { ...defaultMemorySettings(), hostStaticBytes: 479, safetyBytes: 0, flashUsedBytes: 32833 } }
    const definition = setFirmwareBuild(d, build)
    const update = await app.inject({ method: 'PUT', url: '/api/products/' + product.id, payload: { revision: 1, definition } })
    assert.equal(update.statusCode, 200, update.body)
    assert.deepEqual(firmwareBuild(update.json().definition).memory, build.memory)
    response = await app.inject({ method: 'GET', url: `/api/products/${product.id}/memory-estimate.json?revision=2` })
    assert.equal(response.statusCode, 200, response.body)
    assert.match(response.headers['content-disposition']!, /memory-r2\.json/)
    const report = response.json()
    assert.equal(report.revision, 2); assert.equal(report.sourceFingerprint, provider.catalog.source.fingerprint)
    assert.equal(report.estimate.totalBytes, 1251); assert.equal(report.estimate.remainingBytes, -227)
    assert.equal(report.estimate.status, 'over'); assert.equal(report.estimate.flash.remainingBytes, -65)
    assert.equal(report.targetCapacityVerified, false); assert.equal(report.targetCompiled, false); assert.equal(report.stackVerified, false)
    const archive = await app.inject({ method: 'GET', url: `/api/products/${product.id}/firmware.zip?revision=2` })
    assert.equal(archive.statusCode, 200)
    const files = unzipSync(archive.rawPayload), record = files['apl-firmware/memory-estimate.json']
    assert.deepEqual(JSON.parse(strFromU8(record)), report)
    const manifest = JSON.parse(strFromU8(files['apl-firmware/manifest.json']))
    assert.equal(manifest.files['memory-estimate.json'], createHash('sha256').update(record).digest('hex'))
    assert.equal((await app.inject({ method: 'GET', url: `/api/products/${product.id}/memory-estimate.json?revision=1` })).statusCode, 409)
    const bad = structuredClone(definition)
    bad.parameters.find(p => p.key === BUILD_PARAMETER)!.value = JSON.stringify({ ...build, memory: { ...build.memory, instanceCount: -1 } })
    assert.equal((await app.inject({ method: 'PUT', url: '/api/products/' + product.id, payload: { revision: 2, definition: bad } })).statusCode, 400)
    const stale = structuredClone(definition)
    stale.parameters.find(p => p.key === 'source_fingerprint')!.value = 'stale-source'
    assert.equal((await app.inject({ method: 'PUT', url: '/api/products/' + product.id, payload: { revision: 2, definition: stale } })).statusCode, 400)
    // Read again from the store: report inputs are saved data, not browser-only state.
    assert.deepEqual(estimateMemory(firmwareBuild(products.get(product.id).definition)), report.estimate)
  } finally {
    await app.close(); products.close()
    assert.ok(resolve(directory).startsWith(resolve(tmpdir()) + sep)); rmSync(directory, { recursive: true, force: true })
  }
})
