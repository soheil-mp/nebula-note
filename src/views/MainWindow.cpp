#include "MainWindow.h"

#include "DatabaseView.h"
#include "History.h"
#include "HomeView.h"
#include "Overlays.h"
#include "PageView.h"
#include "Peek.h"
#include "Pickers.h"
#include "Sidebar.h"
#include "Tabs.h"
#include "TopBar.h"
#include "core/Exporter.h"
#include "core/Importer.h"
#include "core/Settings.h"
#include "core/Templates.h"
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "platform/WindowEffects.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QAbstractTextDocumentLayout>
#include <QKeyEvent>
#include <QTextBlock>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QShortcut>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Nebula Note"));
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(880, 560);
    resize(1360, 880);
    WindowEffects::attachShadow(this, Theme::kRadius);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    m_sidebar = new Sidebar(this);
    m_sidebar->setFixedWidth(Settings::instance().sidebarWidth);
    root->addWidget(m_sidebar);
    auto *right = new QVBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(0);
    m_topBar = new TopBar(this);
    right->addWidget(m_topBar);
    m_tabStrip = new TabStrip(this);
    m_tabStrip->hide();
    right->addWidget(m_tabStrip);
    connect(m_tabStrip, &TabStrip::activated, this, &MainWindow::switchTab);
    connect(m_tabStrip, &TabStrip::closeRequested, this, &MainWindow::closeTab);
    connect(m_tabStrip, &TabStrip::newTabRequested, this, [this] { newTab(); });
    m_stack = new QStackedWidget(this);
    m_home = new HomeView(m_stack);
    m_page = new PageView(m_stack);
    m_database = new DatabaseView(m_stack);
    for (QWidget *w : {static_cast<QWidget *>(m_home), static_cast<QWidget *>(m_page), static_cast<QWidget *>(m_database)})
        m_stack->addWidget(w);
    right->addWidget(m_stack, 1);
    root->addLayout(right, 1);
    m_page->editor()->setOverlayHost(this);
    m_peek = new Peek(this);
    connect(m_peek, &Peek::openFull, this, [this](const QString &id) { navigate(id); });
    connect(m_peek, &Peek::openPage, this, [this](const QString &id) { navigate(id); });
    connect(m_peek, &Peek::toast, this, [this](const QString &m) { toast(m); });
    m_toast = new Toast(this);
    m_toast->setObjectName(QStringLiteral("toast"));

    m_sidebarAnim.setDuration(Theme::animMs(200));
    m_sidebarAnim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_sidebarAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_sidebar->setFixedWidth(v.toInt());
        updateBlur();
    });
    connect(&m_sidebarAnim, &QVariantAnimation::finished, this, [this] {
        if (Settings::instance().sidebarCollapsed)
            m_sidebar->hide();
        updateBlur();
    });
    if (Settings::instance().sidebarCollapsed) {
        m_sidebar->hide();
        m_topBar->setSidebarCollapsed(true);
    }

    // ---- sidebar ----
    connect(m_sidebar, &Sidebar::openPage, this, [this](const QString &id) { navigate(id); });
    connect(m_sidebar, &Sidebar::openView, this, [this](const QString &db, const QString &view) {
        navigate(db);
        if (currentPage() == db)
            m_database->selectView(view);
    });
    connect(m_sidebar, &Sidebar::newPage, this, &MainWindow::newPage);
    connect(m_sidebar, &Sidebar::searchRequested, this, &MainWindow::openQuickFind);
    connect(m_sidebar, &Sidebar::homeRequested, this, [this] { goHome(); });
    connect(m_sidebar, &Sidebar::templatesRequested, this, [this] { openTemplates(); });
    connect(m_sidebar, &Sidebar::inboxRequested, this, &MainWindow::openInbox);
    connect(m_sidebar, &Sidebar::importRequested, this, [this](const QPoint &pos) { importFiles(QString(), pos); });
    connect(m_sidebar, &Sidebar::settingsRequested, this, &MainWindow::openSettings);
    connect(m_sidebar, &Sidebar::trashRequested, this, [this](const QPoint &pos) { openTrash(pos); });
    connect(m_sidebar, &Sidebar::collapseRequested, this, &MainWindow::toggleSidebar);
    connect(m_sidebar, &Sidebar::pageMenuRequested, this, [this](const QString &id, const QPoint &pos) { showPageMenu(id, pos, true); });
    connect(m_sidebar, &Sidebar::widthDragged, this, &MainWindow::applySidebarWidth);

    // ---- top bar ----
    connect(m_topBar, &TopBar::back, this, [this] {
        if (m_historyIndex > 0) {
            --m_historyIndex;
            const QString id = m_history[m_historyIndex];
            id.isEmpty() ? goHome(false) : navigate(id, false);
        }
    });
    connect(m_topBar, &TopBar::forward, this, [this] {
        if (m_historyIndex + 1 < m_history.size()) {
            ++m_historyIndex;
            const QString id = m_history[m_historyIndex];
            id.isEmpty() ? goHome(false) : navigate(id, false);
        }
    });
    connect(m_topBar, &TopBar::toggleSidebar, this, &MainWindow::toggleSidebar);
    connect(m_topBar, &TopBar::navigate, this, [this](const QString &id) { id.isEmpty() ? goHome() : navigate(id); });
    connect(m_topBar, &TopBar::favoriteToggled, this, [this] {
        Workspace *ws = Workspace::instance();
        const QString id = currentPage();
        if (const PageMeta *m = ws->page(id)) {
            const bool fav = m->favorite > 0;
            ws->setFavorite(id, !fav);
            toast(fav ? tr("Removed from Favorites") : tr("Added to Favorites"));
        }
    });
    connect(m_topBar, &TopBar::pageMenuRequested, this, [this](const QPoint &pos) { showPageMenu(currentPage(), pos, false); });
    connect(m_topBar, &TopBar::minimizeRequested, this, &QWidget::showMinimized);
    connect(m_topBar, &TopBar::maximizeRequested, this, [this] { isMaximized() ? showNormal() : showMaximized(); });
    connect(m_topBar, &TopBar::closeRequested, this, &QWidget::close);
    connect(m_topBar, &TopBar::currentClicked, this, [this] {
        if (m_stack->currentWidget() == m_page)
            m_page->focusDefault();
    });

    // ---- home ----
    connect(m_home, &HomeView::openPage, this, [this](const QString &id) { navigate(id); });
    connect(m_home, &HomeView::newPage, this, [this] { newPage(QString()); });
    connect(m_home, &HomeView::newDatabase, this, &MainWindow::newDatabase);
    connect(m_home, &HomeView::importRequested, this, [this] { importFiles(); });
    connect(m_home, &HomeView::templatesRequested, this, [this] { openTemplates(); });
    connect(m_home, &HomeView::searchRequested, this, &MainWindow::openQuickFind);

    // ---- pages ----
    connect(m_page, &PageView::openPage, this, [this](const QString &id) { navigate(id); });
    connect(m_page, &PageView::openBlock, this, [this](const QString &page, const QString &block) {
        navigate(page);
        if (currentPage() == page)
            m_page->revealBlock(block);
    });
    connect(m_page, &PageView::toast, this, [this](const QString &m) { toast(m); });
    connect(m_page, &PageView::templatesRequested, this, [this](const QString &id) { openTemplates(id); });
    connect(m_page, &PageView::importRequested, this, [this](const QString &id) { importFiles(id); });
    connect(m_page, &PageView::convertedToDatabase, this, [this](const QString &id) { navigate(id, false); });
    connect(m_page->editor(), &BlockEditor::toast, this, [this](const QString &m) { toast(m); });
    connect(m_database, &DatabaseView::openPage, this, [this](const QString &id) { navigate(id); });
    connect(m_database, &DatabaseView::toast, this, [this](const QString &m) { toast(m); });

    // ---- workspace ----
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::treeChanged, this, [this] {
            // The open page went to the trash (from elsewhere).
            const QString id = currentPage();
            const PageMeta *m = Workspace::instance()->page(id);
            if (!id.isEmpty() && (!m || m->isTrashed()))
                goHome(false);
        });
    }
    connect(&Settings::instance(), &Settings::changed, this, [this] {
        updateBlur();
        update();
    });

    // ---- shortcuts ----
    auto shortcut = [this](const QKeySequence &seq, std::function<void()> fn) {
        auto *s = new QShortcut(seq, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_P), [this] { openQuickFind(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_K), [this] { openQuickFind(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_N), [this] { newPage(QString()); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), [this] { emit m_topBar->back(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_BracketRight), [this] { emit m_topBar->forward(); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Left), [this] { emit m_topBar->back(); });
    shortcut(QKeySequence(Qt::ALT | Qt::Key_Right), [this] { emit m_topBar->forward(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Backslash), [this] { toggleSidebar(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { openSettings(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U), [this] {
        const PageMeta *m = Workspace::instance()->page(currentPage());
        if (m && !m->parentId.isEmpty())
            navigate(m->parentId);
        else if (m)
            goHome();
    });
    shortcut(QKeySequence::Find, [this] {
        if (m_stack->currentWidget() == m_page)
            m_page->showFind();
    });
    shortcut(QKeySequence(Qt::Key_F11), [this] { isFullScreen() ? showNormal() : showFullScreen(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { newTab(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { closeTab(m_currentTab); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Tab), [this] { switchTab((m_currentTab + 1) % int(m_tabs.size())); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab), [this] { switchTab((m_currentTab + int(m_tabs.size()) - 1) % int(m_tabs.size())); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] { newWindow(currentPage()); });
    if (Workspace *ws = Workspace::instance())
        connect(ws, &Workspace::pageChanged, this, [this] {
            if (m_tabs.size() > 1)
                updateTabs();
        });

    // Reminders: a toast (and a desktop notification) when one comes due.
    m_reminderTimer.setInterval(30000);
    connect(&m_reminderTimer, &QTimer::timeout, this, &MainWindow::checkReminders);
    m_reminderTimer.start();
    QTimer::singleShot(2000, this, &MainWindow::checkReminders);
}

void MainWindow::checkReminders()
{
    Workspace *ws = Workspace::instance();
    if (!ws)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const Workspace::Reminder &r : ws->reminders()) {
        // Only ones that came due recently: old ones wait in the inbox.
        if (r.at > now || r.at < now - 6LL * 3600 * 1000 || m_notified.contains(r.id))
            continue;
        m_notified.insert(r.id);
        const QString text = r.text.isEmpty() ? ws->title(r.pageId) : r.text;
        const QString page = r.pageId;
        const QString block = r.id.section(QLatin1Char(':'), 1, 1);
        toast(tr("Reminder: %1").arg(text.left(80)), tr("Open"), [this, page, block] {
            navigate(page);
            if (currentPage() == page)
                m_page->revealBlock(block);
        });
        const QString notifier = QStandardPaths::findExecutable(QStringLiteral("notify-send"));
        if (!notifier.isEmpty() && !qEnvironmentVariableIsSet("NEBULA_NO_NOTIFY"))
            QProcess::startDetached(notifier, {QStringLiteral("--app-name=Nebula Note"), QStringLiteral("--icon=" NEBULA_APP_ID),
                                               QStringLiteral("Reminder"), text});
    }
    m_sidebar->updateInboxCount();
}

void MainWindow::openInbox(const QPoint &globalPos)
{
    auto *inbox = new InboxPopover(this);
    connect(inbox, &InboxPopover::openPage, this, [this](const QString &page, const QString &block) {
        navigate(page);
        if (!block.isEmpty() && currentPage() == page)
            m_page->revealBlock(block);
    });
    inbox->popupAt(globalPos);
}

void MainWindow::openVersionHistory(const QString &pageId)
{
    flushAll();
    auto *history = new VersionHistory(this, pageId);
    connect(history, &VersionHistory::restored, this, [this](const QString &id) {
        if (currentPage() == id)
            navigate(id, false);
        toast(tr("Version restored. The page as it was is kept in the history."));
    });
    history->open();
}

// ---- navigation ------------------------------------------------------------------------

QString MainWindow::currentPage() const
{
    return m_historyIndex >= 0 && m_historyIndex < m_history.size() ? m_history[m_historyIndex] : QString();
}

void MainWindow::navigate(const QString &pageId, bool push)
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws->page(pageId);
    if (!m || m->isTrashed()) {
        toast(tr("That page is in the trash or no longer exists."));
        return;
    }
    flushAll();
    m_peek->closePeek();
    if (push) {
        while (m_history.size() > m_historyIndex + 1)
            m_history.removeLast();
        if (m_history.isEmpty() || m_history.last() != pageId)
            m_history.append(pageId);
        m_historyIndex = int(m_history.size()) - 1;
    }
    ws->markVisited(pageId);
    if (m->isDatabase()) {
        m_database->setDatabase(pageId);
        m_stack->setCurrentWidget(m_database);
    } else {
        m_page->setPage(pageId);
        m_stack->setCurrentWidget(m_page);
        m_page->focusDefault();
    }
    m_sidebar->setCurrentPage(pageId);
    m_topBar->setPage(pageId);
    updateNavButtons();
    Settings::instance().lastPage = pageId;
    setWindowTitle(ws->title(pageId) + QStringLiteral(" — Nebula Note"));
    updateTabs();
}

void MainWindow::openRowIn(const QString &pageId, const QString &mode)
{
    if (mode == QLatin1String("full") || !Workspace::instance()->exists(pageId)) {
        navigate(pageId);
        return;
    }
    m_peek->setArea(QRect(m_stack->mapTo(this, QPoint(0, 0)), m_stack->size()));
    m_peek->showPage(pageId, mode);
}

void MainWindow::goHome(bool push)
{
    flushAll();
    m_peek->closePeek();
    if (push) {
        while (m_history.size() > m_historyIndex + 1)
            m_history.removeLast();
        if (m_history.isEmpty() || !m_history.last().isEmpty())
            m_history.append(QString());
        m_historyIndex = int(m_history.size()) - 1;
    }
    m_home->refresh();
    m_stack->setCurrentWidget(m_home);
    m_sidebar->setHomeActive(true);
    m_topBar->setPage(QString());
    updateNavButtons();
    Settings::instance().lastPage.clear();
    setWindowTitle(QStringLiteral("Nebula Note"));
    updateTabs();
}

// ---- tabs & windows -------------------------------------------------------------------

void MainWindow::updateTabs()
{
    Workspace *ws = Workspace::instance();
    QList<TabStrip::Tab> tabs;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QString id = i == m_currentTab ? currentPage() : m_tabs[i].history.value(m_tabs[i].index);
        const PageMeta *m = ws->page(id);
        tabs.append({id.isEmpty() || !m ? tr("Home") : ws->title(id), m ? m->icon : QStringLiteral("glyph:home")});
    }
    m_tabStrip->setTabs(tabs, m_currentTab);
    m_tabStrip->setVisible(m_tabs.size() > 1);
}

void MainWindow::newTab(const QString &pageId)
{
    flushAll();
    m_tabs[m_currentTab] = {m_history, m_historyIndex};
    m_tabs.insert(m_currentTab + 1, Tab());
    ++m_currentTab;
    m_history.clear();
    m_historyIndex = -1;
    if (pageId.isEmpty() || !Workspace::instance()->exists(pageId))
        goHome();
    else
        navigate(pageId);
    updateTabs();
}

void MainWindow::switchTab(int index)
{
    if (index < 0 || index >= m_tabs.size() || index == m_currentTab)
        return;
    flushAll();
    m_tabs[m_currentTab] = {m_history, m_historyIndex};
    m_currentTab = index;
    m_history = m_tabs[index].history;
    m_historyIndex = m_tabs[index].index;
    const QString id = currentPage();
    if (id.isEmpty() || !Workspace::instance()->exists(id))
        goHome(m_history.isEmpty());
    else
        navigate(id, false);
    updateTabs();
}

void MainWindow::closeTab(int index)
{
    if (m_tabs.size() <= 1 || index < 0 || index >= m_tabs.size())
        return;
    flushAll();
    if (index == m_currentTab) {
        // Show the neighbour first, then drop the tab.
        const int next = index + 1 < m_tabs.size() ? index + 1 : index - 1;
        switchTab(next);
    }
    m_tabs.removeAt(index);
    if (m_currentTab > index)
        --m_currentTab;
    updateTabs();
}

MainWindow *MainWindow::newWindow(const QString &pageId)
{
    flushAll();
    auto *w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->resize(size());
    w->move(pos() + QPoint(36, 36));
    w->show();
    if (!pageId.isEmpty() && Workspace::instance()->exists(pageId))
        w->navigate(pageId);
    else
        w->goHome();
    return w;
}

void MainWindow::updateNavButtons()
{
    m_topBar->setNavigation(m_historyIndex > 0, m_historyIndex + 1 < m_history.size());
}

void MainWindow::restoreSession()
{
    const QString last = Settings::instance().lastPage;
    const PageMeta *m = Workspace::instance()->page(last);
    if (m && !m->isTrashed())
        navigate(last);
    else
        goHome();
}

void MainWindow::handleArguments(const QStringList &arguments)
{
    // Files passed on the command line are imported.
    for (const QString &arg : arguments.mid(1)) {
        if (arg.startsWith(QLatin1Char('-')))
            continue;
        const QFileInfo fi(arg);
        if (!fi.exists())
            continue;
        const Importer::Result r = Importer::importPath(*Workspace::instance(), fi.absoluteFilePath());
        if (r.ok()) {
            navigate(r.rootId);
            toast(tr("Imported %1").arg(r.summary()));
        }
    }
}

void MainWindow::runAction(const QString &action)
{
    if (action.startsWith(QLatin1String("scroll:"))) {
        if (auto *area = qobject_cast<QAbstractScrollArea *>(m_stack->currentWidget()))
            area->verticalScrollBar()->setValue(action.mid(7).toInt());
        return;
    }
    if (action.startsWith(QLatin1String("view:"))) {
        m_database->selectViewNamed(action.mid(5));
        return;
    }
    if (action.startsWith(QLatin1String("peek:")) || action.startsWith(QLatin1String("center:"))) {
        // Opens the row with this title in a side (peek:) or center (center:) peek.
        const bool center = action.startsWith(QLatin1String("center:"));
        const QString title = action.mid(center ? 7 : 5);
        Workspace *ws = Workspace::instance();
        for (const PageMeta *m : ws->rows(m_database->databaseId()))
            if (m->title == title) {
                openRowIn(m->id, center ? QStringLiteral("center") : QStringLiteral("side"));
                return;
            }
        return;
    }
    if (action.startsWith(QLatin1String("expand:")) || action.startsWith(QLatin1String("tab:"))) {
        // expand:<title> unfolds a page in the sidebar; tab:<title> opens it in a new tab.
        const QString title = action.section(QLatin1Char(':'), 1);
        for (const PageMeta *m : Workspace::instance()->allLive())
            if (m->title == title) {
                if (action.startsWith(QLatin1String("tab:"))) {
                    newTab(m->id);
                } else {
                    if (!Settings::instance().expanded.contains(m->id))
                        Settings::instance().expanded.append(m->id);
                    m_sidebar->tree()->rebuild();
                }
                break;
            }
        return;
    }
    if (action == QLatin1String("history")) {
        if (!currentPage().isEmpty())
            openVersionHistory(currentPage());
        return;
    }
    if (action == QLatin1String("inbox")) {
        openInbox(mapToGlobal(QPoint(m_sidebar->width() + 8, 90)));
        return;
    }
    if (action == QLatin1String("quickfind"))
        openQuickFind();
    else if (action == QLatin1String("settings"))
        openSettings();
    else if (action == QLatin1String("templates"))
        openTemplates();
    else if (action == QLatin1String("trash"))
        openTrash(mapToGlobal(QPoint(m_sidebar->width() + 8, height() - 12)));
    else if (action == QLatin1String("select") && m_stack->currentWidget() == m_page) {
        BlockEditor *ed = m_page->editor();
        ed->setFocus();
        QTextCursor c(ed->document());
        c.movePosition(QTextCursor::NextWord);
        c.movePosition(QTextCursor::NextWord);
        c.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        c.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
        ed->setTextCursor(c);
    } else if (action == QLatin1String("mention") && m_stack->currentWidget() == m_page) {
        BlockEditor *ed = m_page->editor();
        ed->focusEnd(true);
        for (const QString &t : {QStringLiteral("See "), QStringLiteral("@")}) {
            for (const QChar ch : t) {
                QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
                QApplication::sendEvent(ed, &key);
            }
        }
    } else if (action.startsWith(QLatin1String("hover:")) && m_stack->currentWidget() == m_page) {
        BlockEditor *ed = m_page->editor();
        const QTextBlock blk = ed->document()->findBlockByNumber(action.mid(6).toInt());
        const QRectF r = ed->document()->documentLayout()->blockBoundingRect(blk);
        const QPoint pos(BlockEditor::gutter() + 40, int(r.top() + 10));
        QMouseEvent move(QEvent::MouseMove, pos, ed->viewport()->mapToGlobal(pos), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(ed->viewport(), &move);
    } else if (action == QLatin1String("slash") && m_stack->currentWidget() == m_page) {
        BlockEditor *ed = m_page->editor();
        ed->focusEnd(true);
        QKeyEvent slash(QEvent::KeyPress, Qt::Key_Slash, Qt::NoModifier, QStringLiteral("/"));
        QApplication::sendEvent(ed, &slash);
    }
}

void MainWindow::flushAll()
{
    m_page->flush();
    m_database->flush();
    if (m_peek && m_peek->isOpen())
        m_peek->pageView()->flush();
}

void MainWindow::toast(const QString &message, const QString &action, std::function<void()> onAction)
{
    m_toast->show(message, action, std::move(onAction));
}

// ---- actions ---------------------------------------------------------------------------

void MainWindow::newPage(const QString &parentId)
{
    Workspace *ws = Workspace::instance();
    flushAll();
    const QString id = ws->createPage(parentId);
    if (!parentId.isEmpty()) {
        QStringList &expanded = Settings::instance().expanded;
        if (!expanded.contains(parentId))
            expanded.append(parentId);
        m_sidebar->tree()->rebuild();
    }
    navigate(id);
}

void MainWindow::newDatabase()
{
    Workspace *ws = Workspace::instance();
    Schema s = Schema::basic();
    Property status;
    status.id = QStringLiteral("status");
    status.name = tr("Status");
    status.type = PropType::Status;
    status.options = {{QStringLiteral("todo"), tr("Not started"), 2, 0},
                      {QStringLiteral("doing"), tr("In progress"), 5, 1},
                      {QStringLiteral("done"), tr("Done"), 8, 2}};
    s.props.insert(1, status);
    s.views = {Schema::makeView(s, ViewType::Table), Schema::makeView(s, ViewType::Board)};
    const QString id = ws->createDatabase(QString(), QString(), s);
    navigate(id);
}

void MainWindow::openQuickFind()
{
    auto *qf = new QuickFind(this);
    connect(qf, &QuickFind::openPage, this, [this](const QString &id) { navigate(id); });
    connect(qf, &QuickFind::createPage, this, [this](const QString &title) {
        const QString id = Workspace::instance()->createPage(QString(), title);
        navigate(id);
    });
    connect(qf, &QuickFind::action, this, [this](const QString &a) {
        if (a == QLatin1String("new"))
            newPage(QString());
        else if (a == QLatin1String("home"))
            goHome();
        else if (a == QLatin1String("templates"))
            openTemplates();
        else if (a == QLatin1String("import"))
            importFiles();
        else if (a == QLatin1String("settings"))
            openSettings();
        else if (a == QLatin1String("sidebar"))
            toggleSidebar();
        else if (a == QLatin1String("trash"))
            openTrash(mapToGlobal(QPoint(width() / 2 - 200, 120)), false);
    });
    qf->open();
}

void MainWindow::openSettings()
{
    auto *s = new SettingsPanel(this);
    connect(s, &SettingsPanel::importRequested, this, [this] { importFiles(); });
    connect(s, &SettingsPanel::exportAllRequested, this, &MainWindow::exportAll);
    s->open();
}

void MainWindow::openTemplates(const QString &targetPage)
{
    auto *g = new TemplateGallery(this);
    connect(g, &TemplateGallery::picked, this, [this, targetPage](const QString &templateId) {
        Workspace *ws = Workspace::instance();
        flushAll();
        QString page = targetPage;
        const PageMeta *m = ws->page(page);
        if (!m || m->isRow() || !ws->content(page).isEmpty() || m->isDatabase())
            page = ws->createPage(QString());
        Templates::apply(*ws, templateId, page);
        navigate(page, page != targetPage);
        if (page == targetPage && m_stack->currentWidget() == m_page)
            m_page->editor()->reload();
    });
    g->open();
}

void MainWindow::openTrash(const QPoint &globalPos, bool growUp)
{
    // From the sidebar the card sits beside the Trash item and grows upwards.
    auto *t = new TrashPopover(this);
    t->adjustSize();
    t->popupAt(growUp ? globalPos - QPoint(0, t->height() - 2 * Popover::kShadow + 6) : globalPos);
}

void MainWindow::importFiles(const QString &parentId, const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    Theme::addMenuSection(menu, tr("Import"));
    QAction *notion = menu->addAction(Icons::icon(QStringLiteral("download")), tr("Notion export (.zip, Markdown or HTML)…"));
    QAction *files = menu->addAction(Icons::icon(QStringLiteral("page")), tr("Markdown, HTML, text or CSV files…"));
    QAction *word = menu->addAction(Icons::icon(QStringLiteral("file")), tr("Word document (.docx)…"));
    QAction *evernote = menu->addAction(Icons::icon(QStringLiteral("notebook")), tr("Evernote export (.enex)…"));
    QAction *folder = menu->addAction(Icons::icon(QStringLiteral("folder")), tr("Folder of Markdown or HTML (Obsidian, Notion unzipped)…"));
    const QString home = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    auto run = [this, parentId](const QStringList &paths) {
        if (paths.isEmpty())
            return;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        Importer::Result total;
        QString first;
        for (const QString &p : paths) {
            const Importer::Result r = Importer::importPath(*Workspace::instance(), p, parentId);
            if (!r.ok()) {
                total.error = r.error;
                continue;
            }
            if (first.isEmpty())
                first = r.rootId;
            total.pages += r.pages;
            total.databases += r.databases;
            total.rows += r.rows;
            total.images += r.images;
        }
        QApplication::restoreOverrideCursor();
        if (!first.isEmpty()) {
            navigate(first);
            toast(tr("Imported %1").arg(total.summary()));
        } else {
            toast(tr("Import failed: %1").arg(total.error));
        }
    };
    connect(notion, &QAction::triggered, this, [this, run, home] {
        run(QFileDialog::getOpenFileNames(this, tr("Import a Notion export"), home, tr("Notion export (*.zip)")));
    });
    connect(files, &QAction::triggered, this, [this, run, home] {
        run(QFileDialog::getOpenFileNames(this, tr("Import files"), home,
                                          tr("Markdown, HTML, text or CSV (*.md *.markdown *.txt *.html *.htm *.csv)")));
    });
    connect(word, &QAction::triggered, this, [this, run, home] {
        run(QFileDialog::getOpenFileNames(this, tr("Import Word documents"), home, tr("Word document (*.docx)")));
    });
    connect(evernote, &QAction::triggered, this, [this, run, home] {
        run(QFileDialog::getOpenFileNames(this, tr("Import an Evernote export"), home, tr("Evernote export (*.enex)")));
    });
    connect(folder, &QAction::triggered, this, [this, run, home] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Import a folder"), home);
        if (!dir.isEmpty())
            run({dir});
    });
    menu->popup(globalPos.isNull() ? QCursor::pos() : globalPos);
}

void MainWindow::exportPage(const QString &pageId, const QString &format)
{
    Workspace *ws = Workspace::instance();
    flushAll();
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString base = Exporter::safeFileName(ws->title(pageId));
    Exporter::Result r;
    if (format == QLatin1String("md")) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Export as Markdown into…"), docs);
        if (dir.isEmpty())
            return;
        r = Exporter::exportMarkdown(*ws, {pageId}, dir, true);
    } else if (format == QLatin1String("html")) {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export as HTML"), QDir(docs).filePath(base + QStringLiteral(".html")),
                                                          tr("HTML (*.html)"));
        if (path.isEmpty())
            return;
        r = Exporter::exportHtml(*ws, pageId, path);
    } else {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export as PDF"), QDir(docs).filePath(base + QStringLiteral(".pdf")),
                                                          tr("PDF (*.pdf)"));
        if (path.isEmpty())
            return;
        r = Exporter::exportPdf(*ws, pageId, path);
    }
    if (r.ok()) {
        const QString path = r.path;
        toast(tr("Exported “%1”").arg(QFileInfo(path).fileName()), tr("Show"),
              [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath())); });
    } else {
        toast(tr("Export failed: %1").arg(r.error));
    }
}

void MainWindow::exportAll()
{
    Workspace *ws = Workspace::instance();
    flushAll();
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Export everything into…"),
                                                          QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (dir.isEmpty())
        return;
    QStringList roots;
    for (const PageMeta *m : ws->children(QString()))
        roots.append(m->id);
    const QString target = QDir(dir).filePath(Exporter::safeFileName(ws->name()));
    const Exporter::Result r = Exporter::exportMarkdown(*ws, roots, target, true);
    if (r.ok())
        toast(tr("Exported %n file(s)", nullptr, r.files), tr("Show"), [target] { QDesktopServices::openUrl(QUrl::fromLocalFile(target)); });
    else
        toast(tr("Export failed: %1").arg(r.error));
}

void MainWindow::trashPage(const QString &pageId)
{
    Workspace *ws = Workspace::instance();
    const QString title = ws->title(pageId);
    const bool current = currentPage() == pageId || ws->isInside(currentPage(), pageId);
    flushAll();
    ws->trash(pageId);
    if (current) {
        const PageMeta *m = ws->page(pageId);
        if (m && !m->parentId.isEmpty() && ws->exists(m->parentId) && !ws->page(m->parentId)->isTrashed())
            navigate(m->parentId, true);
        else
            goHome();
    }
    toast(tr("Moved “%1” to the trash").arg(title), tr("Undo"), [this, pageId] {
        Workspace::instance()->restore(pageId);
        navigate(pageId);
    });
}

void MainWindow::movePage(const QString &pageId, const QPoint &globalPos)
{
    auto *picker = new PagePicker(this, tr("Move “%1” to…").arg(Workspace::instance()->title(pageId)), pageId, true);
    connect(picker, &PagePicker::picked, this, [this, pageId](const QString &target) {
        Workspace *ws = Workspace::instance();
        if (ws->move(pageId, target, -1))
            toast(target.isEmpty() ? tr("Moved to the top level") : tr("Moved into “%1”").arg(ws->title(target)));
    });
    picker->popupAt(globalPos);
}

void MainWindow::showPageMenu(const QString &pageId, const QPoint &globalPos, bool fromSidebar)
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws->page(pageId);
    if (!m)
        return;
    QMenu *menu = Theme::makeMenu(this);
    if (!fromSidebar && !m->isDatabase()) {
        // Page style, Notion's ••• menu.
        const PageOptions o = ws->options(pageId);
        Theme::addMenuSection(menu, tr("Style"));
        const QList<QPair<QString, QString>> fonts = {{QStringLiteral("sans"), tr("Default")}, {QStringLiteral("serif"), tr("Serif")},
                                                      {QStringLiteral("mono"), tr("Mono")}};
        for (const auto &[key, name] : fonts) {
            QAction *a = menu->addAction(Icons::icon(QStringLiteral("font")), name);
            a->setCheckable(true);
            a->setChecked(o.font == key);
            connect(a, &QAction::triggered, this, [pageId, key = key] {
                PageOptions po = Workspace::instance()->options(pageId);
                po.font = key;
                Workspace::instance()->setOptions(pageId, po);
            });
        }
        QAction *small = menu->addAction(Icons::icon(QStringLiteral("textsize")), tr("Small text"));
        small->setCheckable(true);
        small->setChecked(o.smallText);
        connect(small, &QAction::toggled, this, [pageId](bool on) {
            PageOptions po = Workspace::instance()->options(pageId);
            po.smallText = on;
            Workspace::instance()->setOptions(pageId, po);
        });
        QAction *wide = menu->addAction(Icons::icon(QStringLiteral("width")), tr("Full width"));
        wide->setCheckable(true);
        wide->setChecked(o.fullWidth);
        connect(wide, &QAction::toggled, this, [pageId](bool on) {
            PageOptions po = Workspace::instance()->options(pageId);
            po.fullWidth = on;
            Workspace::instance()->setOptions(pageId, po);
        });
        QAction *lock = menu->addAction(Icons::icon(o.locked ? QStringLiteral("lock") : QStringLiteral("unlock")), tr("Lock page"));
        lock->setCheckable(true);
        lock->setChecked(o.locked);
        connect(lock, &QAction::toggled, this, [pageId](bool on) {
            PageOptions po = Workspace::instance()->options(pageId);
            po.locked = on;
            Workspace::instance()->setOptions(pageId, po);
        });
        // Customize page: what shows around the content.
        QMenu *custom = menu->addMenu(Icons::icon(QStringLiteral("settings")), tr("Customize page"));
        Theme::polishMenu(custom);
        auto option = [&](const QString &text, bool on, std::function<void(PageOptions &, bool)> set) {
            QAction *a = custom->addAction(text);
            a->setCheckable(true);
            a->setChecked(on);
            connect(a, &QAction::toggled, this, [pageId, set](bool v) {
                PageOptions po = Workspace::instance()->options(pageId);
                set(po, v);
                Workspace::instance()->setOptions(pageId, po);
            });
        };
        option(tr("Table of contents in the margin"), o.showToc, [](PageOptions &po, bool v) { po.showToc = v; });
        option(tr("Page discussions"), o.showDiscussion, [](PageOptions &po, bool v) { po.showDiscussion = v; });
        Theme::addMenuSection(custom, tr("Backlinks"));
        for (const auto &[key, name] : QList<QPair<QString, QString>>{{QStringLiteral("expanded"), tr("Show expanded")},
                                                                      {QStringLiteral("popover"), tr("Show in popover")},
                                                                      {QStringLiteral("off"), tr("Off")}}) {
            QAction *a = custom->addAction(name);
            a->setCheckable(true);
            a->setChecked(o.backlinks == key);
            connect(a, &QAction::triggered, this, [pageId, key = key] {
                PageOptions po = Workspace::instance()->options(pageId);
                po.backlinks = key;
                Workspace::instance()->setOptions(pageId, po);
            });
        }
        menu->addAction(Icons::icon(QStringLiteral("history")), tr("Version history"), this, [this, pageId] { openVersionHistory(pageId); });
        menu->addSeparator();
    } else {
        Theme::addMenuSection(menu, ws->title(pageId));
    }
    if (fromSidebar) {
        menu->addAction(Icons::icon(QStringLiteral("external")), tr("Open"), this, [this, pageId] { navigate(pageId); });
        menu->addAction(Icons::icon(QStringLiteral("tab-new")), tr("Open in new tab"), this, [this, pageId] { newTab(pageId); });
        menu->addAction(Icons::icon(QStringLiteral("window-new")), tr("Open in new window"), this, [this, pageId] { newWindow(pageId); });
        menu->addAction(Icons::icon(QStringLiteral("plus")), tr("Add a page inside"), this, [this, pageId] { newPage(pageId); });
        menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Rename"), this, [this, pageId] { m_sidebar->tree()->renamePage(pageId); });
    }
    const bool fav = m->favorite > 0;
    menu->addAction(Icons::icon(fav ? QStringLiteral("star-filled") : QStringLiteral("star")), fav ? tr("Remove from Favorites") : tr("Add to Favorites"),
                    this, [pageId, fav] { Workspace::instance()->setFavorite(pageId, !fav); });
    menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate"), this, [this, pageId] {
        flushAll();
        const QString copy = Workspace::instance()->duplicate(pageId);
        if (!copy.isEmpty())
            navigate(copy);
    });
    if (!m->isRow())
        menu->addAction(Icons::icon(QStringLiteral("move")), tr("Move to…"), this, [this, pageId, globalPos] { movePage(pageId, globalPos); });
    menu->addAction(Icons::icon(QStringLiteral("link")), tr("Copy title"), this,
                    [pageId] { QApplication::clipboard()->setText(Workspace::instance()->title(pageId)); });
    QMenu *exp = menu->addMenu(Icons::icon(QStringLiteral("upload")), tr("Export"));
    Theme::polishMenu(exp);
    exp->addAction(Icons::icon(QStringLiteral("page")), tr("Markdown (with sub-pages)…"), this, [this, pageId] { exportPage(pageId, QStringLiteral("md")); });
    exp->addAction(Icons::icon(QStringLiteral("globe")), tr("HTML…"), this, [this, pageId] { exportPage(pageId, QStringLiteral("html")); });
    exp->addAction(Icons::icon(QStringLiteral("download")), tr("PDF…"), this, [this, pageId] { exportPage(pageId, QStringLiteral("pdf")); });
    if (!m->isDatabase() && !m->isRow())
        menu->addAction(Icons::icon(QStringLiteral("download")), tr("Import into this page…"), this, [this, pageId] { importFiles(pageId); });
    menu->addSeparator();
    menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Move to Trash"), this, [this, pageId] { trashPage(pageId); });
    if (!fromSidebar) {
        menu->addSeparator();
        int words = 0;
        for (const Block &b : ws->content(pageId))
            words += int(b.plainText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size());
        QAction *info = menu->addAction(tr("%n word(s) · edited %1", nullptr, words).arg(relativeTime(m->updated)));
        info->setEnabled(false);
    }
    menu->popup(globalPos);
}

