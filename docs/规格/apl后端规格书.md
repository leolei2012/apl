# APL 后端规格书

| 项目     | 内容                                                                      |
| -------- | ------------------------------------------------------------------------- |
| 文档状态 | 草稿                                                                      |
| 子系统   | 主控（`apl_core`）                                                      |
| 配套文档 | [apl前端规格书](./apl前端规格书.md)                                        |
| 工程规范 | [架构规范](../规范/architecture.md)、[编码规范](../规范/coding_standard.md) |

口头称呼「后端」。代码和目录里的名字用 `core`。

本文记录主控的边界和业务规则。运行规则按《[机型交互逻辑（Excel提炼）](./机型交互逻辑（Excel提炼）.md)》实现；原表冲突或只写「按项目」的地方，选定的读法列在第 6 节，对应取值做成 `apl_core_config.h` 里的宏。故障策略仍待定。

---

## 1. 目的与范围

本文定义库对象 `struct apl_core` 的职责：业务判断、执行器输出，以及它交给人机的状态快照。

本仓库是库，库内不创建实例。使用方持有一份 `struct apl`。`src/apl.c` 在里面放 `struct apl_core` 和 `struct apl_hmi`，并完成初始化和按拍推进。两个对象互不包含、也不互相包含头文件，通过 `link_command` 和 `link_snapshot` 交换数据。

一套主控逻辑，运行中可动态配置。空气净化与加湿是否启用写在 `apl_core` 的动态配置里，不在编译期分成机型。`config/apl_core_config.h` 只提供编译控制和默认值。

主控负责：

- 接收人机的 `link_command` 并决定是否执行
- 开关机、模式、档位、恒湿、滤网、缺水、故障等业务判断
- 产生风机、雾化、UV、负离子等执行器的最终输出
- 周期发布 `link_snapshot`，供人机显示

主控不负责按键扫描和界面绘制。那些属于 [apl前端规格书](./apl前端规格书.md)。

净化、加湿和动态配置是 `apl_core` 的内部状态，不再拆成需要使用方分别实例化的对象。对象方法以 `apl_core_` 为前缀，`self` 为第一个参数。库内不放 `static struct apl`、`static struct apl_core` 或 `static struct apl_hmi`。

---

## 2. 术语

| 术语               | 含义                                                                         |
| ------------------ | ---------------------------------------------------------------------------- |
| 主控 /`apl_core` | 库对象`struct apl_core`。本文描述这个对象                                  |
| 人机 /`apl_hmi`  | 库对象`struct apl_hmi`，见前端规格书                                       |
| `link_command`   | 人机发给主控的操作                                                           |
| `link_snapshot`  | 主控发给人机的状态快照                                                       |
| 编译控制           | 某段功能代码是否编进程序。见`APL_COMPILE_PURIFY`、`APL_COMPILE_HUMIDIFY` |
| 默认值             | 尚无动态配置时的初始值。见`APL_DEFAULT_PURIFY`、`APL_DEFAULT_HUMIDIFY`   |
| 动态配置           | 运行中可修改的配置。已存在时以它为准，覆盖默认值                             |

协议结构以数据命名（`link_command`、`link_snapshot`），不把传输方向写进类型名。

---

## 3. 与人机的边界

```text
hmi → link_command → core（判断并执行）
core → link_snapshot → hmi（原样呈现）
```

规则：

1. 模式、档位、目标湿度、故障是否成立，只在主控判定。
2. 主控用 `link_snapshot` 报告当前状态，以及动态配置里启用了哪些功能。
3. 人机发来的命令对应功能未编译或动态配置未启用时，主控拒绝该命令，并在随后的快照中保持原状态。
4. 命令和快照的类型定义在 [`include/apl_type.h`](../../include/apl_type.h)，主控和人机共用这一份。字段见第 3.2 节，前端规格书第 5、6 节引用同一份定义。

### 3.1 与宿主的边界

