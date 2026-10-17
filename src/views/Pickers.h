#pragma once

#include "ui/Widgets.h"

#include <functional>

// Icon picker: Nebula glyphs and emoji, with search.
class IconPicker : public Popover
{
    Q_OBJECT
public:
    explicit IconPicker(QWidget *parent, bool allowRemove = true, bool allowUpload = false);
    void showEmoji(); // start on the emoji tab

signals:
    void picked(const QString &icon); // empty: remove

private:
    void rebuild();
    class IconGrid *m_grid;
    SearchField *m_search;
    bool m_emoji = false;
    FlatButton *m_glyphTab;
    FlatButton *m_emojiTab;
};

// Cover picker: procedural space scenes or an image file.
class CoverPicker : public Popover
{
    Q_OBJECT
public:
    explicit CoverPicker(QWidget *parent);

signals:
    void picked(const QString &cover); // empty: remove

private:
    class CoverGrid *m_grid;
};

// Page picker with search (Move to…, Link to page).
class PagePicker : public Popover
{
    Q_OBJECT
public:
    // `exclude` and its sub-pages are hidden; rows and databases optional.
    PagePicker(QWidget *parent, const QString &title, const QString &exclude = {}, bool includeTopLevel = true,
               bool includeDatabases = false);
    void setDatabasesOnly(bool only);

signals:
    void picked(const QString &pageId); // "" means the top level

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refresh();
    class PageList *m_list;
    SearchField *m_search;
    QString m_exclude;
    bool m_topLevel;
    bool m_databases;
    bool m_databasesOnly = false;
};