// ---- sidebar -------------------------------------------------------------------------

void MainWindow::toggleSidebar()
{
    Settings &s = Settings::instance();
    s.sidebarCollapsed = !s.sidebarCollapsed;
    s.save();
    m_topBar->setSidebarCollapsed(s.sidebarCollapsed);
    m_sidebarAnim.stop();
    if (s.sidebarCollapsed) {
        m_sidebarAnim.setStartValue(m_sidebar->width());
        m_sidebarAnim.setEndValue(0);
    } else {
        m_sidebar->setFixedWidth(0);
        m_sidebar->show();
        m_sidebarAnim.setStartValue(0);
        m_sidebarAnim.setEndValue(s.sidebarWidth);
    }
    m_sidebarAnim.start();
}

void MainWindow::applySidebarWidth(int width)
{
    Settings::instance().sidebarWidth = width;
    m_sidebar->setFixedWidth(width);
    updateBlur();
}

void MainWindow::updateBlur()
{
    if (!windowHandle())
        return;
    const bool on = Settings::instance().translucentSidebar && m_sidebar->isVisible() && WindowEffects::blurAvailable();
    if (!on) {
        WindowEffects::setBlurBehind(this, false);
        return;
    }
    QPainterPath path;
    const int r = isMaximized() || isFullScreen() ? 0 : Theme::kRadius;
    const QRect sb = m_sidebar->geometry();
    path.addRoundedRect(QRectF(sb.left(), sb.top(), sb.width() + r, sb.height()), r, r);
    QPainterPath clip;
    clip.addRect(sb);
    WindowEffects::setBlurBehind(this, true, QRegion(path.intersected(clip).toFillPolygon().toPolygon()));
}

