#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QToolButton>
#include "studio_theme.h"
#include <QTabWidget>
#include "product_dialog.h"
#include "sdk_preview.h"
#include "apl_platform.h"
#include "workbench_window.h"
#include <QStandardPaths>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPalette>
#include <QPixmap>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QQuickItem>
#include <QStyleFactory>
#include <QTimer>
#include <QTemporaryDir>
#include <QSaveFile>
#include <functional>
#include <QUrl>

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

void attachConsole()
{
#ifdef _WIN32
    // Keep CTest's redirected diagnostics instead of reopening a separate console.
    if (GetFileType(GetStdHandle(STD_ERROR_HANDLE)) == FILE_TYPE_PIPE)
    {
        return;
    }
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
    {
        AllocConsole();
    }
    (void)std::freopen("CONOUT$", "w", stdout);
    (void)std::freopen("CONOUT$", "w", stderr);
#else
    (void)0;
#endif
}

bool loadFrontend(QQmlEngine *engine, const QUrl &url)
{
    QQmlComponent component(engine, url);
    if (component.isError())
    {
        std::fprintf(stderr, "FAIL qml %s\n%s\n",
                     url.toString().toUtf8().constData(),
                     component.errorString().toUtf8().constData());
        return false;
    }

    QObject *object = component.create();
    if (object == nullptr)
    {
        std::fprintf(stderr, "FAIL create %s\n%s\n",
                     url.toString().toUtf8().constData(),
                     component.errorString().toUtf8().constData());
        return false;
    }

    delete object;
    return true;
}

QQuickItem *findVisualItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (auto *child : root->childItems())
        if (auto *found = findVisualItem(child, name)) return found;
    return nullptr;
}

void settleUi()
{
    QEventLoop loop;
    QTimer::singleShot(160, &loop, &QEventLoop::quit);
    loop.exec();
}

