// Structure of the page body: normalisation, folding, geometry, and moving
// blocks — all aware of containers (the root and table cells), so column
// lists and simple tables behave like Notion's.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "CodeHighlighter.h"
#include "DocumentIO.h"
#include "core/Ids.h"
#include "ui/Mermaid.h"

#include <QAbstractTextDocumentLayout>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextTable>

#include <climits>
#include <functional>

using namespace BlockFormat;
using namespace BlockEditorPrivate;
using DocumentIO::Container;
using DocumentIO::Item;

namespace
{
// The block type a fragment's look came from (used when merged text needs
// restyling into a different block type).
BlockType guessOrigin(const QTextCharFormat &cf, const Style &style)
{
    const double px = cf.property(QTextFormat::FontPixelSize).toDouble();
    auto near = [&](double v) { return qAbs(px - v) < 0.6; };
    if (cf.property(CodeProp).toBool())
        return BlockType::Paragraph;
    for (BlockType t : {BlockType::Title, BlockType::Heading1, BlockType::Heading2, BlockType::Heading3, BlockType::Heading4})
        if (near(headingSize(t, style)))
            return t;
    return BlockType::Paragraph;
}

int tableDepth(const QTextTable *t)
{
    return t->format().property(DepthProp).toInt();
}

double toggleRoom(const QTextBlock &block, const Style &s)
{
    return blockTypeIsHeading(typeOf(block)) && toggleableOf(block) ? s.indent() : 0;
}

// The item after the one ending at block number `last`, in container `c`.
Item nextItem(const QTextDocument *doc, int last, const Container &c)
{
    const QTextBlock b = doc->findBlockByNumber(last + 1);
    if (!b.isValid())
        return {};
    if (DocumentIO::containerOf(b) == c)
        return {b, nullptr, b.blockNumber(), b.blockNumber()};
    for (QTextTable *t = DocumentIO::tableOf(b); t; t = qobject_cast<QTextTable *>(t->parentFrame())) {
        if (DocumentIO::containerOf(doc->findBlock(t->firstPosition() - 1)) == c && DocumentIO::firstBlockNumber(t) == last + 1)
            return {QTextBlock(), t, DocumentIO::firstBlockNumber(t), DocumentIO::lastBlockNumber(t)};
    }
    return {};
}

// The item before the one starting at block number `first`, in container `c`.
Item prevItem(const QTextDocument *doc, int first, const Container &c)
{
    const QTextBlock b = doc->findBlockByNumber(first - 1);
    if (!b.isValid())
        return {};
    if (DocumentIO::containerOf(b) == c)
        return {b, nullptr, b.blockNumber(), b.blockNumber()};
    for (QTextTable *t = DocumentIO::tableOf(b); t; t = qobject_cast<QTextTable *>(t->parentFrame())) {
        if (DocumentIO::containerOf(doc->findBlock(t->firstPosition() - 1)) == c && DocumentIO::lastBlockNumber(t) == first - 1)
            return {QTextBlock(), t, DocumentIO::firstBlockNumber(t), DocumentIO::lastBlockNumber(t)};
    }
    return {};
}

int depthOfItem(const Item &item)
{
    return item.isTable() ? tableDepth(item.table) : depthOf(item.block);
}

// The container a block range lives in: its first block's, or the parent of
// a table the range starts with and covers.
Container rangeContainer(const QTextDocument *doc, int first, int last)
{
    const QTextBlock b = doc->findBlockByNumber(first);
    if (QTextTable *whole = DocumentIO::wholeTableAt(b, last))
        return DocumentIO::containerOf(doc->findBlock(whole->firstPosition() - 1));
    return DocumentIO::containerOf(b);
}

// The containers around a block, innermost first, each with the item there.
QList<QPair<Container, Item>> containerChain(const QTextBlock &block)
{
    QList<QPair<Container, Item>> out;
    Container c = DocumentIO::containerOf(block);
    Item item{block, nullptr, block.blockNumber(), block.blockNumber()};
    const QTextDocument *doc = block.document();
    for (;;) {
        out.append({c, item});
        if (c.isRoot())
            break;
        QTextTable *t = const_cast<QTextTable *>(c.table);
        item = Item{QTextBlock(), t, DocumentIO::firstBlockNumber(t), DocumentIO::lastBlockNumber(t)};
        c = DocumentIO::containerOf(doc->findBlock(t->firstPosition() - 1));
    }
    return out;
}

QList<QTextTable *> allTables(QTextFrame *frame)
{
    QList<QTextTable *> out;
    for (QTextFrame *child : frame->childFrames()) {
        if (auto *t = qobject_cast<QTextTable *>(child))
            out.append(t);
        out += allTables(child);
    }
    return out;
}
} // namespace

// ---- normalisation -------------------------------------------------------------------

void BlockEditor::scheduleNormalize()
{
    m_normalizeTimer.start();
}

