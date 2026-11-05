import { test } from 'node:test'
import assert from 'node:assert/strict'
import { dirname, delimiter } from 'node:path'
import { spawnSync } from 'node:child_process'
import { childEnvironment } from '../packages/build/src/process-environment'

test('本机编译与C进程环境保留工具链，过滤部署账号且不改父进程环境', () => {
  const parent = { ...process.env, PATH: 'toolchain-path', APL_STUDIO_USERNAME: 'test-user', APL_STUDIO_PASSWORD: 'test-only-secret',
    apl_studio_password: 'other-case-secret', APL_STUDIO_CC: '/toolchain/gcc', TMPDIR: 'temporary-path' }
  const before = { ...parent }, environment = childEnvironment(process.execPath, parent)
  assert.equal(environment.PATH, dirname(process.execPath) + delimiter + parent.PATH)
  assert.equal(environment.APL_STUDIO_CC, parent.APL_STUDIO_CC)
  assert.equal(environment.TMPDIR, parent.TMPDIR)
  assert.ok(!Object.keys(environment).some(key => ['APL_STUDIO_USERNAME', 'APL_STUDIO_PASSWORD'].includes(key.toUpperCase())))
  assert.deepEqual(parent, before)
  const child = spawnSync(process.execPath, ['-e', 'process.exit(Object.keys(process.env).some(k=>/^APL_STUDIO_(USERNAME|PASSWORD)$/i.test(k))?1:0)'], { env: environment })
  assert.equal(child.status, 0, child.stderr.toString())
})
