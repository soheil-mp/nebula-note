#include "BlockEditor.h"
#include "BlockEditorPrivate.h"
#include "CodeHighlighter.h"
#include "SpellChecker.h"
#include "ObjectPopovers.h"
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QUrl>
#include "core/DateParse.h"
#include "ui/Emoji.h"
#include <QSettings>
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

// ---- slash menu ------------------------------------------------------------------------

QString BlockEditor::queryAfter(int position) const
{
    const QTextCursor c = textCursor();
    if (c.position() <= position)
        return {};
    QTextCursor q(document());
    q.setPosition(position + 1);
    q.setPosition(c.position(), QTextCursor::KeepAnchor);
    return q.selectedText();
}

namespace
{
QList<MenuItem> slashItems(bool inCell)
{
    auto tr = [](const char *s) { return QCoreApplication::translate("BlockEditor", s); };
    QList<MenuItem> items;
    auto add = [&](const QString &section, const QString &id, const QString &icon, const QString &title, const QString &sub,
                   const QString &keys, const QString &hint = {}) {
        MenuItem m;
        m.section = section;
        m.id = id;
        m.icon = icon;
        m.title = title;
        m.subtitle = sub;
        m.keywords = keys.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        m.hint = hint;
        items.append(m);
    };
    const QString basic = tr("Basic blocks");
    const QString media = tr("Media");
    const QString db = tr("Database");
    const QString adv = tr("Advanced blocks");
    const QString inl = tr("Inline");
    if (!inCell) {
        add(basic, QStringLiteral("text"), QStringLiteral("text"), tr("Text"), tr("Just start writing with plain text."), QStringLiteral("plain paragraph p"));
        add(basic, QStringLiteral("page"), QStringLiteral("page"), tr("Page"), tr("Embed a sub-page inside this page."), QStringLiteral("subpage new doc"));
        add(basic, QStringLiteral("todo"), QStringLiteral("checkbox"), tr("To-do list"), tr("Track tasks with a to-do list."), QStringLiteral("task check checkbox []"), QStringLiteral("[]"));
        add(basic, QStringLiteral("h1"), QStringLiteral("h1"), tr("Heading 1"), tr("Big section heading."), QStringLiteral("title big # h1"), QStringLiteral("#"));
        add(basic, QStringLiteral("h2"), QStringLiteral("h2"), tr("Heading 2"), tr("Medium section heading."), QStringLiteral("subtitle ## h2"), QStringLiteral("##"));
        add(basic, QStringLiteral("h3"), QStringLiteral("h3"), tr("Heading 3"), tr("Small section heading."), QStringLiteral("### h3"), QStringLiteral("###"));
        add(basic, QStringLiteral("h4"), QStringLiteral("h4"), tr("Heading 4"), tr("Smallest section heading."), QStringLiteral("#### h4"), QStringLiteral("####"));
        add(basic, QStringLiteral("simpletable"), QStringLiteral("table"), tr("Table"), tr("Add a simple table."), QStringLiteral("grid cells rows simple"));
        add(basic, QStringLiteral("bullet"), QStringLiteral("bullet-list"), tr("Bulleted list"), tr("Create a simple bulleted list."), QStringLiteral("ul unordered - list"), QStringLiteral("-"));
        add(basic, QStringLiteral("number"), QStringLiteral("numbered-list"), tr("Numbered list"), tr("Create a list with numbering."), QStringLiteral("ol ordered 1."), QStringLiteral("1."));
        add(basic, QStringLiteral("toggle"), QStringLiteral("toggle"), tr("Toggle list"), tr("Toggles can hide and show content inside."), QStringLiteral("collapse fold details >"), QStringLiteral(">"));
        add(basic, QStringLiteral("quote"), QStringLiteral("quote"), tr("Quote"), tr("Capture a quote."), QStringLiteral("blockquote citation \""), QStringLiteral("\""));
        add(basic, QStringLiteral("divider"), QStringLiteral("divider"), tr("Divider"), tr("Visually divide blocks."), QStringLiteral("hr line separator ---"), QStringLiteral("---"));
        add(basic, QStringLiteral("link"), QStringLiteral("link"), tr("Link to page"), tr("Link to an existing page."), QStringLiteral("reference page"));
        add(basic, QStringLiteral("callout"), QStringLiteral("callout"), tr("Callout"), tr("Make writing stand out."), QStringLiteral("note tip info box warning"));

        add(media, QStringLiteral("image"), QStringLiteral("image"), tr("Image"), tr("Upload an image from your computer."), QStringLiteral("picture photo png jpg"));
        add(media, QStringLiteral("bookmark"), QStringLiteral("bookmark"), tr("Web bookmark"), tr("Save a link as a visual bookmark."), QStringLiteral("link url website preview card"));
        add(media, QStringLiteral("video"), QStringLiteral("video"), tr("Video"), tr("Upload a video file."), QStringLiteral("movie mp4 film clip"));
        add(media, QStringLiteral("audio"), QStringLiteral("audio"), tr("Audio"), tr("Upload an audio file."), QStringLiteral("sound music mp3 podcast"));
        add(media, QStringLiteral("code"), QStringLiteral("code"), tr("Code"), tr("Capture a code snippet."), QStringLiteral("snippet program ```"), QStringLiteral("```"));
        add(media, QStringLiteral("file"), QStringLiteral("file"), tr("File"), tr("Upload or link a file."), QStringLiteral("attachment upload document"));
        add(media, QStringLiteral("pdf"), QStringLiteral("pdf"), tr("PDF"), tr("Upload a PDF."), QStringLiteral("document pdf"));
        add(media, QStringLiteral("embed"), QStringLiteral("embed"), tr("Embed"), tr("YouTube, Figma, Maps and other links."), QStringLiteral("iframe youtube figma maps gist"));

        add(db, QStringLiteral("table"), QStringLiteral("table"), tr("Table view"), tr("A table database on this page."), QStringLiteral("database grid spreadsheet inline"));
        add(db, QStringLiteral("board"), QStringLiteral("board"), tr("Board view"), tr("A kanban board database."), QStringLiteral("database kanban columns"));
        add(db, QStringLiteral("gallery"), QStringLiteral("gallery"), tr("Gallery view"), tr("A grid of cards with previews."), QStringLiteral("database cards grid"));
        add(db, QStringLiteral("dblist"), QStringLiteral("list"), tr("List view"), tr("A minimal list database."), QStringLiteral("database rows"));
        add(db, QStringLiteral("calendar"), QStringLiteral("calendar"), tr("Calendar view"), tr("A month calendar of dated items."), QStringLiteral("database month schedule"));
        add(db, QStringLiteral("timeline"), QStringLiteral("timeline"), tr("Timeline view"), tr("Plan work on a timeline."), QStringLiteral("database gantt roadmap"));
        add(db, QStringLiteral("chart"), QStringLiteral("chart"), tr("Chart view"), tr("Bar, line and donut charts."), QStringLiteral("database graph visualize"));
        add(db, QStringLiteral("feed"), QStringLiteral("feed"), tr("Feed view"), tr("Pages one after another, with content."), QStringLiteral("database posts updates"));
        add(db, QStringLiteral("form"), QStringLiteral("form"), tr("Form"), tr("Collect answers into a database."), QStringLiteral("database survey questions"));
        add(db, QStringLiteral("dbpage"), QStringLiteral("database"), tr("Database – full page"), tr("A database on its own page."), QStringLiteral("database page new"));
        add(db, QStringLiteral("linked"), QStringLiteral("relation"), tr("Linked view of database"), tr("Show an existing database here."), QStringLiteral("database linked view existing"));

        add(adv, QStringLiteral("toc"), QStringLiteral("toc"), tr("Table of contents"), tr("Show an outline of this page."), QStringLiteral("outline headings index"));
        add(adv, QStringLiteral("equation"), QStringLiteral("sigma"), tr("Block equation"), tr("Display a standalone math equation."), QStringLiteral("math tex latex katex formula $$"));
        add(adv, QStringLiteral("button"), QStringLiteral("button"), tr("Button"), tr("Automate inserting blocks and more."), QStringLiteral("action template automation"));
        add(adv, QStringLiteral("breadcrumb"), QStringLiteral("breadcrumb"), tr("Breadcrumb"), tr("Show where this page lives."), QStringLiteral("path navigation"));
        add(adv, QStringLiteral("synced"), QStringLiteral("synced"), tr("Synced block"), tr("Content that stays the same everywhere."), QStringLiteral("sync shared reuse"));
        for (int n = 1; n <= 4; ++n)
            add(adv, QStringLiteral("th%1").arg(n), QStringLiteral("toggle"), tr("Toggle heading %1").arg(n),
                tr("Hide content inside a heading %1.").arg(n), QStringLiteral("toggle heading collapse h%1 fold").arg(n));
        for (int n = 2; n <= 5; ++n)
            add(adv, QStringLiteral("cols%1").arg(n), QStringLiteral("columns"), tr("%1 columns").arg(n),
                tr("Create %1 columns of blocks.").arg(n), QStringLiteral("col columns layout side %1col").arg(n));
        add(adv, QStringLiteral("mermaid"), QStringLiteral("mermaid"), tr("Code – Mermaid"), tr("Draw a flowchart with Mermaid."), QStringLiteral("diagram flowchart graph"));
    }
    add(inl, QStringLiteral("mention"), QStringLiteral("at"), tr("Mention a page"), tr("Link to a page inline."), QStringLiteral("@ link reference"), QStringLiteral("@"));
    add(inl, QStringLiteral("date"), QStringLiteral("calendar"), tr("Date or reminder"), tr("Insert a date, or a reminder."), QStringLiteral("today now time remind"), QStringLiteral("@today"));
    add(inl, QStringLiteral("inlineeq"), QStringLiteral("sigma"), tr("Inline equation"), tr("Math inside a line of text."), QStringLiteral("math tex latex katex formula"), QStringLiteral("Ctrl+Shift+E"));
    add(inl, QStringLiteral("emoji"), QStringLiteral("smile"), tr("Emoji"), tr("Search for an emoji."), QStringLiteral("emoticon smiley icon"), QStringLiteral(":"));

    // Notion's colour names map onto the gray shades.
    const QString colours = tr("Colours");
    static const char *names[] = {"Gray", "Brown", "Orange", "Yellow", "Green", "Blue", "Purple", "Pink", "Red"};
    add(colours, QStringLiteral("fg:0"), QStringLiteral("minus"), tr("Default"), tr("Reset the text and background shade."), QStringLiteral("color colour default reset plain"));
    for (const char *n : names) {
        const QString name = QString::fromLatin1(n);
        const int fg = shadeForNotionColor(name, false), bg = shadeForNotionColor(name, true);
        add(colours, QStringLiteral("fg:%1").arg(fg), QStringLiteral("font"), tr("%1").arg(name),
            tr("%1 text").arg(textShadeName(fg)), QStringLiteral("color colour text %1").arg(name.toLower()));
        add(colours, QStringLiteral("bg:%1").arg(bg), QStringLiteral("palette"), tr("%1 background").arg(name),
            tr("%1 background").arg(highlightShadeName(bg)), QStringLiteral("color colour background highlight %1").arg(name.toLower()));
    }
    for (int i = 1; i < kTextShadeCount; ++i)
        add(colours, QStringLiteral("fg:%1").arg(i), QStringLiteral("font"), tr("%1 text").arg(textShadeName(i)), QString(), QStringLiteral("color colour shade text"));
    for (int i = 1; i < kHighlightShadeCount; ++i)
        add(colours, QStringLiteral("bg:%1").arg(i), QStringLiteral("palette"), tr("%1 background").arg(highlightShadeName(i)), QString(), QStringLiteral("color colour shade background highlight"));
    if (!inCell) {
        const QString actions = tr("Actions");
        add(actions, QStringLiteral("duplicate"), QStringLiteral("copy"), tr("Duplicate"), QString(), QStringLiteral("copy clone"), QStringLiteral("Ctrl+D"));
        add(actions, QStringLiteral("delete"), QStringLiteral("trash"), tr("Delete"), QString(), QStringLiteral("remove"), QStringLiteral("Del"));
        add(actions, QStringLiteral("turnpage"), QStringLiteral("page"), tr("Turn into page"), QString(), QStringLiteral("convert subpage"));
        add(actions, QStringLiteral("blocklink"), QStringLiteral("link"), tr("Copy link to block"), QString(), QStringLiteral("anchor url share"));
        add(actions, QStringLiteral("comment"), QStringLiteral("comment"), tr("Comment"), QString(), QStringLiteral("discuss note feedback"), QStringLiteral("Ctrl+Shift+M"));
    }
    return items;
}

// Commands used recently come first in the empty "/" menu.
QStringList recentSlash()
{
    return QSettings().value(QStringLiteral("editor/recentSlash")).toStringList();
}

void rememberSlash(const QString &id)
{
    QStringList r = recentSlash();
    r.removeAll(id);
    r.prepend(id);
    while (r.size() > 5)
        r.removeLast();
    QSettings().setValue(QStringLiteral("editor/recentSlash"), r);
}
}

