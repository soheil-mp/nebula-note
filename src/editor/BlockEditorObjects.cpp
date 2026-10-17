// Object blocks and inline objects: equations, tables of contents,
// breadcrumbs, buttons, bookmarks, embeds, files, captions, date mentions
// and the paste-a-link menu.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "DocumentIO.h"
#include "LinkPreview.h"
#include "ObjectPopovers.h"
#include "Popups.h"
#include "core/DateParse.h"
#include "core/Markdown.h"
#include "core/Workspace.h"
#include "ui/DatePopover.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMessageBox>
#include <QPointer>
#include <QScrollBar>
#include <QTextBlock>
#include <QUrl>

using namespace BlockFormat;
using namespace BlockEditorPrivate;

// ---- geometry -------------------------------------------------------------------------

QRectF BlockEditor::objectRect(const QTextBlock &block) const
{
    if (!blockTypeIsAtomic(typeOf(block)))
        return {};
    const QTextFormat f = [&] {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.objectType() >= QTextFormat::UserObject)
                return QTextFormat(cf);
        }
        return QTextFormat();
    }();
    if (!f.isValid())
        return {};
    const QSizeF size = m_objects->intrinsicSize(const_cast<QTextDocument *>(document()), block.position(), f);
    const QRectF br = blockRect(block);
    double x = textLeft(block);
    if (typeOf(block) == BlockType::Image) {
        const QString align = block.blockFormat().property(AlignProp).toString();
        const double right = blockRight(block);
        if (align == QLatin1String("right"))
            x = right - size.width();
        else if (align != QLatin1String("left") && align != QLatin1String("full"))
            x = textLeft(block) + (right - textLeft(block) - size.width()) / 2;
    }
    return QRectF(x, br.top(), size.width(), size.height());
}

int BlockEditor::objectPartAt(const QTextBlock &block, const QPointF &pos) const
{
    const QRectF r = objectRect(block);
    if (!r.contains(pos))
        return -1;
    const Block b = blockProps(block);
    switch (b.type) {
    case BlockType::TableOfContents: {
        const int i = int((pos.y() - r.top() - TextObjects::tocTop(m_ctx.style)) / TextObjects::tocRowHeight(m_ctx.style));
        return i >= 0 && i < TextObjects::headings(const_cast<QTextDocument *>(document())).size() ? i : -1;
    }
    case BlockType::Breadcrumb: {
        const auto parts = TextObjects::breadcrumbParts(r, m_pageId, m_ctx.style);
        for (int i = 0; i < parts.size(); ++i)
            if (parts[i].first.contains(pos))
                return i;
        return -1;
    }
    case BlockType::Button: {
        const QString label = b.data.value(QStringLiteral("label")).toString();
        return TextObjects::buttonRect(r, label.isEmpty() ? tr("New button") : label, m_ctx.style).contains(pos) ? 1 : -1;
    }
    default:
        return 0;
    }
}

QPoint BlockEditor::objectPopupPos(const QTextBlock &block) const
{
    const QRectF r = objectRect(block);
    return viewport()->mapToGlobal(QPoint(int(r.isValid() ? r.left() : textLeft(block)), int((r.isValid() ? r.bottom() : blockRect(block).bottom()) + 6)));
}

// ---- inserting ----------------------------------------------------------------------------

