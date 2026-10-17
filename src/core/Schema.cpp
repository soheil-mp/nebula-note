#include "Schema.h"

#include "Ids.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QLocale>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
struct PropInfo {
    PropType type;
    const char *key;
    const char *name;
    const char *icon;
};

const PropInfo kProps[] = {
    {PropType::Title, "title", QT_TRANSLATE_NOOP("Schema", "Title"), "title"},
    {PropType::Text, "text", QT_TRANSLATE_NOOP("Schema", "Text"), "text"},
    {PropType::Number, "number", QT_TRANSLATE_NOOP("Schema", "Number"), "number"},
    {PropType::Select, "select", QT_TRANSLATE_NOOP("Schema", "Select"), "select"},
    {PropType::MultiSelect, "multi", QT_TRANSLATE_NOOP("Schema", "Multi-select"), "multi"},
    {PropType::Status, "status", QT_TRANSLATE_NOOP("Schema", "Status"), "status"},
    {PropType::Date, "date", QT_TRANSLATE_NOOP("Schema", "Date"), "calendar"},
    {PropType::Checkbox, "checkbox", QT_TRANSLATE_NOOP("Schema", "Checkbox"), "checkbox"},
    {PropType::Url, "url", QT_TRANSLATE_NOOP("Schema", "URL"), "link"},
    {PropType::Email, "email", QT_TRANSLATE_NOOP("Schema", "Email"), "at"},
    {PropType::Phone, "phone", QT_TRANSLATE_NOOP("Schema", "Phone"), "phone"},
    {PropType::Created, "created", QT_TRANSLATE_NOOP("Schema", "Created time"), "clock"},
    {PropType::Edited, "edited", QT_TRANSLATE_NOOP("Schema", "Last edited time"), "clock"},
    {PropType::Formula, "formula", QT_TRANSLATE_NOOP("Schema", "Formula"), "formula"},
    {PropType::Relation, "relation", QT_TRANSLATE_NOOP("Schema", "Relation"), "relation"},
    {PropType::Rollup, "rollup", QT_TRANSLATE_NOOP("Schema", "Rollup"), "rollup"},
    {PropType::Files, "files", QT_TRANSLATE_NOOP("Schema", "Files & media"), "paperclip"},
    {PropType::Person, "person", QT_TRANSLATE_NOOP("Schema", "Person"), "person"},
    {PropType::CreatedBy, "createdby", QT_TRANSLATE_NOOP("Schema", "Created by"), "person"},
    {PropType::EditedBy, "editedby", QT_TRANSLATE_NOOP("Schema", "Last edited by"), "person"},
    {PropType::UniqueId, "uid", QT_TRANSLATE_NOOP("Schema", "ID"), "hash"},
    {PropType::Button, "button", QT_TRANSLATE_NOOP("Schema", "Button"), "cursor"},
};

struct ViewInfo {
    ViewType type;
    const char *key;
    const char *name;
    const char *icon;
};

const ViewInfo kViews[] = {
    {ViewType::Table, "table", QT_TRANSLATE_NOOP("Schema", "Table"), "table"},
    {ViewType::Board, "board", QT_TRANSLATE_NOOP("Schema", "Board"), "board"},
    {ViewType::List, "list", QT_TRANSLATE_NOOP("Schema", "List"), "list"},
    {ViewType::Gallery, "gallery", QT_TRANSLATE_NOOP("Schema", "Gallery"), "gallery"},
    {ViewType::Calendar, "calendar", QT_TRANSLATE_NOOP("Schema", "Calendar"), "calendar"},
    {ViewType::Timeline, "timeline", QT_TRANSLATE_NOOP("Schema", "Timeline"), "timeline"},
    {ViewType::Chart, "chart", QT_TRANSLATE_NOOP("Schema", "Chart"), "chart"},
    {ViewType::Feed, "feed", QT_TRANSLATE_NOOP("Schema", "Feed"), "feed"},
    {ViewType::Form, "form", QT_TRANSLATE_NOOP("Schema", "Form"), "form"},
};

QString tr(const char *s)
{
    return QCoreApplication::translate("Schema", s);
}
}

QString propTypeKey(PropType t)
{
    for (const auto &i : kProps)
        if (i.type == t)
            return QString::fromLatin1(i.key);
    return QStringLiteral("text");
}

PropType propTypeFromKey(const QString &key)
{
    for (const auto &i : kProps)
        if (key == QLatin1String(i.key))
            return i.type;
    return PropType::Text;
}

QString propTypeName(PropType t)
{
    for (const auto &i : kProps)
        if (i.type == t)
            return tr(i.name);
    return {};
}

QString propTypeIcon(PropType t)
{
    for (const auto &i : kProps)
        if (i.type == t)
            return QString::fromLatin1(i.icon);
    return QStringLiteral("text");
}

bool propTypeHasOptions(PropType t)
{
    return t == PropType::Select || t == PropType::MultiSelect || t == PropType::Status;
}

bool propTypeIsComputed(PropType t)
{
    return t == PropType::Created || t == PropType::Edited || t == PropType::Formula || t == PropType::Rollup
        || t == PropType::CreatedBy || t == PropType::EditedBy || t == PropType::UniqueId || t == PropType::Button;
}

QList<PropType> userPropTypes()
{
    return {PropType::Text,     PropType::Number,   PropType::Select,  PropType::MultiSelect, PropType::Status,
            PropType::Date,     PropType::Person,   PropType::Files,   PropType::Checkbox,    PropType::Url,
            PropType::Email,    PropType::Phone,    PropType::Formula, PropType::Relation,    PropType::Rollup,
            PropType::Created,  PropType::CreatedBy, PropType::Edited, PropType::EditedBy,    PropType::Button,
            PropType::UniqueId};
}

namespace
{
const DbLookup *g_lookup = nullptr;
}

void setDbLookup(const DbLookup *lookup)
{
    g_lookup = lookup;
}

const DbLookup *dbLookup()
{
    return g_lookup;
}

QString optionShadeName(int shade)
{
    static const char *names[] = {
        QT_TRANSLATE_NOOP("Schema", "Graphite"), QT_TRANSLATE_NOOP("Schema", "Ash"),
        QT_TRANSLATE_NOOP("Schema", "Smoke"),    QT_TRANSLATE_NOOP("Schema", "Dust"),
        QT_TRANSLATE_NOOP("Schema", "Fog"),      QT_TRANSLATE_NOOP("Schema", "Haze"),
        QT_TRANSLATE_NOOP("Schema", "Mist"),     QT_TRANSLATE_NOOP("Schema", "Moon"),
        QT_TRANSLATE_NOOP("Schema", "Starlight"), QT_TRANSLATE_NOOP("Schema", "Outline"),
    };
    return tr(names[qBound(0, shade, kOptionShadeCount - 1)]);
}

const Option *Property::option(const QString &optionId) const
{
    for (const Option &o : options)
        if (o.id == optionId)
            return &o;
    return nullptr;
}

const Option *Property::optionByName(const QString &name) const
{
    for (const Option &o : options)
        if (o.name.compare(name, Qt::CaseInsensitive) == 0)
            return &o;
    return nullptr;
}

QString viewTypeKey(ViewType t)
{
    for (const auto &i : kViews)
        if (i.type == t)
            return QString::fromLatin1(i.key);
    return QStringLiteral("table");
}

ViewType viewTypeFromKey(const QString &key)
{
    for (const auto &i : kViews)
        if (key == QLatin1String(i.key))
            return i.type;
    return ViewType::Table;
}

QString viewTypeName(ViewType t)
{
    for (const auto &i : kViews)
        if (i.type == t)
            return tr(i.name);
    return {};
}

QString viewTypeIcon(ViewType t)
{
    for (const auto &i : kViews)
        if (i.type == t)
            return QString::fromLatin1(i.icon);
    return QStringLiteral("table");
}

// ---- Schema ------------------------------------------------------------------

const Property *Schema::prop(const QString &id) const
{
    for (const Property &p : props)
        if (p.id == id)
            return &p;
    return nullptr;
}

Property *Schema::prop(const QString &id)
{
    for (Property &p : props)
        if (p.id == id)
            return &p;
    return nullptr;
}

const Property *Schema::titleProp() const
{
    for (const Property &p : props)
        if (p.type == PropType::Title)
            return &p;
    return nullptr;
}

const View *Schema::view(const QString &id) const
{
    for (const View &v : views)
        if (v.id == id)
            return &v;
    return nullptr;
}

View *Schema::view(const QString &id)
{
    for (View &v : views)
        if (v.id == id)
            return &v;
    return nullptr;
}

int Schema::propIndex(const QString &id) const
{
    for (int i = 0; i < props.size(); ++i)
        if (props[i].id == id)
            return i;
    return -1;
}

