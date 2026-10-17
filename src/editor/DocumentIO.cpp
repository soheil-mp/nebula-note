#include "DocumentIO.h"

#include "ui/Theme.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextTable>

using namespace BlockFormat;

namespace DocumentIO
{

// ---- containers ---------------------------------------------------------------------

QTextTable *tableOf(const QTextBlock &block)
{
    if (!block.isValid())
        return nullptr;
    return QTextCursor(block).currentTable();
}

bool isColumns(const QTextTable *t)
{
    return t && t->format().property(TableKindProp).toString() == QLatin1String("columns");
}

bool isSimpleTable(const QTextTable *t)
{
    return t && !isColumns(t);
}

Container containerOf(const QTextBlock &block)
{
    Container c;
    QTextTable *t = tableOf(block);
    if (!t)
        return c;
    const QTextTableCell cell = t->cellAt(block.position());
    c.table = t;
    c.row = cell.row();
    c.col = cell.column();
    return c;
}

bool sameContainer(const QTextBlock &a, const QTextBlock &b)
{
    return containerOf(a) == containerOf(b);
}

int firstBlockNumber(const QTextFrame *frame)
{
    return frame->document()->findBlock(frame->firstPosition()).blockNumber();
}

int lastBlockNumber(const QTextFrame *frame)
{
    return frame->document()->findBlock(frame->lastPosition()).blockNumber();
}

namespace
{
QTextFrame::iterator containerBegin(const QTextDocument *doc, const Container &c)
{
    if (c.isRoot())
        return doc->rootFrame()->begin();
    return c.table->cellAt(c.row, c.col).begin();
}

QTextFrame::iterator containerEnd(const QTextDocument *doc, const Container &c)
{
    if (c.isRoot())
        return doc->rootFrame()->end();
    return c.table->cellAt(c.row, c.col).end();
}
} // namespace

QList<Item> itemsOf(const QTextDocument *doc, const Container &container)
{
    QList<Item> out;
    const QTextFrame::iterator end = containerEnd(doc, container);
    for (QTextFrame::iterator it = containerBegin(doc, container); it != end; ++it) {
        Item item;
        if (QTextFrame *f = it.currentFrame()) {
            item.table = qobject_cast<QTextTable *>(f);
            if (!item.table)
                continue;
            item.first = firstBlockNumber(f);
            item.last = lastBlockNumber(f);
        } else {
            item.block = it.currentBlock();
            item.first = item.last = item.block.blockNumber();
        }
        out.append(item);
    }
    return out;
}

Item itemFor(const QTextBlock &block, const Container &container)
{
    const int n = block.blockNumber();
    for (const Item &item : itemsOf(block.document(), container))
        if (n >= item.first && n <= item.last)
            return item;
    return {};
}

QTextTable *wholeTableAt(const QTextBlock &block, int last)
{
    QTextTable *found = nullptr;
    QTextTable *t = tableOf(block);
    const int n = block.blockNumber();
    while (t && firstBlockNumber(t) == n && lastBlockNumber(t) <= last) {
        found = t;
        // The parent table, if this one sits inside a cell.
        QTextFrame *parent = t->parentFrame();
        t = qobject_cast<QTextTable *>(parent);
    }
    return found;
}

bool isPhantom(const QTextBlock &block)
{
    if (!block.isValid() || block.length() > 1 || typeOf(block) != BlockType::Paragraph)
        return false;
    const Container c = containerOf(block);
    const QList<Item> items = itemsOf(block.document(), c);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].isTable() || items[i].block != block)
            continue;
        const bool nextIsTable = i + 1 < items.size() && items[i + 1].isTable();
        const bool prevIsTable = i > 0 && items[i - 1].isTable();
        if (i == 0 && nextIsTable)
            return true;
        if (i == items.size() - 1 && prevIsTable)
            return true;
        return false;
    }
    return false;
}

// ---- writing --------------------------------------------------------------------------

double columnGap(const Style &style)
{
    return 46 * style.size / 16.0;
}

QTextBlockFormat cellBlockFormat(const Style &style)
{
    QTextBlockFormat f = blockFormat(Block{}, style); // text line height (1.5× the size)
    f.setTopMargin(0);
    f.setLeftMargin(0);
    f.setRightMargin(0);
    f.setBottomMargin(0);
    return f;
}

