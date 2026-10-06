/**
 * @file    apl_hmi.h
 * @brief   人机对象。按键手势与显示属性，库内不创建实例。
 *
 * 编译时把 `config` 加入头文件搜索路径，以便包含 `apl_hmi_config.h`。
 * 本头不包含主控配置，也不包含 `apl_core`。
 * 宿主与人机交换数据只走本头的 getter、setter，以及初始化时注入的 ops。
 * `struct apl_hmi` 里的字节只给使用方分配实例。
 * 短按、长按和组合键是回调，由 setter 绑定，不提供取出函数指针的 getter。
 */

#ifndef APL_HMI_H
#define APL_HMI_H

#include <stdint.h>

#include "apl_hmi_config.h"

#define APL_HMI_VERSION_MAJOR   0
#define APL_HMI_VERSION_MINOR   1
#define APL_HMI_VERSION_PATCH   0
#define APL_HMI_VERSION_STRING  "0.1.0"
#define APL_HMI_VERSION_NUM \
    ((APL_HMI_VERSION_MAJOR << 16) | (APL_HMI_VERSION_MINOR << 8) | APL_HMI_VERSION_PATCH)

/** 宿主调用 apl_hmi_tick 的周期。与主控拍长相同，两边不互相包含。 */
#define APL_HMI_TICK_MS             10u

/** 长按时长。松开早于这个时间是短按，没有更短的下限。 */
#define APL_HMI_LONG_PRESS_MS       3000u

#define APL_HMI_LONG_PRESS_TICKS    (APL_HMI_LONG_PRESS_MS / APL_HMI_TICK_MS)

/** 上电后全灯保持的时间。 */
#define APL_HMI_POWERUP_LAMP_MS     2000u
/** 上电全灯的拍数。2 秒在 10 ms 拍上是 200 拍。 */
#define APL_HMI_POWERUP_LAMP_TICKS  200u

/**
 * 上电全亮。亮度取值范围尚未规定，库仍不夹取其他写入。
 * 255 只作为这次全亮，128 是它的半亮。
 */
#define APL_HMI_LAMP_FULL           255u
/** 电源键在上电全亮结束之后的半亮。 */
#define APL_HMI_LAMP_HALF           128u

/** 数码管位数。内容不是段码，也不保证以 '\\0' 结尾。 */
#define APL_HMI_DIGIT_COUNT         3u

/** 水位灯段数。apl_hmi_water_lamp_set 的序号从 1 到这个值。 */
#define APL_HMI_WATER_LAMP_COUNT    4u

/** 同时登记的组合键上限。具体组合尚未规定，库不预登记。 */
#define APL_HMI_COMBO_MAX           4u

/** 使用方按这个大小持有对象。布局不对外，读写都走 getter 和 setter。 */
#define APL_HMI_OPAQUE_SIZE         512u

struct apl_hmi;

/**
 * 键电平。宿主消抖后给出，同一拍六颗键来自同一次采样。
 * 未编入的键仍占一个字段，库丢掉它的电平。
 */
enum apl_hmi_key_level
{
    APL_HMI_KEY_RELEASED = 0,
    APL_HMI_KEY_PRESSED
};

enum apl_hmi_key_id
{
    APL_HMI_KEY_PURIFY = 0,
    APL_HMI_KEY_HUMIDITY,
    APL_HMI_KEY_TIMER,
    APL_HMI_KEY_LIGHT,
    APL_HMI_KEY_SPEED,
    APL_HMI_KEY_POWER,
    APL_HMI_KEY_COUNT
};

/** 数码管当前单位。同一时刻一个。 */
enum apl_hmi_unit
{
    APL_HMI_UNIT_NONE = 0,
    APL_HMI_UNIT_HOUR,
    APL_HMI_UNIT_PERCENT,
    APL_HMI_UNIT_PM25
};

/** 童锁只挡住面板手势。主控方法不受它影响。 */
enum apl_hmi_child_lock
{
    APL_HMI_CHILD_LOCK_OFF = 0,
    APL_HMI_CHILD_LOCK_ON
};

struct apl_hmi_key_state
{
    enum apl_hmi_key_level purify;
    enum apl_hmi_key_level humidity;
    enum apl_hmi_key_level timer;
    enum apl_hmi_key_level light;
    enum apl_hmi_key_level speed;
    enum apl_hmi_key_level power;
};

/**
 * 宿主注入的按键采样。key_state_get 为空则这一拍不认手势。
 * 对象存活期间 ops 指向的内容必须一直有效。
 */
struct apl_hmi_ops
{
    void (*key_state_get)(struct apl_hmi_key_state *state, void *ctx);
};

/** 占位对象。使用方声明它来持有实例，不读写 storage。 */
struct apl_hmi
{
    union
    {
        void *align_ptr;
        void (*align_fn)(void);
        long long align_ll;
        double align_d;
        unsigned char bytes[APL_HMI_OPAQUE_SIZE];
    } storage;
};

