#include "DatabaseBody.h"

#include "PropertyEditors.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>

namespace
{
constexpr int kBarH = 36;
constexpr int kChipsH = 32;
constexpr int kTitleH = 40;
constexpr int kGutter = 28; // table row grips sit left of the column

FlatButton *toolButton(const QString &icon, const QString &tip)
{
    auto *b = new FlatButton(icon);
    b->setToolTip(tip);
    return b;
}
} // namespace

DatabaseBody::DatabaseBody(QWidget *parent, bool inlineMode)
    : QWidget(parent)
    , m_inline(inlineMode)
{
    if (m_inline) {
        m_titleBar = new QWidget(this);
        auto *tl = new QHBoxLayout(m_titleBar);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(6);
        auto *edit = new QLineEdit;
        edit->setObjectName(QStringLiteral("dbTitle"));
        edit->setFont(Theme::displayFont(19, QFont::Bold));
        edit->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; color: #f1f1f5; }"));
        edit->setPlaceholderText(tr("Untitled database"));
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] {
            if (Workspace *ws = Workspace::instance(); ws && ws->exists(m_dbId) && ws->title(m_dbId) != edit->text())
                ws->setTitle(m_dbId, edit->text());
        });
        tl->addWidget(edit, 1);
        m_openFull = toolButton(QStringLiteral("external"), tr("Open as full page"));
        connect(m_openFull, &FlatButton::clicked, this, [this] { emit openPage(m_dbId); });
        tl->addWidget(m_openFull);
    }

    m_tabsBar = new QWidget(this);
    auto *barLayout = new QHBoxLayout(m_tabsBar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    barLayout->setSpacing(2);
    auto *tabsHolder = new QWidget;
    m_tabsLayout = new QHBoxLayout(tabsHolder);
    m_tabsLayout->setContentsMargins(0, 0, 0, 0);
    m_tabsLayout->setSpacing(2);
    barLayout->addWidget(tabsHolder);
    barLayout->addStretch();
    m_lockBadge = new FlatButton(QStringLiteral("lock"), tr("Locked"));
    m_lockBadge->setToolTip(tr("Properties and views can't be changed. Click to unlock."));
    m_lockBadge->hide();
    m_search = new SearchField(tr("Type to search…"));
    m_search->setFixedWidth(200);
    m_search->hide();
    m_filterBtn = toolButton(QStringLiteral("filter"), tr("Filter"));
    m_sortBtn = toolButton(QStringLiteral("sort"), tr("Sort"));
    m_searchBtn = toolButton(QStringLiteral("search"), tr("Search"));
    m_settingsBtn = toolButton(QStringLiteral("more"), tr("View settings"));
    m_newBtn = new FlatButton(QStringLiteral("plus"), tr("New"));
    m_newBtn->setFilled(true);
    m_newBtn->setToolTip(tr("New page"));
    m_newMenuBtn = new FlatButton(QStringLiteral("chevron-down"));
    m_newMenuBtn->setFilled(true);
    m_newMenuBtn->setToolTip(tr("Templates"));
    for (QWidget *w : {static_cast<QWidget *>(m_lockBadge), static_cast<QWidget *>(m_search), static_cast<QWidget *>(m_filterBtn),
                       static_cast<QWidget *>(m_sortBtn), static_cast<QWidget *>(m_searchBtn), static_cast<QWidget *>(m_settingsBtn)})
        barLayout->addWidget(w);
    barLayout->addSpacing(4);
    barLayout->addWidget(m_newBtn);
    barLayout->addWidget(m_newMenuBtn);

    m_bulkBar = new QWidget(this);
    m_bulkLayout = new QHBoxLayout(m_bulkBar);
    m_bulkLayout->setContentsMargins(0, 0, 0, 0);
    m_bulkLayout->setSpacing(4);
    m_bulkBar->hide();

    m_chipsBar = new QWidget(this);
    m_chipsLayout = new QHBoxLayout(m_chipsBar);
    m_chipsLayout->setContentsMargins(0, 0, 0, 0);
    m_chipsLayout->setSpacing(6);

    m_hscroll = new QScrollArea(this);
    m_hscroll->setFrameStyle(QFrame::NoFrame);
    m_hscroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_hscroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // The sideways scrollbar shows itself while the pointer is over the view.
    m_hscroll->horizontalScrollBar()->setStyleSheet(QStringLiteral("QScrollBar::handle:horizontal { background: transparent; }"));
    m_hscroll->installEventFilter(this);
    m_hscroll->setWidgetResizable(false);
    m_hscroll->viewport()->setAutoFillBackground(false);
    m_hcontent = new QWidget;
    m_hscroll->setWidget(m_hcontent);
    m_hcontent->setAutoFillBackground(false);
    connect(m_hscroll->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int v) {
        if (m_active)
            m_active->setScrollOffset(v);
    });

    connect(m_filterBtn, &FlatButton::clicked, this, [this] { showFilters(m_filterBtn->mapToGlobal(QPoint(-200, m_filterBtn->height() + 4))); });
    connect(m_sortBtn, &FlatButton::clicked, this, [this] { showSorts(m_sortBtn->mapToGlobal(QPoint(-200, m_sortBtn->height() + 4))); });
    connect(m_settingsBtn, &FlatButton::clicked, this, [this] { showSettings(m_settingsBtn->mapToGlobal(QPoint(-280, m_settingsBtn->height() + 4))); });
    connect(m_newBtn, &FlatButton::clicked, this, &DatabaseBody::createRowAndOpen);
    connect(m_newMenuBtn, &FlatButton::clicked, this, [this] { newMenu(m_newMenuBtn->mapToGlobal(QPoint(m_newMenuBtn->width(), m_newMenuBtn->height() + 4))); });
    connect(m_lockBadge, &FlatButton::clicked, this, [this] {
        Workspace *ws = Workspace::instance();
        Schema s = ws->schema(m_dbId);
        s.locked = false;
        ws->setSchema(m_dbId, s);
        emit toast(tr("Database unlocked"));
    });
    // The magnifier opens into the field (which has its own); an empty field
    // folds back when it loses focus, and Esc clears and folds it.
    connect(m_searchBtn, &FlatButton::clicked, this, [this] {
        m_search->show();
        m_searchBtn->hide();
        m_search->setFocus();
        relayout();
    });
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, &DatabaseBody::scheduleRefresh);

    // Esc clears ticked rows.
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, [this] {
        if (m_active && !m_active->selectedRows().isEmpty())
            m_active->clearSelection();
    });

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(30);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DatabaseBody::refresh);
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::schemaChanged, this, [this](const QString &id) {
            if (id == m_dbId) {
                scheduleRefresh();
                return;
            }
            // Relations and rollups show other databases' values.
            for (const Property &p : m_schema.props)
                if (p.type == PropType::Relation && p.relationDb == id) {
                    scheduleRefresh();
                    break;
                }
        });
        connect(ws, &Workspace::propsChanged, this, [this](const QString &id) {
            const PageMeta *m = Workspace::instance()->page(id);
            if (id == m_dbId || (m && m->parentId == m_dbId))
                scheduleRefresh();
            else if (m && m->isRow())
                for (const Property &p : m_schema.props)
                    if ((p.type == PropType::Relation && p.relationDb == m->parentId) || p.type == PropType::Rollup) {
                        scheduleRefresh();
                        break;
                    }
        });
        connect(ws, &Workspace::treeChanged, this, &DatabaseBody::scheduleRefresh);
        connect(ws, &Workspace::pageChanged, this, [this](const QString &id) {
            const PageMeta *m = Workspace::instance()->page(id);
            if (id == m_dbId || (m && m->parentId == m_dbId))
                scheduleRefresh();
        });
        connect(ws, &Workspace::contentChanged, this, [this](const QString &id, QObject *) {
            const PageMeta *m = Workspace::instance()->page(id);
            if (m && m->parentId == m_dbId)
                scheduleRefresh();
        });
    }
}

