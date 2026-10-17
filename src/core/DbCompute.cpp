// Computed database values: formulas, rollups, calculations, grouping and
// sub-item nesting.
#include "Formula.h"
#include "Schema.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <functional>

namespace
{
QString tr(const char *s)
{
    return QCoreApplication::translate("Schema", s);
}

const Schema *schemaOfRow(const RowRef &row)
{
    const DbLookup *l = dbLookup();
    if (!l || !row.meta)
        return nullptr;
    return l->lookupSchema(row.meta->parentId);
}

const Property *propByName(const Schema &s, const QString &name)
{
    for (const Property &p : s.props)
        if (p.name == name)
            return &p;
    for (const Property &p : s.props)
        if (p.name.compare(name, Qt::CaseInsensitive) == 0)
            return &p;
    return nullptr;
}

// A property value as a formula value.
FValue toFormulaValue(const Property &p, const QJsonValue &v)
{
    switch (p.type) {
    case PropType::Title:
    case PropType::Text:
    case PropType::Url:
    case PropType::Email:
    case PropType::Phone:
        return FValue::string(v.toString());
    case PropType::Number:
    case PropType::UniqueId:
        return v.isDouble() ? FValue::number(v.toDouble()) : FValue();
    case PropType::Checkbox:
        return FValue::boolean(v.toBool());
    case PropType::Select:
    case PropType::Status: {
        const Option *o = p.option(v.toString());
        return o ? FValue::string(o->name) : FValue::string(QString());
    }
    case PropType::MultiSelect: {
        QList<FValue> items;
        for (const QJsonValue &x : v.toArray())
            if (const Option *o = p.option(x.toString()))
                items.append(FValue::string(o->name));
        return FValue::makeList(items);
    }
    case PropType::Date:
    case PropType::Created:
    case PropType::Edited: {
        const QString s = v.toString();
        const QDateTime d = dateTimeOf(s);
        if (!d.isValid())
            return {};
        return FValue::dateRange(d, dateEndOf(s), s.size() > 10 && s[10] == QLatin1Char('T'));
    }
    case PropType::Relation: {
        QList<FValue> items;
        for (const QString &id : relationIds(v))
            items.append(FValue::page(id));
        return FValue::makeList(items);
    }
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy: {
        QList<FValue> items;
        for (const QJsonValue &x : v.toArray())
            items.append(FValue::person(x.toString()));
        if (p.type != PropType::Person && items.size() == 1)
            return items.first();
        return FValue::makeList(items);
    }
    case PropType::Files: {
        QList<FValue> items;
        for (const FileRef &f : filesOf(v))
            items.append(FValue::string(f.name));
        return FValue::makeList(items);
    }
    case PropType::Formula:
    case PropType::Rollup: {
        if (v.isDouble())
            return FValue::number(v.toDouble());
        if (v.isBool())
            return FValue::boolean(v.toBool());
        if (v.isArray()) {
            QList<FValue> items;
            for (const QJsonValue &x : v.toArray())
                items.append(FValue::string(x.toString()));
            return FValue::makeList(items);
        }
        if (resultType(p) == PropType::Date) {
            const QString s = v.toString();
            const QDateTime d = dateTimeOf(s);
            if (d.isValid())
                return FValue::dateRange(d, dateEndOf(s), s.size() > 10 && s[10] == QLatin1Char('T'));
        }
        return v.isNull() || v.isUndefined() ? FValue() : FValue::string(v.toString());
    }
    case PropType::Button:
        return {};
    }
    return {};
}

FormulaContext contextFor(const RowRef &row);

FormulaContext contextFor(const RowRef &row)
{
    FormulaContext ctx;
    ctx.pageId = row.meta ? row.meta->id : QString();
    const RowRef self = row;
    ctx.prop = [self](const QString &pageId, const QString &name, QString *error) -> FValue {
        const DbLookup *l = dbLookup();
        RowRef r = self;
        if (!pageId.isEmpty() && (!self.meta || pageId != self.meta->id)) {
            if (!l) {
                *error = tr("Related pages aren't available here");
                return {};
            }
            r.meta = l->lookupPage(pageId);
            r.props = l->lookupProps(pageId);
            if (!r.meta)
                return {};
        }
        const Schema *s = schemaOfRow(r);
        if (!s) {
            *error = tr("Property “%1” isn't available here").arg(name);
            return {};
        }
        const Property *p = propByName(*s, name);
        if (!p) {
            *error = tr("No property named “%1”").arg(name);
            return {};
        }
        if (p->type == PropType::Formula) {
            // Evaluated here so errors (a formula using itself) propagate.
            const FormulaResult fr = Formula::evaluate(p->formula, contextFor(r));
            if (!fr.ok()) {
                *error = fr.error;
                return {};
            }
            return fr.value;
        }
        return toFormulaValue(*p, rowValue(*p, r));
    };
    ctx.pageTitle = [](const QString &id) {
        const DbLookup *l = dbLookup();
        const PageMeta *m = l ? l->lookupPage(id) : nullptr;
        if (!m)
            return QString();
        return m->title.trimmed().isEmpty() ? tr("Untitled") : m->title;
    };
    ctx.personName = [](const QString &id) {
        const DbLookup *l = dbLookup();
        return l ? l->lookupPersonName(id) : id;
    };
    return ctx;
}

QJsonValue rollupValue(const Property &p, const RowRef &row)
{
    const DbLookup *l = dbLookup();
    const Schema *own = schemaOfRow(row);
    if (!l || !own)
        return {};
    const Property *rel = own->prop(p.rollupRelation);
    if (!rel || rel->type != PropType::Relation)
        return {};
    const Schema *target = l->lookupSchema(rel->relationDb);
    if (!target)
        return {};
    const Property *tp = target->prop(p.rollupProp);
    if (!tp)
        tp = target->titleProp();
    if (!tp)
        return {};
    QList<QJsonValue> values;
    for (const QString &id : relationIds(rowValue(*rel, row))) {
        const PageMeta *m = l->lookupPage(id);
        if (!m || m->isTrashed())
            continue;
        values.append(rowValue(*tp, RowRef{m, l->lookupProps(id)}));
    }
    QJsonValue out = aggregate(*tp, values, p.rollupFn);
    return out;
}

} // namespace

