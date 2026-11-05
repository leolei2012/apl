/** Minimal Web Crypto surface, also usable where randomUUID is unavailable. */
export interface IdCrypto {
  randomUUID?: () => string
  getRandomValues?: (bytes: Uint8Array) => unknown
}

/** UUIDv4 using the browser's cryptographic RNG; never use Math.random.
 * randomUUID is SecureContext-only, while getRandomValues is available on HTTP.
 * https://w3c.github.io/webcrypto/#crypto-interface
 */
export function newId(source: IdCrypto | undefined = globalThis.crypto): string {
  if (typeof source?.randomUUID === 'function') return source.randomUUID()
  if (typeof source?.getRandomValues !== 'function')
    throw new Error('当前环境不支持安全随机数，无法生成标识；请更新浏览器或使用 HTTPS')
  const bytes = new Uint8Array(16)
  source.getRandomValues(bytes)
  bytes[6] = (bytes[6] & 0x0f) | 0x40
  bytes[8] = (bytes[8] & 0x3f) | 0x80
  const hex = Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('')
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`
}
