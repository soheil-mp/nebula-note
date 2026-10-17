// Simple tables: rows of plain-text cells with optional header row and
// column, cell shades, resizable columns and Tab/Enter navigation.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "DocumentIO.h"
#include "core/Markdown.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QPainter>
#include <QTextBlock>
#include <QTextTable>

using namespace BlockFormat;
using namespace BlockEditorPrivate;

namespace
{
QJsonObject dataOfTable(const QTextTable *t)
{
    return QJsonDocument::fromJson(t->format().property(DataProp).toString().toUtf8()).object();
}

QJsonArray shadesOf(const QJsonObject &data, int rows, int cols)
{
    QJsonArray in = data.value(QStringLiteral("shades")).toArray();
    QJsonArray out;
    for (int r = 0; r < rows; ++r) {
        QJsonArray row;
        for (int c = 0; c < cols; ++c)
            row.append(in.at(r).toArray().at(c).toInt());
        out.append(row);
    }
    return out;
}
} // namespace

QRectF BlockEditor::simpleTableBox(const QTextTable *t) const
{
    // The border box, from the cells' own blocks (the frame rect includes
    // margins).
    const QTextDocument *doc = document();
    const double pad = t->format().cellPadding();
    const QTextBlock first = doc->findBlock(t->cellAt(0, 0).firstPosition());
    const QTextBlock lastCol = doc->findBlock(t->cellAt(0, t->columns() - 1).firstPosition());
    double bottom = blockRect(first).bottom();
    for (int c = 0; c < t->columns(); ++c)
        bottom = qMax(bottom, blockRect(doc->findBlock(t->cellAt(t->rows() - 1, c).lastPosition())).bottom());
    const QRectF fr = blockRect(first), lr = blockRect(lastCol);
    return QRectF(QPointF(fr.left() - pad, fr.top() - pad), QPointF(lr.left() + lr.width() + pad, bottom + pad));
}

QTextTable *BlockEditor::currentSimpleTable() const
{
    QTextTable *t = textCursor().currentTable();
    return DocumentIO::isSimpleTable(t) ? t : nullptr;
}

QJsonObject BlockEditor::tableData(const QTextTable *t) const
{
    return dataOfTable(t);
}

void BlockEditor::setTableData(QTextTable *t, const QJsonObject &data)
{
    QTextTableFormat tf = t->format();
    tf.setProperty(DataProp, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
    t->setFormat(tf);
    DocumentIO::formatCells(t, m_ctx.style);
}

QList<double> BlockEditor::simpleTableWidths(const QTextTable *t) const
{
    QList<double> out;
    const QList<QTextLength> lens = t->format().columnWidthConstraints();
    const double avail = qMax(200.0, double(viewport()->width() - gutter() - rightMargin()));
    for (int c = 0; c < t->columns(); ++c) {
        const QTextLength l = c < lens.size() ? lens[c] : QTextLength();
        if (l.type() == QTextLength::PercentageLength)
            out.append(l.rawValue() / 100.0 * avail);
        else if (l.type() == QTextLength::FixedLength)
            out.append(l.rawValue());
        else
            out.append(120 * m_ctx.style.size / 16.0);
    }
    return out;
}

void BlockEditor::setSimpleTableWidths(QTextTable *t, const QList<double> &px)
{
    QTextTableFormat tf = t->format();
    const bool fit = dataOfTable(t).value(QStringLiteral("fit")).toBool();
    QList<QTextLength> lens;
    double total = 0;
    for (double w : px)
        total += w;
    for (double w : px)
        lens.append(fit ? QTextLength(QTextLength::PercentageLength, total > 0 ? 100.0 * w / total : 100.0 / px.size())
                        : QTextLength(QTextLength::FixedLength, qMax(40.0, w)));
    tf.setColumnWidthConstraints(lens);
    t->setFormat(tf);
}

void BlockEditor::insertSimpleTable(int rows, int cols)
{
    QTextDocument *doc = document();
    QTextBlock blk = textCursor().block();
    if (DocumentIO::isSimpleTable(DocumentIO::tableOf(blk))) {
        emit toast(tr("Tables can't go inside a table."));
        return;
    }
    Block t;
    t.type = BlockType::Table;
    t.id = Block::newId();
    t.depth = depthOf(blk);
    t.data.insert(QStringLiteral("hr"), true);
    for (int r = 0; r < rows; ++r) {
        QList<Cell> row;
        for (int c = 0; c < cols; ++c)
            row.append(Cell{});
        t.rows.append(row);
    }
    const bool empty = blk.text().isEmpty() && typeOf(blk) == BlockType::Paragraph;
    const int n = blk.blockNumber();
    EditSteps steps(doc);
    steps.run([&](QTextCursor &c) { DocumentIO::insertAfter(c, blk, {t}, m_ctx.style); });
    if (empty)
        steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, n, n, m_ctx.style); });
    updateVisibility();
    scheduleNormalize();
    // Find it and put the caret in the first cell.
    std::function<QTextTable *(QTextFrame *)> find = [&](QTextFrame *frame) -> QTextTable * {
        for (QTextFrame *child : frame->childFrames()) {
            if (auto *tt = qobject_cast<QTextTable *>(child); tt && tt->format().property(IdProp).toString() == t.id)
                return tt;
            if (QTextTable *inner = find(child))
                return inner;
        }
        return nullptr;
    };
    if (QTextTable *table = find(doc->rootFrame()))
        setTextCursor(table->cellAt(0, 0).firstCursorPosition());
}