QString formulaError(const Property &p, const RowRef &row)
{
    if (p.type != PropType::Formula)
        return {};
    return Formula::evaluate(p.formula, contextFor(row)).error;
}

QJsonValue computedValue(const Property &p, const RowRef &row)
{
    if (p.type == PropType::Formula) {
        const FormulaContext ctx = contextFor(row);
        const FormulaResult r = Formula::evaluate(p.formula, ctx);
        if (!r.ok())
            return QJsonValue();
        return Formula::toJson(r.value, &ctx);
    }
    if (p.type == PropType::Rollup)
        return rollupValue(p, row);
    return {};
}

// ---- calculations ------------------------------------------------------------------

QStringList calcFunctions(const Property &p, bool forRollup)
{
    QStringList out;
    if (forRollup)
        out << QStringLiteral("original") << QStringLiteral("showunique");
    else
        out << QStringLiteral("none");
    out << QStringLiteral("count") << QStringLiteral("values") << QStringLiteral("unique") << QStringLiteral("empty")
        << QStringLiteral("notempty") << QStringLiteral("pctempty") << QStringLiteral("pctnotempty");
    const PropType t = resultType(p);
    if (t == PropType::Number)
        out << QStringLiteral("sum") << QStringLiteral("avg") << QStringLiteral("median") << QStringLiteral("min")
            << QStringLiteral("max") << QStringLiteral("range");
    if (t == PropType::Date || t == PropType::Created || t == PropType::Edited)
        out << QStringLiteral("earliest") << QStringLiteral("latest") << QStringLiteral("daterange");
    if (t == PropType::Checkbox) {
        out = forRollup ? QStringList{QStringLiteral("original"), QStringLiteral("count")} : QStringList{QStringLiteral("none"), QStringLiteral("count")};
        out << QStringLiteral("checked") << QStringLiteral("unchecked") << QStringLiteral("pctchecked")
            << QStringLiteral("pctunchecked");
    }
    if (t == PropType::Button)
        out = forRollup ? QStringList{QStringLiteral("count")} : QStringList{QStringLiteral("none"), QStringLiteral("count")};
    return out;
}

