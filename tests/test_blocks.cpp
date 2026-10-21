// The editor's Notion-style blocks: columns, simple tables, toggle headings,
// containers, objects, synced blocks and block selection.
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "editor/BlockFormat.h"
#include "editor/DocumentIO.h"
#include "ui/Tex.h"

#include <QImage>
#include <QPainter>

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include "editor/Popups.h"
#include "editor/TextObjects.h"
#include <QJsonArray>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBlock>
#include <QTextFrame>
#include <QTextTable>

using namespace BlockFormat;

class BlocksTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Workspace *m_ws = nullptr;
    BlockEditor *m_ed = nullptr;
    QString m_page;

    void type(const QString &text)
    {
        QTest::keyClicks(m_ed, text);
        settle();
    }
    void key(Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        QTest::keyClick(m_ed, k, mods);
        settle();
    }
    void settle()
    {
        QTest::qWait(5);
        QApplication::processEvents();
    }
    QTextDocument *doc() const { return m_ed->document(); }
    QTextBlock block(int n) const { return doc()->findBlockByNumber(n); }
    void fresh(const QList<Block> &blocks = {})
    {
        QList<Block> in = blocks;
        m_page = m_ws->createPage({}, QStringLiteral("Test"), in);
        m_ed->setPage(m_page);
        m_ed->setFocus();
        m_ed->focusEnd();
        settle();
    }
    // Puts the caret at the end of the block whose text is `text`.
    QTextBlock caretAt(const QString &text, bool atStart = false)
    {
        for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next()) {
            if (b.text() == text) {
                QTextCursor c(b);
                if (!atStart)
                    c.movePosition(QTextCursor::EndOfBlock);
                m_ed->setTextCursor(c);
                return b;
            }
        }
        return {};
    }
    QTextBlock find(const QString &text) const
    {
        for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next())
            if (b.text() == text)
                return b;
        return {};
    }
    static Block para(const QString &text, int depth = 0) { return Block::make(BlockType::Paragraph, text, depth); }
    static Block columns(const QList<QList<Block>> &cols)
    {
        Block b;
        b.type = BlockType::ColumnList;
        b.columns = cols;
        for (int i = 0; i < cols.size(); ++i)
            b.widths.append(1.0 / cols.size());
        return b;
    }
    // Saved content without ids (for comparisons).
    QList<Block> saved() const
    {
        QList<Block> out = m_ed->blocks();
        forEachBlockMutable(out, [](Block &b) { b.id.clear(); });
        return out;
    }
    static QList<Block> noIds(QList<Block> in)
    {
        forEachBlockMutable(in, [](Block &b) { b.id.clear(); });
        return in;
    }
    QList<QTextTable *> tables() const
    {
        QList<QTextTable *> out;
        std::function<void(QTextFrame *)> walk = [&](QTextFrame *f) {
            for (QTextFrame *c : f->childFrames()) {
                if (auto *t = qobject_cast<QTextTable *>(c))
                    out.append(t);
                walk(c);
            }
        };
        walk(doc()->rootFrame());
        return out;
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        m_ws = new Workspace(this);
        QVERIFY(m_ws->open(m_dir.path()));
        m_ed = new BlockEditor;
        m_ed->resize(900, 700);
        m_ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_ed));
    }

    void cleanupTestCase() { delete m_ed; }

    // ---- columns ---------------------------------------------------------------------

    void columnsRoundTrip()
    {
        const QList<Block> in = {para("intro"), columns({{para("left"), Block::make(BlockType::Bullet, "point", 0)},
                                                         {para("right"), para("more", 1)}}),
                                 para("outro")};
        fresh(in);
        QCOMPARE(saved(), noIds(in));
        QCOMPARE(tables().size(), 1);
        QVERIFY(DocumentIO::isColumns(tables().first()));
        // Every block got a unique id.
        QSet<QString> ids;
        forEachBlock(m_ed->blocks(), [&](const Block &b) {
            QVERIFY(!b.id.isEmpty());
            QVERIFY(!ids.contains(b.id));
            ids.insert(b.id);
        });
    }

    void columnsFirstOnThePage()
    {
        const QList<Block> in = {columns({{para("a")}, {para("b")}})};
        fresh(in);
        // Qt keeps an empty block before the table: hidden and never saved.
        QVERIFY(!block(0).isVisible());
        QCOMPARE(saved(), noIds(in));
        // Up from the first column's first line leaves the body.
        QSignalSpy exit(m_ed, &BlockEditor::exitTop);
        caretAt("a", true);
        key(Qt::Key_Up);
        QCOMPARE(exit.count(), 1);
    }

    void slashColumnsAndTyping()
    {
        fresh({para("before")});
        key(Qt::Key_Return);
        type("/2 col");
        key(Qt::Key_Return);
        QCOMPARE(tables().size(), 1);
        QTextTable *t = tables().first();
        QCOMPARE(t->columns(), 2);
        // The caret is in the first column.
        QCOMPARE(m_ed->textCursor().currentTable(), t);
        type("first");
        key(Qt::Key_Return);
        type("second");
        QList<Block> out = saved();
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].plainText(), QStringLiteral("before"));
        QCOMPARE(out[1].type, BlockType::ColumnList);
        QCOMPARE(out[1].columns.size(), 2);
        QCOMPARE(out[1].columns[0].size(), 2);
        QCOMPARE(out[1].columns[0][1].plainText(), QStringLiteral("second"));
        // Backspace at the start of the second column doesn't merge into the first.
        QTextCursor c = t->cellAt(0, 1).firstCursorPosition();
        m_ed->setTextCursor(c);
        type("x");
        key(Qt::Key_Home);
        key(Qt::Key_Backspace);
        out = saved();
        QCOMPARE(out[1].columns[1].size(), 1);
        QCOMPARE(out[1].columns[1][0].plainText(), QStringLiteral("x"));
        // /3 columns on a line with text keeps the text in the first column.
        fresh({para("keep me")});
        caretAt("keep me");
        type(" /3 col");
        key(Qt::Key_Return);
        out = saved();
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].columns.size(), 3);
        QCOMPARE(out[0].columns[0][0].plainText(), QStringLiteral("keep me "));
    }

    void numberingAndNestingStayInTheirColumn()
    {
        Block n1 = Block::make(BlockType::Numbered, "one"), n2 = Block::make(BlockType::Numbered, "two");
        Block n3 = Block::make(BlockType::Numbered, "uno");
        fresh({columns({{n1, n2}, {n3}})});
        QCOMPARE(m_ed->numberFor(find("two")), 2);
        QCOMPARE(m_ed->numberFor(find("uno")), 1); // a new list in its own column
        QCOMPARE(m_ed->listNumbers().value(find("uno").blockNumber()), 1);
        // A toggle's children stop at the column edge.
        Block tog = Block::make(BlockType::Toggle, "fold");
        fresh({columns({{tog, para("child", 1)}, {para("other", 1)}})});
        QCOMPARE(m_ed->subtreeEnd(find("fold")), find("child").blockNumber());
        // Indenting the first block of a column is refused.
        caretAt("other");
        key(Qt::Key_Tab);
        QCOMPARE(depthOf(find("other")), 1); // unchanged (loaded at 1 though it has no parent)
        caretAt("child");
        key(Qt::Key_Backtab);
        QCOMPARE(depthOf(find("child")), 0);
    }

    void enterAndDeleteRespectEdges()
    {
        fresh({para("top"), columns({{para("l")}, {para("r")}}), para("bottom")});
        // Delete at the end of the block before the table changes nothing.
        caretAt("top");
        key(Qt::Key_Delete);
        QCOMPARE(saved().size(), 3);
        // Backspace at the start of the block after it steps into the last column.
        caretAt("bottom", true);
        key(Qt::Key_Backspace);
        QCOMPARE(m_ed->textCursor().block().text(), QStringLiteral("r"));
        QCOMPARE(saved().size(), 3);
        // Enter in a column adds a block to that column.
        caretAt("l");
        key(Qt::Key_Return);
        type("l2");
        QCOMPARE(saved()[1].columns[0].size(), 2);
        QCOMPARE(saved()[1].columns[1].size(), 1);
    }

    void dragBesideMakesColumns()
    {
        fresh({para("A"), para("B"), para("C")});
        BlockEditor::DropTarget t;
        t.where = BlockEditor::DropTarget::Right;
        t.block = find("A").blockNumber();
        const int c = find("C").blockNumber();
        m_ed->moveRange(c, c, t);
        settle();
        QList<Block> out = saved();
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].type, BlockType::ColumnList);
        QCOMPARE(out[0].columns[0].first().plainText(), QStringLiteral("A"));
        QCOMPARE(out[0].columns[1].first().plainText(), QStringLiteral("C"));
        QCOMPARE(out[1].plainText(), QStringLiteral("B"));
        // Dropping B to the left of a block in a column adds a third column.
        t.where = BlockEditor::DropTarget::Left;
        t.block = find("A").blockNumber();
        const int b = find("B").blockNumber();
        m_ed->moveRange(b, b, t);
        settle();
        out = saved();
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].columns.size(), 3);
        QCOMPARE(out[0].columns[0].first().plainText(), QStringLiteral("B"));
        QCOMPARE(out[0].columns[1].first().plainText(), QStringLiteral("A"));
        double sum = 0;
        for (double w : out[0].widths)
            sum += w;
        QVERIFY(qAbs(sum - 1.0) < 0.01);
        // Moving a column's only block out removes the column…
        const int a = find("A").blockNumber();
        t.where = BlockEditor::DropTarget::After;
        t.block = doc()->lastBlock().blockNumber();
        t.depth = 0;
        m_ed->moveRange(a, a, t);
        settle();
        out = saved();
        QCOMPARE(out[0].type, BlockType::ColumnList);
        QCOMPARE(out[0].columns.size(), 2);
        // …and when one column is left, the list unwraps.
        const int cc = find("C").blockNumber();
        t.block = doc()->lastBlock().blockNumber();
        m_ed->moveRange(cc, cc, t);
        settle();
        out = saved();
        QVERIFY(std::none_of(out.begin(), out.end(), [](const Block &x) { return x.type == BlockType::ColumnList; }));
        QStringList texts;
        for (const Block &x : out)
            texts.append(x.plainText());
        qDebug() << texts;
        QCOMPARE(texts, QStringList({"B", "A", "C"}));
        QVERIFY(tables().isEmpty());
    }

    void moveIntoAndOutOfColumns()
    {
        fresh({para("x"), columns({{para("l1"), para("l2")}, {para("r1")}}), para("y")});
        // Drop "x" after r1 (into the second column).
        BlockEditor::DropTarget t;
        t.where = BlockEditor::DropTarget::After;
        t.block = find("r1").blockNumber();
        m_ed->moveRange(find("x").blockNumber(), find("x").blockNumber(), t);
        settle();
        QList<Block> out = saved();
        for (const Block &x : out)
            qDebug() << blockTypeKey(x.type) << x.plainText() << x.columns.size();
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].columns[1].size(), 2);
        QCOMPARE(out[0].columns[1][1].plainText(), QStringLiteral("x"));
        // Move the whole column list below "y" with Ctrl+Shift+Down from its first block's range.
        const int first = DocumentIO::firstBlockNumber(tables().first());
        const int last = DocumentIO::lastBlockNumber(tables().first());
        t.where = BlockEditor::DropTarget::After;
        t.block = find("y").blockNumber();
        m_ed->moveRange(first, last, t);
        settle();
        out = saved();
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].plainText(), QStringLiteral("y"));
        QCOMPARE(out[1].type, BlockType::ColumnList);
        // Duplicating a column list copies it with fresh ids.
        m_ed->setTextCursor(QTextCursor(doc()->findBlockByNumber(DocumentIO::firstBlockNumber(tables().first()))));
        QTextCursor sel(doc());
        sel.setPosition(find("y").position());
        sel.setPosition(doc()->lastBlock().position(), QTextCursor::KeepAnchor);
        m_ed->setTextCursor(sel);
        const auto range = m_ed->selectedBlockRange();
        QCOMPARE(range.first, find("y").blockNumber());
        QVERIFY(range.second >= DocumentIO::lastBlockNumber(tables().first()));
    }

    void columnWidthsResize()
    {
        Block cols = columns({{para("a")}, {para("b")}, {para("c")}});
        cols.widths = {0.5, 0.25, 0.25};
        fresh({cols});
        QTextTable *t = tables().first();
        QList<double> w = m_ed->columnWidths(t);
        QVERIFY(qAbs(w[0] - 0.5) < 0.001);
        m_ed->setColumnWidths(t, {0.2, 0.3, 0.5});
        QList<Block> out = saved();
        QVERIFY(qAbs(out[0].widths[2] - 0.5) < 0.001);
        // Gaps sit between the columns, where the resize bar appears.
        const QList<BlockEditor::ColumnGap> gaps = m_ed->columnGaps(m_ed->viewport()->rect());
        QCOMPARE(gaps.size(), 2);
        QVERIFY(gaps[0].rect.center().x() < gaps[1].rect.center().x());
        QCOMPARE(m_ed->columnGapAt(gaps[1].rect.center()).column, 1);
        // Text in each column sits right of the previous gap.
        QVERIFY(m_ed->textLeft(find("c")) > gaps[1].rect.center().x());
        QVERIFY(m_ed->blockAt(QPointF(m_ed->textLeft(find("c")) + 5, m_ed->firstLineMid(find("c")))) == find("c"));
        QVERIFY(m_ed->blockAt(QPointF(m_ed->textLeft(find("a")) + 5, m_ed->firstLineMid(find("a")))) == find("a"));
    }

    void foldedToggleHidesColumns()
    {
        Block tog = Block::make(BlockType::Toggle, "fold");
        tog.collapsed = true;
        Block cols = columns({{para("in a")}, {para("in b")}});
        cols.depth = 1;
        fresh({tog, cols, para("after")});
        QVERIFY(!find("in a").isVisible());
        QVERIFY(!find("in b").isVisible());
        QVERIFY(find("after").isVisible());
        QCOMPARE(m_ed->subtreeEnd(find("fold")), find("in b").blockNumber());
        QCOMPARE(noIds(m_ed->blocks()), noIds({tog, cols, para("after")}));
    }

    // ---- simple tables -----------------------------------------------------------------

    static Block table(const QList<QStringList> &rows)
    {
        Block b;
        b.type = BlockType::Table;
        for (const QStringList &r : rows) {
            QList<Cell> row;
            for (const QString &text : r)
                row.append(text.isEmpty() ? Cell{} : Cell{Span{text}});
            b.rows.append(row);
        }
        return b;
    }

    void simpleTableRoundTrip()
    {
        Block t = table({{"Planet", "Moons"}, {"Earth", "1"}, {"Mars", "2"}});
        t.data.insert("hr", true);
        t.data.insert("shades", QJsonArray{QJsonArray{0, 0}, QJsonArray{3, 0}, QJsonArray{0, 0}});
        t.widths = {200, 90};
        fresh({para("above"), t, para("below")});
        const QList<Block> out = saved();
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[1].type, BlockType::Table);
        QCOMPARE(out[1].rows, t.rows);
        QVERIFY(out[1].data.value("hr").toBool());
        QCOMPARE(out[1].data.value("shades").toArray().at(1).toArray().at(0).toInt(), 3);
        QCOMPARE(out[1].widths, QList<double>({200, 90}));
        QTextTable *qt = tables().first();
        QVERIFY(DocumentIO::isSimpleTable(qt));
        // The header row and the shaded cell have backgrounds.
        QVERIFY(qt->cellAt(0, 0).format().background().style() != Qt::NoBrush);
        QVERIFY(qt->cellAt(1, 0).format().background().style() != Qt::NoBrush);
        QVERIFY(qt->cellAt(2, 1).format().background().style() == Qt::NoBrush);
    }

    void slashTableAndNavigation()
    {
        fresh({para("x")});
        key(Qt::Key_Return);
        type("/table");
        // "Table" (simple) comes before the database views.
        key(Qt::Key_Return);
        QCOMPARE(tables().size(), 1);
        QTextTable *t = tables().first();
        QCOMPARE(t->rows(), 3);
        QCOMPARE(t->columns(), 3);
        QCOMPARE(m_ed->textCursor().currentTable(), t);
        type("a");
        key(Qt::Key_Tab);
        type("b");
        key(Qt::Key_Return); // down
        type("e");
        key(Qt::Key_Backtab);
        type("d");
        QList<Block> out = saved();
        Block tb = out[1];
        QCOMPARE(tb.rows[0][0].first().text, QStringLiteral("a"));
        QCOMPARE(tb.rows[0][1].first().text, QStringLiteral("b"));
        QCOMPARE(tb.rows[1][1].first().text, QStringLiteral("e"));
        QCOMPARE(tb.rows[1][0].first().text, QStringLiteral("d"));
        // Tab from the last cell adds a row.
        m_ed->moveToCell(t, 2, 2);
        key(Qt::Key_Tab);
        QCOMPARE(t->rows(), 4);
        type("new");
        QCOMPARE(saved()[1].rows[3][0].first().text, QStringLiteral("new"));
        // Shift+Enter is a line break inside the cell.
        key(Qt::Key_Return, Qt::ShiftModifier);
        type("line");
        QCOMPARE(saved()[1].rows[3][0].first().text, QStringLiteral("new\nline"));
        // Markdown shortcuts don't turn cells into other blocks.
        m_ed->moveToCell(t, 3, 1);
        type("# ");
        settle();
        QCOMPARE(typeOf(m_ed->textCursor().block()), BlockType::Paragraph);
    }

    void tableRowsAndColumns()
    {
        Block t = table({{"a", "b"}, {"c", "d"}});
        t.data.insert("shades", QJsonArray{QJsonArray{0, 2}, QJsonArray{0, 0}});
        fresh({t});
        QTextTable *qt = tables().first();
        m_ed->tableInsertRow(qt, 0);
        m_ed->tableInsertColumn(qt, 1);
        QList<Block> out = saved();
        QCOMPARE(out[0].rows.size(), 3);
        QCOMPARE(out[0].rows[0].size(), 3);
        QCOMPARE(out[0].rows[1][0].first().text, QStringLiteral("a"));
        QCOMPARE(out[0].rows[1][2].first().text, QStringLiteral("b"));
        // The shade moved with its cell.
        QCOMPARE(out[0].data.value("shades").toArray().at(1).toArray().at(2).toInt(), 2);
        m_ed->tableRemoveRow(qt, 0);
        m_ed->tableRemoveColumn(qt, 1);
        out = saved();
        QCOMPARE(out[0].rows, table({{"a", "b"}, {"c", "d"}}).rows);
        QCOMPARE(out[0].data.value("shades").toArray().at(0).toArray().at(1).toInt(), 2);
        // Undo brings the column back in one step.
        m_ed->undo();
        settle();
        QCOMPARE(saved()[0].rows[0].size(), 3);
        // Deleting the last column deletes the table.
        fresh({para("p"), table({{"only"}}), para("q")});
        m_ed->tableRemoveColumn(tables().first(), 0);
        QVERIFY(tables().isEmpty());
        QCOMPARE(saved().size(), 2);
    }

    void tableTurnsIntoDatabase()
    {
        Block t = table({{"Name", "Size"}, {"Io", "small"}, {"Ganymede", "large"}});
        t.data.insert("hr", true);
        fresh({t});
        m_ed->tableToDatabase(tables().first());
        settle();
        const QList<Block> out = saved();
        QCOMPARE(out.size(), 1);
        QCOMPARE(out[0].type, BlockType::Database);
        const QString db = out[0].pageId;
        QVERIFY(m_ws->page(db) && m_ws->page(db)->isDatabase());
        QCOMPARE(m_ws->page(db)->parentId, m_page);
        const Schema s = m_ws->schema(db);
        QCOMPARE(s.props.size(), 2);
        QCOMPARE(s.props[0].name, QStringLiteral("Name"));
        QCOMPARE(s.props[1].name, QStringLiteral("Size"));
        const auto rows = m_ws->rows(db);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows[1]->title, QStringLiteral("Ganymede"));
        QCOMPARE(m_ws->props(rows[1]->id).value(s.props[1].id).toString(), QStringLiteral("large"));
    }

    // ---- TeX --------------------------------------------------------------------------

    void texLayout()
    {
        const QStringList samples = {
            QStringLiteral("E = mc^2"),
            QStringLiteral("\\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}"),
            QStringLiteral("\\sum_{n=1}^{\\infty} \\frac{1}{n^2} = \\frac{\\pi^2}{6}"),
            QStringLiteral("\\int_0^1 x^2\\,dx = \\left( \\frac{x^3}{3} \\right)\\Big|_0^1"),
            QStringLiteral("\\begin{pmatrix} a & b \\\\ c & d \\end{pmatrix}^{-1} = \\frac{1}{ad-bc}\\begin{bmatrix} d & -b \\\\ -c & a \\end{bmatrix}"),
            QStringLiteral("f(x) = \\begin{cases} x^2 & x \\geq 0 \\\\ -x & \\text{otherwise} \\end{cases}"),
            QStringLiteral("\\lim_{x \\to 0} \\frac{\\sin x}{x} = 1, \\quad \\hat{v} \\cdot \\vec{u} \\in \\mathbb{R}^n"),
            QStringLiteral("\\nabla \\times \\mathbf{E} = -\\frac{\\partial \\mathbf{B}}{\\partial t}, \\; \\sqrt[3]{x} \\neq \\overline{z}"),
        };
        double y = 30;
        QImage img(900, 700, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(0x12, 0x12, 0x16));
        QPainter p(&img);
        for (const QString &tex : samples) {
            const Tex::Metrics m = Tex::measure(tex, 22, true);
            QVERIFY2(m.error.isEmpty(), qPrintable(tex + QStringLiteral(": ") + m.error));
            QVERIFY(m.width > 20);
            QVERIFY(m.ascent > 5);
            y += m.ascent;
            Tex::paint(p, QPointF(30, y), tex, 22, QColor(0xE6, 0xE6, 0xEC), true);
            y += m.descent + 24;
        }
        p.end();
        if (qEnvironmentVariableIsSet("NEBULA_TEX_SHOT"))
            img.save(qEnvironmentVariable("NEBULA_TEX_SHOT"));
        // Fractions in display style are taller than inline ones.
        QVERIFY(Tex::measure("\\frac{a}{b}", 16, true).ascent > Tex::measure("\\frac{a}{b}", 16, false).ascent);
        // Errors are reported, not fatal.
        QVERIFY(!Tex::measure("\\frac{a}{", 16, true).error.isEmpty());
        QVERIFY(!Tex::measure("\\nosuchcommand x", 16, true).error.isEmpty());
        QVERIFY(!Tex::measure("\\left( x", 16, true).error.isEmpty());
        QVERIFY(Tex::measure("", 16, true).width == 0);
    }

    // ---- headings, containers, shortcuts ------------------------------------------------

    void toggleHeadingsAndH4()
    {
        Block h = Block::make(BlockType::Heading2, "Section");
        h.toggleable = true;
        h.collapsed = true;
        fresh({h, para("inside", 1), para("after")});
        QVERIFY(!find("inside").isVisible());
        QVERIFY(foldsChildren(find("Section")));
        QCOMPARE(noIds(m_ed->blocks()), noIds({h, para("inside", 1), para("after")}));
        // Ctrl+Enter opens it.
        caretAt("Section");
        key(Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(find("inside").isVisible());
        // Enter at the end of an open toggle heading starts a child.
        caretAt("Section");
        key(Qt::Key_Return);
        type("first child");
        QCOMPARE(depthOf(find("first child")), 1);
        QCOMPARE(typeOf(find("first child")), BlockType::Paragraph);
        // #### makes a heading 4.
        fresh({para("")});
        type("#### Small");
        QCOMPARE(typeOf(find("Small")), BlockType::Heading4);
        // Turning into a toggle heading keeps the text.
        caretAt("Small");
        m_ed->turnIntoToggleHeading(BlockType::Heading1);
        QCOMPARE(typeOf(find("Small")), BlockType::Heading1);
        QVERIFY(toggleableOf(find("Small")));
        // The heading's text starts after the arrow.
        QVERIFY(m_ed->textLeft(find("Small")) > m_ed->blockLeft(find("Small")) + 10);
    }

    void calloutHoldsChildren()
    {
        Block callout = Block::make(BlockType::Callout, "Note");
        callout.icon = "glyph:bulb";
        fresh({callout, para("after")});
        caretAt("Note");
        key(Qt::Key_Return);
        type("inside the callout");
        QCOMPARE(depthOf(find("inside the callout")), 1);
        // Children line up with the callout's text, past the icon.
        QVERIFY(qAbs(m_ed->textLeft(find("inside the callout")) - m_ed->textLeft(find("Note"))) < 2);
        settle();
        QCOMPARE(m_ed->subtreeEnd(find("Note")), find("inside the callout").blockNumber());
    }

    void equationShortcutsAndObjects()
    {
        fresh({para("")});
        type("Energy $$E=mc^2$$");
        QList<Block> out = saved();
        QCOMPARE(out.size(), 1);
        bool found = false;
        for (const Span &sp : out[0].spans)
            found = found || sp.equation == QStringLiteral("E=mc^2");
        QVERIFY(found);
        // Ctrl+Shift+E turns the selection into an inline equation.
        fresh({para("a^2 + b^2")});
        QTextCursor c(find("a^2 + b^2"));
        c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        m_ed->setTextCursor(c);
        key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
        out = saved();
        QCOMPARE(out[0].spans.size(), 1);
        QCOMPARE(out[0].spans[0].equation, QStringLiteral("a^2 + b^2"));
        // Objects survive a round trip with their data and captions.
        Block eq;
        eq.type = BlockType::Equation;
        eq.data.insert("expr", "\\int_0^1 x\\,dx");
        eq.caption = {Span{"area under the line"}};
        Block bm;
        bm.type = BlockType::Bookmark;
        bm.url = "https://example.com";
        bm.data.insert("title", "Example Domain");
        Block file;
        file.type = BlockType::File;
        file.media = "abc.pdf";
        file.data.insert("name", "report.pdf");
        Block toc;
        toc.type = BlockType::TableOfContents;
        Block crumb;
        crumb.type = BlockType::Breadcrumb;
        Block btn;
        btn.type = BlockType::Button;
        btn.data.insert("label", "Add task");
        btn.data.insert("actions", QJsonArray{QJsonObject{{"type", "insert"}, {"markdown", "- [ ] New task"}}});
        Block img;
        img.type = BlockType::Image;
        img.media = "x.png";
        img.align = "right";
        img.caption = {Span{"a picture"}};
        const QList<Block> in = {Block::make(BlockType::Heading1, "Top"), toc, crumb, eq, bm, file, btn, img, Block::make(BlockType::Heading2, "Below")};
        fresh(in);
        QCOMPARE(saved(), noIds(in));
        // The table of contents lists both headings and jumps to them.
        QCOMPARE(TextObjects::headings(doc()).size(), 2);
        const QTextBlock tocBlock = block(1);
        const QRectF tr = m_ed->objectRect(tocBlock);
        QVERIFY(tr.height() > 40);
        const int part = m_ed->objectPartAt(tocBlock, QPointF(tr.left() + 20, tr.top() + TextObjects::tocTop(m_ed->style()) + TextObjects::tocRowHeight(m_ed->style()) * 1.5));
        QCOMPARE(part, 1);
        m_ed->activateObject(tocBlock, part, QPoint());
        settle();
        QCOMPARE(m_ed->textCursor().block().text(), QStringLiteral("Below"));
        // The button inserts its blocks below itself.
        const QTextBlock button = find(QString(QChar::ObjectReplacementCharacter)).isValid() ? QTextBlock() : QTextBlock();
        Q_UNUSED(button)
        int btnNumber = -1;
        for (QTextBlock b = doc()->begin(); b.isValid(); b = b.next())
            if (typeOf(b) == BlockType::Button)
                btnNumber = b.blockNumber();
        QVERIFY(btnNumber >= 0);
        m_ed->runButton(btnNumber);
        settle();
        QCOMPARE(typeOf(block(btnNumber + 1)), BlockType::Todo);
        QCOMPARE(block(btnNumber + 1).text(), QStringLiteral("New task"));
        m_ed->runButton(btnNumber);
        settle();
        QCOMPARE(typeOf(block(btnNumber + 2)), BlockType::Todo);
    }

    void blockSelectionMode()
    {
        fresh({para("one"), para("two"), para("three"), para("four")});
        caretAt("two");
        key(Qt::Key_Escape);
        QVERIFY(m_ed->hasBlockSelection());
        QCOMPARE(m_ed->blockSelection(), qMakePair(1, 1));
        key(Qt::Key_Down, Qt::ShiftModifier);
        QCOMPARE(m_ed->blockSelection(), qMakePair(1, 2));
        key(Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(saved().size(), 6);
        caretAt("one");
        key(Qt::Key_Escape);
        key(Qt::Key_Down);
        QCOMPARE(m_ed->blockSelection(), qMakePair(1, 1));
        key(Qt::Key_Delete);
        QCOMPARE(saved().size(), 5);
        QCOMPARE(saved()[1].plainText(), QStringLiteral("three"));
        // Tab nests the selected block; Enter goes back to editing it.
        caretAt("three");
        key(Qt::Key_Escape);
        key(Qt::Key_Tab);
        QCOMPARE(depthOf(find("three")), 1);
        key(Qt::Key_Return);
        QVERIFY(!m_ed->hasBlockSelection());
        QCOMPARE(m_ed->textCursor().block().text(), QStringLiteral("three"));
        // Esc on a table cell selects the whole table.
        Block t;
        t.type = BlockType::Table;
        t.rows = {{Cell{Span{"x"}}, Cell{Span{"y"}}}};
        fresh({para("p"), t});
        caretAt("x");
        key(Qt::Key_Escape);
        QCOMPARE(m_ed->blockSelection().second - m_ed->blockSelection().first, 1);
        key(Qt::Key_Backspace);
        QVERIFY(tables().isEmpty());
    }

    void dateMentionsAndEmoji()
    {
        fresh({para("")});
        type("Call @tomorrow");
        settle();
        key(Qt::Key_Return);
        QList<Block> out = saved();
        QString date;
        for (const Span &sp : out[0].spans)
            if (!sp.date.isEmpty())
                date = sp.date;
        QCOMPARE(date, QDate::currentDate().addDays(1).toString(Qt::ISODate));
        // "@remind friday 9am" asks for a reminder.
        fresh({para("")});
        type("@remind friday 9am");
        key(Qt::Key_Return);
        out = saved();
        bool remind = false;
        for (const Span &sp : out[0].spans)
            remind = remind || (sp.remind && sp.date.contains("T09:00"));
        QVERIFY(remind);
        // The workspace knows about it once saved.
        m_ed->flush();
        QVERIFY(!m_ws->reminders().isEmpty());
        // ":fire" autocompletes to the emoji.
        fresh({para("")});
        type("hot :fire");
        key(Qt::Key_Return);
        QCOMPARE(saved()[0].plainText(), QStringLiteral("hot 🔥"));
        // "+" at the start of a line links (or creates) a page.
        fresh({para("")});
        type("+Moonbase");
        settle();
        key(Qt::Key_Up); // "New sub-page" is last; take the page search result order as is
        key(Qt::Key_Return);
        out = saved();
        QCOMPARE(out[0].type, BlockType::PageLink);
        QVERIFY(m_ws->exists(out[0].pageId));
    }

    void pastedLinkMenuMakesBookmark()
    {
        fresh({para("")});
        QMimeData mime;
        mime.setText("https://example.org/article");
        m_ed->insertFromMimeData(&mime);
        settle();
        QVERIFY(m_ed->m_pasteMenu && m_ed->m_pasteMenu->isVisible());
        m_ed->runPasteChoice("bookmark");
        settle();
        const QList<Block> out = saved();
        QCOMPARE(out.first().type, BlockType::Bookmark);
        QCOMPARE(out.first().url, QStringLiteral("https://example.org/article"));
        // Links to blocks paste as mentions of that block.
        const QString target = m_ws->createPage({}, "Target", {Block::make(BlockType::Paragraph, "here")});
        const QString blockId = m_ws->content(target).first().id;
        fresh({para("")});
        mime.setText(QStringLiteral("nebula://page/%1#%2").arg(target, blockId));
        m_ed->insertFromMimeData(&mime);
        QCOMPARE(saved().first().spans.first().mention, target);
        QCOMPARE(saved().first().spans.first().mentionBlock, blockId);
    }

    void pastedVideoLinkEmbedsAtOnce()
    {
        fresh({para("")});
        QMimeData mime;
        mime.setText("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
        m_ed->insertFromMimeData(&mime);
        settle();
        // No menu: the video is embedded, and writing goes on below it.
        QVERIFY(!m_ed->m_pasteMenu || !m_ed->m_pasteMenu->isVisible());
        const QList<Block> out = saved();
        QVERIFY(out.size() >= 2);
        QCOMPARE(out[0].type, BlockType::Embed);
        QCOMPARE(out[0].url, QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ"));
        QCOMPARE(out[1].type, BlockType::Paragraph);
        QCOMPARE(m_ed->textCursor().block().blockNumber(), 1);
        // A 16:9 frame.
        const QRectF r = m_ed->objectRect(block(0));
        const QRectF frame = TextObjects::videoFrame(r);
        QVERIFY(qAbs(frame.width() / frame.height() - 16.0 / 9.0) < 0.02);
    }

    void hoveredImageShowsItsToolbar()
    {
        // A real picture, so the image has a size.
        QImage pic(400, 240, QImage::Format_RGB32);
        pic.fill(QColor(40, 40, 46));
        const QString file = m_dir.filePath(QStringLiteral("toolbar.png"));
        QVERIFY(pic.save(file));
        Block img;
        img.type = BlockType::Image;
        img.media = m_ws->importMedia(file);
        fresh({para(QStringLiteral("Above")), img});
        QTest::qWait(30);
        const QTextBlock b = block(1);
        QCOMPARE(typeOf(b), BlockType::Image);
        const QRectF r = m_ed->objectRect(b);
        QVERIFY(r.width() > 150);
        // Hover the picture, then its first tool: a caption editor.
        QTest::mouseMove(m_ed->viewport(), r.center().toPoint());
        settle();
        const auto tools = m_ed->imageTools(b);
        QCOMPARE(tools.size(), 3);
        QCOMPARE(tools.first().first, QStringLiteral("caption"));
        QVERIFY(r.contains(tools.last().second));
        const auto h = m_ed->hitTest(tools[1].second.center().toPoint());
        QCOMPARE(int(h.hit), int(BlockEditor::Hit::ImageTool));
        QCOMPARE(h.part, 1);
    }

    void firstBoxedBlockStaysInside()
    {
        // Qt drops the first block's top margin; a code box there would poke
        // above the editor without room in the page frame.
        Block code = Block::make(BlockType::Code, QStringLiteral("int x;"));
        code.language = QStringLiteral("cpp");
        fresh({code, para(QStringLiteral("After"))});
        QTest::qWait(30);
        const double frameTop = m_ed->document()->rootFrame()->frameFormat().topMargin();
        QVERIFY(frameTop >= padTop(BlockType::Code, m_ed->style()));
        const QRectF r = m_ed->document()->documentLayout()->blockBoundingRect(block(0));
        QVERIFY(r.top() - padTop(BlockType::Code, m_ed->style()) >= 0);
        // A plain first paragraph needs none.
        fresh({para(QStringLiteral("Plain"))});
        QTest::qWait(30);
        QVERIFY(m_ed->document()->rootFrame()->frameFormat().topMargin() < 4);
        // Nor does a column, unless it opens with a box.
        Block callout = Block::make(BlockType::Callout, QStringLiteral("Note"));
        fresh({para(QStringLiteral("Intro")), columns({{callout}, {para(QStringLiteral("Right"))}})});
        QTest::qWait(30);
        QCOMPARE(tables().size(), 1);
        QTextTable *t = tables().first();
        QCOMPARE(t->cellAt(0, 0).format().toTableCellFormat().topPadding(), padTop(BlockType::Callout, m_ed->style()));
        QCOMPARE(t->cellAt(0, 1).format().toTableCellFormat().topPadding(), 0.0);
    }

    void emptyToggleOffersAFirstChild()
    {
        Block toggle = Block::make(BlockType::Toggle, QStringLiteral("Details"));
        fresh({toggle});
        QTest::qWait(50);
        QVERIFY(block(0).blockFormat().bottomMargin() > 10); // room for the hint
        // …which counts in the editor's height even as the last block.
        const QRectF last = m_ed->document()->documentLayout()->blockBoundingRect(block(0));
        QVERIFY(m_ed->preferredHeight() >= last.bottom() + block(0).blockFormat().bottomMargin());
        // A click on the hint starts a child inside.
        const QTextBlock t = block(0);
        const QRectF r = m_ed->document()->documentLayout()->blockBoundingRect(t);
        const QPoint at(int(r.left()) + 60, int(r.bottom()) + 10);
        QTest::mouseClick(m_ed->viewport(), Qt::LeftButton, {}, at);
        settle();
        type(QStringLiteral("inside"));
        QTest::qWait(50);
        const QList<Block> out = saved();
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[1].depth, 1);
        QCOMPARE(out[1].plainText(), QStringLiteral("inside"));
        QCOMPARE(block(0).blockFormat().bottomMargin(), 0.0); // the hint is gone
    }

    void mermaidKeepsRoomAfterResize()
    {
        // A wide flowchart scales with the column; the space kept below the
        // code block must follow, or the diagram covers the next block.
        Block code = Block::make(BlockType::Code, QStringLiteral("graph LR") + QChar::LineSeparator
                                                      + QStringLiteral("A[Dust cloud] --> B(Protostar) --> C{Mass?} --> |large| D((Supernova))"));
        code.language = QStringLiteral("mermaid");
        fresh({code, para(QStringLiteral("After"))});
        const auto room = [&] { return block(0).blockFormat().bottomMargin(); };
        QTest::qWait(150); // normalisation runs on a timer
        const double wide = room();
        QVERIFY2(wide > 80, qPrintable(QString::number(wide)));
        m_ed->resize(420, 700);
        QTest::qWait(150);
        const double narrow = room();
        QVERIFY2(narrow < wide, qPrintable(QStringLiteral("%1 vs %2").arg(narrow).arg(wide)));
        m_ed->resize(900, 700);
        QTest::qWait(150);
        QCOMPARE(room(), wide);
    }

    void syncedBlocksAndBlockLinks()
    {
        fresh({para("shared one"), para("shared two"), para("local")});
        QTextCursor c(find("shared one"));
        c.setPosition(find("shared two").position() + 3, QTextCursor::KeepAnchor);
        m_ed->setTextCursor(c);
        m_ed->turnIntoSynced();
        settle();
        const QString sync = syncOf(find("shared one"));
        QVERIFY(!sync.isEmpty());
        QCOMPARE(syncOf(find("shared two")), sync);
        QVERIFY(syncOf(find("local")).isEmpty());
        // Another page shows the same blocks through a reference.
        Block ref;
        ref.type = BlockType::Synced;
        ref.syncId = sync;
        const QString other = m_ws->createPage({}, "Other", {ref});
        QCOMPARE(m_ws->content(other).size(), 2);
        // Editing here shows there.
        caretAt("shared one");
        type(" edited");
        m_ed->flush();
        QCOMPARE(m_ws->content(other).first().plainText(), QStringLiteral("shared one edited"));
        // Unsync keeps a local copy.
        m_ed->unsyncBlocks(sync);
        settle();
        QVERIFY(syncOf(find("shared one edited")).isEmpty());
        // Copy link to block puts a nebula:// link on the clipboard.
        m_ed->copyLinkToBlock(find("local").blockNumber());
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("nebula://page/%1#%2").arg(m_page, idOf(find("local"))));
        // Scrolling to a block by id finds it.
        QVERIFY(m_ed->scrollToBlockId(idOf(find("local"))));
        QCOMPARE(m_ed->textCursor().block().text(), QStringLiteral("local"));
    }
};

QTEST_MAIN(BlocksTests)
#include "test_blocks.moc"
