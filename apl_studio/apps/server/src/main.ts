import { Context } from 'cordis'
import { resolve, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import * as product from '@apl-studio/product'
import * as templates from '@apl-studio/templates'
import * as api from '@apl-studio/api'
import * as build from '@apl-studio/build'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')
const data = resolve(process.env.APL_STUDIO_DATA ?? resolve(root, '.data'))
const port = Number(process.env.APL_STUDIO_PORT ?? 9190)
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('APL_STUDIO_PORT 必须为有效端口')
const ctx = new Context()
await ctx.plugin(product, { dbPath: resolve(data, 'studio.db') })
await ctx.plugin(templates)
ctx.templates.loadDirectory(resolve(process.env.APL_STUDIO_TEMPLATES ?? resolve(root, '../apl_sdk_template')))
await ctx.plugin(build, { directory: resolve(data, 'sdk') })
await ctx.plugin(api, { host: '127.0.0.1', port, staticDir: resolve(root, 'apps/web/dist') })
try {
  await ctx.api.listen({ host: '127.0.0.1', port })
  console.log(`\n  APL Studio  http://127.0.0.1:${port}\n  产品数据    ${data}\n`)
} catch (error) {
  console.error('启动失败：', (error as Error).message)
  await ctx.fiber.dispose()
  process.exitCode = 1
}
let stopping = false
for (const signal of ['SIGINT', 'SIGTERM'] as const) process.on(signal, async () => {
  if (stopping) return
  stopping = true
  await ctx.fiber.dispose()
})
