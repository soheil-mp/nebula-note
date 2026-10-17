#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

// A small read-only zip reader (stored and deflate entries) for importing
// Notion exports. Zip64 archives are not supported.
class ZipReader
{
public:
    bool open(const QString &path);
    bool openData(const QByteArray &data);
    QString error() const { return m_error; }

    QStringList names() const { return m_order; }
    bool contains(const QString &name) const { return m_entries.contains(name); }
    QByteArray read(const QString &name) const;

    static QByteArray inflateRaw(const QByteArray &compressed, qint64 expectedSize);

private:
    struct Entry {
        quint16 method = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint32 localOffset = 0;
    };
    QByteArray m_data;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QString m_error;
};