bool mouseInteractionTest()
{
    AplPlatform platform;
    WorkbenchWindow window(&platform);
    window.show();
    window.showPage(2);
    platform.start();
    settleUi();
    auto *stage = window.findChild<QQuickWidget *>("stage");
    const auto wait = [](int milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const auto click = [&](int key, int holdMs = 100) {
        const int panel = platform.frontendIndex();
        auto *item = findVisualItem(stage->rootObject(), QString(panel ? "benchKey%1" : "panelKey%1").arg(key));
        if (!item) return false;
        const QPointF local = item->mapToScene(QPointF(item->width()/2, item->height()/2));
        const QPointF global = stage->mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(stage, &press);
        wait(holdMs);
        QApplication::sendEvent(stage, &release);
        wait(50);
        return platform.keyMask() == 0;
    };
    const auto expect = [&](bool condition, const char *message) {
        if (!condition) std::fprintf(stderr, "FAIL mouse panel=%d %s\n%s\n", platform.frontendIndex(), message, platform.eventLog().toUtf8().constData());
        return condition;
    };
    for (int panel = 0; panel < 2; ++panel)
    {
        platform.selectFrontend(panel);
        platform.reset();
        platform.advance(APL_HMI_POWERUP_LAMP_TICKS);
        settleUi();
        if (!expect(click(5) && platform.runState() == APL_RUN_ON, "power starts device")) return false;
        if (!expect(click(4) && platform.gear() == APL_FAN_GEAR_2, "speed after power")) return false;
        if (!expect(click(4) && platform.gear() == APL_FAN_GEAR_3, "repeated speed")) return false;
        if (!expect(click(2) && platform.digits().trimmed() == "1", "timer editing")) return false;
        const bool purify = platform.purifyOn();
        if (!expect(click(0) && platform.purifyOn() != purify && platform.purifyEnabled(), "actual purify switch")) return false;
        if (!expect(click(0) && platform.purifyOn() == purify, "purify restored")) return false;
        const bool humidify = platform.humidifyOn();
        if (!expect(click(1) && platform.humidifyOn() != humidify && platform.humidifyEnabled(), "actual humidify switch")) return false;
        if (!expect(click(1) && platform.humidifyOn() == humidify, "humidify restored")) return false;
        if (!expect(click(3) && !platform.panelLight(), "light off")) return false;
        if (!expect(click(4) && platform.gear() == APL_FAN_GEAR_3 && platform.digitBrightness() > 0, "first touch wakes only")) return false;
        if (!expect(click(4) && platform.gear() == APL_FAN_GEAR_AUTO, "second touch executes")) return false;
        if (!expect(click(3) && platform.panelLight(), "light restored")) return false;
        auto *hint = window.findChild<QLabel *>("interactionHint");
        auto *stateLabel = window.findChild<QLabel *>("runStateLabel");
        if (!expect(click(5) && platform.runState() == APL_RUN_DRY && platform.drySeconds() > 0
            && hint && hint->text().contains("重新开机") && stateLabel && stateLabel->text().contains("关机风干")
            && platform.eventLog().contains("运行状态：关机风干"), "dry state and recovery instruction")) return false;
        const int dryLeft = platform.drySeconds();
        platform.advance(APL_CORE_TICKS_PER_SECOND);
        if (!expect(platform.drySeconds() < dryLeft, "dry countdown")) return false;
        if (!expect(click(5) && platform.runState() == APL_RUN_ON && platform.drySeconds() == 0, "short power resumes from dry")) return false;
        if (!expect(click(4) && platform.gear() == APL_FAN_GEAR_SLEEP, "buttons work after dry resume")) return false;
        if (!expect(click(5) && platform.runState() == APL_RUN_DRY, "power reenters dry")) return false;
        if (!panel)
        {
            window.grab().save(QDir::tempPath() + "/apl-dry-state.png");
        }
        if (!expect(click(5, 3100) && platform.runState() == APL_RUN_STANDBY, "long power ends dry once")) return false;
        if (!expect(click(5) && platform.runState() == APL_RUN_ON, "can restart after complete shutdown")) return false;
    }
    std::puts("Mouse power cycle, six keys, dry recovery and state display tests passed");
    return true;
}

bool developmentTest()
{
    QTemporaryDir sources;
    QString error;
    const auto originals = SdkGenerator::readSources({}, &error);
    if (!sources.isValid() || !SdkGenerator::writeDirectory(originals, sources.path(), &error)) return false;
    AplPlatform platform;
    WorkbenchWindow window(&platform);
    window.show();
    platform.start();
    const auto save = [&](const QString &relative, const QByteArray &bytes) {
        const QString path = sources.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    };
    const auto waitPreview = [&](const std::function<bool()> &action, bool expected) {
        QEventLoop loop;
        QTimer deadline;
        deadline.setSingleShot(true);
        bool completed = false, result = false;
        const auto connection = QObject::connect(&window, &WorkbenchWindow::previewFinished, &loop, [&](bool success) {
            completed = true; result = success; loop.quit();
        });
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        const bool acted = action();
        if (acted && !completed) { deadline.start(30000); loop.exec(); }
        QObject::disconnect(connection);
        if (!acted || !completed || result != expected)
        {
            auto *job = window.findChild<SdkPreviewJob *>();
            std::fprintf(stderr, "FAIL live build expected=%d completed=%d: %s\n", expected, completed,
                job ? job->error().toUtf8().constData() : "no job");
            return false;
        }
        return true;
    };
    if (!waitPreview([&]() { return window.setSourceRoot(sources.path(), &error); }, true)) return false;
    const auto firstFiles = window.verifiedFiles();
    auto *editor = window.findChild<ProductEditor *>("productEditor");
    auto *draftName = editor->findChild<QLineEdit *>("productNameInput");
    draftName->setText("尚未应用的草稿");
    QByteArray changed = originals.value("src/apl.c");
    changed.replace("#include \"apl.h\"", "#include \"apl.h\"\n#include \"live/helper.h\"");
    changed.replace("apl_hmi_init(&self->hmi, hmi_ops, hmi_ctx);",
        "apl_hmi_init(&self->hmi, hmi_ops, hmi_ctx);\n    apl_core_set_fan_gear(&self->core, (enum apl_fan_gear)live_test_gear());");
    const QByteArray helperOne = "#include \"helper.h\"\nint live_test_gear(void) { return 1; }\n";
    const QByteArray helperTwo = "#include \"helper.h\"\nint live_test_gear(void) { return 2; }\n";
    int heartbeats = 0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &window, [&]() { ++heartbeats; });
    heartbeat.start();
    if (!waitPreview([&]() {
        return save("src/live/helper.h", "int live_test_gear(void);\n")
            && save("src/live/helper.c", helperOne) && save("src/apl.c", changed);
    }, true)) return false;
    if (platform.gear() != 1 || heartbeats < 3 || !window.verifiedFiles().contains("src/live/helper.c")
        || window.verifiedFiles().value("product.apl.json") != firstFiles.value("product.apl.json"))
    { std::fprintf(stderr, "FAIL live behavior/new files/responsive UI/draft isolation\n"); return false; }
    // Every atomic save replaces the file handle; subsequent saves must still be observed.
    if (!waitPreview([&]() { return save("src/live/helper.c", helperTwo); }, true) || platform.gear() != 2)
    { std::fprintf(stderr, "FAIL second atomic save\n"); return false; }
    const auto verified = window.verifiedFiles();
    platform.setPaused(true);
    if (!waitPreview([&]() { return save("src/live/helper.c", "#error deliberate_invalid_source\n"); }, false)) return false;
    platform.advance(3);
    if (window.verifiedFiles() != verified || platform.gear() != 2 || !platform.paused())
    { std::fprintf(stderr, "FAIL failed build must preserve runtime, snapshot and pause\n"); return false; }
    if (!waitPreview([&]() { return save("src/live/helper.c", helperOne); }, true) || platform.gear() != 1 || !platform.paused())
    { std::fprintf(stderr, "FAIL recovery after compile error\n"); return false; }
    // Public callback changes cannot be safely hot loaded even when their sizes match.
    QByteArray incompatible = originals.value("include/apl.h");
    incompatible.append("\nvoid live_changed_public_interface(void);\n");
    if (!waitPreview([&]() { return save("include/apl.h", incompatible); }, false)
        || platform.gear() != 1) return false;
    if (!waitPreview([&]() { return save("include/apl.h", originals.value("include/apl.h")); }, true)) return false;
    // Delete/recreate is common during branch changes; keep watching for recovery.
    if (!waitPreview([&]() { return QFile::remove(sources.filePath("src/apl.c")); }, false)) return false;
    if (!waitPreview([&]() { return save("src/apl.c", changed); }, true)) return false;
    // A save arriving during a manual rebuild must supersede that build before activation.
    auto *rebuild = window.findChild<QAction *>("rebuildAplAction");
    if (!rebuild || !waitPreview([&]() {
        rebuild->trigger();
        return save("src/live/helper.c", helperTwo);
    }, true) || platform.gear() != 2 || window.verifiedFiles().value("src/live/helper.c") != helperTwo) return false;
    const auto exportSnapshot = window.verifiedFiles();
    if (!save("src/live/helper.c", "#error unverified_change\n")
        || !SdkGenerator::writeZip(exportSnapshot, sources.filePath("verified.zip"), &error)
        || window.verifiedFiles() != exportSnapshot)
    { std::fprintf(stderr, "FAIL verified export snapshot isolation\n"); return false; }
    // No preview should be changed until the new files successfully build and load.
    window.showPage(2);
    settleUi();
    window.grab().save(QDir::tempPath() + "/apl-live-source.png");
    std::puts("Live APL source reload, compile recovery and snapshot tests passed");
    return true;
}

