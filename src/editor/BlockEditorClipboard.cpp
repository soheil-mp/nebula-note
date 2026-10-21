#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "CodeHighlighter.h"
#include "DocumentIO.h"
#include "Popups.h"
#include "core/Ids.h"
#include "core/Markdown.h"
#include "core/Settings.h"
#include "core/MediaLinks.h"
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

// ---- clipboard -----------------------------------------------------------------------------

QMimeData *BlockEditor::createMimeDataFromSelection() const
{
    // QTextEditMimeData reports a fixed format list, so build a plain
    // QMimeData that can also carry Nebula's own blocks.
    QMimeData *mime = new QMimeData;
    {
        const std::unique_ptr<QMimeData> base(QTextEdit::createMimeDataFromSelection());
        mime->setHtml(base->html());
        mime->setText(base->text());
    }
    if (m_rangeOverride.first >= 0 || m_blockSel.first >= 0) {
        // Whole blocks (block selection).
        const auto r = m_rangeOverride.first >= 0 ? m_rangeOverride : m_blockSel;
        QList<Block> blocks = DocumentIO::range(document(), r.first, r.second);
        if (blocks.isEmpty())
            return mime;
        const int minDepth = std::min_element(blocks.begin(), blocks.end(), [](const Block &a, const Block &b) { return a.depth < b.depth; })->depth;
        for (Block &b : blocks)
            b.depth -= minDepth;
        mime->setData(QString::fromLatin1(kBlocksMime), blocksToJsonString(blocks).toUtf8());
        Markdown::ExportOptions opt;
        opt.pageTitle = [this](const QString &id) { return m_ws ? m_ws->title(id) : QString(); };
        mime->setText(Markdown::fromBlocks(blocks, opt));
        return mime;
    }
    const QTextCursor c = textCursor();
    if (!c.hasSelection())
        return mime;
    const QTextDocument *doc = document();
    const int from = c.selectionStart(), to = c.selectionEnd();
    QList<Block> blocks;
    for (QTextBlock blk = doc->findBlock(from); blk.isValid() && blk.position() <= to; blk = blk.next()) {
        if (blk.position() == to && blk.position() != from)
            break;
        Block b = DocumentIO::blockAt(blk);
        if (!blockTypeIsAtomic(b.type)) {
            // Trim the partial first and last blocks to the selection.
            QList<Span> spans;
            for (auto it = blk.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                const int a = qMax(from, f.position()), e = qMin(to, f.position() + f.length());
                if (a >= e)
                    continue;
                const QTextCharFormat cf = f.charFormat();
                if (cf.objectType() == MentionObject) {
                    for (int i = a; i < e; ++i)
                        spans.append(spanOf(cf, QString(), b.type, b.checked));
                    continue;
                }
                spans.append(spanOf(cf, f.text().mid(a - f.position(), e - a), b.type, b.checked));
            }
            b.spans = normalizeSpans(spans);
        }
        blocks.append(b);
    }
    if (blocks.isEmpty())
        return mime;
    const int minDepth = std::min_element(blocks.begin(), blocks.end(), [](const Block &a, const Block &b) { return a.depth < b.depth; })->depth;
    for (Block &b : blocks)
        b.depth -= minDepth;
    mime->setData(QString::fromLatin1(kBlocksMime), blocksToJsonString(blocks).toUtf8());
    if (blocks.size() > 1) {
        Markdown::ExportOptions opt;
        opt.pageTitle = [this](const QString &id) { return m_ws ? m_ws->title(id) : QString(); };
        mime->setText(Markdown::fromBlocks(blocks, opt));
    } else {
        QString text = mime->text();
        text.replace(QChar::LineSeparator, QLatin1Char('\n'));
        mime->setText(text);
    }
    return mime;
}

bool BlockEditor::canInsertFromMimeData(const QMimeData *source) const
{
    return source->hasFormat(QString::fromLatin1(kBlocksMime)) || source->hasImage() || source->hasUrls() || source->hasText()
           || source->hasHtml();
}

