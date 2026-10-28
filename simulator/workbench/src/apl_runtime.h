#pragma once
#include <QLibrary>
#include <QTemporaryDir>
#include <memory>
extern "C" {
#include "apl.h"
}

struct AplApi
{
    decltype(&::apl_core_get_fan_gear) apl_core_get_fan_gear = &::apl_core_get_fan_gear;
    decltype(&::apl_core_get_power) apl_core_get_power = &::apl_core_get_power;
    decltype(&::apl_core_get_snapshot) apl_core_get_snapshot = &::apl_core_get_snapshot;
    decltype(&::apl_core_get_timer) apl_core_get_timer = &::apl_core_get_timer;
    decltype(&::apl_hmi_filter_query) apl_hmi_filter_query = &::apl_hmi_filter_query;
    decltype(&::apl_hmi_get_ambient_brightness) apl_hmi_get_ambient_brightness = &::apl_hmi_get_ambient_brightness;
    decltype(&::apl_hmi_get_ambient_color) apl_hmi_get_ambient_color = &::apl_hmi_get_ambient_color;
    decltype(&::apl_hmi_get_child_lock) apl_hmi_get_child_lock = &::apl_hmi_get_child_lock;
    decltype(&::apl_hmi_get_digits_brightness) apl_hmi_get_digits_brightness = &::apl_hmi_get_digits_brightness;
    decltype(&::apl_hmi_get_digits_content) apl_hmi_get_digits_content = &::apl_hmi_get_digits_content;
    decltype(&::apl_hmi_get_digits_unit) apl_hmi_get_digits_unit = &::apl_hmi_get_digits_unit;
    decltype(&::apl_hmi_get_icon) apl_hmi_get_icon = &::apl_hmi_get_icon;
    decltype(&::apl_hmi_get_light) apl_hmi_get_light = &::apl_hmi_get_light;
    decltype(&::apl_hmi_get_key_lamp) apl_hmi_get_key_lamp = &::apl_hmi_get_key_lamp;
    decltype(&::apl_hmi_get_powerup_lamp) apl_hmi_get_powerup_lamp = &::apl_hmi_get_powerup_lamp;
    decltype(&::apl_hmi_get_water_color) apl_hmi_get_water_color = &::apl_hmi_get_water_color;
    decltype(&::apl_hmi_get_water_lamp) apl_hmi_get_water_lamp = &::apl_hmi_get_water_lamp;
    decltype(&::apl_hmi_set_child_lock) apl_hmi_set_child_lock = &::apl_hmi_set_child_lock;
    decltype(&::apl_init) apl_init = &::apl_init;
    decltype(&::apl_link_bind) apl_link_bind = &::apl_link_bind;
    decltype(&::apl_tick) apl_tick = &::apl_tick;
};

struct AplLibraryDeleter
{
    void operator()(QLibrary *library) const
    {
        library->unload();
        delete library;
    }
};

/** 默认使用内置 APL；候选库通过完整符号和对象布局校验后才替换。 */
class AplRuntime
{
public:
    const AplApi &api() const { return m_api; }
    bool load(const QString &path, QString *error, std::shared_ptr<QTemporaryDir> directory = {});
private:
    std::shared_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QLibrary, AplLibraryDeleter> m_library;
    AplApi m_api;
};
