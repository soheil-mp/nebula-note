#pragma once

#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>

class QLineEdit;

// An item in a command list (the "/" menu, the "@" picker, Quick Find).
struct MenuItem {
    QString id;
    QString icon;     // glyph name, or a page icon spec when `pageIcon`
    bool pageIcon = false;
    QString title;
    QString subtitle;
    QString section;  // shown as a header when it changes
    QString hint;     // right-aligned (a shortcut or "↵")
    QStringList keywords;
    int score = 0;
};

// A keyboard-driven list in a floating card. It never takes focus: the
// widget that opened it forwards ↑ ↓ Enter Esc.
class CommandPopup : public QWidget
{
    Q_OBJECT
public:
    explicit CommandPopup(QWidget *host);

    void setItems(const QList<MenuItem> &items, bool showSections);
    bool isEmpty() const { return m_items.isEmpty(); }
    void setEmptyText(const QString &text) { m_emptyText = text; }
    void setLargeIcons(bool large) { m_largeIcons = large; }
    void setFixedCardWidth(int w) { m_cardWidth = w; }

    // Positions below `anchor` (host coordinates), or above when there is no room.
    void popup(const QRect &anchor);
    void dismiss();

    void moveSelection(int delta);
    QString currentId() const;
    void activateCurrent();

signals:
    void activated(const QString &id);
    void dismissed();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    struct Row {
        int item = -1; // -1: section header
        QString header;
        int y = 0;
        int h = 0;
    };
    void rebuildRows();
    void ensureVisible(int item);
    int rowAt(const QPoint &pos) const;
    QRect cardRect() const;

    QList<MenuItem> m_items;
    QList<Row> m_rows;
    bool m_sections = true;
    bool m_largeIcons = true;
    int m_current = 0;
    int m_scroll = 0;
    int m_contentHeight = 0;
    int m_cardWidth = 324;
    QString m_emptyText;
    QVariantAnimation m_fade;
};

// The floating formatting toolbar shown over a text selection.
class FormatBar : public QWidget
{
    Q_OBJECT
public:
    explicit FormatBar(QWidget *host);

    struct State {
        bool bold = false, italic = false, underline = false, strike = false, code = false, link = false;
        QString typeName;
    };
    void showFor(const QRect &selectionRect, const State &state);
    void dismiss();
    // Switches to the link field; Enter applies, Esc cancels.
    void editLink(const QString &current);
    bool isEditingLink() const;

signals:
    void action(const QString &id, const QPoint &globalPos);
    void linkEntered(const QString &url);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Button {
        QString id;
        QString icon;
        QString text;
        QRectF rect;
        bool active = false;
        bool separatorAfter = false;
    };
    void layoutButtons();
    QRect cardRect() const;

    QList<Button> m_buttons;
    State m_state;
    int m_hover = -1;
    QLineEdit *m_link = nullptr;
    QVariantAnimation m_fade;
};
