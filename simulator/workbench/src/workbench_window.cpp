#include "workbench_window.h"
#include "apl_platform.h"
#include "product_dialog.h"
#include "studio_theme.h"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFont>
#include <QFrame>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QLabel *label(const QString &text = {}, const char *role = nullptr)
{
    auto *item = new QLabel(text);
    item->setTextFormat(Qt::PlainText);
    item->setWordWrap(true);
    if (role) item->setProperty("role", role);
    if (role && QByteArray(role) == "badge") item->setWordWrap(false);
    return item;
}
QFrame *card(const char *role = "card")
{
    auto *frame = new QFrame;
    frame->setProperty("role", role);
    return frame;
}
QVBoxLayout *column(QWidget *widget, int margin = 22, int spacing = 14)
{
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(spacing);
    return layout;
}
QPushButton *button(const QString &text, const char *role = nullptr)
{
    auto *item = new QPushButton(text);
    if (role) item->setProperty("role", role);
    item->setCursor(Qt::PointingHandCursor);
    return item;
}
QWidget *scrolling(QWidget *content)
{
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    return scroll;
}
void writeSensor(QSlider *slider, QSpinBox *reading, int value)
{
    const QSignalBlocker first(slider), second(reading);
    slider->setValue(value);
    reading->setValue(value);
}
}

WorkbenchWindow::WorkbenchWindow(AplPlatform *platform, QWidget *parent)
    : QMainWindow(parent), m_platform(platform)
{
    m_savedProduct = m_product.toJson();
    resize(1380, 900);
    setMinimumSize(1080, 700);
    buildMenu();
    buildChrome();
    bind();
    initializeDevelopment();
    refreshProduct();
    refreshFrame();
    refreshSensors();
    refreshLog();
    syncFrontendRow();
    loadStage();
    showPage(0);
}

void WorkbenchWindow::buildMenu()
{
    menuBar()->setNativeMenuBar(false);
    auto *file = menuBar()->addMenu("文件(&F)");
    file->addAction("新建产品", QKeySequence::New, this, &WorkbenchWindow::newProduct);
    file->addAction("打开产品…", QKeySequence::Open, this, &WorkbenchWindow::openProduct);
    file->addAction("保存产品", QKeySequence::Save, this, &WorkbenchWindow::saveProduct);
    file->addAction("另存为…", QKeySequence::SaveAs, this, &WorkbenchWindow::saveProductAs);
    file->addSeparator();
    file->addAction("退出", QKeySequence::Quit, this, &QWidget::close);
    auto *simulation = menuBar()->addMenu("仿真(&S)");
    m_supplyAction = simulation->addAction("外部供电");
    m_supplyAction->setCheckable(true);
    m_supplyAction->setChecked(m_platform->supplied());
    simulation->addAction("重新上电", QKeySequence("Ctrl+R"), m_platform, &AplPlatform::reset);
    simulation->addAction("查询滤芯寿命", m_platform, &AplPlatform::queryFilter);
    simulation->addSeparator();
    m_pauseAction = simulation->addAction("暂停仿真");
    m_pauseAction->setCheckable(true);
    simulation->addAction("单步 10 ms", this, [this]() { m_platform->setPaused(true); m_platform->advance(1); });
    m_viewMenu = menuBar()->addMenu("视图(&V)");
    rebuildViewMenu();
    auto *product = menuBar()->addMenu("产品(&P)");
    product->addAction("配置产品", this, &WorkbenchWindow::configureProduct);
    product->addAction("应用配置并预览", this, &WorkbenchWindow::applyProduct);
    product->addAction("导出 APL SDK…", this, &WorkbenchWindow::exportSdk);
    product->addSeparator();
    product->addAction("APL 源码目录…", this, &WorkbenchWindow::chooseSourceRoot);
    auto *rebuild = product->addAction("重新加载 APL", QKeySequence("Ctrl+B"), this, &WorkbenchWindow::rebuildApl);
    rebuild->setObjectName("rebuildAplAction");
    product->addAction("本机编译器…", this, &WorkbenchWindow::chooseCompiler);
    auto *help = menuBar()->addMenu("帮助(&H)");
    help->addAction("关于 APL Studio", this, &WorkbenchWindow::showAbout);
    for (int i = 0; i < 4; ++i)
    {
        auto *shortcut = new QAction(this);
        shortcut->setShortcut(QKeySequence(QString("Ctrl+%1").arg(i + 1)));
        addAction(shortcut);
        connect(shortcut, &QAction::triggered, this, [this, i]() { showPage(i); });
    }
}