QList<const Property *> Schema::orderedProps(const View &v, bool includeHidden) const
{
    QList<const Property *> out;
    QSet<QString> seen;
    auto add = [&](const Property *p) {
        if (!p || seen.contains(p->id))
            return;
        seen.insert(p->id);
        if (!includeHidden && p->type != PropType::Title && v.hidden.contains(p->id))
            return;
        out.append(p);
    };
    add(titleProp());
    for (const QString &id : v.order)
        add(prop(id));
    for (const Property &p : props)
        add(&p);
    return out;
}

QString Schema::newPropId() const
{
    QString id;
    do
        id = Ids::newShortId(4);
    while (prop(id));
    return id;
}

QString Schema::newViewId() const
{
    QString id;
    do
        id = Ids::newShortId(4);
    while (view(id));
    return id;
}

QString Schema::newOptionId(const Property &p)
{
    QString id;
    do
        id = Ids::newShortId(4);
    while (p.option(id));
    return id;
}

QString Schema::defaultGroupProp() const
{
    for (const Property &p : props)
        if (p.type == PropType::Status)
            return p.id;
    for (const Property &p : props)
        if (p.type == PropType::Select)
            return p.id;
    return {};
}

QString Schema::defaultDateProp() const
{
    for (const Property &p : props)
        if (p.type == PropType::Date)
            return p.id;
    for (const Property &p : props)
        if (p.type == PropType::Created)
            return p.id;
    return {};
}

namespace
{
QJsonArray filtersToJson(const QList<FilterRule> &rules)
{
    QJsonArray fs;
    for (const FilterRule &f : rules) {
        if (f.group) {
            QJsonObject g{{QStringLiteral("group"), filtersToJson(f.rules)}};
            if (f.any)
                g[QStringLiteral("any")] = true;
            fs.append(g);
        } else {
            fs.append(QJsonObject{{QStringLiteral("prop"), f.prop}, {QStringLiteral("op"), f.op}, {QStringLiteral("value"), f.value}});
        }
    }
    return fs;
}

QList<FilterRule> filtersFromJson(const QJsonArray &arr, int depth = 0)
{
    QList<FilterRule> out;
    for (const QJsonValue &fv : arr) {
        const QJsonObject fo = fv.toObject();
        FilterRule r;
        if (fo.contains(QStringLiteral("group"))) {
            if (depth >= 3)
                continue;
            r.group = true;
            r.any = fo.value(QStringLiteral("any")).toBool();
            r.rules = filtersFromJson(fo.value(QStringLiteral("group")).toArray(), depth + 1);
        } else {
            r.prop = fo.value(QStringLiteral("prop")).toString();
            r.op = fo.value(QStringLiteral("op")).toString();
            r.value = fo.value(QStringLiteral("value"));
        }
        out.append(r);
    }
    return out;
}
}

QJsonObject Schema::toJson() const
{
    QJsonArray propsArr;
    for (const Property &p : props) {
        QJsonObject o;
        o[QStringLiteral("id")] = p.id;
        o[QStringLiteral("name")] = p.name;
        o[QStringLiteral("type")] = propTypeKey(p.type);
        if (p.numberFormat != QLatin1String("number"))
            o[QStringLiteral("format")] = p.numberFormat;
        if (p.numberShow != QLatin1String("number")) {
            o[QStringLiteral("show")] = p.numberShow;
            o[QStringLiteral("divide")] = p.numberDivide;
        }
        if (p.decimals >= 0)
            o[QStringLiteral("decimals")] = p.decimals;
        if (p.dateFormat != QLatin1String("default"))
            o[QStringLiteral("dateFormat")] = p.dateFormat;
        if (p.time24)
            o[QStringLiteral("time24")] = true;
        if (!p.description.isEmpty())
            o[QStringLiteral("desc")] = p.description;
        if (p.type == PropType::Formula) {
            o[QStringLiteral("formula")] = p.formula;
            o[QStringLiteral("ftype")] = p.formulaType;
        }
        if (p.type == PropType::Relation) {
            o[QStringLiteral("db")] = p.relationDb;
            if (!p.relationPair.isEmpty())
                o[QStringLiteral("pair")] = p.relationPair;
            if (p.relationOne)
                o[QStringLiteral("one")] = true;
        }
        if (p.type == PropType::Rollup) {
            o[QStringLiteral("rel")] = p.rollupRelation;
            o[QStringLiteral("target")] = p.rollupProp;
            o[QStringLiteral("fn")] = p.rollupFn;
        }
        if (p.type == PropType::UniqueId && !p.idPrefix.isEmpty())
            o[QStringLiteral("prefix")] = p.idPrefix;
        if (p.type == PropType::Button && !p.button.isEmpty())
            o[QStringLiteral("button")] = p.button;
        if (!p.options.isEmpty()) {
            QJsonArray opts;
            for (const Option &op : p.options) {
                QJsonObject oo;
                oo[QStringLiteral("id")] = op.id;
                oo[QStringLiteral("name")] = op.name;
                oo[QStringLiteral("shade")] = op.shade;
                if (p.type == PropType::Status)
                    oo[QStringLiteral("group")] = op.group;
                opts.append(oo);
            }
            o[QStringLiteral("options")] = opts;
        }
        propsArr.append(o);
    }
    QJsonArray viewsArr;
    for (const View &v : views) {
        QJsonObject o;
        o[QStringLiteral("id")] = v.id;
        o[QStringLiteral("name")] = v.name;
        o[QStringLiteral("type")] = viewTypeKey(v.type);
        if (!v.filters.isEmpty())
            o[QStringLiteral("filters")] = filtersToJson(v.filters);
        if (v.filterAny)
            o[QStringLiteral("any")] = true;
        if (!v.sorts.isEmpty()) {
            QJsonArray ss;
            for (const SortRule &s : v.sorts)
                ss.append(QJsonObject{{QStringLiteral("prop"), s.prop}, {QStringLiteral("desc"), s.descending}});
            o[QStringLiteral("sorts")] = ss;
        }
        if (!v.groupBy.isEmpty())
            o[QStringLiteral("groupBy")] = v.groupBy;
        if (!v.dateProp.isEmpty())
            o[QStringLiteral("dateProp")] = v.dateProp;
        if (!v.hidden.isEmpty())
            o[QStringLiteral("hidden")] = QJsonArray::fromStringList(v.hidden);
        if (!v.order.isEmpty())
            o[QStringLiteral("order")] = QJsonArray::fromStringList(v.order);
        if (!v.widths.isEmpty()) {
            QJsonObject w;
            for (auto it = v.widths.begin(); it != v.widths.end(); ++it)
                w[it.key()] = it.value();
            o[QStringLiteral("widths")] = w;
        }
        o[QStringLiteral("preview")] = v.cardPreview;
        o[QStringLiteral("size")] = v.cardSize;
        if (!v.collapsedGroups.isEmpty())
            o[QStringLiteral("collapsed")] = QJsonArray::fromStringList(v.collapsedGroups);
        const View d;
        auto put = [&](const char *key, const QJsonValue &val, bool isDefault) {
            if (!isDefault)
                o[QLatin1String(key)] = val;
        };
        put("groupMode", v.groupMode, v.groupMode.isEmpty());
        put("groupStep", v.groupStep, v.groupStep == d.groupStep);
        put("subGroupBy", v.subGroupBy, v.subGroupBy.isEmpty());
        put("hideEmpty", v.hideEmptyGroups, !v.hideEmptyGroups);
        put("hiddenGroups", QJsonArray::fromStringList(v.hiddenGroups), v.hiddenGroups.isEmpty());
        if (!v.calcs.isEmpty()) {
            QJsonObject c;
            for (auto it = v.calcs.begin(); it != v.calcs.end(); ++it)
                c[it.key()] = it.value();
            o[QStringLiteral("calcs")] = c;
        }
        put("wrap", v.wrapCells, !v.wrapCells);
        put("icons", v.showIcons, v.showIcons);
        put("openIn", v.openIn, v.openIn == d.openIn);
        put("fit", v.cardFit, !v.cardFit);
        put("colorColumns", v.colorColumns, v.colorColumns);
        put("weekends", v.showWeekends, v.showWeekends);
        put("limit", v.loadLimit, v.loadLimit == d.loadLimit);
        put("frozen", v.frozen, v.frozen == 0);
        put("pageIcon", v.showPageIcon, v.showPageIcon);
        put("subItems", v.subItems, v.subItems == d.subItems);
        put("endDate", v.endDateProp, v.endDateProp.isEmpty());
        put("scale", v.timelineScale, v.timelineScale == d.timelineScale);
        put("tlTable", v.timelineTable, v.timelineTable);
        put("tlProps", QJsonArray::fromStringList(v.timelineTableProps), v.timelineTableProps.isEmpty());
        put("chart", v.chartType, v.chartType == d.chartType);
        put("chartX", v.chartX, v.chartX.isEmpty());
        put("chartY", v.chartY, v.chartY == d.chartY);
        put("chartGroup", v.chartGroup, v.chartGroup.isEmpty());
        put("chartSort", v.chartSort, v.chartSort == d.chartSort);
        put("chartCum", v.chartCumulative, !v.chartCumulative);
        put("chartLabels", v.chartLabels, v.chartLabels);
        put("chartHideEmpty", v.chartHideEmpty, !v.chartHideEmpty);
        put("formTitle", v.formTitle, v.formTitle.isEmpty());
        put("formDesc", v.formDescription, v.formDescription.isEmpty());
        put("formRequired", QJsonArray::fromStringList(v.formRequired), v.formRequired.isEmpty());
        viewsArr.append(o);
    }
    QJsonObject root{{QStringLiteral("props"), propsArr}, {QStringLiteral("views"), viewsArr}};
    if (locked)
        root[QStringLiteral("locked")] = true;
    if (!subItemsProp.isEmpty())
        root[QStringLiteral("subItems")] = subItemsProp;
    if (!dependencyProp.isEmpty())
        root[QStringLiteral("deps")] = dependencyProp;
    if (!defaultTemplate.isEmpty())
        root[QStringLiteral("template")] = defaultTemplate;
    return root;
}