void writeContent(QTextCursor &c, const Block &b, const Style &style)
{
    const QString obj(QChar::ObjectReplacementCharacter);
    switch (b.type) {
    case BlockType::Divider:
        c.insertText(obj, dividerFormat());
        return;
    case BlockType::Image:
        c.insertText(obj, imageFormat(b.media, b.width));
        return;
    case BlockType::PageLink:
        c.insertText(obj, pageLinkFormat(b.pageId));
        return;
    default:
        if (blockTypeIsAtomic(b.type)) {
            c.insertText(obj, blockObjectFormat());
            return;
        }
        break;
    }
    for (const Span &s : b.spans) {
        if (s.isObject()) {
            c.insertText(obj, objectSpanFormat(s, style));
            continue;
        }
        QString text = s.text;
        // Paragraph breaks inside a span would split the block.
        text.replace(QLatin1Char('\n'), QChar::LineSeparator);
        text.replace(QChar::ParagraphSeparator, QChar::LineSeparator);
        text.remove(QChar::ObjectReplacementCharacter);
        if (b.type == BlockType::Title)
            text.replace(QChar::LineSeparator, QLatin1Char(' '));
        c.insertText(text, spanFormat(s, b.type, style, b.checked, b.color));
    }
    c.setCharFormat(baseCharFormat(b.type, style, b.checked, b.color));
}

namespace
{
bool isTableType(BlockType t)
{
    return t == BlockType::Table || t == BlockType::ColumnList;
}

void fillBlock(QTextBlock block, const Block &b, const Style &style)
{
    QTextCursor c(block);
    c.setBlockFormat(blockFormat(b, style));
    c.setBlockCharFormat(baseCharFormat(b.type, style, b.checked, b.color));
    c.movePosition(QTextCursor::EndOfBlock);
    writeContent(c, b, style);
    if (b.collapsed)
        setCollapsed(block, true);
}

void resetToParagraph(QTextBlock block, const Style &style)
{
    QTextCursor c(block);
    c.setBlockFormat(blockFormat(Block{}, style));
    c.setBlockCharFormat(baseCharFormat(BlockType::Paragraph, style));
    setCollapsed(block, false);
}

QTextBlock blockAfter(const QTextTable *t)
{
    return t->document()->findBlock(t->lastPosition() + 1);
}

// Fills `fresh` (an empty block) and what follows it with blocks. Tables
// are inserted at the end of the current block; the block Qt adds after a
// table becomes the next fresh block. Returns the last block written and,
// through `trailing`, the fresh block left after a final table (or invalid).
QTextBlock fillFrom(QTextCursor &c, QTextBlock fresh, const QList<Block> &blocks, const Style &style, QTextBlock *trailing)
{
    const QTextDocument *doc = fresh.document();
    QTextBlock cur = fresh;
    bool isFresh = true;
    QTextBlock lastWritten;
    for (const Block &b : blocks) {
        if (isTableType(b.type)) {
            if (isFresh)
                resetToParagraph(cur, style);
            c.setPosition(cur.position() + cur.length() - 1);
            QTextTable *t = insertTable(c, b, style);
            cur = blockAfter(t);
            resetToParagraph(cur, style);
            isFresh = true;
            lastWritten = doc->findBlockByNumber(lastBlockNumber(t));
            continue;
        }
        if (isFresh) {
            fillBlock(cur, b, style);
            isFresh = false;
        } else {
            c.setPosition(cur.position() + cur.length() - 1);
            c.insertBlock(blockFormat(b, style), baseCharFormat(b.type, style, b.checked, b.color));
            writeContent(c, b, style);
            if (b.collapsed)
                setCollapsed(c.block(), true);
            cur = c.block();
        }
        lastWritten = cur;
    }
    if (trailing)
        *trailing = isFresh ? cur : QTextBlock();
    return lastWritten;
}

// Removes an empty block that a table insertion left in front of `next`.
void dropFreshBefore(QTextCursor &c, const QTextBlock &fresh)
{
    if (!fresh.isValid() || fresh.length() > 1)
        return;
    const Container here = containerOf(fresh);
    const QList<Item> items = itemsOf(fresh.document(), here);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].isTable() || items[i].block != fresh)
            continue;
        // Only when a block follows and a table precedes (or nothing does).
        if (i + 1 >= items.size() || items[i + 1].isTable())
            return;
        c.setPosition(fresh.position());
        c.setPosition(items[i + 1].block.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        return;
    }
}
} // namespace

