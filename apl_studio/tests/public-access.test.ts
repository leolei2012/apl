import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, writeFileSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, resolve, dirname, basename } from 'node:path'
import { randomUUID } from 'node:crypto'
import { createRequire } from 'node:module'
import { EventEmitter } from 'node:events'
import type { IncomingMessage } from 'node:http'
import { request as httpRequest } from 'node:http'
import { ProductStore } from '@apl-studio/product'
import { TemplateRegistry } from '@apl-studio/templates'
import { createApi } from '@apl-studio/api'
import { exampleDefinition } from '@apl-studio/model'

const publicUrl = 'https://studio.example.com'
const username = 'studio-user', password = 'test-private-password'
const access = { publicUrl, username, password }
const authorization = 'Basic ' + Buffer.from(username + ':' + password).toString('base64')
const publicHeaders = { host: 'studio.example.com', origin: publicUrl, authorization }

function fixture(options?: Parameters<typeof createApi>[6]) {
  const directory = mkdtempSync(join(tmpdir(), 'apl-public-access-'))
  const web = join(directory, 'web')
  mkdirSync(web)
  writeFileSync(join(web, 'index.html'), '<!doctype html><title>public studio fixture</title>')
  writeFileSync(join(web, 'fixture.txt'), 'public asset fixture')
  const store = new ProductStore(join(directory, 'studio.db'))
  const app = createApi(store, new TemplateRegistry(), web, undefined, undefined, undefined, options)
  return { app, store, async close() {
    await app.close(); store.close()
    assert.equal(resolve(dirname(directory)), resolve(tmpdir()))
    assert.ok(basename(directory).startsWith('apl-public-access-'))
    rmSync(directory, { recursive: true, force: true })
  } }
}

test('本地默认仍拒绝外部 Host 和 Origin，开发页面来源保持可用', async () => {
  const f = fixture()
  try {
    assert.equal((await f.app.inject({ url: '/api/products' })).statusCode, 200)
    assert.equal((await f.app.inject({ url: '/api/products', headers: { host: 'other.invalid' } })).statusCode, 403)
    assert.equal((await f.app.inject({ url: '/api/products', headers: { origin: 'https://other.invalid' } })).statusCode, 403)
    assert.equal((await f.app.inject({ url: '/api/products', headers: { origin: 'http://localhost:5174' } })).statusCode, 200)
  } finally { await f.close() }
})

test('公网 HTTPS 同源及授权 CLI 可访问；伪造转发头不能放宽来源或 Host', async () => {
  const f = fixture(access)
  try {
    assert.equal((await f.app.inject({ url: '/api/products', headers: publicHeaders })).statusCode, 200)
    assert.equal((await f.app.inject({ url: '/api/products', headers: { host: 'studio.example.com', authorization } })).statusCode, 200)
    for (const headers of [
      { ...publicHeaders, origin: 'http://studio.example.com' },
      { ...publicHeaders, origin: 'https://other.invalid' },
      { ...publicHeaders, origin: 'https://studio.example.com.evil.invalid' },
      { ...publicHeaders, origin: 'null' },
      { ...publicHeaders, host: 'other.invalid' },
      { ...publicHeaders, host: 'other.invalid', 'x-forwarded-host': 'studio.example.com', 'x-forwarded-proto': 'https' },
      { ...publicHeaders, origin: 'https://other.invalid', 'x-forwarded-host': 'other.invalid', 'x-forwarded-proto': 'https' },
    ]) assert.equal((await f.app.inject({ url: '/api/products', headers })).statusCode, 403, JSON.stringify(headers))
    assert.equal((await f.app.inject({ url: '/api/products', headers: { host: 'localhost', origin: 'http://localhost', authorization } })).statusCode, 200)
    assert.equal((await f.app.inject({ url: '/api/products', headers: { host: 'localhost', origin: 'http://localhost:5174', authorization } })).statusCode, 403)
  } finally { await f.close() }
})

test('身份认证覆盖页面、资源、API、下载及握手入口，健康检查保留来源限制', async () => {
  const f = fixture(access)
  try {
    const product = f.store.create(exampleDefinition(randomUUID))
    for (const url of ['/', '/fixture.txt', '/engineering', '/api/products', `/api/products/${product.id}/definition`, `/api/products/${product.id}/export`, '/ws']) {
      for (const supplied of [undefined, 'Basic ' + Buffer.from(username + ':wrong').toString('base64'), 'Bearer invalid', 'Basic !!!', 'Basic A', 'Basic AA==', 'Basic ' + Buffer.from(username + '\0:' + password).toString('base64')]) {
        const headers: Record<string, string> = { host: 'studio.example.com', origin: publicUrl }
        if (supplied) headers.authorization = supplied
        const response = await f.app.inject({ url, headers })
        assert.equal(response.statusCode, 401, url)
        assert.match(String(response.headers['www-authenticate']), /^Basic\s/i)
      }
    }
    assert.match((await f.app.inject({ url: '/', headers: publicHeaders })).body, /public studio fixture/)
    assert.equal((await f.app.inject({ url: '/fixture.txt', headers: publicHeaders })).body, 'public asset fixture')
    assert.match((await f.app.inject({ url: '/engineering', headers: publicHeaders })).body, /public studio fixture/)
    assert.equal((await f.app.inject({ url: '/api/missing', headers: publicHeaders })).statusCode, 404)
    const definition = await f.app.inject({ url: `/api/products/${product.id}/definition`, headers: publicHeaders })
    assert.equal(definition.statusCode, 200); assert.match(String(definition.headers['content-disposition']), /attachment/)
    const exported = await f.app.inject({ url: `/api/products/${product.id}/export`, headers: publicHeaders })
    assert.equal(exported.statusCode, 200); assert.match(String(exported.headers['content-type']), /application\/zip/)
    assert.equal((await f.app.inject({ url: '/health', headers: { host: 'studio.example.com' } })).statusCode, 200)
    assert.equal((await f.app.inject({ url: '/health', headers: { host: 'other.invalid' } })).statusCode, 403)
    assert.equal((await f.app.inject({ url: '/health', headers: { host: 'studio.example.com', origin: 'https://other.invalid' } })).statusCode, 403)
  } finally { await f.close() }
})