void BlockEditor::normalize()
{
    if (m_loading)
        return;
    QTextDocument *doc = document();
    const Style &s = m_ctx.style;
    const double k = s.size / 16.0;
    struct Fix {
        int block;
        enum Kind { Margins, Restyle, ToParagraph, Split, Id, CellParagraph } kind;
        double top = 0, left = 0, right = 0;
        int splitAt = -1;
        QString id = {};
        double bottom = 0;
    };
    struct TableFix {
        QTextTable *table;
        double top;
        double left;
        QString id = {};
        double bottom = -1; // the next block's spacing (Qt ignores its top margin)
        bool cells = false; // re-apply cell paddings and backgrounds
    };
    QList<Fix> fixes;
    QList<TableFix> tableFixes;
    QSet<QString> seen;
    auto claimId = [&](const QString &id) {
        if (!id.isEmpty() && !seen.contains(id)) {
            seen.insert(id);
            return QString();
        }
        QString fresh = Block::newId();
        seen.insert(fresh);
        return fresh;
    };

    std::function<void(const Container &)> walk = [&](const Container &container) {
        const bool simpleCell = container.table && DocumentIO::isSimpleTable(container.table);
        const QList<Item> items = DocumentIO::itemsOf(doc, container);
        struct Open {
            int depth;
            BlockType type;
            double childLeft;
            double childRight;
            bool children = false;
        };
        QList<Open> stack;
        BlockType prev = BlockType::Paragraph;
        double carry = 0; // extra space owed to the next item (captions)
        bool atStart = true;
        QTextTable *prevTable = nullptr; // the item before was this table
        auto closeTo = [&](int depth) {
            double extra = 0;
            while (!stack.isEmpty() && stack.last().depth >= depth) {
                const Open o = stack.takeLast();
                if (o.children && o.type == BlockType::Callout)
                    extra += padBottom(BlockType::Callout, s) + 6 * k;
                else if (o.children && o.type == BlockType::Quote)
                    extra += 6 * k;
            }
            return extra;
        };
        for (int i = 0; i < items.size(); ++i) {
            const Item &item = items[i];
            if (item.isTable()) {
                QTextTable *t = item.table;
                const BlockType tt = DocumentIO::isColumns(t) ? BlockType::ColumnList : BlockType::Table;
                const int depth = tableDepth(t);
                const double closing = closeTo(depth);
                double extraLeft = 0;
                for (const Open &o : stack)
                    extraLeft += o.childLeft;
                const double top = atStart ? 0 : topMarginFor(tt, prev, s, closing + carry);
                const double left = depth * s.indent() + extraLeft;
                const QTextTableFormat tf = t->format();
                const QString id = claimId(tf.property(IdProp).toString());
                tableFixes.append({t, top, left, id, 0, DocumentIO::cellsNeedFormat(t, s)});
                for (int r = 0; r < t->rows(); ++r)
                    for (int c = 0; c < t->columns(); ++c)
                        walk(Container{t, r, c});
                if (!stack.isEmpty())
                    stack.last().children = true;
                prev = tt;
                carry = 0;
                atStart = false;
                prevTable = t;
                continue;
            }
            QTextTable *tableBefore = prevTable;
            prevTable = nullptr;
            const QTextBlock blk = item.block;
            const BlockType t = typeOf(blk);
            const QString text = blk.text();
            if (const QString id = claimId(idOf(blk)); !id.isEmpty())
                fixes.append({blk.blockNumber(), Fix::Id, 0, 0, 0, -1, id});
            if (simpleCell) {
                bool objects = false;
                for (auto it = blk.begin(); !it.atEnd() && !objects; ++it) {
                    const int ot = it.fragment().charFormat().objectType();
                    objects = ot >= QTextFormat::UserObject && !isInlineObject(ot);
                }
                const QTextBlockFormat bf = blk.blockFormat();
                if (t != BlockType::Paragraph || depthOf(blk) != 0 || objects || qAbs(bf.topMargin()) > 0.5
                    || qAbs(bf.leftMargin()) > 0.5 || qAbs(bf.rightMargin()) > 0.5)
                    fixes.append({blk.blockNumber(), Fix::CellParagraph});
                continue;
            }
            // The empty helper block in front of a table takes no room.
            if (i == 0 && items.size() > 1 && items[1].isTable() && blk.length() <= 1 && t == BlockType::Paragraph) {
                const QTextBlockFormat bf = blk.blockFormat();
                if (qAbs(bf.topMargin()) > 0.5)
                    fixes.append({blk.blockNumber(), Fix::Margins, 0, 0, 0});
                continue;
            }
            if (blockTypeIsAtomic(t)) {
                const int obj = text.indexOf(QChar::ObjectReplacementCharacter);
                if (obj < 0) {
                    fixes.append({blk.blockNumber(), Fix::ToParagraph});
                    prev = BlockType::Paragraph;
                    atStart = false;
                    continue;
                }
                if (text.size() > 1)
                    fixes.append({blk.blockNumber(), Fix::Split, 0, 0, 0, obj});
            }
            const int depth = depthOf(blk);
            const double closing = closeTo(depth);
            double extraLeft = 0, extraRight = 0;
            for (const Open &o : stack) {
                extraLeft += o.childLeft;
                extraRight += o.childRight;
            }
            // The first child of a callout or quote sits inside its box.
            BlockType prevForMargin = prev;
            if (!stack.isEmpty() && !stack.last().children
                && (stack.last().type == BlockType::Callout || stack.last().type == BlockType::Quote))
                prevForMargin = BlockType::Paragraph;
            double top = atStart ? 0 : topMarginFor(t, prevForMargin, s, closing + carry);
            if (tableBefore) {
                // Spacing after a table lives in the table's bottom margin.
                for (TableFix &tf : tableFixes)
                    if (tf.table == tableBefore)
                        tf.bottom = top;
                top = 0;
            }
            const double left = depth * s.indent() + extraLeft + blockLeftInset(t, s) + toggleRoom(blk, s);
            const double right = blockRightInset(t, s) + extraRight;
            const QTextBlockFormat bf = blk.blockFormat();
            // Mermaid code blocks keep room below for their diagram.
            double bottom = 0;
            if (t == BlockType::Code && bf.property(LanguageProp).toString() == QLatin1String("mermaid")) {
                QString src = blk.text();
                src.replace(QChar::LineSeparator, QLatin1Char('\n'));
                const double width = qMax(200.0, viewport()->width() - gutter() * 1.5 - left - right);
                const Mermaid::Info info = Mermaid::measure(src, 13 * k, width - 32 * k);
                if (info.supported)
                    bottom = info.size.height() + 64 * k;
            }
            if (qAbs(bf.topMargin() - top) > 0.5 || qAbs(bf.leftMargin() - left) > 0.5 || qAbs(bf.rightMargin() - right) > 0.5
                || qAbs(bf.bottomMargin() - bottom) > 0.5) {
                Fix fx{blk.blockNumber(), Fix::Margins, top, left, right};
                fx.bottom = bottom;
                fixes.append(fx);
            }
            // Text whose look doesn't match its block (merged from elsewhere).
            if (!blockTypeIsAtomic(t)) {
                const double want = headingSize(t, s);
                bool mismatch = false;
                for (auto it = blk.begin(); !it.atEnd() && !mismatch; ++it) {
                    const QTextCharFormat cf = it.fragment().charFormat();
                    if (cf.objectType() >= QTextFormat::UserObject)
                        continue;
                    const double px = cf.property(QTextFormat::FontPixelSize).toDouble();
                    const double expect = cf.property(CodeProp).toBool() ? want * 0.86 : want;
                    if (qAbs(px - expect) > 0.6)
                        mismatch = true;
                }
                if (mismatch)
                    fixes.append({blk.blockNumber(), Fix::Restyle});
            }
            if (!stack.isEmpty())
                stack.last().children = true;
            const double childLeft = t == BlockType::Callout ? blockLeftInset(BlockType::Callout, s) - s.indent()
                                     : t == BlockType::Quote ? blockLeftInset(BlockType::Quote, s) - s.indent()
                                                             : 0;
            const double childRight = t == BlockType::Callout ? blockRightInset(BlockType::Callout, s) : 0;
            stack.append({depth, t, childLeft, childRight});
            carry = (t == BlockType::Code && !captionOf(blk).isEmpty()) ? 24 * k : 0;
            prev = t;
            atStart = false;
        }
    };
    walk(Container{});
    tableFixes.removeIf([](const TableFix &tf) {
        const QTextTableFormat f = tf.table->format();
        return tf.id.isEmpty() && !tf.cells && qAbs(f.topMargin() - tf.top) <= 0.5 && qAbs(f.leftMargin() - tf.left) <= 0.5
               && qAbs(f.bottomMargin() - qMax(0.0, tf.bottom)) <= 0.5;
    });
    if (fixes.isEmpty() && tableFixes.isEmpty())
        return;

    const bool wasLoading = m_loading;
    m_loading = true; // our own fixes don't schedule another pass
    QTextCursor c(doc);
    c.joinPreviousEditBlock();
    for (const TableFix &tf : tableFixes) {
        QTextTableFormat f = tf.table->format();
        f.setTopMargin(tf.top);
        f.setLeftMargin(tf.left);
        f.setBottomMargin(qMax(0.0, tf.bottom));
        if (!tf.id.isEmpty())
            f.setProperty(IdProp, tf.id);
        tf.table->setFormat(f);
        if (tf.cells)
            DocumentIO::formatCells(tf.table, s);
    }
    // Splits change numbering: do them last, from the bottom up.
    for (const Fix &f : fixes) {
        QTextBlock blk = doc->findBlockByNumber(f.block);
        if (!blk.isValid())
            continue;
        if (f.kind == Fix::Margins) {
            QTextBlockFormat bf = blk.blockFormat();
            bf.setTopMargin(f.top);
            bf.setLeftMargin(f.left);
            bf.setRightMargin(f.right);
            bf.setBottomMargin(f.bottom);
            QTextCursor bc(blk);
            bc.setBlockFormat(bf);
        } else if (f.kind == Fix::Id) {
            QTextBlockFormat bf = blk.blockFormat();
            bf.setProperty(IdProp, f.id);
            QTextCursor bc(blk);
            bc.setBlockFormat(bf);
        } else if (f.kind == Fix::Restyle) {
            // Re-derive inline meaning from each fragment's own origin.
            const Block props = blockProps(blk);
            QList<QPair<int, int>> ranges;
            QList<Span> spans;
            for (auto it = blk.begin(); !it.atEnd(); ++it) {
                const QTextFragment frag = it.fragment();
                const QTextCharFormat cf = frag.charFormat();
                if (cf.objectType() >= QTextFormat::UserObject)
                    continue;
                ranges.append({frag.position(), frag.length()});
                spans.append(spanOf(cf, frag.text(), guessOrigin(cf, s), false));
            }
            QTextCursor bc(doc);
            for (int i = 0; i < ranges.size(); ++i) {
                bc.setPosition(ranges[i].first);
                bc.setPosition(ranges[i].first + ranges[i].second, QTextCursor::KeepAnchor);
                bc.setCharFormat(spanFormat(spans[i], props.type, s, props.checked, props.color));
            }
        } else if (f.kind == Fix::ToParagraph) {
            Block p = blockProps(blk);
            p.type = BlockType::Paragraph;
            applyBlockProps(blk, p, s);
        } else if (f.kind == Fix::CellParagraph) {
            // Simple-table cells hold one plain paragraph: drop block objects.
            for (int pos = blk.position() + blk.length() - 2; pos >= blk.position(); --pos) {
                QTextCursor oc(doc);
                oc.setPosition(pos);
                oc.setPosition(pos + 1, QTextCursor::KeepAnchor);
                const int ot = oc.charFormat().objectType();
                if (doc->characterAt(pos) == QChar::ObjectReplacementCharacter && ot >= QTextFormat::UserObject && !isInlineObject(ot))
                    oc.removeSelectedText();
            }
            Block p = blockProps(blk);
            p.type = BlockType::Paragraph;
            p.depth = 0;
            p.checked = false;
            applyBlockProps(blk, p, s);
            QTextBlockFormat bf = blk.blockFormat();
            bf.setTopMargin(0);
            bf.setLeftMargin(0);
            bf.setRightMargin(0);
            QTextCursor bc(blk);
            bc.setBlockFormat(bf);
        }
    }
    for (auto it = fixes.crbegin(); it != fixes.crend(); ++it) {
        if (it->kind != Fix::Split)
            continue;
        QTextBlock blk = doc->findBlockByNumber(it->block);
        if (!blk.isValid())
            continue;
        const Block props = blockProps(blk);
        Block para;
        para.depth = props.depth;
        QTextCursor bc(doc);
        // Text after the object moves to a paragraph below…
        if (it->splitAt + 1 < blk.length() - 1) {
            bc.setPosition(blk.position() + it->splitAt + 1);
            bc.insertBlock();
            applyBlockProps(bc.block(), para, s);
        }
        // …and text before it to a paragraph above.
        if (it->splitAt > 0) {
            bc.setPosition(blk.position() + it->splitAt);
            bc.insertBlock();
            Block above = para;
            above.id.clear();
            applyBlockProps(bc.block().previous(), above, s);
            applyBlockProps(bc.block(), props, s);
        }
    }
    c.endEditBlock();
    m_loading = wasLoading;
    updateVisibility();
    // Margins of neighbours may have changed with the splits.
    if (std::any_of(fixes.begin(), fixes.end(), [](const Fix &f) { return f.kind == Fix::Split || f.kind == Fix::ToParagraph; }))
        scheduleNormalize();
}

