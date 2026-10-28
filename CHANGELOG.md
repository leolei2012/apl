# Changelog

## Web 仿真台

- APL Studio 增加独立仿真台：运行同一产品配置生成的 AP / 灯控 C 源码，支持按键手势、传感器注入、实际负载、虚拟时钟与通用运行信息槽位。
- 增加上掉电、AP 会话内存储回调、输入与状态事件分离、诊断与会话释放；补充真实 C 运行回归及使用说明。

本文件记录项目的所有变更，按时间倒序排列。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

### Added

- Studio 多品类配置 / 生成 / 验证 / 下载闭环：AP 与独立灯控模板、描述驱动表单、真实 C11 编译和模板消费、历史任务与哈希归档。目录、接口和验证范围由模板声明，规范位于 apl_studio/docs。

- APL Studio Web 平台第一版（`apl_studio`）：React / Fastify / Cordis 工作台、SQLite 产品与修订、功能与参数编辑、数据模型预览、C 接口契约和产品定义包导入导出。源码模板独立接入，网页交互预览仍只检查数据模型。

- APL 源码开发模式：绑定本地工程，监听保存后后台只构建 APL 动态库并重载，保留运行中的 simulator；支持原子保存、新增源文件、错误恢复与过期构建丢弃。SDK 导出固定为成功加载的源码 / 配置快照。

- APL 产品配置与 SDK 导出：三种机型预设、运行参数、六键编译裁剪、JSON v1 产品文件、本机 C 编译预览与 ZIP 源码 SDK。包内包含配置、源码、移植模板、示例、接入说明和 SHA-256 清单。
- `apl_sdk_test`：三种机型的产品读写、非法配置、SDK 消费程序编译运行、预览加载与失败保留；产品操作和生成设计文档位于 `simulator`。

- `link_command`、`link_snapshot`：类型在 `apl_type.h`，`apl_core_command` 执行并拒绝未启用功能的命令，`apl_core_get_snapshot` 给出快照，`apl_link_bind` 把人机接到主控。
- 主控业务：待机、运行、关机风干、产测四态；掉电记忆（`struct apl_core_memory`、`memory_save`、`apl_core_memory_restore`）；面板定时；极速停泵；睡眠；净化复合功能与 UV 联锁；多传感器空气质量与自动档；恒湿风速表与回差；缺水 30 分钟进风干；滤网寿命、重置与提示；Wi-Fi 配网；产测与恢复出厂设置。新增 ops：`get_pm10`、`get_hcho`、`get_tvoc`、`get_uv_interlock`、`set_fan`、`set_pump`、`memory_save`。
- 人机资源：13 个状态图标、水位灯颜色、蜂鸣器（`set_buzzer`、`apl_hmi_buzzer_play`）；面板逻辑（按键到命令、灯光与首次触摸唤醒、定时与恒湿设置预览、滤芯查询、按快照绘制、提示音）。
- `tests/test_link.c`：三种编译配置下检查上述行为。

### Changed

- 修复电源进入风干后短按无法恢复运行：风干中短按电源重新开机，长按结束风干。simulator 显示真实运行状态、风干倒计时和操作提示，诊断中的净化/加湿开关跟随当前值变化；新增两种面板的真实鼠标连续操作回归。

- simulator 界面重构为 APL Studio：统一浅色样式与四页导航，产品配置改为页内编辑，显式应用后才影响仿真。新增传感器精确输入、事件抽屉 / 筛选 / 保存、交付页与非阻塞操作反馈，两种面板均支持键盘操作。

- getter / setter 统一为动词在前：公开接口使用 `apl_core_get_xxx` / `apl_hmi_set_xxx`，ops 使用 `get_pm25`、`set_uv`、`set_anion` 等。
- Qt 面板直接读取 APL 联动显示；支持 PM2.5 / 湿度轮换、定时 / 恒湿设置、滤芯查询、OF / CL 字形。风机和加湿器接入真实 set_fan / set_pump 输出。