void WorkbenchWindow::buildChrome()
{
    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    auto *sidebar = card("sidebar");
    sidebar->setFixedWidth(208);
    auto *side = column(sidebar, 16, 8);
    auto *brand = label("APL Studio", "section");
    brand->setStyleSheet("font-size: 22px; font-weight: 600; color: #242733; padding-top: 15px;");
    side->addWidget(brand);
    side->addWidget(label("产品配置与仿真", "caption"));
    side->addSpacing(25);
    side->addWidget(label("工作区", "caption"));
    m_navigation = new QButtonGroup(this);
    const QStringList titles = {"概览", "产品配置", "实时仿真", "SDK 交付"};
    const QStringList icons = {"overview", "config", "preview", "export"};
    for (int i = 0; i < titles.size(); ++i)
    {
        auto *item = button(titles[i], "nav");
        item->setObjectName(QString("navigation%1").arg(i));
        item->setIcon(StudioTheme::icon(icons[i]));
        item->setIconSize(QSize(20, 20));
        item->setCheckable(true);
        item->setToolTip(QString("%1 · Ctrl+%2").arg(titles[i]).arg(i + 1));
        m_navigation->addButton(item, i);
        side->addWidget(item);
    }
    side->addStretch();
    auto *identity = card();
    auto *identityLayout = column(identity, 14, 6);
    identityLayout->addWidget(label("当前产品", "caption"));
    m_productLabel = label({}, "section");
    identityLayout->addWidget(m_productLabel);
    m_productLocation = label({}, "caption");
    identityLayout->addWidget(m_productLocation);
    side->addWidget(identity);
    m_sourceLabel = label("内置 APL", "caption");
    m_sourceLabel->setObjectName("aplSourceLabel");
    side->addWidget(m_sourceLabel);
    auto *sources = button("APL 源码目录", "quiet");
    connect(sources, &QPushButton::clicked, this, &WorkbenchWindow::chooseSourceRoot);
    side->addWidget(sources);
    auto *compiler = button("编译器设置", "quiet");
    connect(compiler, &QPushButton::clicked, this, &WorkbenchWindow::chooseCompiler);
    side->addWidget(compiler);
    root->addWidget(sidebar);

    auto *workspace = new QWidget;
    auto *workspaceLayout = column(workspace, 0, 0);
    auto *header = card("header");
    auto *heading = new QHBoxLayout(header);
    heading->setContentsMargins(28, 18, 28, 18);
    heading->setSpacing(12);
    auto *titleGroup = new QVBoxLayout;
    titleGroup->setSpacing(4);
    m_headerTitle = label({}, "title");
    m_headerSubtitle = label({}, "subtitle");
    titleGroup->addWidget(m_headerTitle);
    titleGroup->addWidget(m_headerSubtitle);
    heading->addLayout(titleGroup, 1);
    m_previewBadge = label({}, "badge");
    m_previewBadge->setWordWrap(false);
    heading->addWidget(m_previewBadge, 0, Qt::AlignVCenter);
    m_saveButton = button("保存");
    m_saveButton->setObjectName("saveProductButton");
    heading->addWidget(m_saveButton);
    m_primary = button({}, "primary");
    m_primary->setObjectName("primaryAction");
    heading->addWidget(m_primary);
    workspaceLayout->addWidget(header);
    auto *noticeHost = new QWidget;
    auto *noticeLayout = new QHBoxLayout(noticeHost);
    noticeLayout->setContentsMargins(28, 8, 28, 0);
    m_notice = label({}, "subtitle");
    m_notice->setObjectName("operationNotice");
    m_notice->setAccessibleName("操作反馈");
    noticeLayout->addWidget(m_notice, 1);
    m_noticeDetails = button("查看详情", "quiet");
    m_noticeDetails->hide();
    noticeLayout->addWidget(m_noticeDetails);
    workspaceLayout->addWidget(noticeHost);
    m_pages = new QStackedWidget;
    m_pages->setObjectName("workspacePages");
    m_pages->addWidget(buildOverview());
    m_pages->addWidget(buildConfiguration());
    m_pages->addWidget(buildPreview());
    m_pages->addWidget(buildDelivery());
    workspaceLayout->addWidget(m_pages, 1);
    root->addWidget(workspace, 1);
    setCentralWidget(central);
}

