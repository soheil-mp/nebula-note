#pragma once

#include <QVariantAnimation>
#include <QWidget>

class FlatButton;
class PageView;

// A database row opened without leaving the database: a panel at the right
// (side peek) or a card over a dimmed window (center peek), like Notion's.
class Peek : public QWidget
{
    Q_OBJECT
public:
    explicit Peek(QWidget *host);

    void showPage(const QString &pageId, const QString &mode); // "side" | "center"
    void closePeek();
    bool isOpen() const { return isVisible(); }
    QString pageId() const;
    QString mode() const { return m_mode; }
    PageView *pageView() const { return m_view; }
    // The area the side peek sits in (host coordinates), set by the host.
    void setArea(const QRect &area);

signals:
    void openFull(const QString &pageId);
    void openPage(const QString &pageId);
    void toast(const QString &message);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void relayout();
    QRect panelRect() const;

    QWidget *m_panel;
    PageView *m_view;
    FlatButton *m_close;
    FlatButton *m_full;
    FlatButton *m_switch;
    QString m_mode = QStringLiteral("side");
    QRect m_area;
    QVariantAnimation m_anim;
};