void BlockEditor::insertObjectBlock(const Block &input, bool configure)
{
    Block b = input;
    QTextBlock blk = textCursor().block();
    b.depth = depthOf(blk);
    if (b.id.isEmpty())
        b.id = Block::newId();
    if (DocumentIO::isSimpleTable(DocumentIO::tableOf(blk))) {
        emit toast(tr("Table cells hold text only."));
        return;
    }
    int number;
    if (blk.text().isEmpty() && typeOf(blk) == BlockType::Paragraph) {
        EditSteps(document()).run([&](QTextCursor &) { setBlockProps(blk, b); });
        number = blk.blockNumber();
        // An object last on the page gets a paragraph after it to type in.
        if (!nextInContainer(blk).isValid() && DocumentIO::containerOf(blk).isRoot()) {
            Block p;
            p.depth = b.depth;
            insertNewBlockAfter(blk, p, false);
        }
    } else {
        insertNewBlockAfter(blk, b, false);
        number = blk.blockNumber() + 1;
    }
    QTextBlock inserted = document()->findBlockByNumber(number);
    // Find it by id in case numbering moved.
    if (idOf(inserted) != b.id)
        inserted = blockById(b.id);
    if (!inserted.isValid())
        return;
    QTextCursor c(inserted);
    setTextCursor(c);
    updateVisibility();
    scheduleNormalize();
    if (!configure)
        return;
    const QPoint where = objectPopupPos(inserted);
    switch (b.type) {
    case BlockType::Equation:
        editBlockEquation(inserted.blockNumber());
        break;
    case BlockType::Button:
        configureButton(inserted.blockNumber(), where);
        break;
    case BlockType::Bookmark:
    case BlockType::Embed:
        if (b.url.isEmpty())
            askForLink(inserted.blockNumber(), where);
        break;
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf:
        if (b.media.isEmpty() && b.url.isEmpty())
            chooseFile(inserted.blockNumber());
        break;
    default:
        break;
    }
}

void BlockEditor::insertEquation(bool inlineEquation)
{
    if (!inlineEquation) {
        Block b;
        b.type = BlockType::Equation;
        insertObjectBlock(b);
        return;
    }
    // The selection (if any) becomes the TeX source.
    QTextCursor c = textCursor();
    QString tex = c.selectedText();
    tex.replace(QChar::ParagraphSeparator, QLatin1Char(' ')).replace(QChar::LineSeparator, QLatin1Char(' '));
    tex.remove(QChar::ObjectReplacementCharacter);
    c.beginEditBlock();
    c.removeSelectedText();
    const int pos = c.position();
    c.insertText(QString(QChar::ObjectReplacementCharacter), inlineEquationFormat(tex.trimmed()));
    c.endEditBlock();
    setTextCursor(c);
    if (tex.trimmed().isEmpty())
        editInlineEquation(pos);
}

void BlockEditor::insertDateMention(const QDateTime &when, bool hasTime, bool remind)
{
    QTextCursor c = textCursor();
    const QString iso = when.toString(hasTime ? QStringLiteral("yyyy-MM-ddTHH:mm") : QStringLiteral("yyyy-MM-dd"));
    c.insertText(QString(QChar::ObjectReplacementCharacter), dateFormat(iso, remind));
    const Block bp = blockProps(c.block());
    c.insertText(QStringLiteral(" "), baseCharFormat(bp.type, m_ctx.style, bp.checked, bp.color));
    setTextCursor(c);
}

// ---- editing --------------------------------------------------------------------------------

void BlockEditor::setBlockData(int blockNumber, const std::function<void(Block &)> &change)
{
    QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    Block props = blockProps(blk);
    change(props);
    QTextCursor c(blk);
    c.beginEditBlock();
    setBlockProps(blk, props);
    c.endEditBlock();
    // Objects size from their data: lay the block out again.
    document()->markContentsDirty(blk.position(), blk.length());
    scheduleNormalize();
    scheduleSave();
    viewport()->update();
}

void BlockEditor::editBlockEquation(int blockNumber)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid() || isReadOnly())
        return;
    const QString id = idOf(blk);
    auto *pop = new EquationPopover(window(), dataOf(blk).value(QStringLiteral("expr")).toString(), true);
    connect(pop, &EquationPopover::submitted, this, [this, id](const QString &tex) {
        const QTextBlock b = blockById(id);
        if (b.isValid())
            setBlockData(b.blockNumber(), [&](Block &p) { p.data.insert(QStringLiteral("expr"), tex); });
        setFocus();
    });
    pop->popupAt(objectPopupPos(blk));
}

