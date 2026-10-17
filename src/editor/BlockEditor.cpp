#include "BlockEditor.h"
#include "BlockEditorPrivate.h"
#include <QTextTable>

#include "CodeHighlighter.h"
#include "DocumentIO.h"
#include "Popups.h"
#include "core/Ids.h"
#include "core/Markdown.h"
#include "core/Settings.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>
#include <QUrl>
#include <QtMath>

#include <memory>

using namespace BlockFormat;
using namespace BlockEditorPrivate;


namespace BlockEditorPrivate
{
QString romanNumeral(int n)
{
    static const QList<QPair<int, const char *>> table = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
                                                          {90, "xc"},  {50, "l"},   {40, "xl"},  {10, "x"},   {9, "ix"},
                                                          {5, "v"},    {4, "iv"},   {1, "i"}};
    QString out;
    for (const auto &[v, s] : table)
        while (n >= v) {
            out += QLatin1String(s);
            n -= v;
        }
    return out;
}

QString letterNumeral(int n)
{
    QString out;
    while (n > 0) {
        --n;
        out.prepend(QChar(u'a' + n % 26));
        n /= 26;
    }
    return out;
}
}

BlockEditor::BlockEditor(QWidget *parent)
    : QTextEdit(parent)
{
    m_ws = Workspace::instance();
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptRichText(true);
    setTabChangesFocus(false);
    setUndoRedoEnabled(true);
    setCursorWidth(2);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Window, Qt::transparent);
    pal.setColor(QPalette::Text, Theme::colors().moon);
    pal.setColor(QPalette::Highlight, QColor(0x46, 0x46, 0x52));
    pal.setColor(QPalette::HighlightedText, Theme::colors().starlight);
    setPalette(pal);

    document()->setDocumentMargin(0);
    m_ctx.style.family = Theme::uiFamily();
    m_objects = new TextObjects(&m_ctx, this);
    m_objects->install(document());
    m_highlighter = new CodeHighlighter(document());

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &BlockEditor::flush);
    m_normalizeTimer.setSingleShot(true);
    m_normalizeTimer.setInterval(0);
    connect(&m_normalizeTimer, &QTimer::timeout, this, &BlockEditor::normalize);
    m_animTimer.setInterval(16);
    connect(&m_animTimer, &QTimer::timeout, this, [this] {
        m_sparks.removeIf([](const Spark &s) { return s.clock.elapsed() > 700; });
        m_toggleAnims.removeIf([](const ToggleAnim &a) { return a.clock.elapsed() > 200; });
        m_flashes.removeIf([](const Flash &f) { return f.clock.elapsed() > 1600; });
        if (m_sparks.isEmpty() && m_toggleAnims.isEmpty() && m_flashes.isEmpty())
            m_animTimer.stop();
        viewport()->update();
    });
    m_autoScroll.setInterval(30);
    connect(&m_autoScroll, &QTimer::timeout, this, [this] {
        if (!m_scrollArea)
            return;
        const QPoint global = QCursor::pos();
        const QPoint inArea = m_scrollArea->viewport()->mapFromGlobal(global);
        const int h = m_scrollArea->viewport()->height();
        int dy = 0;
        if (inArea.y() < 40)
            dy = -qMin(40, (40 - inArea.y()) / 2 + 2);
        else if (inArea.y() > h - 40)
            dy = qMin(40, (inArea.y() - h + 40) / 2 + 2);
        if (dy == 0)
            return;
        QScrollBar *bar = m_scrollArea->verticalScrollBar();
        bar->setValue(bar->value() + dy);
        // Keep extending the selection / drop target under the pointer.
        QMouseEvent move(QEvent::MouseMove, viewport()->mapFromGlobal(global), global, Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        mouseMoveEvent(&move);
    });

    m_handleFade.setDuration(Theme::animMs(140));
    m_handleFade.setStartValue(0.0);
    m_handleFade.setEndValue(1.0);
    connect(&m_handleFade, &QVariantAnimation::valueChanged, viewport(), qOverload<>(&QWidget::update));

    connect(document(), &QTextDocument::contentsChange, this, [this](int, int removed, int added) {
        if (m_loading || (removed == 0 && added == 0))
            return;
        scheduleNormalize();
        scheduleSave();
    });
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, &BlockEditor::updateHeight);
    m_embedTimer.setSingleShot(true);
    m_embedTimer.setInterval(0);
    connect(&m_embedTimer, &QTimer::timeout, this, &BlockEditor::updateEmbeds);
    connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this, &BlockEditor::scheduleEmbeds);
    connect(document(), &QTextDocument::contentsChanged, this, &BlockEditor::scheduleEmbeds);
    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
        fixCursorVisibility();
        if (m_slash && m_slash->isVisible())
            updateSlashMenu();
        if (m_mention && (m_mention->isVisible() || m_mentionPos >= 0))
            updateMentionMenu();
        m_lastCursorPos = textCursor().position();
        ensureCaretVisible();
        viewport()->update();
    });
    connect(this, &QTextEdit::selectionChanged, this, &BlockEditor::updateFormatBar);
    if (m_ws) {
        connect(m_ws, &Workspace::pageChanged, this, [this](const QString &id) {
            // Mentions and page links draw live titles: re-measure them.
            if (!m_linkedPages.contains(id))
                return;
            document()->markContentsDirty(0, document()->characterCount());
            viewport()->update();
        });
    }
    if (m_ws)
        connect(m_ws, &Workspace::commentsChanged, this, [this](const QString &page) {
            if (page == m_pageId)
                refreshComments();
        });
    connect(&Settings::instance(), &Settings::changed, this, [this] {
        if (!m_pageId.isEmpty())
            setPageOptions(m_options);
    });
}

