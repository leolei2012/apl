# APL 参数表

参数定义与 APL 源码在同一个 Git 仓库管理。Studio 后续按用户选定的 APL Tag / 提交读取此目录，不能用当前分支最新参数解释历史版本。

当前对应 **APL 0.3.0 开发版、参数格式 6、定义修订 7**。

| 文件 | 用途 |
|---|---|
| [APL参数表.xlsx](APL参数表.xlsx) | 工程默认参数模板，包含参数配置、产品信息、选项说明、按键映射；复制后填写黄色参数值和产品信息 |
| [profile.schema.json](profile.schema.json) | 便于 Git 比较和脚本读取的字段定义：编号、中文名称、类型、范围、单位、默认值及来源 |
| [examples/purifier.json](examples/purifier.json) | 纯净化配置示例 |
| [examples/humidifier.json](examples/humidifier.json) | 纯加湿配置示例 |
| [examples/combined.json](examples/combined.json) | 一体机配置示例 |

三种示例都显式关闭 UV、负离子和 Wi-Fi，供用户再选配；Excel 模板展示固件全功能默认值，不与示例混为同一份产品配置。

当前参数格式v6、定义修订7，共23项数值参数、1项PID身份参数及1组内部按键映射。`display_rotate_s`（显示间隔）位于Excel“参数配置”第22行，`timer_step_h`（定时步进）位于第23行。`fan_sleep_rpm`、`fan_gear1_rpm`、`fan_gear2_rpm`、`fan_gear3_rpm`、`fan_turbo_rpm` 位于第24～28行，分别配置五个物理档位的目标转速（0～65535 rpm）；缺省依次为400/600/900/1200/1500 rpm，0目标表示停转。自动/恒湿/风干/备用策略先算输出档位，再查该档rpm。v1～v5旧产品在Studio确认重新绑定后补新增默认值，保留已有显示间隔、步进和业务参数；旧固件/旧表必须按各自版本解码，不能直接套用新结构。

PID在Excel“产品信息”第22行（B22），按文本保存，避免丢失前导零；JSON定义在`identity.pid`，示例值在`profile.pid`。默认空字符串，不使用某款产品的PID作为全局缺省值。Studio内部产品UUID、工程code与此PID互相独立。

## 内部按键映射

`keyMapping` 定义输入槽位、动作编号和标准默认值；产品示例通过 `profile.key_mapping` 保存 `{enabled, short, long}`。C 参数结构中对应 `profile.keymap.enabled`、`short_action[7]`、`long_action[7]`。未配置或 `enabled=false` 沿用标准面板逻辑。开启后可分别改变每个输入的短按、长按动作，不修改宿主的GPIO绑定、按键输入编号或组合键。

Excel“按键映射”的 B3 是启用值（0/1），第7～13行对应固定输入0～6：Purify、Humidity、Timer、Light、Speed、Power、Filter。C列填写短按动作编号，E列填写长按动作编号；D/F列通过右侧动作词典自动显示名称。动作0表示无动作且不唤醒屏幕，动作1～15见表内说明。未编入或未启用的动作可以保留在表中，执行时无效。请为需要用户开关机的产品保留可用电源动作，为使用童锁的产品保留可用解锁动作。

示例把 Light 输入短按改成电源开关：先将 B3 改为1，再将第10行 C10 改为1；宿主仍上报 Light，APL 执行电源动作。事件仍显示原始输入名称。默认组合键继续采用原始 Timer+Speed 和 Power+Timer 输入，独立于单键动作映射。

此功能可由 `APL_HMI_COMPILE_KEYMAP=0` 裁剪；标准固定映射保留。C51 全功能单实例配置表增加15 B、HMI借用指针增加3 B，合计18 B。具体裁剪规则和接口生命周期见[内部按键映射](../按键映射.md)。

## 维护方式

