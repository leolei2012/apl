# APL HMI Datasheet

| 项目 | 内容 |
|---|---|
| 文档状态 | 草稿 |
| 对象 | `struct apl_hmi` |
| 配置 | [config/apl_hmi_config.h](../../config/apl_hmi_config.h) |
| 配套文档 | [apl前端规格书](./apl前端规格书.md) |

本文描述人机库的对象。库内不创建实例。使用方持有 `struct apl`，人机是其中的 `struct apl_hmi`。这里只写显示内容和对象属性，不写引脚、驱动和扫描方式。

对外接口在 [`include/apl_hmi.h`](../../include/apl_hmi.h)。下面是对象模型。引脚、驱动和扫描方式不在库里。

---

## 1. 对象

会发光的对象都有亮度。亮度是该对象自己的属性。

### 1.1 灯

一盏灯一个对象。

| 属性 | 含义 |
|---|---|
| `brightness` | 亮度。0 表示灭 |

### 1.2 按键

一个按键一个对象。每个按键底下有一盏灯，灯是这个按键的属性。亮度用 `apl_hmi_set_key_lamp`、`apl_hmi_get_key_lamp` 按键编号读写。

| 属性 | 含义 |
|---|---|
| `lamp` | 这颗键底下的灯 |
| `on_short` | 短按回调 |
| `on_long` | 长按回调。六颗键都有 |
| `ctx` | 绑定时存下的使用方对象，调用时原样还回 |

回调在初始化之后用 `apl_hmi_set_key_callback` 绑定，一次同时写入短按、长按和 `ctx`。初始为空。签名为：

```c
void on_short(struct apl_hmi *self, void *ctx);
void on_long(struct apl_hmi *self, void *ctx);
```

库是可复用的，回调一律带 `ctx`，库内不用 `s_self` 桥接。`on_short` 或 `on_long` 未绑定（空指针）时，该动作不调用。

本面板有 6 个按键，与 6 盏键灯一一对应。短按、长按的区分在按键对象上完成。有长按的键，时长为 3 秒，认出后调用该键自己的回调。灯对象不记录这 3 秒。

| 成员 | 位置 | 短按回调 | 长按 3 秒回调 | 键下灯表示 |
|---|---|---|---|---|---|
| `key_purify` | 上 | Purify | Wifi | 净化开着 |
| `key_humidity` | 左上 | Humidity | Clean | 加湿开着 |
| `key_timer` | 右上 | Timer | Lock | 定时开着 |
| `key_light` | 左下 | Light | 睡眠 | 面板灯开着 |
| `key_speed` | 右下 | Speed | Turbo | 风速或强力开着 |
| `key_power` | 下 | 电源 | 结束风干 | 机器开着 |

长按的第二功能不单独再配一盏灯。Wi-Fi 与锁若在中间另有指示，尚未建成对象。Clean、Turbo 的呈现尚未规定。

### 1.3 颜色

| 属性 | 含义 |
|---|---|
| `red` | 红 |
| `green` | 绿 |
| `blue` | 蓝 |

分量的取值范围尚未规定。

### 1.4 氛围灯

面板上方那一排是一盏氛围灯，同时是空气质量点阵灯条，不是多盏独立的灯。净化配置和两项都有时，用它呈现空气质量。只加湿时灯位仍在。四档颜色为蓝、绿、黄、红，对应优、良、中、差。预处理时的动态显示式样，待定。

| 属性 | 含义 |
|---|---|
| `lamp.brightness` | 亮度，可单独调。0 表示灭 |
| `color` | RGB，可单独调。改颜色不改变亮度 |

颜色和亮度各改各的。调色用 `apl_hmi_set_ambient_color`、`apl_hmi_get_ambient_color` 写红、绿、蓝；调亮用 `apl_hmi_set_ambient_brightness`、`apl_hmi_get_ambient_brightness`。改颜色不改变亮度。

### 1.5 数码管

中间三位 `888` 是一个数码管对象。它描述显示内容，不描述段码。

| 属性 | 含义 |
|---|---|
| `lamp` | 一盏灯，含亮度 |
| `content` | 三位显示内容 |
| `unit` | 当前单位：小时（`H`）、百分比（`%`）、`PM2.5` |

同一时刻一个单位。显示哪一位、哪一个单位，由使用方按主控快照调用 `apl_hmi_set_digits_brightness`、`apl_hmi_set_digits_content`、`apl_hmi_set_digits_unit` 写入。内容恰好三位，不要求以 `\0` 结尾。读取用对应的 get。

### 1.6 状态图标

中间和底部的符号各是一盏灯，有亮度，用 `apl_hmi_set_icon`、`apl_hmi_get_icon` 按 `enum apl_hmi_icon` 读写：Wi-Fi、滤网白、滤网红、童锁、负离子、UV、恒湿、风干、Sleep、一档、二档、三档、Auto。上电全亮阶段这些图标也全亮。

### 1.7 蜂鸣器

蜂鸣器由宿主在 `struct apl_hmi_ops` 的 `set_buzzer` 开关。`apl_hmi_buzzer_play` 给出声数、响和停的毫秒数，`apl_hmi_tick` 推进。`apl_hmi_init` 后响一声。

### 1.8 水位灯

