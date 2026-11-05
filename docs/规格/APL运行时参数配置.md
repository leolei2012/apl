# APL 运行时参数配置（开发版 0.3.0）

## 方向

嵌入式程序预先编译 APL 全部支持能力；Studio 生成产品参数，设备加载后决定产品功能与运行参数。同一固件可配置为空净、加湿、一体机。编译宏仍可用于固件容量裁剪，运行时配置不能启用未编译能力。

当前实现参数校验、冷启动加载、复制保存到实例、读回，以及参数驱动实际 C 业务。传输协议尚未确定，没有定义串口帧、参数二进制布局、CRC 或 Flash 地址。旧 Studio 的源码生成流程尚未切换到参数交付，已冻结模板保持原字节。

## 已接入字段

对应的 Excel 模板、字段定义及三机型示例已纳入 [APL 工程参数目录](../../apl_lib/parameters/README.md)，后续由 Studio 按选定的 APL Git 版本读取。Modbus RTU 为计划的传输方式，寄存器地址和收发实现尚未定义。

| 字段 | 含义和范围 |
|---|---|
| pid | Wi-Fi产品标识字符串，0～16位ASCII英文字母或数字，保留大小写，默认空；Excel“产品信息”B22按文本保存 |
| version | 当前内存参数模型版本 6；与固件版本分别管理 |
| keymap | APL内部短按/长按动作映射；JSON产品参数名为key_mapping，Excel另表配置；宿主输入编号与GPIO绑定不变 |
| purify_enabled / humidify_enabled | 净化/加湿能力开关，0 或 1，至少启用一项 |
| modules | UV、负离子、Wi-Fi 位掩码；独立选配，不强制依赖净化 |
| sensors | 使用 apl_type.h 的传感器掩码；空气传感器依赖净化，湿度依赖加湿 |
| default_fan_gear | 初始用户档位：Sleep、一、二、三、Auto；不以 Turbo 开机 |
| dry_fan_gear / sensor_fallback_gear | 实际档位 Sleep～三档；既有睡眠缺水风干规则仍优先 |
| turbo_enabled | 允许进入极速，0 或 1；命令与直接档位 setter 均检查 |
| filter_shared | 一体机滤网重置共用/独立选择，0 或 1；沿用现有计时语义 |
| humidity_hysteresis | 水泵恒湿启停回差，0～100 个百分点 |
| dry_s | 风干秒数，0～86400；0 进入风干后在下一秒检查结束，不承诺立即待机 |
| filter_life_h | 滤网寿命，1～1000000 小时；百分比、到期和产测使用同一值 |
| shortage_run_s | 纯加湿缺水恢复窗口，0～86400 秒；一体机仍按既有规则持续净化 |
| water_blink_s | 缺水提醒闪烁时长，0～86400 秒 |
| wifi_pair_s | 配网超时，1～86400 秒 |
| display_rotate_s | PM2.5/湿度每页显示秒数，1～3600，默认20；仅净化+加湿时轮换 |
| timer_step_h | 定时键每次增加的小时数，1～24，默认1；三种能力组合均适用 |
| fan_sleep_rpm / fan_gear1_rpm / fan_gear2_rpm / fan_gear3_rpm / fan_turbo_rpm | 睡眠、一、二、三、极速各档目标转速，0～65535 rpm；缺省依次为400/600/900/1200/1500 rpm，0目标为停转 |

当前参数格式为v6（定义修订7）：v2新增显示间隔，v3新增定时步进，v4新增五档目标rpm，v5新增产品PID，v6新增内部按键映射。Studio的v1～v5旧产品需明确确认绑定当前源码，保留已有功能与参数，补新增字段默认值；PID缺省为空，映射缺省关闭以保留标准行为。已有PID、转速和映射不因重新绑定而被覆盖。保存后再仿真/导出，不静默改写旧修订。C宿主须配套重新编译并用`apl_profile_defaults`初始化v6配置；旧表不能作为v6直接应用，宿主须按版本解码、补默认并校验。禁止传输裸结构体。

显示页计时从进入运行开始，包含5秒预处理、关灯及设置预览时间；不因恢复显示而重新计时。两项功能同时启用时，先PM2.5、再湿度，各停留 `display_rotate_s` 秒；完整周期为其两倍。只启用一项时固定显示对应数据。HMI从快照读取间隔，使用32位拍计数，支持1小时每页而不发生16位溢出；旧宿主快照字段为0时兼容默认20秒。