QString calcName(const QString &fn)
{
    static const QHash<QString, const char *> names = {
        {QStringLiteral("none"), QT_TRANSLATE_NOOP("Schema", "None")},
        {QStringLiteral("original"), QT_TRANSLATE_NOOP("Schema", "Show original")},
        {QStringLiteral("showunique"), QT_TRANSLATE_NOOP("Schema", "Show unique values")},
        {QStringLiteral("count"), QT_TRANSLATE_NOOP("Schema", "Count all")},
        {QStringLiteral("values"), QT_TRANSLATE_NOOP("Schema", "Count values")},
        {QStringLiteral("unique"), QT_TRANSLATE_NOOP("Schema", "Count unique values")},
        {QStringLiteral("empty"), QT_TRANSLATE_NOOP("Schema", "Count empty")},
        {QStringLiteral("notempty"), QT_TRANSLATE_NOOP("Schema", "Count not empty")},
        {QStringLiteral("pctempty"), QT_TRANSLATE_NOOP("Schema", "Percent empty")},
        {QStringLiteral("pctnotempty"), QT_TRANSLATE_NOOP("Schema", "Percent not empty")},
        {QStringLiteral("sum"), QT_TRANSLATE_NOOP("Schema", "Sum")},
        {QStringLiteral("avg"), QT_TRANSLATE_NOOP("Schema", "Average")},
        {QStringLiteral("median"), QT_TRANSLATE_NOOP("Schema", "Median")},
        {QStringLiteral("min"), QT_TRANSLATE_NOOP("Schema", "Min")},
        {QStringLiteral("max"), QT_TRANSLATE_NOOP("Schema", "Max")},
        {QStringLiteral("range"), QT_TRANSLATE_NOOP("Schema", "Range")},
        {QStringLiteral("earliest"), QT_TRANSLATE_NOOP("Schema", "Earliest date")},
        {QStringLiteral("latest"), QT_TRANSLATE_NOOP("Schema", "Latest date")},
        {QStringLiteral("daterange"), QT_TRANSLATE_NOOP("Schema", "Date range")},
        {QStringLiteral("checked"), QT_TRANSLATE_NOOP("Schema", "Checked")},
        {QStringLiteral("unchecked"), QT_TRANSLATE_NOOP("Schema", "Unchecked")},
        {QStringLiteral("pctchecked"), QT_TRANSLATE_NOOP("Schema", "Percent checked")},
        {QStringLiteral("pctunchecked"), QT_TRANSLATE_NOOP("Schema", "Percent unchecked")},
    };
    const char *n = names.value(fn, nullptr);
    return n ? tr(n) : fn;
}

namespace
{
// Values a multi-valued property contributes, one per item, for unique counts.
QStringList atomsOf(const Property &p, const QJsonValue &v)
{
    if (v.isArray()) {
        QStringList out;
        for (const QJsonValue &x : v.toArray())
            out.append(x.isString() ? x.toString() : QString::fromUtf8(QJsonDocument(QJsonArray{x}).toJson(QJsonDocument::Compact)));
        return out;
    }
    if (isEmptyValue(p, v))
        return {};
    if (v.isDouble())
        return {QString::number(v.toDouble(), 'g', 15)};
    if (v.isBool())
        return {v.toBool() ? QStringLiteral("1") : QStringLiteral("0")};
    return {v.toString()};
}
} // namespace

