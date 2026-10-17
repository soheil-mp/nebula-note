#include "Overlays.h"

#include "core/Settings.h"
#include "core/Templates.h"
#include "core/Workspace.h"
#include "editor/SpellChecker.h"
#include "platform/WindowEffects.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QUrl>
#include <QVBoxLayout>

// ---- ModalOverlay ----------------------------------------------------------------

ModalOverlay::ModalOverlay(QWidget *host, const QSize &cardSize, int topOffset)
    : QWidget(host)
    , m_cardSize(cardSize)
    , m_top(topOffset)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    m_card = new QWidget(this);
    m_card->setAttribute(Qt::WA_TranslucentBackground);
    host->installEventFilter(this);
    m_fade.setDuration(Theme::animMs(160));
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this] {
        relayout();
        update();
    });
    hide();
}

void ModalOverlay::relayout()
{
    QWidget *host = parentWidget();
    setGeometry(host->rect());
    const QSize s(qMin(m_cardSize.width(), width() - 40), qMin(m_cardSize.height(), height() - 60));
    const double t = m_fade.state() == QAbstractAnimation::Running ? m_fade.currentValue().toDouble() : 1.0;
    const int top = m_top >= 0 ? qMin(m_top, height() - s.height() - 20) : (height() - s.height()) / 2;
    m_card->setGeometry((width() - s.width()) / 2, top + int((1.0 - t) * 10), s.width(), s.height());
}

void ModalOverlay::open()
{
    relayout();
    show();
    raise();
    setFocus();
    m_fade.setDirection(QAbstractAnimation::Forward);
    m_fade.start();
}

void ModalOverlay::dismiss()
{
    emit dismissed();
    close();
}

bool ModalOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void ModalOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double t = m_fade.state() == QAbstractAnimation::Running ? m_fade.currentValue().toDouble() : 1.0;
    // Round the dim layer to the window's corners.
    QPainterPath frame;
    const bool max = window()->isMaximized() || window()->isFullScreen();
    frame.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), max ? 0 : Theme::kRadius - 1, max ? 0 : Theme::kRadius - 1);
    p.fillPath(frame, QColor(4, 4, 6, int(150 * t)));
    p.setOpacity(t);
    const QRectF card = m_card->geometry();
    Theme::paintSoftShadow(p, card, 16, 24, 2.0);
    Theme::paintCard(p, card, 16, QColor(0x17, 0x17, 0x1B));
}

void ModalOverlay::mousePressEvent(QMouseEvent *e)
{
    if (!m_card->geometry().contains(e->position().toPoint()))
        dismiss();
}

void ModalOverlay::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        dismiss();
        return;
    }
    QWidget::keyPressEvent(e);
}

// ---- QuickFind ---------------------------------------------------------------------

class ResultList : public QWidget
{
    Q_OBJECT
public:
    struct Item {
        QString id;    // "page:<id>", "action:<id>", "create:<title>"
        QString icon;
        bool pageIcon = false;
        QString title;
        QString sub;
        QString section;
        QString hint;
    };
    explicit ResultList(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }
    void setItems(const QList<Item> &items)
    {
        m_items = items;
        m_current = items.isEmpty() ? -1 : 0;
        // Rows and section headers.
        m_rows.clear();
        int y = 6;
        QString section;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items[i].section != section) {
                section = m_items[i].section;
                m_rows.append({-1, section, y, 30});
                y += 30;
            }
            const int h = m_items[i].sub.isEmpty() ? 38 : 50;
            m_rows.append({i, QString(), y, h});
            y += h;
        }
        setFixedHeight(qMax(80, y + 8));
        update();
    }
    void move(int d)
    {
        if (m_items.isEmpty())
            return;
        m_current = (m_current + d + int(m_items.size())) % int(m_items.size());
        for (const Row &r : m_rows)
            if (r.item == m_current)
                emit ensureVisible(r.y, r.h);
        update();
    }
    QString current() const { return m_current >= 0 ? m_items[m_current].id : QString(); }
    QList<Item> items() const { return m_items; }

signals:
    void activated(const QString &id);
    void ensureVisible(int y, int h);

protected:
    struct Row {
        int item;
        QString header;
        int y, h;
    };
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_items.isEmpty()) {
            Cosmos::paintPlanet(p, QPointF(width() / 2.0, 30), 12, 0.9, true);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(c.fog);
            p.drawText(QRectF(0, 50, width(), 24), Qt::AlignCenter, tr("No results"));
            return;
        }
        for (const Row &r : m_rows) {
            if (r.item < 0) {
                p.setFont(Theme::uiFont(11.5, QFont::Medium));
                p.setPen(c.fog);
                p.drawText(QRectF(18, r.y + 8, width() - 36, 20), Qt::AlignVCenter, r.header);
                continue;
            }
            const Item &it = m_items[r.item];
            const QRectF row(8, r.y, width() - 16, r.h - 2);
            if (r.item == m_current) {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x26, 0x26, 0x2C));
                p.drawRoundedRect(row, 8, 8);
            }
            const QRectF iconRect(row.left() + 12, row.top() + (it.sub.isEmpty() ? (row.height() - 18) / 2 : 9), 18, 18);
            if (it.pageIcon)
                Icons::paintPageIcon(p, iconRect, it.icon, c.mist, QStringLiteral("page"));
            else
                Icons::paint(p, iconRect, it.icon, c.mist);
            double right = row.right() - 12;
            if (!it.hint.isEmpty()) {
                p.setFont(Theme::uiFont(11.5));
                p.setPen(c.dust);
                const double hw = QFontMetricsF(p.font()).horizontalAdvance(it.hint);
                p.drawText(QRectF(right - hw, row.top(), hw, it.sub.isEmpty() ? row.height() : 36), Qt::AlignVCenter, it.hint);
                right -= hw + 12;
            }
            p.setFont(Theme::uiFont(14, QFont::Medium));
            p.setPen(r.item == m_current ? c.starlight : c.moon);
            const double x = iconRect.right() + 12;
            p.drawText(QRectF(x, row.top() + (it.sub.isEmpty() ? 0 : 6), right - x, it.sub.isEmpty() ? row.height() : 22), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(it.title, Qt::ElideRight, right - x));
            if (!it.sub.isEmpty()) {
                p.setFont(Theme::uiFont(12));
                p.setPen(c.fog);
                p.drawText(QRectF(x, row.top() + 26, row.right() - x - 12, 18), Qt::AlignVCenter,
                           QFontMetricsF(p.font()).elidedText(it.sub, Qt::ElideRight, row.right() - x - 12));
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        for (const Row &r : m_rows)
            if (r.item >= 0 && e->position().y() >= r.y && e->position().y() < r.y + r.h && r.item != m_current) {
                m_current = r.item;
                update();
            }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (const Row &r : m_rows)
            if (r.item >= 0 && e->position().y() >= r.y && e->position().y() < r.y + r.h)
                emit activated(m_items[r.item].id);
    }

