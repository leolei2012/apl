import type { Context } from 'cordis'
import z from 'schemastery'
import Fastify, { type FastifyInstance } from 'fastify'
import websocket from '@fastify/websocket'
import staticFiles from '@fastify/static'
import { existsSync } from 'node:fs'
import { resolve } from 'node:path'
import { parseDefinition, validateDefinition, interfaceHeader, STUDIO_VERSION, ValidationError } from '@apl-studio/model'
import { profileWorkbook, firmwarePackage, memoryEstimateReport, definitionPackage } from '@apl-studio/definition'
import { problem, type ProductStore } from '@apl-studio/product'
import { RuntimeProfileProvider, type TemplateRegistry } from '@apl-studio/templates'
import type { SdkBuildService, SimulationService, AcceptanceService } from '@apl-studio/build'
import type { SimulationAction } from '@apl-studio/model/simulation'
import { applyTemplate } from '@apl-studio/model/template'
import { installAccess, normalizeAccessOptions, type AccessOptions } from './access'
export { normalizeAccessOptions, type AccessOptions } from './access'

export const name = 'api'
export const inject = ['products', 'templates']
export interface Config extends AccessOptions { host: string; port: number; staticDir: string }
export const Config: z<Config> = z.object({ host: z.string().default('127.0.0.1'), port: z.number().default(9190), staticDir: z.string(),
  publicUrl: z.string().default(''), username: z.string().default(''), password: z.string().default('') })
