#pragma once

#include <QScrollArea>
#include <QTextEdit>

class BlockEditor;
class Discussion;
class OutlineRail;
class PageHeader;
class PropertyPanel;
class SearchField;
class QLabel;

// A document page (or a database row): cover, icon, title, properties, the
// block editor and backlinks, in one scrolling column.
class PageView : public QScrollArea
{
    Q_OBJECT
public:
    explicit PageView(QWidget *parent = nullptr);

    void setPage(const QString &pageId);
    QString pageId() const { return m_pageId; }
    BlockEditor *editor() const { return m_editor; }
    void flush();
    void focusDefault();     // title for empty pages, body otherwise
    void showFind();
    void applyOptions();     // after the page's ••• options change

    // Scrolls to a block once the page is shown (links to blocks).
    void revealBlock(const QString &blockId);

signals:
    void openPage(const QString &pageId);
    void openBlock(const QString &pageId, const QString &blockId);
    void toast(const QString &message);
    void convertedToDatabase(const QString &pageId);

protected:
    void resizeEvent(QResizeEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void relayout();
    void updateStarter();
    void startWriting();
    void updateBacklinks();
    void runFind(int step);
    void closeFind();

    QString m_pageId;
    QWidget *m_canvas;
    PageHeader *m_header;
    PropertyPanel *m_props;
    Discussion *m_discussion;
    OutlineRail *m_outline;
    bool m_discussionOpen = false;
    QWidget *m_starter;
    bool m_starterDismissed = false; // Enter in the title, or a click into the body
    BlockEditor *m_editor;
    class Backlinks *m_backlinks;
    QWidget *m_findBar;
    SearchField *m_findField;
    QLabel *m_findCount;
    QList<QTextCursor> m_matches;
    int m_matchIndex = -1;
    bool m_relayouting = false;
};
