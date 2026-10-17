#pragma once

#include "BlockFormat.h"

#include <QTextBlock>
#include <QTextTableFormat>

class QTextCursor;
class QTextDocument;
class QTextFrame;
class QTextTable;

// Converts between Nebula's block model and a QTextDocument (the page body;
// the title lives in its own field).
//
// Column lists and simple tables are QTextTables. A table's cells hold
// blocks of their own (columns hold any blocks, simple-table cells one
// paragraph each), so "the blocks after this one" always means the blocks
// after it in the same container — the root frame or one table cell. Qt
// keeps a block before and after every table; an empty block first in a
// container right before a table (or last, right after one) is a helper
// that isn't saved.
namespace DocumentIO
{
using BlockFormat::Style;

// ---- containers -------------------------------------------------------------

QTextTable *tableOf(const QTextBlock &block); // innermost table holding the block
bool isColumns(const QTextTable *t);
bool isSimpleTable(const QTextTable *t);

struct Container {
    const QTextTable *table = nullptr; // nullptr: the root frame
    int row = -1;
    int col = -1;
    bool isRoot() const { return !table; }
    bool operator==(const Container &o) const = default;
};
Container containerOf(const QTextBlock &block);
bool sameContainer(const QTextBlock &a, const QTextBlock &b);
int firstBlockNumber(const QTextFrame *frame);
int lastBlockNumber(const QTextFrame *frame);

// One item of a container: a block, or a table with its block range.
struct Item {
    QTextBlock block;
    QTextTable *table = nullptr;
    int first = -1;
    int last = -1;
    bool isTable() const { return table != nullptr; }
};
QList<Item> itemsOf(const QTextDocument *doc, const Container &container);
// The item of `container` that holds `block` (the block, or the table inside
// the container that contains it). Invalid when the block is elsewhere.
Item itemFor(const QTextBlock &block, const Container &container);
// The outermost table whose first block is `block` and that lies within the
// block range first…last (for ranges that select whole tables).
QTextTable *wholeTableAt(const QTextBlock &block, int last);
// Helper blocks Qt keeps around tables (never saved).
bool isPhantom(const QTextBlock &block);

// ---- conversion ---------------------------------------------------------------

void load(QTextDocument *doc, const QList<Block> &blocks, const Style &style);
QList<Block> save(const QTextDocument *doc);

// One text or object block with its content.
Block blockAt(const QTextBlock &block);
// A column list or simple table.
Block tableBlock(const QTextTable *table);
// Blocks numbered first … last (inclusive) in one container; tables lying
// wholly inside the range come back as table blocks.
QList<Block> range(const QTextDocument *doc, int first, int last);

// Writes a block's inline content into the (empty) block under the cursor.
void writeContent(QTextCursor &cursor, const Block &b, const Style &style);
// Inserts blocks (tables included) after `after`, in its container; the
// caller owns the edit block. Returns the last inserted block (for a table,
// its last cell block).
QTextBlock insertAfter(QTextCursor &cursor, const QTextBlock &after, const QList<Block> &blocks, const Style &style);
// Inserts blocks before `before`, in its container.
QTextBlock insertBefore(QTextCursor &cursor, const QTextBlock &before, const QList<Block> &blocks, const Style &style);
// Fills an empty block (and what follows it in its container) with blocks.
QTextBlock fillEmpty(QTextCursor &cursor, QTextBlock empty, const QList<Block> &blocks, const Style &style);
// Removes the blocks first … last of one container (whole tables when the
// range covers them), keeping the neighbours' formats.
void removeRange(QTextCursor &cursor, int first, int last, const Style &style);

// Tables.
QTextTable *insertTable(QTextCursor &cursor, const Block &b, const Style &style);
QTextTableFormat tableFormat(const Block &b, const Style &style, const QTextTable *existing = nullptr);
void formatCells(QTextTable *table, const Style &style);
// Cell paddings and backgrounds no longer match the table's settings (they
// share storage with the cells' first blocks, which edits may restyle).
bool cellsNeedFormat(const QTextTable *table, const Style &style);
QTextBlockFormat cellBlockFormat(const Style &style);
// Column gap (px) between columns of a column list.
double columnGap(const Style &style);
} // namespace DocumentIO
