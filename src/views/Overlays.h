#pragma once

#include "core/Workspace.h"
#include "ui/Widgets.h"

#include <QVariantAnimation>
#include <QWidget>

class QScrollArea;
class QStackedWidget;

// A dimmed full-window layer with a centred card; Esc or a click outside closes it.
class ModalOverlay : public QWidget
{
    Q_OBJECT
public:
    ModalOverlay(QWidget *host, const QSize &cardSize, int topOffset = -1);
    QWidget *card() const { return m_card; }
    void open();
    void dismiss();

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void relayout();

    QSize m_cardSize;
    int m_top;
    QWidget *m_card;
    QVariantAnimation m_fade;
};

// Ctrl+P: jump to any page or run an action.
class QuickFind : public ModalOverlay
{
    Q_OBJECT
public:
    explicit QuickFind(QWidget *host);
    // Notion's search filters and sort (title only, in page, edited/created, sort).
    void setSearchOptions(const Workspace::SearchOptions &options);
    Workspace::SearchOptions searchOptions() const { return m_options; }
    QStringList resultIds() const; // page ids shown, in order (tests)

signals:
    void openPage(const QString &id);
    void action(const QString &id);    // new, settings, import, templates, home, trash
    void createPage(const QString &title);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    void updateFilterLabels();
    void filterMenu(FlatButton *anchor, const QString &which);
    SearchField *m_field;
    class ResultList *m_list;
    QWidget *m_filters;
    FlatButton *m_titleOnly;
    FlatButton *m_inPage;
    FlatButton *m_edited;
    FlatButton *m_created;
    FlatButton *m_sort;
    Workspace::SearchOptions m_options;
    QString m_editedKey, m_createdKey; // "", "today", "week", "month"
};

// Preferences.
class SettingsPanel : public ModalOverlay
{
    Q_OBJECT
public:
    explicit SettingsPanel(QWidget *host);

signals:
    void importRequested();
    void exportAllRequested();

private:
    QWidget *appearancePage();
    QWidget *workspacePage();
    QWidget *shortcutsPage();
    QWidget *aboutPage();
    QStackedWidget *m_stack;
};

// Template gallery.
class TemplateGallery : public ModalOverlay
{
    Q_OBJECT
public:
    explicit TemplateGallery(QWidget *host);

signals:
    void picked(const QString &templateId);
};

// The trash: restore or delete pages for good.
class TrashPopover : public Popover
{
    Q_OBJECT
public:
    explicit TrashPopover(QWidget *parent);

signals:
    void openPage(const QString &id);

private:
    void refresh();
    SearchField *m_search;
    class TrashList *m_list;
    QScrollArea *m_scroll;
    FlatButton *m_empty;
};