读写硬件的 getter、setter，以及水位触发、恢复回调，由宿主实现，收在一张 ops 里，初始化时一次注入。APL 需要读传感器、写执行器或通知水位变化时调用它们。库不提供给宿主用来直接读硬件、写硬件的 get/set 方法，也不再逐个 `bind`。

```c
struct apl_core_ops
{
    enum apl_water_level (*get_water_level)(void *ctx);
    uint16_t (*get_pm25)(void *ctx);
    uint8_t (*get_humidity)(void *ctx);
    int16_t (*get_temperature)(void *ctx);
    void (*water_on_trigger)(struct apl_core *self, void *ctx);
    void (*water_on_recover)(struct apl_core *self, void *ctx);
    void (*set_uv)(enum apl_switch state, void *ctx);
    void (*set_anion)(enum apl_switch state, void *ctx);
    void (*on_power_off)(struct apl_core *self, void *ctx);
    uint16_t (*get_pm10)(void *ctx);         /* 选配 */
    uint16_t (*get_hcho)(void *ctx);         /* 选配，μg/m³ */
    uint16_t (*get_tvoc)(void *ctx);         /* 选配，μg/m³ */
    uint8_t (*get_uv_interlock)(void *ctx);  /* 空则视为闭合 */
    void (*set_fan)(enum apl_switch state, enum apl_fan_gear gear, void *ctx);
    void (*set_pump)(enum apl_switch state, void *ctx);
    void (*memory_save)(const struct apl_core_memory *memory, void *ctx);
};

void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx);
```

`apl_core_init` 把函数指针和 `ctx` 记在对象里。函数指针为空则不调用。库内不用 `s_self` 桥接。表的后七项是后加的，宿主先把整张表清零再填写。

宿主和 APL 双向交换的任何数据都走 getter 和 setter。硬件归宿主，仍放在上面的 ops 里，由 APL 调用。APL 自己持有的状态归库，宿主用库提供的 getter、setter 读写，不访问对象成员。`struct apl_core` 里的字节只给使用方分配实例。到时回调、关机回调和水位回调仍由 setter 或 ops 绑定，不提供取出函数指针的 getter。

宿主调用 APL 的业务方法仍然留在库内：`apl_core_power_on`、`apl_core_power_off`、`apl_core_get_power`、`apl_core_set_timer`、`apl_core_get_timer`、`apl_core_set_fan_gear`、`apl_core_get_fan_gear`，以及按编译开关出现的 `apl_core_filter_purify_reset`、`apl_core_get_filter_purify`、`apl_core_get_purify_enabled`、`apl_core_filter_humidify_reset`、`apl_core_get_filter_humidify`、`apl_core_get_humidify_enabled`。新增 `apl_core_command`、`apl_core_get_snapshot`、`apl_core_get_run_state`、`apl_core_get_memory`、`apl_core_memory_restore`、`apl_core_set_wifi_connected`。动态配置的修改入口尚未规定，因此启用标志只有 getter。滤网除重置外没有一般的 setter。水位不提供给宿主的 getter。UV、负离子仍只走宿主的 `set_uv`、`set_anion`。

### 3.2 link_command 与 link_snapshot

人机经 `apl_core_command` 交来一条 `struct apl_link_command`，主控返回 `APL_LINK_OK` 或 `APL_LINK_REJECTED`。被拒绝时状态不变。主控每拍可用 `apl_core_get_snapshot` 取一份 `struct apl_link_snapshot`。`src/apl.c` 在 `apl_tick` 里先推进主控，再把快照交给人机。

`struct apl_link_command` 只有两项：`type`（`enum apl_link_command_type`）和 `arg`（`uint8_t`，不用时为 0）。