void DatabaseBody::setDatabase(const QString &databaseId)
{
    m_dbId = databaseId;
    m_viewId.clear();
    m_extraRows = 0;
    m_search->clear();
    m_search->hide();
    refresh();
}

void DatabaseBody::setLinkedViews(const QList<View> &views, std::function<void(const QList<View> &)> save)
{
    m_linkedViews = views;
    m_saveLinked = std::move(save);
    scheduleRefresh();
}

void DatabaseBody::setColumn(int left, int width)
{
    if (left == m_left && width == m_width)
        return;
    m_left = left;
    m_width = width;
    if (m_active)
        relayout();
}

void DatabaseBody::setSearchText(const QString &text)
{
    m_search->setVisible(!text.isEmpty());
    m_search->setText(text);
    m_searchBtn->setVisible(text.isEmpty() && currentView().type != ViewType::Form);
    refresh();
}

QList<View> DatabaseBody::views() const
{
    if (m_saveLinked) {
        // Linked views fall back to the database's first view.
        if (m_linkedViews.isEmpty() && !m_schema.views.isEmpty())
            return {m_schema.views.first()};
        return m_linkedViews;
    }
    return m_schema.views;
}

View DatabaseBody::currentView() const
{
    const QList<View> vs = views();
    for (const View &v : vs)
        if (v.id == m_viewId)
            return v;
    return vs.isEmpty() ? View() : vs.first();
}

