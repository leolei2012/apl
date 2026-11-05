import type { FirmwareBuild } from './firmware'

const MAX_BYTES = 1073741824
export interface MemorySettings {
  instanceCount: number
  hostStaticBytes: number | null
  safetyBytes: number
  instanceBytes: number | null
  flashUsedBytes: number | null
}

/** Unknown occupancy stays null; a user-entered zero is an explicit estimate. */
export function defaultMemorySettings(): MemorySettings {
  return { instanceCount: 1, hostStaticBytes: null, safetyBytes: 64, instanceBytes: null, flashUsedBytes: null }
}
const integerBytes = (value: unknown, min = 0): value is number => Number.isSafeInteger(value) && Number(value) >= min && Number(value) <= MAX_BYTES
export function validMemorySettings(value: unknown): value is MemorySettings {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false
  const s = value as MemorySettings
  const keys = ['instanceCount', 'hostStaticBytes', 'safetyBytes', 'instanceBytes', 'flashUsedBytes']
  return Object.keys(s).length === keys.length && Object.keys(s).every(key => keys.includes(key))
    && Number.isSafeInteger(s.instanceCount) && s.instanceCount >= 1 && s.instanceCount <= 64
    && integerBytes(s.safetyBytes)
    && (s.hostStaticBytes === null || integerBytes(s.hostStaticBytes))
    && (s.instanceBytes === null || integerBytes(s.instanceBytes, 1))
    && (s.flashUsedBytes === null || integerBytes(s.flashUsedBytes))
}
export function memorySettings(build: FirmwareBuild): MemorySettings {
  // Older saved build configurations have no memory settings. The optional
  // extension is validated by the firmware parser before persistent use.
  const value = build.memory
  return validMemorySettings(value) ? { ...value } : defaultMemorySettings()
}