private:
    QList<Item> m_items;
    QList<Row> m_rows;
    int m_current = -1;
};

QuickFind::QuickFind(QWidget *host)
    : ModalOverlay(host, QSize(660, 470), int(host->height() * 0.12))
{
    auto *v = new QVBoxLayout(card());
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_field = new SearchField(tr("Search pages, or type a command…"));
    m_field->setFont(Theme::uiFont(16.5));
    m_field->setMinimumHeight(58);
    m_field->setTextMargins(30, 0, 0, 0);
    m_field->setIconGeometry(19, 18);
    m_field->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; border-bottom: 1px solid #26262c; "
                                          "border-radius: 0; padding: 0 18px; color: #f1f1f5; }"));
    v->addWidget(m_field);
    // Filters, shown while searching.
    m_filters = new QWidget;
    m_filters->setObjectName(QStringLiteral("searchFilters"));
    auto *fh = new QHBoxLayout(m_filters);
    fh->setContentsMargins(14, 6, 14, 6);
    fh->setSpacing(4);
    auto filterButton = [&](const QString &icon) {
        auto *b = new FlatButton(icon);
        b->setTextSize(12.5);
        b->setRadius(13);
        fh->addWidget(b);
        return b;
    };
    m_sort = filterButton(QStringLiteral("sort"));
    m_titleOnly = filterButton(QStringLiteral("title"));
    m_titleOnly->setCheckable(true);
    m_inPage = filterButton(QStringLiteral("page"));
    m_edited = filterButton(QStringLiteral("pencil"));
    m_created = filterButton(QStringLiteral("calendar"));
    fh->addStretch();
    m_filters->setStyleSheet(QStringLiteral("#searchFilters { border-bottom: 1px solid #22222a; }"));
    v->addWidget(m_filters);
    connect(m_titleOnly, &FlatButton::toggled, this, [this](bool on) {
        m_options.titleOnly = on;
        updateFilterLabels();
        refresh();
    });
    connect(m_sort, &FlatButton::clicked, this, [this] { filterMenu(m_sort, QStringLiteral("sort")); });
    connect(m_inPage, &FlatButton::clicked, this, [this] { filterMenu(m_inPage, QStringLiteral("in")); });
    connect(m_edited, &FlatButton::clicked, this, [this] { filterMenu(m_edited, QStringLiteral("edited")); });
    connect(m_created, &FlatButton::clicked, this, [this] { filterMenu(m_created, QStringLiteral("created")); });
    updateFilterLabels();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    m_list = new ResultList(scroll);
    scroll->setWidget(m_list);
    m_list->setAutoFillBackground(false);
    v->addWidget(scroll);
    auto *footer = new QLabel(tr("↑↓ to navigate   ·   ↵ to open   ·   Esc to close"));
    footer->setFont(Theme::uiFont(11.5));
    footer->setStyleSheet(QStringLiteral("color: #4a4a53; padding: 9px 18px; border-top: 1px solid #22222a;"));
    v->addWidget(footer);
    connect(m_field, &QLineEdit::textChanged, this, &QuickFind::refresh);
    connect(m_list, &ResultList::ensureVisible, scroll, [scroll](int y, int h) { scroll->ensureVisible(0, y + h / 2, 0, h); });
    connect(m_list, &ResultList::activated, this, [this](const QString &id) {
        if (id.startsWith(QLatin1String("page:")))
            emit openPage(id.mid(5));
        else if (id.startsWith(QLatin1String("action:")))
            emit action(id.mid(7));
        else if (id.startsWith(QLatin1String("create:")))
            emit createPage(id.mid(7));
        dismiss();
    });
    m_field->installEventFilter(this);
    refresh();
    QTimer::singleShot(0, m_field, qOverload<>(&QWidget::setFocus));
}

void QuickFind::setSearchOptions(const Workspace::SearchOptions &options)
{
    m_options = options;
    m_titleOnly->blockSignals(true);
    m_titleOnly->setChecked(options.titleOnly);
    m_titleOnly->blockSignals(false);
    updateFilterLabels();
    refresh();
}

