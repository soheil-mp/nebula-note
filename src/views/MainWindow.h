#pragma once

#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class DatabaseView;
class HomeView;
class PageView;
class Peek;
class Sidebar;
class TabStrip;
class Toast;
class TopBar;
class QStackedWidget;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void navigate(const QString &pageId, bool push = true);
    void goHome(bool push = true);
    void restoreSession();
    void handleArguments(const QStringList &arguments);
    void toast(const QString &message, const QString &action = {}, std::function<void()> onAction = {});
    // For screenshots and automation: quickfind, settings, templates, trash, slash, pagemenu.
    void runAction(const QString &action);
    Peek *peek() const { return m_peek; }
    DatabaseView *databaseView() const { return m_database; }
    PageView *pageView() const { return m_page; }

public slots:
    // Database rows open in a side or center peek, or as a full page.
    void openRowIn(const QString &pageId, const QString &mode);
    void navigateTo(const QString &pageId) { navigate(pageId); }
    void openVersionHistory(const QString &pageId);
    // Tabs and windows.
    void newTab(const QString &pageId = {});
    void closeTab(int index);
    void switchTab(int index);
    int tabCount() const { return int(m_tabs.size()); }
    int currentTab() const { return m_currentTab; }
    TabStrip *tabStrip() const { return m_tabStrip; }
    MainWindow *newWindow(const QString &pageId = {});
    void openInbox(const QPoint &globalPos);
    void checkReminders();

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void changeEvent(QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *) override;

public:
    QString currentPage() const;

private:
    void updateTabs();
    void newPage(const QString &parentId);
    void newDatabase();
    void openQuickFind();
    void openSettings();
    void openTemplates(const QString &targetPage = {});
    void openTrash(const QPoint &globalPos);
    void importFiles(const QString &parentId = {});
    void exportPage(const QString &pageId, const QString &format);
    void exportAll();
    void showPageMenu(const QString &pageId, const QPoint &globalPos, bool fromSidebar);
    void trashPage(const QString &pageId);
    void movePage(const QString &pageId, const QPoint &globalPos);
    void toggleSidebar();
    void applySidebarWidth(int width);
    void updateBlur();
    void updateNavButtons();
    Qt::Edges edgesAt(const QPoint &pos) const;
    void setResizeCursor(Qt::Edges edges);
    void flushAll();

    Sidebar *m_sidebar;
    TopBar *m_topBar;
    QStackedWidget *m_stack;
    HomeView *m_home;
    PageView *m_page;
    DatabaseView *m_database;
    Peek *m_peek;
    Toast *m_toast;
    QStringList m_history; // "" = Home
    int m_historyIndex = -1;
    // Other tabs keep their own history; the current one lives in m_history.
    struct Tab {
        QStringList history;
        int index = -1;
    };
    QList<Tab> m_tabs{Tab()};
    int m_currentTab = 0;
    TabStrip *m_tabStrip;
    QVariantAnimation m_sidebarAnim;
    QTimer m_reminderTimer;
    QSet<QString> m_notified; // reminders already announced this session
    Qt::Edges m_hoverEdges;
    bool m_overrideCursor = false;
};