void DatabaseBody::saveViews(const QList<View> &views)
{
    if (m_saveLinked) {
        m_linkedViews = views;
        m_saveLinked(views);
        scheduleRefresh();
        return;
    }
    Workspace *ws = Workspace::instance();
    Schema s = ws->schema(m_dbId);
    s.views = views;
    ws->setSchema(m_dbId, s);
    m_schema = ws->schema(m_dbId);
}

void DatabaseBody::editViewById(const QString &viewId, const std::function<void(View &)> &fn)
{
    QList<View> vs = m_saveLinked ? views() : Workspace::instance()->schema(m_dbId).views;
    for (View &v : vs)
        if (v.id == viewId) {
            fn(v);
            saveViews(vs);
            return;
        }
}

void DatabaseBody::editView(const std::function<void(View &)> &fn)
{
    editViewById(currentView().id, fn);
}

void DatabaseBody::addView(ViewType type)
{
    QList<View> vs = views();
    Schema probe = m_schema;
    probe.views = vs;
    View v = Schema::makeView(probe, type);
    vs.append(v);
    m_viewId = v.id;
    saveViews(vs);
    refresh();
}

void DatabaseBody::duplicateView(const QString &viewId)
{
    QList<View> vs = views();
    Schema probe = m_schema;
    probe.views = vs;
    for (int i = 0; i < vs.size(); ++i)
        if (vs[i].id == viewId) {
            View copy = vs[i];
            copy.id = probe.newViewId();
            copy.name = tr("%1 (copy)").arg(vs[i].name);
            vs.insert(i + 1, copy);
            m_viewId = copy.id;
            saveViews(vs);
            refresh();
            return;
        }
}

void DatabaseBody::deleteView(const QString &viewId)
{
    QList<View> vs = views();
    if (vs.size() <= 1)
        return;
    vs.removeIf([&](const View &x) { return x.id == viewId; });
    if (m_viewId == viewId)
        m_viewId = vs.first().id;
    saveViews(vs);
    refresh();
}

void DatabaseBody::scheduleRefresh()
{
    if (!m_dbId.isEmpty())
        m_refreshTimer.start();
}

DbView *DatabaseBody::viewFor(ViewType type)
{
    DbView *&v = m_views[int(type)];
    if (!v) {
        switch (type) {
        case ViewType::Board:
            v = new BoardView(m_hcontent);
            break;
        case ViewType::List:
            v = new ListView(m_hcontent);
            break;
        case ViewType::Gallery:
            v = new GalleryView(m_hcontent);
            break;
        case ViewType::Calendar:
            v = new CalendarView(m_hcontent);
            break;
        case ViewType::Timeline:
            v = new TimelineView(m_hcontent);
            break;
        case ViewType::Chart:
            v = new ChartView(m_hcontent);
            break;
        case ViewType::Feed:
            v = new FeedView(m_hcontent);
            break;
        case ViewType::Form:
            v = new FormView(m_hcontent);
            break;
        default:
            v = new TableView(m_hcontent);
        }
        v->setObjectName(QStringLiteral("dbview-") + viewTypeKey(type));
        connect(v, &DbView::openRow, this, &DatabaseBody::openRow);
        connect(v, &DbView::geometryNeedsUpdate, this, &DatabaseBody::relayout);
        connect(v, &DbView::createRow, this, &DatabaseBody::createRow);
        connect(v, &DbView::selectionChanged, this, [this] {
            rebuildBulkBar();
            relayout();
        });
        connect(v, &DbView::loadMore, this, [this] {
            m_extraRows += qMax(10, currentView().loadLimit);
            refresh();
        });
    }
    return v;
}

