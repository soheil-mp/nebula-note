#pragma once

#include <QTimer>
#include <QWidget>

// The title bar: navigation, breadcrumbs, page actions and window buttons.
// Empty space moves the window; double-click maximises.
class TopBar : public QWidget
{
    Q_OBJECT
public:
    explicit TopBar(QWidget *parent = nullptr);

    void setPage(const QString &pageId); // empty: Home
    void setNavigation(bool canBack, bool canForward);
    void setSidebarCollapsed(bool collapsed);
    void setMaximized(bool maximized);

signals:
    void back();
    void forward();
    void toggleSidebar();
    void navigate(const QString &pageId);
    void favoriteToggled();
    void pageMenuRequested(const QPoint &globalPos);
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();
    void currentClicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct Hit {
        QString id; // action id or "crumb:<pageId>"
        QRectF rect;
        QString tooltip;
    };
    void relayout();
    int hitAt(const QPointF &pos) const;

    QString m_pageId;
    bool m_canBack = false;
    bool m_canForward = false;
    bool m_sidebarCollapsed = false;
    bool m_maximized = false;
    QList<Hit> m_hits;
    struct Crumb {
        QString id;
        QString text;
        QString icon;
        QRectF rect;
        bool ellipsis = false;
    };
    QList<Crumb> m_crumbs;
    QString m_edited;
    QRectF m_editedRect;
    int m_hover = -1;
    QTimer m_tick;
};
