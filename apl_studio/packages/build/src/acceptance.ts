import { advancedAcceptance } from './advanced-acceptance'
import { standardAcceptance } from './standard-acceptance'
import { standardDeliverables } from './standard-deliverables'
import { createHash, randomUUID } from 'node:crypto'
import { mkdirSync, readFileSync, readdirSync, renameSync, writeFileSync } from 'node:fs'
import { resolve } from 'node:path'
import type { Product, Value } from '@apl-studio/model'
import type { AcceptanceReport } from '@apl-studio/model/template'
import type { SimulationSession, SimulationSnapshot } from '@apl-studio/model/simulation'
import { safeRelative, type TemplateProvider, type TemplateRegistry } from '@apl-studio/templates'
import { SimulationService } from './simulation'
import { simulationAdapters, simulationSource } from './simulation-adapters'

const hash = (value: string) => createHash('sha256').update(value).digest('hex')
const failure = (message: string, code = 'ACCEPTANCE_REQUIRED') => Object.assign(new Error(message), { statusCode: 409, code })
const uuid = /^[a-f0-9-]{36}$/i
const scope = 'PM2.5自动档边界、Auto/sleep与面板语义、风速键长按极速开关、短按循环；不包含目标硬件验收'
const digest = (report: AcceptanceReport) => hash(JSON.stringify({ ...report, fingerprint: undefined }))
export function generatedFingerprint(files: Map<string, Uint8Array>) {
  return hash(JSON.stringify(Object.fromEntries([...files].sort(([a], [b]) => a < b ? -1 : 1).map(([path, bytes]) => [path, createHash('sha256').update(bytes).digest('hex')]))))
}
function driverFingerprint(product: Product, provider: TemplateProvider) {
  const adapter = simulationAdapters.find(a => a.supports(provider.descriptor))
  if (!adapter) throw failure('当前方案没有真实C验收桥接')
  return hash(simulationSource(adapter, product.definition))
}