void BlockEditor::openSlashMenu()
{
    QWidget *host = m_host ? m_host.data() : window();
    if (!m_slash) {
        m_slash = new CommandPopup(host);
        connect(m_slash, &CommandPopup::activated, this, &BlockEditor::runSlashCommand);
    }
    m_slashPos = textCursor().position() - 1;
    updateSlashMenu();
}

void BlockEditor::updateSlashMenu()
{
    if (!m_slash || m_slashPos < 0)
        return;
    QTextCursor c = textCursor();
    const QTextBlock slashBlock = document()->findBlock(m_slashPos);
    if (c.block() != slashBlock || c.position() <= m_slashPos || document()->characterAt(m_slashPos) != QLatin1Char('/')) {
        closeSlashMenu();
        return;
    }
    const QString query = queryAfter(m_slashPos);
    if (query.startsWith(QLatin1Char(' ')) || query.count(QLatin1Char(' ')) > 2 || query.size() > 28) {
        closeSlashMenu();
        return;
    }
    const bool inCell = DocumentIO::isSimpleTable(DocumentIO::tableOf(c.block()));
    QList<MenuItem> items = slashItems(inCell);
    if (query.isEmpty()) {
        // Recent commands first.
        QList<MenuItem> recent;
        for (const QString &id : recentSlash()) {
            for (const MenuItem &m : items) {
                if (m.id == id) {
                    MenuItem r = m;
                    r.section = tr("Recent");
                    recent.append(r);
                    break;
                }
            }
        }
        items = recent + items;
    }
    if (!query.isEmpty()) {
        QList<MenuItem> scored;
        for (MenuItem &m : items) {
            int s = Workspace::fuzzyScore(query, m.title);
            for (const QString &k : m.keywords)
                s = qMax(s, Workspace::fuzzyScore(query, k) * 9 / 10);
            if (s > 0) {
                m.score = s;
                scored.append(m);
            }
        }
        std::stable_sort(scored.begin(), scored.end(), [](const MenuItem &a, const MenuItem &b) { return a.score > b.score; });
        QSet<QString> seenIds;
        scored.removeIf([&](const MenuItem &m) {
            if (seenIds.contains(m.id + m.title))
                return true;
            seenIds.insert(m.id + m.title);
            return false;
        });
        if (scored.isEmpty() && query.size() > 5) {
            closeSlashMenu();
            return;
        }
        items = scored;
    }
    m_slash->setLargeIcons(true);
    m_slash->setItems(items, query.isEmpty());
    QWidget *host = m_slash->parentWidget();
    const QRect caret = cursorRect();
    const QRect anchor(viewport()->mapTo(host, caret.topLeft()), caret.size());
    m_slash->popup(anchor);
}