QTextTableFormat tableFormat(const Block &b, const Style &style, const QTextTable *existing)
{
    const auto &col = Theme::colors();
    const double k = style.size / 16.0;
    QTextTableFormat tf = existing ? existing->format() : QTextTableFormat();
    const bool columns = b.type == BlockType::ColumnList;
    tf.setProperty(TableKindProp, columns ? QStringLiteral("columns") : QStringLiteral("table"));
    tf.setProperty(TypeProp, int(b.type));
    tf.setProperty(DepthProp, b.depth);
    if (!b.id.isEmpty())
        tf.setProperty(IdProp, b.id);
    if (!b.syncId.isEmpty())
        tf.setProperty(SyncProp, b.syncId);
    else
        tf.clearProperty(SyncProp);
    if (!b.data.isEmpty())
        tf.setProperty(DataProp, QString::fromUtf8(QJsonDocument(b.data).toJson(QJsonDocument::Compact)));
    else
        tf.clearProperty(DataProp);
    tf.setLeftMargin(b.depth * style.indent());
    tf.setRightMargin(0);
    tf.setBottomMargin(0);
    tf.setPadding(0);
    tf.setAlignment(Qt::AlignLeft);
    const int n = columns ? qMax(1, int(b.columns.size())) : [&] {
        int m = 1;
        for (const auto &row : b.rows)
            m = qMax(m, int(row.size()));
        return m;
    }();
    QList<QTextLength> widths;
    if (columns) {
        tf.setBorder(0);
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        tf.setCellSpacing(0);
        tf.setCellPadding(0);
        tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
        double total = 0;
        for (int i = 0; i < n; ++i)
            total += i < b.widths.size() && b.widths[i] > 0 ? b.widths[i] : 1.0 / n;
        for (int i = 0; i < n; ++i) {
            const double w = i < b.widths.size() && b.widths[i] > 0 ? b.widths[i] : 1.0 / n;
            widths.append(QTextLength(QTextLength::PercentageLength, 100.0 * w / total));
        }
    } else {
        tf.setBorder(1);
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
        tf.setBorderBrush(col.ash);
        tf.setBorderCollapse(true);
        tf.setCellSpacing(0);
        tf.setCellPadding(7 * k);
        const bool fit = b.data.value(QStringLiteral("fit")).toBool();
        if (fit) {
            tf.setWidth(QTextLength(QTextLength::PercentageLength, 100));
            double total = 0;
            for (int i = 0; i < n; ++i)
                total += i < b.widths.size() && b.widths[i] > 0 ? b.widths[i] : 120;
            for (int i = 0; i < n; ++i) {
                const double w = i < b.widths.size() && b.widths[i] > 0 ? b.widths[i] : 120;
                widths.append(QTextLength(QTextLength::PercentageLength, 100.0 * w / total));
            }
        } else {
            tf.setWidth(QTextLength());
            for (int i = 0; i < n; ++i)
                widths.append(QTextLength(QTextLength::FixedLength, i < b.widths.size() && b.widths[i] > 0 ? b.widths[i] : 120 * k));
        }
    }
    tf.setColumnWidthConstraints(widths);
    return tf;
}

namespace
{
double columnTopPadding(const QTextTableCell &cell, const Style &style)
{
    const QTextBlock first = cell.firstCursorPosition().block();
    return first.isValid() ? padTop(typeOf(first), style) : 0;
}
} // namespace