| type | arg | 接受条件 | 动作 |
|---|---|---|---|
| `APL_LINK_CMD_POWER` | — | 待机、运行或风干 | 待机/风干→开机；运行→加湿启用时进入风干，否则关机 |
| `APL_LINK_CMD_DRY_STOP` | — | 风干 | 立即关机进入待机 |
| `APL_LINK_CMD_FAN_CYCLE` | — | 运行 | 睡眠→一→二→三→自动→睡眠；极速中按进入极速前的档位往下走 |
| `APL_LINK_CMD_FAN_SET` | 档位 | 运行，档位合法 | 直接设档 |
| `APL_LINK_CMD_TURBO_TOGGLE` | — | 运行，`APL_CORE_HAS_TURBO` | 进入或退出极速，退出回到原档 |
| `APL_LINK_CMD_SLEEP_TOGGLE` | — | 运行 | 进入或退出睡眠档，退出回到原档 |
| `APL_LINK_CMD_TIMER_SET` | 0～24 小时 | 运行 | 设定面板定时，0 取消。到时关机进入待机 |
| `APL_LINK_CMD_PURIFY_TOGGLE` | — | 净化启用，待机或运行 | 净化复合功能（UV、负离子）开关 |
| `APL_LINK_CMD_HUMIDIFY_TOGGLE` | — | 加湿启用，待机或运行；打开时水位有效 | 加湿开关 |
| `APL_LINK_CMD_HUMIDITY_TARGET` | 0（OF）或 40～80、5 步进 | 加湿启用，待机或运行；非 OF 时水位有效 | 设定恒湿目标 |
| `APL_LINK_CMD_FILTER_RESET` | — | 至少一项启用，待机或运行 | 滤网重置，见第 5.10 节 |
| `APL_LINK_CMD_WIFI_PAIR` | — | 待机或运行 | 清除网络并进入配网 |
| `APL_LINK_CMD_FACTORY_ENTER` | — | 待机，上电未满 `APL_CORE_FACTORY_WINDOW_S` 秒 | 进入产测 |
| `APL_LINK_CMD_FACTORY_EXIT` | — | 产测 | 恢复出厂设置，进入待机 |

「水位有效」指还没有读到合法水位，或水位不是 0 段。功能未编译或未启用时，对应命令一律拒绝。

`struct apl_link_snapshot` 字段：

| 字段 | 含义 |
|---|---|
| `run_state` | `APL_RUN_STANDBY`、`APL_RUN_ON`、`APL_RUN_DRY`、`APL_RUN_FACTORY` |
| `fan_gear` | 用户档位 |
| `fan_output` | 实际输出档位（自动、恒湿、风干折算后） |
| `aq_level` | 空气质量综合等级，优、良、中度、重度 |
| `water_level` | 水位段数 0～4 |
| `water_alert` | 缺水提醒：无、闪烁、常亮 |
| `wifi` | 未联网、配网中、已联网 |
| `uv`、`anion`、`pump` | 执行器当前输出 |
| `beep`、`beep_seq` | 最近一次提示音及其序号。序号变化时人机播放一次 |
| `timer_left_s` | 剩余定时秒数 |
| `dry_left_s` | 剩余风干秒数 |
| `pm25` | μg/m³ |
| `purify_enabled`、`humidify_enabled` | 动态配置当前启用的功能 |
| `purify_on`、`humidify_on` | 净化复合功能、加湿是否开着 |
| `humidity`、`humidity_target` | 环境湿度；恒湿目标，0 为 OF |
| `sleep` | 睡眠中：静音、熄屏 |
| `filter_purify_left`、`filter_humidify_left` | 滤网剩余 1～99，0 为到期 |
| `filter_reset_seq`、`filter_reset_forced` | 重置序号；最近一次是否未到期强制重置 |
| `uv_fault` | UV 联锁断开过，直到再次打开净化 |
| `factory_done` | 刚完成恢复出厂设置，人机显示 CL |

未启用的功能，对应字段为 0。

---

## 4. 配置

[`config/apl_core_config.h`](../../config/apl_core_config.h) 只做主控的编译控制和默认值。面板裁剪在人机自己的 [`config/apl_hmi_config.h`](../../config/apl_hmi_config.h)，不写进本文件。本机当前启用哪些功能，以动态配置为准。没有动态配置时，用头文件里的默认值建立第一份。