void BlockEditor::updateVisibility()
{
    QTextDocument *doc = document();
    int from = -1, to = -1;
    auto setVisible = [&](QTextBlock blk, bool visible) {
        if (blk.isVisible() == visible)
            return;
        blk.setVisible(visible);
        if (from < 0 || blk.position() < from)
            from = blk.position();
        to = qMax(to, blk.position() + blk.length());
    };
    std::function<void(const Container &, bool)> walk = [&](const Container &container, bool hidden) {
        const QList<Item> items = DocumentIO::itemsOf(doc, container);
        int hideBelow = -1;
        for (int i = 0; i < items.size(); ++i) {
            const Item &item = items[i];
            const int depth = depthOfItem(item);
            bool visible = !hidden;
            if (hideBelow >= 0 && depth > hideBelow) {
                visible = false;
            } else {
                hideBelow = -1;
                if (!item.isTable() && foldsChildren(item.block) && isCollapsed(item.block))
                    hideBelow = depth;
            }
            if (item.isTable()) {
                QTextTable *t = item.table;
                for (int r = 0; r < t->rows(); ++r)
                    for (int c = 0; c < t->columns(); ++c)
                        walk(Container{t, r, c}, !visible);
                continue;
            }
            // The empty helper block in front of a table stays out of sight.
            if (i == 0 && items.size() > 1 && items[1].isTable() && item.block.length() <= 1
                && typeOf(item.block) == BlockType::Paragraph)
                visible = false;
            setVisible(item.block, visible);
        }
    };
    walk(Container{}, false);
    if (from >= 0) {
        doc->markContentsDirty(from, to - from);
        viewport()->update();
        updateHeight();
    }
}

