#include "product_config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <cmath>

const QVector<ProductField> &ProductConfig::fields()
{
    static const QVector<ProductField> list = {
        {"APL_COMPILE_PURIFY", "包含净化功能", false, 0, 1},
        {"APL_COMPILE_HUMIDIFY", "包含加湿功能", false, 0, 1},
        {"APL_DEFAULT_PURIFY", "默认启用净化", false, 0, 1},
        {"APL_DEFAULT_HUMIDIFY", "默认启用加湿", false, 0, 1},
        {"APL_DEFAULT_FAN_GEAR", "默认风速（0 睡眠，1–3 档，4 自动，5 极速）", false, 0, 5},
        {"APL_CORE_DRY_S", "关机风干时长 / 秒", false, 0, 86400},
        {"APL_CORE_DRY_FAN_GEAR", "风干档位（0–5）", false, 0, 5},
        {"APL_CORE_FILTER_LIFE_H", "滤芯寿命 / 小时", false, 1, 1000000},
        {"APL_CORE_FILTER_SHARED", "净化与加湿共用滤芯维护计时", false, 0, 1},
        {"APL_CORE_WATER_SHORTAGE_RUN_S", "缺水后继续运行 / 秒", false, 0, 86400},
        {"APL_CORE_WATER_BLINK_S", "缺水提示闪烁 / 秒", false, 0, 86400},
        {"APL_CORE_HUMIDITY_HYSTERESIS", "恒湿回差 / 百分点", false, 1, 20},
        {"APL_CORE_FACTORY_WINDOW_S", "上电产测入口保留 / 秒", false, 1, 3600},
        {"APL_CORE_FACTORY_FILTER_LEFT_S", "产测滤芯剩余寿命 / 秒", false, 1, 86400},
        {"APL_CORE_WIFI_PAIR_S", "配网超时 / 秒", false, 1, 86400},
        {"APL_CORE_FACTORY_CL_S", "恢复出厂 CL 显示 / 秒", false, 1, 60},
        {"APL_CORE_HAS_TURBO", "支持极速", false, 0, 1},
        {"APL_HMI_COMPILE_KEY_PURIFY", "Purify 键", true, 0, 1},
        {"APL_HMI_COMPILE_KEY_HUMIDITY", "Humidity 键", true, 0, 1},
        {"APL_HMI_COMPILE_KEY_TIMER", "Timer 键", true, 0, 1},
        {"APL_HMI_COMPILE_KEY_LIGHT", "Light 键", true, 0, 1},
        {"APL_HMI_COMPILE_KEY_SPEED", "Speed 键", true, 0, 1},
        {"APL_HMI_COMPILE_KEY_POWER", "Power 键", true, 0, 1},
    };
    return list;
}

