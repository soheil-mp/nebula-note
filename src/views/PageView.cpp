#include "PageView.h"

#include "Comments.h"
#include "PageHeader.h"
#include "Pickers.h"
#include "PropertyEditors.h"
#include "core/Markdown.h"
#include "core/Settings.h"
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "editor/TextObjects.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTimer>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>
#include <QVBoxLayout>

// ---- canvas & backlinks --------------------------------------------------------------

namespace
{
class Canvas : public QWidget
{
public:
    using QWidget::QWidget;
    std::function<void()> clickedBelow;
    int contentBottom = 0;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->position().y() > contentBottom && clickedBelow)
            clickedBelow();
    }
};
}

class Backlinks : public QWidget
{
public:
    explicit Backlinks(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }
    std::function<void(const QString &)> open;
    std::function<void()> resized;

    // "expanded" lists the pages; "popover" shows a count that opens a menu.
    void setMode(const QString &mode)
    {
        if (mode == m_mode)
            return;
        m_mode = mode;
        m_expanded = mode == QLatin1String("expanded");
        setLinks(m_ids);
    }
    QString mode() const { return m_mode; }

    void setLinks(const QStringList &ids)
    {
        m_ids = ids;
        setFixedHeight(m_ids.isEmpty() ? 0 : (m_expanded ? 40 + int(m_ids.size()) * 30 : 34));
        setVisible(!m_ids.isEmpty());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(c.hairline.lighter(130), 1));
        p.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));
        const QRectF header(0, 6, width(), 26);
        if (m_hover == -2)
            Theme::paintHover(p, QRectF(0, 6, 160, 26), 1.0, 6);
        Icons::paint(p, QRectF(6, header.center().y() - 7, 14, 14), m_expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"),
                     c.fog);
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        p.setPen(c.haze);
        p.drawText(header.adjusted(26, 0, 0, 0), Qt::AlignVCenter, tr("%n backlink(s)", nullptr, int(m_ids.size())));
        if (!m_expanded)
            return;
        Workspace *ws = Workspace::instance();
        for (int i = 0; i < m_ids.size(); ++i) {
            const QRectF r(0, 38 + i * 30, width(), 28);
            if (i == m_hover)
                Theme::paintHover(p, r, 1.0, 6);
            const PageMeta *m = ws->page(m_ids[i]);
            Icons::paintPageIcon(p, QRectF(r.left() + 6, r.center().y() - 8, 16, 16), m ? m->icon : QString(), c.mist, QStringLiteral("page"));
            p.setFont(Theme::uiFont(13.5));
            p.setPen(i == m_hover ? c.starlight : c.moon);
            p.drawText(r.adjusted(30, 0, 0, 0), Qt::AlignVCenter, ws->title(m_ids[i]));
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        if (QRectF(0, 6, 160, 26).contains(e->position()))
            h = -2;
        else if (m_expanded && e->position().y() >= 38)
            h = int((e->position().y() - 38) / 30);
        if (h >= m_ids.size())
            h = -1;
        if (h != m_hover) {
            m_hover = h;
            setCursor(h == -1 ? Qt::ArrowCursor : Qt::PointingHandCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (m_hover == -2 && m_mode == QLatin1String("popover")) {
            QMenu *menu = Theme::makeMenu(this);
            Workspace *ws = Workspace::instance();
            for (const QString &id : m_ids) {
                const PageMeta *m = ws->page(id);
                menu->addAction(Icons::icon(m && m->isDatabase() ? QStringLiteral("database") : QStringLiteral("page")), ws->title(id), this,
                                [this, id] {
                                    if (open)
                                        open(id);
                                });
            }
            menu->popup(mapToGlobal(QPoint(0, 32)));
            return;
        }
        if (m_hover == -2) {
            m_expanded = !m_expanded;
            setLinks(m_ids);
            if (resized)
                resized();
        } else if (m_hover >= 0 && open) {
            open(m_ids[m_hover]);
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

private:
    QStringList m_ids;
    QString m_mode = QStringLiteral("expanded");
    bool m_expanded = true;
    int m_hover = -1;
};

// The page's headings as short dashes in the right margin; hover shows
// their names, a click jumps there (Notion's table of contents rail).
class OutlineRail : public QWidget
{
public:
    explicit OutlineRail(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setAttribute(Qt::WA_TranslucentBackground);
    }
    std::function<void(int)> jump;
    void setEntries(const QList<TocEntry> &entries)
    {
        m_entries = entries;
        m_current = qMin(m_current, int(entries.size()) - 1);
        relayoutSelf();
        update();
    }
    const QList<TocEntry> &entries() const { return m_entries; }
    // The section being read: its line and row stand out.
    void setCurrent(int index)
    {
        if (index != m_current) {
            m_current = index;
            update();
        }
    }
    bool hasEntries() const { return m_entries.size() >= 2; }
    void relayoutSelf()
    {
        const int h = m_expanded ? int(m_entries.size()) * 28 + 20 : int(m_entries.size()) * 9 + 12;
        resize(m_expanded ? 250 : 36, h);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int minLevel = 4;
        for (const TocEntry &e : m_entries)
            minLevel = qMin(minLevel, e.level);
        if (!m_expanded) {
            for (int i = 0; i < m_entries.size(); ++i) {
                const double w = 18 - (m_entries[i].level - minLevel) * 4;
                p.setPen(QPen(i == m_current ? c.mist : c.dust, 2, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(width() - 8 - w, 8 + i * 9), QPointF(width() - 8, 8 + i * 9));
            }
            return;
        }
        Theme::paintSoftShadow(p, QRectF(rect()).adjusted(6, 4, -2, -4), 10, 8, 1.0);
        Theme::paintCard(p, QRectF(rect()).adjusted(6, 4, -2, -4), 10, c.slate);
        p.setFont(Theme::uiFont(12.5));
        const QFontMetricsF fm(p.font());
        for (int i = 0; i < m_entries.size(); ++i) {
            const QRectF r(14, 12 + i * 28, width() - 26, 26);
            if (i == m_hover)
                Theme::paintHover(p, r, 1.0, 6);
            const double indent = (m_entries[i].level - minLevel) * 14 + 8;
            p.setPen(i == m_hover || i == m_current ? c.starlight : c.haze);
            p.drawText(r.adjusted(indent, 0, -6, 0), Qt::AlignVCenter, fm.elidedText(m_entries[i].text, Qt::ElideRight, r.width() - indent - 8));
        }
    }
    void enterEvent(QEnterEvent *) override
    {
        m_expanded = true;
        const QPoint topRight = geometry().topRight();
        relayoutSelf();
        move(topRight.x() - width() + 1, y());
        raise();
        update();
    }
    void leaveEvent(QEvent *) override
    {
        m_expanded = false;
        m_hover = -1;
        const QPoint topRight = geometry().topRight();
        relayoutSelf();
        move(topRight.x() - width() + 1, y());
        update();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int h = m_expanded ? int((e->position().y() - 12) / 28) : -1;
        const int hover = h >= 0 && h < m_entries.size() ? h : -1;
        if (hover != m_hover) {
            m_hover = hover;
            setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (m_hover >= 0 && jump)
            jump(m_entries[m_hover].block);
    }

private:
    QList<TocEntry> m_entries;
    bool m_expanded = false;
    int m_hover = -1;
    int m_current = -1;
};

// ---- PageView -------------------------------------------------------------------------

PageView::PageView(QWidget *parent)
    : QScrollArea(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(false);
    auto *canvas = new Canvas;
    m_canvas = canvas;
    setWidget(m_canvas);
    m_canvas->setAutoFillBackground(false); // setWidget turns it on
    viewport()->setAutoFillBackground(false);

    m_header = new PageHeader(m_canvas);
    m_props = new PropertyPanel(m_canvas);
    m_discussion = new Discussion(m_canvas);
    m_discussion->hide();
    m_editor = new BlockEditor(m_canvas);
    m_outline = new OutlineRail(this);
    m_outline->setObjectName(QStringLiteral("outlineRail"));
    m_outline->hide();
    m_outline->jump = [this](int block) { m_editor->scrollToBlock(block); };
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        // The last heading above the upper third of the view is where we are.
        if (!m_outline->isVisible())
            return;
        const double line = verticalScrollBar()->value() + viewport()->height() / 3.0 - m_editor->y();
        int current = -1;
        const QList<TocEntry> &entries = m_outline->entries();
        for (int i = 0; i < entries.size(); ++i) {
            const double top = m_editor->blockTop(entries[i].block);
            if (top >= 0 && top <= line)
                current = i;
        }
        m_outline->setCurrent(current);
    });
    m_editor->setScrollArea(this);
    m_backlinks = new Backlinks(m_canvas);
    m_backlinks->setObjectName(QStringLiteral("backlinks"));
    m_backlinks->open = [this](const QString &id) { emit openPage(id); };
    m_backlinks->resized = [this] { relayout(); };

    // Starter for empty pages.
    m_starter = new QWidget(m_canvas);
    m_starter->setObjectName(QStringLiteral("starter"));
    auto *sv = new QVBoxLayout(m_starter);
    sv->setContentsMargins(0, 4, 0, 4);
    sv->setSpacing(1);
    auto *hint = new QLabel(tr("Press Enter to start writing, or pick a starting point"));
    hint->setFont(Theme::uiFont(13));
    hint->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 0 0 6px 2px;"));
    sv->addWidget(hint);
    auto addStarter = [&](const QString &icon, const QString &text, std::function<void()> fn) {
        auto *b = new FlatButton(icon, text);
        b->setTextSize(13.5);
        b->setMinimumHeight(30);
        connect(b, &FlatButton::clicked, this, fn);
        sv->addWidget(b, 0, Qt::AlignLeft);
    };
    addStarter(QStringLiteral("page-empty"), tr("Empty page"), [this] { startWriting(); });
    addStarter(QStringLiteral("template"), tr("Templates"), [this] { emit templatesRequested(m_pageId); });
    addStarter(QStringLiteral("download"), tr("Import"), [this] { emit importRequested(m_pageId); });
    auto *dbLabel = new QLabel(tr("Turn into a database"));
    dbLabel->setFont(Theme::uiFont(12, QFont::Medium));
    dbLabel->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 12px 0 4px 2px;"));
    sv->addWidget(dbLabel);
    auto *dbRow = new QHBoxLayout;
    dbRow->setSpacing(4);
    for (const auto &[vt, icon] : {std::pair{ViewType::Table, QStringLiteral("table")}, std::pair{ViewType::Board, QStringLiteral("board")},
                                   std::pair{ViewType::List, QStringLiteral("list")}, std::pair{ViewType::Gallery, QStringLiteral("gallery")},
                                   std::pair{ViewType::Calendar, QStringLiteral("calendar")}, std::pair{ViewType::Timeline, viewTypeIcon(ViewType::Timeline)}}) {
        auto *b = new FlatButton(icon, viewTypeName(vt));
        b->setFilled(true);
        b->setTextSize(13);
        connect(b, &FlatButton::clicked, this, [this, vt = vt] {
            Workspace *ws = Workspace::instance();
            m_editor->flush();
            Schema s = Schema::basic();
            Property status;
            status.id = QStringLiteral("status");
            status.name = tr("Status");
            status.type = PropType::Status;
            status.options = {{QStringLiteral("todo"), tr("Not started"), 2, 0},
                              {QStringLiteral("doing"), tr("In progress"), 5, 1},
                              {QStringLiteral("done"), tr("Done"), 8, 2}};
            Property date;
            date.id = QStringLiteral("date");
            date.name = tr("Date");
            date.type = PropType::Date;
            s.props = {s.props[0], status, date, s.props[1]};
            s.views.clear();
            s.views.append(Schema::makeView(s, vt));
            if (vt != ViewType::Table)
                s.views.append(Schema::makeView(s, ViewType::Table));
            const QString id = m_pageId;
            ws->convertToDatabase(id, s);
            emit convertedToDatabase(id);
        });
        dbRow->addWidget(b);
    }
    dbRow->addStretch();
    sv->addLayout(dbRow);

    // Find bar (stays at the top right while the page scrolls).
    m_findBar = new QWidget(this);
    m_findBar->hide();
    auto *fl = new QHBoxLayout(m_findBar);
    fl->setContentsMargins(10, 8, 8, 8);
    fl->setSpacing(4);
    m_findField = new SearchField(tr("Find in page"));
    m_findField->setFixedWidth(220);
    m_findCount = new QLabel;
    m_findCount->setFont(Theme::uiFont(12));
    m_findCount->setStyleSheet(QStringLiteral("color: #8a8a94; padding: 0 6px;"));
    // Room for the longest count, so the bar doesn't change size as you type.
    m_findCount->setFixedWidth(int(QFontMetricsF(m_findCount->font()).horizontalAdvance(tr("No matches"))) + 14);
    m_findCount->setAlignment(Qt::AlignCenter);
    auto *prev = new FlatButton(QStringLiteral("chevron-up"));
    auto *next = new FlatButton(QStringLiteral("chevron-down"));
    auto *close = new FlatButton(QStringLiteral("close"));
    fl->addWidget(m_findField);
    fl->addWidget(m_findCount);
    fl->addWidget(prev);
    fl->addWidget(next);
    fl->addWidget(close);
    m_findBar->setAttribute(Qt::WA_StyledBackground);
    m_findBar->setStyleSheet(QStringLiteral("QWidget#findbar { background: #1a1a1e; border: 1px solid #2e2e35; border-radius: 10px; }"));
    m_findBar->setObjectName(QStringLiteral("findbar"));
    connect(m_findField, &QLineEdit::textChanged, this, [this] { runFind(0); });
    connect(m_findField, &QLineEdit::returnPressed, this, [this] { runFind(1); });
    connect(prev, &FlatButton::clicked, this, [this] { runFind(-1); });
    connect(next, &FlatButton::clicked, this, [this] { runFind(1); });
    connect(close, &FlatButton::clicked, this, &PageView::closeFind);
    m_findField->installEventFilter(this);

    canvas->clickedBelow = [this] {
        m_starterDismissed = true;
        m_editor->focusEnd(true);
        updateStarter();
    };
    connect(m_header, &PageHeader::heightChanged, this, &PageView::relayout);
    connect(m_header, &PageHeader::titleSubmitted, this, [this] {
        // Enter in the title starts (or continues) the body at the top.
        if (!m_editor->isEmptyPage()) {
            QTextCursor c(m_editor->document());
            c.beginEditBlock();
            c.insertBlock();
            c.movePosition(QTextCursor::PreviousBlock);
            c.endEditBlock();
        }
        startWriting();
    });
    connect(m_header, &PageHeader::titleExitDown, this, [this] { startWriting(); });
    connect(m_props, &PropertyPanel::heightChanged, this, &PageView::relayout);
    connect(m_discussion, &Discussion::heightChanged, this, &PageView::relayout);
    connect(m_header, &PageHeader::commentRequested, this, [this] {
        m_discussionOpen = true;
        m_discussion->show();
        relayout();
        if (QPlainTextEdit *input = m_discussion->findChild<QPlainTextEdit *>())
            input->setFocus();
    });
    // Outline in the margin follows the headings.
    auto *outlineTimer = new QTimer(this);
    outlineTimer->setSingleShot(true);
    outlineTimer->setInterval(300);
    connect(outlineTimer, &QTimer::timeout, this, [this] {
        m_outline->setEntries(TextObjects::headings(m_editor->document()));
        relayout();
    });
    connect(m_editor, &BlockEditor::edited, outlineTimer, qOverload<>(&QTimer::start));
    connect(m_editor, &BlockEditor::emojiRequested, this, [this](const QPoint &global) {
        auto *picker = new IconPicker(window(), false);
        picker->showEmoji();
        connect(picker, &IconPicker::picked, this, [this](const QString &icon) {
            if (!icon.startsWith(QLatin1String("glyph:")) && !icon.isEmpty())
                m_editor->insertText(icon);
        });
        picker->popupAt(global);
    });
    connect(m_editor, &BlockEditor::linkedDatabaseRequested, this, [this](const QPoint &global) {
        auto *picker = new PagePicker(window(), tr("Show a database here"), QString(), false, true);
        picker->setDatabasesOnly(true);
        connect(picker, &PagePicker::picked, this, [this](const QString &db) {
            if (!db.isEmpty())
                m_editor->insertLinkedDatabase(db);
        });
        picker->popupAt(global);
    });
    connect(m_editor, &BlockEditor::commentRequested, this, [this](const QString &blockId, const QString &, const QString &quote, const QPoint &global) {
        auto *composer = new CommentComposer(window(), quote);
        const auto range = m_editor->commentRange();
        connect(composer, &CommentComposer::posted, this, [this, blockId, quote, range](const QString &body) {
            Workspace *ws = Workspace::instance();
            m_editor->flush();
            const QString thread = ws->addComment(m_pageId, blockId, quote, body);
            if (range.first >= 0)
                m_editor->applyCommentThread(range.first, range.second, thread);
            m_editor->flush();
        });
        composer->popupAt(global);
    });
    connect(m_editor, &BlockEditor::commentClicked, this, [this](const QString &blockId, const QString &thread, const QPoint &global) {
        CommentThreads::Filter f;
        f.thread = thread;
        f.blockId = thread.isEmpty() ? blockId : QString();
        auto *threads = new CommentThreads(window(), m_pageId, f);
        connect(threads, &CommentThreads::deleted, this, [this](const QString &t) { m_editor->removeCommentThread(t); });
        threads->popupAt(global);
    });
    connect(m_editor, &BlockEditor::openBlock, this, [this](const QString &page, const QString &block) {
        if (page == m_pageId)
            m_editor->scrollToBlockId(block);
        else
            emit openBlock(page, block);
    });
    connect(m_editor, &BlockEditor::heightChanged, this, &PageView::relayout);
    connect(m_editor, &BlockEditor::edited, this, &PageView::updateStarter);
    connect(m_editor, &BlockEditor::openPage, this, &PageView::openPage);
    connect(m_editor, &BlockEditor::toast, this, &PageView::toast);
    connect(m_editor, &BlockEditor::exitTop, this, [this] { m_header->title()->focusEnd(); });
    connect(m_editor, &BlockEditor::iconRequested, this, [this](int block, const QPoint &global) {
        auto *picker = new IconPicker(window(), false);
        connect(picker, &IconPicker::picked, this, [this, block](const QString &icon) { m_editor->setBlockIcon(block, icon); });
        picker->popupAt(global);
    });
    connect(m_editor, &BlockEditor::moveBlocksRequested, this, [this](const QPoint &global) {
        auto *picker = new PagePicker(window(), tr("Move blocks to…"), QString(), false);
        connect(picker, &PagePicker::picked, this, [this](const QString &target) {
            Workspace *ws = Workspace::instance();
            if (target.isEmpty() || target == m_pageId || !ws->exists(target))
                return;
            const QList<Block> taken = m_editor->takeSelectedBlocks();
            m_editor->flush();
            QList<Block> content = ws->content(target);
            content.append(taken);
            ws->setContent(target, content);
            emit toast(tr("Moved to “%1”").arg(ws->title(target)));
        });
        picker->popupAt(global);
    });
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::contentChanged, this, [this](const QString &id, QObject *source) {
            if (id == m_pageId && source != m_editor) {
                m_editor->reload();
                updateStarter();
            }
            // Someone linked to (or unlinked from) this page.
            if (source == nullptr || id != m_pageId)
                updateBacklinks();
        });
        connect(ws, &Workspace::pageChanged, this, [this](const QString &id) {
            if (id == m_pageId)
                applyOptions();
        });
    }
}

void PageView::setPage(const QString &pageId)
{
    flush();
    closeFind();
    m_pageId = pageId;
    m_starterDismissed = false;
    m_header->setPage(pageId);
    m_props->setRow(pageId);
    m_editor->setPage(pageId);
    m_discussionOpen = false;
    m_discussion->setPage(pageId);
    m_outline->setEntries(TextObjects::headings(m_editor->document()));
    applyOptions();
    updateStarter();
    updateBacklinks();
    verticalScrollBar()->setValue(0);
    relayout();
}

void PageView::applyOptions()
{
    Workspace *ws = Workspace::instance();
    if (!ws || !ws->exists(m_pageId))
        return;
    const PageOptions o = ws->options(m_pageId);
    m_header->setSmall(o.smallText);
    // Serif and mono pages title themselves in that face too.
    m_header->setTitleFamily(o.font == QLatin1String("serif") || o.font == QLatin1String("mono") ? Theme::pageFamily(o.font) : QString());
    m_header->setEditable(!o.locked);
    const BlockFormat::Style &s = m_editor->style();
    const bool changed = s.family != Theme::pageFamily(o.font) || (o.smallText != (s.size < 15.5 * Settings::instance().textScale / 100.0))
                         || m_editor->isReadOnly() != o.locked;
    if (changed)
        m_editor->setPageOptions(o);

    updateBacklinks(); // the backlinks option may have changed
    relayout();
}

void PageView::revealBlock(const QString &blockId)
{
    // After the page has laid out.
    QTimer::singleShot(30, this, [this, blockId] { m_editor->scrollToBlockId(blockId); });
}

void PageView::flush()
{
    m_header->flushTitle();
    m_editor->flush();
}

void PageView::focusDefault()
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws ? ws->page(m_pageId) : nullptr;
    if (m && m->title.isEmpty() && m_editor->isEmptyPage())
        m_header->title()->focusEnd();
    else
        m_editor->setFocus();
}