// ---- window ----------------------------------------------------------------------------

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool square = isMaximized() || isFullScreen();
    const double r = square ? 0 : Theme::kRadius;
    QPainterPath frame;
    frame.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
    // The page surface; the sidebar paints its own (translucent) backdrop.
    QPainterPath page = frame;
    if (m_sidebar->isVisible()) {
        QPainterPath sb;
        sb.addRect(QRectF(0, 0, m_sidebar->geometry().right() + 1, height()));
        page = frame.subtracted(sb);
        if (!Settings::instance().translucentSidebar || !WindowEffects::blurAvailable()) {
            p.fillPath(frame.intersected(sb), Theme::colors().abyss);
        }
    }
    p.fillPath(page, Theme::colors().basalt);
    p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.07), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(frame);
}

void MainWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    updateBlur();
    if (m_peek && m_peek->isOpen())
        m_peek->setArea(QRect(m_stack->mapTo(this, QPoint(0, 0)), m_stack->size()));
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::WindowStateChange) {
        m_topBar->setMaximized(isMaximized());
        layout()->setContentsMargins(isMaximized() || isFullScreen() ? QMargins() : QMargins(1, 1, 1, 1));
        WindowEffects::setShadowEnabled(this, !isMaximized() && !isFullScreen());
        updateBlur();
        update();
    }
    QWidget::changeEvent(e);
}

void MainWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (windowHandle())
        windowHandle()->installEventFilter(this);
    QTimer::singleShot(0, this, &MainWindow::updateBlur);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    flushAll();
    Settings &s = Settings::instance();
    s.maximized = isMaximized();
    if (!isMaximized() && !isFullScreen())
        s.geometry = saveGeometry();
    s.save();
    e->accept();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    QWidget::keyPressEvent(e);
}

Qt::Edges MainWindow::edgesAt(const QPoint &p) const
{
    if (isMaximized() || isFullScreen())
        return {};
    const int m = 5, corner = 16;
    const int w = width(), h = height();
    Qt::Edges e;
    if (p.x() <= m)
        e |= Qt::LeftEdge;
    else if (p.x() >= w - m - 1)
        e |= Qt::RightEdge;
    if (p.y() <= m)
        e |= Qt::TopEdge;
    else if (p.y() >= h - m - 1)
        e |= Qt::BottomEdge;
    if (e & (Qt::LeftEdge | Qt::RightEdge)) {
        if (p.y() <= corner)
            e |= Qt::TopEdge;
        else if (p.y() >= h - corner)
            e |= Qt::BottomEdge;
    }
    if (e & (Qt::TopEdge | Qt::BottomEdge)) {
        if (p.x() <= corner)
            e |= Qt::LeftEdge;
        else if (p.x() >= w - corner)
            e |= Qt::RightEdge;
    }
    return e;
}

