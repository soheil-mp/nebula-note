#pragma once

#include "ui/Accessible.h"

#include "core/Schema.h"

#include <QDate>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <functional>

class QLineEdit;
class QScrollArea;
class QVBoxLayout;

// Everything a database view needs to draw itself.
struct ViewContext {
    QString databaseId;
    Schema schema;
    View view;
    QList<RowRef> rows;      // filtered and sorted
    bool sorted = false;     // a sort is active (manual reordering is off)
    bool editable = true;    // false while the database is locked
    bool inlineMode = false; // shown inside a page
    int limit = 0;           // rows shown at most (0 = all)
    // Saves a change to the view (widths, folded groups…) to the database's
    // own views or to a linked view's copy.
    std::function<void(const std::function<void(View &)> &)> editView;
};

// Base for the layouts. Each sizes its own height; wide layouts also report
// the width they need so the page can scroll sideways.
// Every layout shows the same rows: screen readers get them as a list, each
// named by its title and the properties the view shows.
class DbView : public QWidget, public AccessibleItems
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit DbView(QWidget *parent = nullptr);
    virtual void setContext(const ViewContext &ctx);

    QAccessible::Role accessibleRole() const override { return QAccessible::List; }
    QAccessible::Role accessibleItemRole() const override { return QAccessible::ListItem; }
    int accessibleCount() const override;
    QString accessibleItemName(int index) const override;
    QString accessibleItemDescription(int index) const override;
    QRect accessibleItemRect(int index) const override;
    void accessiblePress(int index) override;
    const ViewContext &context() const { return m_ctx; }
    virtual int contentWidth(int available) const { return available; }
    // Where a row's title sits (for editing a new row's name in place).
    virtual QRect titleRectFor(const QString &rowId) const
    {
        Q_UNUSED(rowId)
        return {};
    }
    void editTitleInline(const QString &rowId, const QRect &globalRect);
    // Rows ticked for bulk actions.
    virtual QStringList selectedRows() const { return {}; }
    virtual void clearSelection() {}
    // Horizontal scroll of the area showing the view (frozen columns).
    void setScrollOffset(int x);
    // True when the view wants all rows (calendars, charts…), not a page of them.
    virtual bool showsAllRows() const { return false; }

signals:
    void openRow(const QString &rowId);
    void geometryNeedsUpdate();
    void createRow(const QJsonObject &props, bool open);
    void selectionChanged();
    void loadMore();

protected:
    virtual void onScroll() { update(); }
    QString titleOf(const RowRef &r) const;
    void rowMenu(const QString &rowId, const QPoint &globalPos);
    QJsonValue valueOf(const RowRef &r, const Property &p) const { return rowValue(p, r); }
    void saveView(const std::function<void(View &)> &fn);
    // Grouping (table, list, gallery, board, timeline).
    const Property *groupProp() const;
    QList<RowGroup> groups() const;
    bool groupCollapsed(const QString &key) const;
    void toggleGroup(const QString &key);
    // Values a new row needs to land in a group.
    QJsonObject groupValues(const Property *prop, const QString &key) const;
    // Whether dropping into a group can change the row's value.
    bool canSetGroup(const Property *prop) const;
    // Moves a row from one group to another (multi-valued: swaps the item).
    void moveToGroup(const RowRef &row, const Property *prop, const QString &fromKey, const QString &toKey);
    int rowLimit() const;
    void paintGroupHeader(QPainter &p, const QRectF &rect, const RowGroup &g, const Property *prop, bool collapsed, bool hover);

    ViewContext m_ctx;
    int m_scrollX = 0;
};

class TableView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit TableView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    QRect titleRectFor(const QString &rowId) const override;
    int contentWidth(int available) const override;
    QStringList selectedRows() const override;
    void clearSelection() override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    bool event(QEvent *) override;

