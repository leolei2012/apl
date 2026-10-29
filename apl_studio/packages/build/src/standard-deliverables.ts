import type { Product } from '@apl-studio/model'
import type { AcceptanceReport, TemplateDescriptor } from '@apl-studio/model/template'

export function standardDeliverables(product: Product, template: TemplateDescriptor, report: AcceptanceReport): Map<string, Uint8Array> {
  const d = product.definition, has = (key: string) => d.functions.some(f => f.key === key)
  const technical = `产品r${product.revision}；模板${template.id}@${template.version}；源码${report.sourceVersion}；模板指纹${report.template.fingerprint}；生成代码指纹${report.sourceFingerprint}；驱动指纹${report.driverFingerprint}；验收${report.id}`
  const pending = 'RH>50与目标湿度的优先级、Auto切换是否退出恒湿、湿差恰好19%的档位尚待确认，保持现行行为。风干1小时/3小时、灯位、传感器边界及项目协议的冲突与缺失也仍需确认，不能据本报告声称整份Excel全部符合。'
  const spec = `# ${d.name.replace(/[\r\n]/g, ' ')} 功能与交互说明\n\n${technical}\n\n能力：${has('purify') ? '空气净化；' : ''}${has('humidify') ? '加湿与风干；' : ''}手动/自动/睡眠、定时、童锁、灯光、维护和记忆由标准库执行。\n\n- 风速短按睡眠→一→二→三→自动；长按极速由配置决定。\n- 睡眠熄屏电源键直接执行；其他键首次只唤醒；灯光长按无附加模式动作。\n- 一体机Turbo期间有效加湿开关操作切至Auto，恒湿目标按现行规则保留。\n- 纯加湿缺水恢复窗口5分钟，到点进入风干；一体机缺水持续净化。\n- 关机根据本次运行提交的泵输出记录决定风干；未运行泵直接待机。记录不是物理负载成功反馈。\n- UV/负离子为复合净化接口；UV输出受联锁和故障状态约束，独立选配尚未完成。\n\n参数：\n${d.parameters.map(p => `- ${p.name}：${p.value}`).join('\n')}\n\n待确认：${pending}\n`
  const checks = `# 真实C标准场景验收\n\n${technical}\n\n范围：${report.scope}\n\n${report.checks.map(c => `- ${c.passed ? '通过' : '失败'}：${c.name}；预期${JSON.stringify(c.expected)}；实际${JSON.stringify(c.actual)}`).join('\n')}\n\n${pending}\n`
  const hardware = `# 工程师适配清单\n\n${technical}\n\nport为占位实现；源文件必须使用同一APL_CALL声明和回调定义。C51使用LARGE reentrant软件栈，工程启动前必须初始化并预留不与静态XDATA重叠的空间。\n\n${template.ports.filter(p => p.requires?.every(has) ?? true).map(p => `- [ ] ${p.name}：${p.description}`).join('\n')}\n- [ ] 确认板间职责、协议、传感器单位和有效性、按键与段码、负载标定、安全联锁。\n- [ ] 10ms前台串行推进；禁止ISR调用、回调递归tick。\n- [ ] NVM逐字段编码、版本/校验及掉电原子性，不保存跨架构原始结构体。\n- [ ] 检查CODE/XDATA、硬件/软件栈及真实ISR/触摸库开销；进行另行授权的实机验证。\n\n${pending}\n`
  return new Map([['docs/product-spec.md', spec], ['docs/acceptance-report.md', checks], ['docs/hardware-checklist.md', hardware], ['verification/acceptance.json', JSON.stringify(report, null, 2) + '\n']].map(([path, text]) => [path, Buffer.from(text)]))
}