QStringList QuickFind::resultIds() const
{
    QStringList out;
    for (const ResultList::Item &it : m_list->items())
        if (it.id.startsWith(QLatin1String("page:")))
            out.append(it.id.mid(5));
    return out;
}

void QuickFind::updateFilterLabels()
{
    Workspace *ws = Workspace::instance();
    static const char *const sorts[] = {QT_TRANSLATE_NOOP("QuickFind", "Best match"), QT_TRANSLATE_NOOP("QuickFind", "Last edited: newest"),
                                        QT_TRANSLATE_NOOP("QuickFind", "Last edited: oldest"), QT_TRANSLATE_NOOP("QuickFind", "Created: newest"),
                                        QT_TRANSLATE_NOOP("QuickFind", "Created: oldest")};
    m_sort->setText(QCoreApplication::translate("QuickFind", sorts[int(m_options.sort)]));
    m_titleOnly->setText(tr("Title only"));
    m_inPage->setText(m_options.inPage.isEmpty() ? tr("In: all pages") : tr("In: %1").arg(ws->title(m_options.inPage).left(24)));
    m_inPage->setFilled(!m_options.inPage.isEmpty());
    auto dateLabel = [this](const QString &key, const QString &prefix) {
        if (key == QLatin1String("today"))
            return tr("%1: today").arg(prefix);
        if (key == QLatin1String("week"))
            return tr("%1: past week").arg(prefix);
        if (key == QLatin1String("month"))
            return tr("%1: past month").arg(prefix);
        return prefix;
    };
    m_edited->setText(dateLabel(m_editedKey, tr("Edited")));
    m_edited->setFilled(!m_editedKey.isEmpty());
    m_created->setText(dateLabel(m_createdKey, tr("Created")));
    m_created->setFilled(!m_createdKey.isEmpty());
    m_sort->setFilled(m_options.sort != Workspace::SearchOptions::BestMatch);
}

void QuickFind::filterMenu(FlatButton *anchor, const QString &which)
{
    QMenu *menu = Theme::makeMenu(this);
    Workspace *ws = Workspace::instance();
    auto since = [](const QString &key) -> qint64 {
        const QDateTime now = QDateTime::currentDateTime();
        if (key == QLatin1String("today"))
            return QDateTime(now.date(), QTime(0, 0)).toMSecsSinceEpoch();
        if (key == QLatin1String("week"))
            return now.addDays(-7).toMSecsSinceEpoch();
        if (key == QLatin1String("month"))
            return now.addMonths(-1).toMSecsSinceEpoch();
        return 0;
    };
    if (which == QLatin1String("sort")) {
        const QList<QPair<Workspace::SearchOptions::Sort, QString>> sorts = {
            {Workspace::SearchOptions::BestMatch, tr("Best match")},
            {Workspace::SearchOptions::EditedNewest, tr("Last edited: newest first")},
            {Workspace::SearchOptions::EditedOldest, tr("Last edited: oldest first")},
            {Workspace::SearchOptions::CreatedNewest, tr("Created: newest first")},
            {Workspace::SearchOptions::CreatedOldest, tr("Created: oldest first")}};
        for (const auto &[sort, name] : sorts) {
            QAction *a = menu->addAction(name);
            a->setCheckable(true);
            a->setChecked(m_options.sort == sort);
            connect(a, &QAction::triggered, this, [this, sort = sort] {
                m_options.sort = sort;
                updateFilterLabels();
                refresh();
            });
        }
    } else if (which == QLatin1String("in")) {
        menu->addAction(Icons::icon(QStringLiteral("globe")), tr("All pages"), this, [this] {
            m_options.inPage.clear();
            updateFilterLabels();
            refresh();
        });
        Theme::addMenuSection(menu, tr("Search inside"));
        QStringList candidates;
        for (const PageMeta *m : ws->recent(6))
            candidates.append(m->id);
        for (const PageMeta *m : ws->children({}))
            if (!candidates.contains(m->id))
                candidates.append(m->id);
        for (const QString &id : candidates) {
            const PageMeta *m = ws->page(id);
            if (!m || m->isRow())
                continue;
            menu->addAction(Icons::icon(m->isDatabase() ? QStringLiteral("database") : QStringLiteral("page")), ws->title(id), this, [this, id] {
                m_options.inPage = id;
                updateFilterLabels();
                refresh();
            });
        }
    } else {
        const bool edited = which == QLatin1String("edited");
        const QString current = edited ? m_editedKey : m_createdKey;
        for (const auto &[key, name] : QList<QPair<QString, QString>>{
                 {QString(), tr("Any time")}, {QStringLiteral("today"), tr("Today")}, {QStringLiteral("week"), tr("Past week")}, {QStringLiteral("month"), tr("Past month")}}) {
            QAction *a = menu->addAction(name);
            a->setCheckable(true);
            a->setChecked(current == key);
            connect(a, &QAction::triggered, this, [this, edited, key = key, since] {
                if (edited) {
                    m_editedKey = key;
                    m_options.editedAfter = since(key);
                } else {
                    m_createdKey = key;
                    m_options.createdAfter = since(key);
                }
                updateFilterLabels();
                refresh();
            });
        }
    }
    menu->popup(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
}

bool QuickFind::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        switch (ke->key()) {
        case Qt::Key_Down:
            m_list->move(1);
            return true;
        case Qt::Key_Up:
            m_list->move(-1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (!m_list->current().isEmpty())
                emit m_list->activated(m_list->current());
            return true;
        case Qt::Key_Escape:
            dismiss();
            return true;
        default:
            break;
        }
    }
    return ModalOverlay::eventFilter(watched, event);
}