### 4.1 编译控制

决定对应代码是否进入程序。业务代码用 `#if APL_COMPILE_PURIFY`、`#if APL_COMPILE_HUMIDIFY` 裁剪。未编译的功能不能被动态配置打开，也不出现在快照的可操作项中。

| 宏                       | 含义                         | 未设置时 |
| ------------------------ | ---------------------------- | -------- |
| `APL_COMPILE_PURIFY`   | 1 = 编译空气净化，0 = 不编译 | 1        |
| `APL_COMPILE_HUMIDIFY` | 1 = 编译加湿，0 = 不编译     | 1        |

取值只能是 0 或 1。两项都为 0 时编译报错。

### 4.2 默认值

动态配置尚不存在时的初始启用状态。已有动态配置后，运行逻辑不再读取这两项。

| 宏                       | 含义                               | 未设置时 |
| ------------------------ | ---------------------------------- | -------- |
| `APL_DEFAULT_PURIFY`   | 1 = 默认启用空气净化，0 = 默认关闭 | 1        |
| `APL_DEFAULT_HUMIDIFY` | 1 = 默认启用加湿，0 = 默认关闭     | 1        |

取值只能是 0 或 1。对应功能未编译时，默认值必须为 0，否则编译报错。

运行参数的默认值也放在这个头文件，不把默认值写进业务模块：

| 宏 | 默认 | 含义 |
|---|---|---|
| `APL_DEFAULT_FAN_GEAR` | 1（一档） | 首次开机档位 |
| `APL_CORE_DRY_S` | 3600 | 风干时长，秒 |
| `APL_CORE_DRY_FAN_GEAR` | 1（一档） | 风干转速 |
| `APL_CORE_FILTER_LIFE_H` | 2200 | 滤网寿命，小时 |
| `APL_CORE_FILTER_SHARED` | 1 | 一体机两套滤网共用计时 |
| `APL_CORE_WATER_SHORTAGE_RUN_S` | 1800 | 缺水后继续运行，秒 |
| `APL_CORE_WATER_BLINK_S` | 300 | 缺水闪烁，秒 |
| `APL_CORE_HUMIDITY_HYSTERESIS` | 3 | 恒湿回差，百分点 |
| `APL_CORE_FACTORY_WINDOW_S` | 30 | 上电后允许进产测，秒 |
| `APL_CORE_FACTORY_FILTER_LEFT_S` | 60 | 产测把滤网置为还剩这么多秒 |
| `APL_CORE_WIFI_PAIR_S` | 180 | 配网超时，秒 |
| `APL_CORE_FACTORY_CL_S` | 2 | CL 显示，秒 |
| `APL_CORE_HAS_TURBO` | 1 | 配置极速 |

### 4.3 动态配置

运行中可修改。净化与加湿是否启用各自独立，改完即作为当前配置，不必重新编译。

约束：

- 只能启用已编译的功能。
- 快照、判断和执行器输出都看当前动态配置。
- 两项都启用时，输出如何合并见第 5.5 节。

动态配置的修改入口、保存位置、掉电后是否保留，待定。掉电记忆见第 5.1 节。

---

## 5. 业务规则

已写明的条目可以作为实现依据。标为待定的条目，实现不以推测代替。

整机状态、UV 与负离子开关、风机档位、水位段数的枚举在 [`include/apl_type.h`](../../include/apl_type.h)。人机自己的按键、单位和童锁枚举在 [`include/apl_hmi.h`](../../include/apl_hmi.h)。

### 5.1 电器公共行为

运行状态（`enum apl_run_state`）同一时刻只有一个：

| 状态 | 含义 | `apl_core_get_power` |
|---|---|---|
| `APL_RUN_STANDBY` | 待机（关机） | `APL_POWER_OFF` |
| `APL_RUN_ON` | 运行 | `APL_POWER_ON` |
| `APL_RUN_DRY` | 关机风干，只在加湿启用时出现 | `APL_POWER_ON` |
| `APL_RUN_FACTORY` | 产测 | `APL_POWER_ON` |

