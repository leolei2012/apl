import { test } from 'node:test'
import assert from 'node:assert/strict'
import net from 'node:net'
import { spawnSync } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import { mkdtempSync, rmSync, existsSync, mkdirSync, writeFileSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { basename, dirname, join, resolve } from 'node:path'
import { exampleDefinition } from '@apl-studio/model'
import { STUDIO_HOST, STUDIO_PORT, STUDIO_ROOT, startStudio } from '../apps/server/src/startup'

function temporary() { return mkdtempSync(join(tmpdir(), 'apl-startup-test-')) }
function cleanup(dir: string) {
  assert.equal(resolve(dirname(dir)), resolve(tmpdir()))
  assert.ok(basename(dir).startsWith('apl-startup-test-'))
  rmSync(dir, { recursive: true, force: true })
}
async function occupy(port = 0) {
  const server = net.createServer()
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject)
    server.listen(port, STUDIO_HOST, resolve)
  })
  return server
}
async function release(server: net.Server) {
  await new Promise<void>((resolve, reject) => server.close(error => error ? reject(error) : resolve()))
}

test('启动端口冲突先于数据库打开，不创建新数据，也不触碰已有库', async () => {
  const dir = temporary(), busy = await occupy()
  const port = (busy.address() as net.AddressInfo).port
  const fresh = join(dir, 'fresh'), existing = join(dir, 'existing')
  mkdirSync(existing)
  const database = join(existing, 'studio.db'), sentinel = Buffer.from('existing database must remain unopened')
  writeFileSync(database, sentinel)
  try {
    for (const dataDirectory of [fresh, existing]) {
      await assert.rejects(startStudio({ port, dataDirectory }), /已被占用/)
    }
    assert.equal(existsSync(fresh), false)
    assert.deepEqual(readFileSync(database), sentinel)
  } finally { await release(busy); cleanup(dir) }
})

test('真实HTTP服务使用同一隔离目录重启后保留产品与修订', async () => {
  const dir = temporary(), dataDirectory = join(dir, 'data')
  let studio: Awaited<ReturnType<typeof startStudio>> | undefined
  try {
    studio = await startStudio({ port: 0, dataDirectory })
    const health = await fetch(studio.url + '/health')
    assert.equal(health.status, 200)
    const definition = exampleDefinition(randomUUID)
    const created = await fetch(studio.url + '/api/products', {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(definition),
    })
    assert.equal(created.status, 201)
    const product = await created.json() as { id: string; revision: number; definition: unknown }
    await studio.close()
    studio = await startStudio({ port: 0, dataDirectory })
    const restored = await fetch(studio.url + '/api/products/' + product.id)
    assert.equal(restored.status, 200)
    assert.deepEqual(await restored.json(), product)
    assert.equal(existsSync(join(dataDirectory, 'studio.db')), true)
  } finally { await studio?.close(); cleanup(dir) }
})

test('命令行固定9190且忽略旧端口环境变量，冲突退出并保持新目录不存在', async () => {
  const dir = temporary(), dataDirectory = join(dir, 'untouched')
  let busy: net.Server | undefined
  try {
    try { busy = await occupy(STUDIO_PORT) } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== 'EADDRINUSE') throw error
      // An existing listener is left running; the child must still fail the preflight.
    }
    const child = spawnSync(process.execPath, ['--import', 'tsx', 'apps/server/src/main.ts'], {
      cwd: STUDIO_ROOT, encoding: 'utf8', timeout: 15000,
      env: { ...process.env, APL_STUDIO_PORT: 'invalid-legacy-value', APL_STUDIO_DATA: dataDirectory },
    })
    assert.equal(child.error, undefined)
    assert.equal(child.status, 1, child.stdout + child.stderr)
    assert.match(child.stderr, /端口 9190 已被占用/)
    assert.equal(existsSync(dataDirectory), false)
  } finally { if (busy) await release(busy); cleanup(dir) }
})