void BlockEditor::moveToCell(QTextTable *t, int row, int col, bool select)
{
    if (!t || row < 0 || col < 0 || row >= t->rows() || col >= t->columns())
        return;
    const QTextTableCell cell = t->cellAt(row, col);
    QTextCursor c = cell.firstCursorPosition();
    if (select)
        c.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    setTextCursor(c);
}

void BlockEditor::tableInsertRow(QTextTable *t, int at)
{
    at = qBound(0, at, t->rows());
    QJsonObject data = dataOfTable(t);
    QJsonArray shades = shadesOf(data, t->rows(), t->columns());
    QJsonArray empty;
    for (int c = 0; c < t->columns(); ++c)
        empty.append(0);
    shades.insert(at, empty);
    EditSteps steps(document());
    steps.run([&](QTextCursor &) {
        t->insertRows(at, 1);
        for (int c = 0; c < t->columns(); ++c) {
            QTextCursor cc = t->cellAt(at, c).firstCursorPosition();
            cc.setBlockFormat(DocumentIO::cellBlockFormat(m_ctx.style));
            cc.setBlockCharFormat(baseCharFormat(BlockType::Paragraph, m_ctx.style));
        }
        data.insert(QStringLiteral("shades"), shades);
        setTableData(t, data);
    });
    scheduleNormalize();
}

void BlockEditor::tableInsertColumn(QTextTable *t, int at)
{
    at = qBound(0, at, t->columns());
    QJsonObject data = dataOfTable(t);
    QJsonArray shades = shadesOf(data, t->rows(), t->columns());
    for (int r = 0; r < shades.size(); ++r) {
        QJsonArray row = shades.at(r).toArray();
        row.insert(at, 0);
        shades.replace(r, row);
    }
    QList<double> widths = simpleTableWidths(t);
    widths.insert(at, 120 * m_ctx.style.size / 16.0);
    EditSteps steps(document());
    steps.run([&](QTextCursor &) {
        t->insertColumns(at, 1);
        for (int r = 0; r < t->rows(); ++r) {
            QTextCursor cc = t->cellAt(r, at).firstCursorPosition();
            cc.setBlockFormat(DocumentIO::cellBlockFormat(m_ctx.style));
            cc.setBlockCharFormat(baseCharFormat(BlockType::Paragraph, m_ctx.style));
        }
        setSimpleTableWidths(t, widths);
        data.insert(QStringLiteral("shades"), shades);
        setTableData(t, data);
    });
    scheduleNormalize();
}

void BlockEditor::tableRemoveRow(QTextTable *t, int row)
{
    if (t->rows() <= 1) {
        deleteTable(t);
        return;
    }
    QJsonObject data = dataOfTable(t);
    QJsonArray shades = shadesOf(data, t->rows(), t->columns());
    shades.removeAt(row);
    EditSteps steps(document());
    steps.run([&](QTextCursor &) {
        t->removeRows(row, 1);
        data.insert(QStringLiteral("shades"), shades);
        setTableData(t, data);
    });
    moveToCell(t, qMin(row, t->rows() - 1), 0);
}

void BlockEditor::tableRemoveColumn(QTextTable *t, int col)
{
    if (t->columns() <= 1) {
        deleteTable(t);
        return;
    }
    QJsonObject data = dataOfTable(t);
    QJsonArray shades = shadesOf(data, t->rows(), t->columns());
    for (int r = 0; r < shades.size(); ++r) {
        QJsonArray rowShades = shades.at(r).toArray();
        rowShades.removeAt(col);
        shades.replace(r, rowShades);
    }
    QList<double> widths = simpleTableWidths(t);
    widths.removeAt(col);
    EditSteps steps(document());
    steps.run([&](QTextCursor &) {
        t->removeColumns(col, 1);
        setSimpleTableWidths(t, widths);
        data.insert(QStringLiteral("shades"), shades);
        setTableData(t, data);
    });
    moveToCell(t, 0, qMin(col, t->columns() - 1));
}