void formatCells(QTextTable *t, const Style &style)
{
    const QJsonObject data = QJsonDocument::fromJson(t->format().property(DataProp).toString().toUtf8()).object();
    const bool columns = isColumns(t);
    const double half = columnGap(style) / 2;
    const bool headerRow = data.value(QStringLiteral("hr")).toBool();
    const bool headerCol = data.value(QStringLiteral("hc")).toBool();
    const QJsonArray shades = data.value(QStringLiteral("shades")).toArray();
    for (int r = 0; r < t->rows(); ++r) {
        for (int c = 0; c < t->columns(); ++c) {
            QTextTableCell cell = t->cellAt(r, c);
            QTextTableCellFormat cf = cell.format().toTableCellFormat();
            if (columns) {
                cf.setLeftPadding(c == 0 ? 0 : half);
                cf.setRightPadding(c == t->columns() - 1 ? 0 : half);
                // Qt drops a cell's first top margin: a box opening the
                // column (callout, code) gets its room from the padding.
                cf.setTopPadding(columnTopPadding(cell, style));
                cf.setBottomPadding(0);
                cf.clearBackground();
            } else {
                const int shade = shades.at(r).toArray().at(c).toInt();
                if (shade > 0)
                    cf.setBackground(Theme::highlightShade(shade).background);
                else if ((headerRow && r == 0) || (headerCol && c == 0))
                    cf.setBackground(QColor(0x22, 0x22, 0x29));
                else
                    cf.clearBackground();
                cf.setVerticalAlignment(QTextCharFormat::AlignTop);
                // The extra leading of a line sits below it: less padding there.
                const double k = style.size / 16.0;
                cf.setTopPadding(7 * k);
                cf.setBottomPadding(4 * k);
                cf.setLeftPadding(9 * k);
                cf.setRightPadding(9 * k);
            }
            cell.setFormat(cf);
        }
    }
}

bool cellsNeedFormat(const QTextTable *t, const Style &style)
{
    const QJsonObject data = QJsonDocument::fromJson(t->format().property(DataProp).toString().toUtf8()).object();
    const bool columns = isColumns(t);
    const bool headerRow = data.value(QStringLiteral("hr")).toBool();
    const bool headerCol = data.value(QStringLiteral("hc")).toBool();
    const QJsonArray shades = data.value(QStringLiteral("shades")).toArray();
    const double half = columnGap(style) / 2;
    for (int r = 0; r < t->rows(); ++r) {
        for (int c = 0; c < t->columns(); ++c) {
            const QTextTableCellFormat cf = t->cellAt(r, c).format().toTableCellFormat();
            if (columns) {
                if (qAbs(cf.leftPadding() - (c == 0 ? 0 : half)) > 0.5 || qAbs(cf.rightPadding() - (c == t->columns() - 1 ? 0 : half)) > 0.5
                    || qAbs(cf.topPadding() - columnTopPadding(t->cellAt(r, c), style)) > 0.5)
                    return true;
                continue;
            }
            const bool wantBg = shades.at(r).toArray().at(c).toInt() > 0 || (headerRow && r == 0) || (headerCol && c == 0);
            if (wantBg != (cf.background().style() != Qt::NoBrush))
                return true;
        }
    }
    return false;
}

QTextTable *insertTable(QTextCursor &c, const Block &b, const Style &style)
{
    const bool columns = b.type == BlockType::ColumnList;
    int rows = 1, cols = 1;
    if (columns) {
        cols = qMax(1, int(b.columns.size()));
    } else {
        rows = qMax(1, int(b.rows.size()));
        for (const auto &row : b.rows)
            cols = qMax(cols, int(row.size()));
    }
    QTextTable *t = c.insertTable(rows, cols, tableFormat(b, style));
    for (int r = 0; r < rows; ++r) {
        for (int col = 0; col < cols; ++col) {
            QTextTableCell cell = t->cellAt(r, col);
            QTextCursor cc = cell.firstCursorPosition();
            if (columns) {
                QList<Block> content = col < b.columns.size() ? b.columns[col] : QList<Block>();
                if (content.isEmpty())
                    content.append(Block{});
                QTextBlock trailing;
                fillFrom(cc, cc.block(), content, style, &trailing);
            } else {
                cc.setBlockFormat(cellBlockFormat(style));
                cc.setBlockCharFormat(baseCharFormat(BlockType::Paragraph, style));
                if (r < b.rows.size() && col < b.rows[r].size()) {
                    Block para;
                    para.spans = b.rows[r][col];
                    writeContent(cc, para, style);
                }
            }
        }
    }
    // Cell formats share storage with each cell's first block: set them last.
    formatCells(t, style);
    return t;
}

