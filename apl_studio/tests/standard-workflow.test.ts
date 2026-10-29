import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, rmSync, readFileSync } from 'node:fs'
import { join, resolve, dirname } from 'node:path'
import { tmpdir } from 'node:os'
import { createHash } from 'node:crypto'
import { unzipSync, strFromU8 } from 'fflate'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { applyTemplate, definitionFromTemplate } from '@apl-studio/model/template'
import { TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { AcceptanceService, SdkBuildService, SimulationService } from '@apl-studio/build'
import { createApi } from '@apl-studio/api'
import { StandardOptions } from '../apps/web/src/StandardOptions'

test('三能力标准方案：配置、真实C验收、门禁、SDK消费与中文下载', async () => {
  const directory = mkdtempSync(join(tmpdir(), 'apl-standard-test-')), registry = new TemplateRegistry()
  registry.loadDirectory(resolve(import.meta.dirname, '../../apl_sdk_template'))
  const store = new ProductStore(join(directory, 'studio.db'))
  const simulations = new SimulationService(join(directory, 'simulation'), registry)
  const acceptances = new AcceptanceService(join(directory, 'acceptance'), registry, simulations)
  const builds = new SdkBuildService(join(directory, 'sdk'), registry, undefined, undefined, acceptances)
  const app = createApi(store, registry, undefined, builds, simulations, acceptances)
  try {
    const template = registry.get('apl.air-care', '0.3.0')!.descriptor
    assert.equal(template.sourceVersion, '0.1.3')
    for (const profile of ['purify', 'humidify', 'combined']) {
      const keys = new Set(['power', 'fan_speed', ...(profile !== 'humidify' ? ['purify', 'pm25'] : []), ...(profile !== 'purify' ? ['humidify', 'humidity', 'drying'] : [])])
      const definition = applyTemplate(definitionFromTemplate(template), template, keys)
      definition.name = `标准方案 ${profile}`; definition.code = 'standard_' + profile
      const p = store.create(definition)
      const html = renderToStaticMarkup(createElement(StandardOptions, { draft: definition, template, setDraft() {} }))
      assert.match(html, /产品类型/); assert.match(html, /净化加湿一体机/)
      assert.equal((await app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: p.revision } })).statusCode, 409)
      const run = await app.inject({ method: 'POST', url: '/api/acceptance/run', payload: { productId: p.id, revision: p.revision } })
      assert.equal(run.statusCode, 202, run.body)
      let latest = acceptances.latest(p), deadline = Date.now() + 70000
      while (latest.report?.status === 'running' && Date.now() < deadline) {
        await new Promise(done => setTimeout(done, 30)); latest = acceptances.latest(p)
      }
      assert.equal(latest.report?.status, 'passed', profile + ': ' + latest.report?.diagnostics)
      assert.equal(latest.current, true); assert.ok(latest.report!.checks.length >= 10)
      assert.ok(latest.report!.checks.every(c => c.passed))
      const response = await app.inject({ method: 'POST', url: '/api/sdk/generate', payload: { productId: p.id, revision: p.revision } })
      assert.equal(response.statusCode, 202, response.body)
      const id = response.json().id; let job = builds.get(id); deadline = Date.now() + 40000
      while (['queued', 'building'].includes(job.status) && Date.now() < deadline) { await new Promise(done => setTimeout(done, 30)); job = builds.get(id) }
      assert.equal(job.status, 'succeeded', job.diagnostics)
      assert.equal(job.checks?.consumer, 'passed')
      const archive = unzipSync(builds.artifact(id).bytes)
      const manifest = JSON.parse(strFromU8(archive['apl-sdk/manifest.json']))
      assert.equal(manifest.verification.acceptance.fingerprint, latest.report!.fingerprint)
      for (const [path, hash] of Object.entries(manifest.files)) assert.equal(createHash('sha256').update(archive['apl-sdk/' + path]).digest('hex'), hash)
      assert.match(strFromU8(archive['apl-sdk/docs/product-spec.md']), /湿差恰好19/)
      assert.ok(archive['apl-sdk/include/apl_platform.h'])
      assert.equal((await app.inject({ method: 'GET', url: `/api/sdk/jobs/${id}/download` })).statusCode, 200)
    }
    const snapshot = JSON.parse(readFileSync(resolve(import.meta.dirname, '../../apl_sdk_template/air-care/0.3.0/source-snapshot.json'), 'utf8'))
    for (const [path, hash] of Object.entries(snapshot.rootFiles)) assert.equal(createHash('sha256').update(readFileSync(resolve(import.meta.dirname, '../../apl_sdk_template/air-care/0.3.0', path))).digest('hex'), hash)
  } finally {
    await app.close(); await simulations.close(); await acceptances.close(); await builds.close(); store.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir())); rmSync(directory, { recursive: true, force: true })
  }
})
