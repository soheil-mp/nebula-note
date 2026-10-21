#pragma once

#include "core/Types.h"
#include "ui/Widgets.h"

#include <functional>

class QVBoxLayout;

// A page's ••• menu, like Notion's: the font as three "Ag" tiles, text size
// and width as segmented choices and the lock as a switch (all of which
// apply live and leave the menu open), then the page's actions.
class PageMenu : public Popover
{
    Q_OBJECT
public:
    PageMenu(QWidget *parent, const QString &pageId);

signals:
    // "favorite", "duplicate", "move", "copytitle", "history", "import",
    // "trash", "export:md", "export:html", "export:pdf"
    void action(const QString &id);

private:
    void rebuild();
    void mainPage();
    void customizePage();
    void addSeparator();
    MenuRow *addRow(const QString &icon, const QString &label, std::function<void()> fn, bool chevron = false);
    void run(const QString &id);
    void setOptions(const std::function<void(PageOptions &)> &change);

    QString m_pageId;
    QString m_page; // "" or "customize"
    QVBoxLayout *m_layout;
};