底部中间是四段水位灯，由低到高为第 1 段到第 4 段。每段是一盏灯，有自己的亮度。使用方用 `apl_hmi_set_water_lamp`、`apl_hmi_get_water_lamp` 按段号 1 到 4 读写。点亮段数与 `apl_core` 的水位一致：0 全灭，1 到 4 从最低段往上点亮。只净化时这四段不显示。四段共用一个颜色，绿或橙，用 `apl_hmi_set_water_color`、`apl_hmi_get_water_color` 读写。

---

## 2. `struct apl_hmi`

对外只有这一个对象。按键、氛围灯、水位灯和数码管是它的属性。六盏键灯在各自的按键里面。使用方读写这些属性时调用 getter 和 setter，不访问对象里的存储。`struct apl_hmi` 里的字节只给使用方分配实例。按键电平仍由宿主注入的 `get_key_state` 提供。

```text
struct apl_hmi
├── key_purify       按键 + 键下灯 + 短按/长按回调
├── key_humidity
├── key_timer
├── key_light        短按灯光，长按睡眠
├── key_speed
├── key_power        短按电源，长按结束风干
├── ambient          空气质量灯条：亮度 + RGB
├── water_1 … water_4 四段水位灯，由低到高，共用颜色
├── icon[13]         状态图标
├── buzzer           提示音
└── digits           亮度 + 三位内容 + 单位
```

上图是属性模型。键灯和回调按 `enum apl_hmi_key_id` 访问，水位灯按段号 1 到 4 访问。方法以 `apl_hmi_` 为前缀，`self` 为第一个参数。库内不放 `static struct apl_hmi`。

`apl_hmi` 不包含 `apl_core`，也不读取 `config/apl_core_config.h`。净化、加湿是否出现，看主控快照。快照里未启用的功能，对应键不产生命令，对应键灯亮度为 0。

按键认出短按或长按后，调用该键对应的回调，并把 `self` 和 `ctx` 传入。人机不在回调里判断业务。使用方在回调里决定是否形成 `link_command`，或改这台人机上的灯。

净化或加湿未启用时，不调用这两颗键的短按回调，键灯亮度为 0。它们的长按回调是否受同样限制，尚未规定，因此长按仍会发出。使用方按主控快照调用 `apl_hmi_set_purify_enabled`、`apl_hmi_set_humidify_enabled`，参数只能是 0 或 1，读取用对应的 get。初始为 1。未启用时，键灯 setter 立即记成 0，`apl_hmi_tick` 也会把亮度保持为 0。

童锁是这个对象的属性，不是主控状态。使用方用 `apl_hmi_set_child_lock`、`apl_hmi_get_child_lock` 读写。为开时，不发出其他键的短按、长按和组合键；定时键的长按仍然发出，使用方用它解锁。主控的开机、档位、定时等方法不受童锁影响。宿主在主控 `on_power_off` 里把童锁设为关。中间的童锁指示灯尚未建成对象。

宿主每 10 ms 调用 `apl_hmi_tick`。按键电平由 `apl_hmi_init` 注入的 `get_key_state` 在同一拍给出六颗键。短按在松开时发出，没有更短的时间下限。按下满 3 秒发出长按一次，这次按下松开时不再补短按。回调为空则不调用。人机不在回调里判断业务。

组合键单独登记，不改变按键采样。`apl_hmi_set_combo` 给出参与的键、保持时间和回调。这些键都按下时，暂停它们各自的短按和长按；保持时间到了调用一次，这次按下结束。提前松开则取消：已松开的键按自己的按下时间补短按或长按，仍按住的键继续单键计时。库不预登记具体组合。同时最多登记 4 组。

---

## 3. 配置

面板在编译期由 `APL_HMI_PANEL` 选定。换面板仍是这一个对象，使用方的实例不变。六颗键各自用 `APL_HMI_COMPILE_KEY_*` 决定是否编入，默认都编入。未编入的键不认手势，键灯保持灭，也不能参加组合键。

上电（`apl_hmi_init`）后，已编入的键灯、氛围灯、四段水位灯和三位数字管以 `APL_HMI_LAMP_FULL`（255）全亮，数字为 `888`。氛围灯颜色不变。之后每拍由 `apl_hmi_tick` 维持。满 `APL_HMI_POWERUP_LAMP_TICKS`（200 拍，2 秒）后这些灯熄灭，数字清空，电源键留在 `APL_HMI_LAMP_HALF`（128）。未编入的键不点亮。净化或加湿未启用时，对应键灯在全亮阶段也被保持为 0。`apl_hmi_get_powerup_lamp` 在这 2 秒内返回 1，使用方这时不要覆盖这些灯。

当前这块圆形面板尚未在 `apl_hmi_config.h` 里登记，默认标识是 `APL_HMI_PANEL_UNSPECIFIED`。现在的实现就是这一对象，不另挂面板编号。人机本地默认值（例如各灯的初始亮度）确定后写入该头文件。

---

## 4. 尚未写入

- Clean（加湿键长按）在面板逻辑里用作一体机恒湿设置；Turbo 的图标呈现尚未规定。
- 亮度与颜色分量的取值范围，以及默认亮度。
- 快照如何写到上述属性上见前端规格书第 6 节。