void BlockEditor::insertBlocksAtCursor(const QList<Block> &input)
{
    if (input.isEmpty())
        return;
    QTextDocument *doc = document();
    EditSteps steps(doc);
    if (textCursor().hasSelection()) {
        steps.run([&](QTextCursor &) {
            QTextCursor c = textCursor();
            c.removeSelectedText();
            setTextCursor(c);
        });
    }
    QTextCursor c = textCursor();
    QTextBlock blk = c.block();
    const Block here = blockProps(blk);
    QList<Block> blocks = input;
    renewBlockIds(blocks);
    for (Block &b : blocks)
        b.depth = qMin(10, b.depth + here.depth);

    // Table cells take text only; a single run of text goes inline anywhere.
    const bool inCell = DocumentIO::isSimpleTable(DocumentIO::tableOf(blk));
    if (inCell || (blocks.size() == 1 && isTextType(blocks.first().type) && isTextType(here.type))) {
        steps.run([&](QTextCursor &) {
            QTextCursor ic = textCursor();
            bool firstBlock = true;
            for (const Block &b : blocks) {
                if (!isTextType(b.type) && !inCell)
                    continue;
                if (!firstBlock)
                    ic.insertText(QString(QChar::LineSeparator), baseCharFormat(here.type, m_ctx.style));
                firstBlock = false;
                for (const Span &s : b.spans) {
                    if (s.isObject())
                        ic.insertText(QString(QChar::ObjectReplacementCharacter), objectSpanFormat(s, m_ctx.style));
                    else
                        ic.insertText(QString(s.text).replace(QLatin1Char('\n'), QChar::LineSeparator),
                                      spanFormat(s, here.type, m_ctx.style, here.checked, here.color));
                }
            }
            setTextCursor(ic);
        });
        return;
    }
    // Columns don't nest: their contents come along instead.
    if (!DocumentIO::containerOf(blk).isRoot()) {
        QList<Block> flat;
        for (const Block &b : blocks) {
            if (b.type != BlockType::ColumnList) {
                flat.append(b);
                continue;
            }
            for (const QList<Block> &col : b.columns)
                for (Block x : col) {
                    x.depth += b.depth;
                    flat.append(x);
                }
        }
        blocks = flat;
    }
    QTextBlock last;
    steps.run([&](QTextCursor &ec) {
        if (blk.text().isEmpty() && here.type == BlockType::Paragraph) {
            // Replace the empty block.
            applyBlockProps(blk, Block{}, m_ctx.style);
            last = DocumentIO::fillEmpty(ec, blk, blocks, m_ctx.style);
            return;
        }
        if (!c.atBlockEnd() && !c.atBlockStart()) {
            QTextCursor split = c;
            split.insertBlock();
            blk = split.block().previous();
        } else if (c.atBlockStart() && !blk.text().isEmpty()) {
            // Insert above the block under the cursor.
            last = DocumentIO::insertBefore(ec, blk, blocks, m_ctx.style);
            return;
        }
        last = DocumentIO::insertAfter(ec, blk, blocks, m_ctx.style);
    });
    updateVisibility();
    m_highlighter->rehighlight();
    if (last.isValid()) {
        QTextCursor m(doc);
        m.setPosition(last.position() + last.length() - 1);
        setTextCursor(m);
    }
}

