# APL 仿真器

APL 的 Windows 终端宿主，用鼠标、键盘模拟面板操作，用传感器输入和状态显示检查真实 C 库的行为。

## 文档与文件

| 文件 | 用途 |
|---|---|
| [使用说明.md](使用说明.md) | 环境、启动、面板操作、输入范围、验证与常见问题 |
| [设计说明.md](设计说明.md) | 模块职责、DLL 接入、时序、显示来源与扩展约定 |
| [panel_sim.py](panel_sim.py) | DLL 构建、ctypes 接口、宿主对象、原终端面板和启动入口 |
| [tui.py](tui.py) | 鼠标与键盘输入、面板绘制、终端循环 |
| [apl.def](apl.def) | DLL 导出符号 |
| [test_tui.py](test_tui.py) | 使用真实 APL DLL 的交互检查 |

## 快速启动

在工程根目录运行：

```powershell
python emulator/panel_sim.py
```

需要 Windows 控制台、Python 和支持 C11 的 Visual Studio Build Tools；无需第三方 Python 包。终端至少 **126 列 × 40 行**。

鼠标按住按键或使用键盘 `1`–`6`；面板下方“保持按下”支持多键保持。`Esc` 释放、`B` 重置、`Q` 退出。

业务判断使用 `apl_core`、`apl_hmi`。宿主负责输入、接口接线和显示映射；未实现的净化、加湿输出不会在仿真器里补做。

## 上电、掉电与记忆存储

`P` 或底部“断电 / 上电”切换外部供电，与面板 `Power` 开关机分开。
断电停止 APL、丢弃运行内存，不触发正常关机回调，也不会临时补存数据。

存储器方法和接入示例见 [存储与供电.md](存储与供电.md)。emulator 只提供字节读写，不规定 APL 保存哪些业务状态。

新增文件：`storage.py` 为文件存储器，`test_power_memory.py` 验证供电与持久化。

```powershell
python emulator/test_power_memory.py
```

## 自定义信息显示

默认 TUI 的右侧运行状态区已改成六个通用显示槽位，无默认业务绑定。接口为 `display_set`、`display_clear`、`display_clear_all`，详见 [显示槽位.md](显示槽位.md)。

## 固定负载

风机、加湿器、UV、负离子与供电具有独立的固定状态区，和六个自定义槽位同时显示。来源为宿主收到的控制输出，详见 [负载状态.md](负载状态.md)。

## 事件记录

手势、逻辑结果和模拟器可观察变化逐条记录，最近 500 条保存在内存。`E` 展开，`[` / `]` 翻页。详见 [事件记录.md](事件记录.md)。
