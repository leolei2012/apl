import type { Product } from '@apl-studio/model'
import { firmwareBuild } from '@apl-studio/model/firmware'
import { estimateMemory, memorySettings } from '@apl-studio/model/memory'

/** Deterministic budget record for a saved revision, never a target acceptance. */
export function memoryEstimateReport(product: Product, sourceFingerprint: string) {
  const build = firmwareBuild(product.definition)
  return {
    format: 'apl-memory-estimate', version: 1, model: build.target === 'c51' ? 'apl-c51-large-profile-v6-keymap' : 'manual-target-sizeof',
    productId: product.id, productCode: product.definition.code, revision: product.revision, sourceFingerprint,
    target: build.target, chip: build.chip, ramBudgetBytes: build.ramBytes, flashBudgetBytes: build.flashKb * 1024,
    defines: build.defines, settings: memorySettings(build), estimate: estimateMemory(build),
    targetCompiled: false, targetCapacityVerified: false, stackVerified: false,
    notes: [
      'C51实例按当前头文件预留公式估算；通用平台sizeof和Flash占用由用户手工提供，未核验来源或新鲜度。',
      '其他数据未填写时保持未知，不按0补成完整合计；安全余量是预算预留，不是已使用RAM。',
      'APL配置副本及HMI快照已包含在实例中；额外副本、驱动、缓冲区、回调表及共享数据需另外计入。',
      'C51按同一XDATA预算池估算，IRAM、硬件栈和ISR保存需另行检查；最终以完整链接报告及栈验证为准。',
    ],
  }
}