- 首次开机档位默认一档（`APL_DEFAULT_FAN_GEAR`）。
- `APL_CORE_OPAQUE_SIZE` 改为 320，`APL_HMI_OPAQUE_SIZE` 改为 1024。
- 滤网计数单位定为秒，只在运行中累计；运行中每秒读传感器。

### Removed

- 旧版通用外设、端口绑定、通用项目 JSON、任意 C 控制程序动态库与 MCP 扩展。当前工具只配置、预览和交付 APL，自身的产品文件及编译预览流程另见 Added。

- 终端宿主。删去 `simulator/panel_sim.py`、`simulator/tui.py`、`simulator/storage.py`、`simulator/apl.def`，以及 `test_tui.py`、`test_events.py`、`test_loads.py`、`test_power_memory.py`。移除原终端配套文档，显示槽位、供电存储和事件记录保留在 Qt 仿真台。

### Changed

- 修复电源进入风干后短按无法恢复运行：风干中短按电源重新开机，长按结束风干。simulator 显示真实运行状态、风干倒计时和操作提示，诊断中的净化/加湿开关跟随当前值变化；新增两种面板的真实鼠标连续操作回归。

- 主控配置头由 `config/apl_config.h` 改名为 `config/apl_core_config.h`。宏名不变。人机仍不包含它。
- 上电后已编入的键灯、氛围灯、四段水位灯和三位数字管全亮 2 秒，数字为 888，然后熄灭，电源键半亮。全亮是 255，半亮是 128。`apl_hmi_get_powerup_lamp` 在这 2 秒内返回 1。
- 仿真器目录由 `emulator` 改名为 `simulator`。
- 六颗按键改为各自选配。`APL_HMI_COMPILE_KEY_PURIFY`、`APL_HMI_COMPILE_KEY_HUMIDITY`、`APL_HMI_COMPILE_KEY_TIMER`、`APL_HMI_COMPILE_KEY_LIGHT`、`APL_HMI_COMPILE_KEY_SPEED`、`APL_HMI_COMPILE_KEY_POWER` 默认都是 1。未编入的键不认短按、长按和组合，键灯读写视为非法。键编号和六路采样结构不变。
- 整机状态、UV 与负离子开关、风机档位、水位段数改到 `include/apl_type.h`。`apl_core.h` 包含这份枚举。人机的按键、单位和童锁仍在 `apl_hmi.h`。
- 去掉 `apl_core/`、`apl_hmi/` 目录。公开头放在 `include/`，实现放在 `src/`。`include/apl.h` 与 `src/apl.c` 组成 `struct apl`：`apl_init` 初始化主控和人机，`apl_tick` 先调用 `apl_core_tick` 再调用 `apl_hmi_tick`。库内仍不创建静态实例。人机数据手册改到 `docs/规格/datasheet.md`。
- 宿主与 APL 双向的数据交换改为 getter 和 setter。`struct apl_core`、`struct apl_hmi` 里的字节只用于分配实例。硬件仍由宿主注入的 ops 提供。到时、关机、按键和组合键仍是回调。

### Added

- APL 源码开发模式：绑定本地工程，监听保存后后台只构建 APL 动态库并重载，保留运行中的 simulator；支持原子保存、新增源文件、错误恢复与过期构建丢弃。SDK 导出固定为成功加载的源码 / 配置快照。

- APL 仿真台保留暂停 / 单步、4 KB 文件存储、六个通用调试槽位及固定负载接线；两个面板改为内嵌资源。新增 APL 存储和供电验证。

