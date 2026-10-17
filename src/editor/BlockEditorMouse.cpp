#include "BlockEditor.h"
#include "BlockEditorPrivate.h"
#include <QTextTable>
#include <climits>

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

// ---- mouse -------------------------------------------------------------------------------

BlockEditor::HitInfo BlockEditor::hitTest(const QPoint &pos) const
{
    HitInfo h;
    // Column resize bars between columns.
    if (!isReadOnly()) {
        const ColumnGap gap = columnGapAt(pos);
        if (gap.table) {
            h.hit = Hit::ColumnGap;
            return h;
        }
        // The + bars under and beside a hovered simple table.
        if (m_hoverTable) {
            const QRectF box = simpleTableBox(m_hoverTable);
            if (QRectF(box.left(), box.bottom() + 1, box.width(), 16).contains(pos)) {
                h.hit = Hit::TableAddRow;
                return h;
            }
            if (QRectF(box.right() + 1, box.top(), 16, box.height()).contains(pos)) {
                h.hit = Hit::TableAddColumn;
                return h;
            }
        }
    }
    const QTextBlock blk = blockAt(pos);
    if (!blk.isValid())
        return h;
    h.block = blk.blockNumber();
    const BlockType t = typeOf(blk);
    const double k = m_ctx.style.size / 16.0;
    // A simple table is one item: its handles sit by its first row.
    if (QTextTable *table = DocumentIO::tableOf(blk); table && DocumentIO::isSimpleTable(table)) {
        const QRectF tr = tableRect(table);
        const QTextBlock firstCell = document()->findBlockByNumber(DocumentIO::firstBlockNumber(table));
        const double y = firstLineMid(firstCell);
        h.block = firstCell.blockNumber();
        if (pos.x() < tr.left() && pos.x() > tr.left() - 52 && qAbs(pos.y() - y) < 14) {
            h.hit = pos.x() < tr.left() - 27 ? Hit::Plus : Hit::Handle;
            return h;
        }
        h.block = blk.blockNumber();
    }
    const double mid = firstLineMid(blk);
    const double left = blockLeft(blk);
    const QRectF br = blockRect(blk);

    // The comment bubble at the right edge.
    if (m_openComments.value(idOf(blk)) > 0 && commentBubbleRect(blk).adjusted(-4, -4, 4, 4).contains(pos)) {
        h.hit = Hit::CommentBubble;
        h.href = idOf(blk);
        return h;
    }

    // Handles in the gutter.
    if (!DocumentIO::isSimpleTable(DocumentIO::tableOf(blk)) && pos.x() < left && pos.x() > left - 52
        && qAbs(pos.y() - (t == BlockType::Code ? br.top() - padTop(t, m_ctx.style) + 18 * k : mid)) < 14) {
        h.hit = pos.x() < left - 27 ? Hit::Plus : Hit::Handle;
        return h;
    }
    const double inset = m_ctx.style.indent();
    if (blockTypeIsHeading(t) && toggleableOf(blk) && pos.x() >= left && pos.x() < left + inset && qAbs(pos.y() - mid) < 14 * k) {
        h.hit = Hit::ToggleArrow;
        return h;
    }
    if (t == BlockType::Todo && pos.x() >= left && pos.x() < left + inset && qAbs(pos.y() - mid) < 12 * k) {
        h.hit = Hit::Checkbox;
        return h;
    }
    if (t == BlockType::Toggle && pos.x() >= left && pos.x() < left + inset && qAbs(pos.y() - mid) < 12 * k) {
        h.hit = Hit::ToggleArrow;
        return h;
    }
    if (t == BlockType::Callout && pos.x() >= left + 10 * k && pos.x() < left + 46 * k && pos.y() >= br.top() - 6
        && pos.y() < br.top() + 30 * k) {
        h.hit = Hit::CalloutIcon;
        return h;
    }
    if (t == BlockType::Code) {
        const double top = br.top() - padTop(t, m_ctx.style);
        if (pos.y() >= top && pos.y() < top + 26 * k) {
            if (pos.x() >= left + 10 && pos.x() < left + 150)
                h.hit = Hit::CodeLanguage;
            else if (pos.x() > blockRight(blk) - 70 && pos.x() <= blockRight(blk))
                h.hit = Hit::CodeCopy;
            if (h.hit != Hit::None)
                return h;
        }
    }
    // Block objects (equations, buttons, bookmarks…): the whole card.
    if (blockTypeIsAtomic(t) && t != BlockType::Divider && t != BlockType::Image && t != BlockType::PageLink) {
        const int part = objectPartAt(blk, pos);
        if (part >= 0) {
            h.hit = Hit::Object;
            h.part = part;
            h.position = blk.position();
            return h;
        }
    }
    // Objects and links under the pointer.
    const int docPos = document()->documentLayout()->hitTest(QPointF(pos) + QPointF(horizontalScrollBar()->value(), verticalScrollBar()->value()),
                                                             Qt::ExactHit);
    if (docPos >= 0) {
        QTextCursor c(document());
        c.setPosition(docPos);
        c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        const QTextCharFormat cf = c.charFormat();
        const int ot = cf.objectType();
        if (ot == PageLinkObject) {
            h.hit = Hit::PageLink;
            h.pageId = cf.property(PageIdProp).toString();
            h.position = docPos;
            return h;
        }
        if (ot == MentionObject) {
            h.hit = Hit::Mention;
            h.pageId = cf.property(PageIdProp).toString();
            h.href = cf.property(MentionBlockProp).toString(); // a block inside it
            h.position = docPos;
            return h;
        }
        if (ot == InlineEquationObject) {
            h.hit = Hit::InlineEquation;
            h.position = docPos;
            return h;
        }
        if (ot == DateObject) {
            h.hit = Hit::Date;
            h.position = docPos;
            return h;
        }
        if (ot == ImageObject) {
            h.position = docPos;
            const QRectF ir = objectRect(blk);
            const double x0 = ir.left(), x1 = ir.right();
            if (qAbs(pos.x() - x0) < 10) {
                h.hit = Hit::ImageEdge;
                h.edge = -1;
            } else if (qAbs(pos.x() - x1) < 10) {
                h.hit = Hit::ImageEdge;
                h.edge = 1;
            }
            return h;
        }
        const QString href = anchorAt(pos);
        if (!href.isEmpty()) {
            h.hit = Hit::Link;
            h.href = href;
            return h;
        }
    }
    // Page link rows are clickable across their width.
    if (t == BlockType::PageLink && pos.x() >= textLeft(blk) && pos.y() >= br.top() && pos.y() <= br.bottom()) {
        h.hit = Hit::PageLink;
        h.pageId = blockProps(blk).pageId;
    }
    return h;
}

