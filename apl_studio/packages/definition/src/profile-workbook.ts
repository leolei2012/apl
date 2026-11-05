import { unzipSync, zipSync, strFromU8, strToU8 } from 'fflate'
import type { Product } from '@apl-studio/model'
import { profilePid, profileValues, validateProfile, type ProfileCatalog } from '@apl-studio/model/profile'
import { profileKeyMapping, keyMappingActions } from '@apl-studio/model/key-mapping'

const xmlText = (s: string) => s.replaceAll('&', '&amp;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('"', '&quot;').replaceAll("'", '&apos;')
/** Fill the version-owned Excel template; do not infer device addresses from C layout. */
export function profileWorkbook(template: Uint8Array, product: Product, catalog: ProfileCatalog): Uint8Array {
  const errors = validateProfile(product.definition, catalog)
  if (errors.length) throw new Error(errors.join('；'))
  const files = unzipSync(template), values = profileValues(product.definition)
  function fill(file: string, cells: Record<string, string | number>) {
    if (!files[file]) throw new Error('参数表模板缺少工作表')
    let xml = strFromU8(files[file])
    for (const [address, value] of Object.entries(cells)) {
      const pattern = new RegExp('<(?:[A-Za-z_][\\w.-]*:)?c\\b[^>]*\\br="' + address + '"[^>]*?(?:/>|>[\\s\\S]*?</(?:[A-Za-z_][\\w.-]*:)?c>)')
      if (!pattern.test(xml)) throw new Error('参数表模板缺少单元格 ' + address)
      xml = xml.replace(pattern, old => {
        const parts = old.match(/^<((?:[A-Za-z_][\w.-]*:)?)(?:c)\b([^>]*)/)!
        const prefix = parts[1]
        const attrs = parts[2].replace(/\/$/, '').replace(/\s+t="[^"]*"/g, '')
        return typeof value === 'number' ? `<${prefix}c${attrs}><${prefix}v>${value}</${prefix}v></${prefix}c>` : `<${prefix}c${attrs} t="inlineStr"><${prefix}is><${prefix}t xml:space="preserve">${xmlText(value)}</${prefix}t></${prefix}is></${prefix}c>`
      })
    }
    files[file] = strToU8(xml)
  }
  fill('xl/worksheets/sheet1.xml', Object.fromEntries(catalog.schema.parameters.map((p, i) => ['D' + (i + 6), values[p.key]])))
  fill('xl/worksheets/sheet2.xml', { B6: product.definition.name, B7: product.revision,
    B11: catalog.schema.aplVersion, B12: catalog.source.dirty ? 'development' : catalog.schema.releaseStatus,
    B14: '', B15: catalog.source.dirty ? '' : catalog.source.baseCommit, B22: profilePid(product.definition) })
  // Traceability is visible in product metadata as well, without relying on custom properties.
  fill('xl/worksheets/sheet2.xml', { C15: catalog.source.dirty ? `本地开发快照 SHA-256：${catalog.source.fingerprint}` : `Git提交；源码指纹：${catalog.source.fingerprint}` })
  const mapping = profileKeyMapping(product.definition), cells: Record<string, string | number> = { B3: Number(mapping.enabled) }
  for (let i = 0; i < 7; i++) {
    cells['C' + (i + 7)] = mapping.short[i]; cells['E' + (i + 7)] = mapping.long[i]
    cells['D' + (i + 7)] = keyMappingActions[mapping.short[i]].name
    cells['F' + (i + 7)] = keyMappingActions[mapping.long[i]].name
  }
  fill('xl/worksheets/sheet4.xml', cells)
  return zipSync(files)
}