- Qt 仿真台 `simulator/workbench`。平台持有一份 `struct apl`，每 10 ms 调用 `apl_tick`。外壳是 Qt Widgets，顶部是菜单栏，中间用 `QQuickWidget` 放可切换的 QML 前端。前端只读 getter、只上报按键电平。随带圆形面板和按键台。
- `include/apl_type.h`。主控和人机共用的枚举放在这里，两个模块仍不互相包含。
- `apl_init`、`apl_tick`。使用方持有一份 `struct apl`，里面是 `struct apl_core` 和 `struct apl_hmi`。
- 主控读取：`apl_core_get_power`、`apl_core_get_fan_gear`，以及按编译开关出现的滤网计数和启用标志 getter。动态配置的修改入口仍未规定，启用标志没有 setter。
- 人机读写：键灯、氛围灯亮度与颜色、水位灯、数码管、童锁、净化/加湿启用标志，以及 `apl_hmi_set_key_callback`。
- `apl_core` 实现：`include/apl_core.h`、`src/apl_core.c`。开关机、秒定时、风机档位、滤网清零，以及 10 ms 一拍上的定时和水位数边沿。
- `apl_hmi` 实现：`include/apl_hmi.h`、`src/apl_hmi.c`。六键短按和 3 秒长按、童锁、组合键登记。显示属性由使用方通过 setter 写入。
- `tests/test_apl.c`：宿主侧检查上述行为。净化单开、加湿单开、两项都开三种编译都能过。
- 库目录 `apl_core`、`apl_hmi`。对外两个对象：`struct apl_core`、`struct apl_hmi`。库内不创建实例。
- `config/apl_core_config.h`：主控编译控制与默认值。`APL_COMPILE_PURIFY`、`APL_COMPILE_HUMIDIFY` 决定功能是否编入；`APL_DEFAULT_PURIFY`、`APL_DEFAULT_HUMIDIFY` 是尚无动态配置时的初始启用。
- `config/apl_hmi_config.h`：人机编译控制与默认值。`APL_HMI_PANEL` 在编译期选定面板。
- 规格草稿：`docs/规格/apl后端规格书.md`、`docs/规格/apl前端规格书.md`。
- `docs/规格/datasheet.md`：人机对象数据手册。六个按键各带一盏键灯和对应回调；灯有亮度，氛围灯另有 RGB，三位数字管只描述显示内容。
- `apl_core` 的风机对象：睡眠、一档、二档、三档、自动、极速。同一时刻一个档位。`apl_core_set_fan_gear` 设定档位。
- `apl_core` 的 UV、负离子：各自只有开和关。
- `apl_core` 的定时：`apl_core_set_timer` 以秒设定时间，到时调用宿主传入的 `on_timer` 一次。回调里可以开机，也可以关机。`apl_core_get_timer` 返回剩余秒数。
- `apl_core` 公开方法：`apl_core_power_on`、`apl_core_power_off`、`apl_core_set_timer`（单位秒，到时回调）。UV、负离子的开关由宿主以 setter 注入。
- `apl_core` 水位为四段：`APL_WATER_LEVEL_0` 到 `APL_WATER_LEVEL_4`。降到 0 时 `water_on_trigger` 一次，从 0 升起时 `water_on_recover` 一次。
- 宿主注入收成 `struct apl_core_ops`，由 `apl_core_init` 一次传入。不再逐个 `bind`。
- `apl_core_ops` 增加宿主提供的 `get_pm25`、`get_humidity`、`get_temperature`。
- `apl_core` 的滤网维护时间分为两套：净化滤芯 `apl_core_filter_purify_reset`，加湿滤网 `apl_core_filter_humidify_reset`。
- `apl_core_ops` 增加 `on_power_off`。进入关机时调用一次，宿主用来清除人机童锁。定时仍以秒为单位。
- 人机氛围灯即空气质量点阵灯条。灯光键长按为睡眠，电源键长按为结束风干。
- `apl_core` 的两项功能：空气净化、加湿。各自按编译开关编入，按动态配置启用。
- `apl_core` 的整机状态：`APL_POWER_OFF`（关机）、`APL_POWER_ON`（开机）。`apl_core_power_on` 进入开机，`apl_core_power_off` 进入关机。