private:
    struct Column {
        const Property *prop;
        double x;
        double w;
        bool frozen;
    };
    struct Line {
        enum Kind { Header, Group, Row, Add, Calc, More } kind;
        double y;
        double h;
        int row = -1;      // index into m_ctx.rows
        int depth = 0;     // sub-item depth
        int children = 0;  // sub-items under this row
        QString group = {}; // group key (Group, Add, Calc)
        int groupIndex = -1;
    };
    void layoutColumns();
    void layoutLines();
    double rowHeight(const RowRef &r) const;
    int columnAt(double x) const;
    int lineAt(double y) const;
    double colX(const Column &c) const; // on screen (frozen columns follow the scroll)
    void saveWidth(const QString &propId, int w);
    void calcMenu(const Property *prop, const QPoint &globalPos);
    void paintCell(QPainter &p, const QRectF &cell, const Column &col, const RowRef &row, const Line &line, bool hoverRow);

    QList<Column> m_cols;
    QList<Line> m_lines;
    QList<RowGroup> m_groups;
    double m_tableWidth = 0;
    double m_frozenRight = 0; // right edge of the frozen columns (content coordinates)
    int m_hoverLine = -1;
    int m_hoverCol = -1;
    bool m_hoverOpen = false;
    bool m_hoverAddCol = false;
    bool m_hoverCheck = false;
    bool m_hoverToggle = false;
    int m_resizeCol = -1;
    double m_resizeStartX = 0;
    double m_resizeStartW = 0;
    int m_dragRow = -1;     // line index being dragged
    int m_dropLine = -1;
    int m_dragCol = -1;     // column being moved
    double m_dragColX = 0;
    QPoint m_pressPos;
    bool m_pressOnGrip = false;
    bool m_pressOnHeader = false;
    QSet<QString> m_selected;
    QString m_lastSelected;
    QSet<QString> m_collapsedRows; // sub-item parents folded
};

class BoardView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit BoardView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    QRect titleRectFor(const QString &rowId) const override;
    int contentWidth(int available) const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    struct Card {
        int row;
        QRectF rect;
    };
    struct Column {
        RowGroup group;
        int lane = 0;
        QList<Card> cards;
        QRectF rect;
        QRectF headerRect;
        QRectF addRect;
        QRectF moreRect; // "Load more" under the cards shown so far
        int more = 0;    // cards not shown yet
        bool collapsed = false;
    };
    struct Lane {
        RowGroup group;
        QRectF headerRect;
        double top = 0;
        double bottom = 0;
    };
    void layoutBoard();
    double columnWidth() const;
    double cardHeight(const RowRef &r) const;
    void paintCard(QPainter &p, const QRectF &rect, const RowRef &r, bool hover, bool ghost);
    int columnAt(const QPointF &pos) const;

    const Property *m_groupProp = nullptr;
    const Property *m_laneProp = nullptr;
    QList<Column> m_columns;
    QList<Lane> m_lanes;
    QSet<QString> m_foldedLanes;
    QRectF m_addGroupRect;
    QPair<int, int> m_hoverCard{-1, -1};
    int m_hoverAdd = -1;
    int m_hoverHeader = -1;
    int m_hoverLane = -1;
    bool m_hoverAddGroup = false;
    QPair<int, int> m_dragCard{-1, -1};
    QPointF m_dragPos;
    QPointF m_dragOffset;
    QPoint m_pressPos;
    bool m_dragging = false;
    int m_dropColumn = -1;
    int m_dropIndex = -1;
    QRectF m_emptyButton;
    double m_width = 0;
    // Cards shown per column (lane + group key); big boards load more on demand.
    QHash<QString, int> m_columnLimit;
    int m_hoverMore = -1;
};

class ListView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit ListView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    QRect titleRectFor(const QString &rowId) const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    struct Line {
        enum Kind { Group, Row, Add, More } kind;
        double y;
        int row = -1;
        int depth = 0;
        int children = 0;
        QString group = {};
        int groupIndex = -1;
    };
    void layoutLines();
    int lineAt(double y) const;
    QList<Line> m_lines;
    QList<RowGroup> m_groups;
    QHash<QString, double> m_propWidths; // widest value per shown property
    int m_hover = -1;
    bool m_hoverToggle = false;
    QSet<QString> m_collapsedRows;
};

class GalleryView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit GalleryView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    QRect titleRectFor(const QString &rowId) const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    struct Card {
        int row = -1; // -1: "+ New" in this group
        QRectF rect;
        QString group = {};
    };
    struct Header {
        int groupIndex;
        QRectF rect;
    };
    void layoutCards();
    QString previewMedia(const RowRef &row) const;
    QString previewText(const QString &rowId) const;
    QList<Card> m_cards;
    QList<Header> m_headers;
    QList<RowGroup> m_groups;
    QRectF m_moreRect;
    double m_coverH = 140;
    int m_hover = -1;
    int m_hoverHeader = -1;
    bool m_hoverMore = false;
    mutable QHash<QString, QPair<QString, QString>> m_previewCache; // row → (media, text)
};

