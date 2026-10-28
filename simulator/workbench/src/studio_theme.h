#pragma once
#include <QCheckBox>
#include <QColor>
#include <QIcon>
#include <QVariantAnimation>

class QApplication;

namespace StudioTheme
{
void apply(QApplication &app);
QIcon icon(const QString &name, const QColor &color = QColor("#646773"));
}

/** 标准复选框语义，提供键盘操作和屏幕阅读器支持。 */
class StudioSwitch : public QCheckBox
{
public:
    explicit StudioSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {40, 24}; }
protected:
    void paintEvent(QPaintEvent *) override;
    bool hitButton(const QPoint &point) const override { return rect().contains(point); }
private:
    QVariantAnimation m_animation;
    qreal m_position = 0;
};
