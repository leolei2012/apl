#include "apl_port.h"

/* 示例拥有实例；APL 库内不创建应用实例。 */
static struct apl s_apl;

void apl_example_init(void)
{
    apl_port_init(&s_apl);
}

void apl_example_tick_10ms(void)
{
    apl_tick(&s_apl);
    /* TODO: 读取 apl_hmi_get_digits_content / get_digits_unit / get_icon /
     * get_key_lamp / get_water_lamp 等 getter，刷新实际面板。
     * 该函数必须每 10 ms 调用一次；所有命令和 tick 串行执行。 */
}
