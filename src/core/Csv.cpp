#include "Csv.h"

#include <QRegularExpression>
#include <QSet>

namespace Csv
{

QList<QStringList> parse(const QString &input)
{
    QString text = input;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    // Pick the separator that appears most in the first line, outside quotes.
    QChar sep = QLatin1Char(',');
    {
        int commas = 0, semis = 0, tabs = 0;
        bool quoted = false;
        for (const QChar c : text) {
            if (c == QLatin1Char('"'))
                quoted = !quoted;
            else if (!quoted && (c == QLatin1Char('\n')))
                break;
            else if (!quoted && c == QLatin1Char(','))
                ++commas;
            else if (!quoted && c == QLatin1Char(';'))
                ++semis;
            else if (!quoted && c == QLatin1Char('\t'))
                ++tabs;
        }
        if (tabs > commas && tabs >= semis)
            sep = QLatin1Char('\t');
        else if (semis > commas)
            sep = QLatin1Char(';');
    }

    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    bool fieldStarted = false;
    const int n = int(text.size());
    for (int i = 0; i < n; ++i) {
        const QChar c = text[i];
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < n && text[i + 1] == QLatin1Char('"')) {
                    field += QLatin1Char('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == QLatin1Char('"') && !fieldStarted) {
            quoted = true;
            fieldStarted = true;
        } else if (c == sep) {
            row.append(field);
            field.clear();
            fieldStarted = false;
        } else if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            if (c == QLatin1Char('\r') && i + 1 < n && text[i + 1] == QLatin1Char('\n'))
                ++i;
            row.append(field);
            field.clear();
            fieldStarted = false;
            if (!(row.size() == 1 && row.first().isEmpty()))
                rows.append(row);
            row.clear();
        } else {
            field += c;
            fieldStarted = true;
        }
    }
    if (fieldStarted || !row.isEmpty()) {
        row.append(field);
        rows.append(row);
    }
    return rows;
}

QString escapeField(const QString &field)
{
    if (field.contains(QLatin1Char(',')) || field.contains(QLatin1Char('"')) || field.contains(QLatin1Char('\n'))
        || field.contains(QLatin1Char('\r')) || field.startsWith(QLatin1Char(' ')) || field.endsWith(QLatin1Char(' '))) {
        QString f = field;
        f.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + f + QLatin1Char('"');
    }
    return field;
}