`apl_core_power_on` 进入运行。首次开机（没有手动开过机，或刚恢复出厂设置）档位设为 `APL_DEFAULT_FAN_GEAR`；非首次保持记忆的档位。开机时已缺水，直接按第 5.8 节缺水处理。

`apl_core_power_off` 进入待机。从非待机进入待机时，清掉剩余定时、恒湿目标（回到 OF）和配网，退出极速，然后调用 ops 里的 `on_power_off` 一次。已经待机时再调用，不再发回调。童锁在人机，宿主在 `on_power_off` 里调用 `apl_hmi_set_child_lock` 把它关掉。宿主直接调用 `apl_core_power_off` 不经过风干；面板的电源键走 `APL_LINK_CMD_POWER`。

关机风干：运行中收到 `APL_LINK_CMD_POWER` 且加湿启用时，进入风干，停泵，风机按 `APL_CORE_DRY_FAN_GEAR` 运行 `APL_CORE_DRY_S` 秒，到时关机。风干中 `APL_LINK_CMD_DRY_STOP` 立即关机。风干中电源键短按重新开机，清除风干倒计时并恢复用户档位；长按仍立即结束风干进入待机。

定时是 `apl_core` 的内部对象。宿主用 `apl_core_set_timer` 可以做定时开机或关机；面板用 `APL_LINK_CMD_TIMER_SET` 按小时设定，到时整机关机进入待机，不走风干。

掉电记忆：需要保存的内容是 `struct apl_core_memory`：滤网累计秒数、档位、是否在运行、是否用过、净化和加湿开关、产测未完成标志。内容变化时主控调用 ops 的 `memory_save`；滤网计时每满一小时存一次。宿主上电后在第一拍之前调用 `apl_core_memory_restore`：产测未完成则先恢复出厂设置；掉电前在运行则自动开机；风干中掉电不算运行，保持待机。定时、童锁、恒湿不保存。

公开方法：

```c
void apl_core_power_on(struct apl_core *self);
void apl_core_power_off(struct apl_core *self);
enum apl_power apl_core_get_power(const struct apl_core *self);
void apl_core_set_timer(struct apl_core *self,
                        uint32_t seconds,
                        void (*on_timer)(struct apl_core *self, void *ctx),
                        void *ctx);
uint32_t apl_core_get_timer(struct apl_core *self);
```

`apl_core_power_on` 开机。`apl_core_power_off` 关机。`apl_core_get_power` 读取整机状态。`apl_core_set_timer` 设置定时，`seconds` 的单位是秒，`on_timer` 到时调用，并传入 `self` 和 `ctx`。定时开机时，回调里调用 `apl_core_power_on`。定时关机时，回调里调用 `apl_core_power_off`。`on_timer` 为空则到时不调用。再次调用按新的秒数和回调重新设定。`apl_core_get_timer` 返回剩余的计时秒数；没有定时时返回 `0`。面板按小时设置走 `APL_LINK_CMD_TIMER_SET`，1 小时等于 3600 秒，0 取消。传入 `0` 表示没有定时，到时回调不触发。

使用方每 10 ms 调用一次 `apl_tick`，其中先调用 `apl_core_tick`。剩余秒数每 100 拍减 1。关机期间定时也走，到时回调里可以开机。

使用方每 10 ms 调用一次 `apl_tick`，其中先调用 `apl_core_tick`。剩余秒数每 100 拍减 1。关机期间宿主定时也走，到时回调里可以开机。

### 5.2 风机

风机是 `apl_core` 的内部对象。用户档位同一时刻只有一个：

| 档位 | 含义 |
|---|---|
| `APL_FAN_GEAR_SLEEP` | 睡眠：静音，人机 3 秒后熄屏 |
| `APL_FAN_GEAR_1` | 一档 |
| `APL_FAN_GEAR_2` | 二档 |
| `APL_FAN_GEAR_3` | 三档 |
| `APL_FAN_GEAR_AUTO` | 自动 |
| `APL_FAN_GEAR_TURBO` | 极速 |