void load(QTextDocument *doc, const QList<Block> &blocks, const Style &style)
{
    const bool undo = doc->isUndoRedoEnabled();
    doc->setUndoRedoEnabled(false);
    doc->clear();
    QTextCursor c(doc);
    QList<Block> body = blocks;
    if (body.isEmpty())
        body.append(Block{});
    fillFrom(c, doc->begin(), body, style, nullptr);
    doc->setUndoRedoEnabled(undo);
    doc->setModified(false);
}

// ---- reading ----------------------------------------------------------------------------

Block blockAt(const QTextBlock &block)
{
    Block b = blockProps(block);
    if (blockTypeIsAtomic(b.type))
        return b;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        const QTextCharFormat cf = frag.charFormat();
        if (isInlineObject(cf.objectType())) {
            // Identical neighbouring objects share a fragment: one span each.
            for (int i = 0; i < frag.length(); ++i)
                b.spans.append(spanOf(cf, QString(QChar::ObjectReplacementCharacter), b.type, b.checked));
            continue;
        }
        QString text = frag.text();
        if (cf.objectType() >= QTextFormat::UserObject) {
            text.remove(QChar::ObjectReplacementCharacter);
            if (text.isEmpty())
                continue;
        }
        b.spans.append(spanOf(cf, text, b.type, b.checked));
    }
    b.spans = normalizeSpans(b.spans);
    return b;
}

namespace
{
QList<Block> readItems(const QTextDocument *doc, const Container &container);

Block readTable(const QTextTable *t)
{
    const QTextTableFormat tf = t->format();
    Block b;
    b.type = isColumns(t) ? BlockType::ColumnList : BlockType::Table;
    b.id = tf.property(IdProp).toString();
    b.depth = tf.property(DepthProp).toInt();
    b.syncId = tf.property(SyncProp).toString();
    b.data = QJsonDocument::fromJson(tf.property(DataProp).toString().toUtf8()).object();
    const QList<QTextLength> widths = tf.columnWidthConstraints();
    if (b.type == BlockType::ColumnList) {
        double total = 0;
        for (const QTextLength &w : widths)
            total += w.rawValue();
        for (int c = 0; c < t->columns(); ++c) {
            Container cell{t, 0, c};
            b.columns.append(readItems(t->document(), cell));
            const double w = c < widths.size() && total > 0 ? widths[c].rawValue() / total : 1.0 / t->columns();
            b.widths.append(qRound(w * 10000) / 10000.0);
        }
    } else {
        for (int r = 0; r < t->rows(); ++r) {
            QList<Cell> row;
            for (int c = 0; c < t->columns(); ++c) {
                const QTextTableCell cell = t->cellAt(r, c);
                Cell spans;
                bool firstBlock = true;
                for (QTextFrame::iterator it = cell.begin(); !it.atEnd(); ++it) {
                    const QTextBlock blk = it.currentBlock();
                    if (!blk.isValid())
                        continue;
                    if (!firstBlock) {
                        Span br;
                        br.text = QStringLiteral("\n");
                        spans.append(br);
                    }
                    firstBlock = false;
                    Block para = blockAt(blk);
                    for (Span &s : para.spans)
                        s.text.replace(QChar::LineSeparator, QLatin1Char('\n'));
                    spans += para.spans;
                }
                row.append(normalizeSpans(spans));
            }
            b.rows.append(row);
        }
        // Fitted tables store percentages; keep them as widths on a 720px page.
        for (const QTextLength &w : widths)
            b.widths.append(w.type() == QTextLength::PercentageLength ? qRound(w.rawValue() * 7.2) : qRound(w.rawValue()));
    }
    return b;
}

QList<Block> readItems(const QTextDocument *doc, const Container &container)
{
    QList<Block> out;
    const QList<Item> items = itemsOf(doc, container);
    for (int i = 0; i < items.size(); ++i) {
        const Item &item = items[i];
        if (item.isTable()) {
            out.append(readTable(item.table));
            continue;
        }
        // Helper blocks around tables aren't content.
        const bool empty = item.block.length() <= 1 && typeOf(item.block) == BlockType::Paragraph;
        if (empty && ((i == 0 && i + 1 < items.size() && items[i + 1].isTable())
                      || (i == items.size() - 1 && i > 0 && items[i - 1].isTable())))
            continue;
        out.append(blockAt(item.block));
    }
    return out;
}
} // namespace

Block tableBlock(const QTextTable *table)
{
    return readTable(table);
}