void BlockEditor::closeSlashMenu()
{
    m_slashPos = -1;
    if (m_slash)
        m_slash->dismiss();
}

void BlockEditor::runSlashCommand(const QString &id)
{
    if (m_slashPos < 0)
        return;
    QTextCursor c = textCursor();
    // Remove "/query".
    c.setPosition(m_slashPos);
    c.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    closeSlashMenu();

    QTextBlock blk = c.block();
    const bool emptyBlock = blk.text().isEmpty();
    static const QHash<QString, BlockType> types = {
        {QStringLiteral("text"), BlockType::Paragraph}, {QStringLiteral("h1"), BlockType::Heading1},
        {QStringLiteral("h2"), BlockType::Heading2},    {QStringLiteral("h3"), BlockType::Heading3},
        {QStringLiteral("h4"), BlockType::Heading4},
        {QStringLiteral("bullet"), BlockType::Bullet},  {QStringLiteral("number"), BlockType::Numbered},
        {QStringLiteral("todo"), BlockType::Todo},      {QStringLiteral("toggle"), BlockType::Toggle},
        {QStringLiteral("quote"), BlockType::Quote},    {QStringLiteral("callout"), BlockType::Callout},
        {QStringLiteral("code"), BlockType::Code}};
    if (types.contains(id)) {
        const BlockType t = types.value(id);
        if (emptyBlock || typeOf(blk) == BlockType::Paragraph) {
            turnInto(t);
        } else {
            Block b;
            b.type = t;
            b.depth = depthOf(blk);
            if (t == BlockType::Code)
                b.language = QStringLiteral("plain");
            if (t == BlockType::Callout)
                b.icon = QStringLiteral("glyph:bulb");
            insertNewBlockAfter(blk, b);
        }
        return;
    }
    if (id == QLatin1String("divider")) {
        Block d;
        d.type = BlockType::Divider;
        d.depth = depthOf(blk);
        if (emptyBlock) {
            c.beginEditBlock();
            setBlockProps(blk, d);
            c.endEditBlock();
            Block p;
            p.depth = d.depth;
            insertNewBlockAfter(blk, p);
        } else {
            insertNewBlockAfter(blk, d);
            Block p;
            p.depth = d.depth;
            insertNewBlockAfter(textCursor().block(), p);
        }
        return;
    }
    if (id == QLatin1String("page")) {
        createSubpage(false);
        return;
    }
    rememberSlash(id);
    auto objectBlock = [&](BlockType t) {
        Block b;
        b.type = t;
        insertObjectBlock(b);
    };
    if (id == QLatin1String("equation")) {
        objectBlock(BlockType::Equation);
        return;
    }
    if (id == QLatin1String("inlineeq")) {
        insertEquation(true);
        return;
    }
    if (id == QLatin1String("toc")) {
        objectBlock(BlockType::TableOfContents);
        return;
    }
    if (id == QLatin1String("breadcrumb")) {
        objectBlock(BlockType::Breadcrumb);
        return;
    }
    if (id == QLatin1String("button")) {
        objectBlock(BlockType::Button);
        return;
    }
    if (id == QLatin1String("bookmark")) {
        objectBlock(BlockType::Bookmark);
        return;
    }
    if (id == QLatin1String("embed")) {
        objectBlock(BlockType::Embed);
        return;
    }
    if (id == QLatin1String("video") || id == QLatin1String("audio") || id == QLatin1String("file") || id == QLatin1String("pdf")) {
        objectBlock(id == QLatin1String("video") ? BlockType::Video
                    : id == QLatin1String("audio") ? BlockType::Audio
                    : id == QLatin1String("pdf")   ? BlockType::Pdf
                                                   : BlockType::File);
        return;
    }
    if (id == QLatin1String("mermaid")) {
        Block b;
        b.type = BlockType::Code;
        b.language = QStringLiteral("mermaid");
        b.setPlainText(QStringLiteral("graph TD") + QChar::LineSeparator + QStringLiteral("    A[Idea] --> B{Worth it?}") + QChar::LineSeparator
                       + QStringLiteral("    B -->|Yes| C[Build it]") + QChar::LineSeparator + QStringLiteral("    B -->|No| D[Write it down]"));
        if (emptyBlock) {
            b.depth = depthOf(blk);
            b.id = idOf(blk);
            QTextCursor e(blk);
            e.beginEditBlock();
            setBlockProps(blk, b);
            QTextCursor w(blk);
            w.movePosition(QTextCursor::EndOfBlock);
            DocumentIO::writeContent(w, b, m_ctx.style);
            e.endEditBlock();
            m_highlighter->rehighlightBlock(blk);
        } else {
            b.depth = depthOf(blk);
            insertNewBlockAfter(blk, b);
        }
        return;
    }
    if (id == QLatin1String("emoji")) {
        emit emojiRequested(viewport()->mapToGlobal(cursorRect().bottomLeft() + QPoint(0, 6)));
        return;
    }
    if (id == QLatin1String("blocklink")) {
        copyLinkToBlock(blk.blockNumber());
        return;
    }
    if (id == QLatin1String("comment")) {
        addComment();
        return;
    }
    if (id == QLatin1String("synced")) {
        turnIntoSynced();
        return;
    }
    if (id == QLatin1String("linked")) {
        emit linkedDatabaseRequested(viewport()->mapToGlobal(cursorRect().bottomLeft() + QPoint(0, 6)));
        return;
    }
    if (id.startsWith(QLatin1String("th"))) {
        static const BlockType hs[] = {BlockType::Heading1, BlockType::Heading2, BlockType::Heading3, BlockType::Heading4};
        const BlockType h = hs[qBound(1, id.mid(2).toInt(), 4) - 1];
        if (emptyBlock || typeOf(blk) == BlockType::Paragraph || blockTypeIsHeading(typeOf(blk))) {
            turnIntoToggleHeading(h);
        } else {
            Block b;
            b.type = h;
            b.toggleable = true;
            b.depth = depthOf(blk);
            insertNewBlockAfter(blk, b);
        }
        return;
    }
    if (id == QLatin1String("simpletable")) {
        insertSimpleTable(3, 3);
        return;
    }
    if (id.startsWith(QLatin1String("cols"))) {
        insertColumnList(qBound(2, id.mid(4).toInt(), 5));
        return;
    }
    if (id == QLatin1String("table") || id == QLatin1String("board") || id == QLatin1String("dblist") || id == QLatin1String("gallery")
        || id == QLatin1String("calendar") || id == QLatin1String("timeline") || id == QLatin1String("chart") || id == QLatin1String("feed")
        || id == QLatin1String("form")) {
        const ViewType vt = id == QLatin1String("board")      ? ViewType::Board
                            : id == QLatin1String("dblist")   ? ViewType::List
                            : id == QLatin1String("gallery")  ? ViewType::Gallery
                            : id == QLatin1String("calendar") ? ViewType::Calendar
                            : id == QLatin1String("timeline") ? ViewType::Timeline
                            : id == QLatin1String("chart")    ? ViewType::Chart
                            : id == QLatin1String("feed")     ? ViewType::Feed
                            : id == QLatin1String("form")     ? ViewType::Form
                                                              : ViewType::Table;
        createInlineDatabase(int(vt));
        return;
    }
    if (id == QLatin1String("dbpage")) {
        createSubpage(true, int(ViewType::Table));
        return;
    }
    if (id == QLatin1String("link")) {
        m_mentionTrigger = QStringLiteral("link");
        m_mentionPos = textCursor().position();
        textCursor().insertText(QStringLiteral(" "));
        QTextCursor back = textCursor();
        back.movePosition(QTextCursor::PreviousCharacter);
        setTextCursor(back);
        m_mentionPos = back.position() - 1;
        openMentionMenu(QStringLiteral("link"));
        return;
    }
    if (id == QLatin1String("mention")) {
        textCursor().insertText(QStringLiteral("@"));
        openMentionMenu(QStringLiteral("@"));
        return;
    }
    if (id == QLatin1String("image")) {
        const QString path = QFileDialog::getOpenFileName(this, tr("Insert image"), QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg)"));
        if (!path.isEmpty())
            insertImageFile(path);
        return;
    }
    if (id == QLatin1String("date")) {
        insertDateMention(QDateTime(QDate::currentDate(), QTime(0, 0)), false, false);
        return;
    }
    if (id.startsWith(QLatin1String("fg:"))) {
        const int n = id.mid(3).toInt();
        setShade(n, n == 0 ? 0 : -1, true);
        return;
    }
    if (id.startsWith(QLatin1String("bg:"))) {
        setShade(-1, id.mid(3).toInt(), true);
        return;
    }
    if (id == QLatin1String("duplicate")) {
        duplicateBlocks();
        return;
    }
    if (id == QLatin1String("delete")) {
        deleteBlocks();
        return;
    }
    if (id == QLatin1String("turnpage")) {
        turnInto(BlockType::PageLink);
        return;
    }
}

