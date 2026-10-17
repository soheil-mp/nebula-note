#pragma once

#include <QWidget>

// Open pages as tabs (Ctrl+T, Ctrl+W, Ctrl+Tab). Shown when more than one
// tab is open.
class TabStrip : public QWidget
{
    Q_OBJECT
public:
    struct Tab {
        QString title;
        QString icon; // page icon ("glyph:…", emoji) or empty
    };
    explicit TabStrip(QWidget *parent = nullptr);
    void setTabs(const QList<Tab> &tabs, int current);
    int count() const { return int(m_tabs.size()); }
    int current() const { return m_current; }
    QRect tabRect(int index) const;

signals:
    void activated(int index);
    void closeRequested(int index);
    void newTabRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void layoutTabs();
    QRect closeRect(int index) const;

    QList<Tab> m_tabs;
    QList<QRect> m_rects;
    QRect m_addRect;
    int m_current = 0;
    int m_hover = -1;
    bool m_hoverClose = false;
    bool m_hoverAdd = false;
};