BlockEditor::~BlockEditor()
{
    flush();
}

void BlockEditor::setOverlayHost(QWidget *host)
{
    m_host = host;
}

// ---- page ------------------------------------------------------------------------

void BlockEditor::setPage(const QString &pageId)
{
    flush();
    m_pageId = pageId;
    closeSlashMenu();
    closeMentionMenu();
    if (m_formatBar)
        m_formatBar->dismiss();
    m_ctx.pageId = pageId;
    m_flashes.clear();
    clearBlockSelection();
    m_options = m_ws ? m_ws->options(pageId) : PageOptions();
    setPageOptions(m_options);
}

void BlockEditor::setPageOptions(const PageOptions &options)
{
    flush();
    m_options = options;
    Style s;
    s.family = Theme::pageFamily(options.font);
    s.mono = options.font == QLatin1String("mono");
    s.size = (options.smallText ? 14.0 : 16.0) * Settings::instance().textScale / 100.0;
    m_ctx.style = s;
    setReadOnly(options.locked);
    m_highlighter->setSpellCheck(Settings::instance().spellCheck && !options.locked);
    reload();
}

void BlockEditor::reload()
{
    m_loading = true;
    const int pos = textCursor().position();
    // Reloading (a new font, small text) keeps the reader where they were.
    const int scrolled = m_scrollArea ? m_scrollArea->verticalScrollBar()->value() : -1;
    QTextDocument *doc = document();
    doc->setUndoRedoEnabled(false);
    QList<Block> content = m_ws && !m_pageId.isEmpty() ? m_ws->content(m_pageId) : QList<Block>();
    ensureBlockIds(content);
    DocumentIO::load(doc, content, m_ctx.style);
    QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    ff.setLeftMargin(gutter());
    ff.setRightMargin(rightMargin());
    ff.setTopMargin(2);
    ff.setBottomMargin(10);
    ff.setPadding(0);
    ff.setBorder(0);
    doc->rootFrame()->setFrameFormat(ff);
    m_loading = false;
    scheduleEmbeds();
    normalize(); // margins and ids, outside the undo history
    doc->setUndoRedoEnabled(true);
    m_dirty = false;
    {
        const QStringList linked = blocksLinkedPages(DocumentIO::save(doc));
        m_linkedPages = QSet<QString>(linked.begin(), linked.end());
    }
    updateVisibility();
    m_highlighter->rehighlight();
    QTextCursor c(doc);
    c.setPosition(qBound(0, pos, doc->characterCount() - 1));
    m_loading = true;
    setTextCursor(c);
    m_loading = false;
    updateHeight();
    refreshComments();
    if (scrolled >= 0)
        m_scrollArea->verticalScrollBar()->setValue(scrolled);
}