// ---- geometry --------------------------------------------------------------------------

QRectF BlockEditor::blockRect(const QTextBlock &block) const
{
    return document()->documentLayout()->blockBoundingRect(block).translated(-horizontalScrollBar()->value(),
                                                                            -verticalScrollBar()->value());
}

QRectF BlockEditor::tableRect(const QTextTable *table) const
{
    return document()
        ->documentLayout()
        ->frameBoundingRect(const_cast<QTextTable *>(table))
        .translated(-horizontalScrollBar()->value(), -verticalScrollBar()->value());
}

double BlockEditor::firstBaseline(const QTextBlock &block) const
{
    const QRectF r = blockRect(block);
    const QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0)
        return r.top() + m_ctx.style.size;
    const QTextLine line = layout->lineAt(0);
    return r.top() + line.y() + line.ascent();
}

double BlockEditor::firstLineMid(const QTextBlock &block) const
{
    const QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0)
        return blockRect(block).top() + m_ctx.style.size * 0.75;
    const QTextLine line = layout->lineAt(0);
    // Middle of the glyphs (not of the stretched line box).
    return blockRect(block).top() + line.y() + (line.ascent() + line.descent()) / 2.0;
}

double BlockEditor::blockLeft(const QTextBlock &block) const
{
    const QTextBlockFormat bf = block.blockFormat();
    return blockRect(block).left() + bf.leftMargin() - blockLeftInset(typeOf(block), m_ctx.style) - toggleRoom(block, m_ctx.style);
}

double BlockEditor::textLeft(const QTextBlock &block) const
{
    return blockRect(block).left() + block.blockFormat().leftMargin();
}

double BlockEditor::blockRight(const QTextBlock &block) const
{
    // A block's rect is as wide as its lines: the container's right edge is
    // past its right margin.
    const QTextBlockFormat bf = block.blockFormat();
    const QRectF r = blockRect(block);
    const double containerRight = r.left() + r.width() + bf.leftMargin() + bf.rightMargin();
    const double extra = bf.rightMargin() - blockRightInset(typeOf(block), m_ctx.style);
    return containerRight - qMax(0.0, extra);
}

QTextBlock BlockEditor::firstVisibleBlock(const QRect &rect) const
{
    QTextBlock blk = cursorForPosition(QPoint(gutter() + 4, qMax(0, rect.top()))).block();
    // Start at the outermost table holding it: its cells come in column order.
    QTextTable *outer = nullptr;
    for (QTextTable *t = DocumentIO::tableOf(blk); t; t = qobject_cast<QTextTable *>(t->parentFrame()))
        outer = t;
    if (outer)
        blk = document()->findBlock(outer->firstPosition() - 1);
    // Backgrounds reach above their block: start a little earlier.
    for (int i = 0; i < 2 && blk.previous().isValid(); ++i)
        blk = blk.previous();
    return blk;
}

QList<QTextBlock> BlockEditor::blocksIn(const QRect &rect) const
{
    QList<QTextBlock> out;
    for (QTextBlock blk = firstVisibleBlock(rect); blk.isValid(); blk = blk.next()) {
        if (!blk.isVisible())
            continue;
        const QRectF r = blockRect(blk);
        if (r.top() - 60 > rect.bottom()) {
            // Root blocks come in visual order; cells of a column may not.
            if (!DocumentIO::tableOf(blk))
                break;
            continue;
        }
        if (r.bottom() + 60 < rect.top())
            continue;
        out.append(blk);
    }
    return out;
}

QTextBlock BlockEditor::blockAt(const QPointF &pos) const
{
    QTextBlock best;
    double bestLeft = -1e9;
    QTextBlock above;
    double aboveTop = -1e9;
    const QRect band(0, int(pos.y()) - 80, viewport()->width(), 160);
    for (const QTextBlock &blk : blocksIn(band)) {
        const QRectF r = blockRect(blk);
        const double top = r.top() - blk.blockFormat().topMargin();
        const double reachLeft = r.left() - 58;
        const QTextBlockFormat bf = blk.blockFormat();
        const double reachRight = r.left() + r.width() + bf.leftMargin() + bf.rightMargin() + 8;
        if (pos.x() >= reachLeft && pos.x() <= reachRight && top <= pos.y() && top > aboveTop) {
            above = blk;
            aboveTop = top;
        }
        if (pos.y() < top || pos.y() > r.bottom() + 2)
            continue;
        if (pos.x() < reachLeft || pos.x() > reachRight)
            continue;
        // The innermost container (the one starting furthest right) wins.
        if (r.left() > bestLeft) {
            best = blk;
            bestLeft = r.left();
        }
    }
    return best.isValid() ? best : above;
}

// ---- structure ----------------------------------------------------------------------------

QTextBlock BlockEditor::previousInContainer(const QTextBlock &block) const
{
    const Container c = DocumentIO::containerOf(block);
    for (Item it = prevItem(document(), block.blockNumber(), c); it.first >= 0; it = prevItem(document(), it.first, c))
        if (!it.isTable())
            return it.block;
    return {};
}

QTextBlock BlockEditor::nextInContainer(const QTextBlock &block) const
{
    const Container c = DocumentIO::containerOf(block);
    for (Item it = nextItem(document(), block.blockNumber(), c); it.first >= 0; it = nextItem(document(), it.last, c))
        if (!it.isTable())
            return it.block;
    return {};
}

int BlockEditor::itemDepth(const QTextBlock &first) const
{
    if (QTextTable *t = DocumentIO::wholeTableAt(first, INT_MAX))
        return tableDepth(t);
    return depthOf(first);
}

int BlockEditor::subtreeEnd(const QTextBlock &block) const
{
    const Container c = DocumentIO::containerOf(block);
    const int depth = depthOf(block);
    int last = block.blockNumber();
    for (Item it = nextItem(document(), last, c); it.first >= 0; it = nextItem(document(), it.last, c)) {
        if (depthOfItem(it) <= depth)
            break;
        last = it.last;
    }
    return last;
}

QPair<int, int> BlockEditor::selectedBlockRange() const
{
    if (m_rangeOverride.first >= 0)
        return m_rangeOverride;
    const QTextCursor c = textCursor();
    const QTextDocument *doc = document();
    QTextBlock a = doc->findBlock(c.selectionStart());
    QTextBlock b = doc->findBlock(c.selectionEnd());
    // A selection ending at the very start of a block doesn't include it.
    if (b.blockNumber() > a.blockNumber() && c.selectionEnd() == b.position())
        b = b.previous();
    if (DocumentIO::sameContainer(a, b))
        return {a.blockNumber(), b.blockNumber()};
    // Across containers: widen to the items of the innermost common one.
    const auto chainA = containerChain(a), chainB = containerChain(b);
    for (const auto &ca : chainA) {
        for (const auto &cb : chainB) {
            if (ca.first == cb.first)
                return {qMin(ca.second.first, cb.second.first), qMax(ca.second.last, cb.second.last)};
        }
    }
    return {a.blockNumber(), a.blockNumber()};
}