QWidget *WorkbenchWindow::buildOverview()
{
    auto *body = new QWidget;
    auto *layout = column(body, 28, 20);
    auto *hero = card();
    auto *intro = column(hero, 30, 18);
    intro->addWidget(label("配置产品。验证效果。交付源码。", "heroTitle"));
    intro->addWidget(label("为净化与加湿产品配置 APL，在面板中验证行为，再交付可移植的源码 SDK。", "subtitle"));
    auto *quick = new QHBoxLayout;
    auto *create = button("新建产品");
    auto *open = button("打开已有产品…");
    open->setIcon(StudioTheme::icon("folder"));
    connect(create, &QPushButton::clicked, this, &WorkbenchWindow::newProduct);
    connect(open, &QPushButton::clicked, this, &WorkbenchWindow::openProduct);
    quick->addWidget(create); quick->addWidget(open); quick->addStretch();
    intro->addLayout(quick);
    layout->addWidget(hero);
    auto *steps = new QHBoxLayout;
    steps->setSpacing(16);
    const QStringList names = {"定义产品", "亲手验证", "交付工程"};
    const QStringList descriptions = {"选择机型、参数和按键，保存为可重复使用的产品配置。",
        "操作真实 APL 面板，调整环境输入，观察输出与事件。", "生成源码、配置、移植模板与接入说明，一起交给工程师。"};
    for (int i = 0; i < 3; ++i)
    {
        auto *step = card();
        auto *content = column(step, 22, 14);
        content->addWidget(label(QString("0%1").arg(i + 1), "badge"), 0, Qt::AlignLeft);
        content->addWidget(label(names[i], "section"));
        content->addWidget(label(descriptions[i], "subtitle"));
        content->addStretch();
        auto *go = button(QStringList{"配置产品", "查看仿真", "准备 SDK"}.at(i), "quiet");
        connect(go, &QPushButton::clicked, this, [this, i]() { showPage(i + 1); });
        content->addWidget(go, 0, Qt::AlignLeft);
        steps->addWidget(step, 1);
    }
    layout->addLayout(steps);
    auto *summary = card();
    auto *summaryLayout = column(summary);
    summaryLayout->addWidget(label("当前产品", "section"));
    m_overviewFacts = label({}, "subtitle");
    summaryLayout->addWidget(m_overviewFacts);
    layout->addWidget(summary);
    layout->addStretch();
    return scrolling(body);
}

QWidget *WorkbenchWindow::buildConfiguration()
{
    auto *page = new QWidget;
    auto *layout = column(page, 28, 8);
    m_editor = new ProductEditor(m_product);
    layout->addWidget(m_editor, 1);
    layout->addWidget(label("编辑不会改变正在运行的预览。点击右上角“应用并预览”后，使用新配置重新上电。", "caption"));
    return page;
}

void WorkbenchWindow::makeSensor(QVBoxLayout *layout, const QString &title, int low, int high,
                                 QSlider **slider, QSpinBox **reading, const QString &unit)
{
    auto *row = new QHBoxLayout;
    row->addWidget(label(title));
    row->addStretch();
    *reading = new QSpinBox;
    (*reading)->setRange(low, high);
    (*reading)->setSuffix(unit);
    (*reading)->setFixedWidth(108);
    (*reading)->setAccessibleName(title);
    row->addWidget(*reading);
    layout->addLayout(row);
    *slider = new QSlider(Qt::Horizontal);
    (*slider)->setRange(low, high);
    (*slider)->setAccessibleName(title);
    (*slider)->setMinimumHeight(18);
    layout->addWidget(*slider);
    layout->addSpacing(4);
}

