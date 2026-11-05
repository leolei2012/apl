import { test } from 'node:test'
import assert from 'node:assert/strict'
import net from 'node:net'
import { get } from 'node:http'
import { spawnSync } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import { mkdtempSync, rmSync, existsSync, mkdirSync, writeFileSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { basename, dirname, join, resolve } from 'node:path'
import { exampleDefinition } from '@apl-studio/model'
import { STUDIO_HOST, STUDIO_PORT, STUDIO_ROOT, startStudio } from '../apps/server/src/startup'
import { resolveServerSettings } from '../apps/server/src/server-settings'

const local = { host: STUDIO_HOST, publicUrl: '', username: '', password: '' }
function childEnvironment(port: string, dataDirectory: string): NodeJS.ProcessEnv {
  return { ...process.env, APL_STUDIO_HOST: STUDIO_HOST, APL_STUDIO_PORT: port,
    APL_STUDIO_PUBLIC_URL: '', APL_STUDIO_USERNAME: '', APL_STUDIO_PASSWORD: '', APL_STUDIO_DATA: dataDirectory }
}

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
async function httpRequest(url: URL, headers: Record<string, string>) {
  return new Promise<{ status: number; body: string }>((resolve, reject) => {
    const request = get(url, { headers }, response => {
      const chunks: Buffer[] = []
      response.on('data', chunk => chunks.push(chunk))
      response.on('end', () => resolve({ status: response.statusCode!, body: Buffer.concat(chunks).toString() }))
      response.on('error', reject)
    })
    request.on('error', reject)
  })
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
      await assert.rejects(startStudio({ ...local, port, dataDirectory }), /已被占用/)
    }
    assert.equal(existsSync(fresh), false)
    assert.deepEqual(readFileSync(database), sentinel)
  } finally { await release(busy); cleanup(dir) }
})

test('真实HTTP服务使用同一隔离目录重启后保留产品与修订', async () => {
  const dir = temporary(), dataDirectory = join(dir, 'data')
  let studio: Awaited<ReturnType<typeof startStudio>> | undefined
  try {
    studio = await startStudio({ ...local, port: 0, dataDirectory })
    const health = await fetch(studio.url + '/health')
    assert.equal(health.status, 200)
    const definition = exampleDefinition(randomUUID)
    const created = await fetch(studio.url + '/api/products', {
      method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(definition),
    })
    assert.equal(created.status, 201)
    const product = await created.json() as { id: string; revision: number; definition: unknown }
    await studio.close()
    studio = await startStudio({ ...local, port: 0, dataDirectory })
    const restored = await fetch(studio.url + '/api/products/' + product.id)
    assert.equal(restored.status, 200)
    assert.deepEqual(await restored.json(), product)
    assert.equal(existsSync(join(dataDirectory, 'studio.db')), true)
  } finally { await studio?.close(); cleanup(dir) }
})

test('命令行读取配置端口，冲突退出并保持新目录不存在，不改动其他监听服务', async () => {
  const dir = temporary(), dataDirectory = join(dir, 'untouched')
  const busy = await occupy()
  const port = (busy.address() as net.AddressInfo).port
  try {
    const child = spawnSync(process.execPath, ['--import', 'tsx', 'apps/server/src/main.ts'], {
      cwd: STUDIO_ROOT, encoding: 'utf8', timeout: 15000,
      env: childEnvironment(String(port), dataDirectory),
    })
    assert.equal(child.error, undefined)
    assert.equal(child.status, 1, child.stdout + child.stderr)
    assert.ok(child.stderr.includes(`端口 ${port} 已被占用`))
    assert.equal(existsSync(dataDirectory), false)
  } finally { await release(busy); cleanup(dir) }
})

