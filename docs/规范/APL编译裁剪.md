# APL编译裁剪

适用于当前`apl_lib`开发源码。产品参数只能启用已编入的能力；裁剪决定哪些实现和私有状态存在。默认保持全功能，能力配置统一在[apl_build_config.h](../../apl_lib/config/apl_build_config.h)，缺省参数及细项校验在`apl_core_config.h`，逻辑键配置在`apl_hmi_config.h`。

可以编辑config，或用实际MCU工程统一的`DEFINE`/`-D`覆盖。Studio也可按产品生成配套配置头。所有APL源文件、宿主和公开头必须采用同一组宏并完整重编译；生成包与工程外部宏冲突会报编译错误。

## 可裁剪功能

| 类别 | 开关 | 约束及关闭后的行为 |
|---|---|---|
| 净化、加湿 | `APL_COMPILE_PURIFY`、`APL_COMPILE_HUMIDIFY` | 至少保留一项；关闭时相应`APL_DEFAULT_*`必须为0 |
| UV、负离子、Wi-Fi | `APL_COMPILE_UV`、`APL_COMPILE_ANION`、`APL_COMPILE_WIFI` | 参数模块位必须为0；移除专属实现、状态、输入/输出及命令路径 |
| 空气传感器 | `APL_COMPILE_SENSOR_PM25/PM10/HCHO/TVOC` | 依赖净化；参数传感器位必须为0，不再采样或参与分级 |
| 湿度传感器 | `APL_COMPILE_SENSOR_HUMIDITY` | 依赖加湿；移除湿度输入和闭环，连续加湿仍可保留 |
| 极速 | `APL_CORE_HAS_TURBO` | 移除切换、恢复档和专属rpm/选配参数 |
| 定时 | `APL_COMPILE_TIMER` | 移除计数、回调及设置逻辑，定时API不再提供；Timer长按童锁及组合用途独立保留 |
| 滤网维护 | `APL_COMPILE_FILTER` | 移除寿命计数、百分比、查询、复位和对应参数/API |
| 产测 | `APL_COMPILE_FACTORY` | 移除产测窗口、状态、注入/恢复及CL显示 |
| 运行记忆 | `APL_COMPILE_MEMORY` | 移除读取/恢复API及保存通知实现；共用首次运行状态仍保留，不单独承诺RAM缩减 |
| 睡眠 | `APL_COMPILE_SLEEP` | 移除睡眠策略与专属rpm；配置/设置拒绝Sleep档，旧记忆中的该档被忽略 |
| 风干 | `APL_COMPILE_DRY` | 仅与加湿同时编入时生效；关闭后正常关机仍关闭负载 |
| HMI整体 | `APL_COMPILE_HMI` | `struct apl`不再含hmi，不链接人机实现；宿主负责交互 |
| RGB氛围灯 | `APL_HMI_COMPILE_AMBIENT` | 移除8字节颜色/输出缓存及绘制/输出逻辑，四个ambient API不再提供 |
| 蜂鸣器 | `APL_HMI_COMPILE_BUZZER` | 播放状态/代码与两个API均移除，提示音和安全报警音全部关闭 |
| 内部按键映射 | `APL_HMI_COMPILE_KEYMAP` | 移除15字节配置和3字节HMI指针；保留标准按键策略，映射setter API不再提供 |
| 每键自定义回调 | `APL_HMI_COMPILE_KEY_CALLBACKS` | 移除回调槽和`apl_hmi_set_key_callback`；标准link面板与观察事件仍可使用 |
| 组合槽 | `APL_HMI_COMBO_MAX` | 允许0或2～4，默认4；0时API与组合逻辑不存在，标准两组入口随各自功能编入 |
| 滤网缓存 | `APL_CORE_COMPILE_FILTER_CACHE` | 0时现场计算百分比，结果不变；仅在FILTER编入时有意义 |
| 单个逻辑键 | `APL_HMI_COMPILE_KEY_*` | 关闭识别及键灯；通用数组/编号保留，不保证RAM按键数量等比例缩减 |

所有布尔开关只允许0/1；一体机缺省配置为全功能。UV、负离子和空气传感器的未指定值随净化能力，湿度传感器随加湿能力；其他开关缺省为1。组合槽1或大于4会编译报错。可选功能按函数组和状态归属进行条件编译，不要求每项功能都有独立`.c`文件；当前Wi-Fi有独立实现文件，关闭时Studio会将其从交付源集省略。完整边界见[模块解耦与裁剪](APL模块解耦与裁剪.md)。

## 专用API与共用接口

裁掉功能后，专用API不声明、不定义，不能调用空实现作为兼容。典型接口包括定时set/get、记忆读取/恢复、滤网查询/复位、Wi-Fi连接/PID、氛围灯set/get、蜂鸣器play/busy、每键回调和组合注册。宿主按同一宏保护调用。

共用ops槽位、通用命令/快照DTO与灯位/图标数组保留稳定编号；关闭的功能输出中性值，不再执行所属业务。不使用的宿主驱动不会被APL自动移除，应从宿主源集及回调填充中去掉引用。

UV联锁、缺水停泵等保护不提供“保留负载但关闭保护”的选项。关闭蜂鸣器会取消全部报警声音，产品需要明确选择适用性。

