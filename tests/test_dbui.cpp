// Database layouts and the database body, driven with real mouse events
// (offscreen): tables, boards, calendars, timelines, charts, forms, filters,
// linked views, inline databases in pages and the row peek.
#include "core/Settings.h"
#include "core/Templates.h"
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "ui/AnimatedMedia.h"
#include "ui/Plurals.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "views/DatabaseBody.h"
#include "views/DatabaseView.h"
#include "views/MainWindow.h"
#include "views/PageView.h"
#include "views/Peek.h"

#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QDirIterator>
#include <QFontDatabase>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSet>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>

class DbUiTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Workspace *m_ws = nullptr;
    QString m_db;
    QStringList m_rows; // Alpha, Beta, Gamma, Delta, Epsilon
    DatabaseBody *m_body = nullptr;
    QDate m_monday;

    void settle(int ms = 60) { QTest::qWait(ms); }

    static void send(QWidget *w, QEvent::Type type, const QPointF &pos, Qt::MouseButton button, Qt::MouseButtons buttons,
                     Qt::KeyboardModifiers mods = {})
    {
        QMouseEvent e(type, pos, w->mapToGlobal(pos), button, buttons, mods);
        QApplication::sendEvent(w, &e);
    }
    static void hover(QWidget *w, const QPointF &pos) { send(w, QEvent::MouseMove, pos, Qt::NoButton, Qt::NoButton); }
    static void click(QWidget *w, const QPointF &pos, Qt::KeyboardModifiers mods = {})
    {
        hover(w, pos);
        send(w, QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, mods);
        send(w, QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, mods);
    }
    static void drag(QWidget *w, const QPointF &from, const QPointF &to)
    {
        hover(w, from);
        send(w, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        const int steps = 6;
        for (int i = 1; i <= steps; ++i)
            send(w, QEvent::MouseMove, from + (to - from) * (double(i) / steps), Qt::NoButton, Qt::LeftButton);
        send(w, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
    }

    void setView(const std::function<void(View &)> &fn)
    {
        m_body->editView(fn);
        m_body->refresh();
        settle(20);
    }
    template<typename T>
    T *active() const
    {
        return qobject_cast<T *>(m_body->activeView());
    }
    QString rowNamed(const QString &title) const
    {
        for (const PageMeta *m : m_ws->rows(m_db))
            if (m->title == title)
                return m->id;
        return {};
    }
    // Resets the first view to a plain table showing every property.
    void resetView(ViewType type = ViewType::Table)
    {
        Schema s = m_ws->schema(m_db);
        View v = Schema::makeView(s, type, QStringLiteral("Main"));
        v.id = s.views.first().id;
        v.hidden.clear();
        s.views = {v};
        m_ws->setSchema(m_db, s);
        m_body->selectView(v.id);
        m_body->refresh();
        settle(20);
    }

private slots:
    void initTestCase()
    {
        QDirIterator it(QStringLiteral(":/fonts"), {QStringLiteral("*.otf")});
        while (it.hasNext())
            QFontDatabase::addApplicationFont(it.next());
        qApp->setPalette(Theme::palette());
        qApp->setStyleSheet(Theme::styleSheet());
        m_ws = new Workspace(this);
        QVERIFY(m_ws->open(m_dir.filePath(QStringLiteral("ws"))));
        m_ws->setPeople({{QStringLiteral("me"), QStringLiteral("Me")}, {QStringLiteral("ava"), QStringLiteral("Ava")}});

        Schema s;
        auto prop = [](const QString &id, const QString &name, PropType t, QList<Option> opts = {}) {
            Property p;
            p.id = id;
            p.name = name;
            p.type = t;
            p.options = opts;
            return p;
        };
        s.props = {prop(QStringLiteral("title"), QStringLiteral("Name"), PropType::Title),
                   prop(QStringLiteral("status"), QStringLiteral("Status"), PropType::Status,
                        {{QStringLiteral("todo"), QStringLiteral("To do"), 2, 0},
                         {QStringLiteral("doing"), QStringLiteral("Doing"), 5, 1},
                         {QStringLiteral("done"), QStringLiteral("Done"), 8, 2}}),
                   prop(QStringLiteral("tags"), QStringLiteral("Tags"), PropType::MultiSelect,
                        {{QStringLiteral("a"), QStringLiteral("Apollo"), 3, 0}, {QStringLiteral("b"), QStringLiteral("Borealis"), 6, 0}}),
                   prop(QStringLiteral("owner"), QStringLiteral("Owner"), PropType::Person),
                   prop(QStringLiteral("est"), QStringLiteral("Estimate"), PropType::Number),
                   prop(QStringLiteral("due"), QStringLiteral("Due"), PropType::Date),
                   prop(QStringLiteral("done"), QStringLiteral("Checked"), PropType::Checkbox)};
        s.views = {Schema::makeView(s, ViewType::Table, QStringLiteral("Main"))};
        m_db = m_ws->createDatabase({}, QStringLiteral("Tasks"), s);
        // A Monday in the middle of a month keeps calendar weeks predictable.
        const QDate today = QDate::currentDate();
        m_monday = QDate(today.year(), today.month(), 8);
        m_monday = m_monday.addDays(1 - m_monday.dayOfWeek());
        struct R {
            const char *title, *status;
            QStringList tags;
            const char *owner;
            double est;
            int start, len; // days from m_monday; len 0 = no date
        };
        const QList<R> rows = {{"Alpha", "todo", {"a"}, "me", 3, 0, 3},
                               {"Beta", "doing", {"a", "b"}, "ava", 5, 4, 5},
                               {"Gamma", "done", {"b"}, "me", 2, 1, 1},
                               {"Delta", "todo", {}, "", 8, 9, 2},
                               {"Epsilon", "doing", {"b"}, "ava", 1, 0, 0}};
        for (const R &r : rows) {
            QJsonObject props{{QStringLiteral("status"), QString::fromLatin1(r.status)},
                              {QStringLiteral("tags"), QJsonArray::fromStringList(r.tags)},
                              {QStringLiteral("est"), r.est}};
            if (*r.owner)
                props.insert(QStringLiteral("owner"), QJsonArray{QString::fromLatin1(r.owner)});
            if (r.len > 0)
                props.insert(QStringLiteral("due"), makeDateValue(QDateTime(m_monday.addDays(r.start), QTime()), false,
                                                                   r.len > 1 ? QDateTime(m_monday.addDays(r.start + r.len - 1), QTime()) : QDateTime()));
            m_rows.append(m_ws->createRow(m_db, QString::fromLatin1(r.title), props));
        }
        m_body = new DatabaseBody(nullptr, false);
        m_body->resize(1300, 900);
        m_body->setColumn(60, 1150);
        m_body->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_body));
        m_body->setDatabase(m_db);
        settle();
        QVERIFY(active<TableView>());
    }

    void cleanupTestCase()
    {
        delete m_body;
    }

    // ---- table ---------------------------------------------------------------------

    void tableGroupsAndCalculations()
    {
        resetView();
        setView([](View &v) {
            v.groupBy = QStringLiteral("status");
            v.calcs = {{QStringLiteral("est"), QStringLiteral("sum")}};
        });
        TableView *t = active<TableView>();
        QVERIFY(t);
        int groups = 0, calcs = 0, rows = 0;
        for (const auto &l : t->m_lines) {
            groups += l.kind == TableView::Line::Group;
            calcs += l.kind == TableView::Line::Calc;
            rows += l.kind == TableView::Line::Row;
        }
        QCOMPARE(groups, 3);
        QCOMPARE(calcs, 4); // one per group and the footer
        QCOMPARE(rows, 5);
        const Property *est = m_body->schema().prop(QStringLiteral("est"));
        QCOMPARE(calculate(*est, t->m_ctx.rows, QStringLiteral("sum")), QStringLiteral("19"));
        // Folding a group hides its rows.
        t->toggleGroup(QStringLiteral("todo"));
        m_body->refresh();
        t = active<TableView>();
        rows = 0;
        for (const auto &l : t->m_lines)
            rows += l.kind == TableView::Line::Row;
        QCOMPARE(rows, 3);
        // A new row in a group gets the group's value.
        int addLine = -1;
        for (int i = 0; i < t->m_lines.size(); ++i)
            if (t->m_lines[i].kind == TableView::Line::Add && t->m_lines[i].group == QLatin1String("done"))
                addLine = i;
        QVERIFY(addLine >= 0);
        const int before = int(m_ws->rows(m_db).size());
        click(t, QPointF(200, t->m_lines[addLine].y + 10));
        settle();
        QCOMPARE(int(m_ws->rows(m_db).size()), before + 1);
        const PageMeta *added = m_ws->rows(m_db).last();
        QCOMPARE(m_ws->props(added->id).value(QStringLiteral("status")).toString(), QStringLiteral("done"));
        m_ws->deleteForever(added->id);
        setView([](View &v) {
            v.groupBy.clear();
            v.collapsedGroups.clear();
        });
    }

    void tableSelectionAndBulkActions()
    {
        resetView();
        TableView *t = active<TableView>();
        QVERIFY(t);
        auto rowLine = [&](const QString &id) {
            for (const auto &l : t->m_lines)
                if (l.kind == TableView::Line::Row && t->m_ctx.rows[l.row].meta->id == id)
                    return l;
            return TableView::Line{TableView::Line::Header, 0, 0};
        };
        click(t, QPointF(12, rowLine(m_rows[0]).y + 18));
        QCOMPARE(t->selectedRows(), QStringList{m_rows[0]});
        QVERIFY(m_body->m_bulkBar->isVisible());
        QVERIFY(!m_body->m_tabsBar->isVisible());
        // Shift-click extends the selection over the rows between.
        click(t, QPointF(12, rowLine(m_rows[2]).y + 18), Qt::ShiftModifier);
        QCOMPARE(t->selectedRows().size(), 3);
        // Duplicate from the bulk bar.
        const int before = int(m_ws->rows(m_db).size());
        FlatButton *dup = nullptr;
        for (FlatButton *b : m_body->m_bulkBar->findChildren<FlatButton *>())
            if (b->text() == QLatin1String("Duplicate"))
                dup = b;
        QVERIFY(dup);
        dup->click();
        settle();
        QCOMPARE(int(m_ws->rows(m_db).size()), before + 3);
        QVERIFY(t->selectedRows().isEmpty());
        QVERIFY(!m_body->m_bulkBar->isVisible());
        // Select the copies and delete them.
        m_body->refresh();
        t = active<TableView>();
        QStringList copies;
        for (const PageMeta *m : m_ws->rows(m_db))
            if (!m_rows.contains(m->id))
                copies.append(m->id);
        QCOMPARE(copies.size(), 3);
        for (const QString &id : copies)
            t->m_selected.insert(id);
        emit t->selectionChanged();
        FlatButton *del = nullptr;
        for (FlatButton *b : m_body->m_bulkBar->findChildren<FlatButton *>())
            if (b->text() == QLatin1String("Delete"))
                del = b;
        QVERIFY(del);
        del->click();
        settle();
        QCOMPARE(int(m_ws->rows(m_db).size()), before);
        for (const QString &id : copies)
            m_ws->deleteForever(id);
    }

    void tableSubItemsNestAndFold()
    {
        resetView();
        m_ws->enableSubItems(m_db, true);
        const Schema s = m_ws->schema(m_db);
        QVERIFY(!s.subItemsProp.isEmpty());
        const QString child = m_ws->createRow(m_db, QStringLiteral("Alpha child"), {{s.subItemsProp, QJsonArray{m_rows[0]}}});
        m_body->refresh();
        TableView *t = active<TableView>();
        int childLine = -1, parentLine = -1;
        for (int i = 0; i < t->m_lines.size(); ++i) {
            const auto &l = t->m_lines[i];
            if (l.kind != TableView::Line::Row)
                continue;
            if (t->m_ctx.rows[l.row].meta->id == child)
                childLine = i;
            if (t->m_ctx.rows[l.row].meta->id == m_rows[0])
                parentLine = i;
        }
        QVERIFY(parentLine >= 0 && childLine == parentLine + 1);
        QCOMPARE(t->m_lines[childLine].depth, 1);
        QCOMPARE(t->m_lines[parentLine].children, 1);
        // The toggle left of the title folds the sub-item away.
        const double tx = t->colX(t->m_cols[0]) + 8 + 4;
        click(t, QPointF(tx, t->m_lines[parentLine].y + 18));
        bool shown = false;
        for (const auto &l : t->m_lines)
            if (l.kind == TableView::Line::Row && t->m_ctx.rows[l.row].meta->id == child)
                shown = true;
        QVERIFY(!shown);
        // Flattened: everything at depth 0.
        setView([](View &v) { v.subItems = QStringLiteral("flat"); });
        t = active<TableView>();
        for (const auto &l : t->m_lines)
            if (l.kind == TableView::Line::Row)
                QCOMPARE(l.depth, 0);
        m_ws->deleteForever(child);
        m_ws->enableSubItems(m_db, false);
        setView([](View &v) { v.subItems = QStringLiteral("nested"); });
    }

    void tableFrozenColumnsAndReorder()
    {
        resetView();
        setView([](View &v) { v.frozen = 1; });
        TableView *t = active<TableView>();
        QVERIFY(t->m_cols.first().frozen);
        QVERIFY(!t->m_cols[1].frozen);
        const double x0 = t->colX(t->m_cols[0]);
        t->setScrollOffset(120);
        QCOMPARE(t->colX(t->m_cols[0]), x0 + 120);
        QCOMPARE(t->colX(t->m_cols[1]), t->m_cols[1].x);
        t->setScrollOffset(0);
        setView([](View &v) { v.frozen = 0; });
        // Dragging a header moves the column.
        t = active<TableView>();
        const QString moved = t->m_cols[1].prop->id;
        const double fromX = t->m_cols[1].x + 30;
        const double toX = t->m_cols[3].x + t->m_cols[3].w - 2;
        drag(t, QPointF(fromX, 17), QPointF(toX, 17));
        m_body->refresh();
        settle(20);
        t = active<TableView>();
        QCOMPARE(t->m_cols[3].prop->id, moved);
        // Resizing a column saves its width.
        const double edge = t->m_cols[1].x + t->m_cols[1].w;
        const QString id = t->m_cols[1].prop->id;
        drag(t, QPointF(edge, 17), QPointF(edge + 60, 17));
        QCOMPARE(m_body->currentView().widths.value(id), int(t->m_cols[1].w));
        QVERIFY(m_body->currentView().widths.value(id) > 200);
    }

    // ---- board ---------------------------------------------------------------------

    void boardDragsCardsBetweenGroups()
    {
        resetView(ViewType::Board);
        setView([](View &v) { v.groupBy = QStringLiteral("status"); });
        BoardView *b = active<BoardView>();
        QVERIFY(b);
        auto columnOf = [&](const QString &key) {
            for (int i = 0; i < b->m_columns.size(); ++i)
                if (b->m_columns[i].group.key == key)
                    return i;
            return -1;
        };
        const int todo = columnOf(QStringLiteral("todo")), done = columnOf(QStringLiteral("done"));
        QVERIFY(todo >= 0 && done >= 0);
        QCOMPARE(b->m_columns[todo].cards.size(), 2);
        const QRectF card = b->m_columns[todo].cards.first().rect;
        const QString id = b->m_ctx.rows[b->m_columns[todo].cards.first().row].meta->id;
        drag(b, card.center(), QPointF(b->m_columns[done].rect.center().x(), b->m_columns[done].rect.top() + 4));
        settle();
        QCOMPARE(m_ws->props(id).value(QStringLiteral("status")).toString(), QStringLiteral("done"));
        m_ws->setProp(id, QStringLiteral("status"), QStringLiteral("todo"));
        settle();
    }

    void boardGroupsByMultiSelectWithLanes()
    {
        resetView(ViewType::Board);
        setView([](View &v) {
            v.groupBy = QStringLiteral("tags");
            v.subGroupBy = QStringLiteral("owner");
        });
        BoardView *b = active<BoardView>();
        QVERIFY(b->m_laneProp);
        // Lanes: Me, Ava and "No Owner".
        QCOMPARE(b->m_lanes.size(), 3);
        // Beta has both tags: it shows in both tag columns.
        int betaCards = 0;
        for (const auto &col : b->m_columns)
            for (const auto &c : col.cards)
                betaCards += b->m_ctx.rows[c.row].meta->id == m_rows[1];
        QCOMPARE(betaCards, 2);
        // Moving Gamma (tag b, owner me) to tag a in Ava's lane swaps the tag and the owner.
        int from = -1, to = -1, cardIndex = -1;
        int avaLane = -1;
        for (int li = 0; li < b->m_lanes.size(); ++li)
            if (b->m_lanes[li].group.key == QLatin1String("ava"))
                avaLane = li;
        for (int i = 0; i < b->m_columns.size(); ++i) {
            const auto &col = b->m_columns[i];
            for (int k = 0; k < col.cards.size(); ++k)
                if (b->m_ctx.rows[col.cards[k].row].meta->id == m_rows[2]) {
                    from = i;
                    cardIndex = k;
                }
            if (col.group.key == QLatin1String("a") && col.lane == avaLane)
                to = i;
        }
        QVERIFY(from >= 0 && to >= 0);
        drag(b, b->m_columns[from].cards[cardIndex].rect.center(), b->m_columns[to].rect.center());
        settle();
        const QJsonObject p = m_ws->props(m_rows[2]);
        QCOMPARE(p.value(QStringLiteral("tags")).toArray(), QJsonArray{QStringLiteral("a")});
        QCOMPARE(p.value(QStringLiteral("owner")).toArray(), QJsonArray{QStringLiteral("ava")});
        m_ws->setProps(m_rows[2], {{QStringLiteral("status"), QStringLiteral("done")},
                                   {QStringLiteral("tags"), QJsonArray{QStringLiteral("b")}},
                                   {QStringLiteral("owner"), QJsonArray{QStringLiteral("me")}},
                                   {QStringLiteral("est"), 2.0},
                                   {QStringLiteral("due"), m_monday.addDays(1).toString(Qt::ISODate)}});
        // Folding a column makes it a narrow strip.
        b = active<BoardView>();
        b->toggleGroup(QStringLiteral("b"));
        m_body->refresh();
        b = active<BoardView>();
        bool folded = false;
        for (const auto &col : b->m_columns)
            if (col.group.key == QLatin1String("b")) {
                QVERIFY(col.collapsed);
                QCOMPARE(col.headerRect.width(), 44.0);
                QVERIFY(col.cards.isEmpty());
                folded = true;
            }
        QVERIFY(folded);
        // Hidden groups disappear.
        setView([](View &v) {
            v.collapsedGroups.clear();
            v.subGroupBy.clear();
            v.hiddenGroups = {QString()};
        });
        b = active<BoardView>();
        for (const auto &col : b->m_columns)
            QVERIFY(!col.group.key.isEmpty());
    }

    // ---- calendar ------------------------------------------------------------------

    void calendarSpansRangesAndMovesThem()
    {
        resetView(ViewType::Calendar);
        setView([](View &v) { v.dateProp = QStringLiteral("due"); });
        CalendarView *c = active<CalendarView>();
        QVERIFY(c);
        c->setMonth(m_monday);
        // Beta runs Friday → Tuesday: two chips, continuing across the week break.
        QList<CalendarView::Chip> beta;
        for (const auto &chip : c->m_chips)
            if (c->m_ctx.rows[chip.row].meta->id == m_rows[1])
                beta.append(chip);
        QCOMPARE(beta.size(), 2);
        QVERIFY(beta[0].continuesRight);
        QVERIFY(beta[1].continuesLeft);
        QVERIFY(beta[0].rect.width() > c->m_cellW * 2);
        // Dragging Alpha two days later keeps its length.
        CalendarView::Chip alpha;
        for (const auto &chip : c->m_chips)
            if (c->m_ctx.rows[chip.row].meta->id == m_rows[0])
                alpha = chip;
        const QPointF start(alpha.rect.left() + c->m_cellW / 2, alpha.rect.center().y());
        drag(c, start, start + QPointF(2 * c->m_cellW, 0));
        settle();
        const QJsonValue v = m_ws->props(m_rows[0]).value(QStringLiteral("due"));
        QCOMPARE(dateOf(v), m_monday.addDays(2));
        QCOMPARE(dateEndOf(v).date(), m_monday.addDays(4));
        m_ws->setProp(m_rows[0], QStringLiteral("due"), makeDateValue(QDateTime(m_monday, QTime()), false, QDateTime(m_monday.addDays(2), QTime())));
        // Without weekends the week has five columns.
        setView([](View &v) { v.showWeekends = false; });
        c = active<CalendarView>();
        QCOMPARE(c->visibleDays().size(), 5);
        for (const auto &chip : c->m_chips)
            QVERIFY(chip.rect.right() <= 5 * c->m_cellW + 1);
    }

    // ---- timeline ------------------------------------------------------------------

    void timelineBarsResizeAndUnscheduled()
    {
        resetView(ViewType::Timeline);
        setView([](View &v) {
            v.dateProp = QStringLiteral("due");
            v.timelineScale = QStringLiteral("month");
        });
        TimelineView *t = active<TimelineView>();
        QVERIFY(t);
        QCOMPARE(t->m_bars.size(), 4); // Epsilon has no date
        int unscheduled = 0;
        for (const auto &l : t->m_lines)
            unscheduled += l.kind == TimelineView::Line::Unscheduled && l.row >= 0;
        QCOMPARE(unscheduled, 1);
        // Drag Delta's end three days later.
        int bi = -1;
        for (int i = 0; i < t->m_bars.size(); ++i)
            if (t->m_ctx.rows[t->m_bars[i].row].meta->id == m_rows[3])
                bi = i;
        QVERIFY(bi >= 0);
        t->setScrollOffset(0);
        const QRectF bar = t->m_bars[bi].rect;
        const QPointF edge(bar.right() - 3, bar.center().y());
        drag(t, edge, edge + QPointF(3 * t->dayWidth(), 0));
        settle();
        const QJsonValue v = m_ws->props(m_rows[3]).value(QStringLiteral("due"));
        QCOMPARE(dateOf(v), m_monday.addDays(9));
        QCOMPARE(dateEndOf(v).date(), m_monday.addDays(13));
        // Moving the whole bar shifts both ends.
        m_body->refresh();
        t = active<TimelineView>();
        for (int i = 0; i < t->m_bars.size(); ++i)
            if (t->m_ctx.rows[t->m_bars[i].row].meta->id == m_rows[3])
                bi = i;
        const QPointF mid = t->m_bars[bi].rect.center();
        drag(t, mid, mid - QPointF(2 * t->dayWidth(), 0));
        settle();
        const QJsonValue w = m_ws->props(m_rows[3]).value(QStringLiteral("due"));
        QCOMPARE(dateOf(w), m_monday.addDays(7));
        QCOMPARE(dateEndOf(w).date(), m_monday.addDays(11));
        m_ws->setProp(m_rows[3], QStringLiteral("due"), makeDateValue(QDateTime(m_monday.addDays(9), QTime()), false, QDateTime(m_monday.addDays(10), QTime())));
        // Dependencies draw without trouble, and other scales lay out.
        m_ws->enableDependencies(m_db, true);
        const QString dep = m_ws->schema(m_db).dependencyProp;
        QVERIFY(!dep.isEmpty());
        m_ws->setProp(m_rows[3], dep, QJsonArray{m_rows[1]});
        for (const char *scale : {"day", "week", "biweek", "quarter", "year"}) {
            setView([scale](View &v) { v.timelineScale = QString::fromLatin1(scale); });
            t = active<TimelineView>();
            QVERIFY(t->contentWidth(800) > 800);
            t->grab();
        }
        m_ws->enableDependencies(m_db, false);
    }

    // ---- chart ---------------------------------------------------------------------

    void chartComputesSeries()
    {
        resetView(ViewType::Chart);
        setView([](View &v) {
            v.chartX = QStringLiteral("status");
            v.chartY = QStringLiteral("sum:est");
            v.chartGroup.clear();
            v.chartSort = QStringLiteral("manual");
        });
        ChartView *c = active<ChartView>();
        QVERIFY(c);
        QHash<QString, double> sums;
        for (const auto &p : c->m_points)
            sums.insert(p.key, p.values.value(0));
        QCOMPARE(sums.value(QStringLiteral("todo")), 11.0);
        QCOMPARE(sums.value(QStringLiteral("doing")), 6.0);
        QCOMPARE(sums.value(QStringLiteral("done")), 2.0);
        QCOMPARE(c->m_total, 19.0);
        setView([](View &v) { v.chartSort = QStringLiteral("ydesc"); });
        c = active<ChartView>();
        QCOMPARE(c->m_points.first().key, QStringLiteral("todo"));
        setView([](View &v) { v.chartCumulative = true; });
        c = active<ChartView>();
        QCOMPARE(c->m_points.last().values.value(0), 19.0);
        // Stacked by owner: a series per person.
        setView([](View &v) {
            v.chartCumulative = false;
            v.chartGroup = QStringLiteral("owner");
            v.chartY = QStringLiteral("count");
        });
        c = active<ChartView>();
        QCOMPARE(c->m_series.size(), 3); // Me, Ava and "No Owner" (Delta)
        for (const char *type : {"bar", "hbar", "line", "donut", "number"}) {
            setView([type](View &v) { v.chartType = QString::fromLatin1(type); });
            QVERIFY(!active<ChartView>()->grab().isNull());
        }
    }

    // ---- form ----------------------------------------------------------------------

    void formSubmitsRowsAndChecksRequired()
    {
        resetView(ViewType::Form);
        setView([](View &v) { v.formRequired = {QStringLiteral("est")}; });
        FormView *f = active<FormView>();
        QVERIFY(f);
        auto *title = qobject_cast<QLineEdit *>(f->m_inputs.value(QStringLiteral("title")));
        auto *est = qobject_cast<QLineEdit *>(f->m_inputs.value(QStringLiteral("est")));
        auto *done = qobject_cast<QCheckBox *>(f->m_inputs.value(QStringLiteral("done")));
        QVERIFY(title && est && done);
        const int before = int(m_ws->rows(m_db).size());
        title->setText(QStringLiteral("From the form"));
        f->submit();
        QCOMPARE(int(m_ws->rows(m_db).size()), before); // the estimate is required
        QVERIFY(!f->findChild<QLabel *>(QStringLiteral("formError"))->isHidden());
        est->setText(QStringLiteral("4.5"));
        done->setChecked(true);
        f->submit();
        QCOMPARE(int(m_ws->rows(m_db).size()), before + 1);
        const PageMeta *row = m_ws->rows(m_db).last();
        QCOMPARE(row->title, QStringLiteral("From the form"));
        QCOMPARE(m_ws->props(row->id).value(QStringLiteral("est")).toDouble(), 4.5);
        QCOMPARE(m_ws->props(row->id).value(QStringLiteral("done")).toBool(), true);
        QVERIFY(title->text().isEmpty()); // cleared for the next answer
        m_ws->deleteForever(row->id);
    }

    // ---- filters -------------------------------------------------------------------

    void advancedFiltersNestAndDescribe()
    {
        resetView();
        // Status is Done OR (Estimate > 4 AND Tags contains Apollo).
        FilterRule inner;
        inner.group = true;
        inner.rules = {{QStringLiteral("est"), QStringLiteral("gt"), 4.0}, {QStringLiteral("tags"), QStringLiteral("contains"), QStringLiteral("a")}};
        setView([inner](View &v) {
            v.filterAny = true;
            v.filters = {{QStringLiteral("status"), QStringLiteral("is"), QStringLiteral("done")}, inner};
        });
        QStringList titles;
        for (const RowRef &r : m_body->activeView()->context().rows)
            titles.append(r.meta->title);
        titles.sort();
        QCOMPARE(titles, (QStringList{QStringLiteral("Beta"), QStringLiteral("Gamma")}));
        const Schema &s = m_body->schema();
        QCOMPARE(DbPopovers::describeRule(s, {QStringLiteral("status"), QStringLiteral("is"), QStringLiteral("done")}), QStringLiteral("Status: Done"));
        QCOMPARE(DbPopovers::describeRule(s, {QStringLiteral("done"), QStringLiteral("checked"), QJsonValue()}), QStringLiteral("Checked: Checked"));
        QCOMPARE(DbPopovers::describeRule(s, {QStringLiteral("due"), QStringLiteral("before"), QStringLiteral("@today")}), QStringLiteral("Due: is before Today"));
        QCOMPARE(DbPopovers::describeRule(s, inner), QStringLiteral("2 rules"));
        // Chips: one per top-level rule.
        int chips = 0;
        for (QWidget *w : m_body->m_chipsBar->findChildren<FlatButton *>(QStringLiteral("dbchip")))
            chips += !w->isHidden();
        QCOMPARE(chips, 2);
        // Relative dates follow the calendar.
        const QDate today(2026, 10, 17);
        QCOMPARE(filterDate(QStringLiteral("@today"), today), today);
        QCOMPARE(filterDate(QStringLiteral("@weekago"), today), QDate(2026, 10, 10));
        QCOMPARE(filterDate(QStringLiteral("@monthlater"), today), QDate(2026, 11, 17));
        QCOMPARE(filterDate(QStringLiteral("2026-01-02"), today), QDate(2026, 1, 2));
        setView([](View &v) {
            v.filters.clear();
            v.filterAny = false;
        });
    }

    void filterPopoverBuildsRulesAndGroups()
    {
        resetView();
        DbPopovers::showFilters(m_body, m_body->mapToGlobal(QPoint(100, 100)), m_db, [this] { return m_body->currentView(); },
                                [this](const std::function<void(View &)> &fn) { m_body->editView(fn); });
        settle(30);
        Popover *pop = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("filterPopover") && w->isVisible())
                pop = qobject_cast<Popover *>(w);
        QVERIFY(pop);
        auto button = [&](const QString &text) -> FlatButton * {
            for (FlatButton *b : pop->findChildren<FlatButton *>())
                if (b->text() == text && !b->isHidden())
                    return b;
            return nullptr;
        };
        QVERIFY(button(QStringLiteral("Add filter rule")));
        button(QStringLiteral("Add filter rule"))->click();
        settle(30);
        QCOMPARE(m_body->currentView().filters.size(), 1);
        QVERIFY(button(QStringLiteral("Add filter group")));
        button(QStringLiteral("Add filter group"))->click();
        settle(30);
        const View v = m_body->currentView();
        QCOMPARE(v.filters.size(), 2);
        QVERIFY(v.filters[1].group);
        QCOMPARE(v.filters[1].rules.size(), 1);
        button(QStringLiteral("Delete filter"))->click();
        settle(30);
        QVERIFY(m_body->currentView().filters.isEmpty());
        pop->close();
    }

    void settingsPopoverChangesLayout()
    {
        resetView();
        DbPopovers::showSettings(m_body, m_body->mapToGlobal(QPoint(300, 100)), QStringLiteral("layout"));
        settle(30);
        Popover *pop = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("settingsPopover") && w->isVisible())
                pop = qobject_cast<Popover *>(w);
        QVERIFY(pop);
        auto *board = pop->findChild<FlatButton *>(QStringLiteral("layout-board"));
        QVERIFY(board);
        board->click();
        settle(60);
        QCOMPARE(m_body->currentView().type, ViewType::Board);
        QCOMPARE(m_body->currentView().groupBy, QStringLiteral("status")); // a board groups by status by default
        pop->close();
        // Every page of the settings menu builds for every layout.
        for (ViewType t : {ViewType::Table, ViewType::Board, ViewType::List, ViewType::Gallery, ViewType::Calendar, ViewType::Timeline, ViewType::Chart,
                           ViewType::Feed, ViewType::Form}) {
            resetView(t);
            for (const char *page : {"", "layout", "properties", "group", "subitems", "limit", "openin"}) {
                DbPopovers::showSettings(m_body, m_body->mapToGlobal(QPoint(300, 100)), QString::fromLatin1(page));
                settle(5);
                for (QWidget *w : QApplication::topLevelWidgets())
                    if (w->objectName() == QLatin1String("settingsPopover") && w->isVisible())
                        w->close();
            }
        }
        resetView();
    }

    // ---- body ----------------------------------------------------------------------

    void viewsAddDuplicateDelete()
    {
        resetView();
        m_body->addView(ViewType::Gallery);
        QCOMPARE(m_body->views().size(), 2);
        QVERIFY(active<GalleryView>());
        m_body->duplicateView(m_body->currentViewId());
        QCOMPARE(m_body->views().size(), 3);
        QVERIFY(m_body->currentView().name.endsWith(QStringLiteral("(copy)")));
        m_body->deleteView(m_body->currentViewId());
        QCOMPARE(m_body->views().size(), 2);
        m_body->refresh();
        settle(100); // old tabs are deleted later, new ones shown by the layout
        int tabs = 0;
        for (QWidget *w : m_body->m_tabsBar->findChildren<FlatButton *>(QStringLiteral("viewtab")))
            tabs += w->isVisible();
        QCOMPARE(tabs, 2);
        // Every layout renders with the sample rows.
        for (ViewType t : {ViewType::List, ViewType::Gallery, ViewType::Feed}) {
            resetView(t);
            QVERIFY(m_body->activeView()->height() > 100);
            QVERIFY(!m_body->activeView()->grab().isNull());
        }
        resetView();
    }

    void newRowsFollowFiltersAndTemplates()
    {
        resetView();
        setView([](View &v) { v.filters = {{QStringLiteral("status"), QStringLiteral("is"), QStringLiteral("doing")}}; });
        m_body->createRow({}, false);
        settle();
        const PageMeta *row = m_ws->rows(m_db).last();
        QCOMPARE(m_ws->props(row->id).value(QStringLiteral("status")).toString(), QStringLiteral("doing"));
        m_ws->deleteForever(row->id);
        setView([](View &v) { v.filters.clear(); });
        // The default template gives new rows its properties and content.
        const QString tpl = m_ws->createTemplate(m_db, QStringLiteral("Bug report"));
        m_ws->setProps(tpl, {{QStringLiteral("est"), 1.0}});
        m_ws->setContent(tpl, {Block::make(BlockType::Heading2, QStringLiteral("Steps to reproduce"))});
        Schema s = m_ws->schema(m_db);
        s.defaultTemplate = tpl;
        m_ws->setSchema(m_db, s);
        m_body->refresh();
        m_body->createRow({}, false);
        settle();
        row = m_ws->rows(m_db).last();
        QCOMPARE(row->title, QStringLiteral("Bug report"));
        QCOMPARE(m_ws->props(row->id).value(QStringLiteral("est")).toDouble(), 1.0);
        QCOMPARE(m_ws->content(row->id).first().plainText(), QStringLiteral("Steps to reproduce"));
        // Templates aren't rows.
        for (const PageMeta *m : m_ws->rows(m_db))
            QVERIFY(m->id != tpl);
        m_ws->deleteForever(row->id);
        s = m_ws->schema(m_db);
        s.defaultTemplate.clear();
        m_ws->setSchema(m_db, s);
        m_ws->deleteForever(tpl);
    }

    void lockedDatabasesKeepStructure()
    {
        resetView();
        Schema s = m_ws->schema(m_db);
        s.locked = true;
        m_ws->setSchema(m_db, s);
        m_body->refresh();
        QVERIFY(!m_body->m_lockBadge->isHidden());
        QVERIFY(!m_body->activeView()->context().editable);
        // Rows can still be added.
        const int before = int(m_ws->rows(m_db).size());
        m_body->createRow({}, false);
        settle();
        QCOMPARE(int(m_ws->rows(m_db).size()), before + 1);
        m_ws->deleteForever(m_ws->rows(m_db).last()->id);
        m_body->m_lockBadge->click();
        settle();
        QVERIFY(!m_ws->schema(m_db).locked);
    }

    void linkedViewsSaveThroughTheirOwner()
    {
        DatabaseBody body(nullptr, true);
        body.resize(900, 600);
        body.setColumn(28, 800);
        body.show();
        body.setDatabase(m_db);
        View linked = Schema::makeView(m_ws->schema(m_db), ViewType::List, QStringLiteral("Linked"));
        QList<View> saved;
        body.setLinkedViews({linked}, [&](const QList<View> &views) { saved = views; });
        body.refresh();
        QCOMPARE(body.views().size(), 1);
        QCOMPARE(body.currentView().name, QStringLiteral("Linked"));
        QVERIFY(qobject_cast<ListView *>(body.activeView()));
        body.editView([](View &v) { v.name = QStringLiteral("Renamed"); });
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().name, QStringLiteral("Renamed"));
        // The database's own views are untouched.
        for (const View &v : m_ws->schema(m_db).views)
            QVERIFY(v.name != QLatin1String("Renamed"));
        body.addView(ViewType::Board);
        QCOMPARE(saved.size(), 2);
    }

    void inlineLoadLimitAndLoadMore()
    {
        DatabaseBody body(nullptr, true);
        body.resize(900, 600);
        body.setColumn(28, 800);
        body.show();
        body.setDatabase(m_db);
        Schema s = m_ws->schema(m_db);
        body.editViewById(s.views.first().id, [](View &v) {
            v.type = ViewType::Table;
            v.loadLimit = 2;
        });
        body.refresh();
        auto *t = qobject_cast<TableView *>(body.activeView());
        QVERIFY(t);
        auto count = [&](TableView::Line::Kind k) {
            int n = 0;
            for (const auto &l : t->m_lines)
                n += l.kind == k;
            return n;
        };
        QCOMPARE(count(TableView::Line::Row), 2);
        QCOMPARE(count(TableView::Line::More), 1);
        emit t->loadMore();
        t = qobject_cast<TableView *>(body.activeView());
        QCOMPARE(count(TableView::Line::Row), 5);
        QCOMPARE(count(TableView::Line::More), 0);
        body.editViewById(s.views.first().id, [](View &v) { v.loadLimit = 50; });
    }

    // ---- pages ---------------------------------------------------------------------

    void editorEmbedsInlineDatabases()
    {
        const QString page = m_ws->createPage({}, QStringLiteral("Host page"));
        Block intro = Block::make(BlockType::Paragraph, QStringLiteral("Before the database"));
        Block db;
        db.type = BlockType::Database;
        db.pageId = m_db;
        db.id = Block::newId();
        Block linked;
        linked.type = BlockType::Database;
        linked.pageId = m_db;
        linked.id = Block::newId();
        linked.data.insert(QStringLiteral("linked"), true);
        Schema holder;
        holder.views = {Schema::makeView(m_ws->schema(m_db), ViewType::Board, QStringLiteral("Linked board"))};
        holder.views.first().groupBy = QStringLiteral("status");
        linked.data.insert(QStringLiteral("views"), holder.toJson().value(QStringLiteral("views")));
        m_ws->setContent(page, {intro, db, linked, Block::make(BlockType::Paragraph, QStringLiteral("After"))});

        QScrollArea area;
        area.resize(1100, 900);
        area.setWidgetResizable(true);
        auto *ed = new BlockEditor;
        area.setWidget(ed);
        area.show();
        ed->setPage(page);
        settle(150);
        DatabaseBody *inlineBody = ed->embedFor(db.id);
        DatabaseBody *linkedBody = ed->embedFor(linked.id);
        QVERIFY(inlineBody && linkedBody);
        QVERIFY(inlineBody->isVisible());
        QVERIFY(qobject_cast<TableView *>(inlineBody->activeView()));
        QVERIFY(qobject_cast<BoardView *>(linkedBody->activeView()));
        QVERIFY(linkedBody->isLinked());
        // The block reserves the body's height: the next block starts below it.
        const QTextBlock dbBlock = ed->blockById(db.id);
        const QTextBlock linkedBlock = ed->blockById(linked.id);
        QVERIFY(dbBlock.isValid() && linkedBlock.isValid());
        QVERIFY(linkedBody->geometry().top() >= inlineBody->geometry().bottom() - 2);
        QVERIFY(ed->height() > inlineBody->height() + linkedBody->height());
        // Editing the linked view saves into the block.
        linkedBody->editView([](View &v) { v.name = QStringLiteral("Mine"); });
        ed->flush();
        bool found = false;
        for (const Block &b : m_ws->content(page))
            if (b.id == linked.id) {
                found = true;
                QCOMPARE(b.data.value(QStringLiteral("views")).toArray().first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Mine"));
            }
        QVERIFY(found);
        // Rows added elsewhere show up live.
        const int before = int(inlineBody->activeView()->context().rows.size());
        const QString extra = m_ws->createRow(m_db, QStringLiteral("Live"));
        settle(120);
        QCOMPARE(int(inlineBody->activeView()->context().rows.size()), before + 1);
        m_ws->deleteForever(extra);
        // Removing the block removes its body.
        m_ws->setContent(page, {intro});
        ed->setPage(page);
        settle(120);
        QVERIFY(!ed->embedFor(db.id));
    }

    void inlineDatabaseInsideColumnsLaysOut()
    {
        // Objects in columns measure their cell without re-entering the layout
        // (this crashed when an imported Notion page reopened).
        QImage img(40, 30, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QByteArray bytes;
        {
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
        }
        const QString media = m_ws->importMediaData(bytes, QStringLiteral("png"));
        Block db;
        db.type = BlockType::Database;
        db.pageId = m_db;
        db.id = Block::newId();
        Block pic;
        pic.type = BlockType::Image;
        pic.media = media;
        Block cols;
        cols.type = BlockType::ColumnList;
        cols.widths = {0.6, 0.4};
        cols.columns = {{db, Block::make(BlockType::Todo, QStringLiteral("after"))}, {pic}};
        const QString page = m_ws->createPage({}, QStringLiteral("Columns host"), {Block::make(BlockType::Heading1, QStringLiteral("Top")), cols});
        QScrollArea area;
        area.resize(1100, 900);
        area.setWidgetResizable(true);
        auto *ed = new BlockEditor;
        area.setWidget(ed);
        area.show();
        for (int round = 0; round < 3; ++round) {
            ed->setPage(page);
            settle(80);
            QVERIFY(!area.grab().isNull());
        }
        DatabaseBody *body = ed->embedFor(db.id);
        QVERIFY(body && body->isVisible());
        // It stays inside its column instead of running across the page.
        QVERIFY(body->geometry().left() < ed->width() / 4);
        QVERIFY(body->geometry().right() < int(ed->width() * 0.7));
        QVERIFY(body->geometry().width() > ed->width() / 3);
        m_ws->deleteForever(page);
    }

    void animatedGifsPlayWhilePainted()
    {
        // A two-frame GIF: it plays where it's painted and stops when nothing paints it.
        const QByteArray gif = QByteArray::fromBase64(
            "R0lGODlhCAAIAIEAAMjIyAAAAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQABAAAACwAAAAACAAIAAAIDwABCBxIsKDBgwgTKkwYEAAh+QQBBAABACwAAAAACAAIAIEoKCgAAAAAAAAAAAAIDwABCBxIsKDBgwgTKkwYEAA7");
        const QString media = m_ws->importMediaData(gif, QStringLiteral("gif"));
        QVERIFY(AnimatedMedia::isAnimated(media));
        QImage still(4, 4, QImage::Format_RGB32);
        still.fill(Qt::red);
        QByteArray png;
        {
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            still.save(&buf, "PNG");
        }
        QVERIFY(!AnimatedMedia::isAnimated(m_ws->importMediaData(png, QStringLiteral("png"))));
        struct Canvas : QWidget {
            QString media;
            int paints = 0;
            QSet<QRgb> colours;
            void paintEvent(QPaintEvent *) override
            {
                QPainter p(this);
                const QImage f = AnimatedMedia::frame(media);
                if (f.isNull())
                    return;
                ++paints;
                colours.insert(f.pixel(2, 2));
                p.drawImage(rect(), f);
                AnimatedMedia::repaintOnNextFrame(p, rect(), media);
            }
        } canvas;
        canvas.media = media;
        canvas.resize(40, 40);
        canvas.show();
        QVERIFY(QTest::qWaitForWindowExposed(&canvas));
        QTest::qWait(400);
        QVERIFY2(canvas.paints >= 4, qPrintable(QString::number(canvas.paints)));
        QCOMPARE(canvas.colours.size(), 2); // both frames were shown
        // Hidden: no more repaints are asked for.
        canvas.hide();
        QTest::qWait(150);
        const int before = canvas.paints;
        QTest::qWait(300);
        QCOMPARE(canvas.paints, before);
        // Reduce motion keeps the first frame.
        Settings::instance().reduceMotion = true;
        canvas.paints = 0;
        canvas.show();
        QTest::qWait(400);
        QVERIFY(canvas.paints <= 2);
        Settings::instance().reduceMotion = false;
    }

    void peekOpensRowsBesideTheDatabase()
    {
        Settings::instance().expanded.clear();
        MainWindow win;
        win.resize(1280, 820);
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));
        win.navigate(m_db);
        settle();
        win.openRowIn(m_rows[1], QStringLiteral("side"));
        settle();
        QVERIFY(win.peek()->isOpen());
        QCOMPARE(win.peek()->pageId(), m_rows[1]);
        QCOMPARE(win.peek()->mode(), QStringLiteral("side"));
        QVERIFY(win.peek()->geometry().left() > win.width() / 3);
        win.openRowIn(m_rows[2], QStringLiteral("center"));
        settle();
        QCOMPARE(win.peek()->pageId(), m_rows[2]);
        QCOMPARE(win.peek()->geometry(), win.rect());
        // Open as full page.
        win.peek()->closePeek();
        QVERIFY(!win.peek()->isOpen());
        win.openRowIn(m_rows[3], QStringLiteral("full"));
        settle();
        QCOMPARE(win.pageView()->pageId(), m_rows[3]);
        // Rows clicked in a view use the view's "open in" setting.
        win.navigate(m_db);
        settle();
        DatabaseBody *body = win.databaseView()->body();
        body->editView([](View &v) { v.openIn = QStringLiteral("center"); });
        body->refresh();
        emit body->activeView()->openRow(m_rows[0]);
        settle();
        QVERIFY(win.peek()->isOpen());
        QCOMPARE(win.peek()->mode(), QStringLiteral("center"));
        // Navigating elsewhere closes the peek.
        win.navigate(m_rows[4]);
        settle();
        QVERIFY(!win.peek()->isOpen());
        body->editView([](View &v) { v.openIn = QStringLiteral("side"); });
    }
};

int main(int argc, char **argv)
{
    QApplication::setApplicationName(QStringLiteral("nebula-tests"));
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    PluralTranslator plurals;
    QCoreApplication::installTranslator(&plurals);
    DbUiTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_dbui.moc"