void QuickFind::refresh()
{
    Workspace *ws = Workspace::instance();
    const QString q = m_field->text().trimmed();
    QList<ResultList::Item> items;
    auto pathOf = [ws](const QString &id) {
        QStringList names;
        for (const QString &a : ws->ancestors(id))
            names.append(ws->title(a));
        return names.join(QStringLiteral(" / "));
    };
    if (q.isEmpty()) {
        for (const PageMeta *m : ws->recent(7))
            items.append({QStringLiteral("page:") + m->id, m->icon, true, ws->title(m->id), pathOf(m->id), tr("Recent"),
                          relativeTime(m->visited)});
    } else {
        for (const auto &hit : ws->search(q, m_options, 30)) {
            const PageMeta *m = ws->page(hit.id);
            QString sub = hit.snippet.isEmpty() ? pathOf(hit.id) : hit.snippet;
            items.append({QStringLiteral("page:") + hit.id, m ? m->icon : QString(), true, ws->title(hit.id), sub,
                          hit.titleMatch ? tr("Pages") : tr("In page content"), QString()});
        }
        items.append({QStringLiteral("create:") + q, QStringLiteral("plus"), false, tr("Create page “%1”").arg(q), QString(), tr("Create"),
                      QString()});
    }
    struct A {
        const char *id, *icon, *title, *hint, *keys;
    };
    static const A actions[] = {
        {"new", "page-empty", QT_TRANSLATE_NOOP("QuickFind", "New page"), "Ctrl+N", "create add blank"},
        {"home", "home", QT_TRANSLATE_NOOP("QuickFind", "Go home"), "", "dashboard start"},
        {"templates", "template", QT_TRANSLATE_NOOP("QuickFind", "Templates"), "", "gallery starter"},
        {"import", "download", QT_TRANSLATE_NOOP("QuickFind", "Import Notion, Markdown or CSV"), "", "notion zip markdown csv"},
        {"settings", "settings", QT_TRANSLATE_NOOP("QuickFind", "Settings"), "Ctrl+,", "preferences options appearance"},
        {"sidebar", "sidebar", QT_TRANSLATE_NOOP("QuickFind", "Toggle sidebar"), "Ctrl+\\", "hide show"},
        {"trash", "trash", QT_TRANSLATE_NOOP("QuickFind", "Open trash"), "", "deleted restore"},
    };
    for (const A &a : actions) {
        const QString title = QCoreApplication::translate("QuickFind", a.title);
        if (!q.isEmpty() && Workspace::fuzzyScore(q, title) == 0 && Workspace::fuzzyScore(q, QString::fromLatin1(a.keys)) == 0)
            continue;
        items.append({QStringLiteral("action:") + QString::fromLatin1(a.id), QString::fromLatin1(a.icon), false, title, QString(),
                      tr("Actions"), QString::fromLatin1(a.hint)});
    }
    m_list->setItems(items);
}

// ---- SettingsPanel -------------------------------------------------------------------

namespace
{
QLabel *heading(const QString &text)
{
    auto *l = new QLabel(text);
    l->setFont(Theme::displayFont(20, QFont::DemiBold));
    l->setStyleSheet(QStringLiteral("color: #f1f1f5; padding-bottom: 6px;"));
    return l;
}

QWidget *settingRow(const QString &title, const QString &detail, QWidget *control)
{
    auto *row = new QWidget;
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 10, 0, 10);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto *t = new QLabel(title);
    t->setFont(Theme::uiFont(14, QFont::Medium));
    t->setStyleSheet(QStringLiteral("color: #e6e6eb;"));
    texts->addWidget(t);
    if (!detail.isEmpty()) {
        auto *d = new QLabel(detail);
        d->setWordWrap(true);
        d->setFont(Theme::uiFont(12.5));
        d->setStyleSheet(QStringLiteral("color: #8a8a94;"));
        texts->addWidget(d);
    }
    h->addLayout(texts, 1);
    h->addSpacing(16);
    h->addWidget(control, 0, Qt::AlignVCenter);
    row->setStyleSheet(QStringLiteral("QWidget { border-bottom: none; }"));
    return row;
}

QFrame *separator()
{
    auto *f = new QFrame;
    f->setFixedHeight(1);
    f->setStyleSheet(QStringLiteral("background: #222228;"));
    return f;
}
}

SettingsPanel::SettingsPanel(QWidget *host)
    : ModalOverlay(host, QSize(820, 580))
{
    auto *h = new QHBoxLayout(card());
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto *nav = new QWidget;
    nav->setFixedWidth(210);
    nav->setStyleSheet(QStringLiteral("background: rgba(0,0,0,0.18); border-top-left-radius: 16px; border-bottom-left-radius: 16px;"));
    auto *nv = new QVBoxLayout(nav);
    nv->setContentsMargins(10, 18, 10, 18);
    nv->setSpacing(2);
    auto *title = new QLabel(tr("Settings"));
    title->setFont(Theme::uiFont(12, QFont::Medium));
    title->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 0 10px 8px 10px; background: transparent;"));
    nv->addWidget(title);
    m_stack = new QStackedWidget;
    const QList<QPair<QString, QString>> pages = {{QStringLiteral("palette"), tr("Appearance")},
                                                  {QStringLiteral("database"), tr("Workspace & data")},
                                                  {QStringLiteral("command"), tr("Shortcuts")},
                                                  {QStringLiteral("info"), tr("About")}};
    QList<FlatButton *> buttons;
    for (int i = 0; i < pages.size(); ++i) {
        auto *b = new FlatButton(pages[i].first, pages[i].second);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setTextSize(13.5);
        b->setMinimumHeight(34);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        nv->addWidget(b);
        buttons.append(b);
        connect(b, &FlatButton::clicked, this, [this, i] { m_stack->setCurrentIndex(i); });
    }
    nv->addStretch();
    buttons.first()->setChecked(true);
    h->addWidget(nav);
    auto wrap = [](QWidget *page) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameStyle(QFrame::NoFrame);
        scroll->viewport()->setAutoFillBackground(false);
        page->setAutoFillBackground(false);
        scroll->setWidget(page);
        page->setAutoFillBackground(false);
        return scroll;
    };
    m_stack->addWidget(wrap(appearancePage()));
    m_stack->addWidget(wrap(workspacePage()));
    m_stack->addWidget(wrap(shortcutsPage()));
    m_stack->addWidget(wrap(aboutPage()));
    h->addWidget(m_stack, 1);
    auto *close = new FlatButton(QStringLiteral("close"), QString(), card());
    close->setGeometry(820 - 46, 12, 32, 32);
    connect(close, &FlatButton::clicked, this, &ModalOverlay::dismiss);
}