void BlockEditor::flush()
{
    m_saveTimer.stop();
    if (!m_dirty || m_pageId.isEmpty() || !m_ws || !m_ws->exists(m_pageId))
        return;
    m_dirty = false;
    const QList<Block> blocks = DocumentIO::save(document());
    const QStringList linked = blocksLinkedPages(blocks);
    m_linkedPages = QSet<QString>(linked.begin(), linked.end());
    // Page history: the state before this edit becomes a version once ten
    // minutes have passed since the last one.
    m_ws->snapshot(m_pageId, false, 10);
    m_ws->setContent(m_pageId, blocks, this);
}

void BlockEditor::scheduleSave()
{
    m_dirty = true;
    m_saveTimer.start();
    emit edited();
}

QList<Block> BlockEditor::blocks() const
{
    return DocumentIO::save(document());
}

void BlockEditor::setBlocks(const QList<Block> &blocks)
{
    QList<Block> body = blocks;
    if (body.isEmpty())
        body.append(Block());
    ensureBlockIds(body);
    EditSteps steps(document());
    steps.run([&](QTextCursor &c) {
        c.select(QTextCursor::Document);
        c.removeSelectedText();
        applyBlockProps(c.block(), Block{}, m_ctx.style);
    });
    steps.run([&](QTextCursor &c) { DocumentIO::fillEmpty(c, document()->begin(), body, m_ctx.style); });
    updateVisibility();
    m_highlighter->rehighlight();
}

void BlockEditor::appendBlocks(const QList<Block> &blocks)
{
    if (blocks.isEmpty())
        return;
    if (isEmptyPage()) {
        setBlocks(blocks);
        return;
    }
    QList<Block> body = blocks;
    renewBlockIds(body);
    EditSteps(document()).run([&](QTextCursor &c) {
        QTextBlock last = document()->lastBlock();
        if (last.length() <= 1 && typeOf(last) == BlockType::Paragraph && DocumentIO::tableOf(last.previous()))
            DocumentIO::fillEmpty(c, last, body, m_ctx.style); // the empty block after a table
        else
            DocumentIO::insertAfter(c, last, body, m_ctx.style);
    });
    updateVisibility();
}

bool BlockEditor::isEmptyPage() const
{
    const QTextDocument *doc = document();
    return doc->blockCount() == 1 && doc->begin().text().isEmpty() && typeOf(doc->begin()) == BlockType::Paragraph;
}

int BlockEditor::preferredHeight() const
{
    return qCeil(document()->documentLayout()->documentSize().height()) + 2;
}

void BlockEditor::updateHeight()
{
    const int h = preferredHeight();
    if (h == m_lastHeight)
        return;
    m_lastHeight = h;
    setFixedHeight(h);
    emit heightChanged();
}

void BlockEditor::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    updateHeight();
    scheduleEmbeds();
    // Space kept under Mermaid diagrams depends on the width.
    if (e->size().width() != e->oldSize().width())
        scheduleNormalize();
}

void BlockEditor::wheelEvent(QWheelEvent *e)
{
    // The page scrolls, not the editor.
    e->ignore();
}

void BlockEditor::ensureCaretVisible()
{
    if (!m_scrollArea || !hasFocus() || !m_scrollArea->widget())
        return;
    // While a page loads the caret runs through every block it inserts.
    if (m_loading)
        return;
    const QRect r = cursorRect();
    const QPoint top = viewport()->mapTo(m_scrollArea->widget(), r.topLeft());
    m_scrollArea->ensureVisible(top.x(), top.y() + r.height() / 2, 10, r.height() / 2 + 48);
}

QRect BlockEditor::caretRect() const
{
    return cursorRect().translated(viewport()->pos());
}

// ---- caret -----------------------------------------------------------------------

void BlockEditor::focusStart()
{
    QTextCursor c(document());
    c.movePosition(QTextCursor::Start);
    setTextCursor(c);
    setFocus();
}

void BlockEditor::focusEnd(bool ensureEmptyBlock)
{
    QTextBlock last = document()->lastBlock();
    if (ensureEmptyBlock && !isReadOnly() && !(last.text().isEmpty() && typeOf(last) == BlockType::Paragraph && last.isVisible())) {
        QTextCursor c(document());
        c.beginEditBlock();
        Block p;
        DocumentIO::insertAfter(c, last, {p}, m_ctx.style);
        c.endEditBlock();
    }
    QTextCursor c(document());
    c.movePosition(QTextCursor::End);
    setTextCursor(c);
    setFocus();
}

