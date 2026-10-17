#pragma once

#include <QAbstractButton>
#include <QLineEdit>
#include <QPointer>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

// A floating card in its own popup window: closes on outside clicks or Esc.
class Popover : public QWidget
{
    Q_OBJECT
public:
    explicit Popover(QWidget *parent);

    // Shows the card with its top-left near `globalPos` (kept on screen).
    void popupAt(const QPoint &globalPos);
    // Shows the card below (or above) a widget.
    void popupBelow(QWidget *anchor, int dx = 0);
    QWidget *content() const { return m_content; }
    static constexpr int kShadow = 18;

signals:
    void closed();

protected:
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void hideEvent(QHideEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    // Where the card may go: the screen, and the window when it fits.
    QRect allowedArea(const QPoint &globalPos) const;
    QWidget *m_content;
};

// A small flat button: an icon and/or text over a rounded hover background.
class FlatButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit FlatButton(const QString &icon, const QString &text = {}, QWidget *parent = nullptr);
    void setIconName(const QString &icon);
    void setIconSize(int px) { m_iconSize = px; updateGeometry(); }
    void setTextSize(double px) { m_textSize = px; updateGeometry(); }
    void setMuted(bool muted) { m_muted = muted; update(); }
    void setFilled(bool filled) { m_filled = filled; update(); }
    void setRadius(double r) { m_radius = r; }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString m_icon;
    int m_iconSize = 16;
    double m_textSize = 13;
    double m_radius = 6;
    bool m_muted = true;
    bool m_filled = false;
    QVariantAnimation m_hover;
};

// An on/off switch.
class Switch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit Switch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {38, 22}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void nextCheckState() override;

private:
    QVariantAnimation m_anim;
};

// A search field with a leading icon.
class SearchField : public QLineEdit
{
    Q_OBJECT
public:
    explicit SearchField(const QString &placeholder, QWidget *parent = nullptr);
    // Where the magnifier sits; the text should start after it.
    void setIconGeometry(double x, double size)
    {
        m_iconX = x;
        m_iconSize = size;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double m_iconX = 10;
    double m_iconSize = 15;
};

// A transient message at the bottom of the window, with an optional action.
class Toast : public QWidget
{
    Q_OBJECT
public:
    explicit Toast(QWidget *host);
    void show(const QString &message, const QString &action = {}, std::function<void()> onAction = {});
    QString message() const { return m_message; }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QString m_message;
    QString m_action;
    std::function<void()> m_onAction;
    QRectF m_actionRect;
    QTimer m_timer;
    QVariantAnimation m_fade;
};

// Asks a short question in a styled dialog. Returns true for the primary button.
bool confirmDialog(QWidget *parent, const QString &title, const QString &text, const QString &primary,
                   bool destructive = false);
// Asks for a line of text. Returns false when cancelled.
bool textDialog(QWidget *parent, const QString &title, const QString &label, QString *value);

// Relative time like "just now", "5 minutes ago", "Yesterday".
QString relativeTime(qint64 msecsSinceEpoch);
