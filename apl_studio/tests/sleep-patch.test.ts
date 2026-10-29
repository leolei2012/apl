import { test } from 'node:test'
import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, rmSync, readFileSync, writeFileSync } from 'node:fs'
import { join, resolve, dirname } from 'node:path'
import { tmpdir } from 'node:os'
import { createHash } from 'node:crypto'
import { unzipSync, strFromU8 } from 'fflate'
import { execFileSync } from 'node:child_process'
import { createElement } from 'react'
import { renderToStaticMarkup } from 'react-dom/server'
import { applyTemplate, definitionFromTemplate, toggleTemplateFeature } from '@apl-studio/model/template'
import { TemplateRegistry } from '@apl-studio/templates'
import { ProductStore } from '@apl-studio/product'
import { AcceptanceService, SimulationService, SdkBuildService } from '@apl-studio/build'
import { StandardOptions } from '../apps/web/src/StandardOptions'

test('睡眠补丁0.4.1：配置→真实C输入契约验收→直接源码生成→GCC消费（不执行浏览器或HTTP下载）', async () => {
  const directory = mkdtempSync(join(tmpdir(),'apl-advanced-')), registry = new TemplateRegistry()
  registry.loadDirectory(resolve(import.meta.dirname,'../../apl_sdk_template'))
  const provider = registry.get('apl.air-care','0.4.1')!, template = provider.descriptor
  const store = new ProductStore(join(directory,'studio.db')), simulations = new SimulationService(join(directory,'simulation'),registry)
  const acceptances = new AcceptanceService(join(directory,'acceptance'),registry,simulations)
  const builds = new SdkBuildService(join(directory,'archives'),registry,undefined,undefined,acceptances)
  const results: unknown[] = []
  try {
    assert.equal(template.sourceVersion,'0.2.1')
    for (const profile of ['purify','humidify','combined','all_optional','no_optional']) {
      const keys = new Set(['power','fan_speed',...(profile!=='humidify'?['purify','pm25']:[]),...(profile!=='purify'?['humidify','humidity','drying']:[])])
      if(profile!=='no_optional') for(const key of ['uv','anion','wifi']) keys.add(key)
      if(profile==='all_optional') for(const key of ['sensor_pm10','sensor_hcho','sensor_tvoc']) keys.add(key)
      if(profile==='no_optional') { keys.delete('pm25'); keys.delete('humidity') }
      let definition = applyTemplate(definitionFromTemplate(template),template,keys)
      definition.name = '选配验证 '+profile; definition.code = 'advanced_'+profile
      assert.deepEqual(provider.validate(definition),[])
      const html=renderToStaticMarkup(createElement(StandardOptions,{draft:definition,template,setDraft(){}}))
      assert.match(html,/UV模块/); assert.match(html,/备用档/)
      const toggled=toggleTemplateFeature(definition,template,'wifi',!keys.has('wifi'))
      assert.equal(toggled.functions.some(f=>f.key==='wifi'),!keys.has('wifi'))
      assert.deepEqual(provider.validate(toggled),[])
      const product=store.create(definition)
      acceptances.run(product); await acceptances.close()
      const {report,current}=acceptances.latest(product)
      assert.equal(report?.status,'passed',profile+': '+report?.diagnostics+' '+JSON.stringify(report?.checks.at(-1)))
      assert.equal(current,true); assert.ok(report!.checks.length>=9)
      const files=await provider.generate(definition)
      acceptances.requireSource(acceptances.require(product),files)
      const initial = await builds.create(product)
      let job = builds.get(initial.id), deadline = Date.now()+60000
      while (['queued','building'].includes(job.status) && Date.now()<deadline) {
        await new Promise(done=>setTimeout(done,30)); job=builds.get(initial.id)
      }
      assert.equal(job.status,'succeeded',job.diagnostics)
      assert.deepEqual(job.checks,{compile:'passed',consumer:'passed'})
      // In-process artifact validation only: no HTTP endpoint or browser download.
      const archive = unzipSync(builds.artifact(job.id).bytes)
      const packaged = JSON.parse(strFromU8(archive['apl-sdk/manifest.json']))
      assert.equal(packaged.sdkReady,true); assert.equal(packaged.sourceVersion,'0.2.1')
      assert.equal(packaged.verification.acceptance.fingerprint,report!.fingerprint)
      for(const [path,hash] of Object.entries(packaged.files)) assert.equal(createHash('sha256').update(archive['apl-sdk/'+path]).digest('hex'),hash)

      const output=resolve(process.env.APL_ADVANCED_OUTPUT ?? join(directory,'generated'),profile)
      const delivery = new Map([...files,...acceptances.deliverables(product,provider,report!)])
      for(const [path,data] of delivery) {
        const target=join(output,path); mkdirSync(dirname(target),{recursive:true}); writeFileSync(target,data)
      }
      const hashes=Object.fromEntries([...delivery].map(([path,data])=>[path,createHash('sha256').update(data).digest('hex')]))
      writeFileSync(join(output,'manifest.json'),JSON.stringify({format:'apl-source-validation-v1',scope:'直接源码生成与本机验收；未执行归档下载，不替代已取消浏览器操作',template:report!.template,acceptance:report!.fingerprint,files:hashes},null,2))
      const args=['-std=c11','-Wall','-Wextra','-Werror',...['include','config','port'].map(f=>'-I'+join(output,f)),...['apl.c','apl_core.c','apl_hmi.c','apl_product.c'].map(f=>join(output,'src',f)),join(output,'port/apl_port.c'),join(output,'verification/consumer.c'),'-o',join(output,'consumer.exe')]
      execFileSync(process.env.CC!,args,{encoding:'utf8'})
      const consumer=execFileSync(join(output,'consumer.exe'),[],{encoding:'utf8'})
      const doc=readFileSync(join(output,'docs/product-spec.md'),'utf8');assert.match(doc,/选配能力/);assert.match(doc,/1.5秒/)
      results.push({profile,checks:report!.checks.length,acceptance:report!.fingerprint,source:report!.sourceFingerprint,consumer:consumer.trim(),archiveVerification:{id:job.id,checks:job.checks,fingerprint:job.fingerprint,fileCount:Object.keys(packaged.files).length},output})
    }
    // Every source byte in the new frozen candidate matches the committed root snapshot.
    const root=resolve(import.meta.dirname,'../../apl_sdk_template/air-care/0.4.1')
    const snapshot=JSON.parse(readFileSync(join(root,'source-snapshot.json'),'utf8'))
    for(const [path,hash] of Object.entries(snapshot.rootFiles)) assert.equal(createHash('sha256').update(readFileSync(join(root,path))).digest('hex'),hash)
    if(process.env.APL_ADVANCED_OUTPUT) writeFileSync(join(process.env.APL_ADVANCED_OUTPUT,'results.json'),JSON.stringify({scope:'no browser/HTTP download; direct generation and native C validation',results},null,2))
    console.log(JSON.stringify(results))
  } finally {
    await simulations.close(); await acceptances.close(); await builds.close(); store.close()
    assert.equal(dirname(directory),resolve(tmpdir())); rmSync(directory,{recursive:true,force:true})
  }
})