void BlockEditor::insertFromMimeData(const QMimeData *source)
{
    if (isReadOnly())
        return;
    m_normalizeFull = true; // pasted blocks may repeat ids from elsewhere on the page
    const QTextBlock blk = textCursor().block();
    const BlockType type = typeOf(blk);
    if (type == BlockType::Code) {
        QString text = source->text();
        text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        text.replace(QLatin1Char('\n'), QChar::LineSeparator);
        textCursor().insertText(text);
        return;
    }
    if (source->hasFormat(QString::fromLatin1(kBlocksMime))) {
        insertBlocksAtCursor(blocksFromJsonString(QString::fromUtf8(source->data(QString::fromLatin1(kBlocksMime)))));
        return;
    }
    if (source->hasImage() && m_ws) {
        const QImage img = qvariant_cast<QImage>(source->imageData());
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        const QString media = m_ws->importMediaData(png, QStringLiteral("png"));
        if (!media.isEmpty()) {
            Block b;
            b.type = BlockType::Image;
            b.media = media;
            insertBlocksAtCursor({b});
        }
        return;
    }
    if (source->hasUrls()) {
        QList<Block> images;
        for (const QUrl &u : source->urls()) {
            if (!u.isLocalFile())
                continue;
            const QString path = u.toLocalFile();
            if (!QImageReader::imageFormat(path).isEmpty() && m_ws) {
                Block b;
                b.type = BlockType::Image;
                b.media = m_ws->importMedia(path);
                if (!b.media.isEmpty())
                    images.append(b);
            }
        }
        if (!images.isEmpty()) {
            insertBlocksAtCursor(images);
            return;
        }
    }
    const QString text = source->text();
    // A link pasted over selected text links it.
    static const QRegularExpression url(QStringLiteral("^(https?://|mailto:)\\S+$"));
    if (textCursor().hasSelection() && url.match(text.trimmed()).hasMatch()) {
        setLink(text.trimmed());
        return;
    }
    // A link to a Nebula page (or a block in it) becomes a mention.
    static const QRegularExpression own(QStringLiteral("^nebula://page/([A-Za-z0-9_-]+)(?:#([A-Za-z0-9_-]+))?$"));
    if (const auto m = own.match(text.trimmed()); m.hasMatch() && m_ws && m_ws->exists(m.captured(1))) {
        QTextCursor c = textCursor();
        c.insertText(QString(QChar::ObjectReplacementCharacter), mentionFormat(m.captured(1), m_ctx.style, m.captured(2)));
        const Block bp = blockProps(c.block());
        c.insertText(QStringLiteral(" "), baseCharFormat(bp.type, m_ctx.style, bp.checked, bp.color));
        setTextCursor(c);
        m_linkedPages.insert(m.captured(1));
        return;
    }
    // A bare link on an empty line: offer a bookmark or an embed.
    if (url.match(text.trimmed()).hasMatch() && !text.trimmed().startsWith(QLatin1String("mailto:"))) {
        QTextBlock here = textCursor().block();
        if (here.text().isEmpty() && typeOf(here) == BlockType::Paragraph && !DocumentIO::isSimpleTable(DocumentIO::tableOf(here))) {
            QTextCursor c = textCursor();
            Span s;
            s.text = text.trimmed();
            s.link = s.text;
            c.insertText(s.text, spanFormat(s, BlockType::Paragraph, m_ctx.style));
            setTextCursor(c);
            setCurrentCharFormat(baseCharFormat(BlockType::Paragraph, m_ctx.style));
            if (MediaLinks::isVideoLink(s.text)) {
                // A video link embeds the video right away.
                m_pasteBlock = here.blockNumber();
                m_pasteUrl = s.text;
                runPasteChoice(QStringLiteral("embed"));
                return;
            }
            showPasteMenu(here.blockNumber(), s.text);
            return;
        }
    }
    if (source->hasHtml() && !source->html().trimmed().isEmpty()) {
        // Rich text from other apps: keep the structure, drop the styling.
        QTextDocument html;
        html.setHtml(source->html());
        QList<Block> blocks;
        for (QTextBlock b = html.begin(); b.isValid(); b = b.next()) {
            Block out;
            const QTextBlockFormat bf = b.blockFormat();
            const int level = bf.headingLevel();
            if (level == 1)
                out.type = BlockType::Heading1;
            else if (level == 2)
                out.type = BlockType::Heading2;
            else if (level >= 3)
                out.type = BlockType::Heading3;
            if (QTextList *list = b.textList()) {
                const auto style = list->format().style();
                out.type = (style == QTextListFormat::ListDecimal || style == QTextListFormat::ListLowerAlpha
                            || style == QTextListFormat::ListUpperAlpha || style == QTextListFormat::ListLowerRoman)
                               ? BlockType::Numbered
                               : BlockType::Bullet;
                if (bf.marker() == QTextBlockFormat::MarkerType::Checked || bf.marker() == QTextBlockFormat::MarkerType::Unchecked) {
                    out.type = BlockType::Todo;
                    out.checked = bf.marker() == QTextBlockFormat::MarkerType::Checked;
                }
                out.depth = qMax(0, list->format().indent() - 1);
            }
            if (bf.nonBreakableLines())
                out.type = BlockType::Code;
            for (auto it = b.begin(); !it.atEnd(); ++it) {
                const QTextFragment f = it.fragment();
                const QTextCharFormat cf = f.charFormat();
                Span s;
                s.text = f.text();
                s.text.remove(QChar::ObjectReplacementCharacter);
                if (s.text.isEmpty())
                    continue;
                s.bold = cf.fontWeight() >= QFont::Bold && out.type == BlockType::Paragraph;
                s.italic = cf.fontItalic();
                s.underline = cf.fontUnderline() && !cf.isAnchor();
                s.strike = cf.fontStrikeOut();
                s.code = cf.fontFixedPitch() && out.type != BlockType::Code;
                if (cf.isAnchor())
                    s.link = cf.anchorHref();
                out.spans.append(s);
            }
            out.spans = normalizeSpans(out.spans);
            blocks.append(out);
        }
        // Drop empty paragraphs between blocks (HTML spacing).
        blocks.removeIf([](const Block &b) { return b.type == BlockType::Paragraph && b.spans.isEmpty(); });
        if (!blocks.isEmpty()) {
            insertBlocksAtCursor(blocks);
            return;
        }
    }
    if (text.contains(QLatin1Char('\n'))) {
        Markdown::ImportOptions opt;
        const QList<Block> blocks = Markdown::toBlocks(text, opt);
        if (!blocks.isEmpty()) {
            insertBlocksAtCursor(blocks);
            return;
        }
    }
    QTextCursor c = textCursor();
    const Block bp = blockProps(c.block());
    Span s;
    s.text = text;
    // Bare URLs become links.
    if (url.match(text.trimmed()).hasMatch())
        s.link = text.trimmed();
    c.insertText(text, spanFormat(s, bp.type, m_ctx.style, bp.checked, bp.color));
    setTextCursor(c);
    setCurrentCharFormat(baseCharFormat(bp.type, m_ctx.style, bp.checked, bp.color));
}

void BlockEditor::dragEnterEvent(QDragEnterEvent *e)
{
    QTextEdit::dragEnterEvent(e);
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void BlockEditor::dragMoveEvent(QDragMoveEvent *e)
{
    QTextEdit::dragMoveEvent(e);
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void BlockEditor::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasUrls() && !isReadOnly()) {
        setTextCursor(cursorForPosition(e->position().toPoint()));
        insertFromMimeData(e->mimeData());
        e->acceptProposedAction();
        return;
    }
    QTextEdit::dropEvent(e);
}