QPair<int, int> BlockEditor::withChildren(QPair<int, int> range) const
{
    const QTextDocument *doc = document();
    const Container c = rangeContainer(doc, range.first, range.second);
    int last = range.second;
    // Children of every item in the range come along.
    Item it = DocumentIO::itemFor(doc->findBlockByNumber(range.first), c);
    while (it.first >= 0 && it.first <= last) {
        const int depth = depthOfItem(it);
        int end = it.last;
        for (Item n = nextItem(doc, end, c); n.first >= 0 && depthOfItem(n) > depth; n = nextItem(doc, n.last, c))
            end = n.last;
        last = qMax(last, end);
        it = nextItem(doc, it.last, c);
    }
    return {range.first, last};
}

int BlockEditor::numberFor(const QTextBlock &block) const
{
    const Container c = DocumentIO::containerOf(block);
    const int depth = depthOf(block);
    int n = 1;
    for (Item it = prevItem(document(), block.blockNumber(), c); it.first >= 0; it = prevItem(document(), it.first, c)) {
        const int d = depthOfItem(it);
        if (d > depth)
            continue;
        if (d < depth || it.isTable() || typeOf(it.block) != BlockType::Numbered)
            break;
        ++n;
    }
    return n;
}

QHash<int, int> BlockEditor::listNumbers() const
{
    QHash<int, int> numbers;
    const QTextDocument *doc = document();
    std::function<void(const Container &)> walk = [&](const Container &container) {
        QList<int> counters(12, 0);
        QList<bool> active(12, false);
        for (const Item &item : DocumentIO::itemsOf(doc, container)) {
            const int d = qBound(0, depthOfItem(item), 11);
            for (int i = d + 1; i < 12; ++i)
                active[i] = false;
            if (item.isTable()) {
                active[d] = false;
                for (int r = 0; r < item.table->rows(); ++r)
                    for (int c = 0; c < item.table->columns(); ++c)
                        walk(Container{item.table, r, c});
                continue;
            }
            if (typeOf(item.block) == BlockType::Numbered) {
                counters[d] = active[d] ? counters[d] + 1 : 1;
                active[d] = true;
                numbers.insert(item.block.blockNumber(), counters[d]);
            } else {
                active[d] = false;
            }
        }
    };
    walk(Container{});
    return numbers;
}

void BlockEditor::setBlockProps(QTextBlock block, const Block &props)
{
    const Block old = blockProps(block);
    const bool wasAtomic = blockTypeIsAtomic(old.type);
    const bool atomic = blockTypeIsAtomic(props.type);
    QTextCursor c(block);
    if (wasAtomic && !atomic) {
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
    }
    Block p = props;
    if (p.id.isEmpty())
        p.id = old.id;
    applyBlockProps(block, p, m_ctx.style);
    if (atomic && (!wasAtomic || old.type != props.type)) {
        c.setPosition(block.position());
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        DocumentIO::writeContent(c, props, m_ctx.style);
    }
    if (old.type == BlockType::Code || props.type == BlockType::Code)
        m_highlighter->rehighlightBlock(block);
}

void BlockEditor::insertNewBlockAfter(const QTextBlock &block, const Block &props, bool focus)
{
    QTextDocument *doc = document();
    QTextBlock inserted;
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) {
        // After a folded toggle, the new block goes below its hidden children.
        if (foldsChildren(block) && isCollapsed(block)) {
            const Container cont = DocumentIO::containerOf(block);
            const int end = subtreeEnd(block);
            const Item last = DocumentIO::itemFor(doc->findBlockByNumber(end), cont);
            if (last.isTable())
                inserted = DocumentIO::insertBefore(c, doc->findBlock(last.table->lastPosition() + 1), {props}, m_ctx.style);
            else
                inserted = DocumentIO::insertAfter(c, doc->findBlockByNumber(end), {props}, m_ctx.style);
        } else {
            inserted = DocumentIO::insertAfter(c, block, {props}, m_ctx.style);
        }
    });
    updateVisibility();
    if (focus) {
        QTextCursor fc(doc);
        fc.setPosition(inserted.position() + (blockTypeIsAtomic(props.type) ? 0 : inserted.length() - 1));
        setTextCursor(fc);
    }
}

// ---- moving -------------------------------------------------------------------------------

BlockEditor::DropTarget BlockEditor::dropTargetAt(const QPointF &pos) const
{
    DropTarget t;
    const QTextDocument *doc = document();
    QTextBlock target = blockAt(pos);
    if (!target.isValid())
        return t;
    // A simple table is one item: drop above or below it.
    QTextTable *cellTable = DocumentIO::tableOf(target);
    if (cellTable && DocumentIO::isSimpleTable(cellTable)) {
        const QRectF tr = tableRect(cellTable);
        if (pos.y() < tr.center().y()) {
            t.where = DropTarget::After;
            t.block = doc->findBlock(cellTable->firstPosition() - 1).blockNumber();
        } else {
            t.where = DropTarget::Before;
            t.block = doc->findBlock(cellTable->lastPosition() + 1).blockNumber();
        }
        t.depth = tableDepth(cellTable);
        return t;
    }
    const QRectF r = blockRect(target);
    const double left = blockLeft(target);
    const double right = blockRight(target);
    const int n = target.blockNumber();
    // Beside a top-level block (or a block in a column): a new column.
    const Container cont = DocumentIO::containerOf(target);
    const bool sideAllowed = depthOf(target) == 0 && (cont.isRoot() || DocumentIO::isColumns(cont.table));
    if (sideAllowed && pos.y() >= r.top() - 2 && pos.y() <= r.bottom() + 2) {
        if (pos.x() < left - 4 && pos.x() > left - 60) {
            t.where = DropTarget::Left;
            t.block = n;
            return t;
        }
        if (pos.x() > right - 40) {
            t.where = DropTarget::Right;
            t.block = n;
            return t;
        }
    }
    const double mid = r.center().y();
    if (pos.y() < mid) {
        t.where = DropTarget::Before;
        t.block = n;
        t.depth = depthOf(target);
    } else if (foldsChildren(target) && isCollapsed(target)) {
        const int end = subtreeEnd(target);
        const Item next = nextItem(doc, end, cont);
        const Item last = DocumentIO::itemFor(doc->findBlockByNumber(end), cont);
        if (next.first >= 0 && !next.isTable()) {
            t.where = DropTarget::Before;
            t.block = next.first;
        } else if (!last.isTable()) {
            t.where = DropTarget::After;
            t.block = end;
        } else {
            t.where = DropTarget::Before;
            t.block = doc->findBlock(last.table->lastPosition() + 1).blockNumber();
        }
        t.depth = depthOf(target);
    } else {
        t.where = DropTarget::After;
        t.block = n;
        const Item next = nextItem(doc, n, cont);
        const bool hasKids = next.first >= 0 && depthOfItem(next) > depthOf(target);
        t.depth = hasKids ? depthOf(target) + 1 : depthOf(target);
    }
    return t;
}

