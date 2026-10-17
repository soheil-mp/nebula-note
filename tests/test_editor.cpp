#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "editor/BlockFormat.h"
#include "editor/DocumentIO.h"

#include <QApplication>
#include <QMimeData>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>
#include <QTextBlock>

using namespace BlockFormat;

class EditorTests : public QObject
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
    void settle() { QTest::qWait(5); }
    QTextBlock block(int n) const { return m_ed->document()->findBlockByNumber(n); }
    BlockType typeAt(int n) const { return typeOf(block(n)); }
    void fresh(const QList<Block> &blocks = {})
    {
        m_page = m_ws->createPage({}, QStringLiteral("Test"), blocks);
        m_ed->setPage(m_page);
        m_ed->setFocus();
        m_ed->focusEnd();
        settle();
    }

private slots:
    void initTestCase()
    {
        m_ws = new Workspace(this);
        QVERIFY(m_ws->open(m_dir.path()));
        m_ed = new BlockEditor;
        m_ed->resize(800, 600);
        m_ed->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_ed));
    }

    void cleanupTestCase()
    {
        delete m_ed;
    }

    void roundTripAllBlockTypes()
    {
        QList<Block> in;
        in.append(Block::make(BlockType::Heading1, QStringLiteral("Title")));
        Block p;
        Span b{QStringLiteral("bold")};
        b.bold = true;
        Span plain{QStringLiteral(" and ")};
        Span link{QStringLiteral("link")};
        link.link = QStringLiteral("https://example.com");
        Span hl{QStringLiteral("hi")};
        hl.highlight = 2;
        hl.color = 3;
        p.spans = {b, plain, link, hl};
        in.append(p);
        in.append(Block::make(BlockType::Bullet, QStringLiteral("one")));
        in.append(Block::make(BlockType::Bullet, QStringLiteral("two"), 1));
        Block todo = Block::make(BlockType::Todo, QStringLiteral("done"));
        todo.checked = true;
        in.append(todo);
        Block toggle = Block::make(BlockType::Toggle, QStringLiteral("more"));
        toggle.collapsed = true;
        in.append(toggle);
        in.append(Block::make(BlockType::Paragraph, QStringLiteral("hidden"), 1));
        Block code = Block::make(BlockType::Code, QStringLiteral("a") + QChar::LineSeparator + QStringLiteral("b"));
        code.language = QStringLiteral("python");
        in.append(code);
        Block callout = Block::make(BlockType::Callout, QStringLiteral("note"));
        callout.icon = QStringLiteral("glyph:bulb");
        callout.background = 2;
        in.append(callout);
        in.append(Block::make(BlockType::Divider));
        Block pageLink;
        pageLink.type = BlockType::PageLink;
        pageLink.pageId = QStringLiteral("abc");
        in.append(pageLink);
        in.append(Block::make(BlockType::Quote, QStringLiteral("wise")));
        ensureBlockIds(in);
        fresh(in);
        QCOMPARE(m_ed->blocks(), in);
        // The collapsed toggle hides its child.
        QVERIFY(!block(6).isVisible());
        QVERIFY(block(5).isVisible());
    }

    void markdownBlockShortcuts()
    {
        fresh();
        type(QStringLiteral("# "));
        QCOMPARE(typeAt(0), BlockType::Heading1);
        QVERIFY(block(0).text().isEmpty());
        type(QStringLiteral("Big"));
        key(Qt::Key_Return);
        QCOMPARE(typeAt(1), BlockType::Paragraph);
        type(QStringLiteral("- item"));
        QCOMPARE(typeAt(1), BlockType::Bullet);
        QCOMPARE(block(1).text(), QStringLiteral("item"));
        key(Qt::Key_Return);
        QCOMPARE(typeAt(2), BlockType::Bullet);
        key(Qt::Key_Return); // empty bullet → text
        QCOMPARE(typeAt(2), BlockType::Paragraph);
        type(QStringLiteral("[] task"));
        QCOMPARE(typeAt(2), BlockType::Todo);
        key(Qt::Key_Return);
        type(QStringLiteral("1. first"));
        QCOMPARE(typeAt(3), BlockType::Numbered);
        key(Qt::Key_Return);
        key(Qt::Key_Return);
        type(QStringLiteral("> fold"));
        QCOMPARE(typeAt(4), BlockType::Toggle);
        key(Qt::Key_Return);
        key(Qt::Key_Return);
        type(QStringLiteral("\" said"));
        QCOMPARE(typeAt(5), BlockType::Quote);
    }

    void backspaceUnwrapsBlocks()
    {
        fresh();
        type(QStringLiteral("## Head"));
        QCOMPARE(typeAt(0), BlockType::Heading2);
        key(Qt::Key_Home);
        key(Qt::Key_Backspace);
        QCOMPARE(typeAt(0), BlockType::Paragraph);
        QCOMPARE(block(0).text(), QStringLiteral("Head"));
    }

    void indentAndOutdentWithChildren()
    {
        fresh();
        type(QStringLiteral("- a"));
        key(Qt::Key_Return);
        type(QStringLiteral("b"));
        key(Qt::Key_Tab);
        QCOMPARE(depthOf(block(1)), 1);
        key(Qt::Key_Return);
        type(QStringLiteral("c"));
        key(Qt::Key_Tab);
        QCOMPARE(depthOf(block(2)), 2);
        // Outdenting b takes c along.
        QTextCursor c(block(1));
        m_ed->setTextCursor(c);
        key(Qt::Key_Backtab);
        QCOMPARE(depthOf(block(1)), 0);
        QCOMPARE(depthOf(block(2)), 1);
        // Can't indent further than one level below the previous block.
        m_ed->setTextCursor(QTextCursor(block(2)));
        key(Qt::Key_Tab);
        key(Qt::Key_Tab);
        QCOMPARE(depthOf(block(2)), 1);
    }

    void togglesHideChildren()
    {
        fresh();
        type(QStringLiteral("> Toggle"));
        key(Qt::Key_Return);
        QCOMPARE(typeAt(1), BlockType::Toggle);
        // Make the second line a child paragraph.
        key(Qt::Key_Backspace); // toggle → paragraph
        key(Qt::Key_Tab);
        type(QStringLiteral("child"));
        QCOMPARE(depthOf(block(1)), 1);
        m_ed->setTextCursor(QTextCursor(block(0)));
        key(Qt::Key_Return, Qt::ControlModifier); // collapse
        QVERIFY(isCollapsed(block(0)));
        QVERIFY(!block(1).isVisible());
        // The caret can't land in the hidden child.
        QTextCursor end(block(0));
        end.movePosition(QTextCursor::EndOfBlock);
        m_ed->setTextCursor(end);
        key(Qt::Key_Right);
        QVERIFY(m_ed->textCursor().block().isVisible());
        key(Qt::Key_Return, Qt::ControlModifier);
        QVERIFY(block(1).isVisible());
    }

    void inlineMarkdown()
    {
        fresh();
        type(QStringLiteral("say **loud** and *soft* and `code` and ~~old~~ "));
        const QList<Block> b = m_ed->blocks();
        QCOMPARE(b.size(), 1);
        QCOMPARE(b[0].plainText(), QStringLiteral("say loud and soft and code and old "));
        const auto has = [&](auto pred) { return std::any_of(b[0].spans.begin(), b[0].spans.end(), pred); };
        QVERIFY(has([](const Span &s) { return s.bold && s.text == QLatin1String("loud"); }));
        QVERIFY(has([](const Span &s) { return s.italic && s.text == QLatin1String("soft"); }));
        QVERIFY(has([](const Span &s) { return s.code && s.text == QLatin1String("code"); }));
        QVERIFY(has([](const Span &s) { return s.strike && s.text == QLatin1String("old"); }));
    }

    void formattingShortcuts()
    {
        fresh();
        type(QStringLiteral("hello world"));
        QTextCursor c = m_ed->textCursor();
        c.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        m_ed->setTextCursor(c);
        key(Qt::Key_B, Qt::ControlModifier);
        const QList<Block> b = m_ed->blocks();
        QVERIFY(b[0].spans.last().bold);
        QCOMPARE(b[0].spans.last().text, QStringLiteral("world"));
        key(Qt::Key_B, Qt::ControlModifier);
        QCOMPARE(m_ed->blocks()[0].spans.size(), 1);
    }

    void dividerAndCode()
    {
        fresh();
        type(QStringLiteral("---"));
        QCOMPARE(typeAt(0), BlockType::Divider);
        QCOMPARE(typeAt(1), BlockType::Paragraph);
        type(QStringLiteral("```"));
        QCOMPARE(typeAt(1), BlockType::Code);
        type(QStringLiteral("x = 1"));
        key(Qt::Key_Return);
        type(QStringLiteral("y = 2"));
        QCOMPARE(m_ed->document()->blockCount(), 2);
        QCOMPARE(block(1).text(), QStringLiteral("x = 1") + QChar::LineSeparator + QStringLiteral("y = 2"));
        // Two empty lines leave the code block.
        key(Qt::Key_Return);
        key(Qt::Key_Return);
        key(Qt::Key_Return);
        QCOMPARE(typeAt(2), BlockType::Paragraph);
    }

    void moveAndDuplicateBlocks()
    {
        fresh({Block::make(BlockType::Paragraph, QStringLiteral("A")), Block::make(BlockType::Paragraph, QStringLiteral("B")),
               Block::make(BlockType::Bullet, QStringLiteral("B1"), 1), Block::make(BlockType::Paragraph, QStringLiteral("C"))});
        m_ed->setTextCursor(QTextCursor(block(0)));
        key(Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier);
        // A jumped over B and its child.
        QCOMPARE(block(0).text(), QStringLiteral("B"));
        QCOMPARE(block(1).text(), QStringLiteral("B1"));
        QCOMPARE(block(2).text(), QStringLiteral("A"));
        m_ed->setTextCursor(QTextCursor(block(0)));
        key(Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(m_ed->document()->blockCount(), 6);
        QCOMPARE(block(2).text(), QStringLiteral("B"));
        QCOMPARE(block(3).text(), QStringLiteral("B1"));
        QCOMPARE(depthOf(block(3)), 1);
    }

    void copyPasteKeepsBlocks()
    {
        fresh({Block::make(BlockType::Heading2, QStringLiteral("Sec")), Block::make(BlockType::Todo, QStringLiteral("do it"))});
        m_ed->selectAll();
        QMimeData *mime = m_ed->createMimeDataFromSelection();
        QVERIFY(mime->hasFormat(QStringLiteral("application/x-nebula-blocks")));
        QVERIFY(mime->text().contains(QStringLiteral("## Sec")));
        m_ed->focusEnd(true);
        m_ed->insertFromMimeData(mime);
        delete mime;
        settle();
        const QList<Block> b = m_ed->blocks();
        QCOMPARE(b.size(), 4);
        QCOMPARE(b[2].type, BlockType::Heading2);
        QCOMPARE(b[3].type, BlockType::Todo);
    }

    void pastedMarkdownBecomesBlocks()
    {
        fresh();
        QMimeData mime;
        mime.setText(QStringLiteral("# Hello\n\n- one\n- two\n"));
        m_ed->insertFromMimeData(&mime);
        settle();
        const QList<Block> b = m_ed->blocks();
        QCOMPARE(b.size(), 3);
        QCOMPARE(b[0].type, BlockType::Heading1);
        QCOMPARE(b[2].type, BlockType::Bullet);
    }

    void savesToWorkspace()
    {
        fresh();
        type(QStringLiteral("persist me"));
        m_ed->flush();
        QCOMPARE(m_ws->content(m_page).first().plainText(), QStringLiteral("persist me"));
    }

    void marginsFollowPredecessor()
    {
        fresh({Block::make(BlockType::Paragraph, QStringLiteral("a")), Block::make(BlockType::Code, QStringLiteral("x")),
               Block::make(BlockType::Code, QStringLiteral("y"))});
        const Style &s = m_ed->style();
        // Two code blocks in a row keep both paddings apart.
        QCOMPARE(block(2).blockFormat().topMargin(), topMarginFor(BlockType::Code, BlockType::Code, s));
        QVERIFY(block(2).blockFormat().topMargin() >= padTop(BlockType::Code, s) + padBottom(BlockType::Code, s));
        // Turning the first code block into text updates the next block's spacing.
        m_ed->setTextCursor(QTextCursor(block(1)));
        m_ed->turnInto(BlockType::Paragraph);
        settle();
        QCOMPARE(block(2).blockFormat().topMargin(), topMarginFor(BlockType::Code, BlockType::Paragraph, s));
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // Settings go to Qt's test locations, never the user's config.
    QStandardPaths::setTestModeEnabled(true);
    EditorTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_editor.moc"
