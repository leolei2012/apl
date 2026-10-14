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
        || platform->waterLamps().at(0).toInt() != 255
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
    if (!platform->powerOn() || platform->keyLamps().at(APL_HMI_KEY_POWER).toInt() != 255)
    {
        std::fprintf(stderr, "FAIL power on lamp=%d\n",
                     platform->keyLamps().at(APL_HMI_KEY_POWER).toInt());
        return false;
    }

    platform->keyDown(APL_HMI_KEY_LIGHT);
    platform->keyUp(APL_HMI_KEY_LIGHT);
    if (platform->panelLight() || platform->digitBrightness() != 0)
    {
        std::fprintf(stderr, "FAIL light off\n");
        return false;
    }

    platform->keyDown(APL_HMI_KEY_TIMER);
    platform->keyUp(APL_HMI_KEY_TIMER);
    if (platform->timerSeconds() != 3600
        || platform->digits() != QStringLiteral("  1")
        || platform->digitUnit() != APL_HMI_UNIT_HOUR
        || platform->keyLamps().at(APL_HMI_KEY_TIMER).toInt() != 255)
    {
        std::fprintf(stderr, "FAIL timer digits='%s' unit=%d seconds=%d\n",
                     platform->digits().toUtf8().constData(),
                     platform->digitUnit(),
                     platform->timerSeconds());
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

    if (platform->pmReads() != 0 || platform->uvCalls() != 0 || platform->anionCalls() != 0)
    {
        std::fprintf(stderr, "FAIL library called sensors %d %d %d\n",
                     platform->pmReads(),
                     platform->uvCalls(),
                     platform->anionCalls());
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

    QMenuBar *menus = window.menuBar();
    const QStringList menuNames = {
        QStringLiteral("文件"),
        QStringLiteral("仿真"),
        QStringLiteral("视图"),
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

    platform->keyDown(APL_HMI_KEY_LIGHT);
    platform->keyUp(APL_HMI_KEY_LIGHT);

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
    if (platform->keyMask() != 0 || platform->powerOn() == beforeKeyboard)
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
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette palette;
    const QColor windowColor(QStringLiteral("#1b1e22"));
    const QColor textColor(QStringLiteral("#e8eaed"));
    const QColor buttonColor(QStringLiteral("#2a3038"));
    palette.setColor(QPalette::Window, windowColor);
    palette.setColor(QPalette::WindowText, textColor);
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#14171b")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#22262c")));
    palette.setColor(QPalette::Text, textColor);
    palette.setColor(QPalette::Button, buttonColor);
    palette.setColor(QPalette::ButtonText, textColor);
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#2f6fad")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::Mid, QColor(QStringLiteral("#3a414b")));
    palette.setColor(QPalette::Dark, QColor(QStringLiteral("#121418")));
    palette.setColor(QPalette::Shadow, QColor(QStringLiteral("#0c0e11")));
    palette.setColor(QPalette::Light, QColor(QStringLiteral("#4a5260")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#22262c")));
    palette.setColor(QPalette::ToolTipText, textColor);
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#8b949e")));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(QStringLiteral("#6d7580")));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(QStringLiteral("#6d7580")));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(QStringLiteral("#6d7580")));
    app.setPalette(palette);
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    if (!QFontInfo(font).family().contains(QStringLiteral("YaHei")))
    {
        font = QFont(QStringLiteral("Microsoft YaHei"));
    }
    font.setPointSize(10);
    app.setFont(font);

    AplPlatform platform;
    const QString dataRoot=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dataRoot);
    if(!testing && !platform.openMemory(dataRoot+"/nvram.bin")) return 2;
    if (testing)
    {
        return selfTest(&platform) ? 0 : 1;
    }

    WorkbenchWindow window(&platform);
    window.show();
    platform.start();
    return app.exec();
}
