import { spawn, type ChildProcessWithoutNullStreams, type ChildProcess } from 'node:child_process'
import { mkdirSync, writeFileSync, readFileSync } from 'node:fs'
import { resolve, dirname, delimiter } from 'node:path'
import { randomUUID, createHash } from 'node:crypto'
import { ValidationError, type Product, type Value } from '@apl-studio/model'
import type { SimulationAction, SimulationDescriptor, SimulationEvent, SimulationSession, SimulationSlot, SimulationSnapshot } from '@apl-studio/model/simulation'
import { safeRelative, type TemplateRegistry } from '@apl-studio/templates'
import { defaultCompiler, validateFiles } from './index'
import { simulationAdapters, type SimulationAdapter } from './simulation-adapters'

const failure = (statusCode: number, message: string, code = 'SIMULATION_ERROR') => Object.assign(new Error(message), { statusCode, code })
interface NativeEvent { kind: 'input' | 'output'; key: string; value: number; ms: number }
interface Runtime {
  session: SimulationSession; adapter: SimulationAdapter; child?: ChildProcessWithoutNullStreams; compiler?: ChildProcess
  chain: Promise<unknown>; task?: Promise<void>; exit?: Promise<void>; sequence: number; lastSeen: number; automaticPending: boolean
  pending?: { id: number; resolve: (s: SimulationSnapshot) => void; reject: (e: Error) => void; timer: ReturnType<typeof setTimeout> }
}
export function simulationValue(slot: SimulationSlot | undefined, value: Value): string {
  if (slot?.boolean) return value ? '开' : '关'
  return (slot?.options?.find(o => o.value === value)?.label ?? String(value).replaceAll('\0', '')) + (slot?.unit ? ' ' + slot.unit : '')
}
/** Each session owns one C instance, input environment, memory and virtual clock. */
export class SimulationService {
  private runtimes = new Map<string, Runtime>()
  private closed = false
  private timer: ReturnType<typeof setInterval>
  readonly directory: string
  constructor(directory: string, private registry: TemplateRegistry, private compiler = defaultCompiler(), private adapters: SimulationAdapter[] = simulationAdapters) {
    this.directory = resolve(directory)
    mkdirSync(this.directory, { recursive: true })
    this.timer = setInterval(() => {
      for (const runtime of this.runtimes.values()) {
        if (Date.now() - runtime.lastSeen > 120000 && ['building', 'ready'].includes(runtime.session.status)) this.stop(runtime.session.id, '页面超过两分钟未连接，仿真已停止')
        if (runtime.session.status !== 'ready' || !runtime.session.running || runtime.automaticPending) continue
        runtime.automaticPending = true
        void this.enqueue(runtime, async () => { if (runtime.session.running) await this.send(runtime, 0, 10, 0) }).catch(() => {}).finally(() => { runtime.automaticPending = false })
      }
    }, 100)
    this.timer.unref()
  }
  describe(product: Product): SimulationDescriptor | null {
    const reference = product.definition.template, template = reference && this.registry.get(reference.id, reference.version)
    return template ? this.adapters.find(a => a.supports(template.descriptor))?.describe(product.definition) ?? null : null
  }
  get(id: string): SimulationSession {
    const runtime = this.runtime(id)
    runtime.lastSeen = Date.now()
    return structuredClone(runtime.session)
  }
  private runtime(id: string) {
    const runtime = this.runtimes.get(id)
    if (!runtime) throw failure(404, '仿真会话不存在或服务已重启，请重新启动', 'SIMULATION_NOT_FOUND')
    return runtime
  }
  async create(input: Product, running = true): Promise<SimulationSession> {
    if (this.closed) throw failure(503, '仿真服务正在停止')
    const product = structuredClone(input), reference = product.definition.template
    const provider = reference && this.registry.get(reference.id, reference.version)
    if (!provider) throw failure(409, '请先绑定可用的源码模板', 'SOURCE_TEMPLATE_REQUIRED')
    const adapter = this.adapters.find(a => a.supports(provider.descriptor))
    if (!adapter) throw failure(409, '此模板尚未提供 C 仿真桥接', 'SIMULATION_UNSUPPORTED')
    const errors = provider.validate(product.definition)
    if (errors.length) throw new ValidationError(errors.map(message => ({ path: 'template', message })))
    if ([...this.runtimes.values()].filter(r => ['building', 'ready'].includes(r.session.status)).length >= 4) throw failure(409, '最多同时运行四个仿真，请先停止不用的会话')
    // Reserve a slot before asynchronous generation, so simultaneous requests cannot exceed the limit.
    const common = readFileSync(new URL('./runners/common.c', import.meta.url), 'utf8'), source = common.replace('/* ADAPTER */', adapter.source(product.definition))
    const session: SimulationSession = { id: randomUUID(), productId: product.id, revision: product.revision,
      template: { ...reference!, fingerprint: provider.descriptor.fingerprint }, driverFingerprint: createHash('sha256').update(source).digest('hex'),
      descriptor: adapter.describe(product.definition), createdAt: new Date().toISOString(), status: 'building', running, snapshot: null, events: [], diagnostics: '' }
    const runtime: Runtime = { session, adapter, chain: Promise.resolve(), sequence: 0, lastSeen: Date.now(), automaticPending: false }
    this.runtimes.set(session.id, runtime)
    for (const [id, old] of this.runtimes) if (this.runtimes.size > 32 && ['failed', 'stopped'].includes(old.session.status)) this.runtimes.delete(id)
    this.event(runtime, 'system', `准备产品修订 r${product.revision} 的 C 仿真`)
    runtime.task = (async () => {
      const files = await provider.generate(product.definition)
      if (session.status !== 'building') return
      validateFiles(files)
      if (files.has('__studio_simulation.c')) throw new Error('模板占用了仿真桥接文件名')
      const root = resolve(this.directory, session.id), sdk = resolve(root, 'source')
      mkdirSync(sdk, { recursive: true })
      for (const [path, bytes] of files) { const target = resolve(sdk, path); mkdirSync(dirname(target), { recursive: true }); writeFileSync(target, bytes) }
      writeFileSync(resolve(sdk, '__studio_simulation.c'), source)
      writeFileSync(resolve(root, 'session.json'), JSON.stringify({ product, template: session.template, driverFingerprint: session.driverFingerprint }, null, 2))
      const executable = resolve(root, process.platform === 'win32' ? 'simulation.exe' : 'simulation')
      const sources = [...files.keys()].filter(path => path.endsWith('.c') && adapter.sourceDirectories.some(folder => path.startsWith(safeRelative(folder) + '/')))
      if (!sources.length) throw new Error('仿真模板没有业务 C 源码')
      await this.compile(runtime, ['-std=c11', '-Wall', '-Wextra', '-Werror', '-finput-charset=UTF-8', ...adapter.includeDirectories.map(path => '-I' + resolve(sdk, safeRelative(path))), ...sources.map(path => resolve(sdk, path)), resolve(sdk, '__studio_simulation.c'), '-o', executable], root)
      if (session.status !== 'building') return
      this.event(runtime, 'system', 'C 业务程序编译通过，启动独立实例')
      await this.launch(runtime, executable, root)
      if (session.status === 'building') { session.status = 'ready'; this.event(runtime, 'system', '仿真已就绪，设备已上电') }
    })().catch(error => { if (!['stopped', 'failed'].includes(session.status)) this.fail(runtime, (error as Error).message) })
    return structuredClone(session)
  }
  private event(runtime: Runtime, kind: SimulationEvent['kind'], message: string, ms = runtime.session.snapshot?.ms ?? 0) {
    const events = runtime.session.events
    events.push({ seq: (events.at(-1)?.seq ?? 0) + 1, ms, kind, message })
    if (events.length > 300) events.splice(0, events.length - 300)
  }
  private nativeEvent(runtime: Runtime, event: NativeEvent) {
    if (!['input', 'output'].includes(event.kind) || typeof event.key !== 'string' || !Number.isFinite(event.ms) || !Number.isFinite(event.value)) throw new Error('C 仿真事件格式无效')
    const descriptor = runtime.session.descriptor
    if (event.kind === 'input') this.event(runtime, 'input', event.key + (event.value ? '长按' : '短按'), event.ms)
    else {
      const slot = descriptor.outputs.find(s => s.key === event.key)
      this.event(runtime, 'output', `${slot?.name ?? event.key} → ${simulationValue(slot, event.value)}`, event.ms)
    }
  }
  private snapshot(runtime: Runtime, snapshot: SimulationSnapshot) {
    if (!snapshot || typeof snapshot.powered !== 'boolean' || !Number.isSafeInteger(snapshot.ms) || snapshot.ms < 0) throw new Error('C 仿真快照无效')
    for (const group of ['values', 'outputs', 'display', 'internal', 'memory'] as const) {
      const values = snapshot[group]
      if (!values || Array.isArray(values) || typeof values !== 'object' || Object.keys(values).length > 100 || Object.values(values).some(v => !['number', 'boolean', 'string'].includes(typeof v) || typeof v === 'string' && v.length > 500 || typeof v === 'number' && !Number.isFinite(v))) throw new Error('C 仿真显示槽位无效')
    }
    const previous = runtime.session.snapshot
    if (previous) {
      for (const group of ['display', 'internal'] as const) for (const [key, value] of Object.entries(snapshot[group])) {
        if (previous[group][key] === value) continue
        const slot = runtime.session.descriptor[group].find(s => s.key === key)
        // Every named slot is observable; raw lamp/icon bytes remain in the snapshot inspector.
        if (slot) this.event(runtime, 'state', `${slot.name} → ${simulationValue(slot, value)}`, snapshot.ms)
      }
      if (JSON.stringify(previous.memory) !== JSON.stringify(snapshot.memory)) this.event(runtime, 'memory', '模拟存储器已更新', snapshot.ms)
    }
    runtime.session.snapshot = snapshot
  }
  private environment() { return { ...process.env, PATH: dirname(this.compiler) + delimiter + (process.env.PATH ?? '') } }
  private compile(runtime: Runtime, args: string[], cwd: string) {
    return new Promise<void>((accept, reject) => {
      const child = spawn(this.compiler, args, { cwd, env: this.environment(), windowsHide: true, shell: false, stdio: ['ignore', 'pipe', 'pipe'] })
      runtime.compiler = child
      const collect = (chunk: Buffer) => { runtime.session.diagnostics = (runtime.session.diagnostics + chunk.toString('utf8')).slice(-128 * 1024) }
      child.stdout.on('data', collect); child.stderr.on('data', collect)
      const timeout = setTimeout(() => { child.kill(); reject(new Error('仿真编译超时')) }, 60000)
      child.once('error', error => { clearTimeout(timeout); reject(new Error('无法启动本机 C 编译器：' + error.message)) })
      child.once('close', code => { clearTimeout(timeout); runtime.compiler = undefined; code === 0 ? accept() : reject(new Error(`仿真编译失败，退出码 ${code}`)) })
    })
  }
  private launch(runtime: Runtime, executable: string, cwd: string) {
    return new Promise<void>((accept, reject) => {
      const child = spawn(executable, [], { cwd, env: this.environment(), windowsHide: true, shell: false, stdio: ['pipe', 'pipe', 'pipe'] })
      runtime.child = child
      runtime.exit = new Promise(resolveExit => child.once('close', () => resolveExit()))
      let buffer = '', ready = false
      const timeout = setTimeout(() => { reject(new Error('C 仿真程序未返回初始状态')); child.kill() }, 10000)
      child.stderr.on('data', (chunk: Buffer) => { runtime.session.diagnostics = (runtime.session.diagnostics + chunk.toString('utf8')).slice(-128 * 1024) })
      child.stdout.on('data', (chunk: Buffer) => {
        buffer += chunk.toString('utf8')
        try {
          if (buffer.length > 1024 * 1024) throw new Error('C 仿真输出超过限制')
          let end: number
          while ((end = buffer.indexOf('\n')) >= 0) {
            const line = buffer.slice(0, end).trim(); buffer = buffer.slice(end + 1)
            if (!line) continue
            const message = JSON.parse(line)
            if (message.event) this.nativeEvent(runtime, message.event)
            else if (message.id === 0 && !ready) { this.snapshot(runtime, message.snapshot); ready = true; clearTimeout(timeout); accept() }
            else if (runtime.pending?.id === message.id) {
              this.snapshot(runtime, message.snapshot)
              const pending = runtime.pending!; runtime.pending = undefined; clearTimeout(pending.timer); pending.resolve(message.snapshot)
            } else throw new Error('C 仿真回复顺序无效')
          }
        } catch (error) { clearTimeout(timeout); reject(error); this.fail(runtime, (error as Error).message) }
      })
      child.once('error', error => { clearTimeout(timeout); reject(error); this.fail(runtime, error.message) })
      child.once('close', code => {
        clearTimeout(timeout); runtime.child = undefined
        if (!ready) reject(new Error('C 仿真进程启动失败'))
        if (!['stopped', 'failed'].includes(runtime.session.status)) this.fail(runtime, `C 仿真进程意外退出（${code}）`)
      })
    })
  }
  private fail(runtime: Runtime, message: string) {
    runtime.session.status = 'failed'; runtime.session.running = false
    runtime.session.diagnostics = (runtime.session.diagnostics + '\n' + message).slice(-128 * 1024)
    this.event(runtime, 'system', message)
    if (runtime.pending) { clearTimeout(runtime.pending.timer); runtime.pending.reject(new Error(message)); runtime.pending = undefined }
    runtime.child?.kill(); runtime.compiler?.kill()
  }
  private enqueue<T>(runtime: Runtime, action: () => Promise<T>): Promise<T> {
    const task = runtime.chain.then(() => { if (runtime.session.status !== 'ready') throw failure(409, '仿真尚未就绪或已停止'); return action() })
    runtime.chain = task.catch(() => {})
    return task
  }
  private send(runtime: Runtime, op: number, a: number, b: number): Promise<SimulationSnapshot> {
    return new Promise((accept, reject) => {
      const id = ++runtime.sequence
      const timer = setTimeout(() => this.fail(runtime, 'C 仿真操作超时'), 5000)
      runtime.pending = { id, resolve: accept, reject, timer }
      runtime.child!.stdin.write(`${id} ${op} ${a} ${b}\n`, error => { if (error) this.fail(runtime, error.message) })
    })
  }
  async action(id: string, input: SimulationAction): Promise<SimulationSession> {
    const runtime = this.runtime(id)
    runtime.lastSeen = Date.now()
    const invalid = () => failure(400, '仿真操作或输入值无效', 'INVALID_SIMULATION_INPUT')
    if (!input || typeof input !== 'object') throw invalid()
    // Copy before queueing; callers cannot mutate a validated operation during another command.
    const action = structuredClone(input)
    await this.enqueue(runtime, async () => {
      if (action.type === 'running') {
        if (typeof action.value !== 'boolean') throw invalid()
        runtime.session.running = action.value; this.event(runtime, 'system', action.value ? '仿真继续运行' : '仿真已暂停')
      } else if (action.type === 'advance') {
        if (!Number.isSafeInteger(action.ms) || action.ms < 10 || action.ms > 60000 || action.ms % 10) throw invalid()
        await this.send(runtime, 0, action.ms / 10, 0)
      } else if (action.type === 'supply') {
        if (typeof action.on !== 'boolean') throw invalid()
        this.event(runtime, 'system', action.on ? '模拟上电' : '模拟掉电')
        await this.send(runtime, 1, Number(action.on), 0)
      } else if (action.type === 'reset') {
        this.event(runtime, 'system', '重置设备、时钟和模拟存储器')
        await this.send(runtime, 2, 0, 0)
      } else if (action.type === 'input') {
        const control = runtime.session.descriptor.controls.find(c => c.key === action.key)
        if (!control || !Object.hasOwn(runtime.adapter.inputCodes, action.key)) throw invalid()
        const value = action.value
        if (control.target !== 'sensor' && !runtime.session.snapshot?.powered) throw failure(409, '设备已掉电，请先上电')
        if (control.kind === 'gesture' ? !['short', 'long'].includes(String(value)) || typeof value !== 'string'
          : control.kind === 'bool' ? typeof value !== 'boolean'
          : typeof value !== 'number' || !Number.isInteger(value) || (control.kind === 'enum' ? !control.options?.some(o => o.value === value) : value < (control.min ?? 0) || value > (control.max ?? 2147483647) || (value - (control.min ?? 0)) % (control.step ?? 1) !== 0)) throw invalid()
        if (control.kind !== 'gesture') this.event(runtime, 'input', `${control.name}${control.target === 'sensor' ? '注入' : '设置'} ${String(value)}${control.unit ?? ''}`)
        const before = runtime.session.events.at(-1)?.seq ?? 0
        await this.send(runtime, 3, runtime.adapter.inputCodes[action.key], control.kind === 'gesture' ? Number(value === 'long') : Number(value))
        if (control.kind === 'gesture' && !runtime.session.events.some(e => e.seq > before && e.kind === 'input')) this.event(runtime, 'input', `${control.name}${value === 'long' ? '长按' : '短按'}输入，未被面板接受`)
      } else throw invalid()
    })
    return this.get(id)
  }
  stop(id: string, message = '仿真已停止') {
    const runtime = this.runtime(id)
    if (runtime.session.status === 'stopped') return
    runtime.session.status = 'stopped'; runtime.session.running = false
    this.event(runtime, 'system', message)
    if (runtime.pending) { clearTimeout(runtime.pending.timer); runtime.pending.reject(failure(409, message)); runtime.pending = undefined }
    runtime.child?.kill(); runtime.compiler?.kill()
  }
  async close() {
    this.closed = true; clearInterval(this.timer)
    for (const runtime of this.runtimes.values()) this.stop(runtime.session.id, '服务停止，仿真已结束')
    await Promise.allSettled([...this.runtimes.values()].flatMap(r => [r.task, r.chain, r.exit]))
  }
}
