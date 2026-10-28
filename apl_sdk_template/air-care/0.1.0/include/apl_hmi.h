/**
 * @file    apl_hmi.h
 * @brief   人机对象。按键手势与显示属性，库内不创建实例。
 *
 * 编译时把 `config` 加入头文件搜索路径，以便包含 `apl_hmi_config.h`。
 * 本头不包含主控配置，也不包含 `apl_core`。与主控共用的快照和命令类型在 `apl_type.h`。
 * 宿主与人机交换数据只走本头的 getter、setter，以及初始化时注入的 ops。
 * `struct apl_hmi` 里的字节只给使用方分配实例。
 * 短按、长按和组合键是回调，由 setter 绑定，不提供取出函数指针的 getter。
 */

#ifndef APL_HMI_H
#define APL_HMI_H

#include <stdint.h>

#include "apl_hmi_config.h"
#include "apl_type.h"

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

/** 水位灯段数。apl_hmi_set_water_lamp 的序号从 1 到这个值。 */
#define APL_HMI_WATER_LAMP_COUNT    4u

/** 设置、查询、唤醒后无操作多久确认或熄屏。原表 3 秒。 */
#define APL_HMI_IDLE_MS             3000u
#define APL_HMI_IDLE_TICKS          (APL_HMI_IDLE_MS / APL_HMI_TICK_MS)

/** 一体机湿度与 PM2.5 轮流显示的周期。原表 20 秒。 */
#define APL_HMI_ROTATE_MS           20000u
#define APL_HMI_ROTATE_TICKS        (APL_HMI_ROTATE_MS / APL_HMI_TICK_MS)

/** 同时登记的组合键上限。具体组合尚未规定，库不预登记。 */
#define APL_HMI_COMBO_MAX           4u

/** 使用方按这个大小持有对象。布局不对外，读写都走 getter 和 setter。 */
#define APL_HMI_OPAQUE_SIZE         1024u

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

/**
 * 状态图标。datasheet 第 4 节列为「哪些要建成灯对象」的中间符号，按指示灯与显示表建成。
 * 每个图标一盏灯，只有亮度。
 */
enum apl_hmi_icon
{
    APL_HMI_ICON_WIFI = 0,
    APL_HMI_ICON_FILTER_WHITE,   /**< 滤网正常 */
    APL_HMI_ICON_FILTER_RED,     /**< 滤网到期 */
    APL_HMI_ICON_LOCK,
    APL_HMI_ICON_ANION,
    APL_HMI_ICON_UV,
    APL_HMI_ICON_HUMIDITY,       /**< 底部左侧恒湿图标 */
    APL_HMI_ICON_DRY,            /**< 底部右侧风干图标 */
    APL_HMI_ICON_GEAR_SLEEP,
    APL_HMI_ICON_GEAR_1,
    APL_HMI_ICON_GEAR_2,
    APL_HMI_ICON_GEAR_3,
    APL_HMI_ICON_GEAR_AUTO,
    APL_HMI_ICON_COUNT
};