```c
void apl_core_set_fan_gear(struct apl_core *self, enum apl_fan_gear gear);
enum apl_fan_gear apl_core_get_fan_gear(const struct apl_core *self);
```

`apl_core_set_fan_gear` 记下用户档位。实际输出 `fan_output` 按下列顺序决定，有变化时调用 ops 的 `set_fan`：

1. 待机：风机停。风干：`APL_CORE_DRY_FAN_GEAR`。
2. 极速、睡眠：原样输出。
3. 恒湿生效（加湿开、目标不是 OF）：按第 5.4 节恒湿风速表；一体机自动档取恒湿档和空气质量档中较大的一个。
4. 自动：净化启用时按空气质量，优一档、良二档、中度和重度三档；只加湿时二档。
5. 其余按用户档位。

初始档位为 `APL_DEFAULT_FAN_GEAR`。关机不改档位（极速除外，关机退出极速）。

### 5.3 空气净化

`APL_COMPILE_PURIFY` 为 1 时编入，动态配置启用后才运行。净化复合功能由 `APL_LINK_CMD_PURIFY_TOGGLE` 开关，默认关。运行中且开着时负离子开；UV 另需安全微动开关闭合（`get_uv_interlock`）。UV 开着时联锁断开，立即关 UV，`uv_fault` 置 1，发 `APL_BEEP_UV_ALARM`；重新打开净化才清除。

空气质量每秒取一次，取各传感器中最差一项：

| 传感器 | 优 | 良 | 中度污染 | 重度污染 |
|---|---|---|---|---|
| PM2.5（μg/m³） | ≤ 35 | 36～75 | 76～150 | > 150 |
| PM10（μg/m³） | ≤ 40 | 41～70 | 71～150 | > 150 |
| 甲醛（μg/m³） | ≤ 50 | — | 51～80 | > 80 |
| TVOC（μg/m³） | ≤ 300 | 301～500 | 501～1000 | > 1000 |

PM10、甲醛、TVOC 是选配，ops 为空就不参与。

### 5.4 加湿

`APL_COMPILE_HUMIDIFY` 为 1 时编入，动态配置启用后才运行。加湿开关默认开。水泵输出条件：运行、加湿开、不是极速、不缺水、恒湿未达标。

恒湿目标 40%～80%、5% 步进，或 OF（0）。开机、断电默认 OF。差值 = 目标 − 环境湿度：

| 差值 | 风速 |
|---|---|
| < 10 | 一档 |
| 10～19 | 二档 |
| ≥ 20 | 三档 |
| ≤ 0（已达标） | 一档 |

达到目标停泵；湿度回落到目标减 `APL_CORE_HUMIDITY_HYSTERESIS` 以下再开泵。

### 5.5 两项同时启用

一套风机、一套输出。风机按第 5.2 节合并；水泵按第 5.4 节；UV、负离子按第 5.3 节，互不影响。缺水只停加湿，净化照常。

### 5.6 故障

待定。每条故障需要写明：判定条件、影响哪一项能力、恢复条件、快照中的故障码。

### 5.7 UV 与负离子

UV 和负离子各是 `apl_core` 的一个内部对象，使用方不单独实例化。各自只有开和关，没有档位。

| 对象      | 状态                                  |
| --------- | ------------------------------------- |
| `uv`    | `APL_SWITCH_OFF`、`APL_SWITCH_ON` |
| `anion` | `APL_SWITCH_OFF`、`APL_SWITCH_ON` |

开关是 `struct apl_core_ops` 里的 `set_uv`、`set_anion`，由宿主实现。APL 要改变开关时调用它们。`state` 只能是 `APL_SWITCH_OFF` 或 `APL_SWITCH_ON`。何时打开、关闭，见第 5.3 节。