void BlockEditor::editInlineEquation(int position)
{
    if (isReadOnly())
        return;
    QTextCursor c(document());
    c.setPosition(position);
    c.setPosition(position + 1, QTextCursor::KeepAnchor);
    if (c.charFormat().objectType() != InlineEquationObject)
        return;
    const QRect caret = cursorRect(c);
    auto *pop = new EquationPopover(window(), c.charFormat().property(EquationProp).toString(), false);
    QPointer<BlockEditor> self(this);
    // Track the position across edits with a cursor.
    auto anchor = std::make_shared<QTextCursor>(document());
    anchor->setPosition(position);
    connect(pop, &EquationPopover::submitted, this, [this, anchor](const QString &tex) {
        QTextCursor ec(document());
        ec.setPosition(anchor->position());
        ec.setPosition(anchor->position() + 1, QTextCursor::KeepAnchor);
        if (ec.charFormat().objectType() != InlineEquationObject)
            return;
        if (tex.isEmpty()) {
            ec.removeSelectedText();
        } else {
            QTextCharFormat f = ec.charFormat();
            f.setProperty(EquationProp, tex);
            ec.setCharFormat(f);
        }
        document()->markContentsDirty(ec.block().position(), ec.block().length());
        setFocus();
        QTextCursor after(document());
        after.setPosition(qMin(anchor->position() + 1, document()->characterCount() - 1));
        setTextCursor(after);
    });
    pop->popupAt(viewport()->mapToGlobal(caret.bottomLeft() + QPoint(0, 8)));
}

void BlockEditor::editDate(int position, const QPoint &globalPos)
{
    if (isReadOnly())
        return;
    QTextCursor c(document());
    c.setPosition(position);
    c.setPosition(position + 1, QTextCursor::KeepAnchor);
    if (c.charFormat().objectType() != DateObject)
        return;
    QString value = c.charFormat().property(DateProp).toString();
    if (c.charFormat().property(RemindProp).toBool())
        value += QStringLiteral("#r");
    DatePopover::Options opt;
    opt.range = false;
    auto *pop = new DatePopover(window(), value, opt);
    auto anchor = std::make_shared<QTextCursor>(document());
    anchor->setPosition(position);
    connect(pop, &DatePopover::changed, this, [this, anchor](const QString &v) {
        QTextCursor ec(document());
        ec.setPosition(anchor->position());
        ec.setPosition(anchor->position() + 1, QTextCursor::KeepAnchor);
        if (ec.charFormat().objectType() != DateObject)
            return;
        if (v.isEmpty()) {
            ec.removeSelectedText();
            return;
        }
        QTextCharFormat f = ec.charFormat();
        f.setProperty(DateProp, v.section(QLatin1Char('#'), 0, 0));
        f.setProperty(RemindProp, v.endsWith(QLatin1String("#r")));
        ec.setCharFormat(f);
        document()->markContentsDirty(ec.block().position(), ec.block().length());
    });
    pop->popupAt(globalPos);
}

void BlockEditor::configureButton(int blockNumber, const QPoint &globalPos)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid() || isReadOnly())
        return;
    const QString id = idOf(blk);
    auto *pop = new ButtonPopover(window(), dataOf(blk));
    connect(pop, &ButtonPopover::submitted, this, [this, id](const QJsonObject &data) {
        const QTextBlock b = blockById(id);
        if (b.isValid())
            setBlockData(b.blockNumber(), [&](Block &p) { p.data = data; });
    });
    pop->popupAt(globalPos);
}