没有运行定时时，第一次短按从 `timer_step_h` 开始，后续每次累加同一步进；超过24小时回到0（取消），从0再次按回到步进值。不额外插入不满一步的24小时：例如步进5为5→10→15→20→0，步进24为24→0。有运行定时时，第一次仍只显示向上取整的剩余小时数，后续再按步进增加，不强制对齐倍数。按住连增也使用相同步进，1.5秒启动、0.5秒连增、经过0停2秒及3秒无操作确认维持原规则；设置外的长按童锁规则不变。旧宿主快照字段为0时按默认1小时处理。

## 产品 PID 与宿主接入

PID是Wi-Fi产品标识字符串，例如`aaerrxvja3m7lvhz`。在Studio“运行参数 → Wi-Fi 联网”分类配置；关闭Wi-Fi时隐藏输入并保留原值。它不参与净化/加湿业务判断，也不等同于Studio的内部产品UUID或工程code。最多16个ASCII字母/数字，区分大小写，允许留空；空表示尚未配置，不影响离线APL运行。Wi-Fi宿主需要PID时自行检查非空，APL不自动连接云端。

```c
struct apl_profile profile;
#if APL_COMPILE_WIFI
char pid[APL_PRODUCT_PID_MAX + 1u];
apl_profile_defaults(&profile);
memcpy(profile.pid, "aaerrxvja3m7lvhz", 17u); /* 需要#include <string.h> */
/* 冷启动：apl_init_profile(..., &profile)，检查返回APL_PROFILE_OK后再调度。 */
apl_core_get_pid(&device.core, pid);
/* 宿主将pid交给联网模块；无需每1ms读取。 */
#endif
```

配置由实例复制持有，修改调用方的原表不会更改当前实例。`apl_core_get_profile`也能读回PID；`apl_core_get_pid`复制17字节（含结尾NUL），调用方必须提供足够缓冲区；self为空返回全零缓冲区，输出为空指针时无操作。版本错误、非法字符和17字节内无NUL的表会被拒绝，初始化失败保持对象原值。

`APL_COMPILE_WIFI=0`时，C参数结构不含PID和配网超时，也不提供PID/连接状态专用接口；宿主与生成代码须使用同一宏保护访问。逻辑参数表仍可保留Wi-Fi值，但离线构建不加载这些字段。禁止根据不同构建下的裸结构体布局传输参数。

PID属于产品参数，随产品配置保存，不写入`memory_save`运行记忆。宿主负责配置持久化及协议编码，Modbus地址、字符串字节顺序和烧录工具仍待定义。启用Wi-Fi时，PID固定数组增加17字节C51实例预留；包含默认启用的内部映射时，默认组合516 B，标准面板裁剪425 B，精简面板391 B，纯主控176 B，不含宿主缓冲区或栈。不同功能组合应使用配套工具重测。

## 内部按键映射与宿主接入

`profile.keymap`包含启用值及七个输入的短按、长按动作数组。调用`apl_profile_defaults`后，`enabled=0`使用标准面板逻辑；产品启用自定义后按数组执行动作。输入编号仍固定为Purify=0、Humidity=1、Timer=2、Light=3、Speed=4、Power=5、Filter=6，宿主继续提供相同的`get_key_state`电平，不需要改变GPIO或驱动绑定。

```c
apl_profile_defaults(&profile);
#if APL_COMPILE_HMI && APL_HMI_COMPILE_KEYMAP
profile.keymap.enabled = 1u;
profile.keymap.short_action[APL_HMI_KEY_LIGHT] = APL_KEY_ACTION_POWER;
profile.keymap.short_action[APL_HMI_KEY_POWER] = APL_KEY_ACTION_LIGHT_TOGGLE;
#endif
/* 冷启动时apl_init_profile(..., &profile)，成功后apl_link_bind。 */
```

APL复制参数并将内部持有的表绑定到HMI，初始化输入缓冲区可复用。`key_event`仍报告原始输入及短/长按，例如上例按Light仍报告Light短按，业务执行电源动作。键灯getter的索引也不变。Timer+Speed滤网复位、Power+Timer产测仍按原始输入组合识别，不随单键映射改变。

