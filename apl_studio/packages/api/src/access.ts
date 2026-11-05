import { createHash, timingSafeEqual } from 'node:crypto'
import type { FastifyInstance } from 'fastify'

/** One shared workspace protected by deployment credentials; no user accounts. */
export interface AccessOptions { publicUrl?: string; username?: string; password?: string }
const localHosts = new Set(['localhost', '127.0.0.1', '[::1]'])
const developmentOrigins = ['http://127.0.0.1:5174', 'http://localhost:5174']

export function normalizeAccessOptions(options: AccessOptions = {}): Required<AccessOptions> {
  const { publicUrl = '', username = '', password = '' } = options
  if ([publicUrl, username, password].some(value => typeof value !== 'string')) throw new Error('访问配置必须是字符串')
  if (!!username !== !!password) throw new Error('部署账号和密码必须同时配置')
  if (username.length > 128 || password.length > 4096 || /[:\x00-\x1f\x7f]/.test(username) || /[\x00-\x1f\x7f]/.test(password))
    throw new Error('部署账号或密码格式无效')
  let origin = ''
  if (publicUrl) {
    let url: URL
    try { url = new URL(publicUrl) } catch { throw new Error('公网地址需要完整的 http:// 或 https:// 地址') }
    if (!['http:', 'https:'].includes(url.protocol) || url.username || url.password || url.pathname !== '/' || url.search || url.hash || publicUrl !== publicUrl.trim())
      throw new Error('公网地址仅支持域名根路径，不能包含账号、子路径、查询或片段')
    if (!username || !password) throw new Error('公网部署需要配置访问账号和密码')
    origin = url.origin
  }
  return { publicUrl: origin, username, password }
}

function authority(value: string | undefined, protocol = 'http:'): URL | undefined {
  if (!value || /[\s\/@?#\\]/.test(value)) return
  try { return new URL(protocol + '//' + value) } catch { return }
}

/** The configured external origin is authoritative, never caller-supplied proxy headers. */
export function installAccess(app: FastifyInstance, input: AccessOptions = {}) {
  const options = normalizeAccessOptions(input)
  const external = options.publicUrl ? new URL(options.publicUrl) : undefined
  const expected = options.username ? createHash('sha256').update(options.username + ':' + options.password).digest() : undefined
  app.addHook('onRequest', async (request, reply) => {
    const local = authority(request.headers.host)
    const publicAddress = external && authority(request.headers.host, external.protocol)
    const isPublic = !!external && publicAddress?.host === external.host
    if (!local || !isPublic && !localHosts.has(local.hostname))
      return reply.code(403).send({ error: '请求域名与工作台配置不匹配', code: 'HOST_NOT_ALLOWED' })
    const origin = request.headers.origin
    if (origin !== undefined) {
      const allowed = isPublic ? [external!.origin] : [local.origin, ...(!external ? developmentOrigins : [])]
      if (!allowed.includes(origin)) return reply.code(403).send({ error: '请求来源与工作台不匹配', code: 'ORIGIN_NOT_ALLOWED' })
    }
    // Health probes expose no products or build controls and need no credential.
    if (!expected || request.url.split('?')[0] === '/health') return
    const token = request.headers.authorization?.match(/^Basic ([A-Za-z0-9+/]+={0,2})$/i)?.[1]
    const supplied = token && createHash('sha256').update(Buffer.from(token, 'base64')).digest()
    if (!supplied || !timingSafeEqual(expected, supplied)) return reply.code(401)
      .header('WWW-Authenticate', 'Basic realm="APL Studio", charset="UTF-8"')
      .header('Cache-Control', 'no-store')
      .send({ error: '请输入工作台访问账号和密码', code: 'AUTH_REQUIRED' })
  })
}