void DatabaseBody::refresh()
{
    Workspace *ws = Workspace::instance();
    if (!ws || !ws->exists(m_dbId))
        return;
    m_schema = ws->schema(m_dbId);
    if (m_schema.views.isEmpty())
        return;
    const QList<View> vs = views();
    bool found = false;
    for (const View &v : vs)
        if (v.id == m_viewId)
            found = true;
    if (!found)
        m_viewId = vs.first().id;
    const View view = currentView();
    QList<RowRef> rows;
    for (const PageMeta *m : ws->rows(m_dbId))
        rows.append({m, ws->props(m->id)});
    ViewContext ctx;
    ctx.databaseId = m_dbId;
    ctx.schema = m_schema;
    ctx.view = view;
    ctx.rows = applyView(m_schema, view, rows, m_search->text());
    ctx.sorted = !view.sorts.isEmpty();
    ctx.editable = !m_schema.locked;
    ctx.inlineMode = m_inline;
    ctx.limit = m_inline ? qMax(1, view.loadLimit) + m_extraRows : 0;
    const QString viewId = view.id;
    ctx.editView = [this, viewId](const std::function<void(View &)> &fn) { editViewById(viewId, fn); };
    DbView *target = viewFor(view.type);
    for (DbView *v : std::as_const(m_views))
        v->setVisible(v == target);
    if (m_active != target) {
        m_hscroll->horizontalScrollBar()->setValue(0);
        target->setScrollOffset(0);
    }
    m_active = target;
    const int avail = m_width > 0 ? m_width : width();
    target->resize(qMax(200, avail), target->height());
    target->setContext(ctx);
    if (auto *title = m_titleBar ? m_titleBar->findChild<QLineEdit *>(QStringLiteral("dbTitle")) : nullptr; title && !title->hasFocus())
        title->setText(ws->title(m_dbId));
    m_lockBadge->setVisible(m_schema.locked);
    rebuildTabs();
    rebuildChips();
    rebuildBulkBar();
    relayout();
    if (!m_editAfterRefresh.isEmpty()) {
        const QString id = m_editAfterRefresh;
        m_editAfterRefresh.clear();
        QTimer::singleShot(0, this, [this, id] {
            if (!m_active)
                return;
            const QRect r = m_active->titleRectFor(id);
            if (!r.isEmpty())
                m_active->editTitleInline(id, r);
        });
    }
}

