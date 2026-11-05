#pragma once

#include "Types.h"

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// The structure of a database: its properties (columns) and views.
enum class PropType {
    Title,
    Text,
    Number,
    Select,
    MultiSelect,
    Status,
    Date,
    Checkbox,
    Url,
    Email,
    Phone,
    Created,
    Edited,
    Formula,
    Relation,
    Rollup,
    Files,
    Person,
    CreatedBy,
    EditedBy,
    UniqueId,
    Button,
};

QString propTypeKey(PropType t);
PropType propTypeFromKey(const QString &key);
QString propTypeName(PropType t);
QString propTypeIcon(PropType t); // glyph name for Icons
bool propTypeHasOptions(PropType t);
bool propTypeIsComputed(PropType t);
QList<PropType> userPropTypes(); // the types offered when adding a property

// Ten pill styles (dark → light, then outlined) replace Notion's tag colours.
constexpr int kOptionShadeCount = 10;
QString optionShadeName(int shade);

struct Option {
    QString id;
    QString name;
    int shade = 0;
    int group = 0; // status only: 0 to-do, 1 in progress, 2 complete
};

struct Property {
    QString id;
    QString name;
    PropType type = PropType::Text;
    QList<Option> options;
    QString numberFormat = QStringLiteral("number"); // number, comma, percent, dollar, euro, pound, yen
    QString numberShow = QStringLiteral("number");   // number | bar | ring
    double numberDivide = 100;                       // bar/ring: the value that fills it
    int decimals = -1;                               // -1: as needed
    QString dateFormat = QStringLiteral("default");  // default | full | mdy | dmy | ymd | relative
    bool time24 = false;
    QString description;
    // Formula
    QString formula;
    QString formulaType = QStringLiteral("text"); // text | number | bool | date | list (inferred)
    // Relation: the related database, and the paired property there when two-way.
    QString relationDb;
    QString relationPair;
    bool relationOne = false; // limited to one page
    // Rollup: a property of the pages a relation points at, aggregated.
    QString rollupRelation;
    QString rollupProp;
    QString rollupFn = QStringLiteral("original");
    // Unique ID
    QString idPrefix;
    // Button: {"label", "icon", "actions": [{"type": "set"|"open"|"add"|"delete", …}]}
    QJsonObject button;

    const Option *option(const QString &optionId) const;
    const Option *optionByName(const QString &name) const;
};

enum class ViewType { Table, Board, List, Gallery, Calendar, Timeline, Chart, Feed, Form };
QString viewTypeKey(ViewType t);
ViewType viewTypeFromKey(const QString &key);
QString viewTypeName(ViewType t);
QString viewTypeIcon(ViewType t);

struct FilterRule {
    QString prop;
    QString op; // see filterOps()
    QJsonValue value;
    // A filter group (advanced filters): nested rules joined by AND, or OR
    // when `any`. Groups nest up to three levels like Notion's.
    bool group = false;
    bool any = false;
    QList<FilterRule> rules = {};
};

struct SortRule {
    QString prop;
    bool descending = false;
};