QWidget *SettingsPanel::appearancePage()
{
    Settings &s = Settings::instance();
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(36, 30, 36, 30);
    v->setSpacing(0);
    v->addWidget(heading(tr("Appearance")));

    auto *stars = new QSlider(Qt::Horizontal);
    stars->setRange(0, 100);
    stars->setValue(qRound(s.starfield * 100));
    stars->setFixedWidth(132);
    connect(stars, &QSlider::valueChanged, this, [](int v) {
        Settings::instance().starfield = v / 100.0;
        Settings::instance().apply();
    });
    v->addWidget(settingRow(tr("Starfield"), tr("How bright the stars behind the sidebar and covers glow."), stars));
    v->addWidget(separator());

    auto *translucent = new Switch;
    translucent->setChecked(s.translucentSidebar);
    connect(translucent, &Switch::toggled, this, [](bool on) {
        Settings::instance().translucentSidebar = on;
        Settings::instance().apply();
    });
    v->addWidget(settingRow(tr("Translucent sidebar"),
                            WindowEffects::blurAvailable() ? tr("Blur the desktop behind the sidebar.") : tr("Needs a compositor with blur (KWin)."),
                            translucent));
    v->addWidget(separator());

    auto *motion = new Switch;
    motion->setChecked(s.reduceMotion);
    connect(motion, &Switch::toggled, this, [](bool on) {
        Settings::instance().reduceMotion = on;
        Settings::instance().apply();
    });
    v->addWidget(settingRow(tr("Reduce motion"), tr("Turn off fades, slides and the sparkle on checked to-dos."), motion));
    v->addWidget(separator());

    auto *size = new QComboBox;
    size->setFont(Theme::uiFont(13));
    for (int pct : {90, 100, 110, 120, 130})
        size->addItem(QStringLiteral("%1%").arg(pct), pct);
    size->setCurrentIndex(qMax(0, size->findData(s.textScale)));
    connect(size, &QComboBox::activated, this, [size] {
        Settings::instance().textScale = size->currentData().toInt();
        Settings::instance().apply();
    });
    v->addWidget(settingRow(tr("Text size"), tr("Scales page text and the column width."), size));
    v->addWidget(separator());

    auto *backlinks = new Switch;
    backlinks->setChecked(s.showBacklinks);
    connect(backlinks, &Switch::toggled, this, [](bool on) {
        Settings::instance().showBacklinks = on;
        Settings::instance().apply();
    });
    v->addWidget(settingRow(tr("Show backlinks"), tr("List the pages that link to a page at its bottom."), backlinks));
    v->addWidget(separator());

    auto *spelling = new Switch;
    const SpellChecker &checker = SpellChecker::instance();
    spelling->setChecked(s.spellCheck && checker.isAvailable());
    spelling->setEnabled(checker.isAvailable());
    connect(spelling, &Switch::toggled, this, [](bool on) {
        Settings::instance().spellCheck = on;
        Settings::instance().apply();
    });
    v->addWidget(settingRow(tr("Check spelling"),
                            checker.isAvailable() ? tr("Underline misspelled words as you type (%1).").arg(QLocale(checker.language()).nativeLanguageName())
                                                  : tr("Needs Hunspell and a dictionary installed."),
                            spelling));
    v->addStretch();
    return page;
}

