import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { createHash, randomUUID } from 'node:crypto'
import { zipSync } from 'fflate'
import type { BuildJob } from '@apl-studio/model/template'
import { SdkBuildService } from '@apl-studio/build'
import { TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { createApi } from '@apl-studio/api'

const digest = (bytes: Uint8Array | string) => createHash('sha256').update(bytes).digest('hex')
function fixture(checks: BuildJob['checks'] = { compile: 'passed', consumer: 'passed' }) {
  const directory = mkdtempSync(join(tmpdir(), 'apl-artifact-test-'))
  const content = Buffer.from('int product_value(void) { return 7; }\n')
  const header = Buffer.from('int product_value(void);\n')
  const hashes = { 'api/product.h': digest(header), 'src/product.c': digest(content) }
  const job: BuildJob = {
    id: randomUUID(), productId: randomUUID(), revision: 2, productName: '归档回归', productCode: 'archive_test', sdkVersion: '0.1.0',
    template: { id: 'test.archive', version: '1.0.0', fingerprint: digest('template') },
    createdAt: new Date().toISOString(), updatedAt: new Date().toISOString(), sourceVersion: '1.0.0',
    status: 'succeeded', phase: '编译与消费验证通过', diagnostics: '', compilerVersion: 'fixture',
    fingerprint: digest(JSON.stringify(hashes)), filename: 'archive_test-sdk-0.1.0-r2.zip',
    checks,
  }
  const manifest = {
    format: 'apl-sdk', schemaVersion: 1, sdkReady: true, fingerprint: job.fingerprint, files: hashes,
    product: { id: job.productId, revision: job.revision, code: job.productCode, version: job.sdkVersion },
    template: job.template, sourceVersion: job.sourceVersion, verification: job.checks,
  }
  const files = { 'apl-sdk/src/product.c': content, 'apl-sdk/api/product.h': header, 'apl-sdk/manifest.json': Buffer.from(JSON.stringify(manifest)) }
  const jobs = join(directory, 'jobs'), root = join(jobs, job.id), archive = join(root, 'apl-sdk.zip')
  mkdirSync(root, { recursive: true })
  writeFileSync(join(root, 'job.json'), JSON.stringify(job))
  const bytes = zipSync(files)
  writeFileSync(archive, bytes)
  const registry = new TemplateRegistry(), service = new SdkBuildService(jobs, registry)
  const store = new ProductStore(join(directory, 'studio.db')), app = createApi(store, registry, undefined, service)
  return { job, manifest, files, bytes, archive, service, app, async close() {
    await service.close(); await app.close(); store.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir()))
    assert.ok(basename(directory).startsWith('apl-artifact-test-'))
    rmSync(directory, { recursive: true, force: true })
  } }
}

test('历史 SDK 无需新增元数据即可校验和下载，响应字节与归档一致', async () => {
  const f = fixture()
  try {
    const response = await f.app.inject('/api/sdk/jobs/' + f.job.id + '/download')
    assert.equal(response.statusCode, 200, response.body)
    assert.equal(response.headers['content-type'], 'application/zip')
    assert.deepEqual(response.rawPayload, Buffer.from(f.bytes))
  } finally { await f.close() }
})

test('成功任务的 ZIP 在服务启动后丢失，下载返回可操作的 409 而不是 500', async () => {
  const f = fixture()
  try {
    rmSync(f.archive)
    const response = await f.app.inject('/api/sdk/jobs/' + f.job.id + '/download')
    assert.equal(response.statusCode, 409, response.body)
    assert.equal(response.json().code, 'SDK_ARTIFACT_MISSING')
    assert.match(response.json().error, /重新生成/)
  } finally { await f.close() }
})

test('损坏 ZIP、源码增删改均拒绝下载，即使重新计算了包内 manifest', async () => {
  const f = fixture()
  try {
    const forgedContent = Buffer.from('int product_value(void) { return 99; }\n')
    const forgedHashes = { ...f.manifest.files, 'src/product.c': digest(forgedContent) }
    const forgedManifest = { ...f.manifest, files: forgedHashes, fingerprint: digest(JSON.stringify(forgedHashes)) }
    const variants = [
      Buffer.from('broken archive'),
      zipSync({ ...f.files, 'apl-sdk/src/product.c': forgedContent }),
      zipSync({ ...f.files, 'apl-sdk/extra.c': Buffer.from('unexpected') }),
      zipSync({ 'apl-sdk/manifest.json': f.files['apl-sdk/manifest.json'] }),
      zipSync({ 'apl-sdk/src/product.c': f.files['apl-sdk/src/product.c'] }),
      zipSync({ ...f.files, 'apl-sdk/src/product.c': forgedContent, 'apl-sdk/manifest.json': Buffer.from(JSON.stringify(forgedManifest)) }),
    ]
    for (const bytes of variants) {
      writeFileSync(f.archive, bytes)
      const response = await f.app.inject('/api/sdk/jobs/' + f.job.id + '/download')
      assert.equal(response.statusCode, 409)
      assert.equal(response.json().code, 'SDK_ARTIFACT_INVALID')
    }
    writeFileSync(f.archive, f.bytes)
    assert.equal((await f.app.inject('/api/sdk/jobs/' + f.job.id + '/download')).statusCode, 200)
  } finally { await f.close() }
})

test('只修改 manifest 的修订、模板、验证结果或文件哈希也不能作为已验证 SDK 下载', async () => {
  const f = fixture()
  try {
    const variants = [
      { ...f.manifest, product: { ...f.manifest.product, revision: 3 } },
      { ...f.manifest, product: { ...f.manifest.product, id: randomUUID() } },
      { ...f.manifest, template: { ...f.manifest.template, version: '2.0.0' } },
      { ...f.manifest, sourceVersion: '2.0.0' },
      { ...f.manifest, verification: { compile: 'passed', consumer: 'failed' } },
      { ...f.manifest, sdkReady: false },
      { ...f.manifest, files: { 'src/product.c': digest('changed') } },
    ]
    for (const manifest of variants) {
      writeFileSync(f.archive, zipSync({ ...f.files, 'apl-sdk/manifest.json': Buffer.from(JSON.stringify(manifest)) }))
      const response = await f.app.inject('/api/sdk/jobs/' + f.job.id + '/download')
      assert.equal(response.statusCode, 409)
      assert.equal(response.json().code, 'SDK_ARTIFACT_INVALID')
    }
  } finally { await f.close() }
})

test('恢复记录只有成功状态而没有两项通过结果时仍禁止下载', async () => {
  for (const checks of [{ compile: 'passed', consumer: 'failed' }, null]) {
    const f = fixture(checks as BuildJob['checks'])
    try {
      const response = await f.app.inject('/api/sdk/jobs/' + f.job.id + '/download')
      assert.equal(response.statusCode, 409)
      assert.equal(response.json().code, 'SDK_NOT_VERIFIED')
    } finally { await f.close() }
  }
})
