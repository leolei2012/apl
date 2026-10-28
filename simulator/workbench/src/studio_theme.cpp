#include "studio_theme.h"
#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>

void StudioTheme::apply(QApplication &app)
{
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f5f5f7"));
    palette.setColor(QPalette::WindowText, QColor("#202126"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f8f8fa"));
    palette.setColor(QPalette::Text, QColor("#202126"));
    palette.setColor(QPalette::Button, Qt::white);
    palette.setColor(QPalette::ButtonText, QColor("#202126"));
    palette.setColor(QPalette::Highlight, QColor("#007aff"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor("#92949c"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#a6a8b0"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#a6a8b0"));
    app.setPalette(palette);
    QString family = "Segoe UI";
    const auto available = QFontDatabase::families();
    for (const auto &candidate : QStringList{"Microsoft YaHei UI", "PingFang SC", "Noto Sans SC", "Segoe UI"})
        if (available.contains(candidate)) { family = candidate; break; }
    QFont font(family);
    font.setPixelSize(14);
    app.setFont(font);
    app.setStyleSheet(QStringLiteral("QWidget { font-family: \"%1\"; }\n").arg(family) + QStringLiteral(R"(
        QMainWindow, QDialog { background: #f5f5f7; }
        QLabel { color: #202126; background: transparent; }
        QLabel[role="title"] { font-size: 25px; font-weight: 600; }
        QLabel[role="heroTitle"] { font-size: 32px; font-weight: 600; }
        QLabel[role="section"] { font-size: 16px; font-weight: 600; }
        QLabel[role="caption"] { color: #747782; font-size: 12px; }
        QLabel[role="subtitle"] { color: #747782; font-size: 13px; }
        QLabel[role="badge"] { background: #eaf3ff; color: #0067d8; padding: 6px 11px; border-radius: 10px; font-size: 12px; }
        QLabel[role="badge"][tone="pending"] { background: #fff2dc; color: #94620a; }
        QLabel[role="badge"][tone="ready"] { background: #e5f6ec; color: #217848; }
        QLabel[role="error"] { color: #b43232; background: #fff0f0; border-radius: 9px; padding: 10px; }
        QFrame[role="card"] { background: #ffffff; border: 1px solid #e4e5e9; border-radius: 16px; }
        QFrame[role="sidebar"] { background: #eeeff3; border-right: 1px solid #dedfe5; }
        QFrame[role="header"] { background: #fafafc; border-bottom: 1px solid #e5e6eb; }
        QFrame[role="notice"] { background: #eaf3ff; border-radius: 12px; }
        QPushButton, QToolButton { background: #ffffff; color: #353741; border: 1px solid #dfe1e7;
            border-radius: 9px; padding: 8px 14px; min-height: 18px; }
        QPushButton:hover, QToolButton:hover { background: #f1f2f6; border-color: #c6c9d1; }
        QPushButton:pressed, QToolButton:pressed { background: #e6e8ee; }
        QPushButton:focus, QToolButton:focus { border-color: #007aff; }
        QPushButton:disabled, QToolButton:disabled { color: #a2a5ae; background: #f4f4f7; border-color: #e8e9ed; }
        QPushButton[role="primary"] { background: #007aff; border-color: #007aff; color: white; font-weight: 600; }
        QPushButton[role="primary"]:hover { background: #006ae0; border-color: #006ae0; }
        QPushButton[role="primary"]:pressed { background: #005fc9; }
        QPushButton[role="primary"]:disabled { background: #b5d6fa; border-color: #b5d6fa; color: #f4f9ff; }
        QPushButton[role="quiet"], QToolButton[role="quiet"] { background: transparent; border-color: transparent; color: #657080; }
        QPushButton[role="quiet"]:hover, QToolButton[role="quiet"]:hover { background: #e6e8ee; color: #202126; }
        QPushButton[role="nav"] { text-align: left; background: transparent; border: 1px solid transparent;
            padding: 12px 14px; border-radius: 10px; color: #656976; }
        QPushButton[role="nav"]:hover { background: #e4e6ec; }
        QPushButton[role="nav"]:checked { background: #dceaff; color: #0068db; font-weight: 600; }
        QPushButton[role="segment"] { background: #e9eaf0; border-color: transparent; padding: 7px 14px; border-radius: 7px; }
        QPushButton[role="segment"]:checked { background: white; border-color: #dddfe5; color: #202126; font-weight: 600; }
        QLineEdit, QSpinBox, QComboBox { background: #f8f8fa; border: 1px solid #dfe1e6; border-radius: 8px;
            padding: 7px 10px; min-height: 20px; color: #252832; selection-background-color: #007aff; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { background: white; border-color: #007aff; }
        QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: #a0a3ad; background: #f2f3f6; }
        QSpinBox QLineEdit { background: transparent; border: none; padding: 0; min-height: 0; }
        QComboBox::drop-down { border: none; width: 28px; }
        QComboBox::down-arrow { image: url(:/studio/chevron-down.svg); width: 12px; height: 12px; }
        QToolButton::menu-indicator { image: none; }
        QToolButton[role="icon"] { padding: 0; min-width: 30px; min-height: 30px; }
        QSpinBox::up-arrow { image: url(:/studio/chevron-up.svg); width: 10px; height: 10px; }
        QSpinBox::down-arrow { image: url(:/studio/chevron-down.svg); width: 10px; height: 10px; }
        QComboBox QAbstractItemView { background: white; color: #252832; selection-background-color: #e2efff; selection-color: #0068db; }
        QSpinBox::up-button, QSpinBox::down-button { width: 18px; border: none; background: #edeef3; }
        QSlider::groove:horizontal { height: 4px; background: #e6e8ed; border-radius: 2px; }
        QSlider::sub-page:horizontal { background: #007aff; border-radius: 2px; }
        QSlider::handle:horizontal { width: 16px; margin: -7px 0; background: white; border: 1px solid #c8ccd4; border-radius: 8px; }
        QSlider::handle:horizontal:hover { border-color: #007aff; }
        QScrollArea, QStackedWidget { background: transparent; border: none; }
        QScrollBar:vertical { background: transparent; width: 8px; margin: 3px 0; }
        QScrollBar::handle:vertical { background: #cdd0d8; min-height: 28px; border-radius: 4px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
        QTabWidget::pane { border: none; background: transparent; }
        QTabBar::tab { color: #777b86; background: transparent; padding: 10px 16px; border-bottom: 2px solid transparent; }
        QTabBar::tab:selected { color: #007aff; border-bottom-color: #007aff; }
        QTabBar::tab:hover { background: #f0f3f8; }
        QPlainTextEdit { font-family: "Consolas"; font-size: 12px; }
        QPlainTextEdit, QTreeWidget { background: transparent; border: none; color: #4d5564; selection-background-color: #dceaff; selection-color: #164b85; }
        QTreeWidget::item { padding: 7px 4px; border-radius: 5px; }
        QMenuBar { background: #f5f5f7; color: #626672; font-size: 12px; }
        QMenuBar::item { padding: 5px 10px; background: transparent; }
        QMenuBar::item:selected { background: #e4e8ef; border-radius: 4px; }
        QMenu { background: white; color: #333742; border: 1px solid #dfe1e6; padding: 6px; }
        QMenu::item { padding: 7px 25px 7px 13px; border-radius: 5px; }
        QMenu::item:selected { background: #e6f0ff; color: #0069df; }
        QMenu::separator { height: 1px; background: #ebecf0; margin: 5px; }
        QToolTip { color: #30333c; background: white; border: 1px solid #dfe1e6; padding: 6px; }
        QSplitter::handle { background: transparent; width: 8px; height: 8px; }
    )"));
}

QIcon StudioTheme::icon(const QString &name, const QColor &color)
{
    QPixmap pixmap(40, 40);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "overview")
    {
        for (int x : {3, 11}) for (int y : {3, 11}) p.drawRoundedRect(QRectF(x, y, 6, 6), 1.5, 1.5);
    }
    else if (name == "config")
    {
        for (int y : {5, 10, 15}) p.drawLine(3, y, 17, y);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(7, 5), 2, 2); p.drawEllipse(QPointF(13, 10), 2, 2); p.drawEllipse(QPointF(8, 15), 2, 2);
    }
    else if (name == "preview" || name == "play")
    {
        if (name == "preview") p.drawRoundedRect(QRectF(2, 2, 16, 16), 4, 4);
        QPainterPath path; path.moveTo(8, 6); path.lineTo(14, 10); path.lineTo(8, 14); path.closeSubpath(); p.drawPath(path);
    }
    else if (name == "pause") { p.drawLine(7, 5, 7, 15); p.drawLine(13, 5, 13, 15); }
    else if (name == "export")
    {
        QPainterPath path; path.moveTo(3, 6); path.lineTo(10, 2); path.lineTo(17, 6); path.lineTo(17, 14);
        path.lineTo(10, 18); path.lineTo(3, 14); path.closeSubpath(); p.drawPath(path);
        p.drawLine(3, 6, 10, 10); p.drawLine(10, 10, 17, 6); p.drawLine(10, 10, 10, 18);
    }
    else if (name == "folder") { p.drawRoundedRect(QRectF(2, 6, 16, 11), 2, 2); p.drawLine(3, 6, 3, 3); p.drawLine(3, 3, 8, 3); p.drawLine(8, 3, 11, 6); }
    else if (name == "check") { p.drawLine(4, 10, 8, 14); p.drawLine(8, 14, 16, 6); }
    else if (name == "arrow") { p.drawLine(4, 10, 16, 10); p.drawLine(12, 6, 16, 10); p.drawLine(12, 14, 16, 10); }
    else { p.drawEllipse(QRectF(3, 3, 14, 14)); p.drawLine(10, 6, 10, 10); p.drawLine(10, 10, 13, 12); }
    return QIcon(pixmap);
}

StudioSwitch::StudioSwitch(QWidget *parent) : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(sizeHint());
    m_animation.setDuration(120);
    connect(this, &QCheckBox::toggled, this, [this](bool checked) {
        m_animation.stop();
        m_animation.setStartValue(m_position);
        m_animation.setEndValue(checked ? 1.0 : 0.0);
        m_animation.start();
    });
    connect(&m_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) { m_position = value.toReal(); update(); });
}

void StudioSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal position = m_animation.state() == QAbstractAnimation::Running ? m_position : isChecked() ? 1.0 : 0.0;
    if (hasFocus()) { p.setPen(QPen(QColor("#7cb9ff"), 1)); p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(0.5, 0.5, 39, 23), 12, 12); }
    p.setPen(Qt::NoPen);
    p.setBrush(isEnabled() ? (isChecked() ? QColor("#007aff") : QColor("#cdd0d8")) : QColor("#e2e4e9"));
    p.drawRoundedRect(QRectF(3, 3, 34, 18), 9, 9);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(5 + position * 16, 5, 14, 14));
}