namespace
{
QStringList toStringList(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray())
        out.append(x.toString());
    return out;
}
}

Schema Schema::fromJson(const QJsonObject &o)
{
    Schema s;
    for (const QJsonValue &pv : o.value(QStringLiteral("props")).toArray()) {
        const QJsonObject po = pv.toObject();
        Property p;
        p.id = po.value(QStringLiteral("id")).toString();
        p.name = po.value(QStringLiteral("name")).toString();
        p.type = propTypeFromKey(po.value(QStringLiteral("type")).toString());
        p.numberFormat = po.value(QStringLiteral("format")).toString(QStringLiteral("number"));
        p.numberShow = po.value(QStringLiteral("show")).toString(QStringLiteral("number"));
        p.numberDivide = po.value(QStringLiteral("divide")).toDouble(100);
        if (p.numberDivide <= 0)
            p.numberDivide = 100;
        p.decimals = po.value(QStringLiteral("decimals")).toInt(-1);
        p.dateFormat = po.value(QStringLiteral("dateFormat")).toString(QStringLiteral("default"));
        p.time24 = po.value(QStringLiteral("time24")).toBool();
        p.description = po.value(QStringLiteral("desc")).toString();
        p.formula = po.value(QStringLiteral("formula")).toString();
        p.formulaType = po.value(QStringLiteral("ftype")).toString(QStringLiteral("text"));
        p.relationDb = po.value(QStringLiteral("db")).toString();
        p.relationPair = po.value(QStringLiteral("pair")).toString();
        p.relationOne = po.value(QStringLiteral("one")).toBool();
        p.rollupRelation = po.value(QStringLiteral("rel")).toString();
        p.rollupProp = po.value(QStringLiteral("target")).toString();
        p.rollupFn = po.value(QStringLiteral("fn")).toString(QStringLiteral("original"));
        p.idPrefix = po.value(QStringLiteral("prefix")).toString();
        p.button = po.value(QStringLiteral("button")).toObject();
        for (const QJsonValue &ov : po.value(QStringLiteral("options")).toArray()) {
            const QJsonObject oo = ov.toObject();
            Option op;
            op.id = oo.value(QStringLiteral("id")).toString();
            op.name = oo.value(QStringLiteral("name")).toString();
            op.shade = qBound(0, oo.value(QStringLiteral("shade")).toInt(), kOptionShadeCount - 1);
            op.group = qBound(0, oo.value(QStringLiteral("group")).toInt(), 2);
            p.options.append(op);
        }
        if (!p.id.isEmpty())
            s.props.append(p);
    }
    // Every database has exactly one title property, first.
    if (!s.titleProp()) {
        Property t;
        t.id = QStringLiteral("title");
        t.name = tr("Name");
        t.type = PropType::Title;
        s.props.prepend(t);
    }
    for (const QJsonValue &vv : o.value(QStringLiteral("views")).toArray()) {
        const QJsonObject vo = vv.toObject();
        View v;
        v.id = vo.value(QStringLiteral("id")).toString();
        v.name = vo.value(QStringLiteral("name")).toString();
        v.type = viewTypeFromKey(vo.value(QStringLiteral("type")).toString());
        v.filters = filtersFromJson(vo.value(QStringLiteral("filters")).toArray());
        v.filterAny = vo.value(QStringLiteral("any")).toBool();
        for (const QJsonValue &sv : vo.value(QStringLiteral("sorts")).toArray()) {
            const QJsonObject so = sv.toObject();
            v.sorts.append({so.value(QStringLiteral("prop")).toString(), so.value(QStringLiteral("desc")).toBool()});
        }
        v.groupBy = vo.value(QStringLiteral("groupBy")).toString();
        v.dateProp = vo.value(QStringLiteral("dateProp")).toString();
        v.hidden = toStringList(vo.value(QStringLiteral("hidden")));
        v.order = toStringList(vo.value(QStringLiteral("order")));
        const QJsonObject w = vo.value(QStringLiteral("widths")).toObject();
        for (auto it = w.begin(); it != w.end(); ++it)
            v.widths.insert(it.key(), it.value().toInt());
        v.cardPreview = vo.value(QStringLiteral("preview")).toString(QStringLiteral("cover"));
        v.cardSize = vo.value(QStringLiteral("size")).toString(QStringLiteral("medium"));
        v.collapsedGroups = toStringList(vo.value(QStringLiteral("collapsed")));
        const View d;
        v.groupMode = vo.value(QStringLiteral("groupMode")).toString();
        v.groupStep = vo.value(QStringLiteral("groupStep")).toDouble(d.groupStep);
        if (v.groupStep <= 0)
            v.groupStep = d.groupStep;
        v.subGroupBy = vo.value(QStringLiteral("subGroupBy")).toString();
        v.hideEmptyGroups = vo.value(QStringLiteral("hideEmpty")).toBool();
        v.hiddenGroups = toStringList(vo.value(QStringLiteral("hiddenGroups")));
        const QJsonObject calcs = vo.value(QStringLiteral("calcs")).toObject();
        for (auto it = calcs.begin(); it != calcs.end(); ++it)
            v.calcs.insert(it.key(), it.value().toString());
        v.wrapCells = vo.value(QStringLiteral("wrap")).toBool();
        v.showIcons = vo.value(QStringLiteral("icons")).toBool(true);
        v.openIn = vo.value(QStringLiteral("openIn")).toString(d.openIn);
        v.cardFit = vo.value(QStringLiteral("fit")).toBool();
        v.colorColumns = vo.value(QStringLiteral("colorColumns")).toBool(true);
        v.showWeekends = vo.value(QStringLiteral("weekends")).toBool(true);
        v.loadLimit = qMax(10, vo.value(QStringLiteral("limit")).toInt(d.loadLimit));
        v.frozen = qMax(0, vo.value(QStringLiteral("frozen")).toInt());
        v.showPageIcon = vo.value(QStringLiteral("pageIcon")).toBool(true);
        v.subItems = vo.value(QStringLiteral("subItems")).toString(d.subItems);
        v.endDateProp = vo.value(QStringLiteral("endDate")).toString();
        v.timelineScale = vo.value(QStringLiteral("scale")).toString(d.timelineScale);
        v.timelineTable = vo.value(QStringLiteral("tlTable")).toBool(true);
        v.timelineTableProps = toStringList(vo.value(QStringLiteral("tlProps")));
        v.chartType = vo.value(QStringLiteral("chart")).toString(d.chartType);
        v.chartX = vo.value(QStringLiteral("chartX")).toString();
        v.chartY = vo.value(QStringLiteral("chartY")).toString(d.chartY);
        v.chartGroup = vo.value(QStringLiteral("chartGroup")).toString();
        v.chartSort = vo.value(QStringLiteral("chartSort")).toString(d.chartSort);
        v.chartCumulative = vo.value(QStringLiteral("chartCum")).toBool();
        v.chartLabels = vo.value(QStringLiteral("chartLabels")).toBool(true);
        v.chartHideEmpty = vo.value(QStringLiteral("chartHideEmpty")).toBool();
        v.formTitle = vo.value(QStringLiteral("formTitle")).toString();
        v.formDescription = vo.value(QStringLiteral("formDesc")).toString();
        v.formRequired = toStringList(vo.value(QStringLiteral("formRequired")));
        if (!v.id.isEmpty())
            s.views.append(v);
    }
    s.locked = o.value(QStringLiteral("locked")).toBool();
    s.subItemsProp = o.value(QStringLiteral("subItems")).toString();
    s.dependencyProp = o.value(QStringLiteral("deps")).toString();
    s.defaultTemplate = o.value(QStringLiteral("template")).toString();
    if (s.views.isEmpty())
        s.views.append(makeView(s, ViewType::Table));
    return s;
}

