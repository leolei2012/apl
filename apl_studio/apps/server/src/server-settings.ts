import { isIP } from 'node:net'
import { normalizeAccessOptions } from '@apl-studio/api'

export const STUDIO_HOST = '127.0.0.1'
export const STUDIO_PORT = 9190

export interface StudioNetworkOptions {
  host?: string; port?: number; publicUrl?: string; username?: string; password?: string
}
export interface ServerSettings {
  host: string; port: number; publicUrl: string; username: string; password: string
}

function loopback(host: string): boolean { return ['localhost', '127.0.0.1', '::1'].includes(host.toLowerCase()) }
function validHost(host: unknown): host is string {
  if (typeof host !== 'string' || !host || host.length > 253 || /[\s/\\@?#]/.test(host)) return false
  if (isIP(host)) return true
  return host.split('.').every(label => /^[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?$/i.test(label))
}

/** Resolve and validate deployment settings without opening files or sockets.
 * Programmatic options override environment values. Only options.port may be 0
 * for isolated tests; the command-line environment always requires a real port.
 */
export function resolveServerSettings(options: StudioNetworkOptions = {}, environment: NodeJS.ProcessEnv = process.env): ServerSettings {
  const host = options.host ?? environment.APL_STUDIO_HOST ?? STUDIO_HOST
  if (!validHost(host)) throw new Error('APL_STUDIO_HOST 必须是有效的主机名或 IP 地址，不包含协议、端口或路径')
  let port: number
  if (options.port !== undefined) {
    port = options.port
    if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('监听端口必须是 0～65535 的整数')
  } else {
    const configured = environment.APL_STUDIO_PORT
    if (configured === undefined) port = STUDIO_PORT
    else {
      if (!/^\d+$/.test(configured)) throw new Error('APL_STUDIO_PORT 必须是 1～65535 的整数')
      port = Number(configured)
      if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('APL_STUDIO_PORT 必须是 1～65535 的整数')
    }
  }
  let access: ReturnType<typeof normalizeAccessOptions>
  try {
    access = normalizeAccessOptions({
      publicUrl: options.publicUrl ?? environment.APL_STUDIO_PUBLIC_URL ?? '',
      username: options.username ?? environment.APL_STUDIO_USERNAME ?? '',
      password: options.password ?? environment.APL_STUDIO_PASSWORD ?? '',
    })
  } catch (error) {
    throw new Error('APL_STUDIO_PUBLIC_URL / APL_STUDIO_USERNAME / APL_STUDIO_PASSWORD：' + (error as Error).message)
  }
  if (!loopback(host) && !access.publicUrl) throw new Error('非本机监听需要设置 APL_STUDIO_PUBLIC_URL 和访问账号密码')
  return { host, port, ...access }
}