动作0不执行且不唤醒，已裁掉或产品未启用的动作无效；未知动作编号或启用值超出0/1时整表拒绝。动作、童锁解锁、定时连增和唤醒的细节见[内部按键映射](../../apl_lib/按键映射.md)。`APL_HMI_COMPILE_KEYMAP=0`保留标准固定行为并移除配置成员和专用API，生成代码和宿主须按相同宏保护字段访问。逻辑表可保留禁用状态下的映射数据，不代表固件已支持自定义映射。

## 风机目标转速与宿主接入

APL先按原有自动、恒湿、风干、预处理和输入异常策略确定最终输出档位，再映射该档的目标rpm。AUTO没有独立固定转速；备用档和风干档沿用其配置档位对应的rpm。用户档位与最终输出档位不同的情况下，getter返回最终输出档位的rpm。待机、产测或风机关闭时返回0；0rpm配置也返回0，表示该档停转。档位优先级仍按既有逻辑，配置表不自动排序或强制转速递增。

```c
/* 初始化前填写各档rpm，调用apl_init_profile后每1ms执行本段。 */
uint16_t target_rpm;
apl_tick_1ms(&device);
target_rpm = apl_core_get_fan_target_rpm(&device.core);
motor_set_target_rpm(target_rpm); /* 宿主的电机驱动接口 */
```

`apl_core_get_fan_target_rpm`是只读目标getter，不是测速值；self为空返回0。无须注册 `set_fan` 回调也可轮询。原 `set_fan(on, gear, ctx)` 保留为兼容/变化通知，回调前fan开关和输出档位已提交，因此回调内读getter也得到对应的新目标rpm。宿主不再需要另维护档位到rpm的表；PWM、闭环调速和测速由宿主驱动负责。

快照新增 `fan_target_rpm`，Studio显示“风机目标转速”并记录目标变化事件，掉电显示0。恢复运行记忆后在首个 10ms 业务帧提交风机输出，再给出当前配置的目标rpm；记忆仍只保存用户档位，rpm配置属于产品参数，不写入运行记忆。缺省各档为400/600/900/1200/1500 rpm，可不导表启动后使用；通用起始值可按产品覆盖。未模拟电机惯性或伪造实测转速。已有v4表中的0或自定义转速会保留，不随定义修订自动改变。

关闭模块会拒绝相关命令，并抑制模块输出和故障/联锁输入调用。关闭传感器不读取数值/状态回调，不计入预处理或闭环控制。快照报告当前选配；HMI 通过快照使用原有机型按键与显示逻辑。

本版可选用预定义动作配置单键短按和长按，未提供任意脚本动作、物理 PWM/RPM 标定、可配置空气质量阈值、任意显示编排或记忆字段选择。Excel 未决策略仍沿用原实现，不将冲突写成新标准。

## 接入顺序

### 尚未导入参数表

APL固件本身包含一份完整缺省配置，不依赖Excel、Studio或存储器。设备首次上电、宿主确认尚无已导入表时，给初始化入口传入`NULL`即可：

```c
/* 冷启动；物理负载已关闭。NULL表示没有参数表。 */
apl_init_profile(&device, &core_ops, core_ctx, &hmi_ops, hmi_ctx, NULL);
apl_link_bind(&device);
/* 如需恢复本产品的运行记忆，在首次调度前恢复；随后每1ms调用apl_tick_1ms。 */
```

`apl_core_init_profile`同样支持`profile == NULL`。两者成功返回`APL_PROFILE_OK`，配置与`apl_init` / `apl_core_init`一致；正常从待机开始，上电自检和开机预处理规则保持不变。可用`apl_core_get_profile`读取实际缺省表，或用`apl_profile_defaults`取得独立副本。缺省值由`apl_lib/config/apl_core_config.h`等现有固件默认项集中生成；当前默认净化+加湿、一档开机、显示轮换20秒、定时步进1小时。各档目标rpm缺省依次为400/600/900/1200/1500，开机一档返回600 rpm；这些是通用起始值，可按实际电机覆盖，0rpm仍表示停转。完整宿主示例与维护入口见[APL使用说明](../../apl_lib/使用说明.md)。