void BlockEditor::runButton(int blockNumber)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid() || !m_ws)
        return;
    const QJsonObject data = dataOf(blk);
    const QJsonArray actions = data.value(QStringLiteral("actions")).toArray();
    if (actions.isEmpty()) {
        configureButton(blockNumber, objectPopupPos(blk));
        return;
    }
    const QString confirm = data.value(QStringLiteral("confirm")).toString();
    if (!confirm.isEmpty()
        && QMessageBox::question(window(), data.value(QStringLiteral("label")).toString(), confirm, QMessageBox::Yes | QMessageBox::Cancel)
               != QMessageBox::Yes)
        return;
    const QString id = idOf(blk);
    for (const QJsonValue &v : actions) {
        const QJsonObject a = v.toObject();
        const QString type = a.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("insert")) {
            QList<Block> blocks = Markdown::toBlocks(a.value(QStringLiteral("markdown")).toString());
            if (blocks.isEmpty())
                continue;
            renewBlockIds(blocks);
            const QTextBlock b = blockById(id);
            if (!b.isValid())
                continue;
            for (Block &x : blocks)
                x.depth += depthOf(b);
            // Below the button and anything inserted by earlier clicks.
            EditSteps(document()).run([&](QTextCursor &c) { DocumentIO::insertAfter(c, b, blocks, m_ctx.style); });
            updateVisibility();
            scheduleNormalize();
        } else if (type == QLatin1String("add")) {
            const QString db = a.value(QStringLiteral("db")).toString();
            if (!m_ws->exists(db)) {
                emit toast(tr("That database no longer exists."));
                continue;
            }
            QString title = a.value(QStringLiteral("title")).toString();
            title.replace(QStringLiteral("@today"), QLocale(QLocale::English).toString(QDate::currentDate(), QStringLiteral("MMMM d, yyyy")));
            flush();
            m_ws->createRow(db, title);
            emit toast(tr("Added a page to %1").arg(m_ws->title(db)));
        } else if (type == QLatin1String("page")) {
            const QString page = a.value(QStringLiteral("page")).toString();
            if (m_ws->exists(page)) {
                flush();
                emit openPage(page);
            }
        } else if (type == QLatin1String("url")) {
            QString url = a.value(QStringLiteral("url")).toString();
            if (!url.contains(QStringLiteral("://")))
                url.prepend(QStringLiteral("https://"));
            QDesktopServices::openUrl(QUrl(url));
        }
    }
}

void BlockEditor::askForLink(int blockNumber, const QPoint &globalPos)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid() || isReadOnly())
        return;
    const QString id = idOf(blk);
    const bool embed = typeOf(blk) == BlockType::Embed;
    auto *pop = new LinePopover(window(), embed ? tr("Embed a link") : tr("Web bookmark"),
                                embed ? tr("Paste a YouTube, Figma, Maps… link") : tr("Paste a link"), blockProps(blk).url,
                                embed ? tr("Embed") : tr("Create bookmark"));
    connect(pop, &LinePopover::submitted, this, [this, id, embed](QString url) {
        if (url.isEmpty())
            return;
        if (!url.contains(QStringLiteral("://")))
            url.prepend(QStringLiteral("https://"));
        const QTextBlock b = blockById(id);
        if (!b.isValid())
            return;
        setBlockData(b.blockNumber(), [&](Block &p) {
            p.url = url;
            if (embed)
                p.data.insert(QStringLiteral("provider"), LinkPreview::providerOf(url));
        });
        fetchPreview(id, url);
    });
    pop->popupAt(globalPos);
}

void BlockEditor::fetchPreview(const QString &blockId, const QString &url)
{
    QPointer<BlockEditor> self(this);
    const QString page = m_pageId;
    LinkPreview::instance()->fetch(url, [self, blockId, url, page](const LinkPreview::Info &info) {
        if (!self || self->m_pageId != page || !info.ok)
            return;
        const QTextBlock b = self->blockById(blockId);
        if (!b.isValid() || blockProps(b).url != url)
            return;
        self->setBlockData(b.blockNumber(), [&](Block &p) {
            p.data.insert(QStringLiteral("title"), info.title);
            if (!info.description.isEmpty())
                p.data.insert(QStringLiteral("desc"), info.description);
            if (!info.site.isEmpty())
                p.data.insert(QStringLiteral("site"), info.site);
            if (!info.imageMedia.isEmpty())
                p.data.insert(QStringLiteral("image"), info.imageMedia);
        });
    });
}