static QByteArray configTemplate(bool hmi)
{
    QFile file(hmi ? ":/sdk/config/apl_hmi_config.h" : ":/sdk/config/apl_core_config.h");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

ProductConfig ProductConfig::defaults()
{
    ProductConfig config;
    for (const auto &field : fields())
    {
        const QString source = QString::fromUtf8(configTemplate(field.hmi));
        const QRegularExpression rx("(?m)^#define\\s+" + field.macro + "\\s+(\\d+)u?\\s*$");
        const auto match = rx.match(source);
        config.values[field.macro] = match.hasMatch() ? match.captured(1).toInt() : -1;
    }
    return config;
}

QString ProductConfig::validate() const
{
    if (name.trimmed().isEmpty() || name.size() > 80 || name.contains('\n') || name.contains('\r'))
        return QStringLiteral("产品名称需要 1–80 个字符，不能换行。");
    if (frontend != "round" && frontend != "bench") return QStringLiteral("面板类型无效。");
    if (values.size() != fields().size()) return QStringLiteral("配置字段不完整或包含未知字段。");
    for (const auto &field : fields())
    {
        if (!values.contains(field.macro) || values[field.macro] < field.minimum || values[field.macro] > field.maximum)
            return QStringLiteral("%1 超出范围 %2–%3。").arg(field.label).arg(field.minimum).arg(field.maximum);
    }
    if (!values["APL_COMPILE_PURIFY"] && !values["APL_COMPILE_HUMIDIFY"])
        return QStringLiteral("净化和加湿至少选择一项。");
    if (values["APL_DEFAULT_PURIFY"] > values["APL_COMPILE_PURIFY"]
        || values["APL_DEFAULT_HUMIDIFY"] > values["APL_COMPILE_HUMIDIFY"])
        return QStringLiteral("未包含的功能不能默认启用。");
    if (!values["APL_DEFAULT_PURIFY"] && !values["APL_DEFAULT_HUMIDIFY"])
        return QStringLiteral("至少默认启用一项功能，才能在面板上操作产品。");
    if (!values["APL_CORE_HAS_TURBO"] && values["APL_DEFAULT_FAN_GEAR"] == 5)
        return QStringLiteral("未支持极速时，默认档位不能选择极速。");
    if (values["APL_CORE_FACTORY_FILTER_LEFT_S"] > values["APL_CORE_FILTER_LIFE_H"] * qint64(3600))
        return QStringLiteral("产测剩余寿命不能超过完整滤芯寿命。");
    return {};
}

QJsonObject ProductConfig::toJson() const
{
    QJsonObject core, hmi;
    for (const auto &field : fields()) (field.hmi ? hmi : core)[field.macro] = values.value(field.macro);
    return {{"schemaVersion", 1}, {"name", name}, {"frontend", frontend}, {"core", core}, {"hmi", hmi}};
}

bool ProductConfig::fromJson(const QJsonObject &json, ProductConfig *result, QString *error)
{
    auto fail = [error](const QString &why) { if (error) *error = why; return false; };
    const QSet<QString> allowed = {"schemaVersion", "name", "frontend", "core", "hmi"};
    for (auto it = json.begin(); it != json.end(); ++it)
        if (!allowed.contains(it.key())) return fail("未知产品字段: " + it.key());
    if (json["schemaVersion"] != QJsonValue(1) || !json["name"].isString()
        || !json["frontend"].isString() || !json["core"].isObject() || !json["hmi"].isObject())
        return fail(QStringLiteral("不是支持的 APL 产品配置（schemaVersion = 1）。"));
    ProductConfig next;
    next.name = json["name"].toString();
    next.frontend = json["frontend"].toString();
    const auto core = json["core"].toObject(), hmi = json["hmi"].toObject();
    if (core.size() + hmi.size() != fields().size()) return fail(QStringLiteral("配置字段数量不正确。"));
    for (const auto &field : fields())
    {
        const auto value = (field.hmi ? hmi : core)[field.macro];
        const double number = value.toDouble(-1);
        if (!value.isDouble() || !std::isfinite(number) || number < field.minimum
            || number > field.maximum || std::floor(number) != number)
            return fail(QStringLiteral("配置项无效: ") + field.macro);
        next.values[field.macro] = static_cast<int>(number);
    }
    const QString why = next.validate();
    if (!why.isEmpty()) return fail(why);
    *result = next;
    return true;
}

bool ProductConfig::save(const QString &path, QString *error) const
{
    const QString why = validate();
    if (!why.isEmpty()) { if (error) *error = why; return false; }
    QSaveFile file(path);
    const auto bytes = QJsonDocument(toJson()).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    { if (error) *error = file.errorString(); return false; }
    return true;
}

bool ProductConfig::load(const QString &path, ProductConfig *result, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
    { if (error) *error = QStringLiteral("JSON 格式错误: ") + parse.errorString(); return false; }
    return fromJson(doc.object(), result, error);
}

QByteArray ProductConfig::header(bool hmi) const
{
    QString text = QString::fromUtf8(configTemplate(hmi));
    for (const auto &field : fields())
    {
        if (field.hmi != hmi) continue;
        const QRegularExpression rx("(?m)^(#define\\s+" + field.macro + "\\s+)\\d+u?[^\\r\\n]*");
        text.replace(rx, "#define " + field.macro + " " + QString::number(values.value(field.macro)) + "u");
    }
    return text.toUtf8();
}
