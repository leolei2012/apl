import type { Product, Value } from '@apl-studio/model'
import type { SimulationAction, SimulationSnapshot } from '@apl-studio/model/simulation'

type Act = (action: SimulationAction) => Promise<SimulationSnapshot>
type Check = (name: string, expected: Record<string, Value>, actual: Record<string, Value>, snapshot: SimulationSnapshot) => void

/** Exercise the real generated C through ordinary panel and sensor inputs. */
export async function standardAcceptance(product: Product, act: Act, check: Check) {
  const has = (key: string) => product.definition.functions.some(f => f.key === key)
  const purify = has('purify'), humidify = has('humidify')
  const turbo = product.definition.parameters.find(p => p.key === 'turbo_enabled')?.value === true
  let s: SimulationSnapshot
  const advance = async (ms: number) => {
    while (ms > 0) { const step = Math.min(ms, 60000); s = await act({ type: 'advance', ms: step }); ms -= step }
    return s
  }
  const input = async (key: string, value: Value) => (s = await act({ type: 'input', key, value }))
  const start = async (water = 3) => {
    s = await act({ type: 'reset' })
    if (humidify) { await input('water_level', water); await input('humidity', 45) }
    if (purify) { await input('pm25', 10); await input('uv_interlock', true) }
    await advance(2100)
    await input('Power', 'short')
    await advance(1100)
  }
  const select = async (gear: number) => {
    for (let n = 0; n < 12 && s.internal.fan_speed !== gear; n++) await input('Speed', 'short')
    if (s.internal.fan_speed !== gear) throw new Error('标准风速键未能进入所选模式')
  }
  await start()
  await select(1)
  check('手动一档驱动实际风机', { running: 1, fan: 1, gear: 1 }, { running: s!.internal.run_state, fan: s!.outputs.fan, gear: s!.outputs.fan_gear }, s!)
  await input('Light', 'long')
  check('按键说明E11：灯光长按无附加模式动作', { gear: 1, light: 1 }, { gear: s!.internal.fan_speed, light: s!.display.light }, s!)
  await input('Speed', 'long')
  check('风速长按遵守极速配置', { gear: turbo ? 5 : 1 }, { gear: s!.internal.fan_speed }, s!)
  if (turbo && purify && humidify) {
    await input('Humidity', 'short')
    check('一体机E11：Turbo期间加湿键切至Auto', { gear: 4 }, { gear: s!.internal.fan_speed }, s!)
  }
  await select(0)
  await advance(6000)
  await input('Speed', 'short')
  check('更改记录D23：睡眠熄屏非电源键首次只唤醒', { gear: 0 }, { gear: s!.internal.fan_speed }, s!)
  await advance(6000)
  await input('Power', 'short')
  check('更改记录D23：睡眠熄屏电源键直接执行关机', { running: humidify ? 2 : 0 }, { running: s!.internal.run_state }, s!)
  if (humidify) {
    await start(0)
    await input('Power', 'short')
    check('C17/E17及C21/E21：本次未提交泵开启则关机直接待机', { running: 0, pump: 0 }, { running: s!.internal.run_state, pump: s!.outputs.pump }, s!)
    await start()
    await select(1)
    check('有效水位允许加湿负载', { pump: 1 }, { pump: s!.outputs.pump }, s!)
    await input('water_level', 0)
    await advance(1000)
    check('缺水立即停止泵，风机保持运行', { pump: 0, fan: 1 }, { pump: s!.outputs.pump, fan: s!.outputs.fan }, s!)
    await input('water_level', 3)
    await advance(1000)
    check('恢复窗口内有效水位恢复加湿', { pump: 1, running: 1 }, { pump: s!.outputs.pump, running: s!.internal.run_state }, s!)
    await input('water_level', 0)
    await advance(299000)
    check('缺水恢复窗口结束前仍运行', { running: 1 }, { running: s!.internal.run_state }, s!)
    await advance(1100)
    check(purify ? '一体机D20/E20：缺水5分钟后仍持续净化' : '加湿器E16：缺水5分钟后进入风干', { running: purify ? 1 : 2, pump: 0 }, { running: s!.internal.run_state, pump: s!.outputs.pump }, s!)
    if (purify) {
      await advance(1500000)
      check('一体机缺水30分钟后继续净化', { running: 1, fan: 1 }, { running: s!.internal.run_state, fan: s!.outputs.fan }, s!)
    } else {
      await act({ type: 'supply', on: false }); await act({ type: 'supply', on: true }); await advance(2100)
      check('加湿器D17：风干中掉电重启直接待机', { running: 0 }, { running: s!.internal.run_state }, s!)
    }
  }
  await start()
  await select(4)
  if (purify) {
    for (const [pm, output] of (humidify ? [[10, 1], [1000, 3]] : [[35, 0], [36, 1], [75, 1], [76, 2], [150, 2], [151, 3]])) {
      await input('pm25', pm); await advance(1000)
      check(`自动调速PM2.5=${pm}`, { output, requested: 4, sleep: 0 }, { output: s!.outputs.fan_gear, requested: s!.internal.fan_speed, sleep: s!.internal.sleep }, s!)
    }
    if (s!.internal.purify !== 1) await input('Purify', 'short')
    await advance(100)
    check('净化复合输出在联锁闭合时开启', { uv: 1 }, { uv: s!.outputs.uv }, s!)
    await input('uv_interlock', false); await advance(100)
    check('UV安全联锁断开后输出关闭', { uv: 0, fault: 1 }, { uv: s!.outputs.uv, fault: s!.internal.uv_fault }, s!)
  } else {
    await input('humidity', 45); await advance(1000)
    check('纯加湿自动档按环境湿度调速', { output: 2, requested: 4 }, { output: s!.outputs.fan_gear, requested: s!.internal.fan_speed }, s!)
  }
  check('维护计时初始值有效', { left: 99 }, { left: humidify ? s!.internal.filter_humidify_left : s!.internal.filter_purify_left }, s!)
}
