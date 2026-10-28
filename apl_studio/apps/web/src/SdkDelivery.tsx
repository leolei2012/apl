import { useEffect, useState } from 'react'
import { Download, FileCode2, Boxes, CheckCircle2, CircleDashed, Loader2, Info, RefreshCw } from 'lucide-react'
import type { Definition, Product } from '@apl-studio/model'
import { validateTemplateConfiguration, type BuildJob, type TemplateDescriptor } from '@apl-studio/model/template'
import { api, download } from './api'
import { Select } from './components'

export function SdkDelivery({ product, definition, template, dirty, onSave, onExport }: { product: Product; definition: Definition; template: TemplateDescriptor; dirty: boolean; onSave: () => void; onExport: (json?: boolean) => void }) {
  const [jobs, setJobs] = useState<BuildJob[]>([]), [pending, setPending] = useState(false), [error, setError] = useState('')
  const [files, setFiles] = useState<Record<string, string>>({}), [file, setFile] = useState('include/apl_product.h')
  const previewPaths = template.previewFiles ?? ['include/apl_product.h', 'config/apl_core_config.h', 'config/apl_hmi_config.h', 'README.md']
  const selectedFile = previewPaths.includes(file) ? file : previewPaths[0]
  const issues = validateTemplateConfiguration(definition, template)
  const signature = JSON.stringify(definition)
  useEffect(() => {
    let stopped = false, timer: ReturnType<typeof setTimeout> | undefined
    const refresh = async () => {
      try {
        const all = await api.jobs(product.id)
        if (stopped) return
        setJobs(all)
        timer = setTimeout(refresh, all.some(job => job.status === 'queued' || job.status === 'building') ? 700 : 4000)
      } catch (error) { if (!stopped) { setError((error as Error).message); timer = setTimeout(refresh, 3000) } }
    }
    void refresh()
    return () => { stopped = true; clearTimeout(timer) }
  }, [product.id, product.revision])
  useEffect(() => {
    let active = true
    setFiles({})
    if (issues.length) return
    const timer = setTimeout(() => {
      api.previewTemplate(definition).then(result => { if (active) setFiles(result.files) }).catch(error => { if (active) { setFiles({}); setError((error as Error).message) } })
    }, 250)
    return () => { active = false; clearTimeout(timer) }
  }, [signature, template.fingerprint])
  const current = jobs.find(job => job.revision === product.revision && job.template.id === template.id && job.template.version === template.version && job.template.fingerprint === template.fingerprint)
  const active = pending || current?.status === 'queued' || current?.status === 'building'
  async function generate() {
    setPending(true); setError('')
    try { const job = await api.generate(product); setJobs(old => [job, ...old.filter(j => j.id !== job.id)]) }
    catch (error) { setError((error as Error).message) }
    finally { setPending(false) }
  }
  async function downloadSdk(job: BuildJob) {
    try { await download(`/api/sdk/jobs/${job.id}/download`, job.filename ?? 'apl-sdk.zip') }
    catch (error) { setError((error as Error).message) }
  }
  return <><div className="delivery-summary"><div className="delivery-icon"><FileCode2 size={27}/></div><div><h2>生成产品 SDK</h2><p>{template.name} v{template.version} · 源码 {template.sourceVersion} · 产品修订 r{product.revision}</p></div><button className="button primary" disabled={dirty || !!issues.length || !!active} onClick={() => void generate()}>{active ? <Loader2 className="spin" size={16}/> : <Boxes size={16}/>} {active ? '正在生成与验证' : current?.status === 'succeeded' ? '重新生成并验证' : '生成并验证 SDK'}</button></div>
    {dirty && <div className="inline-alert"><Info size={16}/><span>先保存当前功能与参数。每个 SDK 固定使用一份已保存修订和模板源码快照。</span><button className="text-button" onClick={onSave} disabled={!!issues.length}>保存更改</button></div>}
    {(issues.length > 0 || error) && <div className="inline-alert warning" role="alert"><Info size={16}/><span>{issues[0] ?? error}</span></div>}
    <div className="sdk-checks"><div><span className="stat-icon">{current?.checks.compile === 'passed' ? <CheckCircle2 size={18}/> : <CircleDashed size={18}/>}</span><span><strong>C11 编译检查</strong><small>{current?.checks.compile === 'passed' ? '真实源码、产品接口与移植示例已编译' : current?.checks.compile === 'failed' ? '编译未通过，请查看诊断' : '等待生成后执行'}</small></span></div><div><span className="stat-icon">{current?.checks.consumer === 'passed' ? <CheckCircle2 size={18}/> : <CircleDashed size={18}/>}</span><span><strong>模板消费验证</strong><small>{current?.checks.consumer === 'passed' ? current.verificationScope ?? template.verification?.scope ?? '模板基础消费验证通过' : current?.checks.consumer === 'failed' ? '消费验证未通过' : '执行实际编译的本机 C 程序'}</small></span></div></div>
    <div className="delivery-grid"><section className="card code-card"><div className="section-heading"><div><h2>实际生成文件预览</h2><p>{dirty ? '当前草稿' : `修订 r${product.revision}`} · 与 SDK 使用同一生成器</p></div></div><div className="native-file-select"><Select label="生成文件" value={selectedFile} onChange={setFile}>{previewPaths.map(path => <option key={path} value={path}>{path}</option>)}</Select></div><pre className="code-preview"><code>{files[selectedFile] ?? (issues.length ? '请先完成有效配置。' : '正在生成文件预览…')}</code></pre></section><div className="stack"><section className="card"><h2>当前交付状态</h2><div className="package-meta"><span>模板版本</span><strong>{template.id}@{template.version}</strong><span>模板指纹</span><strong>{template.fingerprint.slice(0, 12)}</strong><span>目标 SDK</span><strong>v{definition.sdkVersion}</strong><span>生成任务</span><strong>{current?.phase ?? '尚未生成'}</strong></div>
      {current?.status === 'succeeded' && !dirty ? <button className="button primary full" onClick={() => void downloadSdk(current)}><Download size={16}/>下载已验证 SDK</button> : <button className="button full" disabled><Download size={16}/>验证通过后可下载 SDK</button>}
      {current && <details className="build-diagnostics"><summary>构建诊断与验证范围</summary><p>验证使用本机 C11 编译器，覆盖基础消费流程；不等于目标 MCU 或真实硬件验收。</p><pre>{current.compilerVersion}{'\n'}{current.diagnostics || current.phase}</pre></details>}
      <button className="text-button definition-backup" disabled={dirty} onClick={() => onExport(true)}>另存产品 JSON</button></section>
      <section className="note-card"><Info size={18}/><div><strong>工程师仍需完成硬件移植</strong><p>包内包含产品源码库、产品适配代码、配置、port 占位模板、示例、验证程序和 manifest。请在自己的应用目录维护硬件实现。</p></div></section></div></div>
    {!!jobs.length && <section className="card build-history"><div className="section-heading"><h2>交付记录</h2><button className="text-button" onClick={() => api.jobs(product.id).then(setJobs).catch(error => setError(error.message))}><RefreshCw size={13}/>刷新</button></div>{jobs.map(job => <div className="build-row" key={job.id}><span className={'badge ' + (job.status === 'succeeded' ? 'purple' : '')}>{job.status === 'succeeded' ? '已验证' : job.status === 'failed' ? '未通过' : job.status === 'cancelled' ? '已停止' : '生成中'}</span><div><strong>r{job.revision} · SDK {job.sdkVersion}</strong><small>{job.template.id}@{job.template.version} · {new Date(job.createdAt).toLocaleString('zh-CN')}</small></div><span className="subtle">{job.phase}</span>{job.status === 'succeeded' && <button className="button small" onClick={() => void downloadSdk(job)}>下载 r{job.revision}</button>}{job.status === 'failed' && <details className="history-error"><summary>查看诊断</summary><pre>{job.diagnostics}</pre></details>}</div>)}</section>}
  </>
}