bool BlockEditor::cursorOnFirstLine() const
{
    const QTextCursor c = textCursor();
    QTextBlock firstVisible = document()->begin();
    while (firstVisible.isValid() && !firstVisible.isVisible())
        firstVisible = firstVisible.next();
    if (!firstVisible.isValid())
        return true;
    // The first line of any block sitting at the very top (columns included).
    if (blockRect(c.block()).top() > blockRect(firstVisible).top() + 1)
        return false;
    const QTextLayout *layout = c.block().layout();
    return !layout || layout->lineCount() == 0 || layout->lineForTextPosition(c.positionInBlock()).lineNumber() == 0;
}

void BlockEditor::fixCursorVisibility()
{
    QTextCursor c = textCursor();
    QTextBlock b = c.block();
    if (b.isVisible())
        return;
    const bool forward = c.position() >= m_lastCursorPos;
    QTextBlock target = b;
    if (forward) {
        while (target.isValid() && !target.isVisible())
            target = target.next();
    }
    if (!target.isValid() || !forward) {
        target = b;
        while (target.isValid() && !target.isVisible())
            target = target.previous();
    }
    if (!target.isValid())
        return;
    const int pos = forward && target.blockNumber() > b.blockNumber() ? target.position() : target.position() + target.length() - 1;
    c.setPosition(pos, c.hasSelection() ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
    setTextCursor(c);
}

void BlockEditor::splitBlock()
{
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (c.hasSelection())
        c.removeSelectedText();
    QTextBlock blk = c.block();
    const Block props = blockProps(blk);
    const BlockType t = props.type;

    if (blockTypeIsAtomic(t)) {
        c.endEditBlock();
        Block p;
        p.depth = props.depth;
        insertNewBlockAfter(blk, p);
        return;
    }
    const bool empty = blk.text().isEmpty();
    if (empty && (blockTypeIsList(t) || t == BlockType::Quote || t == BlockType::Callout || blockTypeIsHeading(t))) {
        c.endEditBlock();
        if (blockTypeIsList(t) && props.depth > 0) {
            indentBlocks(-1);
        } else {
            Block p = props;
            p.type = BlockType::Paragraph;
            p.toggleable = false;
            QTextCursor e(blk);
            e.beginEditBlock();
            setBlockProps(blk, p);
            e.endEditBlock();
        }
        return;
    }
    if (empty && t == BlockType::Paragraph && props.depth > 0) {
        c.endEditBlock();
        indentBlocks(-1);
        return;
    }

    Block next = props;
    next.spans.clear();
    next.checked = false;
    next.collapsed = false;
    next.toggleable = false;
    next.id = Block::newId();
    next.caption.clear();
    next.syncId = props.syncId; // typing inside a synced block stays inside it
    if (!blockTypeIsList(t))
        next.type = BlockType::Paragraph;
    // Enter at the end of a callout's text starts a block inside it.
    if (t == BlockType::Callout && c.atBlockEnd() && !empty) {
        next.type = BlockType::Paragraph;
        next.depth = props.depth + 1;
    }
    const bool folds = foldsChildren(blk);
    if (folds && !isCollapsed(blk) && (subtreeEnd(blk) > blk.blockNumber() || t != BlockType::Toggle)) {
        // An open toggle (or toggle heading): the new block becomes its first child.
        next.type = BlockType::Paragraph;
        next.depth = props.depth + 1;
    }
    if (folds && isCollapsed(blk) && c.atBlockEnd()) {
        c.endEditBlock();
        next.depth = props.depth;
        if (t != BlockType::Toggle)
            next.type = BlockType::Paragraph;
        insertNewBlockAfter(blk, next);
        return;
    }
    const bool atStart = c.atBlockStart() && !empty;
    c.insertBlock();
    QTextBlock below = c.block();
    QTextBlock above = below.previous();
    if (atStart) {
        // An empty block appears above; this block keeps its identity.
        Block emptyProps = blockTypeIsList(t) ? next : Block{};
        emptyProps.depth = props.depth;
        emptyProps.id = Block::newId();
        emptyProps.syncId = props.syncId;
        if (t == BlockType::Toggle)
            emptyProps.type = BlockType::Toggle;
        setBlockProps(above, emptyProps);
        // The text below keeps the block's identity.
        Block keep = props;
        setBlockProps(below, keep);
        setCollapsed(below, props.collapsed);
        setCollapsed(above, false);
    } else {
        setBlockProps(below, next);
        setCollapsed(below, false);
    }
    c.endEditBlock();
    setTextCursor(c);
    updateVisibility();
}

bool BlockEditor::handleBackspaceAtStart()
{
    QTextCursor c = textCursor();
    if (c.hasSelection() || !c.atBlockStart())
        return false;
    QTextBlock blk = c.block();
    Block props = blockProps(blk);
    if (blockTypeIsAtomic(props.type))
        return false;
    if (props.type != BlockType::Paragraph) {
        props.type = BlockType::Paragraph;
        props.checked = false;
        props.toggleable = false;
        c.beginEditBlock();
        setBlockProps(blk, props);
        c.endEditBlock();
        return true;
    }
    if (props.depth > 0) {
        indentBlocks(-1);
        return true;
    }
    const DocumentIO::Container here = DocumentIO::containerOf(blk);
    const DocumentIO::Item prevItemHere = [&] {
        const QList<DocumentIO::Item> items = DocumentIO::itemsOf(document(), here);
        for (int i = 0; i < items.size(); ++i)
            if (!items[i].isTable() && items[i].block == blk)
                return i > 0 ? items[i - 1] : DocumentIO::Item{};
        return DocumentIO::Item{};
    }();
    if (prevItemHere.isTable()) {
        // Right after columns or a table: step into its last block (an empty
        // block here goes away).
        QTextTable *t = prevItemHere.table;
        if (blk.text().isEmpty() && nextInContainer(blk).isValid()) {
            const int n = blk.blockNumber();
            EditSteps(document()).run([&](QTextCursor &ec) { DocumentIO::removeRange(ec, n, n, m_ctx.style); });
        }
        QTextCursor in(document());
        in.setPosition(t->lastPosition());
        setTextCursor(in);
        return true;
    }
    if (!here.isRoot() && prevItemHere.first < 0)
        return true; // the first block of a column or cell: nothing to merge with
    QTextBlock prev = prevItemHere.first >= 0 ? prevItemHere.block : QTextBlock();
    if (!prev.isValid() || DocumentIO::isPhantom(prev)) {
        if (blk.text().isEmpty() && nextInContainer(blk).isValid() && here.isRoot()) {
            const int n = blk.blockNumber();
            EditSteps(document()).run([&](QTextCursor &ec) { DocumentIO::removeRange(ec, n, n, m_ctx.style); });
        }
        emit exitTop();
        return true;
    }
    if (!prev.isVisible()) {
        // Don't merge into a collapsed toggle's hidden children.
        while (prev.isValid() && !prev.isVisible())
            prev = prev.previous();
        if (prev.isValid()) {
            QTextCursor m(document());
            m.setPosition(prev.position() + prev.length() - 1);
            setTextCursor(m);
        }
        return true;
    }
    if (blockTypeIsAtomic(typeOf(prev))) {
        c.beginEditBlock();
        c.setPosition(prev.position());
        c.setPosition(blk.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.endEditBlock();
        setTextCursor(c);
        return true;
    }
    return false;
}

bool BlockEditor::handleDeleteAtEnd()
{
    QTextCursor c = textCursor();
    if (c.hasSelection() || !c.atBlockEnd())
        return false;
    QTextBlock blk = c.block();
    QTextBlock next = blk.next();
    if (!next.isValid())
        return true;
    // Never merge across a container edge (into a table, a column or a cell).
    if (!DocumentIO::sameContainer(blk, next) || nextInContainer(blk) != next)
        return true;
    if (!next.isVisible() || blockTypeIsAtomic(typeOf(blk)))
        return true;
    if (blockTypeIsAtomic(typeOf(next))) {
        c.beginEditBlock();
        c.setPosition(blk.position() + blk.length() - 1);
        c.setPosition(next.position() + next.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.endEditBlock();
        return true;
    }
    return false;
}

// ---- commands ------------------------------------------------------------------------

void BlockEditor::turnInto(BlockType type)
{
    const auto [first, last] = selectedBlockRange();
    QTextDocument *doc = document();
    if (type == BlockType::PageLink) {
        // Turn the block (and its children) into a sub-page.
        if (!m_ws)
            return;
        flush();
        QTextBlock blk = doc->findBlockByNumber(first);
        const int end = subtreeEnd(blk);
        QList<Block> children = end > first ? DocumentIO::range(doc, first + 1, end) : QList<Block>();
        for (Block &b : children)
            b.depth = qMax(0, b.depth - depthOf(blk) - 1);
        QString title = blk.text();
        title.remove(QChar::ObjectReplacementCharacter);
        const QString id = m_ws->createPage(m_pageId, title.trimmed(), children);
        EditSteps steps(doc);
        if (end > first)
            steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, first + 1, end, m_ctx.style); });
        steps.run([&](QTextCursor &) {
            Block link;
            link.type = BlockType::PageLink;
            link.depth = depthOf(blk);
            link.pageId = id;
            link.id = idOf(blk);
            setBlockProps(doc->findBlockByNumber(first), link);
        });
        flush();
        return;
    }
    QTextCursor c(doc);
    c.beginEditBlock();
    for (int i = first; i <= last; ++i) {
        QTextBlock blk = doc->findBlockByNumber(i);
        Block props = blockProps(blk);
        if (blockTypeIsAtomic(props.type) || blockTypeIsAtomic(type))
            continue;
        if (DocumentIO::isSimpleTable(DocumentIO::tableOf(blk)))
            continue; // table cells stay plain text
        props.type = type;
        if (!blockTypeIsHeading(type))
            props.toggleable = false;
        if (type == BlockType::Code && props.language.isEmpty())
            props.language = QStringLiteral("plain");
        if (type == BlockType::Callout && props.icon.isEmpty())
            props.icon = QStringLiteral("glyph:bulb");
        setBlockProps(blk, props);
    }
    c.endEditBlock();
    updateVisibility();
}

