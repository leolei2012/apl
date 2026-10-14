#pragma once
#include <QAbstractListModel>
#include <QUrl>

/** The two built-in APL test panels; no user project/plugin discovery. */
class FrontendCatalog : public QAbstractListModel
{
    Q_OBJECT
  public:
    enum Role
    {
        NameRole = Qt::UserRole + 1,
        DescriptionRole,
        UrlRole
    };
    explicit FrontendCatalog(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QUrl urlAt(int row) const;
    QString nameAt(int row) const;
    QString descriptionAt(int row) const;

  private:
    struct Item
    {
        QString name;
        QString description;
        QUrl url;
    };
    QList<Item> m_items;
};
