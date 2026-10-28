#pragma once
#include "product_config.h"
#include <QDialog>

class QLineEdit;
class QComboBox;
class QLabel;
class QButtonGroup;

/** 可直接嵌入主窗口的产品设置，编辑不会自动重启仿真。 */
class ProductEditor : public QWidget
{
    Q_OBJECT
public:
    explicit ProductEditor(const ProductConfig &config, QWidget *parent = nullptr);
    ProductConfig config() const;
    void setConfig(const ProductConfig &config);
signals:
    void edited();
private:
    void notifyEdited();
    void selectMode(int mode);
    void updateAvailability();
    ProductConfig m_base;
    bool m_loading = false;
    QLineEdit *m_name;
    QComboBox *m_frontend;
    QButtonGroup *m_modes;
    QLabel *m_validation;
    QMap<QString, QWidget *> m_fields;
};

/** 同一个编辑器也可在独立对话框中复用。 */
class ProductDialog : public QDialog
{
public:
    ProductDialog(const ProductConfig &config, QWidget *parent);
    ProductConfig config() const;
private:
    void accept() override;
    ProductEditor *m_editor;
};