void MainWindow::setResizeCursor(Qt::Edges edges)
{
    if (edges == m_hoverEdges)
        return;
    m_hoverEdges = edges;
    if (m_overrideCursor) {
        QGuiApplication::restoreOverrideCursor();
        m_overrideCursor = false;
    }
    if (!edges)
        return;
    const bool l = edges & Qt::LeftEdge, r = edges & Qt::RightEdge, t = edges & Qt::TopEdge, b = edges & Qt::BottomEdge;
    Qt::CursorShape shape = Qt::SizeVerCursor;
    if ((l && t) || (r && b))
        shape = Qt::SizeFDiagCursor;
    else if ((r && t) || (l && b))
        shape = Qt::SizeBDiagCursor;
    else if (l || r)
        shape = Qt::SizeHorCursor;
    QGuiApplication::setOverrideCursor(shape);
    m_overrideCursor = true;
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != windowHandle())
        return QWidget::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->buttons() == Qt::NoButton)
            setResizeCursor(edgesAt(me->position().toPoint()));
        break;
    }
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const Qt::Edges edges = edgesAt(me->position().toPoint());
            if (edges && windowHandle()->startSystemResize(edges)) {
                setResizeCursor({});
                return true;
            }
        }
        break;
    }
    case QEvent::Leave:
        setResizeCursor({});
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}
