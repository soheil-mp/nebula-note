#pragma once

#include "ui/Widgets.h"

#include <QJsonObject>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QCheckBox;
class Switch;

// Edits TeX with a live preview. Enter applies, Shift+Enter adds a line.
class EquationPopover : public Popover
{
    Q_OBJECT
public:
    EquationPopover(QWidget *parent, const QString &tex, bool display);

signals:
    void submitted(const QString &tex);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updatePreview();
    QPlainTextEdit *m_edit;
    QWidget *m_preview;
    QLabel *m_error;
    bool m_display;
};

// One line of text (links, captions). Enter applies.
class LinePopover : public Popover
{
    Q_OBJECT
public:
    LinePopover(QWidget *parent, const QString &title, const QString &placeholder, const QString &text,
                const QString &button = {});

signals:
    void submitted(const QString &text);

private:
    QLineEdit *m_edit;
};

// A button block's label and actions.
class ButtonPopover : public Popover
{
    Q_OBJECT
public:
    ButtonPopover(QWidget *parent, const QJsonObject &data);

signals:
    void submitted(const QJsonObject &data);

private:
    void updateFields();
    QLineEdit *m_label;
    QComboBox *m_action;
    QPlainTextEdit *m_blocks;
    QComboBox *m_database;
    QLineEdit *m_rowTitle;
    QComboBox *m_page;
    QLineEdit *m_url;
    Switch *m_confirm;
    QLineEdit *m_confirmText;
    QWidget *m_blocksRow;
    QWidget *m_databaseRow;
    QWidget *m_pageRow;
    QWidget *m_urlRow;
};

// An image over a dark backdrop, fit to the window. Esc or a click closes.
class ImageViewer : public QWidget
{
    Q_OBJECT
public:
    ImageViewer(QWidget *window, const QString &path, const QString &caption);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QImage m_image;
    QString m_caption;
};
