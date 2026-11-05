# APL

Web APL Studio现支持服务器公网部署，包含访问账号、HTTPS/WebSocket反代配置和Docker持久化部署入口，见[公网部署](apl_studio/deploy/README.md)。本地默认仍为`127.0.0.1:9190`。

新方向是固定全功能固件，由 [Web APL Studio](apl_studio/README.md) 生成产品参数，再导入设备。APL 开发版 0.3.0 已提供运行时方案的校验、冷启动加载和读回，见 [运行时参数配置](docs/规格/APL运行时参数配置.md)。导入协议待定；当前 Web 的 SDK 交付和冻结模板仍按既有流程运行，尚未改为参数下发。`simulator/` 内旧 Qt 工具保留作历史参考。

空气净化与加湿库。库内不创建实例，使用方持有一份 `struct apl`。

宿主接入从 [APL使用说明](apl_lib/使用说明.md) 开始：未导入参数表时使用固件缺省配置，包含各档风机目标转速；文档统一维护编译、启动、参数加载、转速读取和掉电记忆的使用步骤。

移植工程需提供的周期调度、硬件回调、RGB输出和存储条件见 [宿主接入要求](apl_lib/宿主接入要求.md)。

库源码集中在 [`apl_lib/`](apl_lib/README.md)：`src/`、`include/`、`config/`保留原有内部结构。测试仍在工程根`tests/`，冻结模板和导出SDK继续使用各自包内的相对目录。

| 文件 | 职责 |
|---|---|
| [apl_lib/include/apl.h](./apl_lib/include/apl.h)、[apl_lib/src/apl.c](./apl_lib/src/apl.c) | 组合对象。推荐每 1ms 调用 `apl_tick_1ms`，内部每 10ms 先主控、后人机推进一个业务帧 |
| [apl_lib/include/apl_type.h](./apl_lib/include/apl_type.h) | 整机状态、开关、风机档位、水位段数。主控和人机共用，两边不互相包含 |
| [apl_lib/include/apl_core.h](./apl_lib/include/apl_core.h)、[apl_lib/src/apl_core.c](./apl_lib/src/apl_core.c) | 业务判断、动态配置、执行器输出、状态快照 |
| [apl_lib/include/apl_hmi.h](./apl_lib/include/apl_hmi.h)、[apl_lib/src/apl_hmi.c](./apl_lib/src/apl_hmi.c) | 按键与显示。面板在编译期选定 |

主控和人机互不包含。`apl.c` 把它们放在同一个对象里。人机交出 `link_command`，主控写入 `link_snapshot`。净化与加湿是否启用写在 `apl_core` 的动态配置里；没编译进程序的功能不能被打开。

## 配置

运行时产品参数的 Excel 模板、机器可读字段定义和三机型示例位于 [apl_lib/parameters](apl_lib/parameters/README.md)，与 APL 源码一起管理版本。未来 Studio 按所选 APL Git 版本读取相应参数定义。

当前参数格式v6、定义修订7增加[内部按键映射](apl_lib/按键映射.md)：Studio分别配置短按、长按，APL按动作执行，宿主GPIO和输入编号不变。未配置时保留标准行为；事件和组合键仍使用原始输入。配置随产品保存、仿真及导出，可通过编译开关裁掉自定义映射。

| 文件 | 用途 |
|---|---|
| [apl_lib/config/apl_core_config.h](./apl_lib/config/apl_core_config.h) | 主控的编译控制与默认值 |
| [apl_lib/config/apl_hmi_config.h](./apl_lib/config/apl_hmi_config.h) | 人机的编译控制与默认值。`APL_HMI_PANEL` 选定面板，六颗键各自选配 |

`apl_hmi` 不读取 `apl_core_config.h`。目标湿度、档位等业务默认值放在主控配置；背光等面板本地默认值放在人机配置。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/规范](./docs/规范/README.md) | 架构规范、编码规范 |
| [docs/规格/apl后端规格书.md](./docs/规格/apl后端规格书.md) | `apl_core` |
| [docs/规格/apl前端规格书.md](./docs/规格/apl前端规格书.md) | `apl_hmi` |
| [docs/规格/datasheet.md](./docs/规格/datasheet.md) | 人机对象：六个按键及键下灯、氛围灯、数码管 |
| [apl_studio/README.md](./apl_studio/README.md) | APL Studio：多品类产品配置、Web C 仿真台、真实 C11 SDK 生成、消费验证与 ZIP 交付（AP / 灯控） |
| [simulator/README.md](./simulator/README.md) | Qt 仿真台。外壳是 Widgets，中间的面贴是 QML |

规格书仍是草稿。`link_command`、`link_snapshot` 的字段见后端规格书第 3.2 节；净化、加湿、风干、缺水、滤网、产测等业务按《机型交互逻辑（Excel提炼）》实现，原表冲突处的取舍列在前后端规格书的「待确认」一节。故障策略和动态配置的修改入口尚未规定。

已实现的接口：

| 文件 | 内容 |
|---|---|
| [apl_lib/include/apl.h](./apl_lib/include/apl.h) | `apl_init` 初始化两边，`apl_tick_1ms` 为 1ms 调度入口；旧 `apl_tick` 保留 10ms 语义，同一实例不可混调 |
| [apl_lib/include/apl_type.h](./apl_lib/include/apl_type.h) | 主控和人机共用的枚举，以及 `struct apl_link_command`、`struct apl_link_snapshot`、`struct apl_core_memory` |
| [apl_lib/include/apl_core.h](./apl_lib/include/apl_core.h) | 运行四态、风干、定时、风机与自动/恒湿、净化与 UV 联锁、加湿与缺水、滤网、Wi-Fi、产测、掉电记忆；`apl_core_command`、`apl_core_get_snapshot` |
| [apl_lib/include/apl_hmi.h](./apl_lib/include/apl_hmi.h) | 六键短按/长按、童锁、组合键、键灯、状态图标、水位灯与颜色、氛围灯、数码管、蜂鸣器；接上 link 后的面板逻辑。上电全灯 2 秒后熄灭，电源键半亮 |

从工程根编译时，把头文件搜索路径指到 `apl_lib/include` 和 `apl_lib/config`，源码位于`apl_lib/src`。`tests/test_apl.c`、`tests/test_link.c`、`tests/test_hmi_key_optional.c` 是宿主侧检查，命令写在各文件头。

[simulator](./simulator/README.md) 是 Qt 仿真台。它持有一份 `struct apl` 并每 10 ms 调用 `apl_tick`。外壳是 Widgets，提供概览、产品配置、实时仿真和 SDK 交付四页工作区。仿真页中央是 QML 面贴，右侧是传感器、负载和诊断。前端只读 getter、只上报按键电平。库还没有的业务，仿真台不另外实现。编译和启动见 [使用说明](./simulator/使用说明.md)，接线见 [设计说明](./simulator/设计说明.md)。

Qt 仿真台仅用于测试本工程 APL，保留内置面板、传感器、固定负载、事件、供电和存储。说明见 [simulator 文档入口](simulator/README.md)。

APL 产品配置和源码 SDK 导出已集成到仿真台：配置功能、业务参数和按键后单独构建 APL 预览，导出包含源码、配置、移植模板和示例的 ZIP。保存 APL 实现后自动重载，无需每次重建 simulator。见 [APL 开发与热重载](simulator/APL开发与热重载.md)、[产品配置与 SDK 导出](simulator/产品配置与SDK导出.md) 和 [配置格式与生成设计](simulator/产品配置格式与生成设计.md)。
