#pragma once

#include "ui/Widgets.h"

class QLabel;
class QPlainTextEdit;
class QVBoxLayout;

// Writes the first comment of a new thread (inline or on a block).
class CommentComposer : public Popover
{
    Q_OBJECT
public:
    CommentComposer(QWidget *parent, const QString &quote);

signals:
    void posted(const QString &body);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QPlainTextEdit *m_edit;
};

// Comment threads of a page: one thread, the threads on a block, or all of
// them (the comments panel), with replies, editing, resolving and reopening.
class CommentThreads : public Popover
{
    Q_OBJECT
public:
    struct Filter {
        QString thread;  // just this thread
        QString blockId; // threads anchored to this block
        bool all = false;
    };
    CommentThreads(QWidget *parent, const QString &pageId, const Filter &filter);

signals:
    void resolved(const QString &thread, bool resolved);
    void deleted(const QString &thread);

private:
    void rebuild();
    QString m_pageId;
    Filter m_filter;
    bool m_showResolved = false;
    QWidget *m_list;
    QVBoxLayout *m_listLayout;
};

// Page-level comments under the title (Notion's page discussion).
class Discussion : public QWidget
{
    Q_OBJECT
public:
    explicit Discussion(QWidget *parent = nullptr);
    void setPage(const QString &pageId);
    void refresh();

signals:
    void heightChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString m_pageId;
    QVBoxLayout *m_layout;
    QWidget *m_items;
    QVBoxLayout *m_itemsLayout;
    QPlainTextEdit *m_input;
};
