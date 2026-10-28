# APL Studio

嵌入式产品标准化配置与 SDK 交付平台。平台统一产品模型、表单、校验、修订和交付；各品类模板提供业务源码、配置映射、硬件移植接口与验证程序。

AP 空气治理是第一个模板，另有独立 C11 灯控示例验证多品类流程。现有目录名、`product.apl.json`、`apl-sdk/` 和包格式标识沿用历史命名，不限制产品必须使用 APL 库。

## 启动

需要 Node.js ≥22.19.0、npm 和能够生成本机可执行程序的 GCC 兼容 C11 编译器。

```powershell
npm ci
npm start
```

访问 http://127.0.0.1:9190；Windows 也可双击 [start.cmd](start.cmd)。服务只监听本机。

开发时分别运行 `npm run dev`、`npm run dev:web`；Vite 页面 http://127.0.0.1:5174 代理 API / WebSocket。服务托管已构建页面，用 `npm run build:web` 更新。

## 使用流程

1. 打开“源码模板”，选择 AP 或灯控，点击“用此模板创建产品”。空白产品也可在概览绑定模板。
2. 编辑产品名称、标识、品类、SDK 版本。模板按精确版本绑定，应用前说明草稿规范化与不支持项目的移除。
3. 在“功能定义”选择能力与默认值，在“产品参数”配置数值、查看需要移植的硬件接口。
4. 保存产品修订；未保存草稿不能用于生成，过期修订返回冲突并保留草稿。
5. 在“交付中心”预览实际文件，点击“生成并验证 SDK”。服务固定产品与源码快照，编译真实 C 库和移植示例，并执行模板消费程序。
6. 两项检查通过后下载 ZIP。失败保留诊断，旧成功归档仍可下载。
7. 工程师将 SDK 加入 MCU 工程，在自己的应用目录实现硬件回调。重新生成仅产生归档，不覆盖用户工程。

产品内“仿真台”运行实际生成的 C 业务程序，支持 AP 文字面板、短按/长按、环境输入、实际负载、暂停/单步/快进、模拟上掉电和会话内存储。“模型预览”保留数据类型检查。详情见 [Web 仿真台](docs/Web仿真台.md)。目标 MCU 与硬件移植仍需工程师验收。

## 已实现

- 多产品管理；布尔、整数、枚举、字符串功能及权限、默认值和约束；SQLite 持久化与修订。
- 模板描述驱动的能力、参数、依赖、移植接口和文件预览。新增普通参数无需改 React。
- `apl.air-care@0.1.0`：真实 APL 快照，净化加湿、仅净化、仅加湿三种组合。
- `standard.lighting@1.0.0`：独立 C11 灯控示例，电源、亮度和开灯延迟，独立目录及 API。
- 构建队列、真实 C11 编译与消费执行、诊断、历史交付和 ZIP 文件哈希。
- Web 仿真台：独立 C 进程、虚拟时钟、面板手势、传感器输入、实际输出回调、通用运行信息槽位与事件。
- JSON 导入导出与定义包。定义包标记 `sdkReady: false`，验证通过的源码 SDK 标记 `sdkReady: true`。
- WebSocket 变更通知、重连、Ctrl+S、草稿保护与移动端布局。

目前没有共享能力目录、MCP、账户、多租户或任意芯片工程生成。仿真支持 AP 与灯控的已接入版本，新增产品族需要自己的 C 宿主桥接。Qt 工具保留在 [simulator](../simulator/README.md)。

## 目录与技术栈

沿用 ProbeStation 的 Node.js / TypeScript、React 18、Vite 6、Fastify 5、Cordis、schemastery 与 npm workspaces，版本由 package-lock.json 锁定。

| 目录 | 职责 |
|---|---|
| apps/server / apps/web | 本地服务与产品工作台 |
| packages/model | 产品与模板描述、共用校验 |
| packages/product | SQLite 产品与修订 |
| packages/templates | 注册、AP 专用生成器、通用 C 生成器 |
| packages/build | SDK 构建归档、仿真会话与 C 宿主桥接 |
| packages/api / packages/definition | HTTP / WebSocket 与定义 ZIP |
| ../apl_sdk_template | 独立的版本化产品源码模板 |
| docs / tests | 规范与无硬件回归 |

## 设置与数据

- `APL_STUDIO_DATA`：默认 .data，包含 studio.db 和 sdk/<任务 UUID>/，停服后备份整个目录。
- `APL_STUDIO_PORT`：默认9190，Vite 代理使用默认端口。
- `APL_STUDIO_TEMPLATES`：默认相邻的 apl_sdk_template，启动时加载；发布新版本后重启。
- `APL_STUDIO_CC`：本机编译器路径，其次读取 CC。Windows 检测已有 C:/Qt/Tools/mingw1310_64/bin/gcc.exe，否则使用 PATH 中的 gcc；不依赖 Qt 界面或 Qt 库。

编译器仅在服务端配置，网页不接受任意源码或执行命令。模板属于本地受信任源码；公网部署需独立构建环境和项目隔离。

## 检查

```powershell
npm run check
```

执行类型检查、网页构建及无硬件测试，覆盖两种品类的真实 C 编译执行、参数映射、失败门禁、哈希与版本一致性。浏览器检查产物放在忽略目录 output/playwright/。

更多说明：[产品标准化方向](docs/产品标准化方向.md)、[源码模板接入约定](docs/源码模板接入约定.md)、[产品定义与接口](docs/产品定义与接口.md)。