void DatabaseBody::rebuildTabs()
{
    while (QLayoutItem *item = m_tabsLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_tabButtons.clear();
    const QList<View> vs = views();
    for (const View &v : vs) {
        auto *tab = new FlatButton(viewTypeIcon(v.type), v.name);
        tab->setCheckable(true);
        tab->setChecked(v.id == m_viewId);
        tab->setTextSize(13);
        tab->setContextMenuPolicy(Qt::CustomContextMenu);
        tab->setObjectName(QStringLiteral("viewtab"));
        const QString id = v.id;
        connect(tab, &FlatButton::clicked, this, [this, id, tab] {
            if (id == m_viewId)
                viewMenu(id, tab->mapToGlobal(QPoint(0, tab->height() + 4)));
            else
                selectView(id);
        });
        connect(tab, &QWidget::customContextMenuRequested, this, [this, id, tab](const QPoint &pos) { viewMenu(id, tab->mapToGlobal(pos)); });
        m_tabsLayout->addWidget(tab);
        m_tabButtons.append({id, tab});
    }
    // Views that don't fit wait behind "N more…".
    m_moreTabs = new FlatButton(QString(), QString());
    m_moreTabs->setTextSize(13);
    m_moreTabs->setMuted(true);
    m_moreTabs->hide();
    connect(m_moreTabs, &FlatButton::clicked, this, [this] {
        QMenu *menu = Theme::makeMenu(this);
        const QList<View> all = views();
        for (const auto &[id, tab] : std::as_const(m_tabButtons)) {
            if (!tab->isHidden())
                continue;
            for (const View &v : all)
                if (v.id == id)
                    menu->addAction(Icons::icon(viewTypeIcon(v.type)), v.name, this, [this, id = id] { selectView(id); });
        }
        menu->popup(m_moreTabs->mapToGlobal(QPoint(0, m_moreTabs->height() + 4)));
    });
    m_tabsLayout->addWidget(m_moreTabs);
    if (!m_schema.locked) {
        auto *add = new FlatButton(QStringLiteral("plus"));
        add->setToolTip(tr("Add a view"));
        connect(add, &FlatButton::clicked, this, [this, add] { addViewMenu(add->mapToGlobal(QPoint(0, add->height() + 4))); });
        m_tabsLayout->addWidget(add);
    }
    const View v = currentView();
    m_filterBtn->setChecked(!v.filters.isEmpty());
    m_sortBtn->setChecked(!v.sorts.isEmpty());
    const bool form = v.type == ViewType::Form;
    m_filterBtn->setVisible(!form);
    m_sortBtn->setVisible(!form);
    m_searchBtn->setVisible(!form && m_search->isHidden());
}

void DatabaseBody::fitTabs()
{
    if (!m_moreTabs || m_tabButtons.isEmpty())
        return;
    // Room left of the toolbar buttons.
    int controls = 16;
    for (QWidget *w : {static_cast<QWidget *>(m_lockBadge), static_cast<QWidget *>(m_search), static_cast<QWidget *>(m_filterBtn),
                       static_cast<QWidget *>(m_sortBtn), static_cast<QWidget *>(m_searchBtn), static_cast<QWidget *>(m_settingsBtn),
                       static_cast<QWidget *>(m_newBtn), static_cast<QWidget *>(m_newMenuBtn)})
        if (!w->isHidden())
            controls += w->sizeHint().width() + 2;
    const int addW = m_schema.locked ? 0 : 34;
    const int avail = m_tabsBar->width() - controls - addW;
    QList<int> widths;
    int total = 0;
    for (const auto &entry : std::as_const(m_tabButtons)) {
        widths.append(entry.second->sizeHint().width() + 2);
        total += widths.last();
    }
    QSet<int> shown;
    if (total <= avail) {
        for (int i = 0; i < widths.size(); ++i)
            shown.insert(i);
    } else {
        // Keep the current view, then as many others (in order) as fit
        // beside the "more" button.
        m_moreTabs->setText(tr("%n more…", nullptr, int(m_tabButtons.size())));
        const int moreW = m_moreTabs->sizeHint().width() + 2;
        int used = moreW;
        for (int i = 0; i < m_tabButtons.size(); ++i)
            if (m_tabButtons[i].first == m_viewId) {
                shown.insert(i);
                used += widths[i];
            }
        for (int i = 0; i < m_tabButtons.size(); ++i) {
            if (shown.contains(i))
                continue;
            if (used + widths[i] > avail)
                break;
            shown.insert(i);
            used += widths[i];
        }
    }
    const int hidden = int(m_tabButtons.size() - shown.size());
    for (int i = 0; i < m_tabButtons.size(); ++i)
        m_tabButtons[i].second->setVisible(shown.contains(i));
    m_moreTabs->setText(tr("%n more…", nullptr, hidden));
    m_moreTabs->setVisible(hidden > 0);
}

void DatabaseBody::rebuildChips()
{
    while (QLayoutItem *item = m_chipsLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const View v = currentView();
    bool any = false;
    auto chip = [this](const QString &icon, const QString &text) {
        auto *c = new FlatButton(icon, text);
        c->setFilled(true);
        c->setTextSize(12.5);
        c->setRadius(13);
        c->setObjectName(QStringLiteral("dbchip"));
        m_chipsLayout->addWidget(c);
        return c;
    };
    if (!v.sorts.isEmpty()) {
        QString text;
        if (v.sorts.size() == 1) {
            const Property *p = m_schema.prop(v.sorts.first().prop);
            text = p ? p->name : tr("Sort");
        } else {
            text = tr("%n sorts", nullptr, int(v.sorts.size()));
        }
        auto *c = chip(v.sorts.size() == 1 ? (v.sorts.first().descending ? QStringLiteral("arrow-down") : QStringLiteral("arrow-up")) : QStringLiteral("sort"),
                       text);
        connect(c, &FlatButton::clicked, this, [this, c] { showSorts(c->mapToGlobal(QPoint(0, c->height() + 4))); });
        any = true;
    }
    if (!v.sorts.isEmpty() && !v.filters.isEmpty()) {
        auto *sep = new QWidget;
        sep->setFixedSize(1, 18);
        sep->setStyleSheet(QStringLiteral("background: #2c2c34;"));
        m_chipsLayout->addWidget(sep);
    }
    for (const FilterRule &f : v.filters) {
        const QString text = f.group ? tr("%n rule(s)", nullptr, filterRuleCount(f.rules)) : DbPopovers::describeRule(m_schema, f);
        auto *c = chip(f.group ? QStringLiteral("filter") : QStringLiteral("filter"), text);
        connect(c, &FlatButton::clicked, this, [this, c] { showFilters(c->mapToGlobal(QPoint(0, c->height() + 4))); });
        any = true;
    }
    if (any) {
        auto *add = new FlatButton(QStringLiteral("plus"), tr("Filter"));
        add->setTextSize(12.5);
        add->setMuted(true);
        connect(add, &FlatButton::clicked, this, [this, add] { showFilters(add->mapToGlobal(QPoint(0, add->height() + 4))); });
        m_chipsLayout->addWidget(add);
        if (v.filterAny && v.filters.size() > 1) {
            auto *any = new QLabel(tr("matching any"));
            any->setFont(Theme::uiFont(12));
            any->setStyleSheet(QStringLiteral("color: #7a7a86;"));
            m_chipsLayout->addWidget(any);
        }
    }
    m_chipsLayout->addStretch();
    m_chipsBar->setVisible(any);
}

void DatabaseBody::rebuildBulkBar()
{
    while (QLayoutItem *item = m_bulkLayout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const QStringList selected = m_active ? m_active->selectedRows() : QStringList();
    m_bulkBar->setVisible(!selected.isEmpty());
    m_tabsBar->setVisible(selected.isEmpty());
    if (selected.isEmpty())
        return;
    auto *count = new QLabel(tr("%n selected", nullptr, int(selected.size())));
    count->setFont(Theme::uiFont(13, QFont::DemiBold));
    count->setStyleSheet(QStringLiteral("color: #e6e6ec; padding: 0 10px;"));
    m_bulkLayout->addWidget(count);
    auto *edit = new FlatButton(QStringLiteral("pencil"), tr("Edit property"));
    connect(edit, &FlatButton::clicked, this, [this, edit] {
        if (m_active)
            DbPopovers::bulkEdit(this, edit->mapToGlobal(QPoint(0, edit->height() + 4)), m_dbId, m_active->selectedRows());
    });
    m_bulkLayout->addWidget(edit);
    auto *dup = new FlatButton(QStringLiteral("copy"), tr("Duplicate"));
    connect(dup, &FlatButton::clicked, this, [this] {
        if (!m_active)
            return;
        Workspace *ws = Workspace::instance();
        const QStringList ids = m_active->selectedRows();
        ws->batch([&] {
            for (const QString &id : ids)
                ws->duplicate(id);
        });
        m_active->clearSelection();
        emit toast(tr("Duplicated %n page(s)", nullptr, int(ids.size())));
    });
    m_bulkLayout->addWidget(dup);
    auto *del = new FlatButton(QStringLiteral("trash"), tr("Delete"));
    connect(del, &FlatButton::clicked, this, [this] {
        if (!m_active)
            return;
        Workspace *ws = Workspace::instance();
        const QStringList ids = m_active->selectedRows();
        ws->batch([&] {
            for (const QString &id : ids)
                ws->trash(id);
        });
        m_active->clearSelection();
        emit toast(tr("Moved %n page(s) to Trash", nullptr, int(ids.size())));
    });
    m_bulkLayout->addWidget(del);
    m_bulkLayout->addStretch();
    auto *clear = new FlatButton(QStringLiteral("close"));
    clear->setToolTip(tr("Clear selection (Esc)"));
    connect(clear, &FlatButton::clicked, this, [this] {
        if (m_active)
            m_active->clearSelection();
    });
    m_bulkLayout->addWidget(clear);
}

void DatabaseBody::relayout()
{
    if (m_relayouting || !m_active)
        return;
    m_relayouting = true;
    const int W = width();
    const int colW = m_width > 0 ? m_width : W - m_left;
    int y = 0;
    if (m_titleBar) {
        m_titleBar->setGeometry(m_left, 4, colW, kTitleH - 8);
        m_openFull->setVisible(true);
        y += kTitleH;
    }
    // Narrow databases (in a column) keep room for their tabs: search moves
    // out of the toolbar, New shows just its icon and templates hide.
    const bool compact = colW < 460;
    const bool form = currentView().type == ViewType::Form;
    m_searchBtn->setVisible(!compact && !form && m_search->isHidden());
    m_newMenuBtn->setVisible(!compact);
    m_newBtn->setText(compact ? QString() : tr("New"));
    m_tabsBar->setGeometry(m_left - 8, y, colW + 8, kBarH);
    fitTabs();
    m_bulkBar->setGeometry(m_left - 4, y, colW + 4, kBarH);
    y += kBarH + 2;
    if (!m_chipsBar->isHidden()) {
        m_chipsBar->setGeometry(m_left, y + 2, colW, kChipsH - 4);
        y += kChipsH;
    }
    // A hairline under the toolbar, like Notion's.
    y += 6;
    const bool table = qobject_cast<TableView *>(m_active) != nullptr;
    const int x = table ? m_left - kGutter : m_left;
    const int avail = table ? colW + kGutter : colW;
    m_active->resize(avail, m_active->height());
    const int contentW = m_active->contentWidth(avail);
    const bool needsScroll = contentW > avail + 1;
    if (needsScroll) {
        // Wide layouts scroll sideways across the whole body.
        m_active->setGeometry(x, 0, contentW, m_active->height());
        m_hcontent->setFixedSize(x + contentW + qMax(0, W - m_left - colW), m_active->height());
        m_hscroll->setGeometry(0, y, W, m_active->height() + 12);
    } else {
        m_active->setGeometry(0, 0, avail, m_active->height());
        m_hcontent->setFixedSize(avail, m_active->height());
        m_hscroll->setGeometry(x, y, avail, m_active->height());
    }
    y += m_hscroll->height() + (m_inline ? 4 : 0);
    const int old = height();
    setFixedHeight(y);
    m_relayouting = false;
    update();
    if (old != y)
        emit heightChanged();
}

bool DatabaseBody::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search) {
        // Esc belongs to the field while it's open, not to the view's shortcut.
        if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            event->accept();
            return true;
        }
        const bool esc = event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
        if (esc || (event->type() == QEvent::FocusOut && m_search->text().isEmpty())) {
            if (esc && !m_search->text().isEmpty())
                m_search->clear();
            m_search->hide();
            m_searchBtn->setVisible(currentView().type != ViewType::Form);
            relayout();
            return esc;
        }
    }
    if (watched == m_hscroll && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)) {
        m_hscroll->horizontalScrollBar()->setStyleSheet(
            event->type() == QEvent::Enter ? QString() : QStringLiteral("QScrollBar::handle:horizontal { background: transparent; }"));
    }
    return QWidget::eventFilter(watched, event);
}

