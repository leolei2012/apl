#pragma once

#include "product_config.h"
#include <QMap>

class SdkGenerator
{
public:
    using Files = QMap<QString, QByteArray>;
    static Files readSources(const QString &root, QString *error);
    static Files generate(const ProductConfig &config, QString *error, const QString &sourceRoot = {});
    static QByteArray fingerprint(const Files &files);
    static bool writeDirectory(const Files &files, const QString &path, QString *error);
    static bool writeZip(const Files &files, const QString &path, QString *error);
};
