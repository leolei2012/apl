#include "apl_memory.h"
#include <QFile>
#include <QSaveFile>
#include <cstring>

bool AplMemory::open(const QString &path)
{
    QByteArray bytes(4096, char(0xff));
    QFile file(path);
    if (file.exists())
    {
        if (!file.open(QIODevice::ReadOnly))
        {
            m_error = file.errorString();
            return false;
        }
        bytes = file.readAll();
        if (bytes.size() != 4096)
        {
            m_error = QStringLiteral("存储文件必须为 4096 字节");
            return false;
        }
    }
    m_bytes = bytes;
    m_path = path;
    m_writes = 0;
    m_error.clear();
    return true;
}
bool AplMemory::read(uint32_t offset, uint8_t *data, uint32_t length) const
{
    if (!data || offset > 4096u || length > 4096u - offset)
        return false;
    std::memcpy(data, m_bytes.constData() + offset, length);
    return true;
}
bool AplMemory::write(uint32_t offset, const uint8_t *data, uint32_t length)
{
    if (!data || offset > 4096u || length > 4096u - offset)
    {
        m_error = QStringLiteral("存储访问越界");
        return false;
    }
    QByteArray next = m_bytes;
    std::memcpy(next.data() + offset, data, length);
    if (next == m_bytes)
        return true;
    if (!m_path.isEmpty())
    {
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly) || file.write(next) != next.size() || !file.commit())
        {
            m_error = file.errorString();
            return false;
        }
    }
    m_bytes = next;
    ++m_writes;
    m_error.clear();
    return true;
}
bool AplMemory::erase()
{
    const QByteArray bytes(4096, char(0xff));
    return write(0, reinterpret_cast<const uint8_t *>(bytes.constData()), 4096);
}