QWidget *WorkbenchWindow::buildPreview()
{
    auto *page = new QWidget;
    auto *row = new QHBoxLayout(page);
    row->setContentsMargins(28, 20, 28, 24);
    row->setSpacing(16);
    auto *preview = card();
    auto *previewLayout = column(preview, 16, 10);
    auto *tools = new QHBoxLayout;
    tools->setSpacing(6);
    m_panels = new QButtonGroup(this);
    for (int i = 0; i < m_platform->frontends()->rowCount(); ++i)
    {
        auto *panel = button(m_platform->frontends()->nameAt(i), "segment");
        panel->setCheckable(true);
        m_panels->addButton(panel, i);
        tools->addWidget(panel);
    }
    tools->addStretch();
    auto *supply = new StudioSwitch;
    supply->setObjectName("supplySwitch");
    supply->setAccessibleName("外部供电");
    supply->setChecked(m_platform->supplied());
    supply->setToolTip("外部供电。关闭会模拟突然掉电。");
    tools->addWidget(label("供电", "caption"));
    tools->addWidget(supply);
    connect(supply, &QCheckBox::toggled, m_platform, &AplPlatform::setSupplied);
    connect(m_platform, &AplPlatform::suppliedChanged, supply, [this, supply]() {
        const QSignalBlocker blocker(supply); supply->setChecked(m_platform->supplied()); supply->update();
    });
    m_pauseButton = new QToolButton;
    m_pauseButton->setObjectName("pausePreview");
    m_pauseButton->setProperty("role", "icon");
    m_pauseButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_pauseButton->setFixedSize(36, 36);
    m_pauseButton->setAccessibleName("暂停或继续仿真");
    tools->addWidget(m_pauseButton);
    auto *more = new QToolButton;
    more->setText("•••");
    more->setAccessibleName("更多仿真操作");
    more->setPopupMode(QToolButton::InstantPopup);
    auto *moreMenu = new QMenu(more);
    moreMenu->addAction("单步 10 ms", this, [this]() { m_platform->setPaused(true); m_platform->advance(1); });
    moreMenu->addAction("重新上电", m_platform, &AplPlatform::reset);
    moreMenu->addAction("查询滤芯寿命", m_platform, &AplPlatform::queryFilter);
    more->setMenu(moreMenu);
    tools->addWidget(more);
    previewLayout->addLayout(tools);
    auto *stageHost = new QWidget;
    stageHost->setMinimumSize(220, 160);
    auto *stageLayout = new QGridLayout(stageHost);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    m_stage = new QQuickWidget;
    m_stage->setObjectName("stage");
    m_stage->setResizeMode(QQuickWidget::SizeRootObjectToView);
    m_stage->setClearColor(QColor("#ffffff"));
    m_stage->engine()->addImportPath(QString::fromUtf8(APL_QT_PREFIX) + "/qml");
    m_stage->engine()->rootContext()->setContextProperty("platform", m_platform);
    stageLayout->addWidget(m_stage, 0, 0);
    m_stageNote = label({}, "error");
    m_stageNote->setAlignment(Qt::AlignCenter);
    m_stageNote->hide();
    stageLayout->addWidget(m_stageNote, 0, 0, Qt::AlignCenter);
    previewLayout->addWidget(stageHost, 1);
    m_interactionHint = label({}, "caption");
    m_interactionHint->setObjectName("interactionHint");
    previewLayout->addWidget(m_interactionHint);
    auto *eventsHeader = new QHBoxLayout;
    m_eventsButton = new QToolButton;
    m_eventsButton->setObjectName("eventsToggle");
    m_eventsButton->setProperty("role", "quiet");
    m_eventsButton->setCheckable(true);
    m_eventsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_eventsButton->setArrowType(Qt::RightArrow);
    eventsHeader->addWidget(m_eventsButton);
    m_message = label({}, "caption");
    m_message->setWordWrap(false);
    m_message->setMinimumWidth(0);
    m_message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    eventsHeader->addWidget(m_message, 1);
    previewLayout->addLayout(eventsHeader);
    m_logDrawer = new QWidget;
    m_logDrawer->setObjectName("eventDrawer");
    auto *logLayout = column(m_logDrawer, 0, 8);
    auto *filters = new QHBoxLayout;
    m_logFilter = new QLineEdit;
    m_logFilter->setObjectName("eventFilter");
    m_logFilter->setPlaceholderText("筛选事件…");
    m_logFilter->setClearButtonEnabled(true);
    filters->addWidget(m_logFilter, 1);
    m_followEvents = new QToolButton;
    m_followEvents->setText("跟随");
    m_followEvents->setCheckable(true);
    m_followEvents->setChecked(true);
    m_followEvents->setToolTip("有新事件时滚动到底部");
    filters->addWidget(m_followEvents);
    auto *saveLog = button("保存…", "quiet");
    connect(saveLog, &QPushButton::clicked, this, &WorkbenchWindow::saveEventLog);
    filters->addWidget(saveLog);
    logLayout->addLayout(filters);
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setObjectName("eventLog");
    m_log->setFixedHeight(112);
    m_log->setFont(QFont("Consolas", 9));
    logLayout->addWidget(m_log);
    previewLayout->addWidget(m_logDrawer);
    m_logDrawer->hide();
    connect(m_eventsButton, &QToolButton::toggled, this, [this](bool open) {
        m_logDrawer->setVisible(open);
        m_eventsButton->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
    });
    row->addWidget(preview, 1);

    auto *inspector = card();
    inspector->setFixedWidth(274);
    auto *inspection = column(inspector, 16, 12);
    inspection->addWidget(label("设备检查器", "section"));
    auto *tabs = new QTabWidget;
    auto *environment = new QWidget;
    auto *sensors = column(environment, 0, 4);
    makeSensor(sensors, "水位", 0, 4, &m_waterSlider, &m_waterValue, " 段");
    makeSensor(sensors, "PM2.5", 0, 1000, &m_pm25Slider, &m_pm25Value);
    makeSensor(sensors, "湿度", 0, 100, &m_humiditySlider, &m_humidityValue, "%");
    makeSensor(sensors, "温度", -20, 60, &m_temperatureSlider, &m_temperatureValue, " °C");
    m_pm25Value->setObjectName("pm25Input");
    m_pm25Value->setSuffix(" µg/m³");
    m_pm25Value->setFixedWidth(126);
    m_waterValue->setObjectName("waterInput");
    m_humidityValue->setObjectName("humidityInput");
    m_temperatureValue->setObjectName("temperatureInput");
    auto *presets = new QHBoxLayout;
    auto *clean = button("清新", "quiet"), *polluted = button("污染", "quiet"), *empty = button("缺水", "quiet");
    connect(clean, &QPushButton::clicked, this, [this]() { m_platform->setPm25(12); });
    connect(polluted, &QPushButton::clicked, this, [this]() { m_platform->setPm25(260); });
    connect(empty, &QPushButton::clicked, this, [this]() { m_platform->setWater(0); });
    presets->addWidget(clean); presets->addWidget(polluted); presets->addWidget(empty);
    sensors->addLayout(presets);
    sensors->addStretch();
    tabs->addTab(scrolling(environment), "环境输入");
    auto *diagnostics = new QWidget;
    auto *diagnosticLayout = column(diagnostics, 0, 10);
    for (QLabel **entry : {&m_power, &m_gear, &m_timer, &m_digits, &m_childLock, &m_panelLight,
                          &m_powerup, &m_purify, &m_humidify})
    { *entry = label(); diagnosticLayout->addWidget(*entry); }
    diagnosticLayout->addWidget(label("调用统计", "section"));
    for (QLabel **entry : {&m_pmReads, &m_uvCalls, &m_anionCalls, &m_waterTriggers, &m_waterRecovers})
    { *entry = label({}, "caption"); diagnosticLayout->addWidget(*entry); }
    diagnosticLayout->addWidget(label("调试槽位", "section"));
    auto *slotSummary = label({}, "caption");
    auto refreshSlots = [this, slotSummary]() {
        QStringList rows;
        for (const auto &entry : m_platform->displaySlots())
        {
            const auto item = entry.toMap();
            if (!item.isEmpty()) rows << item["label"].toString() + "  " + item["value"].toString();
        }
        slotSummary->setText(rows.isEmpty() ? "暂无调试信息" : rows.join('\n'));
    };
    connect(m_platform, &AplPlatform::frameChanged, slotSummary, refreshSlots);
    refreshSlots();
    diagnosticLayout->addWidget(slotSummary);
    diagnosticLayout->addStretch();
    tabs->addTab(scrolling(diagnostics), "运行诊断");
    inspection->addWidget(tabs, 1);
    inspection->addWidget(label("实际负载输出", "section"));
    for (QLabel **entry : {&m_fan, &m_humidifier, &m_uv, &m_anion})
    { *entry = label({}, "subtitle"); inspection->addWidget(*entry); }
    row->addWidget(inspector);
    return page;
}