QWidget *SettingsPanel::workspacePage()
{
    Workspace *ws = Workspace::instance();
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(36, 30, 36, 30);
    v->setSpacing(0);
    v->addWidget(heading(tr("Workspace & data")));
    auto *name = new QLineEdit(ws->name());
    name->setFont(Theme::uiFont(13.5));
    name->setFixedWidth(220);
    connect(name, &QLineEdit::editingFinished, this, [name] { Workspace::instance()->setName(name->text()); });
    v->addWidget(settingRow(tr("Workspace name"), tr("Shown at the top of the sidebar."), name));
    v->addWidget(separator());
    auto *folder = new FlatButton(QStringLiteral("folder"), tr("Open folder"));
    folder->setFilled(true);
    connect(folder, &FlatButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(Workspace::instance()->directory())); });
    v->addWidget(settingRow(tr("Data folder"), ws->directory(), folder));
    v->addWidget(separator());
    auto *backup = new FlatButton(QStringLiteral("shield"), tr("Back up now"));
    backup->setFilled(true);
    connect(backup, &FlatButton::clicked, this, [backup] {
        const QString path = Workspace::instance()->backup(true);
        backup->setText(path.isEmpty() ? QObject::tr("Backup failed") : QObject::tr("Saved ✓"));
    });
    v->addWidget(settingRow(tr("Backups"), tr("A copy is made automatically once a day; the last seven are kept in backups/."), backup));
    v->addWidget(separator());
    auto *imp = new FlatButton(QStringLiteral("download"), tr("Import…"));
    imp->setFilled(true);
    connect(imp, &FlatButton::clicked, this, [this] {
        dismiss();
        emit importRequested();
    });
    v->addWidget(settingRow(tr("Import"), tr("A Notion export (.zip or folder), Markdown files or folders, or a CSV file."), imp));
    v->addWidget(separator());
    auto *exp = new FlatButton(QStringLiteral("upload"), tr("Export all…"));
    exp->setFilled(true);
    connect(exp, &FlatButton::clicked, this, [this] {
        dismiss();
        emit exportAllRequested();
    });
    v->addWidget(settingRow(tr("Export everything"), tr("Every page as Markdown in folders, databases as CSV, images alongside."), exp));
    v->addStretch();
    return page;
}

QWidget *SettingsPanel::shortcutsPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(36, 30, 36, 30);
    v->addWidget(heading(tr("Shortcuts")));
    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("Ctrl+P / Ctrl+K"), tr("Quick Find")},
        {QStringLiteral("Ctrl+N"), tr("New page")},
        {QStringLiteral("Ctrl+[  /  Ctrl+]"), tr("Back / forward")},
        {QStringLiteral("Ctrl+\\"), tr("Toggle sidebar")},
        {QStringLiteral("Ctrl+F"), tr("Find in page")},
        {QStringLiteral("Ctrl+Shift+U"), tr("Go to parent page")},
        {QStringLiteral("/"), tr("Command menu")},
        {QStringLiteral("@  or  [["), tr("Mention a page")},
        {QStringLiteral("Ctrl+B · I · U · E"), tr("Bold · italic · underline · code")},
        {QStringLiteral("Ctrl+Shift+S"), tr("Strikethrough")},
        {QStringLiteral("Ctrl+K"), tr("Link the selection")},
        {QStringLiteral("Ctrl+Shift+0 … 9"), tr("Turn into text, headings, to-do, lists, toggle, code, page")},
        {QStringLiteral("Tab / Shift+Tab"), tr("Nest / un-nest")},
        {QStringLiteral("Ctrl+Shift+↑ / ↓"), tr("Move block")},
        {QStringLiteral("Ctrl+D"), tr("Duplicate block")},
        {QStringLiteral("Ctrl+Enter"), tr("Check to-do · open toggle · open page")},
        {QStringLiteral("Ctrl+Alt+T"), tr("Expand or collapse all toggles")},
        {QStringLiteral("Shift+Enter"), tr("Line break inside a block")},
        {QStringLiteral("Esc"), tr("Select the block (then arrows, Delete, Ctrl+D)")},
        {QStringLiteral("Ctrl+Alt+0 … 9"), tr("The same, Notion's other layout")},
        {QStringLiteral("Ctrl+Shift+E"), tr("Inline equation")},
        {QStringLiteral("Ctrl+Shift+M"), tr("Comment on the selection")},
        {QStringLiteral("Ctrl+Shift+H"), tr("Apply the last color again")},
        {QStringLiteral("Ctrl+/"), tr("Block menu (turn into, color…)")},
        {QStringLiteral("Ctrl+T  /  Ctrl+W"), tr("New tab / close tab")},
        {QStringLiteral("Ctrl+Tab"), tr("Next tab")},
        {QStringLiteral("Ctrl+Shift+N"), tr("New window")},
        {QStringLiteral("F11"), tr("Full screen")},
    };
    auto *gridHolder = new QWidget;
    auto *grid = new QGridLayout(gridHolder);
    grid->setContentsMargins(0, 0, 12, 0);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(10);
    for (int i = 0; i < rows.size(); ++i) {
        auto *k = new QLabel(rows[i].first);
        k->setFont(Theme::monoFont(12.5));
        k->setStyleSheet(QStringLiteral("color: #f1f1f5; background: #23232a; border: 1px solid #2e2e35; border-radius: 6px; padding: 3px 8px;"));
        auto *d = new QLabel(rows[i].second);
        d->setFont(Theme::uiFont(13.5));
        d->setStyleSheet(QStringLiteral("color: #a9a9b2;"));
        grid->addWidget(k, i, 0, Qt::AlignLeft);
        grid->addWidget(d, i, 1);
    }
    grid->setColumnStretch(1, 1);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    gridHolder->setAutoFillBackground(false);
    scroll->setWidget(gridHolder);
    v->addWidget(scroll, 1);
    return page;
}

QWidget *SettingsPanel::aboutPage()
{
    auto *page = new QWidget;
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(36, 40, 36, 30);
    v->setSpacing(8);
    auto *emblem = new QLabel;
    QPixmap pm(QSize(96, 96) * devicePixelRatioF());
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        QSvgRenderer(QStringLiteral(":/icons/app/nebula.svg")).render(&p);
    }
    pm.setDevicePixelRatio(devicePixelRatioF());
    emblem->setPixmap(pm);
    v->addWidget(emblem);
    auto *name = new QLabel(QStringLiteral("Nebula Note"));
    name->setFont(Theme::displayFont(28, QFont::Bold));
    name->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    v->addWidget(name);
    auto *ver = new QLabel(tr("Version %1 · Notes, docs and databases in deep space").arg(QStringLiteral(NEBULA_VERSION)));
    ver->setFont(Theme::uiFont(13.5));
    ver->setStyleSheet(QStringLiteral("color: #8a8a94;"));
    v->addWidget(ver);
    auto *body = new QLabel(tr("Built with Qt %1. Your workspace is a single SQLite file on this computer; nothing leaves it.\n\n"
                               "Fonts: Inter (SIL Open Font License). Covers are drawn by the app.")
                                .arg(QString::fromLatin1(qVersion())));
    body->setWordWrap(true);
    body->setFont(Theme::uiFont(13));
    body->setStyleSheet(QStringLiteral("color: #a9a9b2; padding-top: 12px;"));
    v->addWidget(body);
    v->addStretch();
    return page;
}

