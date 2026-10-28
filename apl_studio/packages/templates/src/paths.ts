export function safeRelative(path: string): string {
  if (!path || path.startsWith('/') || path.includes('\\') || !/^[a-zA-Z0-9_./-]+$/.test(path)
    || path.split('/').some(p => !p || p === '.' || p === '..' || p.endsWith('.') || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(p))) throw new Error('模板文件路径不安全：' + path)
  return path
}