void BlockEditor::moveRange(int first, int last, const DropTarget &target)
{
    QTextDocument *doc = document();
    if (target.where == DropTarget::None || (target.block >= first && target.block <= last))
        return;
    QList<Block> blocks = DocumentIO::range(doc, first, last);
    if (blocks.isEmpty())
        return;
    QTextBlock anchorBlock = doc->findBlockByNumber(target.block);
    if (!anchorBlock.isValid())
        return;
    // Column lists don't nest.
    const Container targetCont = DocumentIO::containerOf(anchorBlock);
    const bool intoColumn = !targetCont.isRoot() && DocumentIO::isColumns(targetCont.table);
    bool movingColumns = false;
    forEachBlock(blocks, [&](const Block &b) { movingColumns = movingColumns || b.type == BlockType::ColumnList; });
    if (intoColumn && movingColumns)
        return;
    if ((target.where == DropTarget::Left || target.where == DropTarget::Right) && movingColumns)
        return;

    if (target.where == DropTarget::Left || target.where == DropTarget::Right) {
        const bool left = target.where == DropTarget::Left;
        if (intoColumn)
            addColumnBeside(const_cast<QTextTable *>(targetCont.table), targetCont.col, left, first, last);
        else
            wrapIntoColumns(first, last, target.block, left);
        return;
    }

    const int delta = target.depth - blocks.first().depth;
    for (Block &b : blocks)
        b.depth = qBound(0, b.depth + delta, 10);
    QTextCursor anchor(doc);
    anchor.setPosition(anchorBlock.position());
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, first, last, m_ctx.style); });
    QTextBlock a = anchor.block();
    QTextBlock firstMoved;
    int firstMovedNumber = -1;
    steps.run([&](QTextCursor &c) {
        if (target.where == DropTarget::Before) {
            // insertBefore keeps `a` below the new blocks; the first new
            // block takes its place.
            firstMovedNumber = a.blockNumber();
            DocumentIO::insertBefore(c, a, blocks, m_ctx.style);
        } else if (DocumentIO::isPhantom(a)) {
            // An empty helper block next to a table becomes the first moved block.
            firstMovedNumber = a.blockNumber();
            DocumentIO::fillEmpty(c, a, blocks, m_ctx.style);
        } else {
            firstMovedNumber = a.blockNumber() + 1;
            DocumentIO::insertAfter(c, a, blocks, m_ctx.style);
        }
    });
    QTextCursor movedCursor(doc);
    movedCursor.setPosition(doc->findBlockByNumber(firstMovedNumber).position());
    cleanupColumns();
    firstMoved = movedCursor.block();
    updateVisibility();
    scheduleNormalize();
    if (firstMoved.isValid()) {
        QTextCursor sel(doc);
        sel.setPosition(firstMoved.position());
        setTextCursor(sel);
    }
}

void BlockEditor::indentBlocks(int delta)
{
    QTextDocument *doc = document();
    const auto range = withChildren(selectedBlockRange());
    const Container cont = rangeContainer(doc, range.first, range.second);
    if (!cont.isRoot() && DocumentIO::isSimpleTable(cont.table))
        return;
    const Item firstItem = DocumentIO::itemFor(doc->findBlockByNumber(range.first), cont);
    const int firstDepth = depthOfItem(firstItem);
    if (delta > 0) {
        Item prev = prevItem(doc, firstItem.first, cont);
        while (prev.first >= 0 && !prev.isTable() && !prev.block.isVisible())
            prev = prevItem(doc, prev.first, cont);
        const int maxDepth = prev.first >= 0 ? depthOfItem(prev) + 1 : 0;
        if (firstDepth + 1 > maxDepth)
            return;
    } else if (firstDepth == 0) {
        return;
    }
    QTextCursor c(doc);
    c.beginEditBlock();
    for (Item it = firstItem; it.first >= 0 && it.first <= range.second; it = nextItem(doc, it.last, cont)) {
        if (it.isTable()) {
            QTextTableFormat tf = it.table->format();
            tf.setProperty(DepthProp, qBound(0, tableDepth(it.table) + delta, 10));
            it.table->setFormat(tf);
            continue;
        }
        Block props = blockProps(it.block);
        props.depth = qBound(0, props.depth + delta, 10);
        applyBlockProps(it.block, props, m_ctx.style);
    }
    c.endEditBlock();
    updateVisibility();
    scheduleNormalize();
}

void BlockEditor::moveBlocks(int delta)
{
    QTextDocument *doc = document();
    const auto range = withChildren(selectedBlockRange());
    const Container cont = rangeContainer(doc, range.first, range.second);
    const QTextCursor before = textCursor();
    const QTextBlock firstBlock = doc->findBlockByNumber(range.first);
    const int offset = before.position() - firstBlock.position();
    const Item firstItem = DocumentIO::itemFor(firstBlock, cont);
    const int depth = depthOfItem(firstItem);
    DropTarget t;
    t.depth = depth;
    if (delta < 0) {
        // Jump over the previous sibling with all its children.
        Item prev = prevItem(doc, firstItem.first, cont);
        while (prev.first >= 0 && depthOfItem(prev) > depth)
            prev = prevItem(doc, prev.first, cont);
        if (prev.first < 0)
            return;
        if (prev.isTable()) {
            t.where = DropTarget::After;
            t.block = doc->findBlock(prev.table->firstPosition() - 1).blockNumber();
        } else {
            t.where = DropTarget::Before;
            t.block = prev.first;
        }
    } else {
        const Item next = nextItem(doc, range.second, cont);
        if (next.first < 0)
            return;
        int end = next.last;
        for (Item n = nextItem(doc, end, cont); n.first >= 0 && depthOfItem(n) > depthOfItem(next); n = nextItem(doc, n.last, cont))
            end = n.last;
        const Item last = DocumentIO::itemFor(doc->findBlockByNumber(end), cont);
        if (last.isTable()) {
            t.where = DropTarget::Before;
            t.block = doc->findBlock(last.table->lastPosition() + 1).blockNumber();
        } else {
            t.where = DropTarget::After;
            t.block = end;
        }
    }
    moveRange(range.first, range.second, t);
    QTextCursor c = textCursor();
    c.setPosition(qMin(c.position() + offset, c.block().position() + c.block().length() - 1));
    setTextCursor(c);
}

