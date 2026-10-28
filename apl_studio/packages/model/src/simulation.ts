import type { Value } from './index'

export interface SimulationControl {
  key: string; name: string; kind: 'gesture' | 'bool' | 'int' | 'enum'; target: 'key' | 'sensor' | 'value'
  unit?: string; min?: number; max?: number; step?: number; default: Value
  options?: { value: number; label: string }[]; position?: string
}
export interface SimulationSlot { key: string; name: string; unit?: string; options?: { value: number; label: string }[]; boolean?: boolean }
export interface SimulationDescriptor {
  id: string; name: string; tickMs: number; panel: 'round' | 'controls'; memory: boolean
  controls: SimulationControl[]; outputs: SimulationSlot[]; internal: SimulationSlot[]; display: SimulationSlot[]
}
export interface SimulationSnapshot {
  powered: boolean; ms: number
  values: Record<string, Value>; outputs: Record<string, Value>; display: Record<string, Value>; internal: Record<string, Value>; memory: Record<string, Value>
}
export interface SimulationEvent { seq: number; ms: number; kind: 'input' | 'output' | 'state' | 'system' | 'memory'; message: string }
export interface SimulationSession {
  id: string; productId: string; revision: number; template: { id: string; version: string; fingerprint: string }
  driverFingerprint: string; status: 'building' | 'ready' | 'failed' | 'stopped'; running: boolean
  descriptor: SimulationDescriptor; snapshot: SimulationSnapshot | null; events: SimulationEvent[]; diagnostics: string; createdAt: string
}
export type SimulationAction = { type: 'running'; value: boolean } | { type: 'supply'; on: boolean } | { type: 'reset' }
  | { type: 'advance'; ms: number } | { type: 'input'; key: string; value: Value }
