/** 用户移植层：复制到工程后维护此文件，升级 SDK 时保留。 */
#include "apl_port.h"
#include <string.h>

static enum apl_water_level get_water_level(void *ctx)
{
    (void)ctx;
    /* TODO: 实际水位 0–4；未接线返回缺水。 */
    return APL_WATER_LEVEL_0;
}

static uint16_t get_pm25(void *ctx)
{
    (void)ctx;
    /* TODO: 返回 PM2.5，整数 μg/m³。 */
    return 0u;
}

static uint8_t get_humidity(void *ctx)
{
    (void)ctx;
    /* TODO: 实际湿度 0–100%。 */
    return 0u;
}

static uint8_t get_uv_interlock(void *ctx)
{
    (void)ctx;
    /* TODO: 安全联锁闭合返回 1；未接线不允许 UV 开启。 */
    return 0u;
}

static void set_fan(enum apl_switch state, enum apl_fan_gear gear, void *ctx)
{
    (void)state; (void)gear; (void)ctx;
    /* TODO: state + gear 映射到本项目 PWM / 风机驱动。 */
}

static void set_pump(enum apl_switch state, void *ctx)
{
    (void)state; (void)ctx;
    /* TODO: 水泵 / 雾化负载。 */
}

static void set_uv(enum apl_switch state, void *ctx)
{
    (void)state; (void)ctx;
    /* TODO: UV 输出。 */
}

static void set_anion(enum apl_switch state, void *ctx)
{
    (void)state; (void)ctx;
    /* TODO: 负离子输出。 */
}

static void get_key_state(struct apl_hmi_key_state *state, void *ctx)
{
    (void)ctx;
    memset(state, 0, sizeof(*state));
    /* TODO: 一次采集六键，提供已消抖电平；未编入的键会被忽略。 */
}

static void set_buzzer(uint8_t on, void *ctx)
{
    (void)on; (void)ctx;
    /* TODO: 蜂鸣器输出。 */
}

static void memory_save(const struct apl_core_memory *memory, void *ctx)
{
    (void)memory; (void)ctx;
    /* TODO: 按字段编码、版本号和校验保存到 Flash / EEPROM。
     * 不直接持久化 struct 原始字节；回调运行在 tick / 命令调用线程内。
     * 如需异步写入，应复制内容再排队。 */
}

void apl_port_init(struct apl *self)
{
    /* ops 必须在 APL 对象使用期间一直有效，不能放在函数栈上。 */
    static struct apl_core_ops core_ops;
    static struct apl_hmi_ops hmi_ops;
    memset(&core_ops, 0, sizeof(core_ops));
    memset(&hmi_ops, 0, sizeof(hmi_ops));
    core_ops.get_water_level = get_water_level;
    core_ops.get_pm25 = get_pm25;
    core_ops.get_humidity = get_humidity;
    core_ops.get_uv_interlock = get_uv_interlock;
    core_ops.set_fan = set_fan;
    core_ops.set_pump = set_pump;
    core_ops.set_uv = set_uv;
    core_ops.set_anion = set_anion;
    core_ops.memory_save = memory_save;
    hmi_ops.get_key_state = get_key_state;
    hmi_ops.set_buzzer = set_buzzer;
    apl_init(self, &core_ops, 0, &hmi_ops, 0);
    /* TODO: 有合法保存记录时，在此调用 apl_core_memory_restore(&self->core, &memory)。 */
    apl_link_bind(self);
}