// ---- mention menu ------------------------------------------------------------------------

void BlockEditor::openMentionMenu(const QString &trigger)
{
    QWidget *host = m_host ? m_host.data() : window();
    if (!m_mention) {
        m_mention = new CommandPopup(host);
        m_mention->setLargeIcons(false);
        m_mention->setFixedCardWidth(360);
        connect(m_mention, &CommandPopup::activated, this, &BlockEditor::runMention);
    }
    m_mentionTrigger = trigger;
    if (trigger == QLatin1String("@") || trigger == QLatin1String("+") || trigger == QLatin1String(":"))
        m_mentionPos = textCursor().position() - 1;
    else if (trigger == QLatin1String("[["))
        m_mentionPos = textCursor().position() - 2;
    updateMentionMenu();
}

void BlockEditor::updateMentionMenu()
{
    if (!m_mention || m_mentionPos < 0 || !m_ws)
        return;
    QTextCursor c = textCursor();
    if (c.block() != document()->findBlock(m_mentionPos) || c.position() < m_mentionPos + (m_mentionTrigger == QLatin1String("link") ? 0 : 1)) {
        closeMentionMenu();
        return;
    }
    const int queryStart = m_mentionTrigger == QLatin1String("[[") ? m_mentionPos + 1 : m_mentionPos;
    QString query = m_mentionTrigger == QLatin1String("link") ? QString() : queryAfter(queryStart);
    if (m_mentionTrigger == QLatin1String("[[") && query.endsWith(QLatin1String("]]")))
        query.chop(2);
    if (query.size() > 40 || query.count(QLatin1Char(' ')) > 4) {
        closeMentionMenu();
        return;
    }
    QList<MenuItem> items;
    // ":" picks an emoji by name (two letters before the list opens).
    if (m_mentionTrigger == QLatin1String(":")) {
        if (query.size() < 2 || query.contains(QLatin1Char(' '))) {
            if (query.contains(QLatin1Char(' ')) || query.size() > 30)
                closeMentionMenu();
            else if (m_mention)
                m_mention->dismiss();
            return;
        }
        for (const auto &e : Emoji::search(query, 10)) {
            MenuItem item;
            item.id = QStringLiteral("emoji:") + e.second;
            item.icon = e.second;
            item.pageIcon = true;
            item.title = QLatin1Char(':') + e.first + QLatin1Char(':');
            items.append(item);
        }
        if (items.isEmpty()) {
            if (m_mention)
                m_mention->dismiss();
            return;
        }
        m_mention->setItems(items, false);
        QWidget *host = m_mention->parentWidget();
        const QRect caret = cursorRect();
        m_mention->popup(QRect(viewport()->mapTo(host, caret.topLeft()), caret.size()));
        return;
    }
    // "@" also understands dates and reminders ("@tomorrow 3pm", "@remind friday").
    if (m_mentionTrigger == QLatin1String("@")) {
        const QDateTime now = QDateTime::currentDateTime();
        auto addDate = [&](const QDateTime &when, bool hasTime, bool remind, const QString &title) {
            MenuItem item;
            item.id = QStringLiteral("date:%1|%2|%3").arg(when.toString(Qt::ISODate)).arg(hasTime ? 1 : 0).arg(remind ? 1 : 0);
            item.icon = remind ? QStringLiteral("bell") : QStringLiteral("calendar");
            item.title = title;
            item.subtitle = QLocale(QLocale::English).toString(when.date(), QStringLiteral("dddd, MMMM d, yyyy"))
                            + (hasTime ? QLatin1Char(' ') + QLocale(QLocale::English).toString(when.time(), QStringLiteral("h:mm AP")) : QString());
            item.section = remind ? tr("Remind") : tr("Date");
            items.append(item);
        };
        if (query.trimmed().isEmpty()) {
            addDate(QDateTime(now.date(), QTime(0, 0)), false, false, tr("Today"));
            addDate(QDateTime(now.date().addDays(1), QTime(0, 0)), false, false, tr("Tomorrow"));
            addDate(QDateTime(now.date().addDays(1), QTime(9, 0)), true, true, tr("Remind me tomorrow at 9:00 AM"));
        } else {
            const DateParse::Result r = DateParse::parse(query, now);
            if (r.when.isValid())
                addDate(r.when, r.hasTime, r.remind, DateParse::relativeLabel(r.when, r.hasTime, now.date()));
            else if (QStringLiteral("remind").startsWith(query.trimmed().toLower()) || query.trimmed().toLower() == QLatin1String("remind me"))
                addDate(QDateTime(now.date().addDays(1), QTime(9, 0)), true, true, tr("Remind me tomorrow at 9:00 AM"));
            else if (QStringLiteral("today").startsWith(query.trimmed().toLower()) || QStringLiteral("tomorrow").startsWith(query.trimmed().toLower())) {
                const DateParse::Result g = DateParse::parse(QStringLiteral("today").startsWith(query.trimmed().toLower()) ? QStringLiteral("today")
                                                                                                                         : QStringLiteral("tomorrow"),
                                                             now);
                addDate(g.when, false, false, DateParse::relativeLabel(g.when, false, now.date()));
            }
        }
    }
    auto pathOf = [this](const QString &id) {
        QStringList names;
        for (const QString &a : m_ws->ancestors(id))
            names.append(m_ws->title(a));
        return names.join(QStringLiteral(" / "));
    };
    if (query.trimmed().isEmpty()) {
        for (const PageMeta *m : m_ws->recent(10)) {
            if (m->id == m_pageId || m->isRow())
                continue;
            MenuItem item;
            item.id = m->id;
            item.icon = m->icon;
            item.pageIcon = true;
            item.title = m_ws->title(m->id);
            item.subtitle = pathOf(m->id);
            item.section = tr("Recent");
            items.append(item);
        }
    } else {
        for (const auto &hit : m_ws->search(query, 12)) {
            if (!hit.titleMatch || hit.id == m_pageId)
                continue;
            const PageMeta *m = m_ws->page(hit.id);
            MenuItem item;
            item.id = hit.id;
            item.icon = m ? m->icon : QString();
            item.pageIcon = true;
            item.title = m_ws->title(hit.id);
            item.subtitle = pathOf(hit.id);
            item.section = tr("Pages");
            items.append(item);
        }
        MenuItem create;
        create.id = QStringLiteral("new:") + query.trimmed();
        create.icon = QStringLiteral("plus");
        create.title = tr("New sub-page “%1”").arg(query.trimmed());
        create.section = tr("Create");
        items.append(create);
    }
    // Dates first when the query reads like one; pages first otherwise.
    if (m_mentionTrigger == QLatin1String("@") && !query.trimmed().isEmpty()) {
        QList<MenuItem> dates, rest;
        for (const MenuItem &m : items)
            (m.id.startsWith(QLatin1String("date:")) ? dates : rest).append(m);
        items = dates + rest;
    }
    m_mention->setEmptyText(tr("Type to search pages"));
    m_mention->setItems(items, true);
    QWidget *host = m_mention->parentWidget();
    const QRect caret = cursorRect();
    m_mention->popup(QRect(viewport()->mapTo(host, caret.topLeft()), caret.size()));
}