### 5.8 水位检测

水位检测是 `apl_core` 的内部对象，使用方不单独实例化。水位为四段，`get_water_level` 返回当前点亮段数。

| 返回值                | 含义             |
| --------------------- | ---------------- |
| `APL_WATER_LEVEL_0` | 四段都灭         |
| `APL_WATER_LEVEL_1` | 点亮最低一段     |
| `APL_WATER_LEVEL_2` | 由低到高点亮两段 |
| `APL_WATER_LEVEL_3` | 由低到高点亮三段 |
| `APL_WATER_LEVEL_4` | 四段全亮         |

水位读取和两路回调是 `struct apl_core_ops` 里的 `get_water_level`、`water_on_trigger`、`water_on_recover`。`get_water_level` 由宿主实现。APL 需要当前水位时调用它，不在库内保存一份给宿主读取的水位。`apl_core_tick` 每拍采样一次。第一次采到的合法水位只作为边沿起点，不发回调。从 1 及以上降到 0 时，调用 `water_on_trigger` 一次。从 0 升到 1 及以上时，调用 `water_on_recover` 一次。调用时把 `self` 和 `ctx` 原样传入。只在 1 到 4 之间变化时，不调用这两路回调。返回值不是 0 到 4 时，这一拍丢弃，不改起点。

加湿启用时的缺水逻辑：

1. 运行中降到 0 段（或开机时已是 0 段）进入缺水：停泵，风机照常；`water_alert` 为闪烁，发 `APL_BEEP_WATER_SHORTAGE`（睡眠中不响）。
2. 缺水 `APL_CORE_WATER_BLINK_S` 秒后 `water_alert` 改为常亮。
3. 缺水 `APL_CORE_WATER_SHORTAGE_RUN_S` 秒仍未加水，进入风干。
4. 回到 1 段以上即解除缺水，恢复加湿。抽出水箱读作 0 段，不重新计时。
5. 升到 4 段时发 `APL_BEEP_WATER_FULL`（睡眠中不响）。只有段数时 85% 与 100% 无法区分，按水满提示。

### 5.9 PM2.5、湿度、温度

这三个读数由宿主通过 `struct apl_core_ops` 传入，APL 需要时调用，不在库内另存一份给宿主读取。

| ops                 | 返回         | 单位                 |
| ------------------- | ------------ | -------------------- |
| `get_pm25`        | `uint16_t` | μg/m³，整数        |
| `get_humidity`    | `uint8_t`  | 相对湿度，整数百分比 |
| `get_temperature` | `int16_t`  | 摄氏度，整数         |

运行中每秒读一次：净化启用时读 PM2.5（及选配的 PM10、甲醛、TVOC），加湿启用时读湿度。温度目前没有业务使用。取值范围待定。

### 5.10 滤网维护时间

滤网维护时间有两套，都是 `apl_core` 的内部对象，使用方不单独实例化。

| 对象         | 编译条件                      | 重置 | 读取 |
| ------------ | ----------------------------- | ---- | ---- |
| 净化滤芯计时 | `APL_COMPILE_PURIFY` 为 1   | `apl_core_filter_purify_reset` | `apl_core_get_filter_purify` |
| 加湿滤网计时 | `APL_COMPILE_HUMIDIFY` 为 1 | `apl_core_filter_humidify_reset` | `apl_core_get_filter_humidify` |

计时单位为秒，只在运行（含产测）中累计，启用的那一套才走。寿命 `APL_CORE_FILTER_LIFE_H` 小时；快照给出剩余 1～99（向上取整），到期为 0。宿主的 `*_reset` 只清零计数；面板走 `APL_LINK_CMD_FILTER_RESET`：

1. 待机或运行都可以重置。
2. 一体机且 `APL_CORE_FILTER_SHARED` 为 1 时两套一起清零；为 0 时清用得多的一套。单功能时清启用的那一套。
3. 有到期的：`filter_reset_forced` 为 0，发 `APL_BEEP_FILTER_RESET`（500 ms）。都未到期：`filter_reset_forced` 为 1，发 `APL_BEEP_FILTER_FORCED`（300 ms），人机让红灯闪 3 次。
4. `filter_reset_seq` 加 1。

