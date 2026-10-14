#include "workbench_window.h"

#include "apl_platform.h"

#include <QAction>
#include <QActionGroup>
#include <QFont>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace {

const QColor kWindow(QStringLiteral("#1b1e22"));
const QColor kPanel(QStringLiteral("#22262c"));
const QColor kStage(QStringLiteral("#121418"));
const QColor kText(QStringLiteral("#e8eaed"));

void paintPanel(QWidget *widget, const QColor &color)
{
    widget->setAutoFillBackground(true);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, color);
    palette.setColor(QPalette::WindowText, kText);
    palette.setColor(QPalette::Base, color);
    palette.setColor(QPalette::Text, kText);
    widget->setPalette(palette);
}

void writeSlider(QSlider *slider, QLabel *reading, int value)
{
    reading->setText(QString::number(value));
    if (slider->isSliderDown() || slider->value() == value)
    {
        return;
    }

    QSignalBlocker blocker(slider);
    slider->setValue(value);
}

}

WorkbenchWindow::WorkbenchWindow(AplPlatform *platform, QWidget *parent)
    : QMainWindow(parent)
    , m_platform(platform)
    , m_stage(nullptr)
    , m_stageNote(nullptr)
    , m_frontends(nullptr)
    , m_description(nullptr)
    , m_viewMenu(nullptr)
    , m_supplyAction(nullptr)
    , m_message(nullptr)
    , m_waterSlider(nullptr)
    , m_pm25Slider(nullptr)
    , m_humiditySlider(nullptr)
    , m_temperatureSlider(nullptr)
    , m_waterValue(nullptr)
    , m_pm25Value(nullptr)
    , m_humidityValue(nullptr)
    , m_temperatureValue(nullptr)
    , m_power(nullptr)
    , m_gear(nullptr)
    , m_timer(nullptr)
    , m_digits(nullptr)
    , m_childLock(nullptr)
    , m_panelLight(nullptr)
    , m_powerup(nullptr)
    , m_purify(nullptr)
    , m_humidify(nullptr)
    , m_fan(nullptr)
    , m_humidifier(nullptr)
    , m_uv(nullptr)
    , m_anion(nullptr)
    , m_pmReads(nullptr)
    , m_uvCalls(nullptr)
    , m_anionCalls(nullptr)
    , m_waterTriggers(nullptr)
    , m_waterRecovers(nullptr)
    , m_log(nullptr)
{
    setWindowTitle(QStringLiteral("APL 仿真台"));
    resize(1280, 820);
    setMinimumSize(1080, 700);
    buildChrome();
    bind();
    refreshFrame();
    refreshSensors();
    refreshLog();
    syncFrontendRow();
    loadStage();
}

QWidget *WorkbenchWindow::makePanel() const
{
    auto *panel = new QWidget;
    paintPanel(panel, kPanel);
    return panel;
}

QLabel *WorkbenchWindow::addSection(QVBoxLayout *layout, const QString &text)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(QStringLiteral("color: #8b949e;"));
    QFont font = label->font();
    font.setPointSize(9);
    label->setFont(font);
    layout->addWidget(label);
    return label;
}

QLabel *WorkbenchWindow::addLine(QVBoxLayout *layout, const QString &color)
{
    auto *label = new QLabel;
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(color));
    layout->addWidget(label);
    return label;
}

void WorkbenchWindow::makeSensor(QVBoxLayout *layout, const QString &title, int low, int high,
                                 QSlider **slider, QLabel **reading)
{
    auto *row = new QHBoxLayout;
    auto *name = new QLabel(title);
    name->setMinimumWidth(88);
    name->setStyleSheet(QStringLiteral("color: #c5ccd4;"));
    *reading = new QLabel(QStringLiteral("0"));
    (*reading)->setStyleSheet(QStringLiteral("color: #f4f7fb;"));
    row->addWidget(name);
    row->addWidget(*reading);
    row->addStretch();
    layout->addLayout(row);

    *slider = new QSlider(Qt::Horizontal);
    (*slider)->setRange(low, high);
    (*slider)->setSingleStep(1);
    (*slider)->setPageStep(1);
    layout->addWidget(*slider);
}