宿主负责判断存储中是否有完整有效的参数表：未导入时传`NULL`；有效表解码后传其地址。APL不读取Flash、不把空白存储器直接当作C结构体，也不自动保存缺省表。非空表的版本/能力/参数错误仍返回原错误并保持对象不变，不能把初始化失败当作已经完成初始化；冷启动需要回退时，宿主先保留错误信息，再显式传`NULL`初始化。`apl_profile_validate(NULL)`仍返回`APL_PROFILE_INVALID_ARGUMENT`，空指针不是一张可校验的表。

### 已导入参数表

```c
struct apl device;
struct apl_profile profile;
enum apl_profile_result result;

apl_profile_defaults(&profile);
/* 宿主把已经完整接收、解码和校验的字段填入 profile。 */
profile.purify_enabled = 1u;
profile.humidify_enabled = 0u;
profile.modules = APL_PROFILE_MODULE_WIFI;
profile.sensors = APL_SENSOR_PM25_MASK;
profile.filter_life_h = 2400UL;

result = apl_init_profile(&device, &core_ops, core_ctx,
                          &hmi_ops, hmi_ctx, &profile);
if (APL_PROFILE_OK == result)
{
    apl_link_bind(&device);
    /* 需要时恢复与本产品方案兼容的运行记忆，然后每1ms调用apl_tick_1ms。 */
}
```

- `apl_profile_defaults`：由固件现有宏生成缺省配置，不依赖堆内存。
- `apl_profile_validate`：检查模型版本、范围、布尔值、选配依赖与固件能力，无硬件调用。
- `apl_init_profile` / `apl_core_init_profile`：空表指针按缺省配置初始化；非空时先校验整表，成功才初始化并复制到对象，失败不修改原对象、不调用硬件。调用方随后可复用接收缓冲区。
- `apl_core_get_profile`：读回对象实际持有的配置。对象为空返回固件缺省表，输出为空则不写。
- 旧 `apl_init` / `apl_core_init` 继续从编译默认值启动，保留已有接入方式。

这些初始化入口只能在停止 tick 且物理负载已关闭时调用。它们不是在线换表接口，不能靠清空内存代替关闭硬件。首版采用启动时加载；设备收到新配置后的停机、持久化和重启流程由宿主明确安排。API 和 tick 必须串行，不能在串口中断中重入。

组合对象推荐使用 `apl_tick_1ms`，其每实例分频计数在成功初始化时清零，累计 10 次调用才执行原 10ms 业务帧。独立主控的 `apl_core_tick` 和旧组合入口 `apl_tick` 均保留 10ms 周期，不能与新入口混调；不改变参数时间单位、参数表版本、自检和长按等业务时长。移植时使用配套头文件共同重新编译，细则见[宿主接入要求](../../apl_lib/宿主接入要求.md)。

参数表与 `apl_core_memory` 是两类数据：前者定义产品方案，后者保存运行记忆/滤网计数。`memory_save` 不保存参数表。恢复旧运行记忆前，宿主要确认它属于当前产品；APL 会屏蔽已禁用模块请求，但不会自行判断两张产品表是否属于同一个产品。

## 协议待定时的边界

`struct apl_profile` 是 C 内存模型，含填充和目标平台相关布局，不能直接 memcpy 为串口数据或 Flash 格式。之后应在协议适配层逐字段解码为该结构，再调用整表校验。建议后续明确格式版本、长度、产品/固件兼容信息、完整性检查、完整接收确认及保存后读回；当前不预设具体协议。

错误结果区分空参数、版本不支持、固件未提供能力和参数值不合法。失败时宿主保留已验证配置；冷启动无有效存储时，可显式回退固件默认配置。库不静默修正错误字段。

## 验证

执行 `python tests/run_platform_checks.py`。新增 `tests/test_profile.c` 在同一全功能可执行程序中加载三种产品配置，检查真实采样/输出、配置复制、错误配置不修改对象、禁用模块拒绝命令、旧记忆不能重新开启禁用模块、滤网寿命、风干和配网超时。原有三种编译能力组合及裁剪分支继续回归。测试不代表 MCU、NVM 或串口链路已验证。

本轮本机 GCC 严格告警编译和执行共 67 项通过（原有 66 项＋同一固件多配置 1 项）。未运行 C51 编译器、真实设备或串口链路；0.3.0 仍为开发版，不是已冻结 Studio 模板。
