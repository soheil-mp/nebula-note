#pragma once

#include <QPointer>
#include <QRegion>
#include <QTimer>
#include <QWidget>

class QLineEdit;
class QScrollArea;

// The page tree with Favorites and Pages sections.
class PageTree : public QWidget
{
    Q_OBJECT
public:
    explicit PageTree(QWidget *parent = nullptr);
    void rebuild();
    void setCurrent(const QString &id);
    void revealPage(const QString &id);
    void renamePage(const QString &id);
    // Where the rows put ink (icons and labels), for keeping the starfield off them.
    QRegion inkRegion() const;

signals:
    void openPage(const QString &id);
    void openView(const QString &databaseId, const QString &viewId);
    void newPage(const QString &parentId);
    void pageMenu(const QString &id, const QPoint &globalPos);
    void scrollTo(int y);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;

private:
    struct Row {
        enum Kind { Header, Page, Empty, View } kind = Page;
        QString id;      // page id (View: the database)
        QString viewId;  // View rows
        QString viewIcon;
        QString key; // expansion key ("f:" prefix in Favorites)
        QString title;
        int depth = 0;
        bool hasChildren = false;
        bool expanded = false;
        bool favorites = false;
        int y = 0;
        int h = 30;
    };
    int rowAt(int y) const;
    QRect chevronRect(const Row &r) const;
    QRect plusRect(const Row &r) const;
    QRect moreRect(const Row &r) const;
    void toggle(const Row &r);
    void appendPages(const QString &parentId, int depth, bool favorites);
    void appendViews(const QString &databaseId, int depth);
    void finishRename(bool commit);

    QList<Row> m_rows;
    QString m_current;
    int m_hover = -1;
    int m_hoverPart = 0; // 0 row, 1 chevron, 2 plus, 3 more, 4 header plus
    // drag & drop
    int m_pressRow = -1;
    QPoint m_pressPos;
    bool m_dragging = false;
    int m_dropRow = -1;
    int m_dropMode = 0; // -1 above, 0 into, 1 below
    QPointer<QLineEdit> m_rename;
    QString m_renameId;
};

// The left column of the window.
class Sidebar : public QWidget
{
    Q_OBJECT
public:
    explicit Sidebar(QWidget *parent = nullptr);
    PageTree *tree() const { return m_tree; }
    void setCurrentPage(const QString &id);
    void setHomeActive(bool active);
    int inboxCount() const { return m_inboxCount; }
    void updateInboxCount();

signals:
    void openPage(const QString &id);
    void openView(const QString &databaseId, const QString &viewId);
    void newPage(const QString &parentId);
    void searchRequested();
    void homeRequested();
    void templatesRequested();
    void inboxRequested(const QPoint &globalPos);
    void importRequested(const QPoint &globalPos);
    void settingsRequested();
    void trashRequested(const QPoint &globalPos);
    void collapseRequested();
    void pageMenuRequested(const QString &id, const QPoint &globalPos);
    void widthDragged(int width);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct Item {
        QString id;
        QString icon;
        QString text;
        QString hint;
        QRect rect;
    };
    void layoutItems();
    int itemAt(const QPoint &pos) const;

    QList<Item> m_top;
    QList<Item> m_bottom;
    QRect m_headerRect;
    QRect m_collapseRect;
    QRect m_newRect;
    int m_hover = -1; // index into top (0…) or bottom (100…), 200 header, 201 collapse, 202 new
    bool m_homeActive = false;
    bool m_resizing = false;
    int m_inboxCount = 0;
    PageTree *m_tree;
    QScrollArea *m_scroll;
};