void BlockEditor::turnIntoToggleHeading(BlockType heading)
{
    const auto [first, last] = selectedBlockRange();
    QTextDocument *doc = document();
    QTextCursor c(doc);
    c.beginEditBlock();
    for (int i = first; i <= last; ++i) {
        QTextBlock blk = doc->findBlockByNumber(i);
        Block props = blockProps(blk);
        if (blockTypeIsAtomic(props.type) || DocumentIO::isSimpleTable(DocumentIO::tableOf(blk)))
            continue;
        props.type = heading;
        props.toggleable = true;
        setBlockProps(blk, props);
    }
    c.endEditBlock();
    updateVisibility();
    scheduleNormalize();
}

void BlockEditor::setShade(int textShade, int highlightShade, bool wholeBlock)
{
    if (textShade >= 0 || highlightShade >= 0) {
        m_lastTextShade = textShade;
        m_lastHighlight = highlightShade;
    }
    QTextCursor sel = textCursor();
    QTextDocument *doc = document();
    if (wholeBlock || !sel.hasSelection()) {
        const auto [first, last] = selectedBlockRange();
        QTextCursor c(doc);
        c.beginEditBlock();
        for (int i = first; i <= last; ++i) {
            QTextBlock blk = doc->findBlockByNumber(i);
            Block props = blockProps(blk);
            if (textShade >= 0)
                props.color = textShade;
            if (highlightShade >= 0)
                props.background = highlightShade;
            setBlockProps(blk, props);
        }
        c.endEditBlock();
        viewport()->update();
        return;
    }
    const int from = sel.selectionStart(), to = sel.selectionEnd();
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock blk = doc->findBlock(from); blk.isValid() && blk.position() < to; blk = blk.next()) {
        const Block props = blockProps(blk);
        QList<QPair<int, int>> ranges;
        QList<Span> spans;
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.charFormat().objectType() >= QTextFormat::UserObject)
                continue;
            const int a = qMax(from, f.position()), b = qMin(to, f.position() + f.length());
            if (a >= b)
                continue;
            Span s = spanOf(f.charFormat(), QString(), props.type, props.checked);
            if (textShade >= 0)
                s.color = textShade;
            if (highlightShade >= 0)
                s.highlight = highlightShade;
            ranges.append({a, b});
            spans.append(s);
        }
        for (int i = 0; i < ranges.size(); ++i) {
            c.setPosition(ranges[i].first);
            c.setPosition(ranges[i].second, QTextCursor::KeepAnchor);
            c.setCharFormat(spanFormat(spans[i], props.type, m_ctx.style, props.checked, props.color));
        }
    }
    c.endEditBlock();
}