QWidget *WorkbenchWindow::buildDelivery()
{
    auto *body = new QWidget;
    auto *layout = column(body, 28, 20);
    auto *summary = card();
    auto *content = column(summary, 28, 16);
    auto *symbol = new QLabel;
    symbol->setPixmap(StudioTheme::icon("export", QColor("#007aff")).pixmap(56, 56));
    content->addWidget(symbol);
    content->addWidget(label("APL SDK，准备交付。", "heroTitle"));
    content->addWidget(label("源码、产品配置和移植说明，打包在同一份 SDK 中。工程师可以使用自己的嵌入式工具链构建。", "subtitle"));
    m_deliveryStatus = label({}, "badge");
    content->addWidget(m_deliveryStatus, 0, Qt::AlignLeft);
    layout->addWidget(summary);
    auto *columns = new QHBoxLayout;
    columns->setSpacing(16);
    auto *package = card();
    auto *packageLayout = column(package);
    packageLayout->addWidget(label("软件包内容", "section"));
    auto *tree = new QTreeWidget;
    tree->setHeaderHidden(true);
    tree->setRootIsDecorated(true);
    tree->setFixedHeight(300);
    tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *root = new QTreeWidgetItem(tree, {"apl-sdk"});
    for (const auto &name : QStringList{"src /  APL C 源码", "include /  公共接口", "config /  产品配置", "port /  硬件移植模板", "examples /  调用示例", "README.md  接入说明", "manifest.json  版本与文件校验", "product.apl.json  可重新打开的配置"})
        new QTreeWidgetItem(root, {name});
    root->setExpanded(true);
    packageLayout->addWidget(tree, 1);
    columns->addWidget(package, 1);
    auto *handoff = card();
    auto *handoffLayout = column(handoff);
    handoffLayout->addWidget(label("交给工程师之后", "section"));
    handoffLayout->addWidget(label("1. 加入源码与包含路径\n\n2. 实现按键、传感器和负载接口\n\n3. 每 10 ms 推进 APL\n\n4. 刷新实际面板，验证硬件行为", "subtitle"));
    handoffLayout->addStretch();
    handoffLayout->addWidget(label("C11 源码 · 无 Qt 依赖\n包含存储移植入口；硬件接口需按项目实现。", "caption"));
    columns->addWidget(handoff, 1);
    layout->addLayout(columns);
    m_deliveryDetails = label({}, "caption");
    m_deliveryDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_deliveryDetails);
    m_exportLocation = label({}, "subtitle");
    m_exportLocation->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_exportLocation);
    m_revealExport = button("在文件夹中显示", "quiet");
    m_revealExport->hide();
    connect(m_revealExport, &QPushButton::clicked, this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_lastExportPath).absolutePath()));
    });
    layout->addWidget(m_revealExport, 0, Qt::AlignLeft);
    layout->addStretch();
    return scrolling(body);
}