void DatabaseBody::resizeEvent(QResizeEvent *)
{
    if (!m_active)
        return;
    relayout();
    m_active->setContext(m_active->context());
    relayout();
}

void DatabaseBody::paintEvent(QPaintEvent *)
{
    if (!m_active)
        return;
    QPainter p(this);
    const int y = m_hscroll->geometry().top() - 3;
    const int colW = m_width > 0 ? m_width : width() - m_left;
    p.setPen(QPen(Theme::colors().hairline, 1));
    p.drawLine(QPointF(m_left, y + 0.5), QPointF(m_left + colW, y + 0.5));
}

void DatabaseBody::selectView(const QString &viewId)
{
    m_viewId = viewId;
    m_extraRows = 0;
    if (m_active)
        m_active->clearSelection();
    refresh();
}

void DatabaseBody::selectViewNamed(const QString &name)
{
    for (const View &v : views())
        if (v.name == name)
            selectView(v.id);
}

void DatabaseBody::viewMenu(const QString &viewId, const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    View v;
    for (const View &x : views())
        if (x.id == viewId)
            v = x;
    if (v.id.isEmpty())
        return;
    Theme::addMenuSection(menu, v.name);
    if (!m_schema.locked) {
        menu->addAction(Icons::icon(QStringLiteral("settings")), tr("Edit view…"), this, [this, viewId, globalPos] {
            selectView(viewId);
            showSettings(globalPos);
        });
        menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Rename"), this, [this, viewId, v] {
            QString name = v.name;
            if (textDialog(window(), tr("Rename view"), tr("A name for this view."), &name) && !name.trimmed().isEmpty())
                editViewById(viewId, [name](View &x) { x.name = name.trimmed(); });
        });
        menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate view"), this, [this, viewId] { duplicateView(viewId); });
    }
    menu->addAction(Icons::icon(QStringLiteral("link")), tr("Copy link to view"), this, [this, viewId] {
        QApplication::clipboard()->setText(QStringLiteral("nebula://page/%1?v=%2").arg(m_dbId, viewId));
        emit toast(tr("Link copied"));
    });
    if (!m_schema.locked) {
        QAction *del = menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete view"), this, [this, viewId] { deleteView(viewId); });
        del->setEnabled(views().size() > 1);
    }
    menu->popup(globalPos);
}