void BlockEditor::toggleInline(const QString &what)
{
    QTextCursor sel = textCursor();
    QTextDocument *doc = document();
    auto get = [&](const Span &s) {
        if (what == QLatin1String("bold"))
            return s.bold;
        if (what == QLatin1String("italic"))
            return s.italic;
        if (what == QLatin1String("underline"))
            return s.underline;
        if (what == QLatin1String("strike"))
            return s.strike;
        return s.code;
    };
    auto set = [&](Span &s, bool v) {
        if (what == QLatin1String("bold"))
            s.bold = v;
        else if (what == QLatin1String("italic"))
            s.italic = v;
        else if (what == QLatin1String("underline"))
            s.underline = v;
        else if (what == QLatin1String("strike"))
            s.strike = v;
        else
            s.code = v;
    };
    const Block bp = blockProps(sel.block());
    if (!sel.hasSelection()) {
        // Applies to what is typed next.
        Span s = spanOf(currentCharFormat(), QString(), bp.type, bp.checked);
        set(s, !get(s));
        setCurrentCharFormat(spanFormat(s, bp.type, m_ctx.style, bp.checked, bp.color));
        return;
    }
    const int from = sel.selectionStart(), to = sel.selectionEnd();
    // On if any part is off.
    bool allOn = true;
    for (QTextBlock blk = doc->findBlock(from); blk.isValid() && blk.position() < to; blk = blk.next()) {
        const Block props = blockProps(blk);
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.position() + f.length() <= from || f.position() >= to || f.charFormat().objectType() >= QTextFormat::UserObject)
                continue;
            if (!get(spanOf(f.charFormat(), QString(), props.type, props.checked)))
                allOn = false;
        }
    }
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock blk = doc->findBlock(from); blk.isValid() && blk.position() < to; blk = blk.next()) {
        const Block props = blockProps(blk);
        QList<QPair<int, int>> ranges;
        QList<Span> spans;
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.charFormat().objectType() >= QTextFormat::UserObject)
                continue;
            const int a = qMax(from, f.position()), b = qMin(to, f.position() + f.length());
            if (a >= b)
                continue;
            Span s = spanOf(f.charFormat(), QString(), props.type, props.checked);
            set(s, !allOn);
            ranges.append({a, b});
            spans.append(s);
        }
        for (int i = 0; i < ranges.size(); ++i) {
            c.setPosition(ranges[i].first);
            c.setPosition(ranges[i].second, QTextCursor::KeepAnchor);
            c.setCharFormat(spanFormat(spans[i], props.type, m_ctx.style, props.checked, props.color));
        }
    }
    c.endEditBlock();
    updateFormatBar();
}