功能是否启用分别由 `apl_core_get_purify_enabled`、`apl_core_get_humidify_enabled` 读取，修改入口尚未规定。

### 5.11 Wi-Fi

`APL_LINK_CMD_WIFI_PAIR` 进入配网，`APL_CORE_WIFI_PAIR_S` 秒未联网退出。宿主用 `apl_core_set_wifi_connected` 报告联网状态。关机、风干退出配网。

### 5.12 产测与恢复出厂设置

1. 上电未满 `APL_CORE_FACTORY_WINDOW_S` 秒、待机时，`APL_LINK_CMD_FACTORY_ENTER` 进入产测：写待恢复标志并保存，记下真实滤网计时，把两套计时置为离寿命终点还剩 `APL_CORE_FACTORY_FILTER_LEFT_S` 秒，发 `APL_BEEP_FACTORY`。
2. 产测中 `memory_save` 给出的仍是真实滤网计时。
3. `APL_LINK_CMD_FACTORY_EXIT` 恢复出厂设置：滤网回到真实值，档位回到默认，清除定时、配网、睡眠、极速、恒湿和净化开关，加湿回到开，标记为首次开机，清待恢复标志，进入待机，调用 `on_power_off`（宿主借此清童锁），`factory_done` 保持 `APL_CORE_FACTORY_CL_S` 秒。
4. 上电时 `apl_core_memory_restore` 看到待恢复标志，先完成第 3 步。

---

## 6. 待确认

原表冲突或只写「按项目」的地方，实现取下列读法。能廉价做成宏的已做成宏。

1. 首次开机档位取一档（三张机型表、更改记录第 2、7 条），汇总表写二档。宏 `APL_DEFAULT_FAN_GEAR`。
2. 定时到期直接待机，不走风干。原表只写「关机进入待机」。
3. 原表未规定风干中电源键短按；当前采用短按恢复运行、长按结束风干，避免无法通过正常电源操作恢复使用。
4. 缺水后 30 分钟内停泵、风机照常（水位表备注「缺水时停止加湿输出」）。
5. 自动档与空气质量的对应（优一、良二、中重三），只加湿时自动档二档。原表未给。
6. 恒湿生效时覆盖手动一至三档；睡眠、极速不覆盖。一体机自动档取较大者。
7. 恒湿回差 3 个百分点，宏 `APL_CORE_HUMIDITY_HYSTERESIS`。
8. 风干转速取一档，宏 `APL_CORE_DRY_FAN_GEAR`。
9. 水位只有段数，85% 与 100% 合成「四段 = 水满」一次提示。`APL_BEEP_WATER_85` 保留未用。
10. 加湿开关默认开，净化复合功能默认关。
11. 空气质量分级端点归较好的一档（TVOC 0.3、0.5 两档原表重叠）；甲醛 0.050 以上直接中度。
12. 一体机两套滤网默认共用计时，宏 `APL_CORE_FILTER_SHARED`。
13. 空净滤网重置条件取「运行或待机」（汇总表），空净表写仅运行。
14. 强制重置提示音按机型表 300 ms，汇总表只写「蜂鸣一声」。
15. 配网超时 180 秒，宏 `APL_CORE_WIFI_PAIR_S`。
16. UV 联锁报警音型（3 声 100 ms）原表未给；恢复要求重新打开净化。
17. 加湿器净化键「切换加湿运行模式」：模式未定义，加湿器上净化未启用，命令被拒绝。
18. 记忆内容：睡眠作为档位一起记忆；灯光、恒湿、定时、童锁不记忆。
19. 动态配置的修改入口、保存位置仍待定。
20. 第 5.6 节故障仍待定，原表没有故障定义。
21. 温度读数没有业务使用。
