import type { Context } from 'cordis'
import z from 'schemastery'
import { spawn, type ChildProcess } from 'node:child_process'
import { mkdirSync, writeFileSync, readFileSync, existsSync, readdirSync, renameSync } from 'node:fs'
import { resolve, dirname, delimiter } from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { zipSync } from 'fflate'
import { STUDIO_VERSION, ValidationError, type Product } from '@apl-studio/model'
import type { BuildJob, NativeVerification } from '@apl-studio/model/template'
import { safeRelative, type TemplateRegistry } from '@apl-studio/templates'
import { SimulationService } from './simulation'
export { SimulationService } from './simulation'
export { simulationAdapters, type SimulationAdapter } from './simulation-adapters'

const uuid = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/i
const failure = (statusCode: number, message: string, code: string) => Object.assign(new Error(message), { statusCode, code })
export function defaultCompiler(): string {
  return process.env.APL_STUDIO_CC ?? process.env.CC ?? (process.platform === 'win32' && existsSync('C:/Qt/Tools/mingw1310_64/bin/gcc.exe') ? 'C:/Qt/Tools/mingw1310_64/bin/gcc.exe' : 'gcc')
}
export function validateFiles(files: Map<string, Uint8Array>) {
  const seen = new Set<string>()
  for (const [path, content] of files) {
    safeRelative(path)
    if (seen.has(path.toLowerCase()) || !(content instanceof Uint8Array)) throw new Error('模板文件重复或内容无效：' + path)
    seen.add(path.toLowerCase())
  }
  if (!files.size) throw new Error('模板生成了空文件集')
}
export class SdkBuildService {
  private jobs = new Map<string, BuildJob>()
  private queue: { job: BuildJob; files: Map<string, Uint8Array>; verification: NativeVerification }[] = []
  private current?: ChildProcess
  private running?: Promise<void>
  private closed = false
  readonly directory: string
  constructor(directory: string, private registry: TemplateRegistry, private compiler = defaultCompiler(), private notify: (job: BuildJob) => void = () => {}) {
    this.directory = resolve(directory)
    mkdirSync(this.directory, { recursive: true })
    for (const entry of readdirSync(this.directory, { withFileTypes: true })) {
      if (!entry.isDirectory() || entry.isSymbolicLink() || !uuid.test(entry.name)) continue
      try {
        const job = JSON.parse(readFileSync(resolve(this.directory, entry.name, 'job.json'), 'utf8')) as BuildJob
        if (job.id !== entry.name || !uuid.test(job.productId)) continue
        if (job.status === 'queued' || job.status === 'building') { job.status = 'failed'; job.phase = '任务中断'; job.diagnostics += '\n服务重启中断了任务，请重新生成。' }
        if (job.status === 'succeeded' && !existsSync(resolve(this.directory, job.id, 'apl-sdk.zip'))) { job.status = 'failed'; job.phase = '产物缺失' }
        this.jobs.set(job.id, job)
      } catch { /* An incomplete record is never treated as a successful artifact. */ }
    }
  }
  list(productId: string): BuildJob[] { return [...this.jobs.values()].filter(j => j.productId === productId).sort((a, b) => b.createdAt.localeCompare(a.createdAt)).map(j => structuredClone(j)) }
  get(id: string): BuildJob { const job = this.jobs.get(id); if (!job) throw failure(404, '生成任务不存在', 'JOB_NOT_FOUND'); return structuredClone(job) }
  artifact(id: string): { bytes: Buffer; filename: string } {
    const job = this.get(id)
    if (job.status !== 'succeeded') throw failure(409, 'SDK 尚未通过编译和消费验证，不能下载', 'SDK_NOT_VERIFIED')
    return { bytes: readFileSync(resolve(this.directory, job.id, 'apl-sdk.zip')), filename: job.filename! }
  }
  async create(product: Product): Promise<BuildJob> {
    if (this.closed) throw failure(503, '构建服务正在停止', 'SERVICE_CLOSING')
    const reference = product.definition.template
    const provider = reference && this.registry.get(reference.id, reference.version)
    if (!provider) throw failure(409, '请先绑定可用的源码模板版本', 'SOURCE_TEMPLATE_REQUIRED')
    const errors = provider.validate(product.definition)
    if (errors.length) throw new ValidationError(errors.map(message => ({ path: 'template', message })))
    const verification = structuredClone(provider.descriptor.verification ?? { sourceDirectories: ['src', 'port', 'examples'], includeDirectories: ['include', 'config', 'port'], consumer: 'verification/smoke.c', scope: 'native C11 configuration and basic consumer' })
    for (const path of [...verification.sourceDirectories, ...verification.includeDirectories, verification.consumer]) safeRelative(path)
    const files = await provider.generate(structuredClone(product.definition))
    validateFiles(files)
    if (!files.has(verification.consumer)) throw new Error('模板缺少声明的消费验证程序')
    // Own all bytes now: later product or provider edits cannot change this job.
    const snapshot = new Map([...files].map(([path, bytes]) => [path, Uint8Array.from(bytes)]))
    const now = new Date().toISOString()
    const job: BuildJob = { id: randomUUID(), productId: product.id, revision: product.revision, productName: product.definition.name, productCode: product.definition.code,
      sdkVersion: product.definition.sdkVersion, template: { id: reference!.id, version: reference!.version, fingerprint: provider.descriptor.fingerprint },
      createdAt: now, updatedAt: now, sourceVersion: provider.descriptor.sourceVersion, verificationScope: verification.scope, status: 'queued', phase: '等待编译', diagnostics: '', compilerVersion: '', checks: { compile: 'pending', consumer: 'pending' } }
    mkdirSync(resolve(this.directory, job.id), { recursive: true })
    this.persist(job)
    this.jobs.set(job.id, job)
    this.queue.push({ job, files: snapshot, verification })
    this.notify(structuredClone(job))
    this.pump()
    return structuredClone(job)
  }
  private persist(job: BuildJob) {
    job.updatedAt = new Date().toISOString()
    const path = resolve(this.directory, job.id, 'job.json')
    writeFileSync(path + '.tmp', JSON.stringify(job, null, 2))
    renameSync(path + '.tmp', path)
  }
  private update(job: BuildJob) { this.persist(job); this.notify(structuredClone(job)) }
  private pump() {
    if (this.running || this.closed) return
    const pending = this.queue.shift()
    if (!pending) return
    this.running = this.run(pending.job, pending.files, pending.verification).catch(error => {
      pending.job.status = 'failed'; pending.job.phase = '任务失败'; pending.job.diagnostics += '\n' + (error as Error).message
      try { this.update(pending.job) } catch { this.notify(structuredClone(pending.job)) }
    }).finally(() => { this.running = undefined; this.pump() })
  }
  private command(args: string[], cwd: string, executable = this.compiler, timeoutMs = 120000): Promise<string> {
    return new Promise((resolveOutput, reject) => {
      let output = '', timedOut = false
      const environment = { ...process.env, PATH: dirname(this.compiler) + delimiter + (process.env.PATH ?? '') }
      const processHandle = spawn(executable, args, { cwd, env: environment, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'], shell: false })
      this.current = processHandle
      const collect = (chunk: Buffer) => { if (output.length < 256 * 1024) output += chunk.toString('utf8') }
      processHandle.stdout.on('data', collect); processHandle.stderr.on('data', collect)
      const timeout = setTimeout(() => { timedOut = true; processHandle.kill() }, timeoutMs)
      processHandle.once('error', error => { clearTimeout(timeout); this.current = undefined; reject(new Error('无法启动本机 C 编译器或验证程序：' + error.message)) })
      processHandle.once('close', code => {
        clearTimeout(timeout); this.current = undefined
        if (this.closed) reject(new Error('构建服务已停止'))
        else if (timedOut) reject(new Error('构建或验证超时\n' + output))
        else if (code !== 0) reject(new Error(`进程退出码 ${code}\n${output}`))
        else resolveOutput(output)
      })
    })
  }
  private async run(job: BuildJob, files: Map<string, Uint8Array>, verification: NativeVerification) {
    const root = resolve(this.directory, job.id), sdk = resolve(root, 'package'), output = resolve(root, 'build')
    mkdirSync(sdk, { recursive: true }); mkdirSync(output, { recursive: true })
    job.status = 'building'; job.phase = '准备源码快照'; this.update(job)
    for (const [path, bytes] of files) { const target = resolve(sdk, path); mkdirSync(dirname(target), { recursive: true }); writeFileSync(target, bytes) }
    try {
      job.compilerVersion = (await this.command(['--version'], root)).split(/\r?\n/)[0]
      job.phase = '编译 C11 SDK 与移植示例'; this.update(job)
      const exe = resolve(output, process.platform === 'win32' ? 'consumer.exe' : 'consumer')
      const sources = [...files.keys()].filter(path => path.endsWith('.c') && (verification.sourceDirectories.some(folder => path.startsWith(folder + '/'))))
      const args = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-finput-charset=UTF-8', ...verification.includeDirectories.map(folder => '-I' + resolve(sdk, folder)),
        ...sources.filter(path => path !== verification.consumer).map(path => resolve(sdk, path)), resolve(sdk, verification.consumer), '-o', exe]
      job.diagnostics += await this.command(args, root)
      job.checks.compile = 'passed'; job.phase = '运行模板消费验证'; this.update(job)
      job.diagnostics += await this.command([], root, exe, 15000)
      job.checks.consumer = 'passed'; job.phase = '打包源码 SDK'; this.update(job)
      const hashes = Object.fromEntries([...files].sort(([a], [b]) => a < b ? -1 : 1).map(([path, bytes]) => [path, createHash('sha256').update(bytes).digest('hex')]))
      job.fingerprint = createHash('sha256').update(JSON.stringify(hashes)).digest('hex')
      const manifest = { format: 'apl-sdk', schemaVersion: 1, studioVersion: STUDIO_VERSION, product: { id: job.productId, revision: job.revision, code: job.productCode, version: job.sdkVersion },
        template: job.template, sourceVersion: job.sourceVersion,
        fingerprint: job.fingerprint, files: hashes, sdkReady: true, verification: { ...job.checks, compiler: job.compilerVersion, scope: verification.scope } }
      files.set('manifest.json', Buffer.from(JSON.stringify(manifest, null, 2) + '\n'))
      writeFileSync(resolve(sdk, 'manifest.json'), files.get('manifest.json')!)
      const archive: Record<string, [Uint8Array, { mtime: Date }]> = {}
      for (const [path, bytes] of files) archive['apl-sdk/' + path] = [bytes, { mtime: new Date(1980, 0, 1) }]
      writeFileSync(resolve(root, 'apl-sdk.zip.tmp'), zipSync(archive))
      renameSync(resolve(root, 'apl-sdk.zip.tmp'), resolve(root, 'apl-sdk.zip'))
      job.filename = `${job.productCode}-sdk-${job.sdkVersion}-r${job.revision}.zip`
      job.status = 'succeeded'; job.phase = '编译与消费验证通过'; this.update(job)
    } catch (error) {
      job.status = this.closed ? 'cancelled' : 'failed'
      if (job.checks.compile === 'pending') job.checks.compile = 'failed'
      else if (job.checks.consumer === 'pending') job.checks.consumer = 'failed'
      job.phase = this.closed ? '任务已停止' : '生成未通过验证'
      job.diagnostics += '\n' + (error as Error).message
      this.update(job)
    }
  }
  async close() {
    this.closed = true
    for (const pending of this.queue.splice(0)) { pending.job.status = 'cancelled'; pending.job.phase = '服务停止'; this.update(pending.job) }
    this.current?.kill()
    await this.running
  }
}
export const name = 'build'
export const inject = ['templates']
export interface Config { directory: string; compiler?: string }
export const Config: z<Config> = z.object({ directory: z.string().required(), compiler: z.string() })
declare module 'cordis' {
  interface Context { sdkBuilds: SdkBuildService; simulations: SimulationService }
  interface Events { 'sdk/changed'(job: BuildJob): void }
}
export function apply(ctx: Context, config: Config) {
  const service = new SdkBuildService(config.directory, ctx.templates, config.compiler, job => ctx.emit('sdk/changed', job))
  ctx.provide('sdkBuilds', service)
  const simulations = new SimulationService(resolve(config.directory, '../simulation'), ctx.templates, config.compiler)
  ctx.provide('simulations', simulations)
  ctx.effect(() => async () => { await simulations.close(); await service.close() })
}