void BlockEditor::setLink(const QString &url)
{
    QTextCursor sel = textCursor();
    QString href = url.trimmed();
    if (!href.isEmpty() && !href.contains(QLatin1String("://")) && !href.startsWith(QLatin1String("mailto:"))) {
        href = href.contains(QLatin1Char('@')) && !href.contains(QLatin1Char('/')) ? QStringLiteral("mailto:") + href
                                                                                   : QStringLiteral("https://") + href;
    }
    QTextDocument *doc = document();
    if (!sel.hasSelection()) {
        if (href.isEmpty())
            return;
        const Block bp = blockProps(sel.block());
        Span s;
        s.text = url.trimmed();
        s.link = href;
        sel.insertText(s.text, spanFormat(s, bp.type, m_ctx.style, bp.checked, bp.color));
        setCurrentCharFormat(baseCharFormat(bp.type, m_ctx.style, bp.checked, bp.color));
        return;
    }
    const int from = sel.selectionStart(), to = sel.selectionEnd();
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock blk = doc->findBlock(from); blk.isValid() && blk.position() < to; blk = blk.next()) {
        const Block props = blockProps(blk);
        QList<QPair<int, int>> ranges;
        QList<Span> spans;
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.charFormat().objectType() >= QTextFormat::UserObject)
                continue;
            const int a = qMax(from, f.position()), b = qMin(to, f.position() + f.length());
            if (a >= b)
                continue;
            Span s = spanOf(f.charFormat(), QString(), props.type, props.checked);
            s.link = href;
            ranges.append({a, b});
            spans.append(s);
        }
        for (int i = 0; i < ranges.size(); ++i) {
            c.setPosition(ranges[i].first);
            c.setPosition(ranges[i].second, QTextCursor::KeepAnchor);
            c.setCharFormat(spanFormat(spans[i], props.type, m_ctx.style, props.checked, props.color));
        }
    }
    c.endEditBlock();
}

