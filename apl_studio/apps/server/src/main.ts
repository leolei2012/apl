import { startStudio } from './startup'

try {
  const studio = await startStudio()
  console.log(`\n  APL Studio  ${studio.url}\n  产品数据    ${studio.data}\n`)
  for (const signal of ['SIGINT', 'SIGTERM'] as const) process.on(signal, async () => {
    await studio.close()
  })
} catch (error) {
  console.error('启动失败：', (error as Error).message)
  process.exitCode = 1
}