/** 水位灯颜色。四段共用一个颜色。 */
enum apl_hmi_water_color
{
    APL_HMI_WATER_GREEN = 0,
    APL_HMI_WATER_ORANGE
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
 * 宿主注入的按键采样。get_key_state 为空则这一拍不认手势。
 * 对象存活期间 ops 指向的内容必须一直有效。
 */
struct apl_hmi_ops
{
    void (*get_key_state)(struct apl_hmi_key_state *state, void *ctx);
    void (*set_buzzer)(uint8_t on, void *ctx);  /**< 1 响，0 停。空则不发声 */
    /** 已通过功能启用、童锁检查的单键手势通知；只观察，不执行控制。ops 应清零初始化。 */
    void (*key_event)(enum apl_hmi_key_id key, uint8_t is_long, void *ctx);
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
 * @param ctx 调用 get_key_state 时原样传回
 *
 * 净化、加湿启用标志初始为 1。使用方随后用 setter 按快照改写。
 * 童锁初始为关。键回调初始为空，使用方调用 apl_hmi_set_key_callback。
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
uint8_t apl_hmi_get_powerup_lamp(const struct apl_hmi *self);

/**
 * @brief 写入一颗键灯的亮度
 * @param self 人机对象
 * @param key 键编号。表外或未编入的键不改变任何灯
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 *
 * 净化或加湿未启用时，对应键灯在这里立即记成 0。随后的 tick 也会保持为 0。
 */
void apl_hmi_set_key_lamp(struct apl_hmi *self, enum apl_hmi_key_id key, uint8_t brightness);

/**
 * @brief 读取一颗键灯的亮度
 * @param self 人机对象
 * @param key 键编号
 * @return 亮度。self 为空、key 非法或该键未编入时返回 0
 */
uint8_t apl_hmi_get_key_lamp(const struct apl_hmi *self, enum apl_hmi_key_id key);

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
void apl_hmi_set_key_callback(struct apl_hmi *self,
                              enum apl_hmi_key_id key,
                              void (*on_short)(struct apl_hmi *self, void *ctx),
                              void (*on_long)(struct apl_hmi *self, void *ctx),
                              void *ctx);

/**
 * @brief 写入氛围灯亮度
 * @param self 人机对象
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 */
void apl_hmi_set_ambient_brightness(struct apl_hmi *self, uint8_t brightness);

/**
 * @brief 读取氛围灯亮度
 * @param self 人机对象
 * @return 亮度。self 为空时返回 0
 */
uint8_t apl_hmi_get_ambient_brightness(const struct apl_hmi *self);

/**
 * @brief 写入氛围灯颜色
 * @param self 人机对象
 * @param red 红。取值范围尚未规定，库不夹取值
 * @param green 绿
 * @param blue 蓝
 *
 * 改颜色不改变亮度。
 */
void apl_hmi_set_ambient_color(struct apl_hmi *self,
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
void apl_hmi_get_ambient_color(const struct apl_hmi *self,
                               uint8_t *red,
                               uint8_t *green,
                               uint8_t *blue);

/**
 * @brief 写入一段水位灯的亮度
 * @param self 人机对象
 * @param index 段号，从 1 到 APL_HMI_WATER_LAMP_COUNT。其他值不改变任何灯
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 */
void apl_hmi_set_water_lamp(struct apl_hmi *self, uint8_t index, uint8_t brightness);

/**
 * @brief 读取一段水位灯的亮度
 * @param self 人机对象
 * @param index 段号，从 1 到 APL_HMI_WATER_LAMP_COUNT
 * @return 亮度。self 为空或段号非法时返回 0
 */
uint8_t apl_hmi_get_water_lamp(const struct apl_hmi *self, uint8_t index);

/**
 * @brief 写入数码管亮度
 * @param self 人机对象
 * @param brightness 亮度。0 为灭。取值范围尚未规定，库不夹取值
 */
void apl_hmi_set_digits_brightness(struct apl_hmi *self, uint8_t brightness);

/**
 * @brief 读取数码管亮度
 * @param self 人机对象
 * @return 亮度。self 为空时返回 0
 */
uint8_t apl_hmi_get_digits_brightness(const struct apl_hmi *self);

/**
 * @brief 写入数码管三位内容
 * @param self 人机对象
 * @param content 恰好 APL_HMI_DIGIT_COUNT 个字符。不要求以 '\\0' 结尾。空指针则不改
 */
void apl_hmi_set_digits_content(struct apl_hmi *self,
                                const char content[APL_HMI_DIGIT_COUNT]);

/**
 * @brief 读取数码管三位内容
 * @param self 人机对象。为空时把 content 写成三个 0
 * @param content 至少 APL_HMI_DIGIT_COUNT 个字符的缓冲区。空指针则不写
 */
void apl_hmi_get_digits_content(const struct apl_hmi *self,
                                char content[APL_HMI_DIGIT_COUNT]);

/**
 * @brief 写入数码管单位
 * @param self 人机对象
 * @param unit 上表中的一个单位。其他值不改变当前单位
 */
void apl_hmi_set_digits_unit(struct apl_hmi *self, enum apl_hmi_unit unit);

/**
 * @brief 读取数码管单位
 * @param self 人机对象
 * @return 当前单位。self 为空时返回 APL_HMI_UNIT_NONE
 */
enum apl_hmi_unit apl_hmi_get_digits_unit(const struct apl_hmi *self);

/**
 * @brief 写入童锁
 * @param self 人机对象
 * @param lock APL_HMI_CHILD_LOCK_OFF 或 APL_HMI_CHILD_LOCK_ON。其他值不改变童锁
 *
 * 本拍开始时已经采样的童锁，要等下一拍才挡住其余手势。
 */
void apl_hmi_set_child_lock(struct apl_hmi *self, enum apl_hmi_child_lock lock);

/**
 * @brief 读取童锁
 * @param self 人机对象
 * @return 当前童锁。self 为空时返回 APL_HMI_CHILD_LOCK_OFF
 */
enum apl_hmi_child_lock apl_hmi_get_child_lock(const struct apl_hmi *self);

/**
 * @brief 写入净化键是否启用
 * @param self 人机对象
 * @param enabled 只能是 0 或 1。其他值不改变当前标志
 *
 * 使用方按主控快照写入。0 时不发净化键短按，并把该键灯亮度保持为 0。
 * 长按是否同样限制尚未规定，因此仍会发出。
 */
void apl_hmi_set_purify_enabled(struct apl_hmi *self, uint8_t enabled);

/**
 * @brief 读取净化键是否启用
 * @param self 人机对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 */
uint8_t apl_hmi_get_purify_enabled(const struct apl_hmi *self);

/**
 * @brief 写入加湿键是否启用
 * @param self 人机对象
 * @param enabled 只能是 0 或 1。其他值不改变当前标志
 *
 * 使用方按主控快照写入。0 时不发加湿键短按，并把该键灯亮度保持为 0。
 * 长按是否同样限制尚未规定，因此仍会发出。
 */
void apl_hmi_set_humidify_enabled(struct apl_hmi *self, uint8_t enabled);

/**
 * @brief 读取加湿键是否启用
 * @param self 人机对象
 * @return 1 启用，0 未启用。self 为空时返回 0
 */
uint8_t apl_hmi_get_humidify_enabled(const struct apl_hmi *self);

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
int apl_hmi_set_combo(struct apl_hmi *self,
                      const enum apl_hmi_key_id *keys,
                      uint8_t key_count,
                      uint16_t hold_ms,
                      void (*on_combo)(struct apl_hmi *self, void *ctx),
                      void *ctx);


/**
 * @brief 写入一个状态图标的亮度
 * @param self 人机对象
 * @param icon 图标编号。表外不改
 * @param brightness 亮度。0 为灭
 */
void apl_hmi_set_icon(struct apl_hmi *self, enum apl_hmi_icon icon, uint8_t brightness);

/**
 * @brief 读取一个状态图标的亮度
 * @param self 人机对象
 * @param icon 图标编号
 * @return 亮度。self 为空或编号非法时返回 0
 */
uint8_t apl_hmi_get_icon(const struct apl_hmi *self, enum apl_hmi_icon icon);

/**
 * @brief 写入水位灯颜色
 * @param self 人机对象
 * @param color 绿或橙。其他值不改
 */
void apl_hmi_set_water_color(struct apl_hmi *self, enum apl_hmi_water_color color);

/**
 * @brief 读取水位灯颜色
 * @param self 人机对象
 * @return 当前颜色。self 为空时返回 APL_HMI_WATER_GREEN
 */
enum apl_hmi_water_color apl_hmi_get_water_color(const struct apl_hmi *self);

/**
 * @brief 播放一段提示音
 * @param self 人机对象
 * @param count 响几声。0 停止当前提示音
 * @param on_ms 每声响多久。按拍向上取整
 * @param off_ms 两声之间停多久
 *
 * 由 apl_hmi_tick 推进，经 ops 的 set_buzzer 开关蜂鸣器。新的一段打断旧的。
 */
void apl_hmi_buzzer_play(struct apl_hmi *self, uint8_t count, uint16_t on_ms, uint16_t off_ms);

/**
 * @brief 提示音是否还在播放
 * @param self 人机对象
 * @return 1 在播放。self 为空时返回 0
 */
uint8_t apl_hmi_get_buzzer_busy(const struct apl_hmi *self);

/**
 * @brief 接上面板逻辑：按键形成 link_command，显示按 link_snapshot 绘制
 * @param self 人机对象
 * @param send 交出操作。空指针则断开，回到只调用键回调的方式
 * @param ctx 调用 send 时原样传回
 *
 * 接上后，六颗键的手势由库按前端规格书第 5 节转成操作，不再调用使用方的键回调；
 * 同时登记两组组合键：定时 + 风速 3 秒为滤网重置，电源 + 定时 3 秒为产测。
 * 灯光开关、首次触摸唤醒、定时和恒湿的设置预览、滤芯寿命查询留在人机本地。
 * 屏、灯、水位、图标和提示音每拍按最近一份快照重画，使用方不要再用 setter 覆盖。
 */
void apl_hmi_link_bind(struct apl_hmi *self,
                       enum apl_link_result (*send)(const struct apl_link_command *command,
                                                    void *ctx),
                       void *ctx);

/**
 * @brief 交入主控的最新快照
 * @param self 人机对象
 * @param snapshot 快照。空指针则不改
 *
 * 未接上面板逻辑时只记下，不绘制。接上后同时按快照写入净化、加湿启用标志。
 */
void apl_hmi_set_snapshot(struct apl_hmi *self, const struct apl_link_snapshot *snapshot);

/**
 * @brief 显示滤芯剩余寿命 3 秒
 * @param self 人机对象
 *
 * 只查询，不改主控状态。当前面板没有单独的滤芯寿命键，由使用方决定入口。
 */
void apl_hmi_filter_query(struct apl_hmi *self);

/**
 * @brief 面板灯光是否开着
 * @param self 人机对象
 * @return 1 开，0 关。self 为空时返回 0
 */
uint8_t apl_hmi_get_light(const struct apl_hmi *self);

#endif /* APL_HMI_H */