void WorkbenchWindow::buildMenu()
{
    menuBar()->setNativeMenuBar(false);
    menuBar()->setStyleSheet(QStringLiteral(
        "QMenuBar { background: #22262c; color: #e8eaed; border-bottom: 1px solid #3a414b; }"
        "QMenuBar::item { background: transparent; padding: 6px 12px; }"
        "QMenuBar::item:selected { background: #315f9a; }"
        "QMenuBar::item:pressed { background: #2f6fad; }"
        "QMenu { background: #2a3038; color: #e8eaed; border: 1px solid #3a414b; }"
        "QMenu::item { padding: 6px 28px 6px 28px; }"
        "QMenu::item:selected { background: #2f6fad; }"
        "QMenu::indicator { width: 14px; height: 14px; margin-left: 6px; }"
        "QMenu::separator { height: 1px; background: #3a414b; margin: 4px 8px; }"));

    auto *quitAction = new QAction(QStringLiteral("退出(&X)"), this);
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    m_supplyAction = new QAction(QStringLiteral("供电(&P)"), this);
    m_supplyAction->setCheckable(true);
    m_supplyAction->setChecked(m_platform->supplied());

    auto *resetAction = new QAction(QStringLiteral("重置(&R)"), this);
    resetAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    connect(resetAction, &QAction::triggered, this, [this]()
    {
        m_platform->reset();
    });

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(quitAction);

    auto *simMenu = menuBar()->addMenu(QStringLiteral("仿真(&S)"));
    simMenu->addAction(m_supplyAction);
    simMenu->addAction(resetAction);
    auto *pauseAction=simMenu->addAction(QStringLiteral("暂停时钟"));
    pauseAction->setCheckable(true);
    connect(pauseAction,&QAction::toggled,m_platform,&AplPlatform::setPaused);
    connect(m_platform,&AplPlatform::frameChanged,this,[this,pauseAction]() {
        QSignalBlocker blocker(pauseAction); pauseAction->setChecked(m_platform->paused());
    });
    auto *stepAction=simMenu->addAction(QStringLiteral("单步 10 ms"));
    connect(stepAction,&QAction::triggered,this,[this]() { m_platform->setPaused(true); m_platform->advance(1); });

    m_viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    rebuildViewMenu();

    auto *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    auto *aboutAction = helpMenu->addAction(QStringLiteral("关于 APL 仿真台(&A)"));
    connect(aboutAction, &QAction::triggered, this, &WorkbenchWindow::showAbout);

    auto *bar = addToolBar(QStringLiteral("常用"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    bar->setStyleSheet(QStringLiteral(
        "QToolBar { background: #22262c; border: none; border-bottom: 1px solid #3a414b;"
        " spacing: 2px; padding: 2px 6px; }"
        "QToolButton { background: transparent; color: #e8eaed; padding: 4px 10px; border-radius: 3px; }"
        "QToolButton:hover { background: #35404c; }"
        "QToolButton:pressed, QToolButton:checked { background: #2f6fad; }"));
    bar->addAction(m_supplyAction);
    bar->addAction(resetAction);
}

void WorkbenchWindow::buildChrome()
{
    buildMenu();

    auto *left = makePanel();
    left->setMinimumWidth(200);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(10, 10, 10, 10);
    leftLayout->setSpacing(8);
    addSection(leftLayout, QStringLiteral("前端"));
    m_frontends = new QListView;
    m_frontends->setModel(m_platform->frontends());
    m_frontends->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_frontends->setSelectionMode(QAbstractItemView::SingleSelection);
    m_frontends->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_frontends->setFrameShape(QFrame::NoFrame);
    m_frontends->setStyleSheet(QStringLiteral(
        "QListView { background: #22262c; border: none; outline: 0; }"
        "QListView::item { min-height: 36px; padding-left: 10px; color: #f4f7fb; }"
        "QListView::item:selected { background: #315f9a; color: #ffffff; }"
        "QListView::item:hover:!selected { background: #2a3038; }"));
    leftLayout->addWidget(m_frontends, 1);
    m_description = addLine(leftLayout, QStringLiteral("#d5dae1"));
    auto *hint = new QLabel(QStringLiteral("仅用于测试 APL。切换视图不会重新初始化主控。"));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8b949e;"));
    QFont hintFont = hint->font();
    hintFont.setPointSize(9);
    hint->setFont(hintFont);
    leftLayout->addWidget(hint);

    auto *stageHost = new QWidget;
    paintPanel(stageHost, kStage);
    auto *stageLayout = new QGridLayout(stageHost);
    stageLayout->setContentsMargins(8, 8, 8, 8);
    m_stage = new QQuickWidget(stageHost);
    m_stage->setObjectName(QStringLiteral("stage"));
    m_stage->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_stage->setClearColor(kStage);
    m_stage->engine()->addImportPath(QString::fromUtf8(APL_QT_PREFIX) + QStringLiteral("/qml"));
    m_stage->engine()->rootContext()->setContextProperty(QStringLiteral("platform"), m_platform);
    m_stageNote = new QLabel(stageHost);
    m_stageNote->setAlignment(Qt::AlignCenter);
    m_stageNote->setWordWrap(true);
    m_stageNote->setMinimumWidth(280);
    m_stageNote->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_stageNote->setStyleSheet(QStringLiteral("color: #ffb4a8; background: transparent;"));
    m_stageNote->hide();
    stageLayout->addWidget(m_stage, 0, 0);
    stageLayout->addWidget(m_stageNote, 0, 0, Qt::AlignCenter);

    auto *right = makePanel();
    right->setMinimumWidth(240);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    paintPanel(scroll, kPanel);
    auto *bench = new QWidget;
    paintPanel(bench, kPanel);
    auto *benchLayout = new QVBoxLayout(bench);
    benchLayout->setContentsMargins(10, 10, 10, 10);
    benchLayout->setSpacing(8);
    m_message = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    QFont messageFont = m_message->font();
    messageFont.setPointSize(14);
    m_message->setFont(messageFont);
    addSection(benchLayout, QStringLiteral("传感器"));
    makeSensor(benchLayout, QStringLiteral("水位"), 0, 4, &m_waterSlider, &m_waterValue);
    makeSensor(benchLayout, QStringLiteral("PM2.5"), 0, 1000, &m_pm25Slider, &m_pm25Value);
    makeSensor(benchLayout, QStringLiteral("湿度"), 0, 100, &m_humiditySlider, &m_humidityValue);
    makeSensor(benchLayout, QStringLiteral("温度"), -20, 60, &m_temperatureSlider, &m_temperatureValue);
    addSection(benchLayout, QStringLiteral("状态"));
    m_power = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_gear = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_timer = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_digits = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_childLock = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_panelLight = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_powerup = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_purify = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_humidify = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    addSection(benchLayout, QStringLiteral("自定义调试槽位"));
    auto *slotSummary=addLine(benchLayout,QStringLiteral("#d5dae1"));
    slotSummary->setTextFormat(Qt::PlainText);
    auto refreshSlots=[this,slotSummary]() {
        QStringList rows;
        for(const auto &entry:m_platform->displaySlots()) {
            const auto item=entry.toMap();
            rows << (item.isEmpty() ? QStringLiteral("--") : item["label"].toString()+": "+item["value"].toString());
        }
        slotSummary->setText(rows.join('\n'));
    };
    connect(m_platform,&AplPlatform::frameChanged,this,refreshSlots);
    refreshSlots();
    addSection(benchLayout, QStringLiteral("固定负载"));
    m_fan = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_humidifier = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_uv = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_anion = addLine(benchLayout, QStringLiteral("#f4f7fb"));
    m_pmReads = addLine(benchLayout, QStringLiteral("#9aa3ad"));
    m_uvCalls = addLine(benchLayout, QStringLiteral("#9aa3ad"));
    m_anionCalls = addLine(benchLayout, QStringLiteral("#9aa3ad"));
    m_waterTriggers = addLine(benchLayout, QStringLiteral("#9aa3ad"));
    m_waterRecovers = addLine(benchLayout, QStringLiteral("#9aa3ad"));
    benchLayout->addStretch();
    scroll->setWidget(bench);
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(scroll);

    auto *split = new QSplitter(Qt::Horizontal);
    split->addWidget(left);
    split->addWidget(stageHost);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({230, 720, 280});
    split->setChildrenCollapsible(false);

    auto *logHost = new QWidget;
    paintPanel(logHost, QColor(QStringLiteral("#14171b")));
    auto *logLayout = new QVBoxLayout(logHost);
    logLayout->setContentsMargins(8, 8, 8, 8);
    logLayout->setSpacing(4);
    auto *logTitle = new QLabel(QStringLiteral("事件"));
    logTitle->setStyleSheet(QStringLiteral("color: #8b949e;"));
    QFont logTitleFont = logTitle->font();
    logTitleFont.setPointSize(9);
    logTitle->setFont(logTitleFont);
    logLayout->addWidget(logTitle);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFrameShape(QFrame::NoFrame);
    m_log->setFixedHeight(132);
    QFont logFont(QStringLiteral("Consolas"));
    logFont.setPointSize(9);
    m_log->setFont(logFont);
    m_log->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: #14171b; color: #d5dae1; border: none; }"));
    logLayout->addWidget(m_log);

    auto *central = new QWidget;
    paintPanel(central, kWindow);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(10);
    outer->addWidget(split, 1);
    outer->addWidget(logHost);
    setCentralWidget(central);
}

void WorkbenchWindow::rebuildViewMenu()
{
    const QList<QActionGroup *> groups = m_viewMenu->findChildren<QActionGroup *>(
        QString(), Qt::FindDirectChildrenOnly);
    for (QActionGroup *group : groups)
    {
        delete group;
    }
    m_viewMenu->clear();
    auto *group = new QActionGroup(m_viewMenu);
    group->setExclusive(true);
    const int rows = m_platform->frontends()->rowCount();
    for (int row = 0; row < rows; ++row)
    {
        auto *action = m_viewMenu->addAction(m_platform->frontends()->nameAt(row));
        action->setCheckable(true);
        group->addAction(action);
        action->setChecked(row == m_platform->frontendIndex());
        connect(action, &QAction::triggered, this, [this, row]()
        {
            m_platform->selectFrontend(row);
        });
    }
}

void WorkbenchWindow::syncViewChecks()
{
    const QList<QAction *> actions = m_viewMenu->actions();
    const int rows = m_platform->frontends()->rowCount();
    if (actions.size() != rows)
    {
        QTimer::singleShot(0, this, &WorkbenchWindow::rebuildViewMenu);
        return;
    }

    for (int row = 0; row < rows; ++row)
    {
        if (actions.at(row)->text() != m_platform->frontends()->nameAt(row))
        {
            QTimer::singleShot(0, this, &WorkbenchWindow::rebuildViewMenu);
            return;
        }
    }

    const int selected = m_platform->frontendIndex();
    if (selected < 0 || selected >= actions.size())
    {
        return;
    }

    QSignalBlocker blocker(actions.at(selected));
    actions.at(selected)->setChecked(true);
}

void WorkbenchWindow::refreshTitle()
{
    const QString name = m_platform->frontendName();
    const QString title = name.isEmpty()
                              ? QStringLiteral("APL 仿真台")
                              : QStringLiteral("%1 - APL 仿真台").arg(name);
    if (windowTitle() != title)
    {
        setWindowTitle(title);
    }
}

void WorkbenchWindow::showAbout()
{
    QMessageBox::about(
        this,
        QStringLiteral("关于 APL 仿真台"),
        QStringLiteral("APL 仿真台用来跑净化和加湿程序。\n\n"
                       "顶部是菜单栏，中间的面贴是 QML。\n"
                       "平台持有一份 APL，每 10 毫秒调用一次 apl_tick。"));
}

void WorkbenchWindow::bind()
{
    connect(m_supplyAction, &QAction::toggled, this, [this](bool checked)
    {
        m_platform->setSupplied(checked);
    });
    connect(m_platform, &AplPlatform::suppliedChanged, this, [this]()
    {
        QSignalBlocker blocker(m_supplyAction);
        m_supplyAction->setChecked(m_platform->supplied());
    });
    connect(m_frontends->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current, const QModelIndex &)
    {
        if (current.isValid())
        {
            m_platform->selectFrontend(current.row());
        }
    });
    connect(m_platform, &AplPlatform::frontendChanged, this, [this]()
    {
        syncFrontendRow();
        syncViewChecks();
        loadStage();
    });
    connect(m_platform, &AplPlatform::frameChanged, this, &WorkbenchWindow::refreshFrame);
    connect(m_platform, &AplPlatform::eventsChanged, this, &WorkbenchWindow::refreshLog);
    connect(m_platform, &AplPlatform::waterChanged, this, &WorkbenchWindow::refreshSensors);
    connect(m_platform, &AplPlatform::pm25Changed, this, &WorkbenchWindow::refreshSensors);
    connect(m_platform, &AplPlatform::humidityChanged, this, &WorkbenchWindow::refreshSensors);
    connect(m_platform, &AplPlatform::temperatureChanged, this, &WorkbenchWindow::refreshSensors);
    connect(m_waterSlider, &QSlider::valueChanged, m_platform, &AplPlatform::setWater);
    connect(m_pm25Slider, &QSlider::valueChanged, m_platform, &AplPlatform::setPm25);
    connect(m_humiditySlider, &QSlider::valueChanged, m_platform, &AplPlatform::setHumidity);
    connect(m_temperatureSlider, &QSlider::valueChanged, m_platform, &AplPlatform::setTemperature);
    connect(m_stage, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status)
    {
        onStageStatus();
    });
}

