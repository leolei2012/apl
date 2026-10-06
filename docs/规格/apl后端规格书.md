# APL 后端规格书

| 项目     | 内容                                                                      |
| -------- | ------------------------------------------------------------------------- |
| 文档状态 | 草稿                                                                      |
| 子系统   | 主控（`apl_core`）                                                      |
| 配套文档 | [apl前端规格书](./apl前端规格书.md)                                        |
| 工程规范 | [架构规范](../规范/architecture.md)、[编码规范](../规范/coding_standard.md) |

口头称呼「后端」。代码和目录里的名字用 `core`。

本文只记录已经对齐的边界。运行规则、阈值、故障策略尚未确认，相关条目标为「待定」，不作为实现依据。

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
4. 快照字段与命令种类的正式定义，待人机规格书第 5、6 节一起确定。两份文档使用同一份定义。

### 3.1 与宿主的边界

读写硬件的 getter、setter，以及水位触发、恢复回调，由宿主实现，收在一张 ops 里，初始化时一次注入。APL 需要读传感器、写执行器或通知水位变化时调用它们。库不提供给宿主用来直接读硬件、写硬件的 get/set 方法，也不再逐个 `bind`。

```c
struct apl_core_ops
{
    enum apl_water_level (*water_level_get)(void *ctx);
    uint16_t (*pm25_get)(void *ctx);
    uint8_t (*humidity_get)(void *ctx);
    int16_t (*temperature_get)(void *ctx);
    void (*water_on_trigger)(struct apl_core *self, void *ctx);
    void (*water_on_recover)(struct apl_core *self, void *ctx);
    void (*uv_set)(enum apl_switch state, void *ctx);
    void (*anion_set)(enum apl_switch state, void *ctx);
    void (*on_power_off)(struct apl_core *self, void *ctx);
};

void apl_core_init(struct apl_core *self, const struct apl_core_ops *ops, void *ctx);
```

`apl_core_init` 把函数指针和 `ctx` 记在对象里。函数指针为空则不调用。库内不用 `s_self` 桥接。

宿主和 APL 双向交换的任何数据都走 getter 和 setter。硬件归宿主，仍放在上面的 ops 里，由 APL 调用。APL 自己持有的状态归库，宿主用库提供的 getter、setter 读写，不访问对象成员。`struct apl_core` 里的字节只给使用方分配实例。到时回调、关机回调和水位回调仍由 setter 或 ops 绑定，不提供取出函数指针的 getter。

宿主调用 APL 的业务方法仍然留在库内：`apl_core_power_on`、`apl_core_power_off`、`apl_core_power_get`、`apl_core_timer_set`、`apl_core_timer_get`、`apl_core_fan_gear_set`、`apl_core_fan_gear_get`，以及按编译开关出现的 `apl_core_filter_purify_reset`、`apl_core_filter_purify_get`、`apl_core_purify_enabled_get`、`apl_core_filter_humidify_reset`、`apl_core_filter_humidify_get`、`apl_core_humidify_enabled_get`。动态配置的修改入口尚未规定，因此启用标志只有 getter。滤网除重置外没有一般的 setter。水位不提供给宿主的 getter。UV、负离子仍只走宿主的 `uv_set`、`anion_set`。

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

后续运行参数的默认值也放在这个头文件，不把默认值写进业务模块。

### 4.3 动态配置

运行中可修改。净化与加湿是否启用各自独立，改完即作为当前配置，不必重新编译。

约束：

- 只能启用已编译的功能。
- 快照、判断和执行器输出都看当前动态配置。
- 两项都启用时，输出如何合并见第 5.5 节。

修改入口、保存位置、掉电后是否保留，待定。

---

## 5. 业务规则

已写明的条目可以作为实现依据。标为待定的条目，实现不以推测代替。

整机状态、UV 与负离子开关、风机档位、水位段数的枚举在 [`include/apl_type.h`](../../include/apl_type.h)。人机自己的按键、单位和童锁枚举在 [`include/apl_hmi.h`](../../include/apl_hmi.h)。

### 5.1 电器公共行为