void BlockEditor::mousePressEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    if (e->button() != Qt::LeftButton) {
        QTextEdit::mousePressEvent(e);
        return;
    }
    closeSlashMenu();
    closeMentionMenu();
    m_rangeOverride = {-1, -1};
    const HitInfo h = hitTest(pos);
    QTextDocument *doc = document();
    QTextBlock blk = doc->findBlockByNumber(h.block);
    clearBlockSelection();
    // A drag that starts outside the text draws a selection box.
    if (h.hit == Hit::None && !isReadOnly()) {
        const QTextBlock near = blockAt(pos);
        const bool outside = !near.isValid() || pos.x() < blockLeft(near) - 52 || pos.x() > blockRight(near) + 10
                             || (pos.x() < textLeft(near) - 4 && !DocumentIO::tableOf(near) && typeOf(near) != BlockType::Image);
        if (outside && pos.x() < gutter() - 4) {
            m_bandSelecting = true;
            m_bandStart = m_bandEnd = pos;
            return;
        }
    }
    if (h.hit == Hit::ColumnGap) {
        const ColumnGap gap = columnGapAt(pos);
        if (gap.table) {
            m_resizeTable = gap.table;
            m_resizeColumn = gap.column;
            m_resizeStartX = pos.x();
            m_resizeStartWidths = gap.simple ? simpleTableWidths(gap.table) : columnWidths(gap.table);
            m_resizeTotalWidth = gap.simple ? 1.0 : tableRect(gap.table).width();
            m_resizeSimple = gap.simple;
            m_resizeJoin = false;
        }
        return;
    }
    if (h.hit == Hit::TableAddRow || h.hit == Hit::TableAddColumn) {
        if (QTextTable *t = m_hoverTable) {
            if (h.hit == Hit::TableAddRow) {
                tableInsertRow(t, t->rows());
                moveToCell(t, t->rows() - 1, 0);
            } else {
                tableInsertColumn(t, t->columns());
                moveToCell(t, 0, t->columns() - 1);
            }
            setFocus();
        }
        return;
    }
    switch (h.hit) {
    case Hit::Checkbox: {
        if (isReadOnly())
            return;
        Block props = blockProps(blk);
        props.checked = !props.checked;
        QTextCursor c(blk);
        c.beginEditBlock();
        setBlockProps(blk, props);
        c.endEditBlock();
        if (props.checked && !Settings::instance().reduceMotion) {
            Spark sp{blk.blockNumber(), {}};
            sp.clock.start();
            m_sparks.append(sp);
            startAnimTick();
        }
        return;
    }
    case Hit::ToggleArrow: {
        if (!foldsChildren(blk))
            return;
        const bool open = isCollapsed(blk);
        setCollapsed(blk, !open);
        updateVisibility();
        ToggleAnim anim{blk.blockNumber(), open, {}};
        anim.clock.start();
        m_toggleAnims.append(anim);
        startAnimTick();
        scheduleSave();
        // Opening an empty toggle gives it a first child to write in.
        if (open && subtreeEnd(blk) == blk.blockNumber() && !isReadOnly()) {
            Block child;
            child.depth = depthOf(blk) + 1;
            insertNewBlockAfter(blk, child);
        }
        return;
    }
    case Hit::Plus: {
        if (isReadOnly())
            return;
        Block p;
        p.depth = depthOf(blk);
        if (blk.text().isEmpty() && typeOf(blk) == BlockType::Paragraph) {
            QTextCursor c(blk);
            setTextCursor(c);
        } else {
            insertNewBlockAfter(blk, p);
        }
        setFocus();
        textCursor().insertText(QStringLiteral("/"));
        openSlashMenu();
        return;
    }
    case Hit::Handle: {
        m_pressOnHandle = true;
        m_pressPos = pos;
        m_dragRange = {h.block, h.block};
        // A simple table moves as a whole.
        if (QTextTable *table = DocumentIO::tableOf(blk); table && DocumentIO::isSimpleTable(table))
            m_dragRange = {DocumentIO::firstBlockNumber(table), DocumentIO::lastBlockNumber(table)};
        return;
    }
    case Hit::CalloutIcon:
        if (!isReadOnly())
            emit iconRequested(h.block, viewport()->mapToGlobal(QPoint(int(blockLeft(blk) + 10), int(blockRect(blk).top() + 26))));
        return;
    case Hit::CodeLanguage:
        if (!isReadOnly())
            showLanguageMenu(h.block, viewport()->mapToGlobal(pos + QPoint(0, 12)));
        return;
    case Hit::CodeCopy: {
        QString code = blk.text();
        code.replace(QChar::LineSeparator, QLatin1Char('\n'));
        QApplication::clipboard()->setText(code);
        emit toast(tr("Code copied"));
        return;
    }
    case Hit::PageLink:
    case Hit::Mention:
        if (!(e->modifiers() & Qt::ShiftModifier)) {
            flush();
            if (!h.href.isEmpty())
                emit openBlock(h.pageId, h.href);
            else
                emit openPage(h.pageId);
            return;
        }
        break;
    case Hit::Object:
        if (!(e->modifiers() & Qt::ShiftModifier)) {
            setTextCursor(QTextCursor(blk));
            activateObject(blk, h.part, e->globalPosition().toPoint());
            return;
        }
        break;
    case Hit::InlineEquation:
        editInlineEquation(h.position);
        return;
    case Hit::CommentBubble:
        emit commentClicked(h.href, QString(), e->globalPosition().toPoint() + QPoint(-300, 14));
        return;
    case Hit::Date:
        editDate(h.position, e->globalPosition().toPoint() + QPoint(0, 12));
        return;
    case Hit::Link:
        if (e->modifiers() & Qt::ControlModifier) {
            static const QRegularExpression own(QStringLiteral("^nebula://page/([A-Za-z0-9_-]+)(?:#([A-Za-z0-9_-]+))?$"));
            if (const auto m = own.match(h.href); m.hasMatch()) {
                flush();
                if (m.captured(2).isEmpty())
                    emit openPage(m.captured(1));
                else
                    emit openBlock(m.captured(1), m.captured(2));
            } else {
                QDesktopServices::openUrl(QUrl(h.href));
            }
            return;
        }
        break;
    case Hit::ImageEdge: {
        if (isReadOnly())
            break;
        QTextCursor c(doc);
        c.setPosition(h.position);
        c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        const QSizeF sz = m_objects->intrinsicSize(doc, h.position, c.charFormat());
        m_resizeImagePos = h.position;
        m_resizeEdge = h.edge;
        m_resizeImageStartX = pos.x();
        m_resizeStartWidth = sz.width();
        return;
    }
    default:
        break;
    }
    if (h.position >= 0 && typeOf(blk) == BlockType::Image) {
        m_ctx.selectedImage = h.position;
        viewport()->update();
    } else if (m_ctx.selectedImage >= 0) {
        m_ctx.selectedImage = -1;
        viewport()->update();
    }
    QTextEdit::mousePressEvent(e);
    // Commented text opens its thread.
    {
        const QTextCursor at = cursorForPosition(pos);
        QTextCursor probe(document());
        probe.setPosition(at.position());
        probe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        const QString thread = probe.charFormat().property(CommentProp).toString();
        if (!thread.isEmpty() && !(e->modifiers() & Qt::ShiftModifier))
            emit commentClicked(idOf(at.block()), thread, e->globalPosition().toPoint() + QPoint(0, 16));
    }
}

