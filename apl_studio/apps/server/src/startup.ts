import { Context } from 'cordis'
import net from 'node:net'
import { resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import * as product from '@apl-studio/product'
import * as templates from '@apl-studio/templates'
import * as api from '@apl-studio/api'
import * as build from '@apl-studio/build'
import { resolveServerSettings, STUDIO_HOST, type StudioNetworkOptions } from './server-settings'

export { STUDIO_HOST, STUDIO_PORT } from './server-settings'
export const STUDIO_ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')

/** ProbeStation-style preflight: fail before opening databases; never pick another port.
 * Another process can still claim the port between this probe and the actual listen.
 */
export async function assertPortAvailable(port: number, host: string = STUDIO_HOST): Promise<void> {
  await new Promise<void>((resolve, reject) => {
    const server = net.createServer()
    server.once('error', (error: NodeJS.ErrnoException) => reject(new Error(error.code === 'EADDRINUSE'
      ? `端口 ${port} 已被占用。若是旧 APL Studio，请在它的终端按 Ctrl+C 后重新启动；否则请检查占用程序。不会自动换端口或停止其他进程。`
      : `无法监听端口 ${port}：${error.code ?? error.message}`)))
    server.listen(port, host, () => server.close(error => error ? reject(error) : resolve()))
  })
}

/** Explicit options also support isolated programmatic tests. */
export interface StudioOptions extends StudioNetworkOptions { dataDirectory?: string; templatesDirectory?: string }
export async function startStudio(options: StudioOptions = {}) {
  const settings = resolveServerSettings(options)
  const { host, port } = settings
  const data = resolve(options.dataDirectory ?? process.env.APL_STUDIO_DATA ?? resolve(STUDIO_ROOT, '.data'))
  const catalog = resolve(options.templatesDirectory ?? process.env.APL_STUDIO_TEMPLATES ?? resolve(STUDIO_ROOT, '../apl_sdk_template'))
  await assertPortAvailable(port, host)
  const ctx = new Context()
  try {
    await ctx.plugin(product, { dbPath: resolve(data, 'studio.db') })
    await ctx.plugin(templates)
    ctx.templates.loadDirectory(catalog)
    ctx.templates.register(new templates.RuntimeProfileProvider(resolve(STUDIO_ROOT, '..')))
    await ctx.plugin(build, { directory: resolve(data, 'sdk') })
    await ctx.plugin(api, { ...settings, staticDir: resolve(STUDIO_ROOT, 'apps/web/dist') })
    const url = await ctx.api.listen({ host, port })
    const address = ctx.api.server.address()
    let stopping = false
    return { url, data, host, port: address && typeof address === 'object' ? address.port : port, publicUrl: settings.publicUrl, async close() {
      if (stopping) return
      stopping = true
      await ctx.fiber.dispose()
    } }
  } catch (error) {
    await ctx.fiber.dispose()
    throw error
  }
}