class CalendarView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit CalendarView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    void setMonth(const QDate &firstOfMonth);
    QDate month() const { return m_month; }
    bool showsAllRows() const override { return true; }

signals:
    void monthChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct Chip {
        int row;
        QRectF rect;
        bool continuesLeft = false;
        bool continuesRight = false;
    };
    void layoutMonth();
    QList<int> visibleDays() const; // 1…7 (Monday…Sunday) shown
    QDate dateAtPos(const QPointF &pos) const;
    QRectF cellRect(int week, int dayColumn) const;
    QDate gridStart() const;

    QDate m_month;
    QList<Chip> m_chips;
    QHash<int, int> m_more; // week*7+col → hidden items
    QRectF m_prevRect, m_nextRect, m_todayRect;
    double m_cellW = 100, m_cellH = 112, m_gridTop = 78;
    int m_hoverCell = -1;
    int m_hoverChip = -1;
    int m_hoverNav = -1;
    int m_dragChip = -1;
    QPointF m_dragPos;
    QPoint m_pressPos;
    bool m_dragging = false;
    QRectF m_emptyButton;
};

// Rows as bars over time (Notion's timeline): drag a bar to move it, its
// ends to change dates; arrows show "Blocked by" dependencies.
class TimelineView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit TimelineView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    int contentWidth(int available) const override;
    bool showsAllRows() const override { return true; }
    void scrollToToday();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    struct Bar {
        int row;
        int line;
        QDate start;
        QDate end;
        QRectF rect;
    };
    struct Line {
        enum Kind { Group, Row, Unscheduled } kind;
        double y;
        int row = -1;
        QString group = {};
        int groupIndex = -1;
    };
    void layoutTimeline();
    double dayWidth() const;
    double xForDate(const QDate &d) const;
    QDate dateForX(double x) const;
    double tableWidth() const;
    QPair<QDate, QDate> datesOf(const RowRef &r) const;

    QList<Bar> m_bars;
    QList<Line> m_lines;
    QList<RowGroup> m_groups;
    QDate m_origin; // first day drawn
    int m_days = 0;
    int m_hoverBar = -1;
    int m_hoverEdge = 0; // -1 start, 1 end
    int m_hoverLine = -1;
    int m_dragBar = -1;
    int m_dragEdge = 0;
    double m_dragStartX = 0;
    QDate m_dragOrigStart, m_dragOrigEnd;
    QDate m_dragNewStart, m_dragNewEnd;
    bool m_dragging = false;
    QRectF m_scaleRects[6];
    QRectF m_todayRect;
    int m_hoverScale = -1; // 0…5 scales, 6 today
    double m_addY = 0;     // the "New" line
    bool m_hoverAdd = false;
};

// Charts of a database: bars, horizontal bars, lines, donuts or one number.
class ChartView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit ChartView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    bool showsAllRows() const override { return true; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    struct Point {
        QString key;
        QString label;
        QList<double> values; // one per series
        QList<QStringList> rows;
        QRectF hit;
    };
    void compute();
    double yValue(const QList<int> &rows) const;
    QList<Point> m_points;
    QStringList m_series; // series labels (stacking by chartGroup)
    QString m_yLabel;
    double m_total = 0;
    int m_hover = -1;
};

// Pages one after another with their content, like a feed of posts.
class FeedView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit FeedView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    // A line of a post's preview, keeping a hint of its block's shape.
    struct PreviewLine {
        enum Kind { Text, Heading, Bullet, Number, Todo, Quote } kind = Text;
        QString text;
        int depth = 0;
        int number = 0;
        bool checked = false;
    };
    struct Post {
        int row;
        QRectF rect;
        QList<PreviewLine> lines;
        bool more = false; // the preview was cut short
        QString image;
    };
    void layoutPosts();
    QList<Post> m_posts;
    QRectF m_addRect;
    int m_hover = -1;
    bool m_hoverAdd = false;
};

// A form whose answers become rows: one question per visible property.
class FormView : public DbView
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit FormView(QWidget *parent = nullptr);
    void setContext(const ViewContext &ctx) override;
    bool showsAllRows() const override { return true; }

protected:
    bool event(QEvent *e) override;

private:
    void rebuild();
    void submit();
    QVBoxLayout *m_layout = nullptr;
    QHash<QString, QWidget *> m_inputs;
    QHash<QString, QJsonValue> m_values;
    QString m_titleValue;
    QWidget *m_done = nullptr;
    QString m_builtFor; // schema/view signature the form was built for
};