void BlockEditor::deleteTable(QTextTable *t)
{
    const int first = DocumentIO::firstBlockNumber(t), last = DocumentIO::lastBlockNumber(t);
    EditSteps(document()).run([&](QTextCursor &c) { DocumentIO::removeRange(c, first, last, m_ctx.style); });
    updateVisibility();
    scheduleNormalize();
}

bool BlockEditor::handleTableKey(int key, Qt::KeyboardModifiers mods)
{
    QTextTable *t = currentSimpleTable();
    if (!t)
        return false;
    const QTextCursor c = textCursor();
    const QTextTableCell cell = t->cellAt(c.position());
    const int row = cell.row(), col = cell.column();
    const bool shift = mods & Qt::ShiftModifier;
    if (key == Qt::Key_Tab && !shift) {
        if (col + 1 < t->columns())
            moveToCell(t, row, col + 1, true);
        else if (row + 1 < t->rows())
            moveToCell(t, row + 1, 0, true);
        else {
            tableInsertRow(t, t->rows());
            moveToCell(t, t->rows() - 1, 0);
        }
        return true;
    }
    if (key == Qt::Key_Backtab || (key == Qt::Key_Tab && shift)) {
        if (col > 0)
            moveToCell(t, row, col - 1, true);
        else if (row > 0)
            moveToCell(t, row - 1, t->columns() - 1, true);
        return true;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !shift && !(mods & Qt::ControlModifier)) {
        // Enter moves down (adding a row at the bottom), like a spreadsheet.
        if (row + 1 >= t->rows())
            tableInsertRow(t, t->rows());
        moveToCell(t, row + 1, col);
        return true;
    }
    if ((key == Qt::Key_Return || key == Qt::Key_Enter) && shift) {
        QTextCursor lc = textCursor();
        lc.insertText(QString(QChar::LineSeparator));
        setTextCursor(lc);
        return true;
    }
    return false;
}

void BlockEditor::tableToDatabase(QTextTable *t)
{
    if (!m_ws || m_pageId.isEmpty())
        return;
    const Block tb = DocumentIO::tableBlock(t);
    if (tb.rows.isEmpty())
        return;
    flush();
    const bool header = tb.data.value(QStringLiteral("hr")).toBool();
    auto text = [](const Cell &cell) {
        QString s;
        for (const Span &sp : cell)
            if (!sp.isObject())
                s += sp.text;
        return s.trimmed();
    };
    // The first column names the rows; the header row names the properties.
    Schema schema = Schema::basic();
    schema.props = {schema.props.first()};
    const int cols = tb.rows.first().size();
    for (int c = 0; c < cols; ++c) {
        const QString name = header ? text(tb.rows.first().value(c)) : QString();
        if (c == 0) {
            if (!name.isEmpty())
                schema.props[0].name = name;
            continue;
        }
        Property p;
        p.id = schema.newPropId();
        p.name = name.isEmpty() ? tr("Column %1").arg(c + 1) : name;
        p.type = PropType::Text;
        schema.props.append(p);
    }
    schema.views = {Schema::makeView(schema, ViewType::Table)};
    const QString db = m_ws->createDatabase(m_pageId, tr("Table"), schema);
    for (int r = header ? 1 : 0; r < tb.rows.size(); ++r) {
        QJsonObject props;
        for (int c = 1; c < cols && c < schema.props.size(); ++c) {
            const QString v = text(tb.rows[r].value(c));
            if (!v.isEmpty())
                props.insert(schema.props[c].id, v);
        }
        m_ws->createRow(db, text(tb.rows[r].value(0)), props);
    }
    // The table becomes an inline view of the new database.
    Block dbBlock;
    dbBlock.type = BlockType::Database;
    dbBlock.pageId = db;
    dbBlock.depth = tb.depth;
    dbBlock.id = Block::newId();
    const QTextBlock after = document()->findBlock(t->lastPosition() + 1);
    QTextCursor anchor(document());
    anchor.setPosition(after.position());
    const int first = DocumentIO::firstBlockNumber(t), last = DocumentIO::lastBlockNumber(t);
    EditSteps steps(document());
    steps.run([&](QTextCursor &c) { DocumentIO::removeRange(c, first, last, m_ctx.style); });
    steps.run([&](QTextCursor &c) { placeBefore(c, anchor.block(), {dbBlock}); });
    m_dirty = true;
    flush();
    updateVisibility();
    emit toast(tr("Turned into a database"));
}