struct View {
    QString id;
    QString name;
    ViewType type = ViewType::Table;
    QList<FilterRule> filters;
    QList<SortRule> sorts;
    QString groupBy;  // board: select/status property
    QString dateProp; // calendar
    QStringList hidden; // property ids not shown
    QStringList order;  // property display order (missing ids go last)
    QHash<QString, int> widths; // table column widths
    QString cardPreview = QStringLiteral("cover"); // gallery/board: cover | content | none | <files property id>
    QString cardSize = QStringLiteral("medium");   // gallery: small | medium | large
    QStringList collapsedGroups; // board columns folded away
    // Filters: the top-level rules join with AND, or OR when filterAny.
    bool filterAny = false;
    // Grouping (every layout but calendar, chart and form).
    QString groupMode;  // dates: relative | day | week | month | year; text: exact | alpha; numbers: step
    double groupStep = 10;
    QString subGroupBy; // board: sub-groups as swimlanes; table/list: nested groups
    bool hideEmptyGroups = false;
    QStringList hiddenGroups;
    QHash<QString, QString> calcs; // property id → calculation (table footer, group headers)
    // Layout options
    bool wrapCells = false;
    bool showIcons = true;
    QString openIn = QStringLiteral("side"); // side | center | full
    bool cardFit = false;      // fit the preview image instead of cropping
    bool colorColumns = true;  // board: tint columns by option shade
    bool showWeekends = true;  // calendar, timeline
    int loadLimit = 50;        // rows shown at first (inline databases)
    int frozen = 0;            // table: frozen columns (0 = none, 1 = title…)
    bool showPageIcon = true;
    QString subItems = QStringLiteral("nested"); // nested | flat | parents | off
    // Timeline
    QString endDateProp;       // empty: the date property's range end
    QString timelineScale = QStringLiteral("month"); // day | week | biweek | month | quarter | year
    bool timelineTable = true;
    QStringList timelineTableProps;
    // Chart. The category axis is chartX (any property, the title included:
    // one bar per page) and the value axis chartY, whichever way the bars run.
    QString chartType = QStringLiteral("bar"); // bar | hbar | line | donut | number
    QString chartX;     // group by this property
    QString chartY = QStringLiteral("count"); // count | <fn>:<prop id>
    QString chartGroup; // stacks / series
    QString chartSort = QStringLiteral("manual"); // manual | xasc | xdesc | yasc | ydesc
    bool chartCumulative = false;
    bool chartLabels = true;
    bool chartHideEmpty = false; // omit zero values
    int chartDecimals = -1;      // -1: as needed
    bool chartAutoRange = true;  // else chartMin…chartMax
    double chartMin = 0;
    double chartMax = 100;
    struct RefLine {
        double value = 0;
        QString label;
        bool operator==(const RefLine &o) const = default;
    };
    QList<RefLine> chartRefLines;
    QString chartColor = QStringLiteral("auto"); // auto (grays) | colorful | gray | brown | orange | yellow | green | blue | purple | pink | red
    QString chartHeight = QStringLiteral("medium"); // small | medium | large | xlarge
    QString chartGrid = QStringLiteral("auto");     // auto | horizontal | vertical | both | none
    QString chartAxisNames = QStringLiteral("none"); // none | x | y | both
    bool chartLegend = true;
    QString chartCaption;
    bool chartSmooth = false;  // line
    bool chartGradient = true; // line: shaded area
    bool chartDots = true;     // line: data points
    // Form
    QString formTitle;
    QString formDescription;
    QStringList formRequired;
};

struct Schema {
    QList<Property> props;
    QList<View> views;
    bool locked = false;          // properties and views can't be changed
    QString subItemsProp;         // the "Parent item" self-relation
    QString dependencyProp;       // the "Blocked by" self-relation
    QString defaultTemplate;      // template page id for new rows

    bool isEmpty() const { return props.isEmpty(); }
    const Property *prop(const QString &id) const;
    Property *prop(const QString &id);
    const Property *titleProp() const;
    const View *view(const QString &id) const;
    View *view(const QString &id);
    int propIndex(const QString &id) const;

    // Properties in a view's display order, with or without hidden ones.
    QList<const Property *> orderedProps(const View &v, bool includeHidden = false) const;

    QString newPropId() const;
    QString newViewId() const;
    static QString newOptionId(const Property &p);

    // First select/status property, for grouping boards.
    QString defaultGroupProp() const;
    // First date property (or Created), for calendars.
    QString defaultDateProp() const;

    QJsonObject toJson() const;
    static Schema fromJson(const QJsonObject &o);

    // A new database: Name, Tags, and a Table view.
    static Schema basic();
    static View makeView(const Schema &schema, ViewType type, const QString &name = {});
};

// ---- values ------------------------------------------------------------------
// Row values live in a page's props JSON keyed by property id:
//   text/url/email/phone: string        number: double
//   select/status: option id            multi-select: array of option ids
//   date: "yyyy-MM-dd" or "yyyy-MM-ddTHH:mm", optionally "start/end" for a
//         range and "#r" at the end for a reminder     checkbox: bool
//   relation: array of page ids          person: array of person ids
//   files: array of {"n": name, "m": media file} or {"n", "u": url}
//   unique id: number
// Title, Created/Edited (time and by) come from the page itself; formulas
// and rollups are computed.

struct RowRef {
    const PageMeta *meta = nullptr;
    QJsonObject props;
};

// Where computed values look things up: other rows (relations, rollups),
// schemas (formulas name properties) and people. The Workspace installs one.
class DbLookup
{
public:
    virtual ~DbLookup() = default;
    virtual const PageMeta *lookupPage(const QString &id) const = 0;
    virtual QJsonObject lookupProps(const QString &id) const = 0;
    virtual const Schema *lookupSchema(const QString &databaseId) const = 0;
    virtual QString lookupPersonName(const QString &personId) const = 0;
    virtual QString lookupMe() const = 0; // the person id of the user
};
void setDbLookup(const DbLookup *lookup);
const DbLookup *dbLookup();