/**
 * @brief 清空显示和手势，并记下按键采样
 * @param self 人机对象。由使用方持有
 * @param ops 按键采样表。可以为空
 * @param ctx 调用 key_state_get 时原样传回
 *
 * 净化、加湿启用标志初始为 1。使用方随后用 setter 按快照改写。
 * 童锁初始为关。键回调初始为空，使用方调用 apl_hmi_key_callback_set。
 * 上电后已编入的键灯、氛围灯、四段水位灯和三位数字管全亮，
 * 数字为 888。APL_HMI_POWERUP_LAMP_TICKS 拍之后这些灯熄灭，电源键半亮。
 */
void apl_hmi_init(struct apl_hmi *self, const struct apl_hmi_ops *ops, void *ctx);

/**
 * @brief 推进一拍，认出短按、长按和组合键
 * @param self 人机对象
 *
 * 宿主每 APL_HMI_TICK_MS 调用一次。短按在松开时发出；按下满 3 秒发出长按，
 * 同一按下不重复，松开时也不再补一次短按。未编入的键不认手势。
 * 回调里只通知使用方，库不判断业务。
 * 不要在回调里再调用 apl_hmi_tick。
 */
void apl_hmi_tick(struct apl_hmi *self);

/**
 * @brief 上电全灯是否还在亮
 * @param self 人机对象
 * @return 1 表示全灯阶段尚未结束。self 为空或 2 秒已到时返回 0
 *
 * 为 1 时，本拍会重新写成全亮。使用方这时不要再用 setter 覆盖这些灯。
 */
uint8_t apl_hmi_powerup_lamp_get(const struct apl_hmi *self);

/**
 * @brief 写入一颗键灯的亮度
 * @param self 人机对象
 * @param key 键编号。表外或未编入的键不改变任何灯
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 *
 * 净化或加湿未启用时，对应键灯在这里立即记成 0。随后的 tick 也会保持为 0。
 */
void apl_hmi_key_lamp_set(struct apl_hmi *self, enum apl_hmi_key_id key, uint8_t brightness);

/**
 * @brief 读取一颗键灯的亮度
 * @param self 人机对象
 * @param key 键编号
 * @return 亮度。self 为空、key 非法或该键未编入时返回 0
 */
uint8_t apl_hmi_key_lamp_get(const struct apl_hmi *self, enum apl_hmi_key_id key);

/**
 * @brief 同时绑定一颗键的短按、长按和 ctx
 * @param self 人机对象
 * @param key 键编号。表外或未编入的键不改变绑定
 * @param on_short 短按回调。空指针则该动作不调用
 * @param on_long 长按回调。空指针则该动作不调用
 * @param ctx 调用这两个回调时原样传回，可以和 ops 的 ctx 不同
 *
 * 一次调用同时替换这三项。不提供取出函数指针的 getter。
 */
void apl_hmi_key_callback_set(struct apl_hmi *self,
                              enum apl_hmi_key_id key,
                              void (*on_short)(struct apl_hmi *self, void *ctx),
                              void (*on_long)(struct apl_hmi *self, void *ctx),
                              void *ctx);

/**
 * @brief 写入氛围灯亮度
 * @param self 人机对象
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 */
void apl_hmi_ambient_brightness_set(struct apl_hmi *self, uint8_t brightness);

/**
 * @brief 读取氛围灯亮度
 * @param self 人机对象
 * @return 亮度。self 为空时返回 0
 */
uint8_t apl_hmi_ambient_brightness_get(const struct apl_hmi *self);

/**
 * @brief 写入氛围灯颜色
 * @param self 人机对象
 * @param red 红。取值范围尚未规定，库不夹取值
 * @param green 绿
 * @param blue 蓝
 *
 * 改颜色不改变亮度。
 */
void apl_hmi_ambient_color_set(struct apl_hmi *self,
                               uint8_t red,
                               uint8_t green,
                               uint8_t blue);

/**
 * @brief 读取氛围灯颜色
 * @param self 人机对象。为空时，非空的输出参数写成 0
 * @param red 红。可以为空，空则不写
 * @param green 绿。可以为空
 * @param blue 蓝。可以为空
 */
void apl_hmi_ambient_color_get(const struct apl_hmi *self,
                               uint8_t *red,
                               uint8_t *green,
                               uint8_t *blue);

/**
 * @brief 写入一段水位灯的亮度
 * @param self 人机对象
 * @param index 段号，从 1 到 APL_HMI_WATER_LAMP_COUNT。其他值不改变任何灯
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 */
void apl_hmi_water_lamp_set(struct apl_hmi *self, uint8_t index, uint8_t brightness);