test('本地也可配置服务级密码，公网 URL 或不完整凭据在建立服务前拒绝', async () => {
  const f = fixture({ username, password })
  try {
    assert.equal((await f.app.inject({ url: '/api/products' })).statusCode, 401)
    assert.equal((await f.app.inject({ url: '/api/products', headers: { authorization } })).statusCode, 200)
    for (const options of [
      { publicUrl }, { username }, { password },
      { ...access, publicUrl: 'ftp://studio.example.com' },
      { ...access, publicUrl: 'https://studio.example.com/apl/' },
      { ...access, publicUrl: 'https://user:password@studio.example.com' },
      { ...access, publicUrl: 'https://studio.example.com?workspace=1' },
      { ...access, publicUrl: 'https://studio.example.com#workspace' },
    ]) assert.throws(() => createApi(f.store, new TemplateRegistry(), undefined, undefined, undefined, undefined, options), JSON.stringify(options))
  } finally { await f.close() }
})

interface TestSocket extends EventEmitter { readonly readyState: number; close(): void; terminate(): void }
const Ws = createRequire(import.meta.url)('ws') as new (url: string, options: { headers: Record<string, string> }) => TestSocket

async function handshake(url: string, headers: Record<string, string>): Promise<{ status: number; body?: { type: string } }> {
  const socket = new Ws(url, { headers })
  try {
    return await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => reject(new Error('WebSocket handshake did not finish')), 5000)
      socket.once('unexpected-response', (_request: unknown, response: IncomingMessage) => {
        clearTimeout(timeout); resolve({ status: response.statusCode! })
        // An HTTP rejection is not a WebSocket connection; close its raw stream.
        response.destroy()
      })
      socket.once('message', (bytes: Buffer) => {
        clearTimeout(timeout)
        try { resolve({ status: 101, body: JSON.parse(String(bytes)) }) } catch (error) { reject(error) }
      })
      socket.once('error', (error: Error) => { clearTimeout(timeout); reject(error) })
    })
  } finally {
    if (socket.readyState !== 3) await new Promise<void>(resolve => {
      const timeout = setTimeout(resolve, 1000)
      socket.once('close', () => { clearTimeout(timeout); resolve() })
      socket.terminate()
    })
  }
}

function get(url: string, headers: Record<string, string>): Promise<{ status: number; authenticate: string | undefined }> {
  // Node's fetch controls Host itself; raw HTTP models a reverse proxy preserving it.
  return new Promise((resolve, reject) => {
    const request = httpRequest(url, { headers, agent: false }, response => {
      response.resume()
      response.once('end', () => resolve({ status: response.statusCode!, authenticate: response.headers['www-authenticate'] }))
    })
    request.setTimeout(5000, () => request.destroy(new Error('HTTP request did not finish')))
    request.once('error', reject); request.end()
  })
}

test('真实 HTTP 和 WebSocket 通过同一公网认证与来源检查', { timeout: 15000 }, async () => {
  const f = fixture(access)
  try {
    const address = await f.app.listen({ host: '127.0.0.1', port: 0 })
    const response = await get(address + '/api/products', publicHeaders)
    assert.equal(response.status, 200)
    const absent = await get(address + '/api/products', { host: 'studio.example.com' })
    assert.equal(absent.status, 401); assert.match(absent.authenticate ?? '', /^Basic\s/i)
    const socketUrl = address.replace('http:', 'ws:') + '/ws'
    assert.equal((await handshake(socketUrl, { host: 'studio.example.com', origin: publicUrl })).status, 401)
    assert.equal((await handshake(socketUrl, { ...publicHeaders, origin: 'https://other.invalid' })).status, 403)
    assert.equal((await handshake(socketUrl, { ...publicHeaders, host: 'other.invalid', 'x-forwarded-host': 'studio.example.com' })).status, 403)
    const connected = await handshake(socketUrl, publicHeaders)
    assert.equal(connected.status, 101); assert.equal(connected.body?.type, 'connected')
  } finally { await f.close() }
})
