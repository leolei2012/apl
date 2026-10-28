#include "apl_runtime.h"

bool AplRuntime::load(const QString &path, QString *error, std::shared_ptr<QTemporaryDir> directory)
{
    std::unique_ptr<QLibrary, AplLibraryDeleter> library(new QLibrary(path));
    if (!library->load()) { *error = library->errorString(); return false; }
    using Abi = unsigned long long (*)();
    const auto abi = reinterpret_cast<Abi>(library->resolve("apl_preview_abi"));
    const unsigned long long expected = (static_cast<unsigned long long>(sizeof(struct apl)) << 32)
        | (static_cast<unsigned long long>(sizeof(struct apl_core_ops)) << 16) | sizeof(struct apl_hmi_ops);
    if (!abi || abi() != expected) { *error = QStringLiteral("预览库对象布局不兼容。"); return false; }
    AplApi next;
    next.apl_core_get_fan_gear = reinterpret_cast<decltype(next.apl_core_get_fan_gear)>(library->resolve("apl_core_get_fan_gear"));
    if (!next.apl_core_get_fan_gear) { *error = QStringLiteral("预览库缺少接口: apl_core_get_fan_gear"); return false; }
    next.apl_core_get_power = reinterpret_cast<decltype(next.apl_core_get_power)>(library->resolve("apl_core_get_power"));
    if (!next.apl_core_get_power) { *error = QStringLiteral("预览库缺少接口: apl_core_get_power"); return false; }
    next.apl_core_get_snapshot = reinterpret_cast<decltype(next.apl_core_get_snapshot)>(library->resolve("apl_core_get_snapshot"));
    if (!next.apl_core_get_snapshot) { *error = QStringLiteral("预览库缺少接口: apl_core_get_snapshot"); return false; }
    next.apl_core_get_timer = reinterpret_cast<decltype(next.apl_core_get_timer)>(library->resolve("apl_core_get_timer"));
    if (!next.apl_core_get_timer) { *error = QStringLiteral("预览库缺少接口: apl_core_get_timer"); return false; }
    next.apl_hmi_filter_query = reinterpret_cast<decltype(next.apl_hmi_filter_query)>(library->resolve("apl_hmi_filter_query"));
    if (!next.apl_hmi_filter_query) { *error = QStringLiteral("预览库缺少接口: apl_hmi_filter_query"); return false; }
    next.apl_hmi_get_ambient_brightness = reinterpret_cast<decltype(next.apl_hmi_get_ambient_brightness)>(library->resolve("apl_hmi_get_ambient_brightness"));
    if (!next.apl_hmi_get_ambient_brightness) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_ambient_brightness"); return false; }
    next.apl_hmi_get_ambient_color = reinterpret_cast<decltype(next.apl_hmi_get_ambient_color)>(library->resolve("apl_hmi_get_ambient_color"));
    if (!next.apl_hmi_get_ambient_color) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_ambient_color"); return false; }
    next.apl_hmi_get_child_lock = reinterpret_cast<decltype(next.apl_hmi_get_child_lock)>(library->resolve("apl_hmi_get_child_lock"));
    if (!next.apl_hmi_get_child_lock) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_child_lock"); return false; }
    next.apl_hmi_get_digits_brightness = reinterpret_cast<decltype(next.apl_hmi_get_digits_brightness)>(library->resolve("apl_hmi_get_digits_brightness"));
    if (!next.apl_hmi_get_digits_brightness) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_digits_brightness"); return false; }
    next.apl_hmi_get_digits_content = reinterpret_cast<decltype(next.apl_hmi_get_digits_content)>(library->resolve("apl_hmi_get_digits_content"));
    if (!next.apl_hmi_get_digits_content) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_digits_content"); return false; }
    next.apl_hmi_get_digits_unit = reinterpret_cast<decltype(next.apl_hmi_get_digits_unit)>(library->resolve("apl_hmi_get_digits_unit"));
    if (!next.apl_hmi_get_digits_unit) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_digits_unit"); return false; }
    next.apl_hmi_get_icon = reinterpret_cast<decltype(next.apl_hmi_get_icon)>(library->resolve("apl_hmi_get_icon"));
    if (!next.apl_hmi_get_icon) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_icon"); return false; }
    next.apl_hmi_get_light = reinterpret_cast<decltype(next.apl_hmi_get_light)>(library->resolve("apl_hmi_get_light"));
    if (!next.apl_hmi_get_light) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_light"); return false; }
    next.apl_hmi_get_key_lamp = reinterpret_cast<decltype(next.apl_hmi_get_key_lamp)>(library->resolve("apl_hmi_get_key_lamp"));
    if (!next.apl_hmi_get_key_lamp) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_key_lamp"); return false; }
    next.apl_hmi_get_powerup_lamp = reinterpret_cast<decltype(next.apl_hmi_get_powerup_lamp)>(library->resolve("apl_hmi_get_powerup_lamp"));
    if (!next.apl_hmi_get_powerup_lamp) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_powerup_lamp"); return false; }
    next.apl_hmi_get_water_color = reinterpret_cast<decltype(next.apl_hmi_get_water_color)>(library->resolve("apl_hmi_get_water_color"));
    if (!next.apl_hmi_get_water_color) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_water_color"); return false; }
    next.apl_hmi_get_water_lamp = reinterpret_cast<decltype(next.apl_hmi_get_water_lamp)>(library->resolve("apl_hmi_get_water_lamp"));
    if (!next.apl_hmi_get_water_lamp) { *error = QStringLiteral("预览库缺少接口: apl_hmi_get_water_lamp"); return false; }
    next.apl_hmi_set_child_lock = reinterpret_cast<decltype(next.apl_hmi_set_child_lock)>(library->resolve("apl_hmi_set_child_lock"));
    if (!next.apl_hmi_set_child_lock) { *error = QStringLiteral("预览库缺少接口: apl_hmi_set_child_lock"); return false; }
    next.apl_init = reinterpret_cast<decltype(next.apl_init)>(library->resolve("apl_init"));
    if (!next.apl_init) { *error = QStringLiteral("预览库缺少接口: apl_init"); return false; }
    next.apl_link_bind = reinterpret_cast<decltype(next.apl_link_bind)>(library->resolve("apl_link_bind"));
    if (!next.apl_link_bind) { *error = QStringLiteral("预览库缺少接口: apl_link_bind"); return false; }
    next.apl_tick = reinterpret_cast<decltype(next.apl_tick)>(library->resolve("apl_tick"));
    if (!next.apl_tick) { *error = QStringLiteral("预览库缺少接口: apl_tick"); return false; }
    m_library = std::move(library);
    m_directory = std::move(directory);
    m_api = next;
    return true;
}