void WorkbenchWindow::refreshFrame()
{
    m_message->setText(m_platform->message());
    refreshTitle();
    m_description->setText(m_platform->frontendDescription());
    m_power->setText(m_platform->powerOn() ? QStringLiteral("开机") : QStringLiteral("关机"));
    m_gear->setText(QStringLiteral("档位  %1").arg(m_platform->gearName()));
    m_timer->setText(QStringLiteral("定时  %1 秒").arg(m_platform->timerSeconds()));
    m_digits->setText(QStringLiteral("数码管  [%1]").arg(m_platform->digits()));
    m_childLock->setText(m_platform->childLock() ? QStringLiteral("童锁开") : QStringLiteral("童锁关"));
    m_panelLight->setText(m_platform->panelLight() ? QStringLiteral("灯光开") : QStringLiteral("灯光关"));
    m_powerup->setText(m_platform->powerupShow()
                           ? QStringLiteral("上电全灯")
                           : QStringLiteral("上电全灯已结束"));
    m_purify->setText(QStringLiteral("净化  %1").arg(
        m_platform->purifyEnabled() ? QStringLiteral("启用") : QStringLiteral("未启用")));
    m_humidify->setText(QStringLiteral("加湿  %1").arg(
        m_platform->humidifyEnabled() ? QStringLiteral("启用") : QStringLiteral("未启用")));
    m_fan->setText(QStringLiteral("风机  %1").arg(m_platform->fanText()));
    m_humidifier->setText(QStringLiteral("加湿器  %1").arg(m_platform->humidifierText()));
    m_uv->setText(QStringLiteral("UV  %1").arg(m_platform->uvText()));
    m_anion->setText(QStringLiteral("负离子  %1").arg(m_platform->anionText()));
    m_pmReads->setText(QStringLiteral("PM 读取  %1").arg(m_platform->pmReads()));
    m_uvCalls->setText(QStringLiteral("UV 调用  %1").arg(m_platform->uvCalls()));
    m_anionCalls->setText(QStringLiteral("负离子调用  %1").arg(m_platform->anionCalls()));
    m_waterTriggers->setText(QStringLiteral("水位触发  %1").arg(m_platform->waterTriggers()));
    m_waterRecovers->setText(QStringLiteral("水位恢复  %1").arg(m_platform->waterRecovers()));
}

