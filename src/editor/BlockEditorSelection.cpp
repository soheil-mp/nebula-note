// Block selection: Esc selects the block being edited; arrows move it,
// Shift extends it, and block commands act on whole blocks — Notion's
// "blue selection", here a soft gray.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "DocumentIO.h"
#include "Popups.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextTable>

using namespace BlockFormat;
using namespace BlockEditorPrivate;

namespace
{
// The item (block or outermost table in the block's container) holding a block.
QPair<int, int> itemRange(const QTextBlock &b)
{
    if (QTextTable *t = DocumentIO::tableOf(b); DocumentIO::isSimpleTable(t)) {
        // A simple table is selected as a whole.
        return {DocumentIO::firstBlockNumber(t), DocumentIO::lastBlockNumber(t)};
    }
    return {b.blockNumber(), b.blockNumber()};
}
} // namespace

void BlockEditor::startBlockSelection(int first, int last)
{
    m_blockSel = {qMin(first, last), qMax(first, last)};
    m_blockSelAnchor = first;
    setCursorWidth(0);
    QTextCursor c = textCursor();
    c.clearSelection();
    setTextCursor(c);
    if (m_formatBar)
        m_formatBar->dismiss();
    viewport()->update();
}

void BlockEditor::clearBlockSelection()
{
    if (m_blockSel.first < 0)
        return;
    m_blockSel = {-1, -1};
    setCursorWidth(blockTypeIsAtomic(typeOf(textCursor().block())) ? 0 : 2);
    viewport()->update();
}

void BlockEditor::selectRangeInCursor(int first, int last)
{
    QTextDocument *doc = document();
    const QTextBlock a = doc->findBlockByNumber(first), b = doc->findBlockByNumber(last);
    if (!a.isValid() || !b.isValid())
        return;
    // Selections across table cells are reported cell-wise by Qt: commands
    // get the exact range instead, with the caret at its start.
    setTextCursor(QTextCursor(a));
    m_rangeOverride = {first, last};
}

QList<QRectF> BlockEditor::blockSelectionRects() const
{
    QList<QRectF> out;
    if (m_blockSel.first < 0)
        return out;
    const QTextDocument *doc = document();
    const double k = m_ctx.style.size / 16.0;
    for (int n = m_blockSel.first; n <= m_blockSel.second; ++n) {
        const QTextBlock b = doc->findBlockByNumber(n);
        if (!b.isValid() || !b.isVisible())
            continue;
        if (QTextTable *t = DocumentIO::tableOf(b); DocumentIO::isSimpleTable(t)) {
            const QRectF box = simpleTableBox(t);
            if (out.isEmpty() || out.last() != box.adjusted(-4, -4, 4, 4))
                out.append(box.adjusted(-4, -4, 4, 4));
            n = DocumentIO::lastBlockNumber(t);
            continue;
        }
        const QRectF r = blockRect(b);
        const BlockType t = typeOf(b);
        const double top = r.top() - (t == BlockType::Code || t == BlockType::Callout ? padTop(t, m_ctx.style) : 2 * k);
        const double bottom = r.bottom() + (t == BlockType::Code || t == BlockType::Callout ? padBottom(t, m_ctx.style) : 2 * k);
        out.append(QRectF(blockLeft(b) - 4 * k, top, blockRight(b) - blockLeft(b) + 8 * k, bottom - top));
    }
    return out;
}

