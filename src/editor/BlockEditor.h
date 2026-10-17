#pragma once

#include "BlockFormat.h"
#include "TextObjects.h"
#include "core/Types.h"

#include <QElapsedTimer>
#include <QSet>
#include <QPointer>
#include <QTextEdit>
#include <QTimer>
#include <QJsonObject>
#include <QTextTable>
#include <QVariantAnimation>

#include <QScrollArea>

#include <functional>

class CodeHighlighter;
class CommandPopup;
class DatabaseBody;
class FormatBar;
class QTextTable;
class Workspace;

// The page body: one QTextEdit with Notion's block behaviour on top —
// markers painted in the margin, Markdown shortcuts, the "/" menu, "@"
// mentions, drag handles, nesting, toggles and inline objects. It grows to
// its content's height and lives inside the page's scroll area.
class BlockEditor : public QTextEdit
{
    Q_OBJECT
    friend class BlocksTests;

public:
    explicit BlockEditor(QWidget *parent = nullptr);
    bool hasBlockSelection() const { return m_blockSel.first >= 0; }
    QPair<int, int> blockSelection() const { return m_blockSel; }
    ~BlockEditor() override;

    // ---- page ----------------------------------------------------------------
    void setPage(const QString &pageId);
    QString pageId() const { return m_pageId; }
    void reload();                      // re-reads the content from the workspace
    void flush();                       // writes pending edits now
    void setBlocks(const QList<Block> &blocks); // replaces the content (undoable)
    void appendBlocks(const QList<Block> &blocks);
    QList<Block> blocks() const;
    bool isEmptyPage() const;
    void setPageOptions(const PageOptions &options);
    const BlockFormat::Style &style() const { return m_ctx.style; }

    // Width of the gutter left of the text column (drag handles live there).
    static int gutter() { return 58; }
    // Top of a block in the editor's coordinates (-1 if hidden or gone).
    double blockTop(int blockNumber) const;
    // Room right of the text column, for comment badges.
    static int rightMargin() { return 48; }
    int preferredHeight() const;
    void setScrollArea(QScrollArea *area) { m_scrollArea = area; }
    void setOverlayHost(QWidget *host);

    // ---- caret ---------------------------------------------------------------
    void focusStart();
    void focusEnd(bool ensureEmptyBlock = false);
    bool cursorOnFirstLine() const;
    QRect caretRect() const; // in editor coordinates

    // ---- commands ------------------------------------------------------------
    void turnInto(BlockType type);
    void turnIntoToggleHeading(BlockType heading);
    void setShade(int textShade, int highlightShade, bool wholeBlock);
    void toggleInline(const QString &what); // bold, italic, underline, strike, code
    void setLink(const QString &url);
    void indentBlocks(int delta);
    void moveBlocks(int delta);
    void duplicateBlocks();
    void deleteBlocks();
    void toggleAllToggles();
    void insertMention(const QString &pageId);
    void insertPageLink(const QString &pageId);
    void insertImageFile(const QString &path);
    void setBlockIcon(int blockNumber, const QString &icon);
    void setCodeLanguage(int blockNumber, const QString &language);
    // Removes the selected blocks (or the current one) and returns them.
    QList<Block> takeSelectedBlocks();
    // Objects
    void insertEquation(bool inlineEquation);
    void insertObjectBlock(const Block &b, bool configure = true);
    void insertDateMention(const QDateTime &when, bool hasTime, bool remind);
    void insertText(const QString &text); // at the caret (emoji picker)
    void insertLinkedDatabase(const QString &databaseId);
    // The live database shown for a database block (tests, automation).
    DatabaseBody *embedFor(const QString &blockId) const;
    void scheduleEmbeds();
    void createInlineDatabase(int viewType);
    void copyLinkToBlock(int blockNumber);
    void addComment();        // on the selection, or the current block
    void turnIntoSynced();    // the selected blocks become a synced block
    void applyCommentThread(int from, int to, const QString &thread);
    void removeCommentThread(const QString &thread);
    void unsyncBlocks(const QString &syncId);
    // The selection a pending comment applies to.
    QPair<int, int> commentRange() const { return {m_commentFrom, m_commentTo}; }
    // Scrolls to a block (by number or id) and briefly highlights it.
    void scrollToBlock(int blockNumber, bool flash = true);
    bool scrollToBlockId(const QString &blockId, bool flash = true);
    QTextBlock blockById(const QString &id) const;

