#pragma once

#include "DbViews.h"

#include <QTimer>
#include <QWidget>

#include <functional>

class FlatButton;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class SearchField;

// The working part of a database: view tabs, the toolbar (filter, sort,
// search, settings, New with templates), filter and sort chips, the bulk
// action bar and the active layout. Full-page databases put a page header
// above it; inline databases and linked views live in a page.
//
// A linked view shows a database with views of its own, kept by whoever
// shows it (a block's data), not in the database.
class DatabaseBody : public QWidget
{
    Q_OBJECT
    friend class DbUiTests;
public:
    explicit DatabaseBody(QWidget *parent = nullptr, bool inlineMode = false);

    void setDatabase(const QString &databaseId);
    QString databaseId() const { return m_dbId; }
    void setLinkedViews(const QList<View> &views, std::function<void(const QList<View> &)> save);
    bool isLinked() const { return bool(m_saveLinked); }
    // Where tabs and toolbar sit, and how wide the content column is. Wide
    // layouts scroll sideways across the whole body.
    void setColumn(int left, int width);
    QString currentViewId() const { return m_viewId; }
    void selectView(const QString &viewId);
    void selectViewNamed(const QString &name);
    DbView *activeView() const { return m_active; }
    const Schema &schema() const { return m_schema; }
    // Views shown in the tabs (the database's own, or the linked copies).
    QList<View> views() const;
    View currentView() const;
    // Applies a change to the current view and saves it.
    void editView(const std::function<void(View &)> &fn);
    void editViewById(const QString &viewId, const std::function<void(View &)> &fn);
    // Adds a view of a layout and selects it.
    void addView(ViewType type);
    void duplicateView(const QString &viewId);
    void deleteView(const QString &viewId);
    void createRow(const QJsonObject &props, bool open);
    void createRowAndOpen();
    void refresh();
    void scheduleRefresh();
    bool isInline() const { return m_inline; }
    // The search box text (tests).
    void setSearchText(const QString &text);

signals:
    void openPage(const QString &pageId);
    void heightChanged();
    void toast(const QString &message);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void paintEvent(QPaintEvent *e) override;

private:
    void relayout();
    void rebuildTabs();
    void fitTabs();
    void rebuildChips();
    void rebuildBulkBar();
    DbView *viewFor(ViewType type);
    void viewMenu(const QString &viewId, const QPoint &globalPos);
    void addViewMenu(const QPoint &globalPos);
    void showFilters(const QPoint &globalPos);
    void showSorts(const QPoint &globalPos);
    void showSettings(const QPoint &globalPos, const QString &page = {});
    void newMenu(const QPoint &globalPos);
    void openRow(const QString &rowId);
    void saveViews(const QList<View> &views);

    bool m_inline = false;
    QString m_dbId;
    QString m_viewId;
    Schema m_schema;
    QList<View> m_linkedViews;
    std::function<void(const QList<View> &)> m_saveLinked;
    int m_left = 0;
    int m_width = 0;
    QWidget *m_titleBar = nullptr;
    QLabel *m_title = nullptr;
    FlatButton *m_openFull = nullptr;
    QWidget *m_tabsBar;
    QHBoxLayout *m_tabsLayout;
    QList<QPair<QString, FlatButton *>> m_tabButtons; // view id → tab
    FlatButton *m_moreTabs = nullptr;                // "2 more…" when tabs overflow
    QWidget *m_bulkBar;
    QHBoxLayout *m_bulkLayout;
    QWidget *m_chipsBar;
    QHBoxLayout *m_chipsLayout;
    FlatButton *m_filterBtn;
    FlatButton *m_sortBtn;
    FlatButton *m_searchBtn;
    FlatButton *m_settingsBtn;
    FlatButton *m_newBtn;
    FlatButton *m_newMenuBtn;
    FlatButton *m_lockBadge;
    SearchField *m_search;
    QScrollArea *m_hscroll;
    QWidget *m_hcontent;
    QHash<int, DbView *> m_views;
    DbView *m_active = nullptr;
    QTimer m_refreshTimer;
    QString m_editAfterRefresh;
    int m_extraRows = 0; // "Load more" pages
    bool m_relayouting = false;
};

// Advanced filters: rules and groups joined by AND/OR, nested up to three
// levels; the popover edits a view's filter tree.
namespace DbPopovers
{
using ViewEdit = std::function<void(const std::function<void(View &)> &)>;
using ViewGet = std::function<View()>;
void showFilters(QWidget *parent, const QPoint &globalPos, const QString &dbId, ViewGet get, ViewEdit edit);
void showSorts(QWidget *parent, const QPoint &globalPos, const QString &dbId, ViewGet get, ViewEdit edit);
// The view settings menu (layout, properties, group, sub-items, load limit,
// open pages in, lock, templates). `page` opens a sub-page directly.
void showSettings(DatabaseBody *body, const QPoint &globalPos, const QString &page = {});
// A one-line summary of a filter rule, for chips.
QString describeRule(const Schema &schema, const FilterRule &rule);
// Sets a property on several rows (bulk edit).
void bulkEdit(QWidget *parent, const QPoint &globalPos, const QString &dbId, const QStringList &rowIds);
} // namespace DbPopovers