QJsonValue aggregate(const Property &prop, const QList<QJsonValue> &values, const QString &fn)
{
    const int n = int(values.size());
    const PropType t = resultType(prop);
    if (fn == QLatin1String("none"))
        return {};
    if (fn == QLatin1String("original") || fn == QLatin1String("showunique")) {
        QJsonArray out;
        QSet<QString> seen;
        for (const QJsonValue &v : values) {
            if (isEmptyValue(prop, v) && t != PropType::Checkbox)
                continue;
            // Multi-valued sources contribute their items.
            QStringList parts;
            if (prop.type == PropType::MultiSelect || prop.type == PropType::Relation || prop.type == PropType::Person
                || prop.type == PropType::Files) {
                for (const QJsonValue &x : v.toArray()) {
                    Property single = prop;
                    parts.append(displayValue(single, QJsonArray{x}));
                }
            } else {
                parts.append(displayValue(prop, v));
            }
            for (const QString &part : parts) {
                if (fn == QLatin1String("showunique")) {
                    if (seen.contains(part))
                        continue;
                    seen.insert(part);
                }
                out.append(part);
            }
        }
        return out;
    }
    if (fn == QLatin1String("count"))
        return n;
    int empty = 0;
    QSet<QString> unique;
    for (const QJsonValue &v : values) {
        const bool e = isEmptyValue(prop, v);
        if (e)
            ++empty;
        for (const QString &a : atomsOf(prop, v))
            unique.insert(a);
    }
    if (fn == QLatin1String("values")) {
        // Notion counts each item of multi-valued properties.
        int count = 0;
        for (const QJsonValue &v : values)
            count += v.isArray() ? int(v.toArray().size()) : (isEmptyValue(prop, v) ? 0 : 1);
        return count;
    }
    if (fn == QLatin1String("unique"))
        return int(unique.size());
    if (fn == QLatin1String("empty"))
        return empty;
    if (fn == QLatin1String("notempty"))
        return n - empty;
    if (fn == QLatin1String("pctempty"))
        return n ? double(empty) / n : 0.0;
    if (fn == QLatin1String("pctnotempty"))
        return n ? double(n - empty) / n : 0.0;
    if (fn == QLatin1String("checked") || fn == QLatin1String("unchecked") || fn == QLatin1String("pctchecked")
        || fn == QLatin1String("pctunchecked")) {
        int checked = 0;
        for (const QJsonValue &v : values)
            if (v.toBool())
                ++checked;
        if (fn == QLatin1String("checked"))
            return checked;
        if (fn == QLatin1String("unchecked"))
            return n - checked;
        if (fn == QLatin1String("pctchecked"))
            return n ? double(checked) / n : 0.0;
        return n ? double(n - checked) / n : 0.0;
    }
    if (fn == QLatin1String("sum") || fn == QLatin1String("avg") || fn == QLatin1String("median")
        || fn == QLatin1String("min") || fn == QLatin1String("max") || fn == QLatin1String("range")) {
        QList<double> xs;
        for (const QJsonValue &v : values)
            if (v.isDouble())
                xs.append(v.toDouble());
        if (xs.isEmpty())
            return fn == QLatin1String("sum") ? QJsonValue(0.0) : QJsonValue();
        std::sort(xs.begin(), xs.end());
        double sum = 0;
        for (double x : xs)
            sum += x;
        if (fn == QLatin1String("sum"))
            return sum;
        if (fn == QLatin1String("avg"))
            return sum / xs.size();
        if (fn == QLatin1String("median")) {
            const int m = int(xs.size());
            return m % 2 ? xs[m / 2] : (xs[m / 2 - 1] + xs[m / 2]) / 2.0;
        }
        if (fn == QLatin1String("min"))
            return xs.first();
        if (fn == QLatin1String("max"))
            return xs.last();
        return xs.last() - xs.first();
    }
    if (fn == QLatin1String("earliest") || fn == QLatin1String("latest") || fn == QLatin1String("daterange")) {
        QDateTime lo, hi;
        QString loS, hiS;
        for (const QJsonValue &v : values) {
            const QDateTime d = dateTimeOf(v);
            if (!d.isValid())
                continue;
            const QDateTime e = dateEndOf(v).isValid() ? dateEndOf(v) : d;
            if (!lo.isValid() || d < lo) {
                lo = d;
                loS = v.toString().section(QLatin1Char('/'), 0, 0).section(QLatin1Char('#'), 0, 0);
            }
            if (!hi.isValid() || e > hi) {
                hi = e;
                const QString s = v.toString().section(QLatin1Char('#'), 0, 0);
                hiS = s.contains(QLatin1Char('/')) ? s.section(QLatin1Char('/'), 1, 1) : s;
            }
        }
        if (!lo.isValid())
            return {};
        if (fn == QLatin1String("earliest"))
            return loS;
        if (fn == QLatin1String("latest"))
            return hiS;
        const qint64 days = lo.date().daysTo(hi.date());
        return days == 1 ? tr("1 day") : tr("%1 days").arg(days);
    }
    return {};
}