void WorkbenchWindow::refreshSensors()
{
    writeSlider(m_waterSlider, m_waterValue, m_platform->water());
    writeSlider(m_pm25Slider, m_pm25Value, m_platform->pm25());
    writeSlider(m_humiditySlider, m_humidityValue, m_platform->humidity());
    writeSlider(m_temperatureSlider, m_temperatureValue, m_platform->temperature());
}

void WorkbenchWindow::refreshLog()
{
    const QString text = m_platform->eventLog();
    if (m_log->toPlainText() == text)
    {
        return;
    }

    m_log->setPlainText(text);
    m_log->moveCursor(QTextCursor::End);
}

void WorkbenchWindow::syncFrontendRow()
{
    const QModelIndex index = m_platform->frontends()->index(m_platform->frontendIndex(), 0);
    if (!index.isValid() || m_frontends->currentIndex() == index)
    {
        return;
    }

    QSignalBlocker blocker(m_frontends->selectionModel());
    m_frontends->setCurrentIndex(index);
}

void WorkbenchWindow::loadStage()
{
    refreshTitle();
    m_description->setText(m_platform->frontendDescription());
    const QUrl url = m_platform->frontendUrl();
    if (!url.isValid() || url.isEmpty())
    {
        m_stage->setSource(QUrl());
        m_stageNote->setText(QStringLiteral("APL 面板资源未加载"));
        m_stageNote->show();
        return;
    }

    m_stageNote->hide();
    if (m_stage->source() != url)
    {
        m_stage->setSource(url);
    }
    onStageStatus();
}

void WorkbenchWindow::onStageStatus()
{
    if (m_stage->status() != QQuickWidget::Error)
    {
        if (m_stage->status() == QQuickWidget::Ready)
        {
            m_stageNote->hide();
        }
        return;
    }

    QStringList lines;
    const QList<QQmlError> errors = m_stage->errors();
    for (const QQmlError &error : errors)
    {
        lines.append(error.toString());
    }
    m_stageNote->setText(QStringLiteral("这份 QML 没有加载\n%1\n%2")
                             .arg(m_stage->source().toString(), lines.join(QStringLiteral("\n"))));
    m_stageNote->show();
}
