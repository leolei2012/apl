import type { Definition, Product, Revision } from '@apl-studio/model'
import type { BuildJob, TemplateDescriptor } from '@apl-studio/model/template'
import type { SimulationAction, SimulationDescriptor, SimulationSession } from '@apl-studio/model/simulation'
export class ApiError extends Error { constructor(message: string, public status: number, public code?: string) { super(message) } }
async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch('/api' + path, { ...options, headers: { ...(options?.body !== undefined ? { 'Content-Type': 'application/json' } : {}), ...options?.headers } })
  if (!response.ok) {
    const body = await response.json().catch(() => ({ error: '服务暂时无法连接' }))
    throw new ApiError(body.error ?? '操作未能完成', response.status, body.code)
  }
  return response.status === 204 ? undefined as T : response.json()
}
export const api = {
  simulationInfo: (productId: string) => request<{ descriptor: SimulationDescriptor | null }>('/products/' + productId + '/simulation'),
  startSimulation: (product: Product) => request<SimulationSession>('/simulation/sessions', { method: 'POST', body: JSON.stringify({ productId: product.id, revision: product.revision }) }),
  simulation: (id: string) => request<SimulationSession>('/simulation/sessions/' + id),
  simulate: (id: string, action: SimulationAction) => request<SimulationSession>('/simulation/sessions/' + id + '/actions', { method: 'POST', body: JSON.stringify(action) }),
  stopSimulation: (id: string) => request<void>('/simulation/sessions/' + id, { method: 'DELETE' }),
  templates: () => request<{ templates: TemplateDescriptor[]; status: string }>('/templates'),
  previewTemplate: (definition: Definition) => request<{ files: Record<string, string> }>('/templates/preview', { method: 'POST', body: JSON.stringify({ definition }) }),
  generate: (product: Product) => request<BuildJob>('/sdk/generate', { method: 'POST', body: JSON.stringify({ productId: product.id, revision: product.revision }) }),
  jobs: (id: string) => request<BuildJob[]>('/products/' + id + '/sdk/jobs'),
  list: () => request<Product[]>('/products'),
  get: (id: string) => request<Product>('/products/' + id),
  create: (definition: Definition) => request<Product>('/products', { method: 'POST', body: JSON.stringify(definition) }),
  update: (product: Product, definition: Definition) => request<Product>('/products/' + product.id, { method: 'PUT', body: JSON.stringify({ revision: product.revision, definition }) }),
  remove: (product: Product) => request<void>('/products/' + product.id, { method: 'DELETE', body: JSON.stringify({ revision: product.revision }) }),
  revisions: (id: string) => request<Revision[]>('/products/' + id + '/revisions'),
}
export async function download(url: string, fallback: string) {
  const response = await fetch(url)
  if (!response.ok) { const body = await response.json(); throw new Error(body.error ?? '导出失败') }
  const filename = response.headers.get('content-disposition')?.match(/filename="([^"]+)"/)?.[1] ?? fallback
  downloadBlob(await response.blob(), filename)
}
export function downloadBlob(blob: Blob, filename: string) {
  const url = URL.createObjectURL(blob), anchor = document.createElement('a')
  anchor.href = url; anchor.download = filename; document.body.append(anchor); anchor.click(); anchor.remove()
  setTimeout(() => URL.revokeObjectURL(url), 1000)
}
