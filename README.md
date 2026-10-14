# APL

空气净化与加湿库。库内不创建实例，使用方持有一份 `struct apl`。

| 文件 | 职责 |
|---|---|
| [include/apl.h](./include/apl.h)、[src/apl.c](./src/apl.c) | 组合对象。初始化主控和人机，每 10 ms 先主控、后人机推进一拍 |
| [include/apl_type.h](./include/apl_type.h) | 整机状态、开关、风机档位、水位段数。主控和人机共用，两边不互相包含 |
| [include/apl_core.h](./include/apl_core.h)、[src/apl_core.c](./src/apl_core.c) | 业务判断、动态配置、执行器输出、状态快照 |
| [include/apl_hmi.h](./include/apl_hmi.h)、[src/apl_hmi.c](./src/apl_hmi.c) | 按键与显示。面板在编译期选定 |

主控和人机互不包含。`apl.c` 把它们放在同一个对象里。人机交出 `link_command`，主控写入 `link_snapshot`。净化与加湿是否启用写在 `apl_core` 的动态配置里；没编译进程序的功能不能被打开。

## 配置

| 文件 | 用途 |
|---|---|
| [config/apl_core_config.h](./config/apl_core_config.h) | 主控的编译控制与默认值 |
| [config/apl_hmi_config.h](./config/apl_hmi_config.h) | 人机的编译控制与默认值。`APL_HMI_PANEL` 选定面板，六颗键各自选配 |

`apl_hmi` 不读取 `apl_core_config.h`。目标湿度、档位等业务默认值放在主控配置；背光等面板本地默认值放在人机配置。

## 文档

| 文档 | 内容 |
|---|---|
| [docs/规范](./docs/规范/README.md) | 架构规范、编码规范 |
| [docs/规格/apl后端规格书.md](./docs/规格/apl后端规格书.md) | `apl_core` |
| [docs/规格/apl前端规格书.md](./docs/规格/apl前端规格书.md) | `apl_hmi` |
| [docs/规格/datasheet.md](./docs/规格/datasheet.md) | 人机对象：六个按键及键下灯、氛围灯、数码管 |
| [simulator/README.md](./simulator/README.md) | Qt 仿真台。外壳是 Widgets，中间的面贴是 QML |

规格书仍是草稿。`link_command`、`link_snapshot` 的字段，以及净化、加湿内部如何输出，尚未规定，库里也还没有这些逻辑。

已实现的接口：

| 文件 | 内容 |
|---|---|
| [include/apl.h](./include/apl.h) | `apl_init` 初始化两边，`apl_tick` 每 10 ms 先调用 `apl_core_tick`，再调用 `apl_hmi_tick` |
| [include/apl_type.h](./include/apl_type.h) | `enum apl_power`、`enum apl_switch`、`enum apl_fan_gear`、`enum apl_water_level` |
| [include/apl_core.h](./include/apl_core.h) | 开关机、定时、风机档位、两套滤网清零、水位边沿。状态通过 getter 和 setter 交换 |
| [include/apl_hmi.h](./include/apl_hmi.h) | 六键短按/长按、童锁、组合键、灯和数码管。上电全灯 2 秒后熄灭，电源键半亮。显示和童锁通过 getter 和 setter 交换 |

编译时把头文件搜索路径指到 `include` 和 `config`。`tests/test_apl.c` 是宿主侧检查。

[simulator](./simulator/README.md) 是 Qt 仿真台。它持有一份 `struct apl` 并每 10 ms 调用 `apl_tick`。外壳是 Widgets，顶部是菜单栏。左侧切换前端，右侧是传感器、负载和状态。中间的面贴是 QML。前端只读 getter、只上报按键电平。库还没有的业务，仿真台不另外实现。编译和启动见 [使用说明](./simulator/使用说明.md)，接线见 [设计说明](./simulator/设计说明.md)。

Qt 仿真台仅用于测试本工程 APL，保留内置面板、传感器、固定负载、事件、供电和存储。说明见 [simulator 文档入口](simulator/README.md)。
