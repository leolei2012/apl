# Changelog

本文件记录项目的所有变更，按时间倒序排列。

格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [Semantic Versioning](https://semver.org/lang/zh-CN/)。

---

## [Unreleased]

### Changed

- 仿真器目录由 `emulator` 改名为 `simulator`。
- 六颗按键改为各自选配。`APL_HMI_COMPILE_KEY_PURIFY`、`APL_HMI_COMPILE_KEY_HUMIDITY`、`APL_HMI_COMPILE_KEY_TIMER`、`APL_HMI_COMPILE_KEY_LIGHT`、`APL_HMI_COMPILE_KEY_SPEED`、`APL_HMI_COMPILE_KEY_POWER` 默认都是 1。未编入的键不认短按、长按和组合，键灯读写视为非法。键编号和六路采样结构不变。
- 整机状态、UV 与负离子开关、风机档位、水位段数改到 `include/apl_type.h`。`apl_core.h` 包含这份枚举。人机的按键、单位和童锁仍在 `apl_hmi.h`。
- 去掉 `apl_core/`、`apl_hmi/` 目录。公开头放在 `include/`，实现放在 `src/`。`include/apl.h` 与 `src/apl.c` 组成 `struct apl`：`apl_init` 初始化主控和人机，`apl_tick` 先调用 `apl_core_tick` 再调用 `apl_hmi_tick`。库内仍不创建静态实例。人机数据手册改到 `docs/规格/datasheet.md`。
- 仿真器默认入口改为鼠标键盘面板。`simulator/tui.py` 合并鼠标、键盘和保持按下，并从 getter 绘制；`simulator/panel_sim.py` 仍编译加载库、持有宿主并驱动 tick。`--plain` 保留纯键盘面板。
- `simulator/panel_sim.py` 改为宿主仿真：加载 APL 库，注入按键与传感器，按 10 ms 调用 tick，画面从 getter 读出。库尚未实现的风干、配网、睡眠、自清洁不再在仿真器里另做一套。
- 宿主与 APL 双向的数据交换改为 getter 和 setter。`struct apl_core`、`struct apl_hmi` 里的字节只用于分配实例。硬件仍由宿主注入的 ops 提供。到时、关机、按键和组合键仍是回调。

### Added

- `include/apl_type.h`。主控和人机共用的枚举放在这里，两个模块仍不互相包含。
- `apl_init`、`apl_tick`。使用方持有一份 `struct apl`，里面是 `struct apl_core` 和 `struct apl_hmi`。
- `simulator/tui.py`、`simulator/test_tui.py`，以及 `simulator/README.md`、`simulator/使用说明.md`、`simulator/设计说明.md`。`test_tui.py` 用真实 DLL 检查鼠标路径。
- 主控读取：`apl_core_power_get`、`apl_core_fan_gear_get`，以及按编译开关出现的滤网计数和启用标志 getter。动态配置的修改入口仍未规定，启用标志没有 setter。
- 人机读写：键灯、氛围灯亮度与颜色、水位灯、数码管、童锁、净化/加湿启用标志，以及 `apl_hmi_key_callback_set`。
- `apl_core` 实现：`include/apl_core.h`、`src/apl_core.c`。开关机、秒定时、风机档位、滤网清零，以及 10 ms 一拍上的定时和水位数边沿。
- `apl_hmi` 实现：`include/apl_hmi.h`、`src/apl_hmi.c`。六键短按和 3 秒长按、童锁、组合键登记。显示属性由使用方通过 setter 写入。
- `tests/test_apl.c`：宿主侧检查上述行为。净化单开、加湿单开、两项都开三种编译都能过。
- 库目录 `apl_core`、`apl_hmi`。对外两个对象：`struct apl_core`、`struct apl_hmi`。库内不创建实例。
- `config/apl_config.h`：主控编译控制与默认值。`APL_COMPILE_PURIFY`、`APL_COMPILE_HUMIDIFY` 决定功能是否编入；`APL_DEFAULT_PURIFY`、`APL_DEFAULT_HUMIDIFY` 是尚无动态配置时的初始启用。
- `config/apl_hmi_config.h`：人机编译控制与默认值。`APL_HMI_PANEL` 在编译期选定面板。
- 规格草稿：`docs/规格/apl后端规格书.md`、`docs/规格/apl前端规格书.md`。
- `docs/规格/datasheet.md`：人机对象数据手册。六个按键各带一盏键灯和对应回调；灯有亮度，氛围灯另有 RGB，三位数字管只描述显示内容。
- `apl_core` 的风机对象：睡眠、一档、二档、三档、自动、极速。同一时刻一个档位。`apl_core_fan_gear_set` 设定档位。
- `apl_core` 的 UV、负离子：各自只有开和关。
- `apl_core` 的定时：`apl_core_timer_set` 以秒设定时间，到时调用宿主传入的 `on_timer` 一次。回调里可以开机，也可以关机。`apl_core_timer_get` 返回剩余秒数。
- `apl_core` 公开方法：`apl_core_power_on`、`apl_core_power_off`、`apl_core_timer_set`（单位秒，到时回调）。UV、负离子的开关由宿主以 setter 注入。
- `apl_core` 水位为四段：`APL_WATER_LEVEL_0` 到 `APL_WATER_LEVEL_4`。降到 0 时 `water_on_trigger` 一次，从 0 升起时 `water_on_recover` 一次。
- 宿主注入收成 `struct apl_core_ops`，由 `apl_core_init` 一次传入。不再逐个 `bind`。
- `apl_core_ops` 增加宿主提供的 `pm25_get`、`humidity_get`、`temperature_get`。
- `apl_core` 的滤网维护时间分为两套：净化滤芯 `apl_core_filter_purify_reset`，加湿滤网 `apl_core_filter_humidify_reset`。
- `apl_core_ops` 增加 `on_power_off`。进入关机时调用一次，宿主用来清除人机童锁。定时仍以秒为单位。
- 人机氛围灯即空气质量点阵灯条。灯光键长按为睡眠，电源键长按为结束风干。
- `apl_core` 的两项功能：空气净化、加湿。各自按编译开关编入，按动态配置启用。
- `apl_core` 的整机状态：`APL_POWER_OFF`（关机）、`APL_POWER_ON`（开机）。`apl_core_power_on` 进入开机，`apl_core_power_off` 进入关机。