void BlockEditor::closeMentionMenu()
{
    m_mentionPos = -1;
    if (m_mention)
        m_mention->dismiss();
}

void BlockEditor::runMention(const QString &id)
{
    if (m_mentionPos < 0 || !m_ws)
        return;
    const QString trigger = m_mentionTrigger;
    QTextCursor c = textCursor();
    c.setPosition(m_mentionPos);
    int end = textCursor().position();
    if (trigger == QLatin1String("link"))
        end = m_mentionPos + 1; // the placeholder space
    c.setPosition(end, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    setTextCursor(c);
    closeMentionMenu();
    if (id.startsWith(QLatin1String("emoji:"))) {
        insertText(id.mid(6));
        return;
    }
    if (id.startsWith(QLatin1String("date:"))) {
        const QStringList parts = id.mid(5).split(QLatin1Char('|'));
        insertDateMention(QDateTime::fromString(parts.value(0), Qt::ISODate), parts.value(1) == QLatin1String("1"),
                          parts.value(2) == QLatin1String("1"));
        return;
    }
    QString target = id;
    if (id.startsWith(QLatin1String("new:"))) {
        flush();
        target = m_ws->createPage(m_pageId, id.mid(4));
    }
    if (trigger == QLatin1String("link") || trigger == QLatin1String("+"))
        insertPageLink(target);
    else
        insertMention(target);
}

// ---- format bar & menus ------------------------------------------------------------------

void BlockEditor::updateFormatBar()
{
    const QTextCursor c = textCursor();
    const bool show = c.hasSelection() && hasFocus() && !isReadOnly() && !(m_slash && m_slash->isVisible());
    if (!show) {
        if (m_formatBar && !m_formatBar->isEditingLink())
            m_formatBar->dismiss();
        return;
    }
    QWidget *host = m_host ? m_host.data() : window();
    if (!m_formatBar) {
        m_formatBar = new FormatBar(host);
        connect(m_formatBar, &FormatBar::action, this, [this](const QString &id, const QPoint &global) {
            if (id == QLatin1String("turn"))
                showTurnIntoMenu(global);
            else if (id == QLatin1String("shade"))
                showShadeMenu(global);
            else if (id == QLatin1String("link"))
                m_formatBar->editLink(textCursor().charFormat().anchorHref());
            else if (id == QLatin1String("mention")) {
                // Turn the selection into a sub-page link.
                const QString text = textCursor().selectedText().trimmed();
                if (!text.isEmpty() && m_ws) {
                    flush();
                    const QString page = m_ws->createPage(m_pageId, text);
                    QTextCursor sc = textCursor();
                    sc.removeSelectedText();
                    setTextCursor(sc);
                    insertMention(page);
                }
            } else
                toggleInline(id);
        });
        connect(m_formatBar, &FormatBar::linkEntered, this, [this](const QString &url) {
            setFocus();
            if (url != QLatin1String("\x1b"))
                setLink(url);
        });
    }
    // Union of the selection's start and end caret rects.
    QTextCursor a(document()), b(document());
    a.setPosition(c.selectionStart());
    b.setPosition(c.selectionEnd());
    const QRect ra = cursorRect(a), rb = cursorRect(b);
    QRect sel = ra.united(rb);
    if (ra.top() != rb.top())
        sel = QRect(ra.left(), ra.top(), qMax(80, viewport()->width() / 3), rb.bottom() - ra.top());
    sel.moveTopLeft(viewport()->mapTo(host, sel.topLeft()));
    const Block bp = blockProps(document()->findBlock(c.selectionStart()));
    FormatBar::State st;
    bool any = false;
    st.bold = st.italic = st.underline = st.strike = st.code = true;
    for (QTextBlock blk = document()->findBlock(c.selectionStart()); blk.isValid() && blk.position() < c.selectionEnd(); blk = blk.next()) {
        const Block props = blockProps(blk);
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.position() + f.length() <= c.selectionStart() || f.position() >= c.selectionEnd()
                || f.charFormat().objectType() >= QTextFormat::UserObject)
                continue;
            const Span s = spanOf(f.charFormat(), QString(), props.type, props.checked);
            st.bold &= s.bold;
            st.italic &= s.italic;
            st.underline &= s.underline;
            st.strike &= s.strike;
            st.code &= s.code;
            st.link |= !s.link.isEmpty();
            any = true;
        }
    }
    if (!any)
        st = FormatBar::State{};
    st.typeName = blockTypeName(bp.type);
    m_formatBar->showFor(sel, st);
}