void DatabaseBody::addViewMenu(const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    Theme::addMenuSection(menu, tr("Add a view"));
    for (ViewType t : {ViewType::Table, ViewType::Board, ViewType::List, ViewType::Gallery, ViewType::Calendar, ViewType::Timeline, ViewType::Chart,
                       ViewType::Feed, ViewType::Form})
        menu->addAction(Icons::icon(viewTypeIcon(t)), viewTypeName(t), this, [this, t] { addView(t); });
    menu->popup(globalPos);
}

void DatabaseBody::showFilters(const QPoint &globalPos)
{
    DbPopovers::showFilters(window(), globalPos, m_dbId, [this] { return currentView(); }, [this](const std::function<void(View &)> &fn) { editView(fn); });
}

void DatabaseBody::showSorts(const QPoint &globalPos)
{
    DbPopovers::showSorts(window(), globalPos, m_dbId, [this] { return currentView(); }, [this](const std::function<void(View &)> &fn) { editView(fn); });
}

void DatabaseBody::showSettings(const QPoint &globalPos, const QString &page)
{
    DbPopovers::showSettings(this, globalPos, page);
}

void DatabaseBody::newMenu(const QPoint &globalPos)
{
    Workspace *ws = Workspace::instance();
    QMenu *menu = Theme::makeMenu(this);
    Theme::addMenuSection(menu, tr("Templates for %1").arg(ws->title(m_dbId).isEmpty() ? tr("this database") : ws->title(m_dbId)));
    const QList<const PageMeta *> templates = ws->templates(m_dbId);
    for (const PageMeta *t : templates) {
        const QString id = t->id;
        QMenu *sub = menu->addMenu(Icons::icon(QStringLiteral("template")),
                                   (t->title.isEmpty() ? tr("Untitled template") : t->title) + (m_schema.defaultTemplate == id ? tr("  · Default") : QString()));
        Theme::polishMenu(sub);
        sub->addAction(Icons::icon(QStringLiteral("plus")), tr("New page from template"), this, [this, id] {
            const QString row = Workspace::instance()->createRowFromTemplate(m_dbId, id);
            if (!row.isEmpty())
                openRow(row);
        });
        sub->addAction(Icons::icon(QStringLiteral("pencil")), tr("Edit template"), this, [this, id] { emit openPage(id); });
        sub->addAction(Icons::icon(QStringLiteral("star")), m_schema.defaultTemplate == id ? tr("Remove as default") : tr("Set as default"), this, [this, id] {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(m_dbId);
            s.defaultTemplate = s.defaultTemplate == id ? QString() : id;
            ws->setSchema(m_dbId, s);
        });
        sub->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate"), this, [id] { Workspace::instance()->duplicate(id); });
        sub->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete"), this, [id] { Workspace::instance()->trash(id); });
    }
    menu->addAction(Icons::icon(QStringLiteral("page-empty")), tr("Empty page"), this, [this] {
        const QString id = Workspace::instance()->createRow(m_dbId);
        openRow(id);
    });
    menu->addSeparator();
    menu->addAction(Icons::icon(QStringLiteral("plus")), tr("New template"), this, [this] {
        const QString id = Workspace::instance()->createTemplate(m_dbId, tr("New template"));
        if (!id.isEmpty())
            emit openPage(id);
    });
    // Hangs from the button's right edge, like the toolbar it belongs to.
    menu->popup(globalPos - QPoint(menu->sizeHint().width(), 0));
}