// ---- TemplateGallery ----------------------------------------------------------------

namespace
{
class TemplateGrid : public QWidget
{
public:
    std::function<void(const QString &)> picked;
    explicit TemplateGrid(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        m_items = Templates::all();
    }
    void relayout()
    {
        m_rects.clear();
        const int cols = 2;
        const double gap = 12, w = (width() - gap) / cols, h = 80;
        for (int i = 0; i < m_items.size(); ++i)
            m_rects.append(QRectF((i % cols) * (w + gap), (i / cols) * (h + gap), w, h));
        setFixedHeight(int(((m_items.size() + 1) / 2) * (h + gap)));
    }

protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < m_items.size(); ++i) {
            const QRectF r = m_rects.value(i);
            const bool hover = i == m_hover;
            Theme::paintCard(p, r, 12, hover ? QColor(0x23, 0x23, 0x29) : QColor(0x1B, 0x1B, 0x20));
            const QRectF tile(r.left() + 14, r.center().y() - 22, 44, 44);
            QRadialGradient g(tile.center() - QPointF(10, 12), 50);
            g.setColorAt(0, QColor(0x33, 0x33, 0x3B));
            g.setColorAt(1, QColor(0x1A, 0x1A, 0x1F));
            p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.08), 1));
            p.setBrush(g);
            p.drawRoundedRect(tile, 11, 11);
            Icons::paint(p, tile.adjusted(12, 12, -12, -12), m_items[i].icon, c.starlight);
            // Name over a description of up to two lines, centred as a block.
            const QFont descFont = Theme::uiFont(12.5);
            const double textW = r.right() - 16 - (tile.right() + 14);
            const QStringList desc = Theme::wrapLines(descFont, m_items[i].description, textW);
            const double lh = 17;
            const double top = r.center().y() - (22 + 2 + desc.size() * lh) / 2;
            p.setFont(Theme::uiFont(14, QFont::DemiBold));
            p.setPen(c.starlight);
            p.drawText(QRectF(tile.right() + 14, top, textW, 22), Qt::AlignVCenter, m_items[i].name);
            if (m_items[i].database) {
                const double tw = QFontMetricsF(p.font()).horizontalAdvance(m_items[i].name);
                Theme::paintPill(p, QPointF(tile.right() + 22 + tw, top + 1), tr("Database"), 1, 20);
            }
            p.setFont(descFont);
            p.setPen(c.haze);
            for (int k = 0; k < desc.size(); ++k)
                p.drawText(QRectF(tile.right() + 14, top + 24 + k * lh, textW, lh), Qt::AlignVCenter, desc[k]);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        for (int i = 0; i < m_rects.size(); ++i)
            if (m_rects[i].contains(e->position()))
                h = i;
        if (h != m_hover) {
            m_hover = h;
            setCursor(h >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (m_hover >= 0 && picked)
            picked(m_items[m_hover].id);
    }

private:
    QList<Templates::Info> m_items;
    QList<QRectF> m_rects;
    int m_hover = -1;
};
}