void BlockEditor::showTurnIntoMenu(const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    const QList<QPair<BlockType, QString>> types = {
        {BlockType::Paragraph, QStringLiteral("text")},       {BlockType::Heading1, QStringLiteral("h1")},
        {BlockType::Heading2, QStringLiteral("h2")},          {BlockType::Heading3, QStringLiteral("h3")},
        {BlockType::Heading4, QStringLiteral("h4")},
        {BlockType::PageLink, QStringLiteral("page")},        {BlockType::Todo, QStringLiteral("checkbox")},
        {BlockType::Bullet, QStringLiteral("bullet-list")},   {BlockType::Numbered, QStringLiteral("numbered-list")},
        {BlockType::Toggle, QStringLiteral("toggle")},        {BlockType::Code, QStringLiteral("code")},
        {BlockType::Quote, QStringLiteral("quote")},          {BlockType::Callout, QStringLiteral("callout")}};
    const BlockType current = typeOf(textCursor().block());
    for (const auto &[t, icon] : types) {
        QAction *a = menu->addAction(Icons::icon(icon), blockTypeName(t));
        a->setCheckable(true);
        a->setChecked(t == current);
        connect(a, &QAction::triggered, this, [this, t = t] { turnInto(t); });
    }
    menu->addSeparator();
    const bool currentToggle = toggleableOf(textCursor().block());
    for (BlockType h : {BlockType::Heading1, BlockType::Heading2, BlockType::Heading3, BlockType::Heading4}) {
        QAction *a = menu->addAction(Icons::icon(QStringLiteral("toggle")), tr("Toggle %1").arg(blockTypeName(h).toLower()));
        a->setCheckable(true);
        a->setChecked(currentToggle && h == current);
        connect(a, &QAction::triggered, this, [this, h] { turnIntoToggleHeading(h); });
    }
    menu->popup(globalPos);
}

void BlockEditor::showShadeMenu(const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    const bool selection = textCursor().hasSelection();
    menu->addSection(tr("Text"));
    for (int i = 0; i < kTextShadeCount; ++i) {
        QPixmap swatch(16, 16);
        swatch.fill(Qt::transparent);
        QPainter p(&swatch);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(Theme::uiFont(12, QFont::DemiBold));
        p.setPen(Theme::textShade(i));
        p.drawText(swatch.rect(), Qt::AlignCenter, QStringLiteral("A"));
        p.end();
        QAction *a = menu->addAction(QIcon(swatch), textShadeName(i));
        connect(a, &QAction::triggered, this, [this, i, selection] { setShade(i, -1, !selection); });
    }
    menu->addSection(tr("Background"));
    for (int i = 0; i < kHighlightShadeCount; ++i) {
        QPixmap swatch(16, 16);
        swatch.fill(Qt::transparent);
        QPainter p(&swatch);
        p.setRenderHint(QPainter::Antialiasing);
        const Theme::Highlight hl = Theme::highlightShade(i);
        p.setPen(QPen(Theme::colors().ash, 1));
        p.setBrush(hl.background.isValid() ? hl.background : QColor(Qt::transparent));
        p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
        p.end();
        QAction *a = menu->addAction(QIcon(swatch), highlightShadeName(i));
        connect(a, &QAction::triggered, this, [this, i, selection] { setShade(-1, i, !selection); });
    }
    menu->popup(globalPos);
}

void BlockEditor::showLanguageMenu(int blockNumber, const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    const QString current = document()->findBlockByNumber(blockNumber).blockFormat().property(LanguageProp).toString();
    for (const QString &lang : CodeHighlighter::languages()) {
        QAction *a = menu->addAction(CodeHighlighter::languageName(lang));
        a->setCheckable(true);
        a->setChecked(lang == current || (current.isEmpty() && lang == QLatin1String("plain")));
        connect(a, &QAction::triggered, this, [this, blockNumber, lang] { setCodeLanguage(blockNumber, lang); });
    }
    menu->popup(globalPos);
}

