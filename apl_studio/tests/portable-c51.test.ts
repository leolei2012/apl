import { test } from 'node:test'
import assert from 'node:assert/strict'
import { cpSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { dirname, join, resolve } from 'node:path'
import { tmpdir } from 'node:os'
import { randomUUID, createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { CSourceTemplate, TemplateRegistry } from '@apl-studio/templates'
import { definitionFromTemplate } from '@apl-studio/model/template'
import { SdkBuildService, defaultCompiler } from '@apl-studio/build'
import type { Product } from '@apl-studio/model'

const repository = resolve(import.meta.dirname, '../..')
function fixture(integerProbe = false) {
  const directory = mkdtempSync(join(tmpdir(), 'apl-portable-test-'))
  const template = join(directory, 'template')
  cpSync(join(repository, 'apl_sdk_template/air-care/0.1.1'), template, { recursive: true })
  const hashes: Record<string, string> = {}
  for (const folder of ['src', 'include', 'config']) {
    for (const name of readdirSync(join(repository, 'apl_lib', folder))) {
      const bytes = readFileSync(join(repository, 'apl_lib', folder, name))
      writeFileSync(join(template, folder, name), bytes)
      hashes[folder + '/' + name] = createHash('sha256').update(bytes).digest('hex')
    }
  }
  const metadata = JSON.parse(readFileSync(join(template, 'template.json'), 'utf8'))
  metadata.version = '0.2.0'; metadata.sourceVersion = '0.2.2'
  if (integerProbe) {
    metadata.features.push({ key: 'integer_probe', name: 'Integer boundary probe', enabledByDefault: true,
      definition: { ...metadata.features[0].definition, id: 'template_integer_probe', key: 'integer_probe',
        name: 'Integer boundary probe', kind: 'int', access: 'readwrite', default: -2147483648,
        min: -2147483648, max: 2147483647, step: 2147483647 } })
    const adapters = JSON.parse(readFileSync(join(template, 'adapter.json'), 'utf8'))
    adapters.integer_probe = { get: 'return 0;', set: 'APL_UNUSED_PARAM(value); return true;' }
    writeFileSync(join(template, 'adapter.json'), JSON.stringify(adapters))
  }
  writeFileSync(join(template, 'template.json'), JSON.stringify(metadata))
  writeFileSync(join(template, 'source-snapshot.json'), JSON.stringify({ formatVersion: 1, templateVersion: '0.2.0', sourceVersion: '0.2.2', rootFiles: hashes }))
  cpSync(join(repository, 'tests/test_sdk_release.c'), join(template, 'verification/consumer.c'))
  const provider = new CSourceTemplate(template)
  return { directory, provider, close() {
    assert.equal(resolve(dirname(directory)), resolve(tmpdir()))
    rmSync(directory, { recursive: true, force: true })
  } }
}
function writeFiles(root: string, files: Map<string, Uint8Array>) {
  for (const [path, bytes] of files) {
    const destination = resolve(root, path)
    assert.ok(destination.startsWith(resolve(root) + (process.platform === 'win32' ? '\\' : '/')))
    mkdirSync(dirname(destination), { recursive: true }); writeFileSync(destination, bytes)
  }
}
function command(executable: string, args: string[]) {
  const result = spawnSync(executable, args, { encoding: 'utf8', timeout: 60000 })
  assert.equal(result.status, 0, `${executable}: ${result.error ?? ''}\n${result.stdout}\n${result.stderr}`)
  return result.stdout
}
function targetCompile(root: string) {
  const compiler = process.env.APL_STUDIO_C51
  if (!compiler) return
  for (const folder of ['src', 'port']) for (const name of readdirSync(join(root, folder)).filter(name => name.endsWith('.c'))) {
    command(compiler, [join(root, folder, name), 'LARGE', 'OBJECTEXTEND', 'OPTIMIZE(8,SIZE)',
      `INCDIR(${join(root, 'include')},${join(root, 'config')},${join(root, 'port')})`,
      `OBJECT(${join(root, folder + '-' + name + '.obj')})`, `PRINT(${join(root, folder + '-' + name + '.lst')})`])
  }
}

test('portable APL candidate compiles/consumes and downloads while frozen templates stay unchanged', async () => {
  const f = fixture(), registry = new TemplateRegistry()
  registry.register(f.provider)
  const builds = new SdkBuildService(join(f.directory, 'sdk'), registry)
  try {
    const definition = definitionFromTemplate(f.provider.descriptor); definition.code = 'portable_sdk'
    const now = new Date().toISOString()
    const product: Product = { id: randomUUID(), revision: 1, createdAt: now, updatedAt: now, definition }
    const initial = await builds.create(product)
    const deadline = Date.now() + 60000
    let job = builds.get(initial.id)
    while (['queued', 'building'].includes(job.status) && Date.now() < deadline) {
      await new Promise(resolve => setTimeout(resolve, 50)); job = builds.get(initial.id)
    }
    assert.equal(job.status, 'succeeded', job.diagnostics)
    assert.equal(job.checks?.compile, 'passed'); assert.equal(job.checks?.consumer, 'passed')
    assert.ok(builds.artifact(job.id).bytes.length > 0)
    const files = await f.provider.generate(definition)
    const source = Buffer.from(files.get('src/apl_product.c')!).toString()
    assert.match(source, /APL_CALL/); assert.doesNotMatch(source, /command = \{ type, arg \}/)
    const target = join(f.directory, 'target'); mkdirSync(target)
    writeFiles(target, files); targetCompile(target)
    assert.equal(readFileSync(join(repository, 'apl_sdk_template/air-care/0.1.1/include/apl.h'), 'utf8').includes('"0.1.1"'), true)
  } finally { await builds.close(); f.close() }
})

test('portable generated integer setter preserves full signed32 range and exact step without int64', async () => {
  const f = fixture(true)
  try {
    const definition = definitionFromTemplate(f.provider.descriptor); definition.code = 'integer_sdk'
    const files = await f.provider.generate(definition)
    const generated = Buffer.from(files.get('src/apl_product.c')!).toString()
    assert.doesNotMatch(generated, /int64_t/)
    assert.match(generated, /-2147483647L - 1L/)
    const target = join(f.directory, 'target'); mkdirSync(target); writeFiles(target, files)
    const consumer = `#include "apl_product.h"
#include <assert.h>
int main(void) {
  apl_integer_sdk_t instance;
  apl_integer_sdk_init(&instance, 0, 0, 0, 0);
  assert(apl_integer_sdk_set_integer_probe(&instance, (-2147483647L-1L)));
  assert(apl_integer_sdk_set_integer_probe(&instance, -1L));
  assert(apl_integer_sdk_set_integer_probe(&instance, 2147483646L));
  assert(!apl_integer_sdk_set_integer_probe(&instance, (-2147483647L)));
  assert(!apl_integer_sdk_set_integer_probe(&instance, 0L));
  assert(!apl_integer_sdk_set_integer_probe(&instance, 2147483647L));
  return 0;
}`
    writeFileSync(join(target, 'range-consumer.c'), consumer)
    const executable = join(target, process.platform === 'win32' ? 'range.exe' : 'range')
    command(defaultCompiler(), ['-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + join(target, 'include'), '-I' + join(target, 'config'),
      ...readdirSync(join(target, 'src')).filter(name => name.endsWith('.c')).map(name => join(target, 'src', name)), join(target, 'range-consumer.c'), '-o', executable])
    command(executable, [])
    targetCompile(target)
  } finally { f.close() }
})
