import { createHash } from 'node:crypto'
import { zipSync, strToU8 } from 'fflate'
import type { Product } from '@apl-studio/model'
import { firmwareBuild, c51InstanceBytes } from '@apl-studio/model/firmware'
import { memoryEstimateReport } from './memory-report'

export function firmwarePackage(product: Product, sourceFiles: Map<string, Uint8Array>, fingerprint: string) {
  const files = new Map(sourceFiles), d = product.definition, build = firmwareBuild(d)
  files.set('product.apl.json', strToU8(JSON.stringify(d, null, 2) + '\n'))
  files.set('firmware-build.json', strToU8(JSON.stringify(build, null, 2) + '\n'))
  files.set('memory-estimate.json', strToU8(JSON.stringify(memoryEstimateReport(product, fingerprint), null, 2) + '\n'))
  files.set('README.md', strToU8(`# APL 产品固件源码包

产品修订：r${product.revision}。APL来源指纹：${fingerprint}。
本包包含C源代码、编译裁剪头和产品默认配置，不是已编译固件，不包含芯片驱动。

1. 将src/*.c加入芯片工程，头文件路径添加include和config。
2. 保留整个config目录；apl_build_config.h自动包含apl_target_config.h，库与宿主必须一起完整重编译。外部宏与本包不一致时编译会报错。
3. 需要本产品默认值时#include "apl_product_profile.h"，声明struct apl_profile p并调用apl_product_profile_defaults(&p)，再调用apl_init_profile(...,&p)，检查返回值后再开始tick。NULL配置仅使用库的通用默认值。
4. 宿主每1ms串行调用apl_tick_1ms；参数传输、Flash保存、外围硬件和联网模块由宿主实现。
5. ${build.defines.APL_COMPILE_HMI ? '已编入APL交互，请接入按键和显示驱动。' : '已裁掉HMI；不得使用device.hmi或apl_hmi_*，宿主通过主控命令/getter接管按键与显示。'}

预算：Flash ${build.flashKb} KiB、RAM ${build.ramBytes} B、栈${build.stackBytes} B。
${build.target === 'c51' ? `Keil C51 LARGE参考实例预留${c51InstanceBytes(build)} B。` : '实际实例大小以目标编译器sizeof为准。'}
该数字不包含驱动、其他静态变量、回调表、中断或栈。memory-estimate.json包含已保存的实例数量、其他数据、软件栈和安全余量估算，未知项保持null。Flash未在本包生成时测量；报告中的手填值需重新核对。
请用目标工程的.M51/.MAP核对最终Flash、各RAM空间和完整调用链栈预算，不能仅凭预设或主机仿真判定容量通过。
裁掉的能力不能通过后续导入参数恢复；不同固件应配套使用本包的产品定义和配置头。
`))
  const hashes = Object.fromEntries([...files].sort(([a], [b]) => a.localeCompare(b)).map(([name, bytes]) => [name, createHash('sha256').update(bytes).digest('hex')]))
  files.set('manifest.json', strToU8(JSON.stringify({ format: 'apl-firmware-source', version: 1, productId: product.id, revision: product.revision,
    sourceFingerprint: fingerprint, build, compiled: false, targetCapacityVerified: false, files: hashes }, null, 2) + '\n'))
  const archive = Object.fromEntries([...files].map(([path, bytes]) => ['apl-firmware/' + path, [bytes, { mtime: new Date(1980, 0, 1) }] as [Uint8Array, { mtime: Date }]]))
  return { bytes: zipSync(archive), filename: `${d.code}-firmware-r${product.revision}.zip` }
}