void BlockEditor::showBlockMenu(int blockNumber, const QPoint &globalPos)
{
    QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    // Commands act on the block (and keep a multi-block selection that contains it).
    const auto range = selectedBlockRange();
    if (!(textCursor().hasSelection() && blockNumber >= range.first && blockNumber <= range.second)) {
        QTextCursor c(document());
        c.setPosition(blk.position());
        setTextCursor(c);
    }
    const BlockType type = typeOf(blk);
    const Block props = blockProps(blk);
    QMenu *menu = Theme::makeMenu(this);
    menu->addSection(toggleableOf(blk) ? tr("Toggle %1").arg(blockTypeName(type).toLower()) : blockTypeName(type));

    // Type-specific actions first, like Notion's.
    const QPoint below = objectPopupPos(blk);
    switch (type) {
    case BlockType::Equation:
        menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Edit equation"), this, [this, blockNumber] { editBlockEquation(blockNumber); });
        break;
    case BlockType::Button:
        menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Edit button"), this, [this, blockNumber, below] { configureButton(blockNumber, below); });
        break;
    case BlockType::Bookmark:
    case BlockType::Embed:
        menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Edit link"), this, [this, blockNumber, below] { askForLink(blockNumber, below); });
        if (!props.url.isEmpty()) {
            menu->addAction(Icons::icon(QStringLiteral("external")), tr("Open link"), this, [url = props.url] { QDesktopServices::openUrl(QUrl(url)); });
            menu->addAction(Icons::icon(QStringLiteral("refresh")), tr("Refresh preview"), this, [this, id = props.id, url = props.url] { fetchPreview(id, url); });
        }
        break;
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf:
        menu->addAction(Icons::icon(QStringLiteral("upload")), tr("Replace file…"), this, [this, blockNumber] { chooseFile(blockNumber); });
        if (!props.media.isEmpty() || !props.url.isEmpty())
            menu->addAction(Icons::icon(QStringLiteral("external")), tr("Open"), this, [this, blockNumber] { openObject(blockNumber); });
        if (!props.media.isEmpty())
            menu->addAction(Icons::icon(QStringLiteral("download")), tr("Download…"), this, [this, props] {
                const QString name = props.data.value(QStringLiteral("name")).toString();
                const QString to = QFileDialog::getSaveFileName(this, tr("Download"), name.isEmpty() ? props.media : name);
                if (!to.isEmpty() && m_ws)
                    QFile::copy(m_ws->mediaPath(props.media), to);
            });
        break;
    case BlockType::Image: {
        QMenu *align = menu->addMenu(Icons::icon(QStringLiteral("align-center")), tr("Align"));
        for (const auto &[key, label, icon] : {std::tuple{QStringLiteral("left"), tr("Left"), QStringLiteral("align-left")},
                                               std::tuple{QStringLiteral("center"), tr("Center"), QStringLiteral("align-center")},
                                               std::tuple{QStringLiteral("right"), tr("Right"), QStringLiteral("align-right")}}) {
            QAction *a = align->addAction(Icons::icon(icon), label, this, [this, blockNumber, key = key] {
                setBlockData(blockNumber, [&](Block &p) { p.align = key == QLatin1String("center") ? QString() : key; });
            });
            a->setCheckable(true);
            a->setChecked(props.align == key || (key == QLatin1String("center") && props.align.isEmpty()));
        }
        QAction *full = menu->addAction(Icons::icon(QStringLiteral("width")), tr("Full width"));
        full->setCheckable(true);
        full->setChecked(props.align == QLatin1String("full"));
        connect(full, &QAction::toggled, this, [this, blockNumber](bool on) {
            setBlockData(blockNumber, [&](Block &p) { p.align = on ? QStringLiteral("full") : QString(); });
        });
        menu->addAction(Icons::icon(QStringLiteral("fullscreen")), tr("View full screen"), this, [this, props] {
            if (m_ws) {
                QString cap;
                for (const Span &sp : props.caption)
                    cap += sp.text;
                new ImageViewer(window(), m_ws->mediaPath(props.media), cap);
            }
        });
        menu->addAction(Icons::icon(QStringLiteral("upload")), tr("Replace…"), this, [this, blockNumber] {
            const QString path = QFileDialog::getOpenFileName(this, tr("Replace image"), QString(),
                                                              tr("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp *.svg)"));
            if (path.isEmpty() || !m_ws)
                return;
            const QString media = m_ws->importMedia(path);
            if (media.isEmpty())
                return;
            QTextBlock b = document()->findBlockByNumber(blockNumber);
            QTextCursor c(b);
            c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            c.beginEditBlock();
            c.insertText(QString(QChar::ObjectReplacementCharacter), imageFormat(media, 0));
            c.endEditBlock();
        });
        menu->addAction(Icons::icon(QStringLiteral("download")), tr("Download…"), this, [this, props] {
            const QString to = QFileDialog::getSaveFileName(this, tr("Download image"), props.media);
            if (!to.isEmpty() && m_ws)
                QFile::copy(m_ws->mediaPath(props.media), to);
        });
        menu->addAction(Icons::icon(QStringLiteral("external")), tr("Open original"), this, [this, props] {
            if (m_ws)
                QDesktopServices::openUrl(QUrl::fromLocalFile(m_ws->mediaPath(props.media)));
        });
        break;
    }
    default:
        break;
    }
    const bool captionable = type == BlockType::Image || type == BlockType::Code || type == BlockType::Equation || type == BlockType::Bookmark
                             || type == BlockType::Embed || blockTypeIsFile(type);
    if (captionable)
        menu->addAction(Icons::icon(QStringLiteral("caption")), props.caption.isEmpty() ? tr("Caption") : tr("Edit caption"), this,
                        [this, blockNumber, below] { editCaption(blockNumber, below); });
    if (menu->actions().size() > 1)
        menu->addSeparator();

    QAction *del = menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete"));
    del->setShortcut(QKeySequence::Delete);
    connect(del, &QAction::triggered, this, &BlockEditor::deleteBlocks);
    QAction *dup = menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate"));
    dup->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    connect(dup, &QAction::triggered, this, &BlockEditor::duplicateBlocks);
    if (!blockTypeIsAtomic(type)) {
        QAction *turn = menu->addAction(Icons::icon(QStringLiteral("refresh")), tr("Turn into…"));
        turn->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
        connect(turn, &QAction::triggered, this, [this, globalPos] { showTurnIntoMenu(globalPos); });
    }
    QAction *link = menu->addAction(Icons::icon(QStringLiteral("link")), tr("Copy link to block"));
    link->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_L));
    connect(link, &QAction::triggered, this, [this, blockNumber] { copyLinkToBlock(blockNumber); });
    QAction *shade = menu->addAction(Icons::icon(QStringLiteral("palette")), tr("Colour…"));
    connect(shade, &QAction::triggered, this, [this, globalPos] { showShadeMenu(globalPos); });
    QAction *move = menu->addAction(Icons::icon(QStringLiteral("move")), tr("Move to…"));
    move->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(move, &QAction::triggered, this, [this, globalPos] { emit moveBlocksRequested(globalPos); });
    QAction *comment = menu->addAction(Icons::icon(QStringLiteral("comment")), tr("Comment"));
    comment->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    connect(comment, &QAction::triggered, this, &BlockEditor::addComment);
    // Synced blocks.
    const QString sync = syncOf(blk);
    if (!sync.isEmpty()) {
        menu->addSeparator();
        const int pages = m_ws ? int(m_ws->syncedPages(sync).size()) : 1;
        QAction *info = menu->addAction(Icons::icon(QStringLiteral("synced")), tr("Synced · editing in %n page(s)", nullptr, qMax(1, pages)));
        info->setEnabled(false);
        menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Copy and sync"), this, [this] {
            copy();
            emit toast(tr("Copied — paste on any page to show this synced block there."));
        });
        menu->addAction(Icons::icon(QStringLiteral("close")), tr("Unsync"), this, [this, sync] { unsyncBlocks(sync); });
        if (m_ws && m_ws->syncedOrigin(sync) != m_pageId && m_ws->exists(m_ws->syncedOrigin(sync)))
            menu->addAction(Icons::icon(QStringLiteral("external")), tr("Go to original"), this, [this, sync] {
                flush();
                emit openPage(m_ws->syncedOrigin(sync));
            });
    } else if (!blockTypeIsAtomic(type) || type == BlockType::Divider) {
        menu->addAction(Icons::icon(QStringLiteral("synced")), tr("Turn into synced block"), this, &BlockEditor::turnIntoSynced);
    }
    menu->addSeparator();
    QAction *up = menu->addAction(Icons::icon(QStringLiteral("arrow-up")), tr("Move up"));
    up->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up));
    connect(up, &QAction::triggered, this, [this] { moveBlocks(-1); });
    QAction *down = menu->addAction(Icons::icon(QStringLiteral("arrow-down")), tr("Move down"));
    down->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down));
    connect(down, &QAction::triggered, this, [this] { moveBlocks(1); });
    QAction *md = menu->addAction(Icons::icon(QStringLiteral("download")), tr("Copy as Markdown"));
    connect(md, &QAction::triggered, this, [this] {
        const auto r = withChildren(selectedBlockRange());
        Markdown::ExportOptions opt;
        opt.pageTitle = [this](const QString &id) { return m_ws ? m_ws->title(id) : QString(); };
        QApplication::clipboard()->setText(Markdown::fromBlocks(DocumentIO::range(document(), r.first, r.second), opt));
        emit toast(tr("Copied as Markdown"));
    });
    // Word and character count of the block(s).
    {
        const auto r = withChildren(selectedBlockRange());
        const QPair<int, int> counts = blocksWordCount(DocumentIO::range(document(), r.first, r.second));
        menu->addSeparator();
        QAction *wc = menu->addAction(tr("%n word(s)", nullptr, counts.first) + QStringLiteral(" · ") + tr("%n character(s)", nullptr, counts.second));
        wc->setEnabled(false);
    }
    menu->popup(globalPos);
}

