import { createHash } from 'node:crypto'
import { zipSync, strToU8 } from 'fflate'
import { parseDefinition, interfaceHeader, STUDIO_VERSION, accessNames, kindNames, type Product } from '@apl-studio/model'

export function definitionPackage(product: Product): { bytes: Uint8Array; filename: string; fingerprint: string } {
  const d = parseDefinition(product.definition)
  const files: Record<string, Uint8Array> = {}
  const json = (value: unknown) => strToU8(JSON.stringify(value, null, 2) + '\n')
  files['product.apl.json'] = json(d)
  files['include/apl_product_contract.h'] = strToU8(interfaceHeader(d))
  files['parameters.json'] = json(Object.fromEntries(d.parameters.map(p => [p.key, p.value])))
  const cell = (value: string) => value.replace(/\|/g, '\\|').replace(/[\r\n]/g, ' ')
  files['README.md'] = strToU8(`# ${cell(d.name)} · 产品定义包\n\n这是 APL Studio 导出的产品定义与接口契约，不是完整 APL SDK。尚未包含业务实现或硬件驱动，头文件仅声明接口。\n\n产品标识：${d.code}；SDK 目标版本：${d.sdkVersion}；产品修订：${product.revision}。\n\n## 文件\n\n- product.apl.json：版本化产品定义，可导入 APL Studio。\n- include/apl_product_contract.h：getter / setter 接口契约预览。\n- parameters.json：构建参数。\n- manifest.json：版本和文件 SHA-256 校验。\n\n## 功能\n\n| 标识 | 名称 | 类型 | 访问 |\n|---|---|---|---|\n${d.functions.map(f => `| ${f.key} | ${cell(f.name)} | ${kindNames[f.kind]} | ${accessNames[f.access]} |`).join('\n')}\n\n## 后续接入\n\nAPL 源码模板作为独立部分，由模板提供方消费这份定义、验证能力边界并生成 SDK。工程师的硬件移植实现需与生成目录分离维护。\n`)
  const hashes = Object.fromEntries(Object.entries(files).sort(([a], [b]) => a < b ? -1 : a > b ? 1 : 0).map(([path, content]) => [path, createHash('sha256').update(content).digest('hex')]))
  const fingerprint = createHash('sha256').update(JSON.stringify(hashes)).digest('hex')
  files['manifest.json'] = json({ format: 'apl-product-definition', schemaVersion: 1, studioVersion: STUDIO_VERSION, productId: product.id, revision: product.revision, sdkVersion: d.sdkVersion, fingerprint, files: hashes, sdkReady: false })
  // Fixed paths and metadata make archive content reproducible for a saved revision.
  const archive: Record<string, [Uint8Array, { mtime: Date }]> = {}
  for (const [path, content] of Object.entries(files)) archive[`apl-definition/${path}`] = [content, { mtime: new Date(1980, 0, 1) }]
  return { bytes: zipSync(archive), filename: `${d.code}-definition-r${product.revision}.zip`, fingerprint }
}