void WorkbenchWindow::showPage(int page)
{
    if (page < 0 || page >= m_pages->count()) return;
    m_pages->setCurrentIndex(page);
    m_navigation->button(page)->setChecked(true);
    const QStringList titles = {"产品概览", "产品配置", "实时仿真", "SDK 交付"};
    const QStringList subtitles = {"从产品定义，到嵌入式交付", "配置一次，预览与交付使用同一套参数", "操作面板，观察真实 APL 的运行结果", "交付产品源码、配置与移植说明"};
    m_headerTitle->setText(titles[page]);
    m_headerSubtitle->setText(subtitles[page]);
    m_primary->setText(QStringList{"配置产品", "应用并预览", "准备交付 SDK", "导出 SDK…"}.at(page));
    m_primary->setEnabled((page == 0 || page == 2 || m_product.validate().isEmpty()) && !buildingApl());
}

void WorkbenchWindow::primaryAction()
{
    switch (m_pages->currentIndex())
    {
    case 0: configureProduct(); break;
    case 1: applyProduct(); break;
    case 2: showPage(3); break;
    case 3: exportSdk(); break;
    }
}

void WorkbenchWindow::rebuildViewMenu()
{
    for (auto *group : m_viewMenu->findChildren<QActionGroup *>(QString(), Qt::FindDirectChildrenOnly)) delete group;
    m_viewMenu->clear();
    auto *group = new QActionGroup(m_viewMenu);
    group->setExclusive(true);
    for (int row = 0; row < m_platform->frontends()->rowCount(); ++row)
    {
        auto *action = m_viewMenu->addAction(m_platform->frontends()->nameAt(row));
        action->setCheckable(true);
        group->addAction(action);
        action->setChecked(row == m_platform->frontendIndex());
        connect(action, &QAction::triggered, this, [this, row]() { showPage(2); m_platform->selectFrontend(row); });
    }
}

void WorkbenchWindow::syncViewChecks()
{
    for (int row = 0; row < m_viewMenu->actions().size(); ++row)
    {
        const QSignalBlocker blocker(m_viewMenu->actions().at(row));
        m_viewMenu->actions().at(row)->setChecked(row == m_platform->frontendIndex());
    }
}

void WorkbenchWindow::refreshTitle()
{
    const QString title = m_product.name + "[*] — APL Studio";
    if (windowTitle() != title) setWindowTitle(title);
    setWindowModified(m_productDirty);
}