export function createApi(products: ProductStore, templates: TemplateRegistry, staticDir?: string, builds?: SdkBuildService, simulations?: SimulationService, acceptances?: AcceptanceService, access: AccessOptions = {}): FastifyInstance {
  const options = normalizeAccessOptions(access)
  const app = Fastify({ bodyLimit: 1024 * 1024, logger: false })
  app.setErrorHandler((error, _request, reply) => {
    const status = (error as { statusCode?: number }).statusCode ?? 500
    reply.code(status).send({ error: status >= 500 ? '服务暂时无法完成操作' : (error as Error).message, code: (error as { code?: string }).code, issues: error instanceof ValidationError ? error.issues : undefined })
    if (status >= 500) app.log.error(error)
  })
  // The WebSocket hook must mark Upgrade requests before access can reject them,
  // allowing the plugin to close rejected sockets and complete service shutdown.
  app.register(websocket)
  app.after(error => {
    if (error) throw error
    installAccess(app, options)
    app.get('/health', async () => ({ status: 'ok', version: STUDIO_VERSION }))
    function validateBound(input: unknown) {
      const definition = parseDefinition(input)
      if (definition.template) {
        const provider = templates.get(definition.template.id, definition.template.version)
        if (!provider) throw problem(409, '绑定的源码模板版本不可用，请重新选择或解除绑定', 'TEMPLATE_UNAVAILABLE')
        const issues = provider.validate(definition)
        if (issues.length) throw new ValidationError(issues.map(message => ({ path: 'template', message })))
      }
      return definition
    }
    app.get('/api/system', async () => ({ version: STUDIO_VERSION, storage: 'SQLite', mode: options.publicUrl ? 'shared' : 'local', capabilities: { products: true, definitionExport: true, interfacePreview: true, aplExecution: !!simulations, simulation: !!simulations, sdkGeneration: !!builds && templates.list().length > 0, sdkConsumerVerification: !!builds } }))
    app.get('/api/products', async () => products.list())
    app.post('/api/products', async (request, reply) => reply.code(201).send(products.create(validateBound(request.body))))
    app.get<{ Params: { id: string } }>('/api/products/:id', async request => products.get(request.params.id))
    app.put<{ Params: { id: string }; Body: { revision: number; definition: unknown } }>('/api/products/:id', async request => {
      if (!request.body || typeof request.body !== 'object') throw problem(400, '缺少产品内容', 'INVALID_BODY')
      return products.update(request.params.id, request.body.revision, validateBound(request.body.definition))
    })
    app.delete<{ Params: { id: string }; Body: { revision: number } }>('/api/products/:id', async (request, reply) => {
      products.remove(request.params.id, request.body?.revision)
      return reply.code(204).send()
    })
    app.get<{ Params: { id: string } }>('/api/products/:id/revisions', async request => products.revisions(request.params.id))
    app.post('/api/definitions/validate', async request => ({ issues: validateDefinition(request.body) }))
    app.post('/api/definitions/interface', async request => ({ filename: 'apl_product_contract.h', content: interfaceHeader(parseDefinition(request.body)) }))
    app.get<{ Params: { id: string } }>('/api/products/:id/definition', async (request, reply) => {
      const product = products.get(request.params.id)
      reply.header('Content-Disposition', `attachment; filename="${product.definition.code}.apl.json"`)
      return reply.type('application/json; charset=utf-8').send(JSON.stringify(product.definition, null, 2))
    })
    app.get<{ Params: { id: string } }>('/api/products/:id/export', async (request, reply) => {
      const pack = definitionPackage(products.get(request.params.id))
      return reply.header('Content-Disposition', `attachment; filename="${pack.filename}"`).header('X-Definition-Fingerprint', pack.fingerprint).type('application/zip').send(Buffer.from(pack.bytes))
    })
    app.get('/api/profile/catalog', async () => {
      const runtime = templates.list().find(t => t.id === 'apl.runtime-profile')
      const provider = runtime && templates.get(runtime.id, runtime.version)
      if (!(provider instanceof RuntimeProfileProvider)) throw problem(409, '当前服务尚未加载运行时参数版本，请重启新版 Studio', 'PROFILE_UNAVAILABLE')
      return provider.catalog
    })
    app.get<{ Params: { id: string }; Querystring: { revision: string } }>('/api/products/:id/parameters.xlsx', async (request, reply) => {
      const product = products.get(request.params.id), reference = product.definition.template
      if (Number(request.query.revision) !== product.revision) throw problem(409, '配置已更新，请刷新后导出', 'REVISION_CONFLICT')
      const provider = reference && templates.get(reference.id, reference.version)
      if (!(provider instanceof RuntimeProfileProvider)) throw problem(409, '请选择支持运行时参数的 APL 版本', 'PROFILE_UNAVAILABLE')
      const issues = provider.validate(product.definition)
      if (issues.length) throw new ValidationError(issues.map(message => ({ path: 'profile', message })))
      return reply.type('application/vnd.openxmlformats-officedocument.spreadsheetml.sheet').header('Content-Disposition', `attachment; filename="${product.definition.code}-r${product.revision}.xlsx"`).send(Buffer.from(profileWorkbook(provider.workbook, product, provider.catalog)))
    })
    app.get('/api/templates', async () => ({ templates: templates.list(), status: templates.list().length ? 'ready' : 'awaiting-source-template' }))
    app.get<{ Params: { id: string }; Querystring: { revision: string } }>('/api/products/:id/memory-estimate.json', async (request, reply) => {
      const product = products.get(request.params.id), reference = product.definition.template
      if (Number(request.query.revision) !== product.revision) throw problem(409, '配置已更新，请刷新后导出', 'REVISION_CONFLICT')
      const provider = reference && templates.get(reference.id, reference.version)
      if (!(provider instanceof RuntimeProfileProvider)) throw problem(409, '请选择支持运行时参数的 APL 版本', 'PROFILE_UNAVAILABLE')
      const issues = provider.validate(product.definition)
      if (issues.length) throw new ValidationError(issues.map(message => ({ path: 'memory', message })))
      return reply.type('application/json; charset=utf-8').header('Content-Disposition', `attachment; filename="${product.definition.code}-memory-r${product.revision}.json"`)
        .send(JSON.stringify(memoryEstimateReport(product, provider.catalog.source.fingerprint), null, 2) + '\n')
    })
    app.get<{ Params: { id: string }; Querystring: { revision: string } }>('/api/products/:id/firmware.zip', async (request, reply) => {
      const product = products.get(request.params.id), reference = product.definition.template
      if (Number(request.query.revision) !== product.revision) throw problem(409, '配置已更新，请刷新后导出', 'REVISION_CONFLICT')
      const provider = reference && templates.get(reference.id, reference.version)
      if (!(provider instanceof RuntimeProfileProvider)) throw problem(409, '请选择支持运行时参数的 APL 版本', 'PROFILE_UNAVAILABLE')
      const issues = provider.validate(product.definition)
      if (issues.length) throw new ValidationError(issues.map(message => ({ path: 'firmware', message })))
      const pack = firmwarePackage(product, provider.firmwareFiles(product.definition), provider.catalog.source.fingerprint)
      return reply.type('application/zip').header('Content-Disposition', `attachment; filename="${pack.filename}"`).send(Buffer.from(pack.bytes))
    })
    app.get<{ Params: { id: string } }>('/api/products/:id/acceptance', async request => {
      if (!acceptances) throw problem(409, '场景验收服务尚未接入', 'ACCEPTANCE_UNAVAILABLE')
      return acceptances.latest(products.get(request.params.id))
    })
    app.post<{ Body: { productId: string; revision: number } }>('/api/acceptance/run', async (request, reply) => {
      if (!acceptances) throw problem(409, '场景验收服务尚未接入', 'ACCEPTANCE_UNAVAILABLE')
      const body = request.body
      if (!body || typeof body.productId !== 'string' || !Number.isSafeInteger(body.revision) || Object.keys(body).some(key => !['productId', 'revision'].includes(key))) throw problem(400, '仅接受产品和修订号，验收结果由服务器运行真实C取得', 'INVALID_ACCEPTANCE_REQUEST')
      const product = products.get(body.productId)
      if (product.revision !== body.revision) throw problem(409, '产品已更新，请保存并读取最新修订后验收', 'REVISION_CONFLICT')
      return reply.code(202).send(acceptances.run(product))
    })
    app.post<{ Params: { id: string; version: string }; Body: { definition: unknown } }>('/api/templates/:id/:version/configure', async request => {
      const provider = templates.get(request.params.id, request.params.version)
      if (!provider) throw problem(404, '源码模板版本不存在', 'TEMPLATE_UNAVAILABLE')
      return { definition: applyTemplate(parseDefinition(request.body.definition), provider.descriptor) }
    })
    app.post<{ Body: { definition: unknown } }>('/api/templates/preview', async request => {
      const definition = validateBound(request.body?.definition), reference = definition.template
      const provider = reference && templates.get(reference.id, reference.version)
      if (!provider) throw problem(409, '请先绑定源码模板', 'SOURCE_TEMPLATE_REQUIRED')
      const files = await provider.generate(definition)
      return { files: Object.fromEntries((provider.descriptor.previewFiles ?? ['include/apl_product.h', 'config/apl_core_config.h', 'config/apl_hmi_config.h', 'README.md']).map(path => { const data = files.get(path); if (!data) throw new Error('模板声明的预览文件不存在：' + path); return [path, Buffer.from(data).toString('utf8')] })) }
    })
    app.post<{ Body: { productId: string; revision: number } }>('/api/sdk/generate', async (request, reply) => {
      if (!builds || !templates.list().length) throw problem(409, '请先接入可用的源码模板与构建服务', 'SOURCE_TEMPLATE_REQUIRED')
      if (!request.body || typeof request.body.productId !== 'string' || !Number.isSafeInteger(request.body.revision)) throw problem(400, '需要产品 ID 和已保存的修订号', 'INVALID_BUILD_REQUEST')
      const product = products.get(request.body.productId)
      if (product.revision !== request.body.revision) throw problem(409, '产品已更新，请读取最新修订后生成', 'REVISION_CONFLICT')
      return reply.code(202).send(await builds.create(product))
    })
    app.get<{ Params: { id: string } }>('/api/products/:id/sdk/jobs', async request => { products.get(request.params.id); return builds?.list(request.params.id) ?? [] })
    app.get<{ Params: { id: string } }>('/api/sdk/jobs/:id', async request => {
      if (!builds) throw problem(409, '构建服务尚未接入', 'SOURCE_TEMPLATE_REQUIRED')
      return builds.get(request.params.id)
    })
    app.get<{ Params: { id: string } }>('/api/sdk/jobs/:id/download', async (request, reply) => {
      if (!builds) throw problem(409, '构建服务尚未接入', 'SOURCE_TEMPLATE_REQUIRED')
      const artifact = builds.artifact(request.params.id)
      return reply.header('Content-Disposition', `attachment; filename="${artifact.filename}"`).type('application/zip').send(artifact.bytes)
    })
    function simulationService() { if (!simulations) throw problem(409, '仿真服务尚未接入', 'SIMULATION_UNAVAILABLE'); return simulations }
    app.get<{ Params: { id: string } }>('/api/products/:id/simulation', async request => ({ descriptor: simulationService().describe(products.get(request.params.id)) }))
    app.post<{ Body: { productId: string; revision: number; running?: boolean } }>('/api/simulation/sessions', async (request, reply) => {
      const body = request.body
      if (!body || typeof body.productId !== 'string' || !Number.isSafeInteger(body.revision) || body.running !== undefined && typeof body.running !== 'boolean') throw problem(400, '需要产品 ID 和已保存的修订号', 'INVALID_SIMULATION_REQUEST')
      const product = products.get(body.productId)
      if (product.revision !== body.revision) throw problem(409, '产品已更新，请读取最新修订后启动仿真', 'REVISION_CONFLICT')
      return reply.code(202).send(await simulationService().create(product, body.running ?? true))
    })
    app.get<{ Params: { id: string } }>('/api/simulation/sessions/:id', async request => simulationService().get(request.params.id))
    app.post<{ Params: { id: string }; Body: SimulationAction }>('/api/simulation/sessions/:id/actions', async request => simulationService().action(request.params.id, request.body))
    app.delete<{ Params: { id: string } }>('/api/simulation/sessions/:id', async (request, reply) => { simulationService().stop(request.params.id); return reply.code(204).send() })
    app.register(async instance => {
      instance.get('/ws', { websocket: true }, socket => {
        socket.send(JSON.stringify({ type: 'connected', version: STUDIO_VERSION }))
        const interval = setInterval(() => { if (socket.readyState === 1) socket.ping() }, 25000)
        socket.on('close', () => clearInterval(interval))
      })
    })
    if (staticDir && existsSync(resolve(staticDir, 'index.html'))) {
      app.register(staticFiles, { root: resolve(staticDir) })
      app.setNotFoundHandler((request, reply) => request.url.startsWith('/api/') || request.url.startsWith('/ws')
        ? reply.code(404).send({ error: '接口不存在' }) : reply.sendFile('index.html'))
    }
  })
  return app
}
declare module 'cordis' { interface Context { api: FastifyInstance } }
export function apply(ctx: Context, config: Config) {
  const app = createApi(ctx.products, ctx.templates, config.staticDir, ctx.get('sdkBuilds', false), ctx.get('simulations', false), ctx.get('acceptances', false), config)
  ctx.provide('api', app)
  ctx.effect(() => () => app.close())
  ctx.on('product/changed', (id, revision) => {
    for (const socket of app.websocketServer.clients) if (socket.readyState === 1) socket.send(JSON.stringify({ type: 'product/changed', id, revision }))
  })
  ctx.on('sdk/changed', job => {
    for (const socket of app.websocketServer.clients) if (socket.readyState === 1) socket.send(JSON.stringify({ type: 'sdk/changed', job }))
  })
}