void BlockEditor::contextMenuEvent(QContextMenuEvent *e)
{
    // Inside a simple table: the table's own menu for that cell.
    {
        const QTextCursor at = cursorForPosition(e->pos());
        if (QTextTable *t = at.currentTable(); DocumentIO::isSimpleTable(t) && !isReadOnly()) {
            const QTextTableCell cell = t->cellAt(at.position());
            if (!textCursor().hasSelection())
                setTextCursor(at);
            showTableMenu(t, cell.row(), cell.column(), e->globalPos());
            return;
        }
    }
    QMenu *menu = Theme::makeMenu(this);
    const bool sel = textCursor().hasSelection();
    // Spelling suggestions for a misspelled word under the pointer.
    if (m_highlighter->spellCheck() && !isReadOnly() && !sel) {
        QTextCursor wc = cursorForPosition(e->pos());
        wc.select(QTextCursor::WordUnderCursor);
        QString word = wc.selectedText();
        word.replace(QChar(0x2019), QLatin1Char('\''));
        if (word.size() > 1 && typeOf(wc.block()) != BlockType::Code && !SpellChecker::instance().isCorrect(word)) {
            menu->addSection(tr("Spelling"));
            const QStringList sugg = SpellChecker::instance().suggestions(word);
            for (const QString &sgg : sugg) {
                QAction *a = menu->addAction(sgg);
                QFont f = a->font();
                f.setBold(true);
                a->setFont(f);
                connect(a, &QAction::triggered, this, [this, wc, sgg]() mutable {
                    wc.beginEditBlock();
                    wc.insertText(sgg);
                    wc.endEditBlock();
                });
            }
            if (sugg.isEmpty())
                menu->addAction(tr("No suggestions"))->setEnabled(false);
            menu->addAction(Icons::icon(QStringLiteral("plus")), tr("Add “%1” to the dictionary").arg(word), this, [this, word] {
                SpellChecker::instance().addToDictionary(word);
                m_highlighter->rehighlight();
            });
            menu->addSeparator();
        }
    }
    QAction *cut = menu->addAction(Icons::icon(QStringLiteral("close")), tr("Cut"), this, &QTextEdit::cut);
    cut->setEnabled(sel && !isReadOnly());
    cut->setShortcut(QKeySequence::Cut);
    QAction *copyA = menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Copy"), this, &QTextEdit::copy);
    copyA->setEnabled(sel);
    copyA->setShortcut(QKeySequence::Copy);
    QAction *paste = menu->addAction(Icons::icon(QStringLiteral("download")), tr("Paste"), this, &QTextEdit::paste);
    paste->setEnabled(canPaste() && !isReadOnly());
    paste->setShortcut(QKeySequence::Paste);
    menu->addSeparator();
    const QPoint global = e->globalPos();
    QAction *turn = menu->addAction(Icons::icon(QStringLiteral("refresh")), tr("Turn into…"));
    connect(turn, &QAction::triggered, this, [this, global] { showTurnIntoMenu(global); });
    QAction *shade = menu->addAction(Icons::icon(QStringLiteral("palette")), tr("Shade…"));
    connect(shade, &QAction::triggered, this, [this, global] { showShadeMenu(global); });
    menu->addSeparator();
    QAction *all = menu->addAction(tr("Select all"), this, &QTextEdit::selectAll);
    all->setShortcut(QKeySequence::SelectAll);
    menu->popup(global);
}

