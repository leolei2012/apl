# APL 0.1.2 开发版平台适配

本阶段只改变可移植性，不修订工作簿业务规则。开发源仍为 `apl_lib`，没有CUCKOO业务副本。
旧 `apl_sdk_template/air-care/0.1.0`、`0.1.1`、`0.2.0` 保持原字节和绑定。
0.1.2尚未冻结发布，Studio真实产品不会自动迁移；测试中的0.1.2候选只存在于独立临时目录。

## 平台边界

`include/apl_platform.h`统一定长类型、布尔、限值、对齐整数类型、编译期断言及`APL_CALL`。
原生C11默认使用标准头和原有调用ABI、long long对齐成员。Keil C51使用1/2/4字节整数、
字节对齐、long对齐成员及LARGE reentrant调用。`APL_STATIC_ASSERT`在目标保留编译期检查。
缺省C51类型可由`APL_PLATFORM_TYPES_HEADER`指定工程已有类型头，不能同时重复定义不兼容类型。

全部APL方法、内部函数及回调类型/实现均成对使用`APL_CALL`。
这使C51自动局部变量和参数进入软件栈，避免以不完整的间接调用树覆盖这些局部帧。
原生构建中宏为空；C51回调定义缺少宏可能产生类型不匹配，禁止用函数指针强转掩盖。
`APL_UNUSED_PARAM`仅用于已初始化的简单非const形参标识符，不用于带副作用的表达式。

每个实例仍必须由一个前台执行流串行推进。不能从ISR调用APL，不能在回调中递归tick。
reentrant是目标调用约定，不能据此宣称一个实例可被并发调用。
对象、枚举、指针、memory结构体布局不承诺跨架构二进制兼容；持久化应逐字段编码、版本化并校验。

## 保持的行为

原滤网剩余比例ceil((life-used)*99/life)改为有界余数累加，输出和取整完全一致。
每次累加后余数小于life，支持1000000小时参数，不需要伪造64位类型或截断精度。
真实core memory/snapshot接口通过独立uint64_t oracle测试，覆盖百分比边界、随机32位计数及最大参数。
恒定Turbo/共用计时分支采用等价预处理裁剪，避免C51报告不可达代码；业务默认值和三项湿度冲突未变。
HMI仅重命名与C51关键字冲突的局部bit标识符，没有改变手势或灯位策略。

## 生成器

只有模板包含`include/apl_platform.h`时才启用新输出：声明/定义的调用宏、C90局部声明顺序、
逐字段结构体初始化、port回调ABI及32位整数校验。适配器初始化求值仍在原参数检查之后。
整数校验先检查signed32上下界，再以uint32差值计算步长，完整覆盖INT32_MIN到INT32_MAX，
最小负值采用不会先溢出有符号long的表达式。旧模板继续生成原C11代码。
新增测试在临时目录组合开发源和旧包装骨架，实际执行SDK编译/消费/下载并编译C51源/port；
这不是新的生产模板或对现有产品绑定的更新。

## 复现

在APL根目录，使用已安装GCC：

```powershell
python tests/run_platform_checks.py --report <报告路径>
python tests/compare_platform_trace.py --baseline 0eef3bb --report <报告路径>
```

Studio在`apl_studio`目录：

```powershell
npm run typecheck
$env:APL_STUDIO_C51 = 'C:/Keil_v5/C51/BIN/C51.exe'
npm test
```

不设置`APL_STUDIO_C51`时新测试仍执行主机SDK/整数边界；该变量仅额外启用已安装C51编译器检查。
没有安装依赖、启动Qt或操作设备。

CUCKOO独立目标在相邻仓库的`integrations/apl/target_c51`：
三能力组合核心可完整编译/链接；可额外组合原有驱动工作树评估资源。
启动文件只在隔离输出中设置XBPSTACK和不重叠的XDATA栈区，正式工程未改。
固定软件栈保留并不等于测得最坏栈；闭集调用图的命名自动帧分析不包括未命名spill、运行库细节和真实ISR。
MCU链接不等于硬件、10ms时序或整份标准验收。7项已记录规格缺口继续保持未满足，留到下一阶段修复。