QString aggregateDisplay(const Property &p, const QString &fn, const QJsonValue &result)
{
    if (result.isNull() || result.isUndefined())
        return {};
    if (fn.startsWith(QLatin1String("pct")))
        return formatNumber(result.toDouble(), QStringLiteral("percent"), -1);
    if (result.isArray()) {
        QStringList parts;
        for (const QJsonValue &x : result.toArray())
            parts.append(x.toString());
        return parts.join(QStringLiteral(", "));
    }
    if (fn == QLatin1String("count") || fn == QLatin1String("values") || fn == QLatin1String("unique")
        || fn == QLatin1String("empty") || fn == QLatin1String("notempty") || fn == QLatin1String("checked")
        || fn == QLatin1String("unchecked"))
        return QString::number(qint64(result.toDouble()));
    if (fn == QLatin1String("earliest") || fn == QLatin1String("latest")) {
        Property d = p;
        d.type = PropType::Date;
        return formatDateFor(d, result.toString());
    }
    if (result.isDouble()) {
        // Rollups format numbers like the property they roll up when known;
        // otherwise plain.
        const QString fmt = p.type == PropType::Number || p.type == PropType::Rollup || p.type == PropType::Formula
                                ? p.numberFormat
                                : QStringLiteral("number");
        double v = result.toDouble();
        if (fn == QLatin1String("avg") && fmt == QLatin1String("number") && p.decimals < 0)
            v = std::round(v * 100.0) / 100.0;
        return formatNumber(v, fmt, p.decimals);
    }
    return result.toString();
}

QString calculate(const Property &p, const QList<RowRef> &rows, const QString &fn)
{
    if (fn.isEmpty() || fn == QLatin1String("none"))
        return {};
    QList<QJsonValue> values;
    values.reserve(rows.size());
    for (const RowRef &r : rows)
        values.append(rowValue(p, r));
    return aggregateDisplay(p, fn, aggregate(p, values, fn));
}

// ---- grouping -------------------------------------------------------------------

bool canGroupBy(const Property &p)
{
    switch (p.type) {
    case PropType::Button:
    case PropType::Files:
        return false;
    default:
        return true;
    }
}

QStringList groupKeysFor(const Property &prop, const QJsonValue &value, const QString &mode, double step)
{
    const PropType t = resultType(prop);
    switch (prop.type) {
    case PropType::Select:
    case PropType::Status:
        return {prop.option(value.toString()) ? value.toString() : QString()};
    case PropType::MultiSelect: {
        QStringList keys;
        for (const QJsonValue &x : value.toArray())
            if (prop.option(x.toString()))
                keys.append(x.toString());
        return keys.isEmpty() ? QStringList{QString()} : keys;
    }
    case PropType::Relation: {
        const QStringList ids = relationIds(value);
        return ids.isEmpty() ? QStringList{QString()} : ids;
    }
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy: {
        QStringList keys;
        for (const QJsonValue &x : value.toArray())
            keys.append(x.toString());
        return keys.isEmpty() ? QStringList{QString()} : keys;
    }
    case PropType::Checkbox:
        return {value.toBool() ? QStringLiteral("true") : QStringLiteral("false")};
    default:
        break;
    }
    if (t == PropType::Checkbox)
        return {value.toBool() ? QStringLiteral("true") : QStringLiteral("false")};
    if (t == PropType::Date || t == PropType::Created || t == PropType::Edited) {
        const QDate d = dateOf(value);
        if (!d.isValid())
            return {QString()};
        const QString m = mode.isEmpty() ? QStringLiteral("relative") : mode;
        if (m == QLatin1String("day"))
            return {d.toString(Qt::ISODate)};
        if (m == QLatin1String("week")) {
            const QDate monday = d.addDays(1 - d.dayOfWeek());
            return {QStringLiteral("w") + monday.toString(Qt::ISODate)};
        }
        if (m == QLatin1String("month"))
            return {d.toString(QStringLiteral("yyyy-MM"))};
        if (m == QLatin1String("year"))
            return {QString::number(d.year())};
        // Relative buckets, like Notion's.
        const qint64 days = QDate::currentDate().daysTo(d);
        if (days == 0)
            return {QStringLiteral("r3today")};
        if (days == 1)
            return {QStringLiteral("r4tomorrow")};
        if (days == -1)
            return {QStringLiteral("r2yesterday")};
        if (days > 1 && days <= 7)
            return {QStringLiteral("r5next7")};
        if (days > 7 && days <= 30)
            return {QStringLiteral("r6next30")};
        if (days < -1 && days >= -7)
            return {QStringLiteral("r1past7")};
        if (days < -7 && days >= -30)
            return {QStringLiteral("r0past30")};
        return {QStringLiteral("m") + d.toString(QStringLiteral("yyyy-MM"))};
    }
    if (t == PropType::Number) {
        if (!value.isDouble())
            return {QString()};
        const double s = step > 0 ? step : 10;
        const double lo = std::floor(value.toDouble() / s) * s;
        return {QStringLiteral("n") + QString::number(lo, 'g', 15)};
    }
    // Text, title, URL, formulas producing text.
    QString text = displayValue(prop, value).trimmed();
    if (text.isEmpty())
        return {QString()};
    if (mode == QLatin1String("alpha"))
        return {text.left(1).toUpper()};
    return {text};
}

