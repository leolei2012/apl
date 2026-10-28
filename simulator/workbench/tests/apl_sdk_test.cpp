#include "product_config.h"
#include "sdk_generator.h"
#include "apl_runtime.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>

static bool check(bool condition, const char *message)
{
    if (!condition) std::fprintf(stderr, "FAIL %s\n", message);
    return condition;
}

static bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

static bool compile(const QStringList &args)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(QString::fromUtf8(APL_PREVIEW_COMPILER), args);
    if (!process.waitForFinished(30000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        std::fprintf(stderr, "FAIL compiler: %s\n", process.readAll().constData());
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : temporary.path();
    QDir().mkpath(root);
    QString error;
    auto defaults = ProductConfig::defaults();
    if (!check(defaults.validate().isEmpty(), "defaults from embedded headers")) return 1;
    ProductConfig roundtrip;
    if (!check(defaults.save(root + "/product.apl.json", &error)
        && ProductConfig::load(root + "/product.apl.json", &roundtrip, &error)
        && defaults.toJson() == roundtrip.toJson(), "configuration round trip")) return 1;
    auto invalid = defaults.toJson();
    invalid["schemaVersion"] = 2;
    if (!check(!ProductConfig::fromJson(invalid, &roundtrip, &error), "reject unsupported schema")) return 1;
    invalid = defaults.toJson();
    auto badCore = invalid["core"].toObject();
    badCore["APL_CORE_DRY_S"] = 0.5;
    invalid["core"] = badCore;
    if (!check(!ProductConfig::fromJson(invalid, &roundtrip, &error), "reject fractional parameters")) return 1;
    auto bad = defaults;
    bad.values["APL_COMPILE_PURIFY"] = bad.values["APL_COMPILE_HUMIDIFY"] = 0;
    if (!check(!bad.validate().isEmpty(), "reject no features")) return 1;
    bad = defaults;
    bad.values["APL_COMPILE_HUMIDIFY"] = 0;
    if (!check(!bad.validate().isEmpty(), "reject enabled but uncompiled feature")) return 1;

    // Live generation must preserve project edits, not silently return embedded files.
    const QString liveRoot = root + "/live-source";
    auto liveSources = SdkGenerator::readSources({}, &error);
    liveSources["config/apl_core_config.h"].append("\n#define APL_LIVE_PRIVATE_PARAMETER 37u\n");
    liveSources["src/private.h"] = "#define APL_PRIVATE_VALUE 42\n";
    if (!check(SdkGenerator::writeDirectory(liveSources, liveRoot, &error), "live source setup")) return 1;
    const auto liveFiles = SdkGenerator::generate(defaults, &error, liveRoot);
    if (!check(liveFiles.value("config/apl_core_config.h").contains("APL_LIVE_PRIVATE_PARAMETER 37u")
        && liveFiles.value("src/private.h") == liveSources.value("src/private.h"), "live headers preserved")) return 1;
    auto missingMacro = liveSources.value("config/apl_core_config.h");
    missingMacro.replace("#define APL_DEFAULT_FAN_GEAR", "#define APL_RENAMED_FAN_GEAR");
    if (!write(liveRoot + "/config/apl_core_config.h", missingMacro)
        || !check(SdkGenerator::generate(defaults, &error, liveRoot).isEmpty(), "missing live product macro rejected")) return 1;
    const auto duplicateMacro = liveSources.value("config/apl_core_config.h") + "\n#define APL_DEFAULT_FAN_GEAR 1u\n";
    if (!write(liveRoot + "/config/apl_core_config.h", duplicateMacro)
        || !check(SdkGenerator::generate(defaults, &error, liveRoot).isEmpty(), "duplicate live product macro rejected")) return 1;
    if (!check(SdkGenerator::generate(defaults, &error, root + "/missing-source").isEmpty(),
        "missing source root must not silently use embedded APL")) return 1;

    for (int mode = 0; mode < 3; ++mode)
    {
        auto config = defaults;
        const bool purify = mode != 2, humidify = mode != 1;
        config.name = QStringLiteral("验证产品 %1").arg(mode);
        config.values["APL_COMPILE_PURIFY"] = config.values["APL_DEFAULT_PURIFY"] = purify;
        config.values["APL_COMPILE_HUMIDIFY"] = config.values["APL_DEFAULT_HUMIDIFY"] = humidify;
        config.values["APL_DEFAULT_FAN_GEAR"] = 3;
        config.values["APL_CORE_HUMIDITY_HYSTERESIS"] = 7;
        config.values["APL_HMI_COMPILE_KEY_LIGHT"] = 0;
        auto files = SdkGenerator::generate(config, &error);
        if (!check(!files.isEmpty(), error.toUtf8().constData())) return 1;
        if (!check(files["config/apl_core_config.h"].contains("#define APL_DEFAULT_FAN_GEAR 3u"), "generated numeric parameter")) return 1;
        const auto manifest = QJsonDocument::fromJson(files["manifest.json"]).object();
        if (!check(manifest["fingerprint"].toString().toLatin1() == SdkGenerator::fingerprint(files), "manifest fingerprint")) return 1;
        const QString folder = root + QString("/mode-%1").arg(mode);
        if (!check(SdkGenerator::writeDirectory(files, folder, &error)
            && SdkGenerator::writeZip(files, root + QString("/mode-%1.zip").arg(mode), &error), "SDK files and ZIP")) return 1;

        // Compile the exported C package, including the user port and example, without Qt.
        const QByteArray smoke = "#include \"apl_port.h\"\nint main(void) { struct apl app; struct apl_link_snapshot s; "
            "apl_port_init(&app); for(int i=0;i<201;i++) apl_tick(&app); "
            "struct apl_link_command c={APL_LINK_CMD_POWER,0}; apl_core_command(&app.core,&c); apl_tick(&app); "
            "apl_core_get_snapshot(&app.core,&s); return s.fan_gear!=3 || s.purify_enabled!="
            + QByteArray::number(purify) + " || s.humidify_enabled!=" + QByteArray::number(humidify)
            + " || apl_hmi_get_key_lamp(&app.hmi,APL_HMI_KEY_LIGHT)!=0; }\n";
        if (!write(folder + "/smoke.c", smoke)) return 1;
        QStringList common = {"-std=c11", "-Wall", "-Wextra", "-Werror", "-finput-charset=UTF-8",
            "-I" + folder + "/include", "-I" + folder + "/config", "-I" + folder + "/port",
            folder + "/src/apl.c", folder + "/src/apl_core.c", folder + "/src/apl_hmi.c"};
        const QString exe = folder + "/smoke.exe";
        if (!compile(common + QStringList{folder + "/port/apl_port.c", folder + "/examples/app.c",
                                          folder + "/smoke.c", "-o", exe})) return 1;
        QProcess smokeProcess;
        smokeProcess.start(exe, {});
        if (!check(smokeProcess.waitForFinished(10000) && smokeProcess.exitStatus() == QProcess::NormalExit
            && smokeProcess.exitCode() == 0, "exported consumer behavior")) return 1;

        // Load a library built from those same files and compare its actual configuration.
        const QByteArray abi = "#include \"apl.h\"\nunsigned long long apl_preview_abi(void) { return "
            "((unsigned long long)sizeof(struct apl)<<32)|((unsigned long long)sizeof(struct apl_core_ops)<<16)|sizeof(struct apl_hmi_ops); }";
        if (!write(folder + "/abi.c", abi)) return 1;
#ifdef Q_OS_WIN
        const QString library = folder + "/preview.dll";
        QStringList shared = {"-shared"};
#else
        const QString library = folder + "/preview.so";
        QStringList shared = {"-shared", "-fPIC"};
#endif
        if (!compile(common + shared + QStringList{folder + "/abi.c", "-o", library})) return 1;
        AplRuntime runtime;
        if (!check(runtime.load(library, &error), error.toUtf8().constData())) return 1;
        struct apl machine;
        runtime.api().apl_init(&machine, nullptr, nullptr, nullptr, nullptr);
        runtime.api().apl_link_bind(&machine);
        for (int i = 0; i < 201; ++i) runtime.api().apl_tick(&machine);
        struct apl_link_snapshot snapshot;
        runtime.api().apl_core_get_snapshot(&machine.core, &snapshot);
        if (!check(snapshot.purify_enabled == purify && snapshot.humidify_enabled == humidify,
                   "preview uses generated feature configuration")) return 1;
        if (!check(!runtime.load(folder + "/missing.dll", &error), "failed load rejected")) return 1;
        runtime.api().apl_tick(&machine);
        runtime.api().apl_core_get_snapshot(&machine.core, &snapshot);
        if (!check(snapshot.purify_enabled == purify, "failed load preserves previous runtime")) return 1;
    }
    std::puts("SDK configuration, export, consumer build and preview tests passed");
    return 0;
}