void BlockEditor::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint pos = e->position().toPoint();
    QTextDocument *doc = document();

    if (m_bandSelecting && (e->buttons() & Qt::LeftButton)) {
        m_bandEnd = pos;
        // Top-level items the box touches.
        const double top = qMin(m_bandStart.y(), m_bandEnd.y()), bottom = qMax(m_bandStart.y(), m_bandEnd.y());
        int first = -1, last = -1;
        for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
            if (!b.isVisible())
                continue;
            QRectF r = blockRect(b);
            int itemFirst = b.blockNumber(), itemLast = b.blockNumber();
            if (QTextTable *t = DocumentIO::wholeTableAt(b, INT_MAX); t && DocumentIO::containerOf(doc->findBlock(t->firstPosition() - 1)).isRoot()) {
                r = tableRect(t);
                itemLast = DocumentIO::lastBlockNumber(t);
            } else if (!DocumentIO::containerOf(b).isRoot()) {
                continue;
            }
            if (r.bottom() >= top && r.top() <= bottom) {
                if (first < 0)
                    first = itemFirst;
                last = itemLast;
            }
            b = doc->findBlockByNumber(itemLast);
        }
        if (first >= 0 && qAbs(m_bandEnd.y() - m_bandStart.y()) > 3)
            startBlockSelection(first, last);
        viewport()->update();
        return;
    }
    if (m_resizeTable && (e->buttons() & Qt::LeftButton)) {
        // Neighbouring columns trade width; neither gets narrower than 10%.
        QList<double> widths = m_resizeStartWidths;
        const int c = m_resizeColumn;
        if (m_resizeSimple && c >= 0 && c < widths.size()) {
            // Simple tables: the column grows on its own (fitted ones trade with a neighbour).
            const double dx = pos.x() - m_resizeStartX;
            const bool fit = tableData(m_resizeTable).value(QStringLiteral("fit")).toBool();
            if (fit && c + 1 < widths.size()) {
                const double pair = widths[c] + widths[c + 1];
                widths[c] = qBound(40.0, widths[c] + dx, pair - 40);
                widths[c + 1] = pair - widths[c];
            } else {
                widths[c] = qMax(40.0, widths[c] + dx);
            }
            QTextCursor uc(document());
            if (m_resizeJoin)
                uc.joinPreviousEditBlock();
            else
                uc.beginEditBlock();
            setSimpleTableWidths(m_resizeTable, widths);
            uc.endEditBlock();
            m_resizeJoin = true;
        } else if (c >= 0 && c + 1 < widths.size() && m_resizeTotalWidth > 0) {
            const double delta = (pos.x() - m_resizeStartX) / m_resizeTotalWidth;
            const double pair = widths[c] + widths[c + 1];
            widths[c] = qBound(0.1, widths[c] + delta, pair - 0.1);
            widths[c + 1] = pair - widths[c];
            // One undo step for the whole drag.
            QTextCursor uc(document());
            if (m_resizeJoin)
                uc.joinPreviousEditBlock();
            else
                uc.beginEditBlock();
            setColumnWidths(m_resizeTable, widths);
            uc.endEditBlock();
            m_resizeJoin = true;
        }
        return;
    }
    if (m_resizeImagePos >= 0 && (e->buttons() & Qt::LeftButton)) {
        // Images grow symmetrically from the centre.
        const double dx = (pos.x() - m_resizeImageStartX) * m_resizeEdge * 2;
        const int w = qMax(60, int(m_resizeStartWidth + dx));
        QTextCursor c(doc);
        c.setPosition(m_resizeImagePos);
        c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QTextCharFormat cf = c.charFormat();
        if (cf.property(WidthProp).toInt() != w) {
            cf.setProperty(WidthProp, w);
            c.setCharFormat(cf);
        }
        return;
    }
    if (m_pressOnHandle && (e->buttons() & Qt::LeftButton)) {
        if (!m_draggingBlocks && (pos - m_pressPos).manhattanLength() > 4) {
            m_draggingBlocks = true;
            // Drag the whole selection when the handle belongs to it.
            const auto sel = selectedBlockRange();
            if (textCursor().hasSelection() && m_dragRange.first >= sel.first && m_dragRange.first <= sel.second)
                m_dragRange = sel;
            m_dragRange = withChildren(m_dragRange);
            viewport()->setCursor(Qt::ClosedHandCursor);
            if (m_scrollArea)
                m_autoScroll.start();
        }
        if (m_draggingBlocks) {
            DropTarget t = dropTargetAt(pos);
            // Can't drop into itself (or right where it already is).
            if (t.block >= m_dragRange.first && t.block <= m_dragRange.second)
                t = DropTarget{};
            if (t.where == DropTarget::After && t.block == m_dragRange.first - 1 && t.depth == itemDepth(doc->findBlockByNumber(m_dragRange.first)))
                t = DropTarget{};
            m_drop = t;
            viewport()->update();
        }
        return;
    }

    if (!(e->buttons() & Qt::LeftButton)) {
        const HitInfo h = hitTest(pos);
        const int hoverBlock = (pos.x() >= 0 && pos.x() <= width()) ? h.block : -1;
        if (hoverBlock != m_hoverBlock) {
            m_hoverBlock = hoverBlock;
            m_handleFade.stop();
            m_handleFade.start();
        }
        if (h.hit != m_hoverHit) {
            m_hoverHit = h.hit;
            viewport()->update();
        }
        const ColumnGap gap = h.hit == Hit::ColumnGap ? columnGapAt(pos) : ColumnGap{};
        if (gap.table != m_hoverGap.table || gap.column != m_hoverGap.column) {
            m_hoverGap = gap;
            viewport()->update();
        }
        // The simple table under the pointer shows its + bars.
        QTextTable *hoverTable = nullptr;
        if (h.hit == Hit::TableAddRow || h.hit == Hit::TableAddColumn)
            hoverTable = m_hoverTable;
        else if (gap.table && gap.simple)
            hoverTable = gap.table;
        else if (h.block >= 0) {
            QTextTable *t = DocumentIO::tableOf(doc->findBlockByNumber(h.block));
            if (DocumentIO::isSimpleTable(t) && simpleTableBox(t).adjusted(-60, 0, 20, 20).contains(pos))
                hoverTable = t;
        }
        if (hoverTable != m_hoverTable) {
            m_hoverTable = hoverTable;
            viewport()->update();
        }
        const int hoveredObject = (h.hit == Hit::Object || h.hit == Hit::InlineEquation || h.hit == Hit::Date) ? h.position : -1;
        const int hoveredPart = h.hit == Hit::Object ? h.part : -1;
        if (hoveredObject != m_ctx.hoveredObject || hoveredPart != m_ctx.hoveredPart) {
            m_ctx.hoveredObject = hoveredObject;
            m_ctx.hoveredPart = hoveredPart;
            viewport()->update();
        }
        QString hoveredPage = (h.hit == Hit::PageLink || h.hit == Hit::Mention) ? h.pageId : QString();
        const int hoveredImage = h.position >= 0 && typeOf(doc->findBlockByNumber(h.block)) == BlockType::Image ? h.position : -1;
        if (hoveredPage != m_ctx.hoveredPageId || hoveredImage != m_ctx.hoveredImage) {
            m_ctx.hoveredPageId = hoveredPage;
            m_ctx.hoveredImage = hoveredImage;
            viewport()->update();
        }
        Qt::CursorShape shape = Qt::IBeamCursor;
        switch (h.hit) {
        case Hit::Checkbox:
        case Hit::ToggleArrow:
        case Hit::Plus:
        case Hit::CalloutIcon:
        case Hit::CodeLanguage:
        case Hit::CodeCopy:
        case Hit::PageLink:
        case Hit::Mention:
        case Hit::InlineEquation:
        case Hit::Date:
        case Hit::CommentBubble:
            shape = Qt::PointingHandCursor;
            break;
        case Hit::Object: {
            const BlockType ot = typeOf(doc->findBlockByNumber(h.block));
            const bool clickable = ot != BlockType::Breadcrumb || h.part >= 0;
            shape = clickable && (ot != BlockType::Button || h.part == 1) && (ot != BlockType::TableOfContents || h.part >= 0)
                        ? Qt::PointingHandCursor
                        : Qt::ArrowCursor;
            break;
        }
        case Hit::Handle:
            shape = Qt::OpenHandCursor;
            break;
        case Hit::ImageEdge:
        case Hit::ColumnGap:
            shape = Qt::SizeHorCursor;
            break;
        case Hit::TableAddRow:
        case Hit::TableAddColumn:
            shape = Qt::PointingHandCursor;
            break;
        case Hit::Link:
            shape = (e->modifiers() & Qt::ControlModifier) ? Qt::PointingHandCursor : Qt::IBeamCursor;
            break;
        default:
            if (hoveredImage >= 0)
                shape = Qt::ArrowCursor;
            break;
        }
        if (viewport()->cursor().shape() != shape)
            viewport()->setCursor(shape);
        if (h.hit == Hit::Link)
            setToolTip(tr("Ctrl+click to open %1").arg(h.href));
        else if (h.hit == Hit::Handle)
            setToolTip(tr("Drag to move · Click for options"));
        else if (h.hit == Hit::Plus)
            setToolTip(tr("Click to add a block below"));
        else
            setToolTip(QString());
    } else if (m_scrollArea && !m_autoScroll.isActive()) {
        m_autoScroll.start();
    }
    QTextEdit::mouseMoveEvent(e);
}