namespace
{
QString groupLabel(const Property &prop, const QString &key, double step)
{
    if (key.isEmpty())
        return tr("No %1").arg(prop.name);
    const PropType t = resultType(prop);
    switch (prop.type) {
    case PropType::Select:
    case PropType::Status:
    case PropType::MultiSelect: {
        const Option *o = prop.option(key);
        return o ? o->name : key;
    }
    case PropType::Relation: {
        const DbLookup *l = dbLookup();
        const PageMeta *m = l ? l->lookupPage(key) : nullptr;
        return m ? (m->title.trimmed().isEmpty() ? tr("Untitled") : m->title) : key;
    }
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy: {
        const DbLookup *l = dbLookup();
        return l ? l->lookupPersonName(key) : key;
    }
    default:
        break;
    }
    if (t == PropType::Checkbox)
        return key == QLatin1String("true") ? tr("Checked") : tr("Unchecked");
    if (t == PropType::Date || t == PropType::Created || t == PropType::Edited) {
        const QLocale en(QLocale::English);
        static const QHash<QString, const char *> rel = {
            {QStringLiteral("r0past30"), QT_TRANSLATE_NOOP("Schema", "Past 30 days")},
            {QStringLiteral("r1past7"), QT_TRANSLATE_NOOP("Schema", "Past 7 days")},
            {QStringLiteral("r2yesterday"), QT_TRANSLATE_NOOP("Schema", "Yesterday")},
            {QStringLiteral("r3today"), QT_TRANSLATE_NOOP("Schema", "Today")},
            {QStringLiteral("r4tomorrow"), QT_TRANSLATE_NOOP("Schema", "Tomorrow")},
            {QStringLiteral("r5next7"), QT_TRANSLATE_NOOP("Schema", "Next 7 days")},
            {QStringLiteral("r6next30"), QT_TRANSLATE_NOOP("Schema", "Next 30 days")},
        };
        if (const char *r = rel.value(key, nullptr))
            return tr(r);
        if (key.startsWith(QLatin1Char('m')))
            return en.toString(QDate::fromString(key.mid(1) + QStringLiteral("-01"), Qt::ISODate), QStringLiteral("MMMM yyyy"));
        if (key.startsWith(QLatin1Char('w'))) {
            const QDate monday = QDate::fromString(key.mid(1), Qt::ISODate);
            return tr("Week of %1").arg(en.toString(monday, QStringLiteral("MMM d, yyyy")));
        }
        if (key.size() == 7)
            return en.toString(QDate::fromString(key + QStringLiteral("-01"), Qt::ISODate), QStringLiteral("MMMM yyyy"));
        if (key.size() == 4)
            return key;
        return en.toString(QDate::fromString(key, Qt::ISODate), QStringLiteral("MMM d, yyyy"));
    }
    if (t == PropType::Number && key.startsWith(QLatin1Char('n'))) {
        const double lo = key.mid(1).toDouble();
        return QStringLiteral("%1 – %2").arg(formatNumber(lo, prop.numberFormat), formatNumber(lo + step, prop.numberFormat));
    }
    return key;
}

// Sort key for non-option groups (dates chronological, numbers numeric).
QString groupSortKey(const Property &prop, const QString &key)
{
    const PropType t = resultType(prop);
    if (t == PropType::Number && key.startsWith(QLatin1Char('n'))) {
        const double v = key.mid(1).toDouble();
        // Fixed-width, order-preserving text for doubles.
        return QString::asprintf("%024.6f", v + 1e15);
    }
    if (t == PropType::Date || t == PropType::Created || t == PropType::Edited) {
        if (key.startsWith(QLatin1Char('r')))
            return QStringLiteral("1") + key; // relative buckets after older months
        if (key.startsWith(QLatin1Char('m')))
            return (QDate::fromString(key.mid(1) + QStringLiteral("-01"), Qt::ISODate) < QDate::currentDate() ? QStringLiteral("0")
                                                                                                             : QStringLiteral("2"))
                 + key.mid(1);
        if (key.startsWith(QLatin1Char('w')))
            return key.mid(1);
        return key;
    }
    if (t == PropType::Checkbox)
        return key == QLatin1String("true") ? QStringLiteral("0") : QStringLiteral("1");
    return key.toLower();
}
} // namespace