Schema Schema::basic()
{
    Schema s;
    Property title;
    title.id = QStringLiteral("title");
    title.name = tr("Name");
    title.type = PropType::Title;
    s.props.append(title);
    Property tags;
    tags.id = QStringLiteral("tags");
    tags.name = tr("Tags");
    tags.type = PropType::MultiSelect;
    s.props.append(tags);
    s.views.append(makeView(s, ViewType::Table));
    return s;
}

View Schema::makeView(const Schema &schema, ViewType type, const QString &name)
{
    View v;
    v.id = schema.newViewId();
    v.type = type;
    v.name = name.isEmpty() ? viewTypeName(type) : name;
    if (type == ViewType::Board)
        v.groupBy = schema.defaultGroupProp();
    if (type == ViewType::Calendar)
        v.dateProp = schema.defaultDateProp();
    if (type == ViewType::Gallery || type == ViewType::Board) {
        // Cards stay calm: show select-like properties and dates, hide long text.
        for (const Property &p : schema.props)
            if (p.type == PropType::Text || p.type == PropType::Url || p.type == PropType::Email
                || p.type == PropType::Phone || propTypeIsComputed(p.type) || p.id == v.groupBy)
                v.hidden.append(p.id);
    }
    if (type == ViewType::List || type == ViewType::Calendar || type == ViewType::Timeline || type == ViewType::Feed) {
        for (const Property &p : schema.props)
            if (p.type != PropType::Title && p.type != PropType::Select && p.type != PropType::Status
                && p.type != PropType::Date && p.type != PropType::Person)
                v.hidden.append(p.id);
    }
    if (type == ViewType::Timeline)
        v.dateProp = schema.defaultDateProp();
    if (type == ViewType::Chart) {
        v.chartX = schema.defaultGroupProp();
        if (v.chartX.isEmpty())
            for (const Property &p : schema.props)
                if (canGroupBy(p) && p.type != PropType::Title) {
                    v.chartX = p.id;
                    break;
                }
    }
    if (type == ViewType::Form) {
        v.formTitle = name.isEmpty() ? QString() : name;
        for (const Property &p : schema.props)
            if (propTypeIsComputed(p.type))
                v.hidden.append(p.id);
    }
    return v;
}

// ---- values ------------------------------------------------------------------

// rowValue for Formula and Rollup lives in DbCompute.cpp.
QJsonValue computedValue(const Property &p, const RowRef &row);

QJsonValue rowValue(const Property &p, const RowRef &row)
{
    switch (p.type) {
    case PropType::Title:
        return row.meta ? QJsonValue(row.meta->title) : QJsonValue();
    case PropType::Created:
        return row.meta ? QJsonValue(QDateTime::fromMSecsSinceEpoch(row.meta->created).toString(QStringLiteral("yyyy-MM-ddTHH:mm")))
                        : QJsonValue();
    case PropType::Edited:
        return row.meta ? QJsonValue(QDateTime::fromMSecsSinceEpoch(row.meta->updated).toString(QStringLiteral("yyyy-MM-ddTHH:mm")))
                        : QJsonValue();
    case PropType::CreatedBy:
    case PropType::EditedBy: {
        // A single-user workspace: whoever is "me" made every page.
        const QString me = g_lookup ? g_lookup->lookupMe() : QStringLiteral("me");
        return QJsonArray{me};
    }
    case PropType::Formula:
    case PropType::Rollup:
        return computedValue(p, row);
    case PropType::Button:
        return QJsonValue();
    case PropType::Relation: {
        const QJsonValue v = row.props.value(p.id);
        if (!g_lookup)
            return v;
        // Pages in the trash drop out of relations until restored.
        QJsonArray live;
        for (const QJsonValue &x : v.toArray()) {
            const PageMeta *m = g_lookup->lookupPage(x.toString());
            if (m && !m->isTrashed())
                live.append(x);
        }
        return live.isEmpty() ? QJsonValue() : QJsonValue(live);
    }
    default:
        return row.props.value(p.id);
    }
}

QString formatNumber(double value, const QString &format, int decimals)
{
    const QLocale c(QLocale::English);
    auto trimmed = [](QString s) {
        if (s.contains(QLatin1Char('.'))) {
            while (s.endsWith(QLatin1Char('0')))
                s.chop(1);
            if (s.endsWith(QLatin1Char('.')))
                s.chop(1);
        }
        return s;
    };
    auto plain = [&](double v, int maxDecimals) {
        if (decimals >= 0)
            return c.toString(v, 'f', decimals);
        return trimmed(c.toString(v, 'f', maxDecimals));
    };
    auto money = [&](const char *sym, double v, int d) {
        return QString::fromUtf8(sym) + c.toString(v, 'f', decimals >= 0 ? decimals : d);
    };
    if (format == QLatin1String("percent"))
        return plain(value * 100.0, 2) + QLatin1Char('%');
    if (format == QLatin1String("comma"))
        return plain(value, 4);
    if (format == QLatin1String("dollar"))
        return money("$", value, 2);
    if (format == QLatin1String("euro"))
        return money("€", value, 2);
    if (format == QLatin1String("pound"))
        return money("£", value, 2);
    if (format == QLatin1String("yen"))
        return money("¥", value, 0);
    if (format == QLatin1String("rupee"))
        return money("₹", value, 2);
    if (format == QLatin1String("franc"))
        return QStringLiteral("CHF ") + c.toString(value, 'f', decimals >= 0 ? decimals : 2);
    // "number": no thousands separators
    QString s = decimals >= 0 ? QString::number(value, 'f', decimals) : trimmed(QString::number(value, 'f', 6));
    if (s == QLatin1String("-0"))
        s = QStringLiteral("0");
    return s;
}

QDateTime dateTimeOf(const QJsonValue &v)
{
    const QString s = v.toString();
    if (s.isEmpty())
        return {};
    if (s.size() > 10 && s[10] == QLatin1Char('T')) {
        QDateTime dt = QDateTime::fromString(s.left(16), QStringLiteral("yyyy-MM-ddTHH:mm"));
        if (dt.isValid())
            return dt;
    }
    const QDate d = QDate::fromString(s.left(10), Qt::ISODate);
    return d.isValid() ? QDateTime(d, QTime(0, 0)) : QDateTime();
}

QDate dateOf(const QJsonValue &v)
{
    return dateTimeOf(v).date();
}

QDateTime dateEndOf(const QJsonValue &v)
{
    QString s = v.toString();
    const int slash = s.indexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    s = s.mid(slash + 1);
    const int hash = s.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        s = s.left(hash);
    return dateTimeOf(s);
}

bool dateHasReminder(const QJsonValue &v)
{
    return v.toString().endsWith(QLatin1String("#r"));
}

QString makeDateValue(const QDateTime &start, bool withTime, const QDateTime &end, bool remind)
{
    if (!start.isValid())
        return {};
    const QString f = withTime ? QStringLiteral("yyyy-MM-ddTHH:mm") : QStringLiteral("yyyy-MM-dd");
    QString s = start.toString(f);
    if (end.isValid() && end != start)
        s += QLatin1Char('/') + end.toString(f);
    if (remind)
        s += QStringLiteral("#r");
    return s;
}

QString formatDateValue(const QString &iso, bool longForm)
{
    const QDateTime dt = dateTimeOf(iso);
    if (!dt.isValid())
        return {};
    const QLocale en(QLocale::English);
    QString s = en.toString(dt.date(), longForm ? QStringLiteral("dddd, MMMM d, yyyy") : QStringLiteral("MMM d, yyyy"));
    if (iso.size() > 10 && iso[10] == QLatin1Char('T'))
        s += QLatin1Char(' ') + en.toString(dt.time(), QStringLiteral("h:mm AP"));
    const QDateTime end = dateEndOf(iso);
    if (end.isValid()) {
        s += QStringLiteral(" → ") + en.toString(end.date(), longForm ? QStringLiteral("dddd, MMMM d, yyyy") : QStringLiteral("MMM d, yyyy"));
        if (iso.contains(QLatin1Char('T')))
            s += QLatin1Char(' ') + en.toString(end.time(), QStringLiteral("h:mm AP"));
    }
    return s;
}

