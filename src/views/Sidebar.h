#pragma once

#include "ui/Accessible.h"

#include <QPointer>
#include <QRegion>
#include <QWidget>

class QLineEdit;
class QScrollArea;

// The page tree with Favorites and Pages sections.
// The sidebar's pages. With the keyboard focus (F6): arrows move, Right and
// Left expand and collapse, Enter opens, F2 renames, the menu key shows the
// page menu. Screen readers see a tree.
class PageTree : public QWidget, public AccessibleItems
{
    Q_OBJECT
    friend class AppTests;
public:
    explicit PageTree(QWidget *parent = nullptr);

    QAccessible::Role accessibleRole() const override { return QAccessible::Tree; }
    QAccessible::Role accessibleItemRole() const override { return QAccessible::TreeItem; }
    int accessibleCount() const override { return int(m_rows.size()); }
    QString accessibleItemName(int index) const override;
    QAccessible::State accessibleItemState(int index) const override;
    QRect accessibleItemRect(int index) const override;
    void accessiblePress(int index) override { activate(index); }
    int accessibleFocus() const override { return m_focusRow; }
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
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;

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
    void activate(int index);
    void focusRow(int index);
    bool focusable(int index) const;

    QList<Row> m_rows;
    QString m_current;
    int m_focusRow = -1;    // keyboard focus
    QString m_focusKey;     // …kept across rebuilds
    int m_hover = -1;
    int m_hoverPart = 0; // 0 row, 1 chevron, 2 plus, 3 more, 4 header plus
    // drag & drop
    int m_pressRow = -1;
    QPoint m_pressPos;
    bool m_dragging = false;
    int m_dropRow = -1;
    int m_dropMode = 0; // -1 above, 0 into, 1 below
    QPoint m_dragPos;   // where the dragged row's ghost follows the pointer
    QPointer<QLineEdit> m_rename;
    QString m_renameId;
};

// The left column of the window.
class Sidebar : public QWidget
{
    Q_OBJECT
public:
    explicit Sidebar(QWidget *parent = nullptr);
    friend class AppTests;
    PageTree *tree() const { return m_tree; }
    void setCurrentPage(const QString &id);
    void setHomeActive(bool active);

signals:
    void openPage(const QString &id);
    void openView(const QString &databaseId, const QString &viewId);
    void newPage(const QString &parentId);
    void searchRequested();
    void homeRequested();
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
    PageTree *m_tree;
    QScrollArea *m_scroll;
};