bool BlockEditor::blockSelectionKey(QKeyEvent *e)
{
    if (m_blockSel.first < 0)
        return false;
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;
    QTextDocument *doc = document();
    auto visibleNeighbour = [&](int from, int dir) {
        // The next visible item in the same container as `from`.
        const QTextBlock fb = doc->findBlockByNumber(from);
        QTextBlock b = dir > 0 ? doc->findBlockByNumber(itemRange(fb).second + 1) : doc->findBlockByNumber(itemRange(fb).first - 1);
        while (b.isValid() && (!b.isVisible() || DocumentIO::isPhantom(b)))
            b = dir > 0 ? b.next() : b.previous();
        return b;
    };
    switch (key) {
    case Qt::Key_Escape:
        clearBlockSelection();
        return true;
    case Qt::Key_Up:
    case Qt::Key_Down: {
        const int dir = key == Qt::Key_Down ? 1 : -1;
        if (ctrl && shift) {
            selectRangeInCursor(m_blockSel.first, m_blockSel.second);
            moveBlocks(dir);
            const auto r = selectedBlockRange();
            startBlockSelection(r.first, r.first + (m_blockSel.second - m_blockSel.first));
            return true;
        }
        const int edge = dir > 0 ? m_blockSel.second : m_blockSel.first;
        const QTextBlock n = visibleNeighbour(edge, dir);
        if (!n.isValid())
            return true;
        const auto ir = itemRange(n);
        if (shift) {
            if (dir > 0)
                m_blockSel.second = qMax(m_blockSel.second, ir.second);
            else
                m_blockSel.first = qMin(m_blockSel.first, ir.first);
        } else {
            m_blockSel = ir;
        }
        viewport()->update();
        ensureBlockVisible(dir > 0 ? m_blockSel.second : m_blockSel.first);
        return true;
    }
    case Qt::Key_Delete:
    case Qt::Key_Backspace: {
        if (isReadOnly())
            return true;
        selectRangeInCursor(m_blockSel.first, m_blockSel.second);
        clearBlockSelection();
        deleteBlocks();
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter: {
        const QTextBlock b = doc->findBlockByNumber(m_blockSel.first);
        clearBlockSelection();
        QTextCursor c(b);
        c.movePosition(QTextCursor::EndOfBlock);
        setTextCursor(c);
        return true;
    }
    case Qt::Key_Tab:
    case Qt::Key_Backtab: {
        if (isReadOnly())
            return true;
        const auto keep = m_blockSel;
        selectRangeInCursor(keep.first, keep.second);
        indentBlocks(key == Qt::Key_Tab && !shift ? 1 : -1);
        startBlockSelection(keep.first, keep.second);
        return true;
    }
    default:
        break;
    }
    if (ctrl && !alt && (key == Qt::Key_D || key == Qt::Key_C || key == Qt::Key_X || key == Qt::Key_Slash || key == Qt::Key_A)) {
        selectRangeInCursor(m_blockSel.first, m_blockSel.second);
        if (key == Qt::Key_D) {
            clearBlockSelection();
            duplicateBlocks();
        } else if (key == Qt::Key_C) {
            QApplication::clipboard()->setMimeData(createMimeDataFromSelection());
            const auto keep = m_blockSel;
            startBlockSelection(keep.first, keep.second);
        } else if (key == Qt::Key_X) {
            if (!isReadOnly()) {
                QApplication::clipboard()->setMimeData(createMimeDataFromSelection());
                clearBlockSelection();
                deleteBlocks();
            }
        } else if (key == Qt::Key_Slash) {
            const QTextBlock b = doc->findBlockByNumber(m_blockSel.first);
            clearBlockSelection();
            showTurnIntoMenu(viewport()->mapToGlobal(QPoint(int(textLeft(b)), int(blockRect(b).bottom() + 4))));
        } else {
            // Ctrl+A: every block.
            startBlockSelection(0, doc->blockCount() - 1);
        }
        return true;
    }
    if (ctrl && (shift || alt) && key >= Qt::Key_0 && key <= Qt::Key_9) {
        selectRangeInCursor(m_blockSel.first, m_blockSel.second);
        clearBlockSelection();
        return false; // the normal shortcut turns the selected blocks into the type
    }
    // Anything else ends the selection and goes to the text.
    if (!e->text().isEmpty() && e->text().at(0).isPrint())
        return true;
    return false;
}

void BlockEditor::ensureBlockVisible(int blockNumber)
{
    const QTextBlock b = document()->findBlockByNumber(blockNumber);
    if (!b.isValid() || !m_scrollArea || !m_scrollArea->widget())
        return;
    const QRectF r = blockRect(b);
    const QPoint top = viewport()->mapTo(m_scrollArea->widget(), r.topLeft().toPoint());
    m_scrollArea->ensureVisible(top.x(), top.y() + int(r.height() / 2), 10, int(r.height() / 2) + 48);
}
