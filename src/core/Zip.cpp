#include "Zip.h"

#include <QFile>
#include <QStringDecoder>

#include <zlib.h>

namespace
{
quint16 u16(const QByteArray &d, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 u32(const QByteArray &d, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(d.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}
}

bool ZipReader::open(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        m_error = f.errorString();
        return false;
    }
    return openData(f.readAll());
}

bool ZipReader::openData(const QByteArray &data)
{
    m_data = data;
    m_entries.clear();
    m_order.clear();
    const qint64 n = m_data.size();
    if (n < 22) {
        m_error = QStringLiteral("Not a zip file");
        return false;
    }
    // End of central directory: scan back over a possible comment.
    qint64 eocd = -1;
    for (qint64 i = n - 22; i >= qMax<qint64>(0, n - 22 - 65535); --i) {
        if (u32(m_data, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        m_error = QStringLiteral("Not a zip file");
        return false;
    }
    const quint16 count = u16(m_data, eocd + 10);
    qint64 pos = u32(m_data, eocd + 16);
    if (pos == 0xFFFFFFFF) {
        m_error = QStringLiteral("Zip64 archives are not supported");
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > n || u32(m_data, pos) != 0x02014b50) {
            m_error = QStringLiteral("Damaged zip directory");
            return !m_entries.isEmpty();
        }
        const quint16 flags = u16(m_data, pos + 8);
        Entry e;
        e.method = u16(m_data, pos + 10);
        e.compressedSize = u32(m_data, pos + 20);
        e.size = u32(m_data, pos + 24);
        const quint16 nameLen = u16(m_data, pos + 28);
        const quint16 extraLen = u16(m_data, pos + 30);
        const quint16 commentLen = u16(m_data, pos + 32);
        e.localOffset = u32(m_data, pos + 42);
        const QByteArray rawName = m_data.mid(pos + 46, nameLen);
        // Bit 11 marks UTF-8 names; Notion always writes UTF-8 anyway.
        QStringDecoder utf8(QStringDecoder::Utf8);
        QString name = (flags & 0x800) ? QString::fromUtf8(rawName) : QString(utf8(rawName));
        if (utf8.hasError())
            name = QString::fromLatin1(rawName);
        name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (!name.endsWith(QLatin1Char('/'))) {
            m_entries.insert(name, e);
            m_order.append(name);
        }
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray ZipReader::read(const QString &name) const
{
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return {};
    const Entry &e = *it;
    const qint64 lh = e.localOffset;
    if (lh + 30 > m_data.size() || u32(m_data, lh) != 0x04034b50)
        return {};
    const qint64 start = lh + 30 + u16(m_data, lh + 26) + u16(m_data, lh + 28);
    const QByteArray raw = m_data.mid(start, e.compressedSize);
    if (e.method == 0)
        return raw;
    if (e.method == 8)
        return inflateRaw(raw, e.size);
    return {};
}

QByteArray ZipReader::inflateRaw(const QByteArray &compressed, qint64 expectedSize)
{
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return {};
    QByteArray out;
    out.resize(qMax<qint64>(expectedSize, 1024));
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(compressed.constData()));
    zs.avail_in = uInt(compressed.size());
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (zs.total_out >= uLong(out.size()))
            out.resize(out.size() * 2);
        zs.next_out = reinterpret_cast<Bytef *>(out.data() + zs.total_out);
        zs.avail_out = uInt(out.size() - zs.total_out);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return {};
        }
        if (ret == Z_OK && zs.avail_in == 0 && zs.avail_out != 0)
            break; // truncated input
    }
    out.resize(qsizetype(zs.total_out));
    inflateEnd(&zs);
    return out;
}