void BlockEditor::chooseFile(int blockNumber)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid() || !m_ws || isReadOnly())
        return;
    const BlockType t = typeOf(blk);
    const QString filter = t == BlockType::Video   ? tr("Videos (*.mp4 *.webm *.mov *.mkv *.avi);;All files (*)")
                           : t == BlockType::Audio ? tr("Audio (*.mp3 *.wav *.ogg *.flac *.m4a *.opus);;All files (*)")
                           : t == BlockType::Pdf   ? tr("PDF documents (*.pdf)")
                                                   : tr("All files (*)");
    const QString id = idOf(blk);
    const QString path = QFileDialog::getOpenFileName(this, tr("Upload a file"), QString(), filter);
    if (path.isEmpty())
        return;
    const QString media = m_ws->importMedia(path);
    if (media.isEmpty()) {
        emit toast(tr("That file couldn't be read."));
        return;
    }
    const QTextBlock b = blockById(id);
    if (!b.isValid())
        return;
    setBlockData(b.blockNumber(), [&](Block &p) {
        p.media = media;
        p.url.clear();
        p.data.insert(QStringLiteral("name"), QFileInfo(path).fileName());
        p.data.insert(QStringLiteral("size"), double(QFileInfo(path).size()));
    });
}

void BlockEditor::openObject(int blockNumber)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    const Block b = blockProps(blk);
    if (!b.media.isEmpty() && m_ws)
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_ws->mediaPath(b.media)));
    else if (!b.url.isEmpty())
        QDesktopServices::openUrl(QUrl(b.url));
}

void BlockEditor::editCaption(int blockNumber, const QPoint &globalPos)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid() || isReadOnly())
        return;
    QString text;
    for (const Span &s : captionOf(blk))
        if (!s.isObject())
            text += s.text;
    const QString id = idOf(blk);
    auto *pop = new LinePopover(window(), tr("Caption"), tr("Write a caption…"), text);
    connect(pop, &LinePopover::submitted, this, [this, id](const QString &caption) {
        const QTextBlock b = blockById(id);
        if (!b.isValid())
            return;
        setBlockData(b.blockNumber(), [&](Block &p) {
            p.caption.clear();
            if (!caption.isEmpty())
                p.caption.append(Span{caption});
        });
    });
    pop->popupAt(globalPos);
}

void BlockEditor::activateObject(const QTextBlock &block, int part, const QPoint &globalPos)
{
    const Block b = blockProps(block);
    switch (b.type) {
    case BlockType::Equation:
        editBlockEquation(block.blockNumber());
        break;
    case BlockType::TableOfContents: {
        const QList<TocEntry> entries = TextObjects::headings(document());
        if (part >= 0 && part < entries.size())
            scrollToBlock(entries[part].block);
        break;
    }
    case BlockType::Breadcrumb: {
        const auto parts = TextObjects::breadcrumbParts(objectRect(block), m_pageId, m_ctx.style);
        if (part >= 0 && part < parts.size() && parts[part].second != m_pageId) {
            flush();
            emit openPage(parts[part].second);
        }
        break;
    }
    case BlockType::Button:
        if (part == 1)
            runButton(block.blockNumber());
        break;
    case BlockType::Bookmark:
    case BlockType::Embed:
        if (b.url.isEmpty())
            askForLink(block.blockNumber(), globalPos);
        else
            QDesktopServices::openUrl(QUrl(b.url));
        break;
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf:
        if (b.media.isEmpty() && b.url.isEmpty())
            chooseFile(block.blockNumber());
        else
            openObject(block.blockNumber());
        break;
    case BlockType::Database:
        if (m_ws && m_ws->exists(b.pageId)) {
            flush();
            emit openPage(b.pageId);
        }
        break;
    default:
        break;
    }
}

