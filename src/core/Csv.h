#pragma once

#include "Schema.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace Csv
{
// RFC 4180: quoted fields, doubled quotes, newlines inside quotes. Detects
// a UTF-8 BOM and ',' / ';' / tab separators.
QList<QStringList> parse(const QString &text);
QString write(const QList<QStringList> &rows);
QString escapeField(const QString &field);

// Guesses a property type from a column's values (Notion's CSV export has
// no type information).
PropType inferType(const QString &header, const QStringList &values);

// A database schema and its rows' values from a parsed CSV. The first column
// is the title. `titles` receives each row's title.
Schema schemaFromCsv(const QList<QStringList> &table, QList<QJsonObject> *rowProps, QStringList *titles,
                     const QString &databaseName = {});
}