    // Clipboard (public so the page can paste into an empty editor).
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;

signals:
    void edited();                         // content changed (saved automatically)
    void openPage(const QString &pageId);  // a page link or mention was activated
    void openBlock(const QString &pageId, const QString &blockId); // a link to a block
    void emojiRequested(const QPoint &globalPos);           // the page shows the emoji picker
    void linkedDatabaseRequested(const QPoint &globalPos);  // pick a database to show here
    void commentRequested(const QString &blockId, const QString &thread, const QString &quote, const QPoint &globalPos);
    void commentClicked(const QString &blockId, const QString &thread, const QPoint &globalPos);
    void heightChanged();
    void exitTop();                        // ↑ / ← / Backspace at the very start
    void iconRequested(int blockNumber, const QPoint &globalPos);
    void moveBlocksRequested(const QPoint &globalPos);
    void toast(const QString &message);

protected:
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void inputMethodEvent(QInputMethodEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    // geometry
    QRectF blockRect(const QTextBlock &block) const; // viewport coordinates
    double firstLineMid(const QTextBlock &block) const;
    double firstBaseline(const QTextBlock &block) const;
    double textLeft(const QTextBlock &block) const;  // x where the block's text starts
    double blockLeft(const QTextBlock &block) const; // x of the block's nesting level
    double blockRight(const QTextBlock &block) const; // right edge of the block's container
    QTextBlock blockAt(const QPointF &pos) const;    // the block under a point (gutter included)
    QTextBlock firstVisibleBlock(const QRect &rect) const;
    QRectF tableRect(const QTextTable *table) const;
    // Blocks whose markers and backgrounds can be drawn in a rect, in order.
    QList<QTextBlock> blocksIn(const QRect &rect) const;

    // structure (all within one container: the root or one table cell)
    int subtreeEnd(const QTextBlock &block) const; // last block number of block's children
    // An open toggle with nothing inside shows "Empty toggle…" below it.
    bool showsEmptyToggleHint(const QTextBlock &block) const;
    QRectF emptyToggleHintRect(const QTextBlock &block) const; // viewport coordinates
    double emptyToggleHintHeight() const { return qRound(m_ctx.style.size * 1.5 + 4 * m_ctx.style.size / 16.0); }
    QPair<int, int> selectedBlockRange() const;
    QPair<int, int> withChildren(QPair<int, int> range) const;
    int numberFor(const QTextBlock &block) const;  // numbered list position
    QHash<int, int> listNumbers() const;           // block number → list number
    QTextBlock previousInContainer(const QTextBlock &block) const; // previous block item, skipping tables
    QTextBlock nextInContainer(const QTextBlock &block) const;
    int itemDepth(const QTextBlock &first) const; // depth of the item starting at a block (tables too)
    void setBlockProps(QTextBlock block, const Block &props);
    struct DropTarget {
        enum Where { None, Before, After, Left, Right } where = None;
        int block = -1; // the block the drop is relative to
        int depth = 0;  // for Before/After
    };
    void moveRange(int first, int last, const DropTarget &target);
    DropTarget dropTargetAt(const QPointF &pos) const;

    // columns and tables
    void insertColumnList(int count);
    void wrapIntoColumns(int first, int last, int targetBlock, bool left);
    void addColumnBeside(QTextTable *table, int column, bool left, int first, int last);
    void cleanupColumns();
    void unwrapTable(QTextTable *table);
    // Inserts blocks before `before`, filling it instead when it is an empty
    // block left at the end of its container.
    QTextBlock placeBefore(QTextCursor &c, const QTextBlock &before, const QList<Block> &blocks);
    void setColumnWidths(QTextTable *table, const QList<double> &widths);
    QList<double> columnWidths(const QTextTable *table) const;
    struct ColumnGap {
        QTextTable *table = nullptr;
        int column = -1; // the gap after this column
        QRectF rect;
        bool simple = false; // a simple table's column border
    };
    ColumnGap columnGapAt(const QPointF &pos) const;
    QList<ColumnGap> columnGaps(const QRect &rect) const;

    // simple tables
    QTextTable *currentSimpleTable() const;
    QRectF simpleTableBox(const QTextTable *t) const;
    QJsonObject tableData(const QTextTable *t) const;
    void setTableData(QTextTable *t, const QJsonObject &data);
    QList<double> simpleTableWidths(const QTextTable *t) const;
    void setSimpleTableWidths(QTextTable *t, const QList<double> &px);
    void insertSimpleTable(int rows, int cols);
    void moveToCell(QTextTable *t, int row, int col, bool select = false);
    void tableInsertRow(QTextTable *t, int at);
    void tableInsertColumn(QTextTable *t, int at);
    void tableRemoveRow(QTextTable *t, int row);
    void tableRemoveColumn(QTextTable *t, int col);
    void deleteTable(QTextTable *t);
    bool handleTableKey(int key, Qt::KeyboardModifiers mods);
    void tableToDatabase(QTextTable *t);
    void showTableMenu(QTextTable *t, int row, int col, const QPoint &globalPos);
    void splitBlock();
    bool handleBackspaceAtStart();
    bool handleDeleteAtEnd();
    bool applyBlockShortcut();
    bool applyInlineShortcut(QChar typed);
    void insertBlocksAtCursor(const QList<Block> &blocks);
    void insertNewBlockAfter(const QTextBlock &block, const Block &props, bool focus = true);
    void createSubpage(bool database, int viewType = 0);

    // housekeeping
    void scheduleNormalize();
    void normalize();
    void updateVisibility();
    void fixCursorVisibility();
    void scheduleSave();
    void updateHeight();
    void ensureCaretVisible();
    void startAnimTick();

    // menus
    void openSlashMenu();
    void updateSlashMenu();
    void closeSlashMenu();
    void runSlashCommand(const QString &id);
    void openMentionMenu(const QString &trigger);
    void updateMentionMenu();
    void closeMentionMenu();
    void runMention(const QString &id);
    void updateFormatBar();
    void showBlockMenu(int blockNumber, const QPoint &globalPos);
    void showTurnIntoMenu(const QPoint &globalPos);
    void showShadeMenu(const QPoint &globalPos);
    void showLanguageMenu(int blockNumber, const QPoint &globalPos);
    QString queryAfter(int position) const;

    // hit testing
    enum class Hit {
        None, Checkbox, ToggleArrow, Handle, Plus, CalloutIcon, CodeLanguage, CodeCopy, PageLink, Mention, Link, ImageEdge,
        ColumnGap, TableAddRow, TableAddColumn, Object, InlineEquation, Date, CommentBubble, ToggleHint, ImageTool
    };
    struct HitInfo {
        Hit hit = Hit::None;
        int block = -1;
        int position = -1;
        QString pageId;
        QString href;
        int edge = 0; // image: -1 left, 1 right
        int part = -1; // objects: the entry or button under the pointer
    };
    // comments
    void refreshComments();
    QRectF commentBubbleRect(const QTextBlock &block) const;
    // block selection
    void startBlockSelection(int first, int last);
    void clearBlockSelection();
    bool blockSelectionKey(QKeyEvent *e);
    void selectRangeInCursor(int first, int last);
    QList<QRectF> blockSelectionRects() const;
    void ensureBlockVisible(int blockNumber);
    // objects
    QRectF objectRect(const QTextBlock &block) const;
    int objectPartAt(const QTextBlock &block, const QPointF &pos) const;
    void activateObject(const QTextBlock &block, int part, const QPoint &globalPos);
    void editBlockEquation(int blockNumber);
    void editInlineEquation(int position);
    void editDate(int position, const QPoint &globalPos);
    void configureButton(int blockNumber, const QPoint &globalPos);
    void runButton(int blockNumber);
    void askForLink(int blockNumber, const QPoint &globalPos);
    void fetchPreview(const QString &blockId, const QString &url);
    void chooseFile(int blockNumber);
    void openObject(int blockNumber);
    void editCaption(int blockNumber, const QPoint &globalPos);
    // The small toolbar on a hovered image: (action, rect) in viewport coordinates.
    QList<QPair<QString, QRectF>> imageTools(const QTextBlock &block) const;
    void runImageTool(int blockNumber, const QString &tool, const QPoint &globalPos);
    void viewImageFullScreen(int blockNumber);
    void setBlockData(int blockNumber, const std::function<void(Block &)> &change);
    void updateEmbeds();
    void showPasteMenu(int blockNumber, const QString &url);
    void runPasteChoice(const QString &id);
    QPoint objectPopupPos(const QTextBlock &block) const;
    HitInfo hitTest(const QPoint &pos) const;

    Workspace *m_ws = nullptr;
    QString m_pageId;
    PageOptions m_options;
    ObjectContext m_ctx;
    TextObjects *m_objects = nullptr;
    CodeHighlighter *m_highlighter = nullptr;
    QPointer<QScrollArea> m_scrollArea;
    QPointer<QWidget> m_host;
    bool m_loading = false;
    bool m_dirty = false;
    QSet<QString> m_linkedPages; // pages this body mentions or links to
    int m_lastHeight = -1;
    int m_lastCursorPos = 0;

    QTimer m_saveTimer;
    QTimer m_normalizeTimer;
    QTimer m_animTimer;
    QTimer m_autoScroll;

    // hover / handles
    int m_hoverBlock = -1;
    Hit m_hoverHit = Hit::None;
    int m_hoverImageTool = -1;
    QVariantAnimation m_handleFade;

    // block drag
    bool m_pressOnHandle = false;
    bool m_draggingBlocks = false;
    QPoint m_pressPos;
    QPair<int, int> m_dragRange{-1, -1};
    DropTarget m_drop;
    QPoint m_dragPoint; // the pointer during a block drag (viewport coordinates)

    // column resize
    ColumnGap m_hoverGap;
    QPointer<QTextTable> m_hoverTable; // simple table under the pointer (its + bars)
    QTextTable *m_resizeTable = nullptr;
    int m_resizeColumn = -1;
    double m_resizeStartX = 0;
    double m_resizeTotalWidth = 0;
    QList<double> m_resizeStartWidths;
    bool m_resizeJoin = false;
    bool m_resizeSimple = false;

    // image resize
    int m_resizeImagePos = -1;
    int m_resizeEdge = 0;
    double m_resizeImageStartX = 0;
    double m_resizeStartWidth = 0;

    // animations
    struct Spark {
        int block;
        QElapsedTimer clock;
    };
    QList<Spark> m_sparks;
    struct ToggleAnim {
        int block;
        bool opening;
        QElapsedTimer clock;
    };
    QList<ToggleAnim> m_toggleAnims;
    struct Flash {
        int block;
        QElapsedTimer clock;
    };
    QList<Flash> m_flashes;

    // menus
    QPointer<CommandPopup> m_slash;
    int m_slashPos = -1;
    QPointer<CommandPopup> m_mention;
    int m_mentionPos = -1;
    QString m_mentionTrigger;
    QPointer<FormatBar> m_formatBar;
    QPair<int, int> m_blockSel{-1, -1}; // selected blocks (block selection mode)
    QPair<int, int> m_rangeOverride{-1, -1}; // the block range commands act on (set by block selection)
    int m_blockSelAnchor = -1;
    bool m_bandSelecting = false;       // dragging a selection box in the margin
    QPoint m_bandStart, m_bandEnd;
    int m_lastTextShade = -1; // Ctrl+Shift+H repeats these
    int m_lastHighlight = 2;
    QHash<QString, int> m_openComments; // block id → open threads anchored there
    // Inline databases and linked views: live widgets over their blocks.
    QHash<QString, QPointer<DatabaseBody>> m_embeds;
    QTimer m_embedTimer;
    int m_commentFrom = -1;
    int m_commentTo = -1;
    QPointer<CommandPopup> m_pasteMenu;
    int m_pasteBlock = -1;
    QString m_pasteUrl;
};
