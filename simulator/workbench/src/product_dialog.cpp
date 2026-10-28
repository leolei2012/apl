#include "product_dialog.h"
#include "studio_theme.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
bool isGearField(const QString &macro)
{
    return macro == "APL_DEFAULT_FAN_GEAR" || macro == "APL_CORE_DRY_FAN_GEAR";
}
QLabel *textLabel(const QString &text, const char *role)
{
    auto *label = new QLabel(text);
    label->setProperty("role", role);
    label->setWordWrap(true);
    label->setTextFormat(Qt::PlainText);
    return label;
}
QFormLayout *settingsCard(QVBoxLayout *layout, const QString &title, const QString &description)
{
    auto *card = new QFrame;
    card->setProperty("role", "card");
    auto *body = new QVBoxLayout(card);
    body->setContentsMargins(22, 20, 22, 20);
    body->setSpacing(12);
    body->addWidget(textLabel(title, "section"));
    if (!description.isEmpty()) body->addWidget(textLabel(description, "subtitle"));
    auto *form = new QFormLayout;
    form->setVerticalSpacing(14);
    form->setHorizontalSpacing(24);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    body->addLayout(form);
    layout->addWidget(card);
    return form;
}
}

ProductEditor::ProductEditor(const ProductConfig &config, QWidget *parent) : QWidget(parent)
{
    setObjectName("productEditor");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);
    auto *tabs = new QTabWidget;
    tabs->setObjectName("configurationTabs");
    QVBoxLayout *pages[3];
    for (int i = 0; i < 3; ++i)
    {
        auto *body = new QWidget;
        pages[i] = new QVBoxLayout(body);
        pages[i]->setContentsMargins(0, 18, 4, 12);
        pages[i]->setSpacing(16);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(body);
        tabs->addTab(scroll, QStringList{"产品功能", "运行参数", "面板按键"}.at(i));
    }
    auto *identity = settingsCard(pages[0], "产品信息", "为这份配置命名，之后可以保存、重新打开和交付。");
    m_name = new QLineEdit;
    m_name->setObjectName("productNameInput");
    m_name->setMaxLength(80);
    m_name->setPlaceholderText("例如：客厅净化加湿一体机");
    identity->addRow("产品名称", m_name);
    m_frontend = new QComboBox;
    m_frontend->addItems({"圆形面板", "按键台"});
    identity->addRow("默认预览面板", m_frontend);
    auto *features = settingsCard(pages[0], "选择产品类型", "选择需要编入 APL 的功能。切换类型会同步调整默认功能和对应按键。");
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(6);
    m_modes = new QButtonGroup(this);
    m_modes->setExclusive(true);
    const QStringList modes = {"净化 + 加湿", "仅净化", "仅加湿"};
    for (int i = 0; i < modes.size(); ++i)
    {
        auto *button = new QPushButton(modes[i]);
        button->setCheckable(true);
        button->setProperty("role", "segment");
        button->setObjectName(QString("productMode%1").arg(i));
        m_modes->addButton(button, i);
        modeRow->addWidget(button, 1);
    }
    features->addRow(modeRow);
    auto *basic = settingsCard(pages[1], "日常运行", "预览和导出的 SDK 将使用相同的运行参数。");
    auto *advancedToggle = new QToolButton;
    advancedToggle->setText("更多运行参数");
    advancedToggle->setProperty("role", "quiet");
    advancedToggle->setCheckable(true);
    advancedToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advancedToggle->setArrowType(Qt::RightArrow);
    pages[1]->addWidget(advancedToggle, 0, Qt::AlignLeft);
    auto *advanced = new QWidget;
    auto *advancedLayout = new QVBoxLayout(advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    auto *advancedForm = settingsCard(advancedLayout, "更多运行参数", "缺水提示、产测和配网参数。保持默认即可开始预览。");
    pages[1]->addWidget(advanced);
    advanced->hide();
    connect(advancedToggle, &QToolButton::toggled, advanced, [advanced, advancedToggle](bool expanded) {
        advanced->setVisible(expanded);
        advancedToggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
    });
    auto *keys = settingsCard(pages[2], "可用按键", "关闭的按键不响应手势，也不参与组合键。按键编号和底层采样接口保持一致。");
    const QSet<QString> common = {"APL_DEFAULT_FAN_GEAR", "APL_CORE_DRY_S", "APL_CORE_FILTER_LIFE_H",
        "APL_CORE_HUMIDITY_HYSTERESIS", "APL_CORE_HAS_TURBO"};
    for (const auto &field : ProductConfig::fields())
    {
        if (field.macro.startsWith("APL_COMPILE_")) continue;
        QWidget *editor;
        if (field.maximum == 1)
        {
            auto *toggle = new StudioSwitch;
            toggle->setAccessibleName(field.label);
            connect(toggle, &QCheckBox::toggled, this, &ProductEditor::notifyEdited);
            editor = toggle;
        }
        else if (isGearField(field.macro))
        {
            auto *gear = new QComboBox;
            gear->addItems({"睡眠", "一档", "二档", "三档", "自动", "极速"});
            connect(gear, &QComboBox::currentIndexChanged, this, &ProductEditor::notifyEdited);
            editor = gear;
        }
        else
        {
            auto *spin = new QSpinBox;
            spin->setRange(field.minimum, field.maximum);
            spin->setToolTip(QString("范围 %1–%2；可输入数值或使用上下方向键微调。").arg(field.minimum).arg(field.maximum));
            spin->setMinimumWidth(126);
            connect(spin, &QSpinBox::valueChanged, this, &ProductEditor::notifyEdited);
            editor = spin;
        }
        editor->setObjectName(field.macro);
        m_fields[field.macro] = editor;
        auto *form = field.hmi ? keys : field.macro.startsWith("APL_DEFAULT_")
            && field.maximum == 1 ? features : common.contains(field.macro) ? basic : advancedForm;
        const QString label = field.macro == "APL_DEFAULT_FAN_GEAR" ? "默认风速"
            : field.macro == "APL_CORE_DRY_FAN_GEAR" ? "风干风速" : field.label;
        form->addRow(label, editor);
    }
    for (auto *page : pages) page->addStretch();
    m_validation = textLabel({}, "error");
    m_validation->setObjectName("configurationError");
    outer->addWidget(tabs, 1);
    outer->addWidget(m_validation);
    connect(m_name, &QLineEdit::textEdited, this, &ProductEditor::notifyEdited);
    connect(m_frontend, &QComboBox::currentIndexChanged, this, &ProductEditor::notifyEdited);
    connect(m_modes, &QButtonGroup::idClicked, this, &ProductEditor::selectMode);
    setConfig(config);
}

