import type { Product, Value } from '@apl-studio/model'
import type { SimulationAction, SimulationSnapshot } from '@apl-studio/model/simulation'
type Act = (action: SimulationAction) => Promise<SimulationSnapshot>
type Check = (name: string, expected: Record<string, Value>, actual: Record<string, Value>, s: SimulationSnapshot) => void

/** Tests generated code with panel and declared input contracts, never with physical hardware. */
export async function advancedAcceptance(product: Product, act: Act, check: Check) {
  const has = (key: string) => product.definition.functions.some(f => f.key === key)
  let s!: SimulationSnapshot
  const input = async (key: string, value: Value) => s = await act({ type: 'input', key, value })
  const advance = async (ms: number) => s = await act({ type: 'advance', ms })
  const select = async (gear: number) => {
    for (let i = 0; i < 12 && s.internal.fan_speed !== gear; ++i) await input('Speed', 'short')
    if (s.internal.fan_speed !== gear) throw new Error('风速键未进入预期档位')
  }
  const start = async () => {
    s = await act({ type: 'reset' })
    for (const key of ['pm25','humidity','pm10','hcho','tvoc']) if (has(['pm10','hcho','tvoc'].includes(key) ? 'sensor_' + key : key)) await input(key + '_status', 2)
    if (has('humidify')) await input('water_level', 3)
    for (const key of ['uv','anion','wifi']) if (has(key)) await input(key + '_fault_input', false)
    if (has('uv')) await input('uv_interlock', true)
    await advance(2100); await input('Power', 'short'); await advance(1100)
  }
  await start(); await select(1)
  check('手动一档驱动风机', { fan: 1, gear: 1 }, { fan: s.outputs.fan, gear: s.outputs.fan_gear }, s)
  await input('Timer','short'); await input('Timer','long')
  check('定时设置长按1.5秒后每0.5秒连增且不锁童锁', { hours: 5, lock: 0 }, { hours: Number(s.display.digits), lock: s.display.child_lock }, s)
  await advance(3100)
  check('定时松手3秒提交所选小时', { hours: 5 }, { hours: Math.ceil(Number(s.internal.timer_left_s)/3600) }, s)
  await select(0); await advance(4000)
  check('睡眠4秒仍显示', { sleep: 1, brightness: 255 }, { sleep: s.internal.sleep, brightness: s.display.brightness }, s)
  await advance(1100)
  check('睡眠独立5秒熄屏', { brightness: 0 }, { brightness: s.display.brightness }, s)
  if (['0.4.1','0.4.2'].includes(product.definition.template?.version ?? '')) check('睡眠熄屏后保留Sleep灯（三机型共同要求）', { sleepLamp: 255 }, { sleepLamp: s.display.icon_8 }, s)
  await input('Speed','short')
  check('睡眠首次非电源键只唤醒', { gear: 0, brightness: 255 }, { gear: s.internal.fan_speed, brightness: s.display.brightness }, s)
  await select(1); await input('Speed','long')
  const turbo = product.definition.parameters.find(p => p.key === 'turbo_enabled')?.value === true
  check('极速开关及三档灯联动', { gear: turbo ? 5 : 1, lamps: turbo ? 3 : 1 }, { gear: s.internal.fan_speed, lamps: [9,10,11].filter(i => Number(s.display['icon_'+i]) > 0).length }, s)
  await select(1)
  if (has('uv')) {
    await input('uv_request', true); await advance(1100)
    check('独立UV请求与有效联锁驱动输出', { uv: 1 }, { uv: s.outputs.uv }, s)
    await input('uv_interlock',false); await advance(1100)
    check('UV联锁断开立即禁止输出', { uv: 0, fan: 1 }, { uv: s.outputs.uv, fan: s.outputs.fan }, s)
    await input('uv_interlock',true); await input('uv_fault_input',true); await advance(1100)
    check('UV宿主故障反馈锁存', { uv: 0, fault: 1 }, { uv: s.outputs.uv, fault: s.internal.uv_fault }, s)
    await input('uv_fault_input',false); await input('uv_request',true); await advance(1100)
    check('故障解除后显式请求确认复位', { uv: 1, fault: 0 }, { uv: s.outputs.uv, fault: s.internal.uv_fault }, s)
  }
  if (has('anion')) {
    if (has('uv')) await input('uv_request',false)
    await input('anion_request',true); await advance(1100)
    check('负离子独立于UV请求', { anion: 1, uv: 0 }, { anion: s.outputs.anion, uv: s.outputs.uv }, s)
    await input('anion_fault_input',true); await advance(1100)
    check('负离子宿主故障禁止输出', { anion: 0 }, { anion: s.outputs.anion }, s)
    await input('anion_fault_input',false)
  }
  if (has('wifi')) {
    await input('wifi_connected',true); await advance(1100)
    check('Wi-Fi连接状态来自宿主', { wifi: 2 }, { wifi: s.internal.wifi }, s)
    await input('wifi_fault_input',true); await advance(1100)
    check('Wi-Fi故障退回离线', { wifi: 0 }, { wifi: s.internal.wifi }, s)
    await input('wifi_fault_input',false)
  }
  await select(4)
  for (const [key, bit] of [['pm25',1],['humidity',16],['pm10',2],['hcho',4],['tvoc',8]] as const) {
    if (!has(['pm10','hcho','tvoc'].includes(key) ? 'sensor_' + key : key)) continue
    await input(key+'_status',1); await advance(1100)
    check(key+'预处理明确报告状态', { warming: bit, ready: 0 }, { warming: Number(s.internal.sensor_warming)&bit, ready: Number(s.internal.sensor_ready)&bit }, s)
    await input(key+'_status',3); await advance(1100)
    check(key+'故障不作为有效样本', { fault: bit, ready: 0 }, { fault: Number(s.internal.sensor_faults)&bit, ready: Number(s.internal.sensor_ready)&bit }, s)
    await input(key+'_status',2); await advance(1100)
    check(key+'恢复有效样本', { fault: 0, ready: bit }, { fault: Number(s.internal.sensor_faults)&bit, ready: Number(s.internal.sensor_ready)&bit }, s)
  }
  await select(1)
  const before = Number(s.internal.filter_reset_seq)
  await input(has('humidify') ? 'combo_filter' : 'Filter', 'long')
  check('品类对应滤网维护操作', { sequence: (before+1)&255 }, { sequence: s.internal.filter_reset_seq }, s)
  await input('Power','short')
  check('电源键执行正常关机或风干', { stoppedRun: true }, { stoppedRun: s.internal.run_state !== 1 }, s)
  if (has('purify') && !has('humidify')) {
    await input('Filter','short')
    check('纯净化待机支持滤网查询', { standby: 0, brightness: 255 }, { standby: s.internal.run_state, brightness: s.display.brightness }, s)
  }
}