test('网络配置缺省本机9190，显式选项优先环境，公网地址规范化为精确来源', () => {
  assert.deepEqual(resolveServerSettings({}, {}), { ...local, port: STUDIO_PORT })
  assert.deepEqual(resolveServerSettings({}, { APL_STUDIO_PORT: '9234' }), { ...local, port: 9234 })
  const deployed = resolveServerSettings({}, { APL_STUDIO_HOST: '0.0.0.0', APL_STUDIO_PORT: '8080',
    APL_STUDIO_PUBLIC_URL: 'https://studio.example.com/', APL_STUDIO_USERNAME: 'engineer', APL_STUDIO_PASSWORD: 'test-secret' })
  assert.deepEqual(deployed, { host: '0.0.0.0', port: 8080, publicUrl: 'https://studio.example.com', username: 'engineer', password: 'test-secret' })
  assert.deepEqual(resolveServerSettings({ ...local, port: 0 }, { APL_STUDIO_HOST: 'bad://host', APL_STUDIO_PORT: 'bad',
    APL_STUDIO_PUBLIC_URL: 'bad', APL_STUDIO_USERNAME: 'bad', APL_STUDIO_PASSWORD: 'bad' }), { ...local, port: 0 })
  assert.equal(resolveServerSettings({ ...local, host: '::1', port: 0 }, {}).host, '::1')
  assert.equal(resolveServerSettings({ ...local, host: 'LOCALHOST' }, {}).host, 'LOCALHOST')
  assert.equal(resolveServerSettings({ ...local, username: 'engineer', password: 'local-secret' }, {}).username, 'engineer')
})

test('非法部署设置在端口预检和数据库打开前失败，不将密码写入错误信息', async () => {
  for (const port of ['', '0', '-1', '1.5', '65536', 'NaN', '9190 ', '1e3'])
    assert.throws(() => resolveServerSettings({}, { APL_STUDIO_PORT: port }), /APL_STUDIO_PORT/)
  for (const host of ['', 'http://localhost', 'localhost:9190', '../host', 'bad host', 'host?', '[::1]', '-host', 'host_1'])
    assert.throws(() => resolveServerSettings({ ...local, host }, {}), /APL_STUDIO_HOST/)
  for (const publicUrl of ['example.com', 'ftp://example.com', 'https://example.com/apl/', 'https://user:secret@example.com',
    'https://example.com/?a=1', 'https://example.com/#fragment'])
    assert.throws(() => resolveServerSettings({ ...local, publicUrl, username: 'engineer', password: 'test-secret' }, {}), /APL_STUDIO_PUBLIC_URL/)
  for (const options of [{ username: 'only-user' }, { password: 'only-password' }, { publicUrl: 'https://studio.example.com' },
    { host: '0.0.0.0' }, { host: '192.0.2.1' }, { username: 'invalid:user', password: 'test-secret' },
    { username: 'engineer', password: 'test-secret\n' }])
    assert.throws(() => resolveServerSettings({ ...local, ...options }, {}))
  const dir = temporary(), dataDirectory = join(dir, 'never-opened'), password = 'secret-with-control\n'
  try {
    await assert.rejects(startStudio({ ...local, port: 0, username: 'engineer', password, dataDirectory }), error => {
      assert.ok(error instanceof Error)
      assert.ok(!error.message.includes(password))
      return true
    })
    assert.equal(existsSync(dataDirectory), false)
    const child = spawnSync(process.execPath, ['--import', 'tsx', 'apps/server/src/main.ts'], {
      cwd: STUDIO_ROOT, encoding: 'utf8', timeout: 15000,
      env: childEnvironment('invalid', dataDirectory),
    })
    assert.equal(child.error, undefined)
    assert.equal(child.status, 1, child.stdout + child.stderr)
    assert.match(child.stderr, /APL_STUDIO_PORT/)
    assert.equal(existsSync(dataDirectory), false)
  } finally { cleanup(dir) }
})

test('公网配置使用指定监听地址，HTTPS域名经同一服务访问且账号设置传入API', async () => {
  const dir = temporary()
  let studio: Awaited<ReturnType<typeof startStudio>> | undefined
  try {
    studio = await startStudio({ host: '0.0.0.0', port: 0, publicUrl: 'https://studio.example.com/',
      username: 'engineer', password: 'test-secret', dataDirectory: join(dir, 'data') })
    assert.equal(studio.host, '0.0.0.0')
    assert.ok(studio.port > 0)
    assert.equal(studio.publicUrl, 'https://studio.example.com')
    const endpoint = new URL(studio.url); endpoint.hostname = '127.0.0.1'
    // node:http preserves the original Host as an actual reverse proxy would.
    const denied = await httpRequest(new URL('/api/products', endpoint), { host: 'studio.example.com' })
    assert.equal(denied.status, 401)
    const allowed = await httpRequest(new URL('/api/products', endpoint), { host: 'studio.example.com', origin: studio.publicUrl,
      authorization: 'Basic ' + Buffer.from('engineer:test-secret').toString('base64') })
    assert.equal(allowed.status, 200, allowed.body)
  } finally { await studio?.close(); cleanup(dir) }
})