TemplateGallery::TemplateGallery(QWidget *host)
    : ModalOverlay(host, QSize(820, 586))
{
    auto *v = new QVBoxLayout(card());
    v->setContentsMargins(28, 24, 28, 24);
    v->setSpacing(6);
    auto *title = new QLabel(tr("Templates"));
    title->setFont(Theme::displayFont(22, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    v->addWidget(title);
    auto *sub = new QLabel(tr("Start from a shape that works. Everything is editable afterwards."));
    sub->setFont(Theme::uiFont(13.5));
    sub->setStyleSheet(QStringLiteral("color: #8a8a94; padding-bottom: 12px;"));
    v->addWidget(sub);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    auto *grid = new TemplateGrid(scroll);
    grid->picked = [this](const QString &id) {
        emit picked(id);
        dismiss();
    };
    scroll->setWidget(grid);
    grid->setAutoFillBackground(false);
    v->addWidget(scroll);
    auto *close = new FlatButton(QStringLiteral("close"), QString(), card());
    close->setGeometry(820 - 48, 14, 32, 32);
    connect(close, &FlatButton::clicked, this, &ModalOverlay::dismiss);
}

// ---- TrashPopover -------------------------------------------------------------------

class TrashList : public QWidget
{
    Q_OBJECT
public:
    explicit TrashList(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }
    void setIds(const QStringList &ids)
    {
        m_ids = ids;
        setFixedHeight(qMax(90, int(ids.size()) * 40 + 6));
        update();
    }

signals:
    void restore(const QString &id);
    void remove(const QString &id);

protected:
    QRectF restoreRect(int i) const { return QRectF(width() - 66, i * 40 + 8, 26, 26); }
    QRectF removeRect(int i) const { return QRectF(width() - 36, i * 40 + 8, 26, 26); }
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Workspace *ws = Workspace::instance();
        if (m_ids.isEmpty()) {
            Cosmos::paintPlanet(p, QPointF(width() / 2.0, 30), 12, 0.9, true);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(c.fog);
            p.drawText(QRectF(0, 52, width(), 24), Qt::AlignCenter, tr("The trash is empty"));
            return;
        }
        for (int i = 0; i < m_ids.size(); ++i) {
            const QRectF r(0, i * 40 + 2, width(), 38);
            if (i == m_hover)
                Theme::paintHover(p, r, 1.0, 7);
            const PageMeta *m = ws->page(m_ids[i]);
            Icons::paintPageIcon(p, QRectF(10, r.center().y() - 9, 18, 18), m ? m->icon : QString(), c.mist,
                                 m && m->isDatabase() ? QStringLiteral("database") : QStringLiteral("page"));
            p.setFont(Theme::uiFont(13.5, QFont::Medium));
            p.setPen(c.moon);
            const double right = width() - 76;
            p.drawText(QRectF(38, r.top() + 2, right - 38, 20), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(ws->title(m_ids[i]), Qt::ElideRight, right - 38));
            p.setFont(Theme::uiFont(11.5));
            p.setPen(c.fog);
            QStringList path;
            for (const QString &a : ws->ancestors(m_ids[i]))
                path.append(ws->title(a));
            p.drawText(QRectF(38, r.top() + 20, right - 38, 16), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(tr("Deleted %1").arg(relativeTime(m ? m->trashed : 0))
                                                              + (path.isEmpty() ? QString() : QStringLiteral(" · ") + path.join(QStringLiteral(" / "))),
                                                          Qt::ElideRight, right - 38));
            if (i == m_hover) {
                for (int k = 0; k < 2; ++k) {
                    const QRectF br = k == 0 ? restoreRect(i) : removeRect(i);
                    if (br.contains(m_mouse))
                        Theme::paintHover(p, br, 1.4, 6);
                    Icons::paint(p, br.adjusted(5, 5, -5, -5), k == 0 ? QStringLiteral("restore") : QStringLiteral("trash"),
                                 br.contains(m_mouse) ? c.starlight : c.haze);
                }
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        m_mouse = e->position();
        const int h = int(e->position().y() / 40);
        m_hover = h < m_ids.size() ? h : -1;
        setCursor(m_hover >= 0 && (restoreRect(m_hover).contains(m_mouse) || removeRect(m_hover).contains(m_mouse)) ? Qt::PointingHandCursor
                                                                                                                  : Qt::ArrowCursor);
        if (m_hover >= 0)
            setToolTip(restoreRect(m_hover).contains(m_mouse) ? tr("Restore") : removeRect(m_hover).contains(m_mouse) ? tr("Delete forever") : QString());
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (m_hover < 0)
            return;
        if (restoreRect(m_hover).contains(e->position()))
            emit restore(m_ids[m_hover]);
        else if (removeRect(m_hover).contains(e->position()))
            emit remove(m_ids[m_hover]);
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

private:
    QStringList m_ids;
    int m_hover = -1;
    QPointF m_mouse;
};

TrashPopover::TrashPopover(QWidget *parent)
    : Popover(parent)
{
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);
    auto *top = new QHBoxLayout;
    auto *title = new QLabel(tr("Trash"));
    title->setFont(Theme::uiFont(13.5, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #f1f1f5; padding-left: 4px;"));
    top->addWidget(title);
    top->addStretch();
    auto *empty = new FlatButton(QStringLiteral("trash"), tr("Empty trash"));
    m_empty = empty;
    top->addWidget(empty);
    v->addLayout(top);
    m_search = new SearchField(tr("Filter by page title…"));
    m_search->setMinimumWidth(380);
    v->addWidget(m_search);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll = scroll;
    m_list = new TrashList(scroll);
    scroll->setWidget(m_list);
    m_list->setAutoFillBackground(false);
    v->addWidget(scroll);
    connect(m_search, &QLineEdit::textChanged, this, &TrashPopover::refresh);
    connect(m_list, &TrashList::restore, this, [this](const QString &id) {
        Workspace::instance()->restore(id);
        refresh();
    });
    connect(m_list, &TrashList::remove, this, [this](const QString &id) {
        if (confirmDialog(window(), tr("Delete “%1” forever?").arg(Workspace::instance()->title(id)),
                          tr("This page and everything inside it will be gone. This can't be undone."), tr("Delete"), true)) {
            Workspace::instance()->deleteForever(id);
        }
        refresh();
    });
    connect(empty, &FlatButton::clicked, this, [this] {
        if (Workspace::instance()->trashedRoots().isEmpty())
            return;
        if (confirmDialog(window(), tr("Empty the trash?"), tr("Every page in the trash will be deleted forever."), tr("Empty trash"), true))
            Workspace::instance()->emptyTrash();
        refresh();
    });
    refresh();
}

void TrashPopover::refresh()
{
    Workspace *ws = Workspace::instance();
    const QString q = m_search->text().trimmed();
    QStringList ids;
    for (const PageMeta *m : ws->trashedRoots())
        if (q.isEmpty() || ws->title(m->id).contains(q, Qt::CaseInsensitive))
            ids.append(m->id);
    m_list->setIds(ids);
    // As tall as the list, up to a scrolling limit; nothing to empty when empty.
    m_scroll->setFixedHeight(qMin(320, m_list->height()));
    m_empty->setVisible(!ws->trashedRoots().isEmpty());
    adjustSize();
}

#include "Overlays.moc"