整机状态同一时刻只有一个：

| 状态              | 含义 |
| ----------------- | ---- |
| `APL_POWER_OFF` | 关机 |
| `APL_POWER_ON`  | 开机 |

`apl_core_power_on` 进入 `APL_POWER_ON`。`apl_core_power_off` 进入 `APL_POWER_OFF`。从开机进入关机时，调用 ops 里的 `on_power_off` 一次，把 `self` 和 `ctx` 传入。已经是关机时再调用 `apl_core_power_off`，不再发这次回调。断电来不及调用回调；定时和童锁不掉电保存，下次上电即为清除后的状态。童锁在人机，宿主在 `on_power_off` 里调用 `apl_hmi_child_lock_set` 把它关掉。定时在主控，`apl_core_power_off` 自己把剩余时间清掉，再发回调。

定时是 `apl_core` 的内部对象，使用方不单独实例化。到设定的时间后调用设定时传入的回调，调用一次。回调里由宿主决定调用 `apl_core_power_on` 或 `apl_core_power_off`，因此同一定时可以做定时开机，也可以做定时关机。

公开方法：

```c
void apl_core_power_on(struct apl_core *self);
void apl_core_power_off(struct apl_core *self);
enum apl_power apl_core_power_get(const struct apl_core *self);
void apl_core_timer_set(struct apl_core *self,
                        uint32_t seconds,
                        void (*on_timer)(struct apl_core *self, void *ctx),
                        void *ctx);
uint32_t apl_core_timer_get(struct apl_core *self);
```

`apl_core_power_on` 开机。`apl_core_power_off` 关机。`apl_core_power_get` 读取整机状态。`apl_core_timer_set` 设置定时，`seconds` 的单位是秒，`on_timer` 到时调用，并传入 `self` 和 `ctx`。定时开机时，回调里调用 `apl_core_power_on`。定时关机时，回调里调用 `apl_core_power_off`。`on_timer` 为空则到时不调用。再次调用按新的秒数和回调重新设定。`apl_core_timer_get` 返回剩余的计时秒数；没有定时时返回 `0`。面板按小时设置时，由宿主换算，1 小时等于 3600 秒。`seconds` 的取值范围，以及如何取消定时，待定。传入 `0` 只表示当前没有剩余时间，到时回调不触发。

使用方每 10 ms 调用一次 `apl_tick`，其中先调用 `apl_core_tick`。剩余秒数每 100 拍减 1。关机期间定时也走，到时回调里可以开机。

其余公共行为待定：睡眠，以及掉电后需要记住的运行参数。

### 5.2 风机

风机是 `apl_core` 的内部对象，使用方不单独实例化。同一时刻只有一个档位。

| 档位                   | 含义 |
| ---------------------- | ---- |
| `APL_FAN_GEAR_SLEEP` | 睡眠 |
| `APL_FAN_GEAR_1`     | 一档 |
| `APL_FAN_GEAR_2`     | 二档 |
| `APL_FAN_GEAR_3`     | 三档 |
| `APL_FAN_GEAR_AUTO`  | 自动 |
| `APL_FAN_GEAR_TURBO` | 极速 |

```c
void apl_core_fan_gear_set(struct apl_core *self, enum apl_fan_gear gear);
enum apl_fan_gear apl_core_fan_gear_get(const struct apl_core *self);
```

`apl_core_fan_gear_set` 把风机设为 `gear`。`gear` 只能是上表中的一个档位。`apl_core_fan_gear_get` 读取当前档位。自动档按什么调节、各档对应的输出，待定。初始档位为二档。关机不改档位。

### 5.3 空气净化

空气净化是 `apl_core` 的一项功能。`APL_COMPILE_PURIFY` 为 1 时编入程序，动态配置启用后才运行。使用方不把它单独实例化。

功能内部如何判断、如何给出输出，待定。

### 5.4 加湿

加湿是 `apl_core` 的一项功能。`APL_COMPILE_HUMIDIFY` 为 1 时编入程序，动态配置启用后才运行。使用方不把它单独实例化。