QList<Block> save(const QTextDocument *doc)
{
    QList<Block> out = readItems(doc, Container{});
    for (Block &b : out)
        if (b.type == BlockType::Title)
            b.type = BlockType::Paragraph;
    // A single empty paragraph is an empty page.
    if (out.size() == 1 && out.first().type == BlockType::Paragraph && out.first().spans.isEmpty())
        out.clear();
    return out;
}

QList<Block> range(const QTextDocument *doc, int first, int last)
{
    QList<Block> out;
    const QTextBlock firstBlock = doc->findBlockByNumber(first);
    if (!firstBlock.isValid())
        return out;
    // The container: the first block's, or the parent of a table the range
    // starts with and covers completely.
    Container container = containerOf(firstBlock);
    if (QTextTable *whole = wholeTableAt(firstBlock, last)) {
        const QTextBlock before = doc->findBlock(whole->firstPosition() - 1);
        container = containerOf(before);
    }
    for (const Item &item : itemsOf(doc, container)) {
        if (item.last < first || item.first > last)
            continue;
        if (item.isTable()) {
            if (item.first >= first && item.last <= last) {
                out.append(readTable(item.table));
            } else {
                // A partly covered table: the covered blocks of its cells.
                for (int n = qMax(first, item.first); n <= qMin(last, item.last); ++n) {
                    const QTextBlock b = doc->findBlockByNumber(n);
                    if (!tableOf(b) || tableOf(b) == item.table)
                        out.append(blockAt(b));
                }
            }
            continue;
        }
        out.append(blockAt(item.block));
    }
    return out;
}

// ---- inserting & removing ------------------------------------------------------------

QTextBlock insertAfter(QTextCursor &c, const QTextBlock &after, const QList<Block> &blocks, const Style &style)
{
    if (blocks.isEmpty())
        return after;
    const QTextDocument *doc = after.document();
    QTextBlock cur = after;
    QTextBlock lastWritten = after;
    QTextBlock fresh;
    for (const Block &b : blocks) {
        if (isTableType(b.type)) {
            if (fresh.isValid()) {
                // Two tables in a row keep the empty block Qt needs between them.
                cur = fresh;
                fresh = QTextBlock();
            }
            c.setPosition(cur.position() + cur.length() - 1);
            QTextTable *t = insertTable(c, b, style);
            cur = blockAfter(t);
            resetToParagraph(cur, style);
            fresh = cur;
            lastWritten = doc->findBlockByNumber(lastBlockNumber(t));
            continue;
        }
        if (fresh.isValid()) {
            fillBlock(fresh, b, style);
            cur = fresh;
            fresh = QTextBlock();
        } else {
            c.setPosition(cur.position() + cur.length() - 1);
            c.insertBlock(blockFormat(b, style), baseCharFormat(b.type, style, b.checked, b.color));
            writeContent(c, b, style);
            setCollapsed(c.block(), b.collapsed);
            cur = c.block();
        }
        lastWritten = cur;
    }
    if (fresh.isValid())
        dropFreshBefore(c, fresh);
    return lastWritten;
}

QTextBlock insertBefore(QTextCursor &c, const QTextBlock &before, const QList<Block> &blocks, const Style &style)
{
    if (blocks.isEmpty())
        return before;
    const Container container = containerOf(before);
    const QList<Item> items = itemsOf(before.document(), container);
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].isTable() || items[i].block != before)
            continue;
        if (i > 0 && !items[i - 1].isTable())
            return insertAfter(c, items[i - 1].block, blocks, style);
        break;
    }
    // First in its container, or right after a table: open a fresh block above.
    const QTextDocument *doc = before.document();
    const int pos = before.position();
    const QTextBlockFormat keep = before.blockFormat();
    const QTextCharFormat keepChar = QTextCursor(before).blockCharFormat();
    const int keepState = before.userState();
    c.setPosition(pos);
    c.insertBlock();
    QTextBlock fresh = doc->findBlock(pos);
    QTextBlock moved = fresh.next();
    {
        QTextCursor mc(moved);
        mc.setBlockFormat(keep);
        mc.setBlockCharFormat(keepChar);
        moved.setUserState(keepState);
    }
    resetToParagraph(fresh, style);
    QTextBlock trailing;
    const QTextBlock last = fillFrom(c, fresh, blocks, style, &trailing);
    if (trailing.isValid())
        dropFreshBefore(c, trailing);
    return last;
}