void WorkbenchWindow::showAbout()
{
    QMessageBox::about(this, "APL Studio", "为净化与加湿产品配置、验证和交付 APL。\n\n内置真实 APL C 库、面板仿真和源码 SDK 生成工具。");
}

void WorkbenchWindow::bind()
{
    connect(m_navigation, &QButtonGroup::idClicked, this, &WorkbenchWindow::showPage);
    connect(m_panels, &QButtonGroup::idClicked, m_platform, &AplPlatform::selectFrontend);
    connect(m_primary, &QPushButton::clicked, this, &WorkbenchWindow::primaryAction);
    connect(m_saveButton, &QPushButton::clicked, this, &WorkbenchWindow::saveProduct);
    connect(m_noticeDetails, &QPushButton::clicked, this, [this]() {
        QMessageBox box(QMessageBox::Information, "操作详情", m_notice->text(), QMessageBox::Ok, this);
        box.setDetailedText(m_details);
        box.exec();
    });
    connect(m_editor, &ProductEditor::edited, this, [this]() {
        if (m_syncingEditor) return;
        m_product = m_editor->config();
        m_productDirty = m_product.toJson() != m_savedProduct;
        refreshProduct();
    });
    connect(m_supplyAction, &QAction::toggled, m_platform, &AplPlatform::setSupplied);
    connect(m_pauseAction, &QAction::toggled, m_platform, &AplPlatform::setPaused);
    connect(m_pauseButton, &QToolButton::clicked, this, [this]() { m_platform->setPaused(!m_platform->paused()); });
    connect(m_platform, &AplPlatform::suppliedChanged, this, [this]() {
        const QSignalBlocker blocker(m_supplyAction); m_supplyAction->setChecked(m_platform->supplied());
    });
    connect(m_platform, &AplPlatform::frontendChanged, this, [this]() { syncFrontendRow(); syncViewChecks(); loadStage(); });
    connect(m_platform, &AplPlatform::frameChanged, this, &WorkbenchWindow::refreshFrame);
    connect(m_platform, &AplPlatform::eventsChanged, this, &WorkbenchWindow::refreshLog);
    connect(m_logFilter, &QLineEdit::textChanged, this, &WorkbenchWindow::refreshLog);
    connect(m_platform, &AplPlatform::waterChanged, this, &WorkbenchWindow::refreshSensors);
    connect(m_platform, &AplPlatform::pm25Changed, this, &WorkbenchWindow::refreshSensors);
    connect(m_platform, &AplPlatform::humidityChanged, this, &WorkbenchWindow::refreshSensors);
    connect(m_platform, &AplPlatform::temperatureChanged, this, &WorkbenchWindow::refreshSensors);
    connect(m_waterSlider, &QSlider::valueChanged, m_platform, &AplPlatform::setWater);
    connect(m_pm25Slider, &QSlider::valueChanged, m_platform, &AplPlatform::setPm25);
    connect(m_humiditySlider, &QSlider::valueChanged, m_platform, &AplPlatform::setHumidity);
    connect(m_temperatureSlider, &QSlider::valueChanged, m_platform, &AplPlatform::setTemperature);
    connect(m_waterValue, &QSpinBox::valueChanged, m_platform, &AplPlatform::setWater);
    connect(m_pm25Value, &QSpinBox::valueChanged, m_platform, &AplPlatform::setPm25);
    connect(m_humidityValue, &QSpinBox::valueChanged, m_platform, &AplPlatform::setHumidity);
    connect(m_temperatureValue, &QSpinBox::valueChanged, m_platform, &AplPlatform::setTemperature);
    connect(m_stage, &QQuickWidget::statusChanged, this, [this]() { onStageStatus(); });
}