QString write(const QList<QStringList> &rows)
{
    QString out;
    for (const QStringList &r : rows) {
        QStringList fields;
        for (const QString &f : r)
            fields.append(escapeField(f));
        out += fields.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}

PropType inferType(const QString &header, const QStringList &values)
{
    QStringList vals;
    for (const QString &v : values)
        if (!v.trimmed().isEmpty())
            vals.append(v.trimmed());
    const QString h = header.trimmed().toLower();
    if (vals.isEmpty())
        return h == QLatin1String("status") ? PropType::Status : PropType::Text;

    auto all = [&](const auto &pred) {
        for (const QString &v : vals)
            if (!pred(v))
                return false;
        return true;
    };

    if (all([](const QString &v) {
            const QString l = v.toLower();
            return l == QLatin1String("yes") || l == QLatin1String("no") || l == QLatin1String("true")
                   || l == QLatin1String("false");
        }))
        return PropType::Checkbox;

    static const QRegularExpression number(QStringLiteral("^[-+]?[$€£¥]?\\s?\\d{1,3}(,?\\d{3})*(\\.\\d+)?%?$|^[-+]?\\d*\\.\\d+%?$"));
    if (all([](const QString &v) { return number.match(v).hasMatch(); }))
        return PropType::Number;

    Property dateProbe;
    dateProbe.type = PropType::Date;
    if (all([&](const QString &v) { return !parseValue(dateProbe, v, false).isNull(); }))
        return PropType::Date;

    static const QRegularExpression url(QStringLiteral("^(https?://|www\\.)\\S+$"), QRegularExpression::CaseInsensitiveOption);
    if (all([](const QString &v) { return url.match(v).hasMatch(); }))
        return PropType::Url;
    static const QRegularExpression email(QStringLiteral("^[^@\\s]+@[^@\\s]+\\.[^@\\s]+$"));
    if (all([](const QString &v) { return email.match(v).hasMatch(); }))
        return PropType::Email;

    // Repeated short values are tags.
    QSet<QString> distinct, tokens;
    bool anyComma = false;
    int totalLength = 0;
    for (const QString &v : vals) {
        distinct.insert(v);
        totalLength += int(v.size());
        if (v.contains(QLatin1Char(',')))
            anyComma = true;
        for (const QString &t : v.split(QLatin1Char(','), Qt::SkipEmptyParts))
            tokens.insert(t.trimmed());
    }
    const double avgLen = double(totalLength) / vals.size();
    if (h == QLatin1String("status") && distinct.size() <= 12)
        return PropType::Status;
    if (anyComma && avgLen < 60 && tokens.size() <= qMax<qsizetype>(12, vals.size() * 6 / 10)
        && tokens.size() < distinct.size() + vals.size())
        return PropType::MultiSelect;
    if (vals.size() >= 2 && avgLen < 32 && distinct.size() <= qMax<qsizetype>(3, vals.size() * 6 / 10) && distinct.size() <= 20)
        return PropType::Select;
    return PropType::Text;
}

Schema schemaFromCsv(const QList<QStringList> &table, QList<QJsonObject> *rowProps, QStringList *titles,
                     const QString &databaseName)
{
    Q_UNUSED(databaseName)
    Schema schema;
    if (table.isEmpty())
        return Schema::basic();
    const QStringList header = table.first();
    const int cols = int(header.size());
    QList<QStringList> columns(cols);
    for (int r = 1; r < table.size(); ++r)
        for (int c = 0; c < cols; ++c)
            columns[c].append(c < table[r].size() ? table[r][c] : QString());

    QStringList ids;
    for (int c = 0; c < cols; ++c) {
        Property p;
        p.name = header[c].trimmed().isEmpty() ? QStringLiteral("Column %1").arg(c + 1) : header[c].trimmed();
        if (c == 0) {
            p.id = QStringLiteral("title");
            p.type = PropType::Title;
        } else {
            p.id = schema.newPropId();
            p.type = inferType(p.name, columns[c]);
        }
        ids.append(p.id);
        schema.props.append(p);
    }

    for (int r = 1; r < table.size(); ++r) {
        QJsonObject values;
        QString title;
        for (int c = 0; c < cols; ++c) {
            const QString cell = c < table[r].size() ? table[r][c] : QString();
            if (c == 0) {
                title = cell.trimmed();
                continue;
            }
            Property &p = schema.props[c];
            const QJsonValue v = parseValue(p, cell, true);
            if (!v.isNull() && !v.isUndefined())
                values[p.id] = v;
        }
        if (rowProps)
            rowProps->append(values);
        if (titles)
            titles->append(title);
    }
    // Status options get groups from their names.
    for (Property &p : schema.props) {
        if (p.type != PropType::Status)
            continue;
        for (Option &o : p.options) {
            const QString n = o.name.toLower();
            if (n.contains(QLatin1String("done")) || n.contains(QLatin1String("complete")) || n.contains(QLatin1String("finished"))
                || n.contains(QLatin1String("shipped")) || n.contains(QLatin1String("read")))
                o.group = 2;
            else if (n.contains(QLatin1String("progress")) || n.contains(QLatin1String("doing")) || n.contains(QLatin1String("review"))
                     || n.contains(QLatin1String("reading")) || n.contains(QLatin1String("active")))
                o.group = 1;
            else
                o.group = 0;
        }
    }
    schema.views.append(Schema::makeView(schema, ViewType::Table));
    if (!schema.defaultGroupProp().isEmpty())
        schema.views.append(Schema::makeView(schema, ViewType::Board));
    return schema;
}

}
