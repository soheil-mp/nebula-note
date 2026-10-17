#pragma once

#include <QDateTime>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

// Notion's formula language (2.0): prop("Name"), + - * / % ^, comparisons,
// and/or/not, ternaries, method calls ("text".upper()), lists, lambdas with
// `current` and `index` (map, filter, find, some, every…), let/lets and the
// date functions. Values are dynamically typed like Notion's.
struct FValue {
    enum Type { Empty, Number, Text, Bool, Date, List, Page, Person };
    Type type = Empty;
    double num = 0;
    QString text;      // Text; the id for Page and Person
    bool flag = false; // Bool
    QDateTime date;    // Date (start)
    QDateTime end;     // Date range end, invalid when not a range
    bool hasTime = false;
    QList<FValue> list;

    static FValue number(double v);
    static FValue string(const QString &v);
    static FValue boolean(bool v);
    static FValue dateTime(const QDateTime &v, bool withTime);
    static FValue dateRange(const QDateTime &start, const QDateTime &end, bool withTime);
    static FValue page(const QString &id);
    static FValue person(const QString &id);
    static FValue makeList(const QList<FValue> &items);

    bool isEmpty() const;
    bool truthy() const;
    bool operator==(const FValue &o) const;
};

// What a formula can see beyond its own expression.
struct FormulaContext {
    QString pageId; // the row being computed (id())
    // Value of a property, by name, of any row (empty page id: this row).
    std::function<FValue(const QString &pageId, const QString &propName, QString *error)> prop;
    std::function<QString(const QString &pageId)> pageTitle;
    std::function<QString(const QString &personId)> personName;
    QDateTime now; // fixed clock for tests; invalid = the real time
};

struct FormulaResult {
    FValue value;
    QString error; // empty on success
    bool ok() const { return error.isEmpty(); }
};

namespace Formula
{
FormulaResult evaluate(const QString &expression, const FormulaContext &ctx);
// Syntax check only.
QString check(const QString &expression);
// Property names the expression references through prop("…").
QStringList referencedProps(const QString &expression);

// Display text of a value ("true"/"false" for booleans, Notion's date style).
QString display(const FValue &v, const FormulaContext *ctx = nullptr);
// Stored/compared form: number → double, bool → bool, date → ISO string
// ("start/end" for ranges), list → array of display strings, text → string.
QJsonValue toJson(const FValue &v, const FormulaContext *ctx = nullptr);
// "number", "text", "bool", "date" or "list".
QString typeName(const FValue &v);

struct FunctionDoc {
    QString name;
    QString syntax;
    QString help;
    QString group; // Text, Math, Logic, Date, Lists, Other
};
const QList<FunctionDoc> &functions();
} // namespace Formula