void BlockEditor::showTableMenu(QTextTable *t, int row, int col, const QPoint &globalPos)
{
    if (!t)
        return;
    QMenu *menu = Theme::makeMenu(this);
    QPointer<QTextTable> table(t);
    const QJsonObject data = dataOfTable(t);
    Theme::addMenuSection(menu, tr("Table"));
    QAction *hr = menu->addAction(tr("Header row"));
    hr->setCheckable(true);
    hr->setChecked(data.value(QStringLiteral("hr")).toBool());
    connect(hr, &QAction::toggled, this, [this, table](bool on) {
        if (!table)
            return;
        QJsonObject d = tableData(table);
        d.insert(QStringLiteral("hr"), on);
        EditSteps(document()).run([&](QTextCursor &) { setTableData(table, d); });
    });
    QAction *hc = menu->addAction(tr("Header column"));
    hc->setCheckable(true);
    hc->setChecked(data.value(QStringLiteral("hc")).toBool());
    connect(hc, &QAction::toggled, this, [this, table](bool on) {
        if (!table)
            return;
        QJsonObject d = tableData(table);
        d.insert(QStringLiteral("hc"), on);
        EditSteps(document()).run([&](QTextCursor &) { setTableData(table, d); });
    });
    QAction *fit = menu->addAction(tr("Fit to page width"));
    fit->setCheckable(true);
    fit->setChecked(data.value(QStringLiteral("fit")).toBool());
    connect(fit, &QAction::toggled, this, [this, table](bool on) {
        if (!table)
            return;
        const QList<double> widths = simpleTableWidths(table);
        QJsonObject d = tableData(table);
        d.insert(QStringLiteral("fit"), on);
        EditSteps(document()).run([&](QTextCursor &) {
            setTableData(table, d);
            QTextTableFormat tf = table->format();
            tf.setWidth(on ? QTextLength(QTextLength::PercentageLength, 100) : QTextLength());
            table->setFormat(tf);
            setSimpleTableWidths(table, widths);
        });
    });
    if (row >= 0 && col >= 0) {
        menu->addSeparator();
        menu->addAction(Icons::icon(QStringLiteral("arrow-up")), tr("Insert row above"), this, [this, table, row] {
            if (table)
                tableInsertRow(table, row);
        });
        menu->addAction(Icons::icon(QStringLiteral("arrow-down")), tr("Insert row below"), this, [this, table, row] {
            if (table)
                tableInsertRow(table, row + 1);
        });
        menu->addAction(Icons::icon(QStringLiteral("chevron-left")), tr("Insert column left"), this, [this, table, col] {
            if (table)
                tableInsertColumn(table, col);
        });
        menu->addAction(Icons::icon(QStringLiteral("chevron-right")), tr("Insert column right"), this, [this, table, col] {
            if (table)
                tableInsertColumn(table, col + 1);
        });
        menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete row"), this, [this, table, row] {
            if (table)
                tableRemoveRow(table, row);
        });
        menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete column"), this, [this, table, col] {
            if (table)
                tableRemoveColumn(table, col);
        });
        QMenu *shade = menu->addMenu(Icons::icon(QStringLiteral("palette")), tr("Cell shade"));
        for (int i = 0; i < kHighlightShadeCount; ++i) {
            QPixmap swatch(16, 16);
            swatch.fill(Qt::transparent);
            QPainter p(&swatch);
            p.setRenderHint(QPainter::Antialiasing);
            const Theme::Highlight h = Theme::highlightShade(i);
            p.setPen(QPen(Theme::colors().ash, 1));
            p.setBrush(h.background.isValid() ? h.background : QColor(Qt::transparent));
            p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
            p.end();
            shade->addAction(QIcon(swatch), highlightShadeName(i), this, [this, table, row, col, i] {
                if (!table)
                    return;
                QJsonObject d = tableData(table);
                QJsonArray shades = shadesOf(d, table->rows(), table->columns());
                QJsonArray r = shades.at(row).toArray();
                r.replace(col, i);
                shades.replace(row, r);
                d.insert(QStringLiteral("shades"), shades);
                EditSteps(document()).run([&](QTextCursor &) { setTableData(table, d); });
            });
        }
    }
    menu->addSeparator();
    menu->addAction(Icons::icon(QStringLiteral("database")), tr("Turn into database"), this, [this, table] {
        if (table)
            tableToDatabase(table);
    });
    menu->addAction(Icons::icon(QStringLiteral("download")), tr("Copy as Markdown"), this, [this, table] {
        if (!table)
            return;
        Markdown::ExportOptions opt;
        opt.pageTitle = [this](const QString &id) { return m_ws ? m_ws->title(id) : QString(); };
        QApplication::clipboard()->setText(Markdown::fromBlocks({DocumentIO::tableBlock(table)}, opt));
        emit toast(tr("Copied as Markdown"));
    });
    menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate"), this, [this, table] {
        if (!table)
            return;
        QTextCursor c(document());
        c.setPosition(table->firstPosition());
        c.setPosition(table->lastPosition(), QTextCursor::KeepAnchor);
        setTextCursor(c);
        duplicateBlocks();
    });
    menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete table"), this, [this, table] {
        if (table)
            deleteTable(table);
    });
    menu->popup(globalPos);
}