void ProductEditor::setConfig(const ProductConfig &config)
{
    m_loading = true;
    findChild<QTabWidget *>("configurationTabs")->setCurrentIndex(0);
    m_base = config;
    m_name->setText(config.name);
    m_frontend->setCurrentIndex(config.frontend == "bench" ? 1 : 0);
    const int mode = !config.values.value("APL_COMPILE_PURIFY") ? 2 : !config.values.value("APL_COMPILE_HUMIDIFY") ? 1 : 0;
    m_modes->button(mode)->setChecked(true);
    for (const auto &field : ProductConfig::fields())
    {
        auto *editor = m_fields.value(field.macro);
        if (!editor) continue;
        if (field.maximum == 1) static_cast<QCheckBox *>(editor)->setChecked(config.values.value(field.macro));
        else if (isGearField(field.macro)) static_cast<QComboBox *>(editor)->setCurrentIndex(config.values.value(field.macro));
        else static_cast<QSpinBox *>(editor)->setValue(config.values.value(field.macro));
    }
    updateAvailability();
    m_loading = false;
    const QString why = config.validate();
    m_validation->setText(why);
    m_validation->setVisible(!why.isEmpty());
}

ProductConfig ProductEditor::config() const
{
    auto next = m_base;
    next.name = m_name->text().trimmed();
    next.frontend = m_frontend->currentIndex() == 0 ? "round" : "bench";
    for (const auto &field : ProductConfig::fields())
    {
        auto *editor = m_fields.value(field.macro);
        if (!editor) continue;
        next.values[field.macro] = field.maximum == 1 ? static_cast<QCheckBox *>(editor)->isChecked()
            : isGearField(field.macro) ? static_cast<QComboBox *>(editor)->currentIndex()
            : static_cast<QSpinBox *>(editor)->value();
    }
    return next;
}

void ProductEditor::selectMode(int mode)
{
    m_loading = true;
    m_base.values["APL_COMPILE_PURIFY"] = mode != 2;
    m_base.values["APL_COMPILE_HUMIDIFY"] = mode != 1;
    const QMap<QString, bool> preset = {{"APL_DEFAULT_PURIFY", mode != 2}, {"APL_DEFAULT_HUMIDIFY", mode != 1},
        {"APL_HMI_COMPILE_KEY_PURIFY", mode != 2}, {"APL_HMI_COMPILE_KEY_HUMIDITY", mode != 1}};
    for (auto it = preset.begin(); it != preset.end(); ++it)
        static_cast<QCheckBox *>(m_fields[it.key()])->setChecked(it.value());
    updateAvailability();
    m_loading = false;
    notifyEdited();
}

void ProductEditor::updateAvailability()
{
    m_fields["APL_DEFAULT_PURIFY"]->setEnabled(m_base.values.value("APL_COMPILE_PURIFY"));
    m_fields["APL_DEFAULT_HUMIDIFY"]->setEnabled(m_base.values.value("APL_COMPILE_HUMIDIFY"));
}

void ProductEditor::notifyEdited()
{
    if (m_loading) return;
    const QString why = config().validate();
    m_validation->setText(why);
    m_validation->setVisible(!why.isEmpty());
    emit edited();
}

ProductDialog::ProductDialog(const ProductConfig &config, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("配置 APL 产品");
    resize(780, 740);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    m_editor = new ProductEditor(config, this);
    layout->addWidget(m_editor);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("应用并预览");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    buttons->button(QDialogButtonBox::Ok)->setProperty("role", "primary");
    connect(buttons, &QDialogButtonBox::accepted, this, &ProductDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ProductDialog::reject);
    layout->addWidget(buttons);
}
ProductConfig ProductDialog::config() const { return m_editor->config(); }
void ProductDialog::accept() { if (config().validate().isEmpty()) QDialog::accept(); }