void BlockEditor::toggleAllToggles()
{
    QTextDocument *doc = document();
    bool anyOpen = false;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (foldsChildren(b) && !isCollapsed(b))
            anyOpen = true;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next())
        if (foldsChildren(b))
            setCollapsed(b, anyOpen);
    updateVisibility();
    scheduleSave();
}

void BlockEditor::insertMention(const QString &pageId)
{
    m_linkedPages.insert(pageId);
    QTextCursor c = textCursor();
    c.insertText(QString(QChar::ObjectReplacementCharacter), mentionFormat(pageId, m_ctx.style));
    const Block bp = blockProps(c.block());
    c.insertText(QStringLiteral(" "), baseCharFormat(bp.type, m_ctx.style, bp.checked, bp.color));
    setTextCursor(c);
}

void BlockEditor::insertPageLink(const QString &pageId)
{
    m_linkedPages.insert(pageId);
    QTextCursor c = textCursor();
    QTextBlock blk = c.block();
    Block link;
    link.type = BlockType::PageLink;
    link.pageId = pageId;
    link.depth = depthOf(blk);
    if (blk.text().isEmpty() && typeOf(blk) == BlockType::Paragraph) {
        c.beginEditBlock();
        setBlockProps(blk, link);
        c.endEditBlock();
        Block p;
        p.depth = link.depth;
        if (!blk.next().isValid())
            insertNewBlockAfter(blk, p);
    } else {
        insertNewBlockAfter(blk, link);
    }
    scheduleNormalize();
}

void BlockEditor::insertImageFile(const QString &path)
{
    if (!m_ws)
        return;
    const QString media = m_ws->importMedia(path);
    if (media.isEmpty()) {
        emit toast(tr("That image couldn't be read."));
        return;
    }
    Block img;
    img.type = BlockType::Image;
    img.media = media;
    QTextCursor c = textCursor();
    QTextBlock blk = c.block();
    img.depth = depthOf(blk);
    if (blk.text().isEmpty() && typeOf(blk) == BlockType::Paragraph) {
        c.beginEditBlock();
        setBlockProps(blk, img);
        c.endEditBlock();
        if (!blk.next().isValid())
            insertNewBlockAfter(blk, Block{});
    } else {
        insertNewBlockAfter(blk, img);
    }
}

void BlockEditor::setBlockIcon(int blockNumber, const QString &icon)
{
    QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    Block props = blockProps(blk);
    props.icon = icon.isEmpty() ? QStringLiteral("glyph:bulb") : icon;
    QTextCursor c(blk);
    c.beginEditBlock();
    setBlockProps(blk, props);
    c.endEditBlock();
    viewport()->update();
}

void BlockEditor::setCodeLanguage(int blockNumber, const QString &language)
{
    QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    Block props = blockProps(blk);
    props.language = language;
    QTextCursor c(blk);
    c.beginEditBlock();
    setBlockProps(blk, props);
    c.endEditBlock();
    m_highlighter->rehighlightBlock(blk);
}

void BlockEditor::createSubpage(bool database, int viewType)
{
    if (!m_ws || m_pageId.isEmpty())
        return;
    flush();
    QString id;
    if (database) {
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
        s.views.append(Schema::makeView(s, ViewType(viewType)));
        if (ViewType(viewType) != ViewType::Table)
            s.views.append(Schema::makeView(s, ViewType::Table));
        id = m_ws->createDatabase(m_pageId, QString(), s);
    } else {
        id = m_ws->createPage(m_pageId);
    }
    insertPageLink(id);
    m_dirty = true;
    flush();
    emit openPage(id);
}