void PageView::startWriting()
{
    // The starting points step aside once you start writing.
    m_starterDismissed = true;
    m_editor->focusStart();
    updateStarter();
}

void PageView::updateStarter()
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws ? ws->page(m_pageId) : nullptr;
    const bool show = m && !m->isRow() && !m_starterDismissed && m_editor->isEmptyPage() && !ws->hasChildren(m_pageId)
                      && !ws->options(m_pageId).locked;
    if (show == m_starter->isHidden()) {
        m_starter->setVisible(show);
        relayout();
    }
}

void PageView::updateBacklinks()
{
    Workspace *ws = Workspace::instance();
    if (!ws)
        return;
    const QString mode = ws->options(m_pageId).backlinks;
    m_backlinks->setMode(mode);
    m_backlinks->setLinks(Settings::instance().showBacklinks && mode != QLatin1String("off") ? ws->backlinks(m_pageId) : QStringList());
    relayout();
}

void PageView::relayout()
{
    if (m_relayouting)
        return;
    const int W = viewport()->width();
    if (W <= 0)
        return; // not laid out yet
    m_relayouting = true;
    Workspace *ws = Workspace::instance();
    const PageOptions o = ws && ws->exists(m_pageId) ? ws->options(m_pageId) : PageOptions();
    const int gutter = BlockEditor::gutter();
    const int margin = qMax(gutter + 18, W / 14);
    const int maxW = qRound(720 * Settings::instance().textScale / 100.0);
    const int colW = o.fullWidth ? qMax(320, W - 2 * margin) : qMin(maxW, qMax(280, W - 2 * margin));
    const int x = (W - colW) / 2;
    m_header->setGeometry(0, 0, W, m_header->height());
    m_header->setColumn(x, colW);
    int y = m_header->height();
    if (!m_props->isHidden()) {
        m_props->setGeometry(x, y + 4, colW, m_props->height());
        y += m_props->height() + 6;
    }
    // Page discussion: shown when it has comments (or one is being written).
    {
        bool hasPageComments = false;
        if (ws && o.showDiscussion)
            for (const Workspace::Comment &c : ws->comments(m_pageId))
                if (c.blockId.isEmpty() && !c.resolved)
                    hasPageComments = true;
        const bool show = o.showDiscussion && (hasPageComments || m_discussionOpen);
        m_discussion->setVisible(show);
        if (show) {
            m_discussion->setGeometry(x, y + 6, colW, m_discussion->height());
            y += m_discussion->height() + 10;
        }
    }
    if (!m_starter->isHidden()) {
        const int h = m_starter->sizeHint().height();
        m_starter->setGeometry(x - 6, y + 6, colW, h);
        y += h + 14;
    }
    m_editor->setGeometry(x - gutter, y, colW + gutter + BlockEditor::rightMargin(), m_editor->height());
    y += m_editor->height();
    if (!m_backlinks->isHidden()) {
        m_backlinks->setGeometry(x, y + 28, colW, m_backlinks->height());
        y += m_backlinks->height() + 28;
    }
    static_cast<Canvas *>(m_canvas)->contentBottom = y;
    const int bottom = qMax(160, int(viewport()->height() * 0.38));
    m_canvas->setFixedSize(W, y + bottom);
    m_findBar->adjustSize();
    m_findBar->move(width() - m_findBar->width() - 20, 12);
    // The outline rail sits in the right margin when there is room.
    const bool roomForOutline = W - (x + colW) > 120;
    m_outline->setVisible(o.showToc && roomForOutline && m_outline->hasEntries());
    if (m_outline->isVisible()) {
        m_outline->relayoutSelf();
        m_outline->move(W - m_outline->width() - 18, qMax(90, viewport()->height() / 3 - m_outline->height() / 2));
        m_outline->raise();
    }
    m_relayouting = false;
}