QJsonValue rowValue(const Property &p, const RowRef &row);
// Why a formula has no value for a row (empty when it evaluates).
QString formulaError(const Property &p, const RowRef &row);
QString displayValue(const Property &p, const QJsonValue &v);
QString formatNumber(double value, const QString &format, int decimals = -1);
QString formatDateValue(const QString &iso, bool longForm = false);
// A date value in the property's date and time format, with its range end.
QString formatDateFor(const Property &p, const QString &value);
QDate dateOf(const QJsonValue &v);
qint64 dateSortKey(const QJsonValue &v); // orders ISO dates/date-times; minimum when empty
QDateTime dateTimeOf(const QJsonValue &v);
// The end of a date range ("start/end"), or invalid.
QDateTime dateEndOf(const QJsonValue &v);
bool dateHasReminder(const QJsonValue &v);
QString makeDateValue(const QDateTime &start, bool withTime, const QDateTime &end = {}, bool remind = false);
bool isEmptyValue(const Property &p, const QJsonValue &v);
// Formulas, rollups and unique ids filter, sort and calculate like the type
// of value they produce (number, date, checkbox or text).
PropType resultType(const Property &p);
// Page ids of a relation value (live pages only).
QStringList relationIds(const QJsonValue &v);
// File entries of a files value: (name, media file or URL).
struct FileRef {
    QString name;
    QString media;
    QString url;
};
QList<FileRef> filesOf(const QJsonValue &v);

// Calculations (table footer, rollups, chart values).
QStringList calcFunctions(const Property &p, bool forRollup = false);
QString calcName(const QString &fn);
QJsonValue aggregate(const Property &p, const QList<QJsonValue> &values, const QString &fn);
QString aggregateDisplay(const Property &p, const QString &fn, const QJsonValue &result);
QString calculate(const Property &p, const QList<RowRef> &rows, const QString &fn);

// Groups for board columns, table/list/gallery sections and charts. A row
// with several values (multi-select, person, relation) is in each group.
struct RowGroup {
    QString key;   // option/page/person id, date bucket, "true"/"false", text; "" = no value
    QString label;
    int shade = -1;       // select-like: the option shade
    int statusGroup = -1; // status: 0 to-do, 1 in progress, 2 complete
    QList<int> rows;      // indexes into the rows
};
QList<RowGroup> groupRows(const Schema &schema, const Property &prop, const QList<RowRef> &rows,
                          const QString &mode = {}, double step = 10, bool hideEmpty = false);
QStringList groupKeysFor(const Property &prop, const QJsonValue &value, const QString &mode = {}, double step = 10);
bool canGroupBy(const Property &p);
// Sub-items: rows nested under their parent (the "Parent item" relation).
struct NestedRow {
    int index;
    int depth;
    int children; // number of direct children shown
};
QList<NestedRow> nestRows(const Schema &schema, const QList<RowRef> &rows, const QSet<QString> &collapsed,
                          const QString &mode);


int compareValues(const Property &p, const QJsonValue &a, const QJsonValue &b);

QStringList filterOps(PropType t);
// Date filter values may be relative ("@today", "@weekago"…) so a saved
// filter follows the calendar like Notion's.
QStringList relativeDateKeys();
QString relativeDateName(const QString &key);
QDate filterDate(const QJsonValue &v, const QDate &today = QDate::currentDate());
QString filterOpName(const QString &op);
bool filterNeedsValue(const QString &op);
bool matchesFilter(const Property &p, const FilterRule &rule, const QJsonValue &v);
// A whole rule tree against a row.
bool rowMatches(const Schema &schema, const QList<FilterRule> &rules, bool any, const RowRef &row);
int filterRuleCount(const QList<FilterRule> &rules); // leaves, for the toolbar badge

// Applies a view's filters, search text and sorts.
QList<RowRef> applyView(const Schema &schema, const View &view, QList<RowRef> rows, const QString &search = {});

// Parses user input (table cell, CSV) into a value of the property's type.
// Unknown select options are created on the property when `addOptions`.
QJsonValue parseValue(Property &p, const QString &text, bool addOptions = true);
