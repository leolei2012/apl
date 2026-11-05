import { c51InstanceBytes, c51MemoryLayout, type FirmwareBuild } from './firmware'
import { memorySettings } from './memory-settings'
export { defaultMemorySettings, validMemorySettings, memorySettings } from './memory-settings'
export type { MemorySettings } from './memory-settings'
export type MemoryStatus = 'unknown' | 'over' | 'tight' | 'within-budget'
export interface MemoryEstimate {
  perInstanceBytes: number | null
  basis: 'c51-reference' | 'manual' | 'unknown'
  coreBytes: number | null
  hmiBytes: number | null
  schedulerBytes: number | null
  aplBytes: number | null
  hostStaticBytes: number | null
  stackBytes: number
  safetyBytes: number
  knownBytes: number
  totalBytes: number | null
  remainingBytes: number | null
  knownRemainingBytes: number
  status: MemoryStatus
  missing: string[]
  flash: { budgetBytes: number; usedBytes: number | null; remainingBytes: number | null; status: MemoryStatus }
}

function capacityStatus(budgetBytes: number, knownBytes: number, completeBytes: number | null): MemoryStatus {
  if (knownBytes > budgetBytes) return 'over'
  if (completeBytes === null) return 'unknown'
  return budgetBytes - completeBytes <= budgetBytes * 0.1 ? 'tight' : 'within-budget'
}

/** Budget model only: neither a whole-program linker report nor stack analysis. */
export function estimateMemory(build: FirmwareBuild): MemoryEstimate {
  const settings = memorySettings(build)
  const c51 = build.target === 'c51'
  const layout = c51 ? c51MemoryLayout(build) : null
  const coreBytes = layout?.coreBytes ?? null, hmiBytes = layout?.hmiBytes ?? null, schedulerBytes = layout?.schedulerBytes ?? null
  const perInstanceBytes = c51 ? c51InstanceBytes(build) : settings.instanceBytes
  const basis = c51 ? 'c51-reference' : perInstanceBytes === null ? 'unknown' : 'manual'
  const aplBytes = perInstanceBytes === null ? null : perInstanceBytes * settings.instanceCount
  const knownBytes = (aplBytes ?? 0) + (settings.hostStaticBytes ?? 0) + build.stackBytes + settings.safetyBytes
  const totalBytes = aplBytes === null || settings.hostStaticBytes === null ? null : knownBytes
  const flashBudgetBytes = build.flashKb * 1024
  const missing: string[] = []
  if (aplBytes === null) missing.push('APL 单实例大小')
  if (settings.hostStaticBytes === null) missing.push('宿主及其他数据')
  return {
    perInstanceBytes, basis, coreBytes, hmiBytes, schedulerBytes, aplBytes,
    hostStaticBytes: settings.hostStaticBytes, stackBytes: build.stackBytes, safetyBytes: settings.safetyBytes,
    knownBytes, totalBytes, remainingBytes: totalBytes === null ? null : build.ramBytes - totalBytes,
    knownRemainingBytes: build.ramBytes - knownBytes, status: capacityStatus(build.ramBytes, knownBytes, totalBytes), missing,
    flash: { budgetBytes: flashBudgetBytes, usedBytes: settings.flashUsedBytes,
      remainingBytes: settings.flashUsedBytes === null ? null : flashBudgetBytes - settings.flashUsedBytes,
      status: capacityStatus(flashBudgetBytes, settings.flashUsedBytes ?? 0, settings.flashUsedBytes) },
  }
}