void PageView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    relayout();
}

void PageView::keyPressEvent(QKeyEvent *e)
{
    if (e->matches(QKeySequence::Find)) {
        showFind();
        return;
    }
    QScrollArea::keyPressEvent(e);
}

// ---- find ------------------------------------------------------------------------------

void PageView::showFind()
{
    m_findBar->show();
    m_findBar->raise();
    relayout();
    const QString sel = m_editor->textCursor().selectedText();
    if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
        m_findField->setText(sel);
    m_findField->setFocus();
    m_findField->selectAll();
    runFind(0);
}

void PageView::closeFind()
{
    if (!m_findBar->isVisible())
        return;
    m_findBar->hide();
    m_matches.clear();
    m_editor->setExtraSelections({});
    m_editor->setFocus();
}

void PageView::runFind(int step)
{
    const QString needle = m_findField->text();
    QTextDocument *doc = m_editor->document();
    if (step == 0) {
        m_matches.clear();
        m_matchIndex = -1;
        if (!needle.isEmpty()) {
            QTextCursor c(doc);
            while (true) {
                c = doc->find(needle, c);
                if (c.isNull())
                    break;
                if (c.block().isVisible())
                    m_matches.append(c);
            }
        }
        m_matchIndex = m_matches.isEmpty() ? -1 : 0;
    } else if (!m_matches.isEmpty()) {
        m_matchIndex = (m_matchIndex + step + int(m_matches.size())) % int(m_matches.size());
    }
    QList<QTextEdit::ExtraSelection> sels;
    for (int i = 0; i < m_matches.size(); ++i) {
        QTextEdit::ExtraSelection s;
        s.cursor = m_matches[i];
        s.format.setBackground(i == m_matchIndex ? QColor(0xC9, 0xC9, 0xD0) : QColor(0x4A, 0x4A, 0x55));
        if (i == m_matchIndex)
            s.format.setForeground(QColor(0x13, 0x13, 0x16));
        sels.append(s);
    }
    m_editor->setExtraSelections(sels);
    m_findCount->setText(needle.isEmpty() ? QString() : m_matches.isEmpty() ? tr("No matches")
                                                                            : tr("%1 of %2").arg(m_matchIndex + 1).arg(m_matches.size()));
    if (m_matchIndex >= 0) {
        const QRect r = m_editor->cursorRect(m_matches[m_matchIndex]);
        const QPoint p = m_editor->viewport()->mapTo(m_canvas, r.topLeft());
        ensureVisible(p.x(), p.y() + r.height() / 2, 20, viewport()->height() / 3);
    }
}

bool PageView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_findField && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Escape) {
            closeFind();
            return true;
        }
        if (ke->key() == Qt::Key_Return && (ke->modifiers() & Qt::ShiftModifier)) {
            runFind(-1);
            return true;
        }
    }
    return QScrollArea::eventFilter(watched, event);
}
