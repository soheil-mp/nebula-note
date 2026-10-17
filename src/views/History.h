#pragma once

#include "Overlays.h"

class BlockEditor;
class QLabel;
class QListWidget;
class QVBoxLayout;

// Page history: versions saved while editing (every ten minutes of work),
// previewed on the left and listed on the right; restore or name one.
class VersionHistory : public ModalOverlay
{
    Q_OBJECT
public:
    VersionHistory(QWidget *host, const QString &pageId);
    QListWidget *list() const { return m_list; }
    BlockEditor *preview() const { return m_preview; }
    void restoreSelected();

signals:
    void restored(const QString &pageId);

private:
    void reload();
    void showSelected();

    QString m_pageId;
    BlockEditor *m_preview;
    QLabel *m_title;
    QListWidget *m_list;
    FlatButton *m_restore;
};

// The inbox: reminders that are due or coming up, and recent comments.
class InboxPopover : public Popover
{
    Q_OBJECT
public:
    explicit InboxPopover(QWidget *parent);
    int itemCount() const { return m_items; }

signals:
    void openPage(const QString &pageId, const QString &blockId);

private:
    void rebuild();
    QVBoxLayout *m_layout;
    int m_items = 0;
};