1. 工程师在 APL 实现或调整参数，同时更新 `profile.schema.json`。参数编号必须与 `struct apl_profile` 字段一致，含义或单位发生不兼容变化时提升参数格式版本，不能复用旧编号表达新含义。
2. 使用生成工具更新工程内 Excel 模板。字段定义由 JSON 维护，Excel 中的字段编号、范围、默认值等由工具生成；用户的产品参数值在模板副本中填写。不要把 Excel 和 JSON 当成两份可以独立修改的规范。
3. 运行一致性检查，核对字段、类型、实际 C 默认值、范围边界及三机型示例。
4. 源码、参数定义、Excel 和必要的说明一起提交。APL、参数格式、定义修订和产品配置修订分别记录；旧 Tag / 提交保留历史字节，不自动升级产品。

APL C 实现仍是运行时校验与业务执行的依据；当前不会从 JSON 自动生成 C 校验器。一致性工具通过实际编译、调用 `apl_profile_defaults` / `apl_profile_validate` 检查两边，不用手工抄写的 C 默认值作为证据。

### 校验

在工程根目录执行：

```powershell
python tools/check_parameters.py
```

需要已安装的本机 GCC；工具会查找 `CC`、PATH 和现有 MinGW。只使用临时本机程序，不连接设备、不访问串口。检查的是仓库内的默认 Excel 模板，不是任意用户编辑后的产品表；Studio 导入和产品表解析仍待实现。

### 重新生成 Excel

生成器使用已有的 `@oai/artifact-tool`，不依赖 Studio 的运行包。在有该工具的环境设置其 `node_modules` 路径：

```powershell
$env:APL_ARTIFACT_MODULES = '<已有工具运行环境的 node_modules 目录>'
node tools/export_parameter_workbook.mjs
python tools/check_parameters.py
```

生成会覆盖工程默认模板，请先将个人产品副本保存到其他文件。脚本不自动下载安装依赖。

## 版本与 Git 关联

- `aplVersion`：目标库版本，必须与 `apl_lib/include/apl.h` 一致。
- `schemaVersion`：参数含义/格式版本，必须与 `APL_PROFILE_VERSION` 一致。
- `schemaRevision`：参数描述定义的修订；不等同于用户产品配置的修订。
- 产品表的 `configRevision`：用户方案的修订，参数调整发布后递增。
- 仓库内模板不填写自己的最终 Git 提交号，避免提交号自引用。Studio 导出某个产品表时，应记录用户所选仓库、Tag、解析后的完整提交号以及参数定义哈希。
- 当前 `development` 且 Tag / 提交号为空，不能据此声明 0.3.0 已发布。Studio Git 版本发现、选择和绑定功能尚未实现。

Git 会同时保存 Excel 二进制和便于审查的 JSON；发布时它们与源码作为一个整体冻结。当前历史 SDK 模板没有因此更新。

功能裁剪后，逻辑参数表仍保留原定义，但C参数结构按编译能力移除对应物理成员：例如Wi-Fi的PID/配网超时、定时步进、滤网寿命、风干参数、睡眠/极速转速及加湿参数。Studio生成宿主按统一的参数守卫映射保护赋值。不同构建的结构体物理布局不可作为烧录格式。详见[模块解耦与裁剪](../../docs/规范/APL模块解耦与裁剪.md)。

## 设备导入边界

计划流程为 Excel / Studio 配置 → 脚本校验 → Modbus RTU 串口写入 → 设备整表校验和保存 → APL 冷启动加载。当前仍只有配置模型与表格，Modbus 收发、寄存器映射、提交命令、完整性校验和设备存储均未接入。

本次没有分配 Modbus 地址。后续寄存器定义、字序、权限、写入暂存及提交/读回规则也应放入此目录，与相应 APL 版本一起管理。不得把 `struct apl_profile` 的内存布局直接当成寄存器表或 Flash 格式。

完整接口及当前参数范围见[运行时参数配置](../../docs/规格/APL运行时参数配置.md)。