功能内部如何判断、如何给出输出，待定。

### 5.5 两项同时启用

待定。仅净化与加湿都已编译且动态配置都启用时适用。如何合并成唯一的执行器输出，写在本节。风机档位只有一个，合并结果写入第 5.2 节的风机对象。

### 5.6 故障

待定。每条故障需要写明：判定条件、影响哪一项能力、恢复条件、快照中的故障码。

### 5.7 UV 与负离子

UV 和负离子各是 `apl_core` 的一个内部对象，使用方不单独实例化。各自只有开和关，没有档位。

| 对象      | 状态                                  |
| --------- | ------------------------------------- |
| `uv`    | `APL_SWITCH_OFF`、`APL_SWITCH_ON` |
| `anion` | `APL_SWITCH_OFF`、`APL_SWITCH_ON` |

开关是 `struct apl_core_ops` 里的 `uv_set`、`anion_set`，由宿主实现。APL 要改变开关时调用它们。`state` 只能是 `APL_SWITCH_OFF` 或 `APL_SWITCH_ON`。何时打开、关闭，待定。

### 5.8 水位检测

水位检测是 `apl_core` 的内部对象，使用方不单独实例化。水位为四段，`water_level_get` 返回当前点亮段数。

| 返回值                | 含义             |
| --------------------- | ---------------- |
| `APL_WATER_LEVEL_0` | 四段都灭         |
| `APL_WATER_LEVEL_1` | 点亮最低一段     |
| `APL_WATER_LEVEL_2` | 由低到高点亮两段 |
| `APL_WATER_LEVEL_3` | 由低到高点亮三段 |
| `APL_WATER_LEVEL_4` | 四段全亮         |

水位读取和两路回调是 `struct apl_core_ops` 里的 `water_level_get`、`water_on_trigger`、`water_on_recover`。`water_level_get` 由宿主实现。APL 需要当前水位时调用它，不在库内保存一份给宿主读取的水位。`apl_core_tick` 每拍采样一次。第一次采到的合法水位只作为边沿起点，不发回调。从 1 及以上降到 0 时，调用 `water_on_trigger` 一次。从 0 升到 1 及以上时，调用 `water_on_recover` 一次。调用时把 `self` 和 `ctx` 原样传入。只在 1 到 4 之间变化时，不调用这两路回调。返回值不是 0 到 4 时，这一拍丢弃，不改起点。

### 5.9 PM2.5、湿度、温度

这三个读数由宿主通过 `struct apl_core_ops` 传入，APL 需要时调用，不在库内另存一份给宿主读取。

| ops                 | 返回         | 单位                 |
| ------------------- | ------------ | -------------------- |
| `pm25_get`        | `uint16_t` | μg/m³，整数        |
| `humidity_get`    | `uint8_t`  | 相对湿度，整数百分比 |
| `temperature_get` | `int16_t`  | 摄氏度，整数         |

取值范围待定。

### 5.10 滤网维护时间

滤网维护时间有两套，都是 `apl_core` 的内部对象，使用方不单独实例化。

| 对象         | 编译条件                      | 重置 | 读取 |
| ------------ | ----------------------------- | ---- | ---- |
| 净化滤芯计时 | `APL_COMPILE_PURIFY` 为 1   | `apl_core_filter_purify_reset` | `apl_core_filter_purify_get` |
| 加湿滤网计时 | `APL_COMPILE_HUMIDIFY` 为 1 | `apl_core_filter_humidify_reset` | `apl_core_filter_humidify_get` |

两项都编译时，两套计时分开累计、分开清零。除重置外没有一般的写入方法。功能是否启用分别由 `apl_core_purify_enabled_get`、`apl_core_humidify_enabled_get` 读取，修改入口尚未规定。计时何时走动、单位、计满之后做什么，待定。

---

## 6. 待讨论

- 动态配置的修改入口、保存位置，以及掉电后是否保留。
- 第 5 节全部业务规则：净化、加湿、两项同时启用时的输出合并、故障隔离。
- `link_command` / `link_snapshot` 的字段、周期和非法命令的处理。
