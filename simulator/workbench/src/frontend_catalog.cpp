#include "frontend_catalog.h"
FrontendCatalog::FrontendCatalog(QObject *parent) : QAbstractListModel(parent)
{
    m_items = {{QStringLiteral("圆形面板"),
                QStringLiteral("APL 六键面板。鼠标按住或 Tab 选键后用空格、Enter 操作。"),
                QUrl("qrc:/panels/round/RoundPanel.qml")},
               {QStringLiteral("按键台"), QStringLiteral("APL 按键与显示调试视图。"),
                QUrl("qrc:/panels/bench/KeyBench.qml")}};
}
int FrontendCatalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}
QVariant FrontendCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const auto &item = m_items.at(index.row());
    switch (role)
    {
    case NameRole:
    case Qt::DisplayRole:
        return item.name;
    case DescriptionRole:
        return item.description;
    case UrlRole:
        return item.url;
    default:
        return {};
    }
}
QHash<int, QByteArray> FrontendCatalog::roleNames() const
{
    return {{NameRole, "name"}, {DescriptionRole, "description"}, {UrlRole, "url"}};
}
QUrl FrontendCatalog::urlAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).url : QUrl();
}
QString FrontendCatalog::nameAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).name : QString();
}
QString FrontendCatalog::descriptionAt(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row).description : QString();
}
