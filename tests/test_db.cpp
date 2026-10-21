// Databases (formulas, relations, rollups, filters, groups, calculations) and
// the workspace's newer storage (synced blocks, comments, versions,
// reminders, templates, search options).
#include "core/Blocks.h"
#include "core/Formula.h"
#include "core/Schema.h"
#include "core/Workspace.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace
{
FValue eval(const QString &expr, QString *error = nullptr)
{
    FormulaContext ctx;
    ctx.now = QDateTime(QDate(2026, 10, 17), QTime(14, 30));
    ctx.prop = [](const QString &, const QString &name, QString *err) -> FValue {
        if (name == QLatin1String("Price"))
            return FValue::number(12.5);
        if (name == QLatin1String("Name"))
            return FValue::string(QStringLiteral("Comet"));
        if (name == QLatin1String("Tags"))
            return FValue::makeList({FValue::string(QStringLiteral("a")), FValue::string(QStringLiteral("b"))});
        if (name == QLatin1String("Due"))
            return FValue::dateTime(QDateTime(QDate(2026, 10, 20), QTime(0, 0)), false);
        *err = QStringLiteral("No property named “%1”").arg(name);
        return {};
    };
    const FormulaResult r = Formula::evaluate(expr, ctx);
    if (error)
        *error = r.error;
    return r.value;
}

QString show(const QString &expr)
{
    QString err;
    const FValue v = eval(expr, &err);
    return err.isEmpty() ? Formula::display(v) : QStringLiteral("ERR: ") + err;
}

Property makeProp(const QString &id, const QString &name, PropType type)
{
    Property p;
    p.id = id;
    p.name = name;
    p.type = type;
    return p;
}
} // namespace