`APL_COMPILE_HMI=0`时，`apl_init`/`apl_init_profile`的HMI参数仍保留，传NULL即可；`apl_link_bind`为空操作。主控、1ms/旧10ms入口及仍编入的其他能力继续可用。不要访问`device.hmi`或调用`apl_hmi_*`。

## 参数格式与Studio

逻辑参数格式现为v6，新增独立短按/长按映射；详见[按键映射](../../apl_lib/按键映射.md)。，Studio可以保留未编入功能的参数以便以后重启用。生成的宿主代码按开关保护赋值，C参数结构仅保留对应物理字段；不得传输裸结构体或依赖不同构建的`sizeof`一致。

此前未包含新开关（TIMER、FILTER、FACTORY、MEMORY、SLEEP、DRY、AMBIENT、KEYMAP）的Studio配置按全部为1补齐解释，保持旧产品行为，不静默改写历史修订。旧工程仍需使用配套头文件重新编译。

在“配置产品 → 芯片与固件裁剪”中选择预设或自定义开关。保存后，面板仿真与源码包采用同一裁剪；纯主控不能运行圆形面板仿真。开启未编入能力时拒绝配置。详见[固件裁剪与芯片预算](../../apl_studio/docs/固件裁剪与芯片预算.md)。

示例：保留紧凑标准面板，在Keil目标统一添加：

```text
APL_HMI_COMPILE_KEY_CALLBACKS=0,APL_HMI_COMBO_MAX=2,APL_CORE_COMPILE_FILTER_CACHE=0
```

示例：由宿主管理交互，且不使用APL运行记忆：

```text
APL_COMPILE_HMI=0,APL_COMPILE_MEMORY=0,APL_CORE_COMPILE_FILTER_CACHE=0
```

## 容量测量

以下是v5新增映射前的Keil C51 LARGE测量，保留作历史比较。v6编入映射时，含HMI的实例在这些值上增加18 B，关闭KEYMAP则无此增量；CODE必须重新测量。实例预留包括主控、HMI及1ms余数，不包括宿主或栈；CODE是隔离源码探针，不是目标完整固件。

| 构建 | 纯净化：实例 / CODE | 纯加湿：实例 / CODE | 一体机：实例 / CODE |
|---|---:|---:|---:|
| 各产品能力的缺省构建 | 443 / 28836 B | 475 / 29923 B | 498 / 34605 B |
| 裁剪参考 | 208 / 11833 B | 241 / 15287 B | 242 / 15565 B |
| 裁剪参考再关闭HMI | 48 / 4314 B | 69 / 5933 B | 70 / 6211 B |

“裁剪参考”关闭新7项功能、蜂鸣器、每键回调、组合键、Wi-Fi、UV、负离子、极速和全部传感器；不是Studio“最小面板”预设。缺省构建中，净化相关模块/传感器随净化能力确定，不强行为纯加湿编入UV等功能。测量收据在`apl_studio/output/memory/features-default.json`、`features-minimum.json`和`features-core-minimum.json`。

其他开关的共同作用由当前头文件公式决定；共享状态仅在最后一个使用者移除时释放。原生GCC仍有固定占位容量，不能把其结果冒充C51。去掉MEMORY主要节省代码；不能据此减少软件栈预算。

以内存估算比较方案，再用目标工程完整链接后的`.M51`/`.MAP`核对Flash、各RAM空间及完整调用链栈。不要只因入口固定栈帧变小就降低256 B预留，也不能用隔离CODE差值承诺整机节省量。

## 复测

```powershell
python tools/check_memory.py --c51 C:/Keil_v5/C51/BIN/C51.exe --define APL_COMPILE_TIMER=0 --define APL_COMPILE_FILTER=0 --report apl_studio/output/memory/trim-profile.json
python tests/run_platform_checks.py --report apl_studio/output/memory/trim-platform-report.json
```

`--define`可重复，工具分别编译三种产品能力组合。此前功能裁剪基线的Studio全套66项测试已启用C51检查并通过；新增19种裁剪组合覆盖GCC行为/宿主消费、公开和私有符号消失及Keil各源文件编译；22组新sizeof与内存公式一致。137项原生回归已通过，收据输入SHA-256与当前源码一致。这些结果不代表实际芯片已经满足容量限制。

## v6 内部按键映射验证

新增 `APL_HMI_COMPILE_KEYMAP`，默认1；关闭后保留标准按键策略。Studio支持独立短按/长按映射，参数格式升级为v6、定义修订7，宿主输入编号及硬件绑定保持不变。

本次 Keil C51 LARGE 实测：纯净化461 B、纯加湿493 B、一体机516 B。这里按各产品编译能力缺省构建；一体机分项为主控192 B、人机323 B、调度1 B。关闭KEYMAP时各含HMI构建减少18 B，纯主控不变。测量收据为 `apl_studio/output/memory/keymap-default.json` 和 `keymap-fixed.json`；这些是隔离编译器对象测量，不是整机链接或栈验证。

139项原生行为回归通过，收据输入哈希与当前C源码一致。Studio全量75项中74项首次通过，剩余1项为旧内存预期322 B未包含映射增量，更新为340 B后单独复测通过；新增9项映射界面、模型、导出和真实C仿真检查另已复测通过。23组新Keil sizeof与估算规则一致，类型检查和Web构建通过。浏览器验证了Light输入映射电源、风机实际开启且事件仍记录Light短按。