QTextBlock fillEmpty(QTextCursor &c, QTextBlock empty, const QList<Block> &blocks, const Style &style)
{
    if (blocks.isEmpty())
        return empty;
    QTextBlock trailing;
    const QTextBlock last = fillFrom(c, empty, blocks, style, &trailing);
    if (trailing.isValid())
        dropFreshBefore(c, trailing);
    return last;
}

void removeRange(QTextCursor &c, int first, int last, const Style &style)
{
    QTextDocument *doc = c.document();
    const QTextBlock firstBlock = doc->findBlockByNumber(first);
    if (!firstBlock.isValid())
        return;
    Container container = containerOf(firstBlock);
    if (QTextTable *whole = wholeTableAt(firstBlock, last))
        container = containerOf(doc->findBlock(whole->firstPosition() - 1));
    const QList<Item> items = itemsOf(doc, container);
    int a = -1, b = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].last < first || items[i].first > last)
            continue;
        if (a < 0)
            a = i;
        b = i;
    }
    if (a < 0)
        return;
    struct Saved {
        QTextBlockFormat format;
        QTextCharFormat charFormat;
        int state = -1;
        bool valid = false;
    };
    auto save = [](const QTextBlock &blk) {
        Saved s;
        if (!blk.isValid())
            return s;
        s.format = blk.blockFormat();
        s.charFormat = QTextCursor(blk).blockCharFormat();
        s.state = blk.userState();
        s.valid = true;
        return s;
    };
    auto restore = [](QTextBlock blk, const Saved &s) {
        if (!blk.isValid() || !s.valid)
            return;
        QTextCursor bc(blk);
        bc.setBlockFormat(s.format);
        bc.setBlockCharFormat(s.charFormat);
        blk.setUserState(s.state);
    };
    const Item *prev = a > 0 ? &items[a - 1] : nullptr;   // always a block when the range starts with a table
    const Item *next = b + 1 < items.size() ? &items[b + 1] : nullptr;
    const Saved savedPrev = prev && !prev->isTable() ? save(prev->block) : Saved{};
    const Saved savedNext = next && !next->isTable() ? save(next->block) : Saved{};
    const Item &firstItem = items[a];
    const Item &lastItem = items[b];
    const int firstStart = firstItem.isTable() ? firstItem.table->firstPosition() - 1 : firstItem.block.position();
    const int lastEnd = lastItem.isTable() ? lastItem.table->lastPosition() + 1
                                           : lastItem.block.position() + lastItem.block.length() - 1;

    // A table that was first in its container leaves no empty helper behind.
    const bool prevIsHelper = prev && !prev->isTable() && a == 1 && firstItem.isTable() && prev->block.length() <= 1
                              && typeOf(prev->block) == BlockType::Paragraph;
    if (next && !next->isTable() && prevIsHelper) {
        c.setPosition(prev->block.position());
        c.setPosition(next->block.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        restore(doc->findBlock(prev->block.position()), savedNext);
        return;
    }
    if (next && !next->isTable()) {
        c.setPosition(firstStart);
        c.setPosition(next->block.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        if (firstItem.isTable() && prev) {
            // The block before the table merged with the one after: split them again.
            c.setPosition(firstStart);
            c.insertBlock();
            const QTextBlock p = doc->findBlock(firstStart);
            restore(p, savedPrev);
            restore(p.next(), savedNext);
        } else {
            restore(doc->findBlock(firstStart), savedNext);
        }
        return;
    }
    // Nothing (or a table) follows: the last removed item must be a block.
    if (prev && !prev->isTable()) {
        const int prevEnd = prev->block.position() + prev->block.length() - 1;
        c.setPosition(prevEnd);
        c.setPosition(lastEnd, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        restore(doc->findBlock(prevEnd), savedPrev);
        return;
    }
    // The whole container (or up to a table): leave one empty paragraph.
    c.setPosition(firstStart);
    c.setPosition(lastEnd, QTextCursor::KeepAnchor);
    c.removeSelectedText();
    resetToParagraph(doc->findBlock(firstStart), style);
}

} // namespace DocumentIO
