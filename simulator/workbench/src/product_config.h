#pragma once

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

struct ProductField
{
    QString macro;
    QString label;
    bool hmi;
    int minimum;
    int maximum;
};

/** 产品配置是预览和 SDK 导出的唯一配置来源。 */
class ProductConfig
{
public:
    QString name = QStringLiteral("我的 APL 产品");
    QString frontend = QStringLiteral("round");
    QMap<QString, int> values;

    static const QVector<ProductField> &fields();
    static ProductConfig defaults();
    QString validate() const;
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject &json, ProductConfig *result, QString *error);
    bool save(const QString &path, QString *error) const;
    static bool load(const QString &path, ProductConfig *result, QString *error);
    QByteArray header(bool hmi) const;
};