/** Only the server runs/records acceptance. Client requests never contain results. */
export class AcceptanceService {
  private reports = new Map<string, AcceptanceReport>()
  private tasks = new Set<Promise<void>>()
  constructor(private directory: string, private registry: TemplateRegistry, private simulations: SimulationService) {
    mkdirSync(directory, { recursive: true })
    for (const file of readdirSync(directory)) {
      if (!/^[a-f0-9-]{36}\.json$/i.test(file)) continue
      try {
        const report = JSON.parse(readFileSync(resolve(directory, file), 'utf8')) as AcceptanceReport
        if (report.id + '.json' !== file || !uuid.test(report.productId) || !Array.isArray(report.checks)) continue
        if (report.status === 'running') { report.status = 'failed'; report.diagnostics = '服务重启中断了验收，请重新运行' }
        if (report.status === 'passed' && (report.fingerprint !== digest(report) || !report.checks.length || report.checks.some(c => !c.passed))) continue
        this.reports.set(report.id, report)
      } catch { /* Incomplete or invalid records are never successful acceptance. */ }
    }
  }
  private provider(product: Product) {
    const reference = product.definition.template
    const provider = reference && this.registry.get(reference.id, reference.version)
    if (!provider || !provider.descriptor.workflow) throw failure('此方案尚未提供产品经理场景验收', 'ACCEPTANCE_UNSUPPORTED')
    const errors = provider.validate(product.definition)
    if (errors.length) throw failure(errors[0], 'INVALID_CONFIGURATION')
    return provider
  }
  private persist(report: AcceptanceReport) {
    const target = resolve(this.directory, report.id + '.json')
    writeFileSync(target + '.tmp', JSON.stringify(report, null, 2))
    renameSync(target + '.tmp', target)
  }
  latest(product: Product): { report: AcceptanceReport | null; current: boolean } {
    const report = [...this.reports.values()].filter(r => r.productId === product.id).sort((a, b) => b.createdAt.localeCompare(a.createdAt))[0] ?? null
    let current = false
    if (report) {
      try {
        const provider = this.provider(product)
        current = (report.status !== 'passed' || !!report.sourceFingerprint) && report.revision === product.revision && report.definitionFingerprint === hash(JSON.stringify(product.definition))
          && report.template.fingerprint === provider.descriptor.fingerprint && report.template.id === provider.descriptor.id
          && report.template.version === provider.descriptor.version && report.sourceVersion === provider.descriptor.sourceVersion
          && report.driverFingerprint === driverFingerprint(product, provider)
      } catch { /* Changed/unavailable template or driver expires acceptance. */ }
    }
    return { report: report ? structuredClone(report) : null, current }
  }
  run(product: Product): AcceptanceReport {
    const provider = this.provider(product), previous = this.latest(product)
    if (previous.current && previous.report?.status === 'running') return previous.report
    if ([...this.reports.values()].some(r => r.status === 'running')) throw failure('已有场景验收正在运行，请稍后重试', 'ACCEPTANCE_BUSY')
    const report: AcceptanceReport = { id: randomUUID(), productId: product.id, revision: product.revision,
      definitionFingerprint: hash(JSON.stringify(product.definition)), template: { id: provider.descriptor.id, version: provider.descriptor.version, fingerprint: provider.descriptor.fingerprint },
      sourceVersion: provider.descriptor.sourceVersion, sourceFingerprint: '', driverFingerprint: driverFingerprint(product, provider), status: 'running', createdAt: new Date().toISOString(), scope: provider.descriptor.workflow === 'air-care-standard-v2' ? '定时连增、睡眠独立计时、品类滤网维护、独立模块与输入状态契约；未含实机、全部Excel和未决规则' : provider.descriptor.workflow === 'air-care-standard-v1' ? '三能力组合的标准面板、Auto/睡眠、Turbo、缺水/风干、维护与UV联锁场景；未含实机、全部规格及未决规则' : scope, checks: [], diagnostics: '' }
    this.reports.set(report.id, report); this.persist(report)
    const task = this.execute(structuredClone(product), report).finally(() => this.tasks.delete(task))
    this.tasks.add(task)
    return structuredClone(report)
  }
  require(product: Product): AcceptanceReport {
    const { report, current } = this.latest(product)
    if (!report || !current || report.status !== 'passed' || report.fingerprint !== digest(report) || !report.checks.length || report.checks.some(c => !c.passed)) {
      throw failure('请先运行当前产品修订的真实C场景验收，全部通过后再生成交付包')
    }
    return report
  }
  requireSource(report: AcceptanceReport, files: Map<string, Uint8Array>) {
    if (report.sourceFingerprint !== generatedFingerprint(files)) throw failure('生成代码与已验收代码不一致，请重新运行场景验收', 'ACCEPTANCE_SOURCE_CHANGED')
  }
  private async execute(product: Product, report: AcceptanceReport) {
    let session: SimulationSession | undefined
    try {
      session = await this.simulations.create(product, false)
      const deadline = Date.now() + 65000
      while (session.status === 'building' && Date.now() < deadline) {
        await new Promise(accept => setTimeout(accept, 30))
        session = this.simulations.get(session.id)
      }
      if (session.status !== 'ready' || session.driverFingerprint !== report.driverFingerprint) throw new Error(session.diagnostics || '真实C验收启动失败或桥接指纹不一致')
      const generated = await this.provider(product).generate(product.definition)
      const executed = new Map([...generated.keys()].map(path => [path, readFileSync(resolve(this.simulations.directory, session!.id, 'source', safeRelative(path)))]))
      report.sourceFingerprint = generatedFingerprint(executed)
      this.requireSource(report, generated)
      const act = async (action: Parameters<SimulationService['action']>[1]) => {
        session = await this.simulations.action(session!.id, action)
        return session.snapshot!
      }
      const check = (name: string, expected: Record<string, Value>, actual: Record<string, Value>, s: SimulationSnapshot) => {
        const passed = Object.entries(expected).every(([key, value]) => actual[key] === value)
        report.checks.push({ name, expected, actual, passed, ms: s.ms }); this.persist(report)
        if (!passed) throw new Error('场景未通过：' + name)
      }
      if (this.provider(product).descriptor.workflow === 'air-care-standard-v2') {
        await advancedAcceptance(product, act, check); report.status = 'passed'; return
      }
      if (this.provider(product).descriptor.workflow === 'air-care-standard-v1') {
        await standardAcceptance(product, act, check); report.status = 'passed'; return
      }
      let s = await act({ type: 'advance', ms: 2100 })
      if (s.internal.run_state !== 1) s = await act({ type: 'input', key: 'Power', value: 'short' })
      // Select modes through the existing C HMI, rather than injecting business commands.
      const select = async (gear: number) => {
        for (let i = 0; i < 10 && s.internal.fan_speed !== gear; i++) s = await act({ type: 'input', key: 'Speed', value: 'short' })
        if (s.internal.fan_speed !== gear) throw new Error('风速键未能进入预期模式')
      }
      await select(4)
      for (const [pm, output] of [[35, 0], [36, 1], [75, 1], [76, 2], [150, 2], [151, 3], [35, 0]]) {
        await act({ type: 'input', key: 'pm25', value: pm })
        s = await act({ type: 'advance', ms: 1000 })
        check(`PM2.5=${pm}自动调速`, { pm, output, requested: 4, sleep: 0, autoLamp: 255, sleepLamp: 0, fan: 1 },
          { pm: s.internal.pm25, output: s.outputs.fan_gear, requested: s.internal.fan_speed, sleep: s.internal.sleep, autoLamp: s.display.icon_12, sleepLamp: s.display.icon_8, fan: s.outputs.fan }, s)
      }
      s = await act({ type: 'advance', ms: 4000 })
      check('Auto最低风速不进入睡眠熄屏', { brightness: 255, sleep: 0 }, { brightness: s.display.brightness, sleep: s.internal.sleep }, s)
      await select(1)
      s = await act({ type: 'input', key: 'Speed', value: 'long' })
      const turbo = product.definition.parameters.find(p => p.key === 'turbo_enabled')!.value === true
      check(turbo ? '长按3秒进入极速' : '关闭极速后长按不改变档位', { requested: turbo ? 5 : 1, output: turbo ? 5 : 1 }, { requested: s.internal.fan_speed, output: s.outputs.fan_gear }, s)
      s = await act({ type: 'input', key: 'Speed', value: 'short' })
      check('长按后短按仍按标准循环到二档', { requested: 2, output: 2 }, { requested: s.internal.fan_speed, output: s.outputs.fan_gear }, s)
      report.status = 'passed'
    } catch (error) { report.status = 'failed'; report.diagnostics = (error as Error).message }
    finally {
      if (session) this.simulations.stop(session.id)
      report.finishedAt = new Date().toISOString(); report.fingerprint = digest(report); this.persist(report)
    }
  }
  async close() { await Promise.allSettled(this.tasks) }
  deliverables(product: Product, provider: TemplateProvider, report: AcceptanceReport): Map<string, Uint8Array> {
    if (['air-care-standard-v1','air-care-standard-v2'].includes(provider.descriptor.workflow ?? '')) return standardDeliverables(product, provider.descriptor, report)
    const turbo = product.definition.parameters.find(p => p.key === 'turbo_enabled')?.value === true
    const gear = product.definition.functions.find(f => f.key === 'fan_speed')?.default
    const gears = ['睡眠', '一档', '二档', '三档', '自动', '极速']
    const title = product.definition.name.replace(/[\r\n]/g, ' ')
    const technical = `产品修订r${product.revision}；模板${report.template.id}@${report.template.version}；源码${report.sourceVersion}；模板指纹${report.template.fingerprint}；生成代码指纹${report.sourceFingerprint}；驱动指纹${report.driverFingerprint}；验收${report.id}`
    const markdown = `# ${title} 功能与交互说明\n\n${technical}\n\n- 标准能力：空气质量自动调速、手动/睡眠/极速、定时和灯光；现有UV/负离子复合接口随模板保留，暂不独立选配；不含加湿。\n- 开机档位：${gears[Number(gear)]}。\n- 风速键短按：睡眠→一→二→三→自动循环；长按3秒：${turbo ? '进入/退出极速' : '无档位切换动作'}。\n- 自动调速：PM2.5≤35使用睡眠风速，36～75一档，76～150二档，>150三档；请求保持自动，不进入睡眠模式或熄屏。\n- 滤芯寿命：${product.definition.parameters.find(p => p.key === 'filter_life_h')?.value}小时。\n\n显示与其余按键采用模板标准实现；未提供自由面板排版、任意动作编程或全功能验收。\n`
    const results = `# 真实C场景验收\n\n${technical}\n\n状态：${report.status === 'passed' ? '通过' : '未通过'}\n\n范围：${report.scope}\n\n${report.checks.map(c => `- ${c.passed ? '通过' : '失败'}：${c.name}；预期${JSON.stringify(c.expected)}；实际${JSON.stringify(c.actual)}`).join('\n')}\n`
    const hardware = `# 工程师硬件适配清单\n\n${technical}\n\n将src全部C文件加入工程，包含目录加入include/config/port；宿主持有对象，每10ms推进，按字段序列化持久数据。port/apl_port.c为占位实现，不代表硬件已连接。请在自己的应用目录实现并核验：\n\n${provider.descriptor.ports.filter(p => !p.requires?.some(k => !product.definition.functions.some(f => f.key === k))).map(p => `- [ ] ${p.name}（${p.required ? '必需' : '可选'}）：${p.signature}；${p.description}`).join('\n')}\n\n- [ ] 按实际风机/PWM标定各档位；单独核验UV安全联锁及负离子接线。\n- [ ] 按当前MCU验证资源、周期、并发/中断约束和掉电存储。\n- [ ] 运行verification消费程序，复核产品配置及acceptance.json；进行目标硬件验收。\n\n业务规则由库处理，Web、硬件适配层与产品应用不要再实现一份调速规则。\n`
    return new Map([['docs/product-spec.md', markdown], ['docs/acceptance-report.md', results], ['docs/hardware-checklist.md', hardware], ['verification/acceptance.json', JSON.stringify(report, null, 2) + '\n']].map(([path, value]) => [path, Buffer.from(value)]))
  }
}