class DbTests : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    Workspace *m_ws = nullptr;

    // A second, raw connection to a workspace file (to check what's stored).
    static QVariant scalar(const QString &file, const QString &sql, const QVariantList &args = {})
    {
        QVariant out;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
            db.setDatabaseName(file);
            if (db.open()) {
                QSqlQuery q(db);
                q.prepare(sql);
                for (const QVariant &a : args)
                    q.addBindValue(a);
                if (q.exec() && q.next())
                    out = q.value(0);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("raw"));
        return out;
    }
    QString dbFile() const { return QDir(m_ws->directory()).filePath(QStringLiteral("workspace.db")); }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        m_ws = new Workspace(this);
        static int n = 0;
        QVERIFY(m_ws->open(m_dir.path() + QStringLiteral("/ws-") + QString::number(++n)));
    }

    void cleanup()
    {
        delete m_ws;
        m_ws = nullptr;
    }

    // ---- formula language ------------------------------------------------------

    void formulaArithmeticAndLogic()
    {
        QCOMPARE(show("1 + 2 * 3"), QStringLiteral("7"));
        QCOMPARE(show("(1 + 2) * 3"), QStringLiteral("9"));
        QCOMPARE(show("2 ^ 3 ^ 2"), QStringLiteral("512"));
        QCOMPARE(show("-2 ^ 2"), QStringLiteral("-4"));
        QCOMPARE(show("10 % 4"), QStringLiteral("2"));
        QCOMPARE(show("7 / 2"), QStringLiteral("3.5"));
        QCOMPARE(show("1 / 0"), QStringLiteral("Infinity"));
        QCOMPARE(show("round(2.345, 2)"), QStringLiteral("2.35"));
        QCOMPARE(show("round(-2.5)"), QStringLiteral("-2"));
        QCOMPARE(show("floor(-1.5) + ceil(1.2)"), QStringLiteral("0"));
        QCOMPARE(show("abs(-3) + sqrt(16) + cbrt(27)"), QStringLiteral("10"));
        QCOMPARE(show("max(1, 5, 3) - min([4, 2, 8])"), QStringLiteral("3"));
        QCOMPARE(show("sum(1, [2, 3], 4)"), QStringLiteral("10"));
        QCOMPARE(show("mean(1, 2, 3, 4)"), QStringLiteral("2.5"));
        QCOMPARE(show("median([5, 1, 3])"), QStringLiteral("3"));
        QCOMPARE(show("1 < 2 and 2 < 3"), QStringLiteral("true"));
        QCOMPARE(show("1 > 2 || not false"), QStringLiteral("true"));
        QCOMPARE(show("!true"), QStringLiteral("false"));
        QCOMPARE(show("1 == 1 && \"a\" != \"b\""), QStringLiteral("true"));
        QCOMPARE(show("5 > 3 ? \"big\" : \"small\""), QStringLiteral("big"));
        QCOMPARE(show("if(false, 1, 2)"), QStringLiteral("2"));
        QCOMPARE(show("ifs(1 > 2, \"a\", 2 > 3, \"b\", \"c\")"), QStringLiteral("c"));
        QCOMPARE(show("empty(\"\") and empty(0) and empty([]) and not empty(\"x\")"), QStringLiteral("true"));
        // Short circuit: the error on the right is never evaluated.
        QCOMPARE(show("true or nosuch()"), QStringLiteral("true"));
        QCOMPARE(show("pi > 3.14 and e < 2.72"), QStringLiteral("true"));
    }

    void formulaText()
    {
        QCOMPARE(show("\"Hello\" + \" \" + \"world\""), QStringLiteral("Hello world"));
        QCOMPARE(show("\"n = \" + 3"), QStringLiteral("n = 3"));
        QCOMPARE(show("length(\"nebula\")"), QStringLiteral("6"));
        QCOMPARE(show("\"nebula\".upper()"), QStringLiteral("NEBULA"));
        QCOMPARE(show("\"Nebula\".lower().length()"), QStringLiteral("6"));
        QCOMPARE(show("slice(\"Andromeda\", 0, 3)"), QStringLiteral("And"));
        QCOMPARE(show("\"Andromeda\".slice(-4)"), QStringLiteral("meda"));
        QCOMPARE(show("contains(\"Milky Way\", \"Way\")"), QStringLiteral("true"));
        QCOMPARE(show("test(\"abc123\", \"\\\\d+\")"), QStringLiteral("true"));
        QCOMPARE(show("replace(\"a-b-c\", \"-\", \"+\")"), QStringLiteral("a+b-c"));
        QCOMPARE(show("replaceAll(\"a-b-c\", \"-\", \"+\")"), QStringLiteral("a+b+c"));
        QCOMPARE(show("replaceAll(\"2026-10-17\", \"(\\\\d+)-(\\\\d+)-(\\\\d+)\", \"$3/$2/$1\")"), QStringLiteral("17/10/2026"));
        QCOMPARE(show("repeat(\"ab\", 3)"), QStringLiteral("ababab"));
        QCOMPARE(show("padStart(\"7\", 3, \"0\")"), QStringLiteral("007"));
        QCOMPARE(show("padEnd(\"7\", 3, \"0\")"), QStringLiteral("700"));
        QCOMPARE(show("join([\"a\", \"b\", \"c\"], \", \")"), QStringLiteral("a, b, c"));
        QCOMPARE(show("split(\"a,b,c\", \",\").length()"), QStringLiteral("3"));
        QCOMPARE(show("toNumber(\"42 apples\") + 1"), QStringLiteral("43"));
        QCOMPARE(show("format(12) + \"!\""), QStringLiteral("12!"));
        QCOMPARE(show("concat([1, 2], [3])"), QStringLiteral("1, 2, 3"));
        QCOMPARE(show("trim(\"  x  \")"), QStringLiteral("x"));
        QCOMPARE(show("match(\"a1b22c333\", \"\\\\d+\")"), QStringLiteral("1, 22, 333"));
    }

    void formulaListsAndLambdas()
    {
        QCOMPARE(show("[1, 2, 3].map(current * 2)"), QStringLiteral("2, 4, 6"));
        QCOMPARE(show("[1, 2, 3, 4].filter(current % 2 == 0)"), QStringLiteral("2, 4"));
        QCOMPARE(show("[5, 6, 7].map(index)"), QStringLiteral("0, 1, 2"));
        QCOMPARE(show("[3, 1, 2].sort()"), QStringLiteral("1, 2, 3"));
        QCOMPARE(show("[3, 1, 2].sort().reverse().first()"), QStringLiteral("3"));
        QCOMPARE(show("[1, 1, 2].unique()"), QStringLiteral("1, 2"));
        QCOMPARE(show("[1, 2, 3].includes(2)"), QStringLiteral("true"));
        QCOMPARE(show("[1, 2, 3].find(current > 1)"), QStringLiteral("2"));
        QCOMPARE(show("[1, 2, 3].findIndex(current > 5)"), QStringLiteral("-1"));
        QCOMPARE(show("[1, 2, 3].some(current > 2)"), QStringLiteral("true"));
        QCOMPARE(show("[1, 2, 3].every(current > 2)"), QStringLiteral("false"));
        QCOMPARE(show("[1, 2, 3].count(current >= 2)"), QStringLiteral("2"));
        QCOMPARE(show("[[1, 2], [3]].flat().length()"), QStringLiteral("3"));
        QCOMPARE(show("at([10, 20, 30], -1)"), QStringLiteral("30"));
        QCOMPARE(show("[10, 20, 30][1]"), QStringLiteral("20"));
        QCOMPARE(show("[1, 2, 3, 4].slice(1, 3)"), QStringLiteral("2, 3"));
        QCOMPARE(show("let(x, 5, x * x)"), QStringLiteral("25"));
        QCOMPARE(show("lets(a, 2, b, a + 1, a * b)"), QStringLiteral("6"));
        // Nested lambdas see the innermost `current`.
        QCOMPARE(show("[1, 2].map([10, 20].map(current + 1).sum() + current)"), QStringLiteral("33, 34"));
    }

    void formulaDates()
    {
        QCOMPARE(show("formatDate(now(), \"YYYY-MM-DD HH:mm\")"), QStringLiteral("2026-10-17 14:30"));
        QCOMPARE(show("formatDate(today(), \"dddd, MMMM Do\")"), QStringLiteral("Saturday, October 17th"));
        QCOMPARE(show("formatDate(dateAdd(today(), 3, \"days\"), \"MMM D\")"), QStringLiteral("Oct 20"));
        QCOMPARE(show("formatDate(dateSubtract(today(), 1, \"months\"), \"YYYY-MM-DD\")"), QStringLiteral("2026-09-17"));
        QCOMPARE(show("dateBetween(prop(\"Due\"), today(), \"days\")"), QStringLiteral("3"));
        QCOMPARE(show("dateBetween(parseDate(\"2027-01-01\"), parseDate(\"2026-10-17\"), \"months\")"), QStringLiteral("2"));
        QCOMPARE(show("year(today()) + month(today()) + date(today())"), QStringLiteral("2053"));
        QCOMPARE(show("day(today())"), QStringLiteral("6")); // Saturday
        QCOMPARE(show("week(parseDate(\"2026-01-05\"))"), QStringLiteral("2"));
        QCOMPARE(show("hour(now()) * 60 + minute(now())"), QStringLiteral("870"));
        QCOMPARE(show("formatDate(dateEnd(dateRange(today(), prop(\"Due\"))), \"D\")"), QStringLiteral("20"));
        QCOMPARE(show("prop(\"Due\") > now()"), QStringLiteral("true"));
        QCOMPARE(show("timestamp(fromTimestamp(1000))"), QStringLiteral("1000"));
        QCOMPARE(show("formatDate(parseDate(\"2026-12-25\"), \"MMMM D, YYYY\")"), QStringLiteral("December 25, 2026"));
    }

    void formulaPropsAndErrors()
    {
        QCOMPARE(show("prop(\"Price\") * 2"), QStringLiteral("25"));
        QCOMPARE(show("Price + 1"), QStringLiteral("13.5")); // bare property names
        QCOMPARE(show("prop(\"Name\").upper()"), QStringLiteral("COMET"));
        QCOMPARE(show("prop(\"Tags\").length()"), QStringLiteral("2"));
        QVERIFY(show("prop(\"Nope\")").startsWith(QStringLiteral("ERR")));
        QVERIFY(show("nosuch(1)").startsWith(QStringLiteral("ERR")));
        QVERIFY(show("1 +").startsWith(QStringLiteral("ERR")));
        QVERIFY(show("(1 + 2").startsWith(QStringLiteral("ERR")));
        QVERIFY(show("\"open").startsWith(QStringLiteral("ERR")));
        QVERIFY(show("round()").startsWith(QStringLiteral("ERR")));
        QVERIFY(!Formula::check("1 + (").isEmpty());
        QVERIFY(Formula::check("prop(\"A\") + 1").isEmpty());
        QCOMPARE(Formula::referencedProps("prop(\"A\") + prop(\"B\") * prop(\"A\")"), QStringList({"A", "B"}));
        QCOMPARE(Formula::typeName(eval("1 + 1")), QStringLiteral("number"));
        QCOMPARE(Formula::typeName(eval("1 > 0")), QStringLiteral("bool"));
        QCOMPARE(Formula::typeName(eval("today()")), QStringLiteral("date"));
        QCOMPARE(Formula::toJson(eval("dateAdd(today(), 1, \"days\")")).toString(), QStringLiteral("2026-10-18"));
        QVERIFY(!Formula::functions().isEmpty());
    }

    // ---- databases in a workspace ------------------------------------------------

    void formulaProperty()
    {
        Schema s = Schema::basic();
        s.props.append(makeProp("price", "Price", PropType::Number));
        s.props.append(makeProp("qty", "Qty", PropType::Number));
        Property total = makeProp("total", "Total", PropType::Formula);
        total.formula = QStringLiteral("prop(\"Price\") * prop(\"Qty\")");
        total.formulaType = QStringLiteral("number");
        total.numberFormat = QStringLiteral("dollar");
        s.props.append(total);
        Property label = makeProp("label", "Label", PropType::Formula);
        label.formula = QStringLiteral("prop(\"Name\") + \" ×\" + prop(\"Qty\")");
        s.props.append(label);
        const QString db = m_ws->createDatabase({}, QStringLiteral("Orders"), s);
        const QString a = m_ws->createRow(db, QStringLiteral("Telescope"), {{"price", 100.0}, {"qty", 2.0}});
        const QString b = m_ws->createRow(db, QStringLiteral("Lens"), {{"price", 30.0}, {"qty", 1.0}});
        const Schema live = m_ws->schema(db);
        const Property *tp = live.prop("total");
        RowRef ra{m_ws->page(a), m_ws->props(a)}, rb{m_ws->page(b), m_ws->props(b)};
        QCOMPARE(rowValue(*tp, ra).toDouble(), 200.0);
        QCOMPARE(displayValue(*tp, rowValue(*tp, ra)), QStringLiteral("$200.00"));
        QCOMPARE(displayValue(*live.prop("label"), rowValue(*live.prop("label"), rb)), QStringLiteral("Lens ×1"));
        // Filter and sort by the formula.
        View v = live.views.first();
        v.filters = {FilterRule{"total", "gt", 50.0}};
        QCOMPARE(applyView(live, v, {ra, rb}).size(), 1);
        v.filters.clear();
        v.sorts = {SortRule{"total", false}};
        const QList<RowRef> sorted = applyView(live, v, {ra, rb});
        QCOMPARE(sorted.first().meta->id, b);
        // Editing an input recomputes.
        m_ws->setProp(b, "qty", 10.0);
        QCOMPARE(rowValue(*tp, RowRef{m_ws->page(b), m_ws->props(b)}).toDouble(), 300.0);
        // A formula referring to itself reports instead of recursing forever.
        Schema loop = m_ws->schema(db);
        loop.prop("total")->formula = QStringLiteral("prop(\"Total\") + 1");
        m_ws->setSchema(db, loop);
        QVERIFY(rowValue(*m_ws->lookupSchema(db)->prop("total"), RowRef{m_ws->page(a), m_ws->props(a)}).isNull());
    }

    void relationsTwoWayAndRollups()
    {
        Schema ps = Schema::basic();
        const QString projects = m_ws->createDatabase({}, QStringLiteral("Projects"), ps);
        Schema ts = Schema::basic();
        ts.props.append(makeProp("hours", "Hours", PropType::Number));
        ts.props.append(makeProp("done", "Done", PropType::Checkbox));
        ts.props.append(makeProp("due", "Due", PropType::Date));
        const QString tasks = m_ws->createDatabase({}, QStringLiteral("Tasks"), ts);
        const QString rel = m_ws->addRelation(tasks, QStringLiteral("Project"), projects, true, QStringLiteral("Tasks"));
        QVERIFY(!rel.isEmpty());
        const Property relProp = *m_ws->lookupSchema(tasks)->prop(rel);
        QCOMPARE(relProp.type, PropType::Relation);
        QVERIFY(!relProp.relationPair.isEmpty());
        const QString pair = relProp.relationPair;
        QCOMPARE(m_ws->lookupSchema(projects)->prop(pair)->name, QStringLiteral("Tasks"));

        const QString p1 = m_ws->createRow(projects, QStringLiteral("Apollo"));
        const QString p2 = m_ws->createRow(projects, QStringLiteral("Gemini"));
        const QString t1 = m_ws->createRow(tasks, QStringLiteral("Design"), {{"hours", 3.0}, {"done", true}, {"due", "2019-10-01"}});
        const QString t2 = m_ws->createRow(tasks, QStringLiteral("Build"), {{"hours", 5.0}, {"due", "2019-10-09"}});
        m_ws->setProp(t1, rel, QJsonArray{p1});
        m_ws->setProp(t2, rel, QJsonArray{p1});
        QCOMPARE(relationIds(m_ws->props(p1).value(pair)), QStringList({t1, t2}));
        // The other side writes back too.
        m_ws->setProp(p2, pair, QJsonArray{t2});
        QCOMPARE(relationIds(m_ws->props(t2).value(rel)), QStringList({p1, p2}));
        // Created with a relation value: synced on creation.
        const QString t3 = m_ws->createRow(tasks, QStringLiteral("Test"), {{rel, QJsonArray{p2}}, {"hours", 1.0}});
        QVERIFY(relationIds(m_ws->props(p2).value(pair)).contains(t3));

        Schema pls = m_ws->schema(projects);
        auto rollup = [&](const QString &id, const QString &target, const QString &fn) {
            Property r = makeProp(id, id, PropType::Rollup);
            r.rollupRelation = pair;
            r.rollupProp = target;
            r.rollupFn = fn;
            pls.props.append(r);
        };
        rollup("count", "title", "count");
        rollup("sum", "hours", "sum");
        rollup("pct", "done", "pctchecked");
        rollup("names", "title", "original");
        rollup("latest", "due", "latest");
        rollup("span", "due", "daterange");
        m_ws->setSchema(projects, pls);
        const Schema *live = m_ws->lookupSchema(projects);
        auto val = [&](const QString &row, const QString &prop) {
            const Property *p = live->prop(prop);
            return displayValue(*p, rowValue(*p, RowRef{m_ws->page(row), m_ws->props(row)}));
        };
        QCOMPARE(val(p1, "count"), QStringLiteral("2"));
        QCOMPARE(val(p1, "sum"), QStringLiteral("8"));
        QCOMPARE(val(p1, "pct"), QStringLiteral("50%"));
        QCOMPARE(val(p1, "names"), QStringLiteral("Design, Build"));
        QCOMPARE(val(p1, "latest"), QStringLiteral("Oct 9, 2019"));
        QCOMPARE(val(p1, "span"), QStringLiteral("8 days"));
        QCOMPARE(val(p2, "sum"), QStringLiteral("6"));
        QCOMPARE(displayValue(relProp, m_ws->props(t2).value(rel)), QStringLiteral("Apollo, Gemini"));
        // A formula can read related pages.
        Schema withFormula = m_ws->schema(projects);
        Property f = makeProp("hrs", "Open hours", PropType::Formula);
        f.formula = QStringLiteral("prop(\"Tasks\").filter(not current.prop(\"Done\")).map(current.prop(\"Hours\")).sum()");
        withFormula.props.append(f);
        m_ws->setSchema(projects, withFormula);
        live = m_ws->lookupSchema(projects);
        QCOMPARE(val(p1, "hrs"), QStringLiteral("5"));
        // Trashed pages drop out of relations and rollups.
        m_ws->trash(t2);
        QCOMPARE(val(p1, "count"), QStringLiteral("1"));
        m_ws->restore(t2);
        QCOMPARE(val(p1, "count"), QStringLiteral("2"));
        // Unlinking removes the back link.
        m_ws->setProp(t1, rel, QJsonValue());
        QCOMPARE(relationIds(m_ws->props(p1).value(pair)), QStringList({t2}));
        // One-way again: the pair keeps its values but stops syncing.
        m_ws->setRelationTwoWay(tasks, rel, false);
        QVERIFY(m_ws->lookupSchema(tasks)->prop(rel)->relationPair.isEmpty());
        m_ws->setProp(t1, rel, QJsonArray{p2});
        QVERIFY(!relationIds(m_ws->props(p2).value(pair)).contains(t1));
        // Removing a property cleans up views and rollups that used it.
        m_ws->removeProperty(projects, pair);
        QVERIFY(!m_ws->lookupSchema(projects)->prop(pair));
        QVERIFY(m_ws->lookupSchema(projects)->prop("count")->rollupRelation.isEmpty());
    }

    void subItemsKeepOneParent()
    {
        const QString db = m_ws->createDatabase({}, QStringLiteral("Work"), Schema::basic());
        m_ws->enableSubItems(db, true);
        const Schema *s = m_ws->lookupSchema(db);
        const QString parent = s->subItemsProp;
        QVERIFY(!parent.isEmpty());
        QVERIFY(s->prop(parent)->relationOne);
        const QString subs = s->prop(parent)->relationPair;
        const QString a = m_ws->createRow(db, "A"), b = m_ws->createRow(db, "B"), c = m_ws->createRow(db, "C");
        m_ws->setProp(c, parent, QJsonArray{a});
        QCOMPARE(relationIds(m_ws->props(a).value(subs)), QStringList({c}));
        // Adding C under B through B's "Sub-items" moves it away from A.
        m_ws->setProp(b, subs, QJsonArray{c});
        QCOMPARE(relationIds(m_ws->props(c).value(parent)), QStringList({b}));
        QVERIFY(relationIds(m_ws->props(a).value(subs)).isEmpty());

        QList<RowRef> rows;
        for (const PageMeta *m : m_ws->rows(db))
            rows.append({m, m_ws->props(m->id)});
        const QList<NestedRow> nested = nestRows(*m_ws->lookupSchema(db), rows, {}, "nested");
        QCOMPARE(nested.size(), 3);
        QCOMPARE(rows[nested[0].index].meta->id, a);
        QCOMPARE(rows[nested[1].index].meta->id, b);
        QCOMPARE(nested[1].children, 1);
        QCOMPARE(rows[nested[2].index].meta->id, c);
        QCOMPARE(nested[2].depth, 1);
        // Collapsed parents hide their children; "parents" shows top level only.
        QCOMPARE(nestRows(*m_ws->lookupSchema(db), rows, {b}, "nested").size(), 2);
        QCOMPARE(nestRows(*m_ws->lookupSchema(db), rows, {}, "parents").size(), 2);
        QCOMPARE(nestRows(*m_ws->lookupSchema(db), rows, {}, "flat").size(), 3);
        m_ws->enableDependencies(db, true);
        QVERIFY(m_ws->lookupSchema(db)->prop(m_ws->lookupSchema(db)->dependencyProp));
    }

    void uniqueIdsAndPeople()
    {
        const QString db = m_ws->createDatabase({}, QStringLiteral("Bugs"), Schema::basic());
        const QString a = m_ws->createRow(db, "First");
        QTest::qWait(2);
        const QString b = m_ws->createRow(db, "Second");
        Schema s = m_ws->schema(db);
        Property id = makeProp("uid", "ID", PropType::UniqueId);
        id.idPrefix = QStringLiteral("BUG");
        s.props.append(id);
        s.props.append(makeProp("who", "Owner", PropType::Person));
        s.props.append(makeProp("by", "Created by", PropType::CreatedBy));
        m_ws->setSchema(db, s);
        QCOMPARE(m_ws->props(a).value("uid").toInt(), 1);
        QCOMPARE(m_ws->props(b).value("uid").toInt(), 2);
        const QString c = m_ws->createRow(db, "Third");
        QCOMPARE(m_ws->props(c).value("uid").toInt(), 3);
        const Property *p = m_ws->lookupSchema(db)->prop("uid");
        QCOMPARE(displayValue(*p, m_ws->props(c).value("uid")), QStringLiteral("BUG-3"));
        QCOMPARE(parseValue(id, "BUG-12").toInt(), 12);

        m_ws->setPeople({{"me", "Ada"}, {"u2", "Grace"}});
        QCOMPARE(m_ws->personName("u2"), QStringLiteral("Grace"));
        m_ws->setProp(a, "who", QJsonArray{"me", "u2"});
        const Property *who = m_ws->lookupSchema(db)->prop("who");
        QCOMPARE(displayValue(*who, m_ws->props(a).value("who")), QStringLiteral("Ada, Grace"));
        const Property *by = m_ws->lookupSchema(db)->prop("by");
        QCOMPARE(displayValue(*by, rowValue(*by, RowRef{m_ws->page(a), m_ws->props(a)})), QStringLiteral("Ada"));
        FilterRule f{"who", "contains", "u2"};
        QVERIFY(matchesFilter(*who, f, m_ws->props(a).value("who")));
        QVERIFY(!matchesFilter(*who, f, m_ws->props(b).value("who")));
    }

    void advancedFilters()
    {
        Schema s = Schema::basic();
        Property status = makeProp("st", "Status", PropType::Status);
        status.options = {{"todo", "Not started", 0, 0}, {"doing", "In progress", 1, 1}, {"done", "Done", 2, 2}};
        s.props.append(status);
        s.props.append(makeProp("n", "Points", PropType::Number));
        s.props.append(makeProp("d", "Date", PropType::Date));
        const QString db = m_ws->createDatabase({}, "Board", s);
        const QString r1 = m_ws->createRow(db, "One", {{"st", "todo"}, {"n", 1.0}, {"d", "2026-10-01"}});
        const QString r2 = m_ws->createRow(db, "Two", {{"st", "doing"}, {"n", 5.0}, {"d", "2026-10-10/2026-10-20"}});
        const QString r3 = m_ws->createRow(db, "Three", {{"st", "done"}, {"n", 8.0}});
        const Schema live = m_ws->schema(db);
        QList<RowRef> rows;
        for (const QString &id : {r1, r2, r3})
            rows.append({m_ws->page(id), m_ws->props(id)});
        auto ids = [&](const View &v) {
            QStringList out;
            for (const RowRef &r : applyView(live, v, rows))
                out.append(r.meta->title);
            return out.join(',');
        };
        View v = live.views.first();
        // (Points > 4 OR Status is To-do) AND Title doesn't contain "Three"
        FilterRule group;
        group.group = true;
        group.any = true;
        group.rules = {FilterRule{"n", "gt", 4.0}, FilterRule{"st", "is", "todo"}};
        v.filters = {group, FilterRule{"title", "notcontains", "Three"}};
        QCOMPARE(ids(v), QStringLiteral("One,Two"));
        v.filterAny = true; // either side
        QCOMPARE(ids(v), QStringLiteral("One,Two,Three"));
        v.filterAny = false;
        v.filters = {FilterRule{"st", "ingroup", 2}};
        QCOMPARE(ids(v), QStringLiteral("Three"));
        v.filters = {FilterRule{"st", "is", QJsonArray{"todo", "done"}}};
        QCOMPARE(ids(v), QStringLiteral("One,Three"));
        // Ranges match every day they cover.
        v.filters = {FilterRule{"d", "is", "2026-10-15"}};
        QCOMPARE(ids(v), QStringLiteral("Two"));
        v.filters = {FilterRule{"d", "between", QJsonArray{"2026-09-30", "2026-10-02"}}};
        QCOMPARE(ids(v), QStringLiteral("One"));
        v.filters = {FilterRule{"d", "onorafter", "2026-10-20"}};
        QCOMPARE(ids(v), QStringLiteral("Two"));
        v.filters = {FilterRule{"d", "empty", {}}};
        QCOMPARE(ids(v), QStringLiteral("Three"));
        // An empty group or a rule without a value hides nothing.
        FilterRule emptyGroup;
        emptyGroup.group = true;
        v.filters = {emptyGroup, FilterRule{"n", "gt", QJsonValue()}};
        QCOMPARE(ids(v), QStringLiteral("One,Two,Three"));
        QCOMPARE(filterRuleCount({group, FilterRule{"n", "gt", 1.0}}), 3);
        // Round trip through JSON keeps the tree.
        Schema withFilters = live;
        withFilters.views.first().filters = {group};
        withFilters.views.first().filterAny = true;
        const Schema back = Schema::fromJson(withFilters.toJson());
        QCOMPARE(back.views.first().filters.size(), 1);
        QVERIFY(back.views.first().filters.first().group);
        QVERIFY(back.views.first().filters.first().any);
        QCOMPARE(back.views.first().filters.first().rules.size(), 2);
        QVERIFY(back.views.first().filterAny);
    }

    void groupingAndCalculations()
    {
        Schema s = Schema::basic();
        Property sel = makeProp("sel", "Stage", PropType::Select);
        sel.options = {{"a", "Alpha", 1, 0}, {"b", "Beta", 2, 0}, {"c", "Gamma", 3, 0}};
        s.props.append(sel);
        Property tags = makeProp("tg", "Labels", PropType::MultiSelect);
        tags.options = {{"x", "X", 0, 0}, {"y", "Y", 0, 0}};
        s.props.append(tags);
        s.props.append(makeProp("n", "Points", PropType::Number));
        s.props.append(makeProp("ok", "OK", PropType::Checkbox));
        s.props.append(makeProp("d", "When", PropType::Date));
        const QString db = m_ws->createDatabase({}, "G", s);
        m_ws->createRow(db, "r1", {{"sel", "a"}, {"tg", QJsonArray{"x", "y"}}, {"n", 1.0}, {"ok", true}, {"d", "2019-01-05"}});
        m_ws->createRow(db, "r2", {{"sel", "a"}, {"tg", QJsonArray{"y"}}, {"n", 4.0}, {"d", "2019-01-20"}});
        m_ws->createRow(db, "r3", {{"n", 25.0}, {"d", "2019-03-02"}});
        m_ws->createRow(db, "r4", {{"sel", "c"}});
        const Schema live = m_ws->schema(db);
        QList<RowRef> rows;
        for (const PageMeta *m : m_ws->rows(db))
            rows.append({m, m_ws->props(m->id)});

        QList<RowGroup> g = groupRows(live, *live.prop("sel"), rows);
        QCOMPARE(g.size(), 4); // No Stage, Alpha, Beta, Gamma
        QCOMPARE(g[0].label, QStringLiteral("No Stage"));
        QCOMPARE(g[1].rows.size(), 2);
        QCOMPARE(g[2].rows.size(), 0);
        QCOMPARE(groupRows(live, *live.prop("sel"), rows, {}, 10, true).size(), 3);
        g = groupRows(live, *live.prop("tg"), rows);
        int y = -1;
        for (int i = 0; i < g.size(); ++i)
            if (g[i].key == QLatin1String("y"))
                y = i;
        QVERIFY(y >= 0);
        QCOMPARE(g[y].rows.size(), 2); // r1 is in both X and Y
        g = groupRows(live, *live.prop("d"), rows, "month");
        QCOMPARE(g.size(), 3);
        QCOMPARE(g[0].label, QStringLiteral("January 2019"));
        QCOMPARE(g[0].rows.size(), 2);
        QCOMPARE(g.last().key, QString()); // no date last
        g = groupRows(live, *live.prop("n"), rows, {}, 10);
        QCOMPARE(g[0].label, QStringLiteral("0 – 10"));
        QCOMPARE(g[0].rows.size(), 2);
        QCOMPARE(g[1].label, QStringLiteral("20 – 30"));
        g = groupRows(live, *live.prop("ok"), rows);
        QCOMPARE(g[0].label, QStringLiteral("Checked"));
        QCOMPARE(g[0].rows.size(), 1);
        g = groupRows(live, *live.prop("title"), rows, "alpha");
        QCOMPARE(g.size(), 1);
        QCOMPARE(g[0].label, QStringLiteral("R"));

        const Property &n = *live.prop("n");
        QCOMPARE(calculate(n, rows, "sum"), QStringLiteral("30"));
        QCOMPARE(calculate(n, rows, "avg"), QStringLiteral("10"));
        QCOMPARE(calculate(n, rows, "median"), QStringLiteral("4"));
        QCOMPARE(calculate(n, rows, "min"), QStringLiteral("1"));
        QCOMPARE(calculate(n, rows, "max"), QStringLiteral("25"));
        QCOMPARE(calculate(n, rows, "range"), QStringLiteral("24"));
        QCOMPARE(calculate(n, rows, "empty"), QStringLiteral("1"));
        QCOMPARE(calculate(n, rows, "pctnotempty"), QStringLiteral("75%"));
        QCOMPARE(calculate(*live.prop("ok"), rows, "checked"), QStringLiteral("1"));
        QCOMPARE(calculate(*live.prop("ok"), rows, "pctunchecked"), QStringLiteral("75%"));
        QCOMPARE(calculate(*live.prop("tg"), rows, "values"), QStringLiteral("3"));
        QCOMPARE(calculate(*live.prop("tg"), rows, "unique"), QStringLiteral("2"));
        QCOMPARE(calculate(*live.prop("d"), rows, "earliest"), QStringLiteral("Jan 5, 2019"));
        QCOMPARE(calculate(*live.prop("d"), rows, "latest"), QStringLiteral("Mar 2, 2019"));
        QCOMPARE(calculate(*live.prop("d"), rows, "daterange"), QStringLiteral("56 days"));
        QCOMPARE(calculate(*live.prop("title"), rows, "count"), QStringLiteral("4"));
        QVERIFY(calcFunctions(n).contains("sum"));
        QVERIFY(!calcFunctions(*live.prop("title")).contains("sum"));
        QVERIFY(calcFunctions(*live.prop("ok")).contains("pctchecked"));
    }

    void datesRangesAndPropertyReminders()
    {
        Property d = makeProp("d", "Due", PropType::Date);
        QCOMPARE(parseValue(d, "2026-10-01 → 2026-10-05").toString(), QStringLiteral("2026-10-01/2026-10-05"));
        QCOMPARE(parseValue(d, "October 1, 2026 → October 5, 2026").toString(), QStringLiteral("2026-10-01/2026-10-05"));
        QCOMPARE(parseValue(d, "2026-10-01/2026-10-03").toString(), QStringLiteral("2026-10-01/2026-10-03"));
        QCOMPARE(dateEndOf(QStringLiteral("2026-10-01/2026-10-05#r")).date(), QDate(2026, 10, 5));
        QVERIFY(dateHasReminder(QStringLiteral("2026-10-01#r")));
        QCOMPARE(makeDateValue(QDateTime(QDate(2026, 1, 2), QTime(9, 30)), true, QDateTime(QDate(2026, 1, 2), QTime(11, 0)), true),
                 QStringLiteral("2026-01-02T09:30/2026-01-02T11:00#r"));
        QCOMPARE(formatDateFor(d, "2019-10-01/2019-10-05"), QStringLiteral("Oct 1, 2019 → Oct 5, 2019"));
        {
            // This year's dates leave the year out, unless a range crosses into another.
            const int y = QDate::currentDate().year();
            QCOMPARE(formatDateFor(d, QStringLiteral("%1-03-04").arg(y)), QStringLiteral("Mar 4"));
            QCOMPARE(formatDateFor(d, QStringLiteral("%1-12-30/%2-01-02").arg(y).arg(y + 1)),
                     QStringLiteral("Dec 30, %1 → Jan 2, %2").arg(y).arg(y + 1));
        }
        d.dateFormat = QStringLiteral("dmy");
        d.time24 = true;
        QCOMPARE(formatDateFor(d, "2026-10-01T18:05"), QStringLiteral("01/10/2026 18:05"));
        d.dateFormat = QStringLiteral("relative");
        QCOMPARE(formatDateFor(d, QDate::currentDate().toString(Qt::ISODate)), QStringLiteral("Today"));

        Schema s = Schema::basic();
        s.props.append(makeProp("due", "Due", PropType::Date));
        const QString db = m_ws->createDatabase({}, "Plans", s);
        const QString row = m_ws->createRow(db, "Launch");
        m_ws->setProp(row, "due", QStringLiteral("2030-01-01#r"));
        QList<Workspace::Reminder> rs = m_ws->reminders();
        QCOMPARE(rs.size(), 1);
        QCOMPARE(rs.first().pageId, row);
        QCOMPARE(QDateTime::fromMSecsSinceEpoch(rs.first().at), QDateTime(QDate(2030, 1, 1), QTime(9, 0)));
        m_ws->setReminderDone(rs.first().id, true);
        QCOMPARE(m_ws->reminders().size(), 0);
        QCOMPARE(m_ws->reminders(true).size(), 1);
        m_ws->setProp(row, "due", QStringLiteral("2030-01-01"));
        QCOMPARE(m_ws->reminders(true).size(), 0);
    }

    void schemaJsonKeepsEverything()
    {
        Schema s = Schema::basic();
        Property f = makeProp("f", "F", PropType::Formula);
        f.formula = "1+1";
        f.formulaType = "number";
        f.numberShow = "ring";
        f.numberDivide = 10;
        f.decimals = 2;
        Property r = makeProp("r", "R", PropType::Relation);
        r.relationDb = "db2";
        r.relationPair = "p2";
        r.relationOne = true;
        Property ro = makeProp("ro", "Ro", PropType::Rollup);
        ro.rollupRelation = "r";
        ro.rollupProp = "x";
        ro.rollupFn = "sum";
        Property u = makeProp("u", "U", PropType::UniqueId);
        u.idPrefix = "T";
        Property b = makeProp("b", "B", PropType::Button);
        b.button = QJsonObject{{"label", "Go"}};
        Property dt = makeProp("dt", "Dt", PropType::Date);
        dt.dateFormat = "ymd";
        dt.time24 = true;
        s.props << f << r << ro << u << b << dt;
        View v = Schema::makeView(s, ViewType::Timeline, "TL");
        v.subGroupBy = "r";
        v.groupMode = "week";
        v.hideEmptyGroups = true;
        v.calcs.insert("f", "sum");
        v.wrapCells = true;
        v.openIn = "center";
        v.frozen = 2;
        v.timelineScale = "quarter";
        v.endDateProp = "dt";
        v.chartType = "donut";
        v.chartY = "sum:f";
        v.loadLimit = 25;
        v.showWeekends = false;
        s.views.append(v);
        s.locked = true;
        s.subItemsProp = "r";
        s.defaultTemplate = "tpl";
        const Schema t = Schema::fromJson(s.toJson());
        QCOMPARE(t.prop("f")->formula, QStringLiteral("1+1"));
        QCOMPARE(t.prop("f")->numberShow, QStringLiteral("ring"));
        QCOMPARE(t.prop("f")->decimals, 2);
        QCOMPARE(t.prop("r")->relationPair, QStringLiteral("p2"));
        QVERIFY(t.prop("r")->relationOne);
        QCOMPARE(t.prop("ro")->rollupFn, QStringLiteral("sum"));
        QCOMPARE(t.prop("u")->idPrefix, QStringLiteral("T"));
        QCOMPARE(t.prop("b")->button.value("label").toString(), QStringLiteral("Go"));
        QCOMPARE(t.prop("dt")->dateFormat, QStringLiteral("ymd"));
        const View &tv = t.views.last();
        QCOMPARE(tv.type, ViewType::Timeline);
        QCOMPARE(tv.subGroupBy, QStringLiteral("r"));
        QCOMPARE(tv.groupMode, QStringLiteral("week"));
        QVERIFY(tv.hideEmptyGroups);
        QCOMPARE(tv.calcs.value("f"), QStringLiteral("sum"));
        QVERIFY(tv.wrapCells);
        QCOMPARE(tv.openIn, QStringLiteral("center"));
        QCOMPARE(tv.frozen, 2);
        QCOMPARE(tv.timelineScale, QStringLiteral("quarter"));
        QCOMPARE(tv.endDateProp, QStringLiteral("dt"));
        QCOMPARE(tv.chartType, QStringLiteral("donut"));
        QCOMPARE(tv.chartY, QStringLiteral("sum:f"));
        QCOMPARE(tv.loadLimit, 25);
        QVERIFY(!tv.showWeekends);
        QVERIFY(t.locked);
        QCOMPARE(t.subItemsProp, QStringLiteral("r"));
        QCOMPARE(t.defaultTemplate, QStringLiteral("tpl"));
        for (ViewType vt : {ViewType::Timeline, ViewType::Chart, ViewType::Feed, ViewType::Form})
            QCOMPARE(viewTypeFromKey(viewTypeKey(vt)), vt);
        for (PropType pt : userPropTypes())
            QCOMPARE(propTypeFromKey(propTypeKey(pt)), pt);
    }

    // ---- workspace storage ---------------------------------------------------------

    void syncedBlocks()
    {
        const QString a = m_ws->createPage({}, "A");
        const QString b = m_ws->createPage({}, "B");
        Block before = Block::make(BlockType::Paragraph, "intro");
        Block s1 = Block::make(BlockType::Heading2, "Shared");
        Block s2 = Block::make(BlockType::Bullet, "point", 1);
        const QString syncId = m_ws->createSynced(a, {});
        s1.syncId = syncId;
        s2.syncId = syncId;
        m_ws->setContent(a, {before, s1, s2});
        // B shows the same region (as a pasted reference).
        Block ref;
        ref.type = BlockType::Synced;
        ref.syncId = syncId;
        m_ws->setContent(b, {ref, Block::make(BlockType::Paragraph, "after")});
        QList<Block> bc = m_ws->content(b);
        QCOMPARE(bc.size(), 3);
        QCOMPARE(bc[0].plainText(), QStringLiteral("Shared"));
        QCOMPARE(bc[0].syncId, syncId);
        QCOMPARE(bc[1].depth, 1);
        // Editing the copy in B changes A too.
        bc[0].setPlainText("Shared, edited");
        bc.insert(2, Block::make(BlockType::Bullet, "new point", 1));
        bc[2].syncId = syncId;
        m_ws->setContent(b, bc);
        const QList<Block> ac = m_ws->content(a);
        QCOMPARE(ac.size(), 4);
        QCOMPARE(ac[1].plainText(), QStringLiteral("Shared, edited"));
        QCOMPARE(ac[3].plainText(), QStringLiteral("new point"));
        QCOMPARE(m_ws->syncedOrigin(syncId), a);
        QStringList pages = m_ws->syncedPages(syncId);
        pages.sort();
        QStringList expected{a, b};
        expected.sort();
        QCOMPARE(pages, expected);
        // Search sees the synced text on both pages.
        QCOMPARE(m_ws->search("edited").size(), 2);
        // Synced blocks inside columns work too.
        Block cols;
        cols.type = BlockType::ColumnList;
        Block inCol = Block::make(BlockType::Paragraph, "Shared, edited");
        inCol.syncId = syncId;
        cols.columns = {{inCol}, {Block::make(BlockType::Paragraph, "right")}};
        cols.widths = {0.5, 0.5};
        m_ws->setContent(b, {cols});
        const QList<Block> colsBack = m_ws->content(b);
        QCOMPARE(colsBack.first().columns.first().first().syncId, syncId);
        QCOMPARE(m_ws->content(a).size(), 2); // the region now holds one block
    }

    void commentsAndThreads()
    {
        const QString p = m_ws->createPage({}, "Discuss");
        QSignalSpy spy(m_ws, &Workspace::commentsChanged);
        const QString t1 = m_ws->addComment(p, {}, {}, "Page-level thought");
        const QString t2 = m_ws->addComment(p, "blk1", "quoted text", "Inline note");
        const QString reply = m_ws->addComment(p, "blk1", "quoted text", "A reply", t2);
        QCOMPARE(spy.count(), 3);
        QList<Workspace::Comment> cs = m_ws->comments(p);
        QCOMPARE(cs.size(), 3);
        QCOMPARE(cs[2].thread, t2);
        QCOMPARE(m_ws->openThreadCount(p), 2);
        m_ws->resolveThread(t2, true);
        QCOMPARE(m_ws->openThreadCount(p), 1);
        // A new reply reopens the thread.
        m_ws->addComment(p, "blk1", {}, "Wait", t2);
        QCOMPARE(m_ws->openThreadCount(p), 2);
        m_ws->editComment(reply, "Edited reply");
        cs = m_ws->comments(p);
        QCOMPARE(cs[2].body, QStringLiteral("Edited reply"));
        QVERIFY(cs[2].edited > 0);
        m_ws->deleteComment(reply);
        QCOMPARE(m_ws->comments(p).size(), 3);
        m_ws->deleteComment(t2); // the thread goes with its first comment
        QCOMPARE(m_ws->comments(p).size(), 1);
        QCOMPARE(m_ws->comments(p).first().id, t1);
    }

    void versionHistory()
    {
        const QString p = m_ws->createPage({}, "Draft", {Block::make(BlockType::Paragraph, "v1")});
        QVERIFY(m_ws->snapshot(p, true));
        QVERIFY(!m_ws->snapshot(p, true)); // nothing changed
        m_ws->setContent(p, {Block::make(BlockType::Paragraph, "v2")});
        QVERIFY(!m_ws->snapshot(p)); // too soon
        QVERIFY(m_ws->snapshot(p, false, 0));
        m_ws->setTitle(p, "Final");
        m_ws->setContent(p, {Block::make(BlockType::Paragraph, "v3")});
        QList<Workspace::Version> vs = m_ws->versions(p);
        QCOMPARE(vs.size(), 2);
        QString title;
        QCOMPARE(m_ws->versionContent(vs.last().id, &title).first().plainText(), QStringLiteral("v1"));
        QCOMPARE(title, QStringLiteral("Draft"));
        m_ws->nameVersion(vs.last().id, "First draft");
        m_ws->restoreVersion(vs.last().id);
        QCOMPARE(m_ws->content(p).first().plainText(), QStringLiteral("v1"));
        QCOMPARE(m_ws->page(p)->title, QStringLiteral("Draft"));
        // The state before the restore was kept.
        vs = m_ws->versions(p);
        QCOMPARE(vs.size(), 3);
        QCOMPARE(m_ws->versionContent(vs.first().id).first().plainText(), QStringLiteral("v3"));
        bool named = false;
        for (const auto &v : vs)
            named = named || v.name == QLatin1String("First draft");
        QVERIFY(named);
    }

    void dateMentionReminders()
    {
        const QString p = m_ws->createPage({}, "Agenda");
        Block b = Block::make(BlockType::Todo, "Call the observatory ");
        Span date;
        date.text = QString(QChar(0xFFFC));
        date.date = QStringLiteral("2031-05-04T15:00");
        date.remind = true;
        b.spans.append(date);
        m_ws->setContent(p, {b});
        QList<Workspace::Reminder> rs = m_ws->reminders();
        QCOMPARE(rs.size(), 1);
        QCOMPARE(rs.first().text, QStringLiteral("Call the observatory"));
        QCOMPARE(QDateTime::fromMSecsSinceEpoch(rs.first().at), QDateTime(QDate(2031, 5, 4), QTime(15, 0)));
        m_ws->trash(p);
        QCOMPARE(m_ws->reminders().size(), 0);
        m_ws->restore(p);
        m_ws->setContent(p, {Block::make(BlockType::Paragraph, "no more")});
        QCOMPARE(m_ws->reminders(true).size(), 0);
    }

    void databaseTemplates()
    {
        Schema s = Schema::basic();
        s.props.append(makeProp("n", "Points", PropType::Number));
        const QString db = m_ws->createDatabase({}, "Sprints", s);
        const QString tpl = m_ws->createTemplate(db, "Bug report");
        m_ws->setProp(tpl, "n", 3.0);
        m_ws->setContent(tpl, {Block::make(BlockType::Heading2, "Steps"), Block::make(BlockType::Numbered, "")});
        m_ws->setIcon(tpl, "glyph:comet");
        QCOMPARE(m_ws->templates(db).size(), 1);
        QVERIFY(m_ws->rows(db).isEmpty());
        QVERIFY(m_ws->children(db).isEmpty());
        QVERIFY(!m_ws->hasChildren(db));
        const QString row = m_ws->createRowFromTemplate(db, tpl);
        QCOMPARE(m_ws->rows(db).size(), 1);
        QCOMPARE(m_ws->props(row).value("n").toDouble(), 3.0);
        QCOMPARE(m_ws->content(row).size(), 2);
        QVERIFY(m_ws->content(row).first().id != m_ws->content(tpl).first().id);
        QCOMPARE(m_ws->page(row)->icon, QStringLiteral("glyph:comet"));
        QCOMPARE(m_ws->page(row)->title, QStringLiteral("Bug report"));
        // Templates never show up in search.
        for (const auto &h : m_ws->search("Bug report"))
            QVERIFY(h.id != tpl);
    }

    void searchFiltersAndSorts()
    {
        const QString top = m_ws->createPage({}, "Galaxies");
        const QString child = m_ws->createPage(top, "Andromeda notes", {Block::make(BlockType::Paragraph, "spiral arms")});
        QTest::qWait(3);
        const QString other = m_ws->createPage({}, "Spiral staircase");
        QTest::qWait(3);
        const QString third = m_ws->createPage({}, "Misc", {Block::make(BlockType::Paragraph, "a spiral")});
        Workspace::SearchOptions o;
        QCOMPARE(m_ws->search("spiral", o).size(), 3);
        o.titleOnly = true;
        QCOMPARE(m_ws->search("spiral", o).size(), 1);
        QCOMPARE(m_ws->search("spiral", o).first().id, other);
        o.titleOnly = false;
        o.inPage = top;
        QCOMPARE(m_ws->search("spiral", o).size(), 1);
        QCOMPARE(m_ws->search("spiral", o).first().id, child);
        o.inPage.clear();
        o.sort = Workspace::SearchOptions::CreatedNewest;
        QCOMPARE(m_ws->search("spiral", o).first().id, third);
        o.sort = Workspace::SearchOptions::CreatedOldest;
        QCOMPARE(m_ws->search("spiral", o).first().id, child);
        o.createdAfter = m_ws->page(other)->created;
        QCOMPARE(m_ws->search("spiral", o).size(), 2);
    }

    // ---- storage (format 2) ----------------------------------------------------

    void savesWriteOnlyChangedBlocks()
    {
        QList<Block> blocks;
        for (int i = 0; i < 50; ++i)
            blocks << Block::make(BlockType::Paragraph, QStringLiteral("line %1").arg(i));
        const QString p = m_ws->createPage({}, "Long", blocks);
        blocks = m_ws->content(p);
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM blocks WHERE page_id=?", {p}).toInt(), 50);
        // Editing one block rewrites only its row.
        const QByteArray before = scalar(dbFile(), "SELECT data FROM blocks WHERE page_id=? AND id=?", {p, blocks[10].id}).toByteArray();
        blocks[20].setPlainText("changed");
        m_ws->setContent(p, blocks);
        QCOMPARE(scalar(dbFile(), "SELECT data FROM blocks WHERE page_id=? AND id=?", {p, blocks[10].id}).toByteArray(), before);
        QVERIFY(scalar(dbFile(), "SELECT data FROM blocks WHERE page_id=? AND id=?", {p, blocks[20].id}).toByteArray().contains("changed"));
        // Order survives inserts in the middle, many at the same spot, moves and deletes…
        for (int i = 0; i < 70; ++i)
            blocks.insert(25, Block::make(BlockType::Paragraph, QStringLiteral("wedge %1").arg(i)));
        blocks.move(0, 60);
        blocks.removeAt(3);
        m_ws->setContent(p, blocks);
        QStringList expect;
        for (const Block &b : blocks)
            expect << b.plainText();
        // …also when read back from the file, not the cache.
        const QString dir = m_ws->directory();
        m_ws->close();
        QVERIFY(m_ws->open(dir));
        QStringList read;
        for (const Block &b : m_ws->content(p))
            read << b.plainText();
        QCOMPARE(read, expect);
        QCOMPARE(read.size(), 119);
    }

    void searchUsesTheIndex()
    {
        const QString a = m_ws->createPage({}, "Astronomy", {Block::make(BlockType::Paragraph, "The heliopause marks the edge."),
                                                            Block::make(BlockType::Paragraph, "Café near the observatory")});
        const QString b = m_ws->createPage({}, "Biology", {Block::make(BlockType::Paragraph, "Cells and membranes")});
        // Any part of a word, several terms (all must match), accents ignored.
        QCOMPARE(m_ws->search("liopau").size(), 1);
        QCOMPARE(m_ws->search("liopau").first().id, a);
        QVERIFY(m_ws->search("liopau").first().snippet.contains("heliopause"));
        QCOMPARE(m_ws->search("edge helio").size(), 1);
        QCOMPARE(m_ws->search("edge cells").size(), 0);
        QCOMPARE(m_ws->search("cafe").size(), 1);
        // One- and two-letter terms still work.
        QCOMPARE(m_ws->search("me").size(), 1); // "membranes"
        QCOMPARE(m_ws->search("th").first().id, a);
        // Edits are found right away (the index catches up before a search).
        m_ws->setContent(b, {Block::make(BlockType::Paragraph, "Mitochondria everywhere")});
        QCOMPARE(m_ws->search("mitochond").size(), 1);
        QCOMPARE(m_ws->search("membranes").size(), 0);
        // Trashed pages don't show; deleted ones leave no search text behind.
        m_ws->trash(b);
        QCOMPARE(m_ws->search("mitochond").size(), 0);
        m_ws->deleteForever(b);
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM search_map WHERE page_id=?", {b}).toInt(), 0);
    }

    void deleteForeverLeavesNothing()
    {
        const QString p = m_ws->createPage({}, "Secret plans", {Block::make(BlockType::Paragraph, "v1")});
        QVERIFY(m_ws->snapshot(p, true));
        m_ws->setContent(p, {Block::make(BlockType::Paragraph, "v2")});
        QVERIFY(m_ws->snapshot(p, true));
        m_ws->addComment(p, {}, {}, "A thought");
        Span when;
        when.text = QString(QChar::ObjectReplacementCharacter);
        when.date = "2030-01-01";
        when.remind = true;
        Block withDate = Block::make(BlockType::Paragraph);
        withDate.spans = {when};
        m_ws->setContent(p, {withDate});
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM reminders WHERE page_id=?", {p}).toInt(), 1);
        m_ws->search("anything"); // index it
        m_ws->trash(p);
        m_ws->deleteForever(p);
        for (const char *table : {"versions", "comments", "reminders", "blocks", "synced_refs", "search_map"})
            QCOMPARE(scalar(dbFile(), QStringLiteral("SELECT COUNT(*) FROM %1 WHERE page_id=?").arg(QLatin1String(table)), {p}).toInt(), 0);
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM pages WHERE id=?", {p}).toInt(), 0);
    }

    void cleanUpRemovesLeftovers()
    {
        const QString p = m_ws->createPage({}, "Keep", {Block::make(BlockType::Paragraph, "kept")});
        m_ws->snapshot(p, true);
        // Rows left behind by an older version of the app, for a page that's gone.
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("orphans"));
            db.setDatabaseName(dbFile());
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("INSERT INTO versions(page_id, created, title, content) VALUES('gone', 1, 'x', '[]')"));
            QVERIFY(q.exec("INSERT INTO comments(id, thread, page_id, body) VALUES('c1', 'c1', 'gone', 'hi')"));
            QVERIFY(q.exec("INSERT INTO blocks(page_id, id, sort, data) VALUES('gone', 'b', 1, '{}')"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("orphans"));
        m_ws->cleanUp();
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM versions WHERE page_id='gone'").toInt(), 0);
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM comments WHERE page_id='gone'").toInt(), 0);
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM blocks WHERE page_id='gone'").toInt(), 0);
        QCOMPARE(scalar(dbFile(), "SELECT COUNT(*) FROM versions WHERE page_id=?", {p}).toInt(), 1);
        QCOMPARE(m_ws->content(p).first().plainText(), QStringLiteral("kept"));
    }

    void versionsAreCompressed()
    {
        QList<Block> blocks;
        for (int i = 0; i < 200; ++i)
            blocks << Block::make(BlockType::Paragraph, QStringLiteral("The same sentence, again and again, number %1.").arg(i));
        const QString p = m_ws->createPage({}, "Repetitive", blocks);
        QVERIFY(m_ws->snapshot(p, true));
        const qint64 stored = scalar(dbFile(), "SELECT length(content) FROM versions WHERE page_id=?", {p}).toLongLong();
        const qint64 raw = blocksToJsonString(m_ws->content(p)).toUtf8().size();
        QVERIFY2(stored * 3 < raw, qPrintable(QStringLiteral("%1 vs %2").arg(stored).arg(raw)));
        QCOMPARE(m_ws->versionContent(m_ws->versions(p).first().id).size(), 200);
    }

    // A workspace saved by the first format (content as one JSON text per
    // page) opens, gets backed up, and comes out the same.
    void migratesFormatOne()
    {
        const QString dir = m_dir.path() + QStringLiteral("/old-format");
        QDir().mkpath(dir);
        const QString file = dir + QStringLiteral("/workspace.db");
        const QString page = QStringLiteral("p1"), other = QStringLiteral("p2"), sync = QStringLiteral("s1");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1"));
            db.setDatabaseName(file);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE pages(id TEXT PRIMARY KEY, parent_id TEXT NOT NULL DEFAULT '', kind INTEGER NOT NULL DEFAULT 0, "
                           "title TEXT NOT NULL DEFAULT '', icon TEXT NOT NULL DEFAULT '', cover TEXT NOT NULL DEFAULT '', "
                           "cover_pos REAL NOT NULL DEFAULT 0.5, content TEXT NOT NULL DEFAULT '[]', plain TEXT NOT NULL DEFAULT '', "
                           "props TEXT NOT NULL DEFAULT '{}', schema TEXT NOT NULL DEFAULT '', options TEXT NOT NULL DEFAULT '{}', "
                           "sort REAL NOT NULL DEFAULT 0, favorite REAL NOT NULL DEFAULT 0, created INTEGER NOT NULL DEFAULT 0, "
                           "updated INTEGER NOT NULL DEFAULT 0, visited INTEGER NOT NULL DEFAULT 0, trashed INTEGER NOT NULL DEFAULT 0)"));
            QVERIFY(q.exec("CREATE TABLE links(src TEXT NOT NULL, dst TEXT NOT NULL, PRIMARY KEY(src, dst))"));
            QVERIFY(q.exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT)"));
            QVERIFY(q.exec("CREATE TABLE synced(id TEXT PRIMARY KEY, origin TEXT NOT NULL DEFAULT '', content TEXT NOT NULL DEFAULT '[]', "
                           "updated INTEGER NOT NULL DEFAULT 0)"));
            QVERIFY(q.exec("CREATE TABLE comments(id TEXT PRIMARY KEY, thread TEXT NOT NULL, page_id TEXT NOT NULL, block_id TEXT NOT NULL "
                           "DEFAULT '', quote TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '', created INTEGER NOT NULL DEFAULT 0, "
                           "edited INTEGER NOT NULL DEFAULT 0, resolved INTEGER NOT NULL DEFAULT 0)"));
            QVERIFY(q.exec("CREATE TABLE versions(id INTEGER PRIMARY KEY AUTOINCREMENT, page_id TEXT NOT NULL, created INTEGER NOT NULL, "
                           "title TEXT NOT NULL DEFAULT '', content TEXT NOT NULL DEFAULT '[]', name TEXT NOT NULL DEFAULT '')"));
            QVERIFY(q.exec("CREATE TABLE reminders(id TEXT PRIMARY KEY, page_id TEXT NOT NULL, text TEXT NOT NULL DEFAULT '', at INTEGER "
                           "NOT NULL, done INTEGER NOT NULL DEFAULT 0)"));
            QVERIFY(q.exec("INSERT INTO meta(key, value) VALUES('schema', '1')"));
            Block shared;
            shared.type = BlockType::Synced;
            shared.syncId = sync;
            const QList<Block> content = {Block::make(BlockType::Heading1, "Orbits"), Block::make(BlockType::Paragraph, "Kepler's laws"),
                                          Block::make(BlockType::Paragraph, "no id here"), shared};
            QList<Block> withIds = content;
            withIds[0].id = "b0";
            withIds[1].id = "b1";
            withIds[3].id = "b0"; // a repeated id gets a new one
            const QString json = blocksToJsonString(withIds);
            q.prepare("INSERT INTO pages(id, title, content, plain, created, updated) VALUES(?, 'Physics', ?, ?, 1, 1)");
            q.addBindValue(page);
            q.addBindValue(json);
            q.addBindValue(QStringLiteral("Orbits\nKepler's laws\nno id here\nShared text"));
            QVERIFY(q.exec());
            QVERIFY(q.exec("INSERT INTO pages(id, title, content, plain, created, updated, sort) VALUES('p2', 'Empty', '[]', '', 2, 2, 2)"));
            q.prepare("INSERT INTO synced(id, origin, content) VALUES(?, ?, ?)");
            q.addBindValue(sync);
            q.addBindValue(page);
            q.addBindValue(blocksToJsonString({Block::make(BlockType::Paragraph, "Shared text")}));
            QVERIFY(q.exec());
            q.prepare("INSERT INTO versions(page_id, created, title, content) VALUES(?, 5, 'Physics (old)', ?)");
            q.addBindValue(page);
            q.addBindValue(blocksToJsonString({Block::make(BlockType::Paragraph, "first draft")}));
            QVERIFY(q.exec());
            QVERIFY(q.exec("INSERT INTO comments(id, thread, page_id, body, created) VALUES('c1', 'c1', 'p1', 'nice', 3)"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("v1"));

        Workspace ws;
        QVERIFY2(ws.open(dir), qPrintable(ws.lastError()));
        QCOMPARE(scalar(file, "PRAGMA user_version").toInt(), Workspace::formatVersion());
        QVERIFY(QFileInfo::exists(dir + QStringLiteral("/backups/workspace-before-format-2.db")));
        const QList<Block> c = ws.content(page);
        QCOMPARE(c.size(), 4); // the synced region expands to its one block
        QCOMPARE(c[0].plainText(), QStringLiteral("Orbits"));
        QCOMPARE(c[1].plainText(), QStringLiteral("Kepler's laws"));
        QCOMPARE(c[3].plainText(), QStringLiteral("Shared text"));
        QCOMPARE(c[3].syncId, sync);
        QSet<QString> ids;
        for (const Block &b : c.mid(0, 3))
            ids.insert(b.id);
        QCOMPARE(ids.size(), 3);
        QVERIFY(!ids.contains(QString()));
        QCOMPARE(ws.syncedPages(sync), QStringList{page});
        QCOMPARE(ws.search("kepler").size(), 1);
        QCOMPARE(ws.search("shared text").size(), 1);
        QCOMPARE(ws.comments(page).size(), 1);
        const QList<Workspace::Version> vs = ws.versions(page);
        QCOMPARE(vs.size(), 1);
        QString title;
        QCOMPARE(ws.versionContent(vs.first().id, &title).first().plainText(), QStringLiteral("first draft"));
        QCOMPARE(title, QStringLiteral("Physics (old)"));
        QCOMPARE(scalar(file, "SELECT zip FROM versions").toInt(), 1);
        // The page differs from its last version: the next snapshot keeps it.
        QVERIFY(ws.snapshot(page, false, 0));
        // Saving after the upgrade works as usual.
        QList<Block> edited = c;
        edited[1].setPlainText("Newton's laws");
        ws.setContent(page, edited);
        QCOMPARE(ws.content(page)[1].plainText(), QStringLiteral("Newton's laws"));
        QCOMPARE(ws.content(other).size(), 0);
        ws.close();
        // Opening again needs no upgrade (and no second backup).
        QVERIFY(ws.open(dir));
        QCOMPARE(QDir(dir + QStringLiteral("/backups")).entryList({QStringLiteral("workspace-before-format-*")}).size(), 1);
        QCOMPARE(ws.content(page)[1].plainText(), QStringLiteral("Newton's laws"));
    }

    void refusesNewerFormats()
    {
        const QString dir = m_dir.path() + QStringLiteral("/future-format");
        {
            Workspace ws;
            QVERIFY(ws.open(dir));
        }
        scalar(dir + QStringLiteral("/workspace.db"), QStringLiteral("PRAGMA user_version=%1").arg(Workspace::formatVersion() + 1));
        Workspace ws;
        QVERIFY(!ws.open(dir));
        QVERIFY(ws.lastError().contains(QStringLiteral("newer")));
    }

    // ---- speed paths (they must keep the same results) ------------------------

    void bulkRowsKeepOrderAndAnnounceOnce()
    {
        const QString db = m_ws->createDatabase({}, "Bulk", Schema::basic());
        QSignalSpy tree(m_ws, &Workspace::treeChanged);
        QSignalSpy props(m_ws, &Workspace::propsChanged);
        QStringList made;
        m_ws->batch([&] {
            for (int i = 0; i < 300; ++i)
                made << m_ws->createRow(db, QStringLiteral("Row %1").arg(i));
        });
        // One announcement per kind, not one per row.
        QCOMPARE(tree.count(), 1);
        QCOMPARE(props.count(), 1);
        QStringList order;
        for (const PageMeta *m : m_ws->rows(db))
            order << m->id;
        QCOMPARE(order, made);
        // The index stays right after moves and more inserts.
        m_ws->moveRow(made[5], made[0]);
        const QString late = m_ws->createRow(db, "Late");
        const QList<const PageMeta *> rows = m_ws->rows(db);
        QCOMPARE(rows.first()->id, made[5]);
        QCOMPARE(rows.last()->id, late);
        QCOMPARE(rows.size(), 301);
        // Pages after a database keep their place among its siblings.
        const QString page = m_ws->createPage({}, "After");
        QCOMPARE(m_ws->children({}).last()->id, page);
        // Every row's properties load in one go and match single reads.
        m_ws->setProp(made[7], "tags", QJsonArray{"x"});
        m_ws->preloadProps(db);
        QCOMPARE(m_ws->props(made[7]).value("tags").toArray().size(), 1);
    }

    void sortsByDatesTextAndOptions()
    {
        Schema s;
        s.props << makeProp("title", "Name", PropType::Title) << makeProp("due", "Due", PropType::Date);
        Property pr = makeProp("prio", "Priority", PropType::Select);
        pr.options = {{"lo", "Low", 0, 0}, {"hi", "High", 0, 0}};
        s.props << pr;
        s.views << Schema::makeView(s, ViewType::Table);
        const QString db = m_ws->createDatabase({}, "Sorts", s);
        const QString a = m_ws->createRow(db, "beta", {{"due", "2026-03-01"}, {"prio", "hi"}});
        const QString b = m_ws->createRow(db, "Alpha", {{"due", "2026-01-15T09:30"}, {"prio", "lo"}});
        const QString c = m_ws->createRow(db, "gamma", {{"prio", "hi"}});
        const QString d = m_ws->createRow(db, "delta", {{"due", "2026-01-15"}});
        QList<RowRef> rows;
        for (const PageMeta *m : m_ws->rows(db))
            rows.append({m, m_ws->props(m->id)});
        auto ids = [&](const View &v) {
            QStringList out;
            for (const RowRef &r : applyView(s, v, rows))
                out << r.meta->id;
            return out;
        };
        View v = s.views.first();
        v.sorts = {{"due", false}};
        // Dates in order, a date before a time on the same day, empty last.
        QCOMPARE(ids(v), QStringList({d, b, a, c}));
        v.sorts = {{"due", true}};
        QCOMPARE(ids(v), QStringList({a, b, d, c}));
        // Text ignores case.
        v.sorts = {{"title", false}};
        QCOMPARE(ids(v), QStringList({b, a, d, c}));
        // Options sort in their own order, then by the next rule.
        v.sorts = {{"prio", false}, {"title", true}};
        QCOMPARE(ids(v), QStringList({b, c, a, d}));
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    DbTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_db.moc"