void DatabaseBody::openRow(const QString &rowId)
{
    const QString mode = currentView().openIn;
    // The main window shows rows in a side or centre peek; without one, open the page.
    bool handled = false;
    if (QWidget *w = window(); w && w->metaObject()->indexOfMethod("openRowIn(QString,QString)") >= 0)
        handled = QMetaObject::invokeMethod(w, "openRowIn", Q_ARG(QString, rowId), Q_ARG(QString, mode));
    if (!handled)
        emit openPage(rowId);
}

void DatabaseBody::createRow(const QJsonObject &props, bool open)
{
    Workspace *ws = Workspace::instance();
    // New rows match the view's simple filters so they don't vanish.
    QJsonObject values = props;
    const View v = currentView();
    if (!v.filterAny)
        for (const FilterRule &f : v.filters) {
            const Property *p = m_schema.prop(f.prop);
            if (f.group || !p || values.contains(p->id) || propTypeIsComputed(p->type))
                continue;
            if (f.op == QLatin1String("checked")) {
                values.insert(p->id, true);
                continue;
            }
            if (f.value.isNull() || f.value.isUndefined())
                continue;
            if (f.op == QLatin1String("is") && (p->type == PropType::Select || p->type == PropType::Status))
                values.insert(p->id, f.value.isArray() ? f.value.toArray().first() : f.value);
            else if (f.op == QLatin1String("contains") && (p->type == PropType::MultiSelect || p->type == PropType::Person || p->type == PropType::Relation))
                values.insert(p->id, f.value.isArray() ? f.value : QJsonArray{f.value});
            else if (f.op == QLatin1String("is") && p->type == PropType::Date)
                values.insert(p->id, f.value);
        }
    QString id;
    if (!m_schema.defaultTemplate.isEmpty() && ws->exists(m_schema.defaultTemplate)) {
        id = ws->createRowFromTemplate(m_dbId, m_schema.defaultTemplate);
        if (!id.isEmpty() && !values.isEmpty()) {
            QJsonObject merged = ws->props(id);
            for (auto it = values.begin(); it != values.end(); ++it)
                merged.insert(it.key(), it.value());
            ws->setProps(id, merged);
        }
    } else {
        id = ws->createRow(m_dbId, QString(), values);
    }
    if (id.isEmpty())
        return;
    if (open) {
        openRow(id);
        return;
    }
    m_editAfterRefresh = id;
    m_refreshTimer.stop();
    refresh();
}

void DatabaseBody::createRowAndOpen()
{
    createRow({}, true);
}