QList<RowGroup> groupRows(const Schema &schema, const Property &prop, const QList<RowRef> &rows, const QString &mode,
                          double step, bool hideEmpty)
{
    Q_UNUSED(schema)
    QList<RowGroup> groups;
    QHash<QString, int> index;
    auto add = [&](const QString &key) -> RowGroup & {
        auto it = index.constFind(key);
        if (it != index.constEnd())
            return groups[*it];
        RowGroup g;
        g.key = key;
        g.label = groupLabel(prop, key, step);
        if (const Option *o = prop.option(key)) {
            g.shade = o->shade;
            if (prop.type == PropType::Status)
                g.statusGroup = o->group;
        }
        index.insert(key, int(groups.size()));
        groups.append(g);
        return groups.last();
    };
    const bool optionGroups = propTypeHasOptions(prop.type);
    const bool checkbox = resultType(prop) == PropType::Checkbox;
    // Option groups exist even when empty (board columns); "No …" first.
    if (optionGroups) {
        add(QString());
        for (const Option &o : prop.options)
            add(o.id);
    } else if (checkbox) {
        add(QStringLiteral("true"));
        add(QStringLiteral("false"));
    }
    for (int i = 0; i < rows.size(); ++i) {
        const QStringList keys = groupKeysFor(prop, rowValue(prop, rows[i]), mode, step);
        for (const QString &k : keys)
            add(k).rows.append(i);
    }
    if (!optionGroups && !checkbox) {
        // "No value" last for computed groups, the rest in natural order.
        std::stable_sort(groups.begin(), groups.end(), [&](const RowGroup &a, const RowGroup &b) {
            if (a.key.isEmpty() != b.key.isEmpty())
                return b.key.isEmpty();
            return groupSortKey(prop, a.key) < groupSortKey(prop, b.key);
        });
    }
    if (optionGroups && !groups.isEmpty() && groups.first().key.isEmpty() && groups.first().rows.isEmpty())
        groups.removeFirst(); // an empty "No …" group only clutters
    if (hideEmpty)
        groups.removeIf([](const RowGroup &g) { return g.rows.isEmpty(); });
    return groups;
}

// ---- sub-items -------------------------------------------------------------------

QList<NestedRow> nestRows(const Schema &schema, const QList<RowRef> &rows, const QSet<QString> &collapsed,
                          const QString &mode)
{
    QList<NestedRow> out;
    const Property *parentProp = schema.prop(schema.subItemsProp);
    if (!parentProp || mode == QLatin1String("off") || mode == QLatin1String("flat")) {
        for (int i = 0; i < rows.size(); ++i)
            out.append({i, 0, 0});
        return out;
    }
    QHash<QString, int> byId;
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i].meta)
            byId.insert(rows[i].meta->id, i);
    QHash<int, QList<int>> children;
    QList<int> roots;
    for (int i = 0; i < rows.size(); ++i) {
        const QStringList parents = relationIds(rowValue(*parentProp, rows[i]));
        const int parent = parents.isEmpty() ? -1 : byId.value(parents.first(), -1);
        if (parent >= 0 && parent != i)
            children[parent].append(i);
        else if (mode == QLatin1String("parents") && !parents.isEmpty())
            continue; // a sub-item whose parent is filtered out
        else
            roots.append(i);
    }
    QSet<int> visited;
    std::function<void(int)> hide = [&](int i) {
        for (int k : children.value(i))
            if (!visited.contains(k)) {
                visited.insert(k);
                hide(k);
            }
    };
    std::function<void(int, int)> walk = [&](int i, int depth) {
        if (visited.contains(i))
            return;
        visited.insert(i);
        const QList<int> kids = children.value(i);
        out.append({i, depth, int(kids.size())});
        if (mode == QLatin1String("parents") || (rows[i].meta && collapsed.contains(rows[i].meta->id))) {
            hide(i);
            return;
        }
        for (int k : kids)
            walk(k, depth + 1);
    };
    for (int r : roots)
        walk(r, 0);
    // Cycles (A parent of B parent of A): show what's left at the top.
    for (int i = 0; i < rows.size(); ++i)
        if (!visited.contains(i) && !(mode == QLatin1String("parents")))
            walk(i, 0);
    return out;
}
