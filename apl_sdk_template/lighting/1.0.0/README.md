# 灯控示例 · {{PRODUCT_CODE}} SDK {{SDK_VERSION}}

这个独立 C11 示例用于验证多品类模板流程，支持电源、0–100% 亮度和开灯延迟；不依赖 APL 空气治理库。

1. 将 lib/light.c 加入工程，头文件目录加入 api、settings。
2. 使用方持有 {{API_PREFIX}}_t 实例；调用 {{API_PREFIX}}_init 并传入 set_light 输出回调与 ctx。回调及 ctx 在实例存活期间保持有效。
3. 每 10 ms 串行调用 {{API_PREFIX}}_tick；所有调用不可重入。
4. 用 get_power / set_power 与 get_brightness / set_brightness 操作。getter 返回请求状态，真实输出由回调给出；开灯延迟期间 power 为 true，输出仍为 false。
5. 复制 board 到自己的应用目录，实现 PWM 与输出使能。占位回调不驱动硬件；重新生成不覆盖用户工程。

配置见 settings/product_config.h：默认开关、默认亮度和开灯延迟来自产品定义。延迟支持 0–5000 ms、10 ms 步进。

本机检查：cmake -S . -B build，然后 cmake --build build，再 ctest --test-dir build。交叉编译时可关闭 BUILD_TESTING。

checks/verify.c 验证默认值、开灯延迟、亮度范围、回调输出和实例隔离。manifest.json 记录产品修订、模板指纹、文件哈希及本机检查结果。仍需在目标 MCU 验证真实驱动和时序。