QString formatDateFor(const Property &p, const QString &value)
{
    const QDateTime start = dateTimeOf(value);
    if (!start.isValid())
        return {};
    const bool withTime = value.size() > 10 && value[10] == QLatin1Char('T');
    const QLocale en(QLocale::English);
    auto one = [&](const QDateTime &d) {
        QString s;
        const QString &f = p.dateFormat;
        if (f == QLatin1String("full"))
            s = en.toString(d.date(), QStringLiteral("MMMM d, yyyy"));
        else if (f == QLatin1String("mdy"))
            s = d.date().toString(QStringLiteral("MM/dd/yyyy"));
        else if (f == QLatin1String("dmy"))
            s = d.date().toString(QStringLiteral("dd/MM/yyyy"));
        else if (f == QLatin1String("ymd"))
            s = d.date().toString(QStringLiteral("yyyy/MM/dd"));
        else if (f == QLatin1String("relative")) {
            const qint64 days = QDate::currentDate().daysTo(d.date());
            if (days == 0)
                s = QCoreApplication::translate("Schema", "Today");
            else if (days == 1)
                s = QCoreApplication::translate("Schema", "Tomorrow");
            else if (days == -1)
                s = QCoreApplication::translate("Schema", "Yesterday");
            else if (days > 1 && days < 7)
                s = en.dayName(d.date().dayOfWeek());
            else if (days < -1 && days > -7)
                s = QCoreApplication::translate("Schema", "Last %1").arg(en.dayName(d.date().dayOfWeek()));
            else
                s = en.toString(d.date(), QStringLiteral("MMM d, yyyy"));
        } else
            s = en.toString(d.date(), QStringLiteral("MMM d, yyyy"));
        if (withTime)
            s += QLatin1Char(' ') + en.toString(d.time(), p.time24 ? QStringLiteral("HH:mm") : QStringLiteral("h:mm AP"));
        return s;
    };
    QString s = one(start);
    const QDateTime end = dateEndOf(value);
    if (end.isValid())
        s += QStringLiteral(" → ") + one(end);
    return s;
}

QStringList relationIds(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &x : v.toArray())
        if (!x.toString().isEmpty())
            out.append(x.toString());
    return out;
}

QList<FileRef> filesOf(const QJsonValue &v)
{
    QList<FileRef> out;
    for (const QJsonValue &x : v.toArray()) {
        if (x.isString()) {
            const QString s = x.toString();
            const bool url = s.contains(QStringLiteral("://"));
            out.append({s.section(QLatin1Char('/'), -1), url ? QString() : s, url ? s : QString()});
            continue;
        }
        const QJsonObject o = x.toObject();
        FileRef f{o.value(QStringLiteral("n")).toString(), o.value(QStringLiteral("m")).toString(), o.value(QStringLiteral("u")).toString()};
        if (f.name.isEmpty())
            f.name = (f.url.isEmpty() ? f.media : f.url).section(QLatin1Char('/'), -1);
        if (!f.media.isEmpty() || !f.url.isEmpty())
            out.append(f);
    }
    return out;
}

PropType resultType(const Property &p)
{
    switch (p.type) {
    case PropType::Formula:
        if (p.formulaType == QLatin1String("number"))
            return PropType::Number;
        if (p.formulaType == QLatin1String("bool"))
            return PropType::Checkbox;
        if (p.formulaType == QLatin1String("date"))
            return PropType::Date;
        return PropType::Text;
    case PropType::Rollup: {
        const QString &f = p.rollupFn;
        if (f == QLatin1String("earliest") || f == QLatin1String("latest"))
            return PropType::Date;
        if (f == QLatin1String("original") || f == QLatin1String("showunique") || f == QLatin1String("daterange"))
            return PropType::Text;
        return PropType::Number;
    }
    case PropType::UniqueId:
        return PropType::Number;
    case PropType::CreatedBy:
    case PropType::EditedBy:
        return PropType::Person;
    default:
        return p.type;
    }
}

namespace
{
// A stand-in property of the type a computed property produces.
Property asResult(const Property &p)
{
    Property q = p;
    q.type = resultType(p);
    if (p.type == PropType::Rollup && p.rollupFn.startsWith(QLatin1String("pct")))
        q.numberFormat = QStringLiteral("percent");
    return q;
}

bool isComputedResult(const Property &p)
{
    return p.type == PropType::Formula || p.type == PropType::Rollup || p.type == PropType::UniqueId;
}

QString titleOfPage(const QString &id)
{
    if (!g_lookup)
        return id;
    const PageMeta *m = g_lookup->lookupPage(id);
    if (!m)
        return {};
    return m->title.trimmed().isEmpty() ? QCoreApplication::translate("Schema", "Untitled") : m->title;
}
}

QString displayValue(const Property &p, const QJsonValue &v)
{
    switch (p.type) {
    case PropType::Number:
        return v.isDouble() ? formatNumber(v.toDouble(), p.numberFormat, p.decimals) : QString();
    case PropType::Select:
    case PropType::Status: {
        const Option *o = p.option(v.toString());
        return o ? o->name : QString();
    }
    case PropType::MultiSelect: {
        QStringList names;
        for (const QJsonValue &x : v.toArray())
            if (const Option *o = p.option(x.toString()))
                names.append(o->name);
        return names.join(QStringLiteral(", "));
    }
    case PropType::Date:
        return formatDateFor(p, v.toString());
    case PropType::Created:
    case PropType::Edited:
        return formatDateFor(p, v.toString());
    case PropType::Checkbox:
        return v.toBool() ? QStringLiteral("Yes") : QStringLiteral("No");
    case PropType::Relation: {
        QStringList names;
        for (const QString &id : relationIds(v))
            names.append(titleOfPage(id));
        return names.join(QStringLiteral(", "));
    }
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy: {
        QStringList names;
        for (const QJsonValue &x : v.toArray())
            names.append(g_lookup ? g_lookup->lookupPersonName(x.toString()) : x.toString());
        return names.join(QStringLiteral(", "));
    }
    case PropType::Files: {
        QStringList names;
        for (const FileRef &f : filesOf(v))
            names.append(f.name);
        return names.join(QStringLiteral(", "));
    }
    case PropType::UniqueId:
        if (!v.isDouble())
            return {};
        return p.idPrefix.isEmpty() ? QString::number(qint64(v.toDouble()))
                                    : p.idPrefix + QLatin1Char('-') + QString::number(qint64(v.toDouble()));
    case PropType::Formula: {
        if (v.isDouble())
            return formatNumber(v.toDouble(), p.numberFormat, p.decimals);
        if (v.isBool())
            return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
        if (v.isArray()) {
            QStringList parts;
            for (const QJsonValue &x : v.toArray())
                parts.append(x.toString());
            return parts.join(QStringLiteral(", "));
        }
        if (p.formulaType == QLatin1String("date"))
            return formatDateFor(p, v.toString());
        return v.toString();
    }
    case PropType::Rollup:
        return aggregateDisplay(p, p.rollupFn, v);
    case PropType::Button:
        return p.button.value(QStringLiteral("label")).toString();
    default:
        return v.toString();
    }
}

bool isEmptyValue(const Property &p, const QJsonValue &v)
{
    switch (p.type) {
    case PropType::Number:
    case PropType::UniqueId:
        return !v.isDouble();
    case PropType::Checkbox:
        return !v.toBool();
    case PropType::MultiSelect: {
        for (const QJsonValue &x : v.toArray())
            if (p.option(x.toString()))
                return false;
        return true;
    }
    case PropType::Select:
    case PropType::Status:
        return !p.option(v.toString());
    case PropType::Date:
    case PropType::Created:
    case PropType::Edited:
        return !dateTimeOf(v).isValid();
    case PropType::Relation:
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy:
        return v.toArray().isEmpty();
    case PropType::Files:
        return filesOf(v).isEmpty();
    case PropType::Formula:
    case PropType::Rollup:
        if (v.isDouble())
            return false;
        if (v.isBool())
            return !v.toBool();
        if (v.isArray())
            return v.toArray().isEmpty();
        return v.toString().trimmed().isEmpty();
    case PropType::Button:
        return true;
    default:
        return v.toString().trimmed().isEmpty();
    }
}

namespace
{
int optionIndex(const Property &p, const QString &id)
{
    for (int i = 0; i < p.options.size(); ++i)
        if (p.options[i].id == id)
            return i;
    return -1;
}
}