// ---- navigation ----------------------------------------------------------------------------

QTextBlock BlockEditor::blockById(const QString &id) const
{
    if (id.isEmpty())
        return {};
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next())
        if (idOf(b) == id)
            return b;
    return {};
}

void BlockEditor::scrollToBlock(int blockNumber, bool flash)
{
    QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    // Unfold the toggles hiding it.
    if (!blk.isVisible()) {
        for (QTextBlock b = blk.previous(); b.isValid(); b = b.previous()) {
            if (foldsChildren(b) && isCollapsed(b) && subtreeEnd(b) >= blockNumber) {
                setCollapsed(b, false);
            }
        }
        updateVisibility();
    }
    const QRectF r = blockRect(blk);
    if (m_scrollArea && m_scrollArea->widget()) {
        const QPoint top = viewport()->mapTo(m_scrollArea->widget(), r.topLeft().toPoint());
        m_scrollArea->verticalScrollBar()->setValue(qMax(0, top.y() - 80));
    }
    QTextCursor c(blk);
    c.movePosition(QTextCursor::EndOfBlock);
    setTextCursor(c);
    if (flash) {
        Flash f{blockNumber, {}};
        f.clock.start();
        m_flashes.append(f);
        startAnimTick();
    }
}

bool BlockEditor::scrollToBlockId(const QString &blockId, bool flash)
{
    const QTextBlock b = blockById(blockId);
    if (!b.isValid())
        return false;
    scrollToBlock(b.blockNumber(), flash);
    return true;
}

// ---- pasting links ---------------------------------------------------------------------------

void BlockEditor::showPasteMenu(int blockNumber, const QString &url)
{
    QWidget *host = m_host ? m_host.data() : window();
    if (!m_pasteMenu) {
        m_pasteMenu = new CommandPopup(host);
        m_pasteMenu->setLargeIcons(false);
        m_pasteMenu->setFixedCardWidth(260);
        connect(m_pasteMenu, &CommandPopup::activated, this, &BlockEditor::runPasteChoice);
    }
    m_pasteBlock = blockNumber;
    m_pasteUrl = url;
    QList<MenuItem> items;
    auto add = [&](const QString &id, const QString &icon, const QString &title) {
        MenuItem m;
        m.id = id;
        m.icon = icon;
        m.title = title;
        items.append(m);
    };
    add(QStringLiteral("link"), QStringLiteral("link"), tr("Keep as link"));
    add(QStringLiteral("bookmark"), QStringLiteral("bookmark"), tr("Create bookmark"));
    add(QStringLiteral("embed"), QStringLiteral("embed"), tr("Create embed"));
    m_pasteMenu->setItems(items, false);
    const QRect caret = cursorRect();
    m_pasteMenu->popup(QRect(viewport()->mapTo(host, caret.topLeft()), caret.size()));
}

void BlockEditor::runPasteChoice(const QString &id)
{
    if (m_pasteMenu)
        m_pasteMenu->dismiss();
    const QTextBlock blk = document()->findBlockByNumber(m_pasteBlock);
    const QString url = m_pasteUrl;
    m_pasteBlock = -1;
    if (!blk.isValid() || id == QLatin1String("link"))
        return;
    // The pasted link was the block's only content: the block becomes the card.
    QString text = blk.text().trimmed();
    if (text != url)
        return;
    Block b;
    b.type = id == QLatin1String("embed") ? BlockType::Embed : BlockType::Bookmark;
    b.url = url;
    b.depth = depthOf(blk);
    b.id = idOf(blk);
    if (b.type == BlockType::Embed)
        b.data.insert(QStringLiteral("provider"), LinkPreview::providerOf(url));
    QTextCursor c(blk);
    c.beginEditBlock();
    setBlockProps(blk, b);
    c.endEditBlock();
    scheduleNormalize();
    fetchPreview(b.id, url);
}
