#pragma once
#include <QByteArray>
#include <QString>
#include <cstdint>

/** APL host byte storage; the library decides what and when to persist. */
class AplMemory
{
  public:
    bool open(const QString &path);
    bool read(uint32_t offset, uint8_t *data, uint32_t length) const;
    bool write(uint32_t offset, const uint8_t *data, uint32_t length);
    bool erase();
    QString error() const
    {
        return m_error;
    }
    int writes() const
    {
        return m_writes;
    }

  private:
    QByteArray m_bytes = QByteArray(4096, char(0xff));
    QString m_path;
    QString m_error;
    int m_writes = 0;
};