void BlockEditor::mouseReleaseEvent(QMouseEvent *e)
{
    m_autoScroll.stop();
    if (m_bandSelecting) {
        m_bandSelecting = false;
        viewport()->update();
        if (m_blockSel.first >= 0)
            setFocus();
        return;
    }
    if (m_resizeTable) {
        m_resizeTable = nullptr;
        m_resizeColumn = -1;
        scheduleSave();
        return;
    }
    if (m_resizeImagePos >= 0) {
        m_resizeImagePos = -1;
        scheduleSave();
        return;
    }
    if (m_pressOnHandle) {
        m_pressOnHandle = false;
        viewport()->setCursor(Qt::OpenHandCursor);
        if (m_draggingBlocks) {
            m_draggingBlocks = false;
            if (m_drop.where != DropTarget::None)
                moveRange(m_dragRange.first, m_dragRange.second, m_drop);
            m_drop = DropTarget{};
            viewport()->update();
        } else if (QTextTable *t = DocumentIO::tableOf(document()->findBlockByNumber(m_dragRange.first));
                   DocumentIO::isSimpleTable(t) && m_dragRange.second > m_dragRange.first) {
            showTableMenu(t, -1, -1, viewport()->mapToGlobal(e->position().toPoint() + QPoint(10, 10)));
        } else {
            showBlockMenu(m_dragRange.first, viewport()->mapToGlobal(e->position().toPoint() + QPoint(10, 10)));
        }
        return;
    }
    QTextEdit::mouseReleaseEvent(e);
    updateFormatBar();
}

void BlockEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    const HitInfo h = hitTest(e->position().toPoint());
    if (h.hit != Hit::None && h.hit != Hit::Link)
        return;
    QTextEdit::mouseDoubleClickEvent(e);
}

void BlockEditor::leaveEvent(QEvent *e)
{
    m_hoverBlock = -1;
    m_hoverHit = Hit::None;
    m_hoverGap = ColumnGap{};
    m_hoverTable = nullptr;
    m_ctx.hoveredObject = -1;
    m_ctx.hoveredPart = -1;
    m_ctx.hoveredPageId.clear();
    m_ctx.hoveredImage = -1;
    viewport()->update();
    QTextEdit::leaveEvent(e);
}

void BlockEditor::focusOutEvent(QFocusEvent *e)
{
    QTextEdit::focusOutEvent(e);
    // Menus stay while their own clicks are processed.
    if (e->reason() != Qt::PopupFocusReason) {
        QTimer::singleShot(0, this, [this] {
            if (!hasFocus()) {
                closeSlashMenu();
                closeMentionMenu();
                if (m_formatBar && !m_formatBar->isEditingLink())
                    m_formatBar->dismiss();
            }
        });
    }
    flush();
    viewport()->update();
}

void BlockEditor::focusInEvent(QFocusEvent *e)
{
    QTextEdit::focusInEvent(e);
    viewport()->update();
}

void BlockEditor::startAnimTick()
{
    if (!m_animTimer.isActive())
        m_animTimer.start();
}

