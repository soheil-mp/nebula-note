// Drives the real window through the main flows (offscreen) to catch crashes
// and wiring mistakes that unit tests can't see.
#include "core/Exporter.h"
#include "core/Importer.h"
#include "core/Settings.h"
#include "core/Templates.h"
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "ui/ExportRenderers.h"
#include "ui/Plurals.h"
#include "ui/Theme.h"
#include "views/DatabaseView.h"
#include "views/History.h"
#include "views/MainWindow.h"
#include "views/Sidebar.h"
#include "views/Tabs.h"
#include "views/TopBar.h"
#include "views/Overlays.h"
#include "views/PageHeader.h"
#include "views/PageView.h"

#include <QApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QListWidget>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>

class AppTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    Workspace *m_ws = nullptr;
    MainWindow *m_win = nullptr;
    QString m_welcome;

    void settle(int ms = 30) { QTest::qWait(ms); }

    template<typename T>
    T *find() const
    {
        return m_win->findChild<T *>();
    }

    QString pageNamed(const QString &title) const
    {
        for (const PageMeta *m : m_ws->allLive())
            if (m->title == title)
                return m->id;
        return {};
    }

private slots:
    void initTestCase()
    {
        QDirIterator it(QStringLiteral(":/fonts"), {QStringLiteral("*.otf")});
        while (it.hasNext())
            QFontDatabase::addApplicationFont(it.next());
        qApp->setPalette(Theme::palette());
        qApp->setStyleSheet(Theme::styleSheet());
        Settings::instance().expanded.clear();
        m_ws = new Workspace(this);
        QVERIFY(m_ws->open(m_dir.filePath(QStringLiteral("ws"))));
        m_welcome = Templates::createSampleWorkspace(*m_ws);
        QVERIFY(!m_welcome.isEmpty());
        m_win = new MainWindow;
        m_win->resize(1280, 820);
        m_win->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_win));
        m_win->navigate(m_welcome);
        settle();
    }

    void cleanupTestCase()
    {
        delete m_win;
    }

    void visitsEveryPageAndView()
    {
        for (const PageMeta *m : m_ws->allLive()) {
            const QString id = m->id;
            m_win->navigate(id);
            settle(10);
            if (m_ws->page(id)->isDatabase()) {
                for (const View &v : m_ws->schema(id).views) {
                    m_win->runAction(QStringLiteral("view:") + v.name);
                    settle(10);
                }
            }
        }
        m_win->goHome();
        settle();
        QVERIFY(true);
    }

    void newPageTitleAndBody()
    {
        QTest::keyClick(m_win, Qt::Key_N, Qt::ControlModifier);
        settle();
        auto *page = find<PageView>();
        QVERIFY(page);
        const QString id = page->pageId();
        QVERIFY(m_ws->exists(id));
        TitleEdit *title = page->findChild<TitleEdit *>();
        QVERIFY(title->hasFocus());
        QTest::keyClicks(title, QStringLiteral("Orbital notes"));
        QTest::keyClick(title, Qt::Key_Return);
        settle(300);
        QCOMPARE(m_ws->page(id)->title, QStringLiteral("Orbital notes"));
        BlockEditor *ed = page->editor();
        QVERIFY(ed->hasFocus());
        QTest::keyClicks(ed, QStringLiteral("# Plan"));
        QTest::keyClick(ed, Qt::Key_Return);
        QTest::keyClicks(ed, QStringLiteral("- [ ] launch"));
        settle();
        ed->flush();
        const QList<Block> blocks = m_ws->content(id);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].type, BlockType::Heading1);
        QCOMPARE(blocks[1].type, BlockType::Todo);
        QCOMPARE(blocks[1].plainText(), QStringLiteral("launch"));
    }

    void ctrlKFromEditorOpensQuickFind()
    {
        m_win->navigate(m_welcome);
        settle();
        BlockEditor *ed = find<PageView>()->editor();
        ed->setFocus();
        settle();
        QTest::keyClick(ed, Qt::Key_K, Qt::ControlModifier);
        settle(60);
        auto *qf = m_win->findChild<QuickFind *>();
        QVERIFY(qf && qf->isVisible());
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        settle(60);
        QVERIFY(!m_win->findChild<QuickFind *>() || !m_win->findChild<QuickFind *>()->isVisible());
    }

    void overlaysOpenAndClose()
    {
        for (const QString &a : {QStringLiteral("quickfind"), QStringLiteral("settings"), QStringLiteral("templates")}) {
            m_win->runAction(a);
            settle(50);
            QWidget *focus = QApplication::focusWidget();
            QVERIFY(focus);
            QTest::keyClick(focus, Qt::Key_Escape);
            settle(50);
        }
        QVERIFY(true);
    }

    void templatesApplyEverywhere()
    {
        for (const Templates::Info &t : Templates::all()) {
            const QString id = m_ws->createPage({});
            Templates::apply(*m_ws, t.id, id);
            m_win->navigate(id);
            settle(10);
            QVERIFY2(m_ws->page(id)->isDatabase() == t.database, qPrintable(t.id));
            if (!t.database)
                QVERIFY2(!m_ws->content(id).isEmpty(), qPrintable(t.id));
            else
                QVERIFY2(!m_ws->rows(id).isEmpty(), qPrintable(t.id));
        }
    }

    void trashAndRestoreCurrentPage()
    {
        const QString id = pageNamed(QStringLiteral("Ideas"));
        QVERIFY(!id.isEmpty());
        m_win->navigate(id);
        settle();
        m_ws->trash(id);
        settle();
        // The window leaves a page that went to the trash.
        QVERIFY(find<PageView>()->pageId() != id || !find<PageView>()->isVisible());
        m_ws->restore(id);
        m_win->navigate(id);
        settle();
        QCOMPARE(find<PageView>()->pageId(), id);
    }

    void databaseRowsAndPropertyChanges()
    {
        const QString db = pageNamed(QStringLiteral("Projects"));
        m_win->navigate(db);
        settle();
        const int before = int(m_ws->rows(db).size());
        const QString row = m_ws->createRow(db, QStringLiteral("Smoke test row"), {{QStringLiteral("status"), QStringLiteral("doing")}});
        settle(80);
        QCOMPARE(int(m_ws->rows(db).size()), before + 1);
        m_ws->setProp(row, QStringLiteral("priority"), QStringLiteral("high"));
        Schema s = m_ws->schema(db);
        s.views[0].filters = {{QStringLiteral("priority"), QStringLiteral("is"), QStringLiteral("high")}};
        m_ws->setSchema(db, s);
        settle(80);
        m_win->navigate(row);
        settle();
        QCOMPARE(find<PageView>()->pageId(), row);
    }

    void exportsEveryPage()
    {
        const QString out = m_dir.filePath(QStringLiteral("export"));
        for (const PageMeta *m : m_ws->children({})) {
            const auto md = Exporter::exportMarkdown(*m_ws, {m->id}, out);
            QVERIFY2(md.ok(), qPrintable(md.error));
            const auto html = Exporter::exportHtml(*m_ws, m->id, out + QStringLiteral("/p.html"));
            QVERIFY(html.ok());
        }
        const auto pdf = Exporter::exportPdf(*m_ws, m_welcome, out + QStringLiteral("/w.pdf"));
        QVERIFY(pdf.ok());
    }

    void smallWindowLayout()
    {
        m_win->resize(880, 560);
        for (const QString &t : {QStringLiteral("Welcome to Nebula Note"), QStringLiteral("Projects"), QStringLiteral("Reading List"),
                                 QStringLiteral("Journal")}) {
            m_win->navigate(pageNamed(t));
            settle(20);
        }
        m_win->goHome();
        settle(20);
        m_win->resize(1280, 820);
        QVERIFY(true);
    }

    void versionHistoryKeepsAndRestores()
    {
        const QString page = m_ws->createPage({}, QStringLiteral("History test"), {Block::make(BlockType::Paragraph, QStringLiteral("First draft"))});
        m_win->navigate(page);
        settle();
        BlockEditor *ed = find<PageView>()->editor();
        ed->focusEnd(false);
        QTest::keyClicks(ed, QStringLiteral(" and more"));
        ed->flush();
        // The state before the edit became the first version.
        QCOMPARE(m_ws->versions(page).size(), 1);
        QCOMPARE(m_ws->versionContent(m_ws->versions(page).first().id).first().plainText(), QStringLiteral("First draft"));
        // Further edits within ten minutes don't pile up versions.
        QTest::keyClicks(ed, QStringLiteral("!"));
        ed->flush();
        QCOMPARE(m_ws->versions(page).size(), 1);
        m_win->openVersionHistory(page);
        settle();
        auto *history = m_win->findChild<VersionHistory *>();
        QVERIFY(history);
        QCOMPARE(history->list()->count(), 2); // current + one version
        history->list()->setCurrentRow(1);
        QCOMPARE(history->preview()->blocks().first().plainText(), QStringLiteral("First draft"));
        history->restoreSelected();
        settle();
        QCOMPARE(m_ws->content(page).first().plainText(), QStringLiteral("First draft"));
        // The replaced state was kept, so the restore can be undone.
        QCOMPARE(m_ws->versions(page).size(), 2);
        QCOMPARE(find<PageView>()->editor()->blocks().first().plainText(), QStringLiteral("First draft"));
    }

    void customizePageControlsMargins()
    {
        const QString target = m_ws->createPage({}, QStringLiteral("Linked to"));
        const QString source = m_ws->createPage({}, QStringLiteral("Links here"));
        Block link;
        link.type = BlockType::PageLink;
        link.pageId = target;
        m_ws->setContent(source, {link});
        m_ws->setContent(target, {Block::make(BlockType::Heading1, QStringLiteral("One")), Block::make(BlockType::Heading2, QStringLiteral("Two")),
                                  Block::make(BlockType::Paragraph, QStringLiteral("Body"))});
        m_win->navigate(target);
        settle(80);
        auto *page = find<PageView>();
        QWidget *backlinks = page->findChild<QWidget *>(QStringLiteral("backlinks"));
        QVERIFY(backlinks && !backlinks->isHidden());
        PageOptions o = m_ws->options(target);
        o.backlinks = QStringLiteral("off");
        o.showToc = false;
        m_ws->setOptions(target, o);
        settle(80);
        QVERIFY(backlinks->isHidden());
        QWidget *outline = page->findChild<QWidget *>(QStringLiteral("outlineRail"));
        QVERIFY(outline && outline->isHidden());
        o.backlinks = QStringLiteral("popover");
        m_ws->setOptions(target, o);
        settle(80);
        QVERIFY(!backlinks->isHidden());
        QCOMPARE(backlinks->height(), 34); // just the count; the list opens in a menu
    }

    void inboxCollectsRemindersAndComments()
    {
        const QString page = m_ws->createPage({}, QStringLiteral("Reminder page"));
        Block todo = Block::make(BlockType::Todo, QStringLiteral("Call mission control "));
        todo.id = Block::newId();
        Span date;
        date.text = QString(QChar::ObjectReplacementCharacter);
        date.date = QDateTime::currentDateTime().addSecs(-120).toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
        date.remind = true;
        todo.spans.append(date);
        m_ws->setContent(page, {todo});
        settle();
        QVERIFY(!m_ws->reminders().isEmpty());
        auto *sidebar = find<Sidebar>();
        sidebar->updateInboxCount();
        QVERIFY(sidebar->inboxCount() >= 1);
        m_ws->addComment(page, todo.id, QStringLiteral("Call"), QStringLiteral("Don't forget the time zone"));
        m_win->openInbox(m_win->mapToGlobal(QPoint(300, 100)));
        settle();
        InboxPopover *inbox = nullptr;
        for (QWidget *w : QApplication::topLevelWidgets())
            if (auto *p = qobject_cast<InboxPopover *>(w); p && p->isVisible())
                inbox = p;
        QVERIFY(inbox);
        QVERIFY(inbox->itemCount() >= 2);
        // Opening an item goes to its page.
        emit inbox->openPage(page, todo.id);
        settle();
        QCOMPARE(m_win->currentPage(), page);
        inbox->close();
        // A reminder that just came due is announced once.
        m_win->checkReminders();
        settle();
        auto *toast = m_win->findChild<Toast *>(QStringLiteral("toast"));
        QVERIFY(toast && toast->message().contains(QStringLiteral("Call mission control")));
        // Done reminders leave the badge.
        for (const Workspace::Reminder &r : m_ws->reminders())
            m_ws->setReminderDone(r.id, true);
        sidebar->updateInboxCount();
        QCOMPARE(sidebar->inboxCount(), 0);
    }

    void quickFindFiltersAndSorts()
    {
        const QString parent = m_ws->createPage({}, QStringLiteral("Comet archive"));
        const QString a = m_ws->createPage(parent, QStringLiteral("Comet Halley"), {Block::make(BlockType::Paragraph, QStringLiteral("periodic"))});
        const QString b = m_ws->createPage({}, QStringLiteral("Notes"), {Block::make(BlockType::Paragraph, QStringLiteral("a comet passed by"))});
        settle();
        auto *qf = new QuickFind(m_win);
        qf->open();
        settle();
        auto *field = qf->findChild<SearchField *>();
        QVERIFY(field);
        field->setText(QStringLiteral("comet"));
        settle();
        QStringList ids = qf->resultIds();
        QVERIFY(ids.contains(a) && ids.contains(b) && ids.contains(parent));
        Workspace::SearchOptions o;
        o.titleOnly = true;
        qf->setSearchOptions(o);
        ids = qf->resultIds();
        QVERIFY(ids.contains(a) && !ids.contains(b));
        o.titleOnly = false;
        o.inPage = parent;
        qf->setSearchOptions(o);
        ids = qf->resultIds();
        ids.sort();
        QStringList inside{parent, a}; // the page and what's inside it
        inside.sort();
        QCOMPARE(ids, inside);
        o.inPage.clear();
        o.sort = Workspace::SearchOptions::CreatedNewest;
        qf->setSearchOptions(o);
        ids = qf->resultIds();
        QCOMPARE(ids.first(), b);
        qf->dismiss();
        settle();
    }

    void sidebarListsDatabaseViews()
    {
        const QString db = pageNamed(QStringLiteral("Projects"));
        QStringList &expanded = Settings::instance().expanded;
        if (!expanded.contains(db))
            expanded.append(db);
        auto *tree = find<PageTree>();
        tree->rebuild();
        settle();
        // Find the first view row of Projects by painting positions: click each view row.
        const Schema s = m_ws->schema(db);
        QVERIFY(s.views.size() >= 3);
        // The rows are internal; drive it through the signal the rows emit.
        QSignalSpy spy(tree, &PageTree::openView);
        bool clicked = false;
        for (int y = 0; y < tree->height() && !clicked; y += 4) {
            QMouseEvent move(QEvent::MouseMove, QPointF(60, y), tree->mapToGlobal(QPointF(60, y)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tree, &move);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(60, y), tree->mapToGlobal(QPointF(60, y)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(60, y), tree->mapToGlobal(QPointF(60, y)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tree, &release);
            if (!spy.isEmpty())
                clicked = true;
        }
        QVERIFY(clicked);
        QCOMPARE(spy.first().at(0).toString(), db);
        settle();
        QCOMPARE(m_win->currentPage(), db);
        QCOMPARE(find<DatabaseView>()->body()->currentViewId(), spy.first().at(1).toString());
        expanded.removeAll(db);
        tree->rebuild();
    }

    void tabsKeepTheirOwnHistory()
    {
        const QString a = pageNamed(QStringLiteral("Journal"));
        const QString b = pageNamed(QStringLiteral("Reading List"));
        m_win->navigate(a);
        settle();
        QCOMPARE(m_win->tabCount(), 1);
        QVERIFY(m_win->tabStrip()->isHidden());
        m_win->newTab(b);
        settle();
        QCOMPARE(m_win->tabCount(), 2);
        QCOMPARE(m_win->currentTab(), 1);
        QVERIFY(!m_win->tabStrip()->isHidden());
        QCOMPARE(m_win->currentPage(), b);
        m_win->navigate(m_welcome);
        settle();
        m_win->switchTab(0);
        settle();
        QCOMPARE(m_win->currentPage(), a);
        m_win->switchTab(1);
        settle();
        QCOMPARE(m_win->currentPage(), m_welcome);
        // Back goes through this tab's own history.
        emit find<TopBar>()->back();
        settle();
        QCOMPARE(m_win->currentPage(), b);
        // Ctrl+T opens a home tab; Ctrl+W closes it.
        QTest::keyClick(m_win, Qt::Key_T, Qt::ControlModifier);
        settle();
        QCOMPARE(m_win->tabCount(), 3);
        QVERIFY(m_win->currentPage().isEmpty());
        QTest::keyClick(m_win, Qt::Key_W, Qt::ControlModifier);
        settle();
        QCOMPARE(m_win->tabCount(), 2);
        m_win->closeTab(0);
        settle();
        QCOMPARE(m_win->tabCount(), 1);
        QCOMPARE(m_win->currentPage(), b);
        QVERIFY(m_win->tabStrip()->isHidden());
    }

    void newWindowsOpenPages()
    {
        const QString page = pageNamed(QStringLiteral("Ideas"));
        MainWindow *w = m_win->newWindow(page);
        QVERIFY(w);
        settle();
        QCOMPARE(w->currentPage(), page);
        QVERIFY(w->isVisible());
        w->close();
        settle();
    }

    void reopenKeepsEverything()
    {
        m_win->navigate(m_welcome);
        settle();
        auto *page = find<PageView>();
        BlockEditor *ed = page->editor();
        ed->focusEnd(true);
        QTest::keyClicks(ed, QStringLiteral("Saved on the way out"));
        settle();
        m_win->close();
        settle();
        QCOMPARE(m_ws->content(m_welcome).last().plainText(), QStringLiteral("Saved on the way out"));
    }
};

int main(int argc, char **argv)
{
    QApplication::setApplicationName(QStringLiteral("nebula-tests"));
    QApplication app(argc, argv);
    // Settings go to Qt's test locations, never the user's config.
    QStandardPaths::setTestModeEnabled(true);
    PluralTranslator plurals;
    QCoreApplication::installTranslator(&plurals);
    installExportRenderers();
    AppTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_app.moc"