int compareValues(const Property &p, const QJsonValue &a, const QJsonValue &b)
{
    switch (p.type) {
    case PropType::Number:
    case PropType::UniqueId: {
        const double x = a.toDouble(), y = b.toDouble();
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    case PropType::Checkbox:
        return int(a.toBool()) - int(b.toBool());
    case PropType::Select:
    case PropType::Status:
        return optionIndex(p, a.toString()) - optionIndex(p, b.toString());
    case PropType::MultiSelect: {
        const QJsonArray x = a.toArray(), y = b.toArray();
        const int n = int(qMin(x.size(), y.size()));
        for (int i = 0; i < n; ++i) {
            const int c = optionIndex(p, x[i].toString()) - optionIndex(p, y[i].toString());
            if (c)
                return c;
        }
        return int(x.size() - y.size());
    }
    case PropType::Date:
    case PropType::Created:
    case PropType::Edited: {
        const QDateTime x = dateTimeOf(a), y = dateTimeOf(b);
        return x < y ? -1 : (x > y ? 1 : 0);
    }
    case PropType::Formula:
    case PropType::Rollup: {
        if (a.isDouble() && b.isDouble()) {
            const double x = a.toDouble(), y = b.toDouble();
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        if (a.isBool() || b.isBool())
            return int(a.toBool()) - int(b.toBool());
        if (resultType(p) == PropType::Date) {
            const QDateTime x = dateTimeOf(a), y = dateTimeOf(b);
            return x < y ? -1 : (x > y ? 1 : 0);
        }
        return QString::localeAwareCompare(displayValue(p, a).toLower(), displayValue(p, b).toLower());
    }
    case PropType::Relation:
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy:
    case PropType::Files:
        return QString::localeAwareCompare(displayValue(p, a).toLower(), displayValue(p, b).toLower());
    default:
        return QString::localeAwareCompare(a.toString().toLower(), b.toString().toLower());
    }
}

QStringList relativeDateKeys()
{
    return {QStringLiteral("@today"),   QStringLiteral("@tomorrow"),  QStringLiteral("@yesterday"), QStringLiteral("@weekago"),
            QStringLiteral("@weeklater"), QStringLiteral("@monthago"), QStringLiteral("@monthlater")};
}

QString relativeDateName(const QString &key)
{
    static const QHash<QString, const char *> names = {
        {QStringLiteral("@today"), QT_TRANSLATE_NOOP("Schema", "Today")},
        {QStringLiteral("@tomorrow"), QT_TRANSLATE_NOOP("Schema", "Tomorrow")},
        {QStringLiteral("@yesterday"), QT_TRANSLATE_NOOP("Schema", "Yesterday")},
        {QStringLiteral("@weekago"), QT_TRANSLATE_NOOP("Schema", "One week ago")},
        {QStringLiteral("@weeklater"), QT_TRANSLATE_NOOP("Schema", "One week from now")},
        {QStringLiteral("@monthago"), QT_TRANSLATE_NOOP("Schema", "One month ago")},
        {QStringLiteral("@monthlater"), QT_TRANSLATE_NOOP("Schema", "One month from now")},
    };
    const char *n = names.value(key);
    return n ? QCoreApplication::translate("Schema", n) : QString();
}

QDate filterDate(const QJsonValue &v, const QDate &today)
{
    const QString s = v.toString();
    if (!s.startsWith(QLatin1Char('@')))
        return dateOf(v);
    if (s == QLatin1String("@today"))
        return today;
    if (s == QLatin1String("@tomorrow"))
        return today.addDays(1);
    if (s == QLatin1String("@yesterday"))
        return today.addDays(-1);
    if (s == QLatin1String("@weekago"))
        return today.addDays(-7);
    if (s == QLatin1String("@weeklater"))
        return today.addDays(7);
    if (s == QLatin1String("@monthago"))
        return today.addMonths(-1);
    if (s == QLatin1String("@monthlater"))
        return today.addMonths(1);
    return {};
}

QStringList filterOps(PropType t)
{
    switch (t) {
    case PropType::Number:
    case PropType::UniqueId:
        return {QStringLiteral("eq"), QStringLiteral("neq"), QStringLiteral("gt"), QStringLiteral("lt"),
                QStringLiteral("gte"), QStringLiteral("lte"), QStringLiteral("empty"), QStringLiteral("notempty")};
    case PropType::Select:
        return {QStringLiteral("is"), QStringLiteral("isnot"), QStringLiteral("empty"), QStringLiteral("notempty")};
    case PropType::Status:
        return {QStringLiteral("is"), QStringLiteral("isnot"), QStringLiteral("ingroup"), QStringLiteral("empty"),
                QStringLiteral("notempty")};
    case PropType::MultiSelect:
    case PropType::Relation:
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy:
        return {QStringLiteral("contains"), QStringLiteral("notcontains"), QStringLiteral("empty"), QStringLiteral("notempty")};
    case PropType::Files:
        return {QStringLiteral("empty"), QStringLiteral("notempty")};
    case PropType::Date:
    case PropType::Created:
    case PropType::Edited:
        return {QStringLiteral("is"),        QStringLiteral("before"),    QStringLiteral("after"),
                QStringLiteral("onorbefore"), QStringLiteral("onorafter"), QStringLiteral("between"),
                QStringLiteral("today"),     QStringLiteral("tomorrow"),  QStringLiteral("yesterday"),
                QStringLiteral("thisweek"),  QStringLiteral("pastweek"),  QStringLiteral("nextweek"),
                QStringLiteral("thismonth"), QStringLiteral("pastmonth"), QStringLiteral("nextmonth"),
                QStringLiteral("pastyear"),  QStringLiteral("nextyear"),  QStringLiteral("empty"),
                QStringLiteral("notempty")};
    case PropType::Checkbox:
        return {QStringLiteral("checked"), QStringLiteral("unchecked")};
    case PropType::Button:
        return {};
    default:
        return {QStringLiteral("contains"), QStringLiteral("notcontains"), QStringLiteral("is"), QStringLiteral("isnot"),
                QStringLiteral("starts"),   QStringLiteral("ends"),        QStringLiteral("empty"), QStringLiteral("notempty")};
    }
}

QString filterOpName(const QString &op)
{
    static const QHash<QString, const char *> names = {
        {QStringLiteral("is"), QT_TRANSLATE_NOOP("Schema", "is")},
        {QStringLiteral("isnot"), QT_TRANSLATE_NOOP("Schema", "is not")},
        {QStringLiteral("ingroup"), QT_TRANSLATE_NOOP("Schema", "is in group")},
        {QStringLiteral("contains"), QT_TRANSLATE_NOOP("Schema", "contains")},
        {QStringLiteral("notcontains"), QT_TRANSLATE_NOOP("Schema", "does not contain")},
        {QStringLiteral("starts"), QT_TRANSLATE_NOOP("Schema", "starts with")},
        {QStringLiteral("ends"), QT_TRANSLATE_NOOP("Schema", "ends with")},
        {QStringLiteral("empty"), QT_TRANSLATE_NOOP("Schema", "is empty")},
        {QStringLiteral("notempty"), QT_TRANSLATE_NOOP("Schema", "is not empty")},
        {QStringLiteral("eq"), "="},
        {QStringLiteral("neq"), "≠"},
        {QStringLiteral("gt"), ">"},
        {QStringLiteral("lt"), "<"},
        {QStringLiteral("gte"), "≥"},
        {QStringLiteral("lte"), "≤"},
        {QStringLiteral("before"), QT_TRANSLATE_NOOP("Schema", "is before")},
        {QStringLiteral("after"), QT_TRANSLATE_NOOP("Schema", "is after")},
        {QStringLiteral("onorbefore"), QT_TRANSLATE_NOOP("Schema", "is on or before")},
        {QStringLiteral("onorafter"), QT_TRANSLATE_NOOP("Schema", "is on or after")},
        {QStringLiteral("between"), QT_TRANSLATE_NOOP("Schema", "is between")},
        {QStringLiteral("today"), QT_TRANSLATE_NOOP("Schema", "is today")},
        {QStringLiteral("tomorrow"), QT_TRANSLATE_NOOP("Schema", "is tomorrow")},
        {QStringLiteral("yesterday"), QT_TRANSLATE_NOOP("Schema", "is yesterday")},
        {QStringLiteral("thisweek"), QT_TRANSLATE_NOOP("Schema", "is this week")},
        {QStringLiteral("pastweek"), QT_TRANSLATE_NOOP("Schema", "is within the past week")},
        {QStringLiteral("nextweek"), QT_TRANSLATE_NOOP("Schema", "is within the next week")},
        {QStringLiteral("thismonth"), QT_TRANSLATE_NOOP("Schema", "is this month")},
        {QStringLiteral("pastmonth"), QT_TRANSLATE_NOOP("Schema", "is within the past month")},
        {QStringLiteral("nextmonth"), QT_TRANSLATE_NOOP("Schema", "is within the next month")},
        {QStringLiteral("pastyear"), QT_TRANSLATE_NOOP("Schema", "is within the past year")},
        {QStringLiteral("nextyear"), QT_TRANSLATE_NOOP("Schema", "is within the next year")},
        {QStringLiteral("checked"), QT_TRANSLATE_NOOP("Schema", "is checked")},
        {QStringLiteral("unchecked"), QT_TRANSLATE_NOOP("Schema", "is not checked")},
    };
    const char *n = names.value(op, nullptr);
    return n ? QCoreApplication::translate("Schema", n) : op;
}

bool filterNeedsValue(const QString &op)
{
    static const QSet<QString> none = {
        QStringLiteral("empty"),     QStringLiteral("notempty"),  QStringLiteral("today"),     QStringLiteral("tomorrow"),
        QStringLiteral("yesterday"), QStringLiteral("thisweek"),  QStringLiteral("pastweek"),  QStringLiteral("nextweek"),
        QStringLiteral("thismonth"), QStringLiteral("pastmonth"), QStringLiteral("nextmonth"), QStringLiteral("pastyear"),
        QStringLiteral("nextyear"),  QStringLiteral("checked"),   QStringLiteral("unchecked")};
    return !none.contains(op);
}

bool matchesFilter(const Property &prop, const FilterRule &rule, const QJsonValue &v)
{
    if (isComputedResult(prop)) {
        const Property q = asResult(prop);
        // A checkbox-like formula still answers "is empty" by its value.
        return matchesFilter(q, rule, q.type == PropType::Text && v.isArray()
                                          ? QJsonValue(displayValue(prop, v))
                                          : v);
    }
    const Property &p = prop;
    const QString &op = rule.op;
    if (op == QLatin1String("empty"))
        return isEmptyValue(p, v);
    if (op == QLatin1String("notempty"))
        return !isEmptyValue(p, v);
    // A rule without a value yet doesn't hide anything.
    if (filterNeedsValue(op)) {
        const QJsonValue &rv = rule.value;
        if (rv.isNull() || rv.isUndefined() || (rv.isString() && rv.toString().isEmpty())
            || (rv.isArray() && rv.toArray().isEmpty()))
            return true;
    }

    switch (p.type) {
    case PropType::Checkbox:
        return op == QLatin1String("checked") ? v.toBool() : !v.toBool();
    case PropType::Number: {
        if (!v.isDouble())
            return op == QLatin1String("neq");
        const double x = v.toDouble();
        const double y = rule.value.isDouble() ? rule.value.toDouble() : rule.value.toString().toDouble();
        if (op == QLatin1String("eq"))
            return qFuzzyCompare(x + 1, y + 1);
        if (op == QLatin1String("neq"))
            return !qFuzzyCompare(x + 1, y + 1);
        if (op == QLatin1String("gt"))
            return x > y;
        if (op == QLatin1String("lt"))
            return x < y;
        if (op == QLatin1String("gte"))
            return x >= y;
        if (op == QLatin1String("lte"))
            return x <= y;
        return true;
    }
    case PropType::Select:
    case PropType::Status: {
        if (op == QLatin1String("ingroup")) {
            const Option *o = p.option(v.toString());
            return o && o->group == rule.value.toInt();
        }
        // The value may be one option id or several (is any of).
        bool same = false;
        if (rule.value.isArray()) {
            for (const QJsonValue &x : rule.value.toArray())
                if (x.toString() == v.toString())
                    same = true;
        } else {
            same = v.toString() == rule.value.toString();
        }
        return op == QLatin1String("isnot") ? !same : same;
    }
    case PropType::MultiSelect:
    case PropType::Relation:
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy: {
        QSet<QString> wanted;
        if (rule.value.isArray())
            for (const QJsonValue &x : rule.value.toArray())
                wanted.insert(x.toString());
        else
            wanted.insert(rule.value.toString());
        bool has = false;
        for (const QJsonValue &x : v.toArray())
            if (wanted.contains(x.toString()))
                has = true;
        return op == QLatin1String("notcontains") ? !has : has;
    }
    case PropType::Date:
    case PropType::Created:
    case PropType::Edited: {
        const QDate d = dateOf(v);
        // A range matches when any day of it does (Notion's behaviour for "is").
        const QDate dEnd = dateEndOf(v).isValid() ? dateEndOf(v).date() : d;
        const QDate today = QDate::currentDate();
        auto overlaps = [&](const QDate &a, const QDate &b) { return d.isValid() && d <= b && dEnd >= a; };
        if (op == QLatin1String("today"))
            return overlaps(today, today);
        if (op == QLatin1String("tomorrow"))
            return overlaps(today.addDays(1), today.addDays(1));
        if (op == QLatin1String("yesterday"))
            return overlaps(today.addDays(-1), today.addDays(-1));
        if (op == QLatin1String("thisweek")) {
            const QDate monday = today.addDays(1 - today.dayOfWeek());
            return overlaps(monday, monday.addDays(6));
        }
        if (op == QLatin1String("pastweek"))
            return overlaps(today.addDays(-7), today);
        if (op == QLatin1String("nextweek"))
            return overlaps(today, today.addDays(7));
        if (op == QLatin1String("thismonth"))
            return overlaps(QDate(today.year(), today.month(), 1), QDate(today.year(), today.month(), today.daysInMonth()));
        if (op == QLatin1String("pastmonth"))
            return overlaps(today.addMonths(-1), today);
        if (op == QLatin1String("nextmonth"))
            return overlaps(today, today.addMonths(1));
        if (op == QLatin1String("pastyear"))
            return overlaps(today.addYears(-1), today);
        if (op == QLatin1String("nextyear"))
            return overlaps(today, today.addYears(1));
        if (op == QLatin1String("between")) {
            const QJsonArray r = rule.value.toArray();
            QDate a = r.size() > 0 ? filterDate(r[0]) : QDate(), b = r.size() > 1 ? filterDate(r[1]) : QDate();
            if (!a.isValid() && !b.isValid())
                return true;
            if (!a.isValid())
                a = QDate(1, 1, 1);
            if (!b.isValid())
                b = QDate(9999, 12, 31);
            if (b < a)
                std::swap(a, b);
            return overlaps(a, b);
        }
        const QDate ref = filterDate(rule.value);
        if (!d.isValid() || !ref.isValid())
            return false;
        if (op == QLatin1String("is"))
            return overlaps(ref, ref);
        if (op == QLatin1String("before"))
            return d < ref;
        if (op == QLatin1String("after"))
            return dEnd > ref;
        if (op == QLatin1String("onorbefore"))
            return d <= ref;
        if (op == QLatin1String("onorafter"))
            return dEnd >= ref;
        return true;
    }
    case PropType::Files:
        return true;
    default: {
        const QString text = p.type == PropType::Title || p.type == PropType::Text || p.type == PropType::Url
                                     || p.type == PropType::Email || p.type == PropType::Phone
                                 ? v.toString()
                                 : displayValue(p, v);
        const QString needle = rule.value.toString();
        if (op == QLatin1String("contains"))
            return text.contains(needle, Qt::CaseInsensitive);
        if (op == QLatin1String("notcontains"))
            return !text.contains(needle, Qt::CaseInsensitive);
        if (op == QLatin1String("is"))
            return text.compare(needle, Qt::CaseInsensitive) == 0;
        if (op == QLatin1String("isnot"))
            return text.compare(needle, Qt::CaseInsensitive) != 0;
        if (op == QLatin1String("starts"))
            return text.startsWith(needle, Qt::CaseInsensitive);
        if (op == QLatin1String("ends"))
            return text.endsWith(needle, Qt::CaseInsensitive);
        return true;
    }
    }
}

bool rowMatches(const Schema &schema, const QList<FilterRule> &rules, bool any, const RowRef &row)
{
    bool sawRule = false;
    for (const FilterRule &f : rules) {
        bool m;
        if (f.group) {
            if (filterRuleCount(f.rules) == 0)
                continue;
            m = rowMatches(schema, f.rules, f.any, row);
        } else {
            const Property *p = schema.prop(f.prop);
            if (!p)
                continue;
            m = matchesFilter(*p, f, rowValue(*p, row));
        }
        sawRule = true;
        if (any && m)
            return true;
        if (!any && !m)
            return false;
    }
    return any ? !sawRule : true;
}

int filterRuleCount(const QList<FilterRule> &rules)
{
    int n = 0;
    for (const FilterRule &f : rules)
        n += f.group ? filterRuleCount(f.rules) : 1;
    return n;
}

QList<RowRef> applyView(const Schema &schema, const View &view, QList<RowRef> rows, const QString &search)
{
    QList<RowRef> out;
    out.reserve(rows.size());
    const QString needle = search.trimmed();
    for (RowRef &r : rows) {
        bool keep = rowMatches(schema, view.filters, view.filterAny, r);
        if (keep && !needle.isEmpty()) {
            bool found = false;
            for (const Property &p : schema.props) {
                if (p.type == PropType::Button)
                    continue;
                if (displayValue(p, rowValue(p, r)).contains(needle, Qt::CaseInsensitive)
                    || (p.type == PropType::Title && rowValue(p, r).toString().contains(needle, Qt::CaseInsensitive))) {
                    found = true;
                    break;
                }
            }
            keep = found;
        }
        if (keep)
            out.append(std::move(r));
    }
    // Sort keys are computed once per row (formulas and rollups aren't free).
    struct Keyed {
        RowRef row;
        QList<QJsonValue> keys;
    };
    QList<Keyed> keyed;
    keyed.reserve(out.size());
    QList<const Property *> sortProps;
    for (const SortRule &s : view.sorts)
        sortProps.append(schema.prop(s.prop));
    for (RowRef &r : out) {
        Keyed k{std::move(r), {}};
        for (const Property *p : sortProps)
            k.keys.append(p ? rowValue(*p, k.row) : QJsonValue());
        keyed.append(std::move(k));
    }
    std::stable_sort(keyed.begin(), keyed.end(), [&](const Keyed &a, const Keyed &b) {
        for (int i = 0; i < sortProps.size(); ++i) {
            const Property *p = sortProps[i];
            if (!p)
                continue;
            const QJsonValue &va = a.keys[i], &vb = b.keys[i];
            const bool ea = isEmptyValue(*p, va) && resultType(*p) != PropType::Checkbox;
            const bool eb = isEmptyValue(*p, vb) && resultType(*p) != PropType::Checkbox;
            if (ea != eb)
                return eb; // empty values always sink to the bottom
            const int c = compareValues(*p, va, vb);
            if (c != 0)
                return view.sorts[i].descending ? c > 0 : c < 0;
        }
        const double sa = a.row.meta ? a.row.meta->sort : 0, sb = b.row.meta ? b.row.meta->sort : 0;
        if (sa != sb)
            return sa < sb;
        return (a.row.meta ? a.row.meta->created : 0) < (b.row.meta ? b.row.meta->created : 0);
    });
    out.clear();
    for (Keyed &k : keyed)
        out.append(std::move(k.row));
    return out;
}

namespace
{
QString parseDateText(const QString &input)
{
    QString text = input.trimmed();
    if (text.isEmpty())
        return {};
    // Notion exports ranges as "start → end"; keep the start.
    const int arrow = text.indexOf(QStringLiteral("→"));
    if (arrow > 0)
        text = text.left(arrow).trimmed();
    text.remove(QStringLiteral("(GMT"), Qt::CaseInsensitive);

    // ISO first.
    if (QDateTime dt = QDateTime::fromString(text.left(16), QStringLiteral("yyyy-MM-ddTHH:mm")); dt.isValid())
        return dt.toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
    if (QDateTime dt = QDateTime::fromString(text.left(16), QStringLiteral("yyyy-MM-dd HH:mm")); dt.isValid())
        return dt.toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
    if (QDate d = QDate::fromString(text.left(10), Qt::ISODate); d.isValid() && (text.size() == 10 || !text[10].isDigit()))
        return d.toString(Qt::ISODate);

    const QLocale en(QLocale::English);
    static const QStringList dateFormats = {
        QStringLiteral("MMMM d, yyyy"), QStringLiteral("MMM d, yyyy"), QStringLiteral("d MMMM yyyy"),
        QStringLiteral("d MMM yyyy"),   QStringLiteral("yyyy/MM/dd"),  QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("M/d/yyyy"),     QStringLiteral("dd.MM.yyyy"),  QStringLiteral("dd-MM-yyyy"),
        QStringLiteral("dddd, MMMM d, yyyy"),
    };
    static const QStringList timeFormats = {QStringLiteral("h:mm AP"), QStringLiteral("h:mm ap"), QStringLiteral("HH:mm"),
                                            QStringLiteral("h:mmAP")};
    for (const QString &df : dateFormats) {
        if (QDate d = en.toDate(text, df); d.isValid())
            return d.toString(Qt::ISODate);
        for (const QString &tf : timeFormats) {
            if (QDateTime dt = en.toDateTime(text, df + QLatin1Char(' ') + tf); dt.isValid())
                return dt.toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
        }
    }
    // A date followed by something we don't understand: try just the date part.
    static const QRegularExpression leadingDate(QStringLiteral("^([A-Za-z]+ \\d{1,2}, \\d{4})"));
    if (auto m = leadingDate.match(text); m.hasMatch()) {
        for (const QString &df : dateFormats)
            if (QDate d = en.toDate(m.captured(1), df); d.isValid())
                return d.toString(Qt::ISODate);
    }
    return {};
}

int nextShade(const Property &p)
{
    // Spread new options across the darker, readable half of the scale first.
    static const int order[] = {2, 5, 7, 3, 6, 1, 8, 4, 0, 9};
    return order[p.options.size() % 10];
}
}

QJsonValue parseValue(Property &p, const QString &text, bool addOptions)
{
    const QString t = text.trimmed();
    switch (p.type) {
    case PropType::Number: {
        QString s = t;
        s.remove(QRegularExpression(QStringLiteral("[\\s,$€£¥]")));
        const bool percent = s.endsWith(QLatin1Char('%'));
        if (percent)
            s.chop(1);
        bool ok = false;
        double v = s.toDouble(&ok);
        if (!ok)
            return QJsonValue();
        if (percent)
            v /= 100.0;
        return v;
    }
    case PropType::Checkbox: {
        static const QSet<QString> yes = {QStringLiteral("yes"), QStringLiteral("true"), QStringLiteral("1"),
                                          QStringLiteral("x"),   QStringLiteral("✓"),    QStringLiteral("✔"),
                                          QStringLiteral("checked"), QStringLiteral("done")};
        return yes.contains(t.toLower());
    }
    case PropType::Select:
    case PropType::Status: {
        if (t.isEmpty())
            return QJsonValue();
        if (const Option *o = p.optionByName(t))
            return o->id;
        if (!addOptions)
            return QJsonValue();
        Option o;
        o.id = Schema::newOptionId(p);
        o.name = t;
        o.shade = nextShade(p);
        p.options.append(o);
        return o.id;
    }
    case PropType::MultiSelect: {
        QJsonArray ids;
        for (QString part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            part = part.trimmed();
            if (part.isEmpty())
                continue;
            if (const Option *o = p.optionByName(part)) {
                if (!ids.contains(o->id))
                    ids.append(o->id);
                continue;
            }
            if (!addOptions)
                continue;
            Option o;
            o.id = Schema::newOptionId(p);
            o.name = part;
            o.shade = nextShade(p);
            p.options.append(o);
            ids.append(o.id);
        }
        return ids.isEmpty() ? QJsonValue() : QJsonValue(ids);
    }
    case PropType::Date: {
        // Ranges: "start → end" (Notion's export) or ISO "start/end".
        QString startText = t, endText;
        for (const QString &sep : {QStringLiteral("→"), QStringLiteral("/"), QStringLiteral(" - ")}) {
            if (sep == QLatin1String("/") && !QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}")).match(t).hasMatch())
                continue;
            const int i = t.indexOf(sep);
            if (i > 0) {
                startText = t.left(i).trimmed();
                endText = t.mid(i + sep.size()).trimmed();
                break;
            }
        }
        const QString iso = parseDateText(startText);
        if (iso.isEmpty())
            return QJsonValue();
        const QString end = endText.isEmpty() ? QString() : parseDateText(endText);
        if (end.isEmpty())
            return iso;
        // Both ends share the start's precision.
        const bool time = iso.size() > 10;
        QDateTime e = dateTimeOf(end);
        return makeDateValue(dateTimeOf(iso), time, e);
    }
    case PropType::Person: {
        QJsonArray ids;
        const DbLookup *l = dbLookup();
        for (QString part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            part = part.trimmed();
            if (part.isEmpty())
                continue;
            ids.append(l && l->lookupPersonName(l->lookupMe()).compare(part, Qt::CaseInsensitive) == 0 ? l->lookupMe() : part);
        }
        return ids.isEmpty() ? QJsonValue() : QJsonValue(ids);
    }
    case PropType::Files: {
        QJsonArray files;
        for (QString part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            part = part.trimmed();
            if (part.isEmpty())
                continue;
            if (part.contains(QStringLiteral("://")))
                files.append(QJsonObject{{QStringLiteral("n"), part.section(QLatin1Char('/'), -1)}, {QStringLiteral("u"), part}});
            else
                files.append(QJsonObject{{QStringLiteral("n"), part.section(QLatin1Char('/'), -1)}, {QStringLiteral("u"), part}});
        }
        return files.isEmpty() ? QJsonValue() : QJsonValue(files);
    }
    case PropType::UniqueId: {
        static const QRegularExpression num(QStringLiteral("(\\d+)$"));
        const auto m = num.match(t);
        return m.hasMatch() ? QJsonValue(m.captured(1).toDouble()) : QJsonValue();
    }
    case PropType::Created:
    case PropType::Edited:
    case PropType::Formula:
    case PropType::Rollup:
    case PropType::CreatedBy:
    case PropType::EditedBy:
    case PropType::Button:
    case PropType::Relation: // resolved by the importer, which knows page titles
        return QJsonValue();
    default:
        return t.isEmpty() ? QJsonValue() : QJsonValue(t);
    }
}