void BlockEditor::duplicateBlocks()
{
    QTextDocument *doc = document();
    const auto range = withChildren(selectedBlockRange());
    const Container cont = rangeContainer(doc, range.first, range.second);
    QList<Block> copy = DocumentIO::range(doc, range.first, range.second);
    renewBlockIds(copy);
    const Item last = DocumentIO::itemFor(doc->findBlockByNumber(range.second), cont);
    QTextBlock firstNew;
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) {
        if (last.isTable()) {
            const QTextBlock after = doc->findBlock(last.table->lastPosition() + 1);
            const int n = after.blockNumber();
            DocumentIO::insertBefore(c, after, copy, m_ctx.style);
            firstNew = doc->findBlockByNumber(n);
        } else {
            DocumentIO::insertAfter(c, last.block, copy, m_ctx.style);
            firstNew = last.block.next();
        }
    });
    updateVisibility();
    if (firstNew.isValid()) {
        QTextCursor m(doc);
        m.setPosition(firstNew.position());
        setTextCursor(m);
    }
}

QList<Block> BlockEditor::takeSelectedBlocks()
{
    QTextDocument *doc = document();
    const auto range = withChildren(selectedBlockRange());
    const QList<Block> taken = DocumentIO::range(doc, range.first, range.second);
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) {
        DocumentIO::removeRange(c, range.first, range.second, m_ctx.style);
        if (doc->blockCount() == 1 && doc->begin().text().isEmpty())
            applyBlockProps(doc->begin(), Block{}, m_ctx.style);
    });
    cleanupColumns();
    updateVisibility();
    scheduleNormalize();
    return taken;
}

void BlockEditor::deleteBlocks()
{
    takeSelectedBlocks();
}

// ---- columns -------------------------------------------------------------------------------

QList<double> BlockEditor::columnWidths(const QTextTable *table) const
{
    QList<double> out;
    const QList<QTextLength> lens = table->format().columnWidthConstraints();
    double total = 0;
    for (int i = 0; i < table->columns(); ++i)
        total += i < lens.size() ? lens[i].rawValue() : 0;
    for (int i = 0; i < table->columns(); ++i)
        out.append(i < lens.size() && total > 0 ? lens[i].rawValue() / total : 1.0 / table->columns());
    return out;
}

void BlockEditor::setColumnWidths(QTextTable *table, const QList<double> &widths)
{
    QTextTableFormat tf = table->format();
    QList<QTextLength> lens;
    double total = 0;
    for (double w : widths)
        total += w;
    for (double w : widths)
        lens.append(QTextLength(QTextLength::PercentageLength, total > 0 ? 100.0 * w / total : 100.0 / widths.size()));
    tf.setColumnWidthConstraints(lens);
    table->setFormat(tf);
}

void BlockEditor::insertColumnList(int count)
{
    QTextDocument *doc = document();
    QTextBlock blk = textCursor().block();
    if (!DocumentIO::containerOf(blk).isRoot()) {
        emit toast(tr("Columns can't go inside a table or another column."));
        return;
    }
    Block cols;
    cols.type = BlockType::ColumnList;
    cols.id = Block::newId();
    cols.depth = depthOf(blk);
    for (int i = 0; i < count; ++i) {
        cols.columns.append({Block{}});
        cols.widths.append(1.0 / count);
    }
    // A block with content moves into the first column.
    if (!(blk.text().isEmpty() && typeOf(blk) == BlockType::Paragraph)) {
        Block current = DocumentIO::blockAt(blk);
        current.depth = 0;
        cols.columns[0] = {current};
    }
    const int n = blk.blockNumber();
    // The columns replace the block.
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) { DocumentIO::insertAfter(c, blk, {cols}, m_ctx.style); });
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, n, n, m_ctx.style); });
    QTextTable *t = nullptr;
    for (QTextTable *table : allTables(doc->rootFrame()))
        if (table->format().property(IdProp).toString() == cols.id)
            t = table;
    updateVisibility();
    scheduleNormalize();
    if (t)
        setTextCursor(t->cellAt(0, 0).lastCursorPosition());
}

void BlockEditor::wrapIntoColumns(int first, int last, int targetBlock, bool left)
{
    QTextDocument *doc = document();
    QList<Block> dragged = DocumentIO::range(doc, first, last);
    if (dragged.isEmpty())
        return;
    const int dragBase = dragged.first().depth;
    for (Block &b : dragged)
        b.depth = qMax(0, b.depth - dragBase);
    QTextCursor xCursor(doc);
    xCursor.setPosition(doc->findBlockByNumber(targetBlock).position());
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, first, last, m_ctx.style); });
    const QTextBlock x = xCursor.block();
    const auto xr = withChildren({x.blockNumber(), x.blockNumber()});
    QList<Block> xBlocks = DocumentIO::range(doc, xr.first, xr.second);
    const int base = depthOf(x);
    for (Block &b : xBlocks)
        b.depth = qMax(0, b.depth - base);
    const Container cont = DocumentIO::containerOf(x);
    const Item after = nextItem(doc, xr.second, cont);
    const bool followedByBlock = after.first >= 0 && !after.isTable();
    Block cols;
    cols.type = BlockType::ColumnList;
    cols.id = Block::newId();
    cols.depth = base;
    cols.columns = left ? QList<QList<Block>>{dragged, xBlocks} : QList<QList<Block>>{xBlocks, dragged};
    cols.widths = {0.5, 0.5};
    QTextCursor anchor(doc);
    anchor.setPosition(x.position());
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, xr.first, xr.second, m_ctx.style); });
    QTextBlock a = anchor.block();
    steps.run([&](QTextCursor &c) {
        if (followedByBlock)
            DocumentIO::insertBefore(c, a, {cols}, m_ctx.style);
        else if (a.length() <= 1 && !previousInContainer(a).isValid() && typeOf(a) == BlockType::Paragraph)
            DocumentIO::fillEmpty(c, a, {cols}, m_ctx.style);
        else
            DocumentIO::insertAfter(c, a, {cols}, m_ctx.style);
    });
    cleanupColumns();
    updateVisibility();
    scheduleNormalize();
}

