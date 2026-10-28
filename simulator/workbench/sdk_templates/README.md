# APL 产品 SDK 接入说明

本包是可移植 C11 源码 SDK，没有 Qt、Windows 或 RTOS 依赖。产品配置在
`product.apl.json`；APL 版本、生成指纹和逐文件 SHA-256 在 `manifest.json`。

## 接入顺序

1. 将 `src` 的三个 C 文件加入嵌入式工程，头文件搜索路径加入 `include`、`config`。
   使用生成的配置头，不再额外用 `-D` 覆盖产品参数，否则会偏离预览结果。
2. 复制 `port/apl_port.c` 和 `.h` 到自己的应用目录，完成标记 TODO 的硬件接口。
   所有接口都是示例占位，传感器返回值和负载空函数不能直接作为真实产品实现。
3. 应用持有 `struct apl`，调用 `apl_port_init`；`examples/app.c` 展示了调用方式。
4. 每 10 ms 串行调用一次 `apl_tick`，再读取 HMI getter 刷新实际键灯、图标、
   数码管内容/单位/亮度、水位灯颜色和氛围灯。getter 不会直接驱动屏幕。
5. 如有掉电记忆，实现 `memory_save`；初始化后将验证通过的记录交给
   `apl_core_memory_restore`。记录按字段编码并带版本和校验，避免结构体对齐和枚举宽度差异。

## 硬件接口

- 输入：`get_key_state`（消抖后的六键）、`get_water_level`、`get_pm25`、`get_humidity`。
- 输出：`set_fan(state, gear)`、`set_pump`、`set_uv`、`set_anion`、`set_buzzer`。
- `get_uv_interlock`：1 表示联锁闭合，示例默认为 0，需要接实际开关。
- PM10 / HCHO / TVOC 为可选 ops，按 `include/apl_core.h` 的单位实现后绑定；未绑定不参与判定。
- `ops` 生命周期必须覆盖 APL 实例；先清零再填写。回调和 `tick` 不得重入。
- 风机档位由移植层映射到具体 PWM 或协议，软件仿真不包含真实风机惯性、传感器误差和 Flash 时序。

## 构建和升级

通用构建：`cmake -S . -B build`，再 `cmake --build build`；交叉编译可传入工具链文件。
Keil / IAR 等工程直接加入 C 文件和包含路径，并选择支持 C11 / `_Static_assert` 的编译器。
包中不包含特定 MCU 的启动文件、链接脚本和外设初始化。

重新配置后导出新的 ZIP，替换 `src`、`include`、`config` 并检查版本差异。
保留自己应用目录里的移植代码；生成器不会写入嵌入式工程。
配置面板类型仅影响仿真视图，不能据此推导实际 GPIO 引脚。

功能裁剪由配置宏控制编译；包中保留完整 APL 源码，便于维护和重新配置。
