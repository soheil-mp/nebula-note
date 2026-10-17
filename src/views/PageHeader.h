#pragma once

#include <QTextEdit>
#include <QWheelEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class FlatButton;

// The page title: large, wrapping, plain text. Enter and ↓ move into the body.
class TitleEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit TitleEdit(QWidget *parent = nullptr);
    void setTitle(const QString &title);
    QString title() const;
    void setPixelSize(double px);
    void setPlaceholder(const QString &text) { m_placeholder = text; }
    void focusEnd();
    int preferredHeight() const;

signals:
    void edited(const QString &title);
    void submitted(); // Enter
    void exitDown();  // ↓ on the last line, Tab
    void heightChanged();

protected:
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void insertFromMimeData(const QMimeData *source) override;
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override { e->ignore(); }
    void focusOutEvent(QFocusEvent *e) override;

private:
    void updateHeight();
    QString m_placeholder;
    double m_px = 40;
    bool m_setting = false;
    int m_lastHeight = -1;
};

// Cover image with hover controls and drag-to-reposition.
class CoverWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CoverWidget(QWidget *parent = nullptr);
    void setCover(const QString &spec, double position);
    void setEditable(bool editable) { m_editable = editable; }

signals:
    void changeRequested(const QPoint &globalPos);
    void removeRequested();
    void positionChanged(double position);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QRectF buttonRect(int i) const;
    QString m_spec;
    double m_pos = 0.5;
    bool m_editable = true;
    bool m_hover = false;
    int m_hoverButton = -1;
    bool m_repositioning = false;
    bool m_dragging = false;
    double m_dragStartY = 0;
    double m_dragStartPos = 0.5;
};

// Full-width page header: cover, icon, hover actions and the title, laid out
// over a text column.
class PageHeader : public QWidget
{
    Q_OBJECT
public:
    explicit PageHeader(QWidget *parent = nullptr);

    void setPage(const QString &pageId);
    QString pageId() const { return m_pageId; }
    void refresh(); // icon, cover, title from the workspace
    void flushTitle(); // saves a pending title edit now
    void setColumn(int x, int width);
    void setSmall(bool small);
    void setEditable(bool editable);
    TitleEdit *title() const { return m_title; }
    void relayout();

signals:
    void heightChanged();
    void titleSubmitted();
    void titleExitDown();
    void commentRequested(); // "Add comment": a page discussion

protected:
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    void pickIcon(const QPoint &globalPos);
    void pickCover(const QPoint &globalPos);
    QRectF iconRect() const;
    class IconBadge *m_iconBadge;

    QString m_pageId;
    QString m_icon;
    QString m_cover;
    CoverWidget *m_coverWidget;
    TitleEdit *m_title;
    FlatButton *m_addIcon;
    FlatButton *m_addCover;
    FlatButton *m_addComment;
    QWidget *m_actions;
    QTimer m_titleTimer;
    int m_colX = 96;
    int m_colW = 720;
    bool m_small = false;
    bool m_editable = true;
    QVariantAnimation m_actionsFade;
};