void BlockEditor::addColumnBeside(QTextTable *table, int column, bool left, int first, int last)
{
    QTextDocument *doc = document();
    QList<Block> dragged = DocumentIO::range(doc, first, last);
    if (dragged.isEmpty() || !table)
        return;
    const int base = dragged.first().depth;
    for (Block &b : dragged)
        b.depth = qMax(0, b.depth - base);
    const QString tableId = table->format().property(IdProp).toString();
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, first, last, m_ctx.style); });
    // The table survives removals inside its cells; find it again to be safe.
    QTextTable *t = nullptr;
    for (QTextTable *candidate : allTables(doc->rootFrame()))
        if (candidate == table || (!tableId.isEmpty() && candidate->format().property(IdProp).toString() == tableId))
            t = candidate;
    if (!t)
        return;
    QList<double> widths = columnWidths(t);
    const int at = qBound(0, left ? column : column + 1, t->columns());
    steps.run([&](QTextCursor &) {
        t->insertColumns(at, 1);
        const double share = 1.0 / t->columns();
        for (double &w : widths)
            w *= (1.0 - share);
        widths.insert(at, share);
        setColumnWidths(t, widths);
        DocumentIO::formatCells(t, m_ctx.style);
    });
    steps.run([&](QTextCursor &c) {
        QTextCursor cc = t->cellAt(0, at).firstCursorPosition();
        cc.setBlockFormat(blockFormat(Block{}, m_ctx.style));
        DocumentIO::fillEmpty(c, cc.block(), dragged, m_ctx.style);
    });
    cleanupColumns();
    updateVisibility();
    scheduleNormalize();
}

void BlockEditor::cleanupColumns()
{
    QTextDocument *doc = document();
    const int caret = textCursor().position();
    bool again = true;
    while (again) {
        again = false;
        for (QTextTable *t : allTables(doc->rootFrame())) {
            if (!DocumentIO::isColumns(t))
                continue;
            QList<double> widths = columnWidths(t);
            for (int col = t->columns() - 1; col >= 0 && t->columns() > 1; --col) {
                const QList<Item> items = DocumentIO::itemsOf(doc, Container{t, 0, col});
                const bool empty = items.size() == 1 && !items.first().isTable() && items.first().block.length() <= 1
                                   && typeOf(items.first().block) == BlockType::Paragraph;
                const QTextTableCell cell = t->cellAt(0, col);
                const bool hasCaret = caret >= cell.firstPosition() && caret <= cell.lastPosition();
                if (empty && !hasCaret) {
                    EditSteps(doc, true).run([&](QTextCursor &) { t->removeColumns(col, 1); });
                    widths.removeAt(col);
                }
            }
            if (t->columns() == 1) {
                unwrapTable(t);
                again = true; // the table list changed
                break;
            }
            if (widths.size() == t->columns() && widths != columnWidths(t)) {
                EditSteps(doc, true).run([&](QTextCursor &) {
                    setColumnWidths(t, widths);
                    DocumentIO::formatCells(t, m_ctx.style);
                });
            }
        }
    }
}

QTextBlock BlockEditor::placeBefore(QTextCursor &c, const QTextBlock &before, const QList<Block> &blocks)
{
    // An empty block left last in its container takes the blocks instead.
    if (before.length() <= 1 && typeOf(before) == BlockType::Paragraph && !nextInContainer(before).isValid()
        && !DocumentIO::tableOf(before.next()))
        return DocumentIO::fillEmpty(c, before, blocks, m_ctx.style);
    return DocumentIO::insertBefore(c, before, blocks, m_ctx.style);
}

void BlockEditor::unwrapTable(QTextTable *table)
{
    QTextDocument *doc = document();
    const Block tb = DocumentIO::tableBlock(table);
    QList<Block> blocks;
    if (tb.type == BlockType::ColumnList) {
        for (const QList<Block> &col : tb.columns)
            blocks += col;
    }
    for (Block &b : blocks)
        b.depth += tb.depth;
    const QTextBlock after = doc->findBlock(table->lastPosition() + 1);
    QTextCursor afterCursor(doc);
    afterCursor.setPosition(after.position());
    const int first = DocumentIO::firstBlockNumber(table), last = DocumentIO::lastBlockNumber(table);
    EditSteps steps(doc, true);
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, first, last, m_ctx.style); });
    if (!blocks.isEmpty()) {
        steps.run([&](QTextCursor &c) {
            // The table's helper blocks go with it; an empty block left
            // last takes the first unwrapped block.
            placeBefore(c, afterCursor.block(), blocks);
        });
    }
}

QList<BlockEditor::ColumnGap> BlockEditor::columnGaps(const QRect &rect) const
{
    QList<ColumnGap> out;
    for (QTextTable *t : allTables(document()->rootFrame())) {
        if (DocumentIO::isSimpleTable(t)) {
            if (!document()->findBlock(t->firstPosition()).isVisible())
                continue;
            const QRectF box = simpleTableBox(t);
            if (box.bottom() < rect.top() || box.top() > rect.bottom())
                continue;
            const double pad = t->format().cellPadding();
            for (int c = 0; c < t->columns(); ++c) {
                const QRectF cr = blockRect(document()->findBlock(t->cellAt(0, c).firstPosition()));
                const double x = cr.left() + cr.width() + pad;
                ColumnGap g{t, c, QRectF(x - 4, box.top(), 8, box.height())};
                g.simple = true;
                out.append(g);
            }
            continue;
        }
        if (!DocumentIO::isColumns(t))
            continue;
        const QRectF tr = tableRect(t);
        if (tr.bottom() < rect.top() || tr.top() > rect.bottom())
            continue;
        // Hidden tables (inside a folded toggle) have no gaps.
        if (!document()->findBlockByNumber(DocumentIO::firstBlockNumber(t)).isVisible()
            && !document()->findBlockByNumber(DocumentIO::lastBlockNumber(t)).isVisible())
            continue;
        const double half = DocumentIO::columnGap(m_ctx.style) / 2;
        const double top = blockRect(document()->findBlockByNumber(DocumentIO::firstBlockNumber(t))).top();
        double bottom = top;
        for (int c = 0; c < t->columns(); ++c) {
            const QTextTableCell cell = t->cellAt(0, c);
            bottom = qMax(bottom, blockRect(document()->findBlock(cell.lastPosition())).bottom());
        }
        for (int c = 0; c + 1 < t->columns(); ++c) {
            const QTextTableCell next = t->cellAt(0, c + 1);
            const double x = blockRect(document()->findBlock(next.firstPosition())).left() - half;
            out.append({t, c, QRectF(x - 6, top, 12, bottom - top)});
        }
    }
    return out;
}

BlockEditor::ColumnGap BlockEditor::columnGapAt(const QPointF &pos) const
{
    for (const ColumnGap &g : columnGaps(QRect(pos.toPoint() - QPoint(1, 1), QSize(2, 2))))
        if (g.rect.contains(pos))
            return g;
    return {};
}