/**
 * @brief 读取一段水位灯的亮度
 * @param self 人机对象
 * @param index 段号，从 1 到 APL_HMI_WATER_LAMP_COUNT
 * @return 亮度。self 为空或段号非法时返回 0
 */
uint8_t apl_hmi_water_lamp_get(const struct apl_hmi *self, uint8_t index);

/**
 * @brief 写入数码管亮度
 * @param self 人机对象
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 */
void apl_hmi_digits_brightness_set(struct apl_hmi *self, uint8_t brightness);

/**
 * @brief 读取数码管亮度
 * @param self 人机对象
 * @return 亮度。self 为空时返回 0
 */
uint8_t apl_hmi_digits_brightness_get(const struct apl_hmi *self);

/**
 * @brief 写入数码管三位内容
 * @param self 人机对象
 * @param content 恰好 APL_HMI_DIGIT_COUNT 个字符。不要求以 '\\0' 结尾。空指针则不改
 */
void apl_hmi_digits_content_set(struct apl_hmi *self,
                                const char content[APL_HMI_DIGIT_COUNT]);

/**
 * @brief 读取数码管三位内容
 * @param self 人机对象。为空时把 content 写成三个 0
 * @param content 至少 APL_HMI_DIGIT_COUNT 个字符的缓冲区。空指针则不写
 */
void apl_hmi_digits_content_get(const struct apl_hmi *self,
                                char content[APL_HMI_DIGIT_COUNT]);

/**
 * @brief 写入数码管单位
 * @param self 人机对象
 * @param unit 上表中的一个单位。其他值不改变当前单位
 */
void apl_hmi_digits_unit_set(struct apl_hmi *self, enum apl_hmi_unit unit);

/**
 * @brief 读取数码管单位
 * @param self 人机对象
 * @return 当前单位。self 为空时返回 APL_HMI_UNIT_NONE
 */
enum apl_hmi_unit apl_hmi_digits_unit_get(const struct apl_hmi *self);

/**
 * @brief 写入童锁
 * @param self 人机对象
 * @param lock APL_HMI_CHILD_LOCK_OFF 或 APL_HMI_CHILD_LOCK_ON。其他值不改变童锁
 *
 * 本拍开始时已经采样的童锁，要等下一拍才挡住其余手势。
 */
void apl_hmi_child_lock_set(struct apl_hmi *self, enum apl_hmi_child_lock lock);

/**
 * @brief 读取童锁
 * @param self 人机对象
 * @return 当前童锁。self 为空时返回 APL_HMI_CHILD_LOCK_OFF
 */
enum apl_hmi_child_lock apl_hmi_child_lock_get(const struct apl_hmi *self);

/**
 * @brief 写入净化键是否启用
 * @param self 人机对象
 * @param enabled 只能是 0 或 1。其他值不改变当前标志
 *
 * 使用方按主控快照写入。0 时不发净化键短按，并把该键灯亮度保持为 0。
 * 长按是否同样限制尚未规定，因此仍会发出。
 */
void apl_hmi_purify_enabled_set(struct apl_hmi *self, uint8_t enabled);

/**
 * @brief 读取净化键是否启用
 * @param self 人机对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 */
uint8_t apl_hmi_purify_enabled_get(const struct apl_hmi *self);

/**
 * @brief 写入加湿键是否启用
 * @param self 人机对象
 * @param enabled 只能是 0 或 1。其他值不改变当前标志
 *
 * 使用方按主控快照写入。0 时不发加湿键短按，并把该键灯亮度保持为 0。
 * 长按是否同样限制尚未规定，因此仍会发出。
 */
void apl_hmi_humidify_enabled_set(struct apl_hmi *self, uint8_t enabled);

/**
 * @brief 读取加湿键是否启用
 * @param self 人机对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 */
uint8_t apl_hmi_humidify_enabled_get(const struct apl_hmi *self);

/**
 * @brief 登记一组组合键
 * @param self 人机对象
 * @param keys 参与的键。至少两颗，不能重复，且都要已编入
 * @param key_count keys 的颗数
 * @param hold_ms 全部按下后需要保持的毫秒数。按拍向上取整，不能为 0
 * @param on_combo 时间到调用一次
 * @param ctx 调用 on_combo 时原样传回
 * @return 0 成功，-1 参数非法、含未编入的键、与已有组合重复或名额已满
 *
 * 这些键都按下时，暂停它们各自的短按和长按。保持时间到了调用一次，
 * 这次按下到此结束，松开不再补短按或长按。提前松开则取消这次组合：
 * 松开的键按自己已按下的时间补短按或长按，仍按住的键继续单键计时。
 */
int apl_hmi_combo_set(struct apl_hmi *self,
                      const enum apl_hmi_key_id *keys,
                      uint8_t key_count,
                      uint16_t hold_ms,
                      void (*on_combo)(struct apl_hmi *self, void *ctx),
                      void *ctx);

#endif /* APL_HMI_H */