bool selfTest(AplPlatform *platform)
{
    if (platform->frontends()->rowCount() < 2)
    {
        std::fprintf(stderr, "FAIL frontend count %d\n", platform->frontends()->rowCount());
        return false;
    }
    if (platform->frontendName() != QStringLiteral("圆形面板"))
    {
        std::fprintf(stderr, "FAIL default frontend\n");
        return false;
    }
    if (!platform->powerupShow() || platform->digits() != QStringLiteral("888"))
    {
        std::fprintf(stderr, "FAIL powerup start digits=%s show=%d\n",
                     platform->digits().toUtf8().constData(),
                     platform->powerupShow() ? 1 : 0);
        return false;
    }
    if (platform->keyLamps().at(APL_HMI_KEY_POWER).toInt() != 255
        || platform->digitBrightness() != 255
        || platform->waterLamps().at(0).toInt() != 255)
    {
        std::fprintf(stderr, "FAIL powerup lamps\n");
        return false;
    }

    platform->advance(APL_HMI_POWERUP_LAMP_TICKS - 1);
    if (!platform->powerupShow())
    {
        std::fprintf(stderr, "FAIL show ended early\n");
        return false;
    }

    platform->advance(1);
    if (platform->powerupShow()
        || platform->keyLamps().at(APL_HMI_KEY_POWER).toInt() != 128
        || platform->keyLamps().at(APL_HMI_KEY_SPEED).toInt() != 0
        || platform->digits() != QStringLiteral("   ")
        || platform->digitBrightness() != 0
        || platform->waterLamps().at(0).toInt() != 0
        || platform->waterLamps().at(2).toInt() != 0)
    {
        std::fprintf(stderr, "FAIL after show power=%d speed=%d digits='%s' bright=%d water=%d,%d\n",
                     platform->keyLamps().at(APL_HMI_KEY_POWER).toInt(),
                     platform->keyLamps().at(APL_HMI_KEY_SPEED).toInt(),
                     platform->digits().toUtf8().constData(),
                     platform->digitBrightness(),
                     platform->waterLamps().at(0).toInt(),
                     platform->waterLamps().at(2).toInt());
        return false;
    }

    platform->setWater(0);
    platform->advance(1);
    platform->setWater(2);
    platform->advance(1);
    if (platform->waterTriggers() != 1 || platform->waterRecovers() != 1)
    {
        std::fprintf(stderr, "FAIL water edge %d %d\n",
                     platform->waterTriggers(),
                     platform->waterRecovers());
        return false;
    }

    platform->keyDown(APL_HMI_KEY_POWER);
    platform->keyUp(APL_HMI_KEY_POWER);
    platform->advance(1);
    if (!platform->powerOn() || platform->keyLamps().at(APL_HMI_KEY_POWER).toInt() != 255)
    {
        std::fprintf(stderr, "FAIL power on lamp=%d\n",
                     platform->keyLamps().at(APL_HMI_KEY_POWER).toInt());
        return false;
    }

    // 运行显示由 APL 快照驱动；传感器采样后显示 PM2.5。
    platform->setPm25(123);
    platform->setHumidity(67);
    platform->advance(100);
    if (platform->digits() != QStringLiteral("123")
        || platform->digitUnit() != APL_HMI_UNIT_PM25 || platform->digitBrightness() != 255)
    {
        std::fprintf(stderr, "FAIL PM display '%s' unit=%d\n", platform->digits().toUtf8().constData(), platform->digitUnit());
        return false;
    }
    platform->advance(APL_HMI_ROTATE_TICKS);
    if (platform->digits() != QStringLiteral(" 67") || platform->digitUnit() != APL_HMI_UNIT_PERCENT)
    {
        std::fprintf(stderr, "FAIL humidity rotation\n");
        return false;
    }

    platform->keyDown(APL_HMI_KEY_LIGHT);
    platform->keyUp(APL_HMI_KEY_LIGHT);
    if (platform->panelLight() || platform->digitBrightness() != 0)
    {
        std::fprintf(stderr, "FAIL light off\n");
        return false;
    }
    // 熄屏后的首次触摸只唤醒，不设置定时。
    platform->keyDown(APL_HMI_KEY_TIMER);
    platform->keyUp(APL_HMI_KEY_TIMER);
    if (platform->timerSeconds() != 0 || platform->digitBrightness() != 255)
    {
        std::fprintf(stderr, "FAIL wake only\n");
        return false;
    }
    platform->keyDown(APL_HMI_KEY_LIGHT);
    platform->keyUp(APL_HMI_KEY_LIGHT);
    platform->keyDown(APL_HMI_KEY_TIMER);
    platform->keyUp(APL_HMI_KEY_TIMER);
    if (platform->timerSeconds() != 0 || platform->digits() != QStringLiteral("  1"))
    {
        std::fprintf(stderr, "FAIL timer setting\n");
        return false;
    }
    // 3 秒无操作后确认，恢复传感器显示而不是一直显示定时。
    platform->advance(APL_HMI_IDLE_TICKS + 1);
    if (platform->timerSeconds() != 3600 || platform->digitUnit() == APL_HMI_UNIT_HOUR
        || platform->keyLamps().at(APL_HMI_KEY_TIMER).toInt() != 255)
    {
        std::fprintf(stderr, "FAIL timer confirmation seconds=%d unit=%d\n", platform->timerSeconds(), platform->digitUnit());
        return false;
    }
    if (!platform->eventLog().contains(QStringLiteral("Light 短按"))
        || !platform->eventLog().contains(QStringLiteral("灯光关")))
    {
        std::fprintf(stderr, "FAIL gesture/result events\n");
        return false;
    }

    platform->keyDown(APL_HMI_KEY_TIMER);
    platform->advance(APL_HMI_LONG_PRESS_TICKS - 1);
    if (!platform->childLock())
    {
        std::fprintf(stderr, "FAIL child lock\n");
        return false;
    }
    platform->keyUp(APL_HMI_KEY_TIMER);

    const bool power_before = platform->powerOn();
    platform->keyDown(APL_HMI_KEY_POWER);
    platform->keyUp(APL_HMI_KEY_POWER);
    if (platform->powerOn() != power_before)
    {
        std::fprintf(stderr, "FAIL child lock blocked power\n");
        return false;
    }

    platform->keyDown(APL_HMI_KEY_TIMER);
    platform->advance(APL_HMI_LONG_PRESS_TICKS - 1);
    platform->keyUp(APL_HMI_KEY_TIMER);
    if (platform->childLock())
    {
        std::fprintf(stderr, "FAIL unlock\n");
        return false;
    }

    if (platform->purifyEnabled() && platform->pmReads() == 0)
    {
        std::fprintf(stderr, "FAIL running core did not sample PM2.5\n");
        return false;
    }

    platform->queryFilter();
    if (platform->digits() != QStringLiteral(" 99") || platform->digitUnit() != APL_HMI_UNIT_NONE)
    {
        std::fprintf(stderr, "FAIL filter display\n");
        return false;
    }
    platform->advance(APL_HMI_IDLE_TICKS + 1);
    if (platform->digitUnit() == APL_HMI_UNIT_NONE)
    {
        std::fprintf(stderr, "FAIL filter query timeout\n");
        return false;
    }
    platform->keyDown(APL_HMI_KEY_HUMIDITY);
    platform->advance(APL_HMI_LONG_PRESS_TICKS - 1);
    platform->keyUp(APL_HMI_KEY_HUMIDITY);
    if (platform->digits() != QStringLiteral(" OF"))
    {
        std::fprintf(stderr, "FAIL humidity target OFF\n");
        return false;
    }
    for (int i = 0; i < 2; ++i)
    {
        platform->keyDown(APL_HMI_KEY_HUMIDITY);
        platform->keyUp(APL_HMI_KEY_HUMIDITY);
    }
    if (platform->digits() != QStringLiteral(" 45"))
    {
        std::fprintf(stderr, "FAIL humidity target setting\n");
        return false;
    }
    platform->advance(APL_HMI_IDLE_TICKS + 1);
    if (platform->iconLamps().at(APL_HMI_ICON_HUMIDITY).toInt() != 255
        || !platform->eventLog().contains(QStringLiteral("Humidity 长按")))
    {
        std::fprintf(stderr, "FAIL humidity target confirmation\n");
        return false;
    }

    const bool power_held = platform->powerOn();
    const int timer_held = platform->timerSeconds();
    platform->selectFrontend(1);
    if (platform->frontendName() != QStringLiteral("按键台")
        || platform->powerOn() != power_held
        || platform->timerSeconds() != timer_held)
    {
        std::fprintf(stderr, "FAIL switch frontend\n");
        return false;
    }
    platform->selectFrontend(0);

    QQmlEngine engine;
    engine.addImportPath(QString::fromUtf8(APL_QT_PREFIX) + QStringLiteral("/qml"));
    engine.rootContext()->setContextProperty(QStringLiteral("platform"), platform);
    if (!loadFrontend(&engine, platform->frontends()->urlAt(0))
        || !loadFrontend(&engine, platform->frontends()->urlAt(1)))
    {
        return false;
    }

    WorkbenchWindow window(platform);
    window.resize(1280, 800);
    window.show();
    QCoreApplication::processEvents();
    settleUi();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-studio-overview.png"));
    auto *pages = window.findChild<QStackedWidget *>("workspacePages");
    auto *inlineEditor = window.findChild<ProductEditor *>("productEditor");
    auto *configure = window.findChild<QPushButton *>("navigation1");
    auto *primary = window.findChild<QPushButton *>("primaryAction");
    if (!pages || pages->count() != 4 || pages->currentIndex() != 0 || !inlineEditor || !configure || !primary)
    {
        std::fprintf(stderr, "FAIL studio navigation structure\n"); return false;
    }
    const bool beforeEditing = platform->powerOn();
    const int timerBeforeEditing = platform->timerSeconds();
    configure->click();
    auto *nameInput = inlineEditor->findChild<QLineEdit *>("productNameInput");
    auto *modePurify = inlineEditor->findChild<QPushButton *>("productMode1");
    auto *modeBoth = inlineEditor->findChild<QPushButton *>("productMode0");
    auto *gearInput = inlineEditor->findChild<QComboBox *>("APL_DEFAULT_FAN_GEAR");
    auto *lightInput = inlineEditor->findChild<QCheckBox *>("APL_HMI_COMPILE_KEY_LIGHT");
    if (!nameInput || !modePurify || !modeBoth || !gearInput || !lightInput || pages->currentIndex() != 1)
    {
        std::fprintf(stderr, "FAIL inline configuration controls\n"); return false;
    }
    const QString originalName = nameInput->text();
    nameInput->setFocus(); nameInput->selectAll();
    QKeyEvent eraseName(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QApplication::sendEvent(nameInput, &eraseName);
    if (primary->isEnabled() || inlineEditor->config().validate().isEmpty())
    {
        std::fprintf(stderr, "FAIL inline invalid configuration\n"); return false;
    }
    QKeyEvent restoreName(QEvent::KeyPress, 0, Qt::NoModifier, originalName);
    QApplication::sendEvent(nameInput, &restoreName);
    modePurify->click();
    const int originalGear = gearInput->currentIndex();
    gearInput->setCurrentIndex(3);
    window.showPage(3);
    window.showPage(1);
    if (!primary->isEnabled() || inlineEditor->config().values["APL_COMPILE_HUMIDIFY"] != 0
        || gearInput->currentIndex() != 3 || platform->powerOn() != beforeEditing
        || platform->timerSeconds() != timerBeforeEditing || !platform->humidifyEnabled())
    {
        std::fprintf(stderr, "FAIL editing must preserve active APL and retain draft\n"); return false;
    }
    gearInput->setCurrentIndex(originalGear);
    modeBoth->click();
    lightInput->setFocus();
    QKeyEvent switchPress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent switchRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(lightInput, &switchPress);
    QApplication::sendEvent(lightInput, &switchRelease);
    if (inlineEditor->config().values["APL_HMI_COMPILE_KEY_LIGHT"] != 0)
    {
        std::fprintf(stderr, "FAIL switch keyboard interaction\n"); return false;
    }
    QApplication::sendEvent(lightInput, &switchPress);
    QApplication::sendEvent(lightInput, &switchRelease);
    if (window.isWindowModified())
    {
        std::fprintf(stderr, "FAIL reverting configuration must clear dirty state\n"); return false;
    }
    QCoreApplication::processEvents();
    settleUi();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-studio-configuration.png"));
    window.showPage(3);
    QCoreApplication::processEvents();
    settleUi();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-studio-delivery.png"));
    window.showPage(2);
    QCoreApplication::processEvents();
    auto *eventsToggle = window.findChild<QToolButton *>("eventsToggle");
    auto *eventFilter = window.findChild<QLineEdit *>("eventFilter");
    auto *eventLog = window.findChild<QPlainTextEdit *>("eventLog");
    auto *pmInput = window.findChild<QSpinBox *>("pm25Input");
    if (!pmInput) { std::fprintf(stderr, "FAIL precise sensor input\n"); return false; }
    auto *sensorText = pmInput->findChild<QLineEdit *>();
    if (!sensorText || sensorText->width() < 45 || sensorText->height() < sensorText->fontMetrics().height())
    { std::fprintf(stderr, "FAIL sensor text is clipped\n"); return false; }
    const int beforePm = platform->pm25();
    pmInput->setValue(240);
    if (platform->pm25() != 240) { std::fprintf(stderr, "FAIL sensor input synchronization\n"); return false; }
    pmInput->setValue(beforePm);
    if (!eventsToggle || !eventFilter || !eventLog)
    {
        std::fprintf(stderr, "FAIL event drawer controls\n"); return false;
    }
    eventsToggle->click();
    eventFilter->setText("Light");
    if (eventLog->toPlainText().isEmpty()) { std::fprintf(stderr, "FAIL event filter empty\n"); return false; }
    for (const auto &row : eventLog->toPlainText().split('\n', Qt::SkipEmptyParts))
        if (!row.contains("Light", Qt::CaseInsensitive)) { std::fprintf(stderr, "FAIL event filter\n"); return false; }
    QCoreApplication::processEvents();
    settleUi();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-studio-events.png"));
    eventFilter->clear();
    eventsToggle->click();
    QCoreApplication::processEvents();
    QQuickWidget *stage = window.findChild<QQuickWidget *>(QStringLiteral("stage"));
    if (stage == nullptr || stage->status() != QQuickWidget::Ready)
    {
        std::fprintf(stderr, "FAIL stage status %d\n",
                     stage == nullptr ? -1 : static_cast<int>(stage->status()));
        if (stage != nullptr)
        {
            const QList<QQmlError> errors = stage->errors();
            for (const QQmlError &error : errors)
            {
                std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData());
            }
        }
        return false;
    }

    auto *pauseControl = window.findChild<QToolButton *>("pausePreview");
    if (!pauseControl) { std::fprintf(stderr, "FAIL preview clock control\n"); return false; }
    pauseControl->click();
    if (!platform->paused()) { std::fprintf(stderr, "FAIL pause preview\n"); return false; }
    pauseControl->click();
    if (platform->paused()) { std::fprintf(stderr, "FAIL resume preview\n"); return false; }
    platform->selectFrontend(1);
    QCoreApplication::processEvents();
    auto *benchSpeed = findVisualItem(stage->rootObject(), "benchKey4");
    if (!benchSpeed) { std::fprintf(stderr, "FAIL key bench keyboard target\n"); return false; }
    benchSpeed->forceActiveFocus();
    const int gearBeforeBench = platform->gear();
    QKeyEvent benchPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QKeyEvent benchRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(stage, &benchPress);
    if ((platform->keyMask() & (1 << APL_HMI_KEY_SPEED)) == 0)
    { std::fprintf(stderr, "FAIL key bench keyboard press\n"); return false; }
    QApplication::sendEvent(stage, &benchRelease);
    if (platform->keyMask() != 0 || platform->gear() == gearBeforeBench)
    { std::fprintf(stderr, "FAIL key bench keyboard release\n"); return false; }
    platform->advance(1);
    settleUi();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-studio-keybench.png"));
    platform->selectFrontend(0);
    QCoreApplication::processEvents();

    QMenuBar *menus = window.menuBar();
    const QStringList menuNames = {
        QStringLiteral("文件"),
        QStringLiteral("仿真"),
        QStringLiteral("视图"),
        QStringLiteral("产品"),
        QStringLiteral("帮助")
    };
    if (menus == nullptr || menus->actions().size() != menuNames.size())
    {
        std::fprintf(stderr, "FAIL menu count\n");
        return false;
    }
    for (qsizetype index = 0; index < menuNames.size(); ++index)
    {
        QMenu *menu = menus->actions().at(index)->menu();
        if (menu == nullptr || !menus->actions().at(index)->text().contains(menuNames.at(index)))
        {
            std::fprintf(stderr, "FAIL menu %lld\n", static_cast<long long>(index));
            return false;
        }
        if (menu->actions().isEmpty())
        {
            std::fprintf(stderr, "FAIL menu empty %lld\n", static_cast<long long>(index));
            return false;
        }
    }
    bool roundChecked = false;
    const QList<QAction *> viewActions = menus->actions().at(2)->menu()->actions();
    for (QAction *action : viewActions)
    {
        if (action->text() == QStringLiteral("圆形面板") && action->isChecked())
        {
            roundChecked = true;
        }
    }
    if (!roundChecked)
    {
        std::fprintf(stderr, "FAIL view menu\n");
        return false;
    }

    QEventLoop loop;
    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();
    const QPixmap image = window.grab();
    const QString path = QDir::tempPath() + QStringLiteral("/apl-workbench.png");
    if (!image.isNull())
    {
        image.save(path);
        std::fprintf(stdout, "image %s\n", path.toUtf8().constData());
    }

    // 圆形前端的键盘输入应当走与鼠标相同的 APL 按键电平。
    QQuickItem *powerKey = stage->rootObject()->findChild<QQuickItem *>(QStringLiteral("panelKey5"));
    QQuickItem *face = stage->rootObject()->findChild<QQuickItem *>(QStringLiteral("panelFace"));
    if (powerKey == nullptr || face == nullptr)
    {
        std::fprintf(stderr, "FAIL round panel items\n");
        return false;
    }
    powerKey->forceActiveFocus();
    const bool beforeKeyboard = platform->powerOn();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(stage, &press);
    if ((platform->keyMask() & (1 << APL_HMI_KEY_POWER)) == 0)
    {
        std::fprintf(stderr, "FAIL keyboard key down\n");
        return false;
    }
    QApplication::sendEvent(stage, &release);
    platform->advance(1);
    // 加湿运行关机先进入风干，仍有供电和风机输出。
    if (platform->keyMask() != 0 || (platform->powerOn() == beforeKeyboard
        && platform->iconLamps().at(APL_HMI_ICON_DRY).toInt() != 255))
    {
        std::fprintf(stderr, "FAIL keyboard key release\n");
        return false;
    }
    for (const QSize size : {QSize(1080, 700), QSize(1280, 960)})
    {
        window.resize(size);
        QCoreApplication::processEvents();
        const QRectF faceBounds = face->mapRectToItem(stage->rootObject(), face->boundingRect());
        if (!stage->rootObject()->boundingRect().contains(faceBounds))
        {
            std::fprintf(stderr, "FAIL scaled panel bounds\n");
            return false;
        }
    }
    window.resize(1280, 800);
    powerKey->setFocus(false);
    platform->reset();
    QEventLoop previewLoop;
    QTimer::singleShot(200, &previewLoop, &QEventLoop::quit);
    previewLoop.exec();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-workbench-all-lamps.png"));

    platform->advance(APL_HMI_POWERUP_LAMP_TICKS);
    platform->keyDown(APL_HMI_KEY_POWER);
    platform->keyDown(APL_HMI_KEY_TIMER);
    platform->advance(APL_HMI_LONG_PRESS_TICKS);
    platform->keyUp(APL_HMI_KEY_POWER);
    platform->keyUp(APL_HMI_KEY_TIMER);
    platform->advance(1);
    if (platform->digits() != QStringLiteral("888") || platform->iconLamps().at(APL_HMI_ICON_UV).toInt() != 255)
    {
        std::fprintf(stderr, "FAIL factory display\n");
        return false;
    }
    platform->keyDown(APL_HMI_KEY_POWER);
    platform->keyUp(APL_HMI_KEY_POWER);
    platform->advance(1);
    if (platform->digits() != QStringLiteral(" CL") || platform->digitBrightness() != 255)
    {
        std::fprintf(stderr, "FAIL factory complete display\n");
        return false;
    }
    QCoreApplication::processEvents();
    window.grab().save(QDir::tempPath() + QStringLiteral("/apl-workbench-cl.png"));

    QTemporaryDir memoryDir;
    uint8_t data[3]={1,2,3}, saved[3]={0,0,0};
    if(!memoryDir.isValid() || !platform->openMemory(memoryDir.path()+"/nvram.bin")
       || !platform->memoryWrite(16,data,3)) { std::fprintf(stderr,"FAIL host memory write\n"); return false; }
    platform->displaySet(0,QStringLiteral("阶段"),QStringLiteral("测试"));
    platform->fanSet(1250); platform->humidifierSet(true);
    if(!platform->fanText().contains("1250") || platform->humidifierText()!=QStringLiteral("开启")) {
        std::fprintf(stderr,"FAIL fixed loads\n"); return false;
    }
    platform->setSupplied(false);
    platform->advance(300);
    if(platform->memoryRead(16,saved,3) || platform->memoryWrite(16,data,3) || platform->memoryErase()
       || !platform->fanText().contains(QStringLiteral("无供电"))) {
        std::fprintf(stderr,"FAIL no-supply behavior\n"); return false;
    }
    platform->setSupplied(true);
    if(!platform->memoryRead(16,saved,3) || saved[2]!=3 || platform->powerOn() || platform->timerSeconds()!=0
       || platform->childLock() || platform->fanText()!=QStringLiteral("未驱动")
       || platform->displaySlots().at(0).toMap()["value"].toString()!=QStringLiteral("测试")) {
        std::fprintf(stderr,"FAIL supply restore\n"); return false;
    }
    platform->displayClear(0);
    if(platform->frontends()->rowCount()!=2) { std::fprintf(stderr,"FAIL built-in panels\n"); return false; }
    auto config = ProductConfig::defaults();
    config.values["APL_COMPILE_HUMIDIFY"] = config.values["APL_DEFAULT_HUMIDIFY"] = 0;
    config.values["APL_HMI_COMPILE_KEY_LIGHT"] = 0;
    config.values["APL_DEFAULT_FAN_GEAR"] = 3;
    ProductDialog productDialog(config, &window);
    productDialog.show();
    QCoreApplication::processEvents();
    settleUi();
    productDialog.grab().save(QDir::tempPath() + QStringLiteral("/apl-product-config.png"));
    auto *productTabs = productDialog.findChild<QTabWidget *>();
    for (int page = 1; productTabs && page < 3; ++page)
    {
        productTabs->setCurrentIndex(page);
        QCoreApplication::processEvents();
        settleUi();
        productDialog.grab().save(QDir::tempPath() + QStringLiteral("/apl-product-config-%1.png").arg(page));
    }
    productDialog.hide();
    QString sdkError;
    const auto sdkFiles = SdkGenerator::generate(config, &sdkError);
    const auto preview = SdkPreview::build(sdkFiles, SdkPreview::defaultCompiler(), &window, &sdkError);
    if (preview.library.isEmpty() || !platform->loadRuntime(preview.library, &sdkError, preview.directory))
    {
        std::fprintf(stderr, "FAIL configured preview: %s\n", sdkError.toUtf8().constData());
        return false;
    }
    platform->advance(APL_HMI_POWERUP_LAMP_TICKS);
    platform->keyDown(APL_HMI_KEY_POWER);
    platform->keyUp(APL_HMI_KEY_POWER);
    platform->advance(1);
    if (!platform->purifyEnabled() || platform->humidifyEnabled() || platform->gear() != 3
        || platform->keyLamps().at(APL_HMI_KEY_LIGHT).toInt() != 0
        || !platform->fanText().contains(QStringLiteral("三档")) || !platform->panelLight())
    {
        std::fprintf(stderr, "FAIL configured preview behavior\n");
        return false;
    }

    QFile log(QDir::tempPath() + QStringLiteral("/apl-workbench-selftest.txt"));
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        log.write("self-test ok\n");
    }
    std::fprintf(stdout, "self-test ok\n");
    return true;
}

}

int main(int argc, char *argv[])
{
    bool testing = false;
    for (int index = 1; index < argc; ++index)
    {
        if (std::strcmp(argv[index], "--self-test") == 0)
        {
            testing = true;
        }
    }
    if (testing)
    {
        attachConsole();
    }

    const QString prefix = QString::fromUtf8(APL_QT_PREFIX);
    QCoreApplication::addLibraryPath(prefix + QStringLiteral("/plugins"));

    qputenv("QT_QUICK_CONTROLS_STYLE", "Fusion");
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("APL 仿真台"));
    QApplication::setApplicationDisplayName(QStringLiteral("APL Studio"));
    StudioTheme::apply(app);

    AplPlatform platform;
    const QString dataRoot=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataRoot);
    if(!testing && !platform.openMemory(dataRoot+"/nvram.bin")) return 2;
    if (testing)
    {
        return mouseInteractionTest() && selfTest(&platform) && developmentTest() ? 0 : 1;
    }

    WorkbenchWindow window(&platform);
    window.show();
    platform.start();
    window.startDevelopment();
    return app.exec();
}