void WorkbenchWindow::refreshFrame()
{
    refreshTitle();
    m_message->setText(m_platform->message());
    m_message->setToolTip(m_platform->message());
    m_power->setText("运行状态  " + m_platform->runStateText());
    m_power->setObjectName("runStateLabel");
    m_interactionHint->setText(m_platform->interactionHint());
    m_gear->setText("用户档位  " + m_platform->gearName());
    m_timer->setText(m_platform->runState() == APL_RUN_DRY
        ? QString("风干剩余  %1 秒").arg(m_platform->drySeconds())
        : QString("定时剩余  %1 秒").arg(m_platform->timerSeconds()));
    m_digits->setText("数码管  [" + m_platform->digits() + "]");
    m_childLock->setText(m_platform->childLock() ? "童锁  已开启" : "童锁  已关闭");
    m_panelLight->setText(m_platform->panelLight() ? "灯光  已开启" : "灯光  已关闭");
    m_powerup->setText(m_platform->powerupShow() ? "上电全灯自检中" : "全灯自检已结束");
    m_purify->setText(!m_platform->purifyEnabled() ? "净化功能  未启用" : m_platform->purifyOn() ? "净化开关  开" : "净化开关  关");
    m_humidify->setText(!m_platform->humidifyEnabled() ? "加湿功能  未启用" : m_platform->humidifyOn() ? "加湿开关  开" : "加湿开关  关");
    m_fan->setText("风机  " + QString(m_platform->fanText()).remove("（输出档位）"));
    m_fan->setToolTip(m_platform->fanText());
    m_humidifier->setText("加湿器  " + m_platform->humidifierText());
    m_uv->setText("UV  " + m_platform->uvText());
    m_anion->setText("负离子  " + m_platform->anionText());
    m_pmReads->setText(QString("PM 读取  %1").arg(m_platform->pmReads()));
    m_uvCalls->setText(QString("UV 调用  %1").arg(m_platform->uvCalls()));
    m_anionCalls->setText(QString("负离子调用  %1").arg(m_platform->anionCalls()));
    m_waterTriggers->setText(QString("缺水触发  %1").arg(m_platform->waterTriggers()));
    m_waterRecovers->setText(QString("水位恢复  %1").arg(m_platform->waterRecovers()));
    const QSignalBlocker blocker(m_pauseAction);
    m_pauseAction->setChecked(m_platform->paused());
    m_pauseButton->setIcon(StudioTheme::icon(m_platform->paused() ? "play" : "pause"));
    m_pauseButton->setToolTip(m_platform->paused() ? "继续仿真" : "暂停仿真");
}

void WorkbenchWindow::refreshSensors()
{
    writeSensor(m_waterSlider, m_waterValue, m_platform->water());
    writeSensor(m_pm25Slider, m_pm25Value, m_platform->pm25());
    writeSensor(m_humiditySlider, m_humidityValue, m_platform->humidity());
    writeSensor(m_temperatureSlider, m_temperatureValue, m_platform->temperature());
}

void WorkbenchWindow::refreshLog()
{
    const QStringList events = m_platform->eventLog().split('\n', Qt::SkipEmptyParts);
    m_eventsButton->setText(QString("事件记录 · %1").arg(events.size()));
    QStringList filtered;
    const QString query = m_logFilter->text().trimmed();
    for (const auto &event : events) if (query.isEmpty() || event.contains(query, Qt::CaseInsensitive)) filtered << event;
    const QString text = filtered.join('\n');
    if (m_log->toPlainText() == text) return;
    const bool follow = m_followEvents->isChecked();
    const int previous = m_log->verticalScrollBar()->value();
    m_log->setPlainText(text);
    m_log->verticalScrollBar()->setValue(follow ? m_log->verticalScrollBar()->maximum() : previous);
}

void WorkbenchWindow::syncFrontendRow()
{
    if (auto *item = m_panels->button(m_platform->frontendIndex())) item->setChecked(true);
}

void WorkbenchWindow::loadStage()
{
    const QUrl url = m_platform->frontendUrl();
    if (url.isEmpty()) { m_stageNote->setText("面板资源暂不可用"); m_stageNote->show(); return; }
    if (m_stage->source() != url) m_stage->setSource(url);
    onStageStatus();
}

void WorkbenchWindow::onStageStatus()
{
    if (m_stage->status() != QQuickWidget::Error) { m_stageNote->hide(); return; }
    QStringList errors;
    for (const auto &error : m_stage->errors()) errors << error.toString();
    m_stageNote->setText("面板加载失败，请检查内置资源。\n" + errors.join('\n'));
    m_stageNote->show();
}

void WorkbenchWindow::showNotice(const QString &message, const QString &details, bool error)
{
    m_notice->setText(message);
    m_notice->setStyleSheet(error ? "color: #ae3838;" : "color: #52705c;");
    m_details = details;
    m_noticeDetails->setVisible(!details.isEmpty());
}

void WorkbenchWindow::saveEventLog()
{
    const QString path = QFileDialog::getSaveFileName(this, "保存当前筛选的事件", "apl-events.txt", "文本文件 (*.txt)");
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto text = m_log->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(text) != text.size() || !file.commit())
        showNotice("事件记录保存失败。", file.errorString(), true);
    else showNotice("事件记录已保存。");
}
