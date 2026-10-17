#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

// The block model of a page. A page's content is a flat list of blocks; a
// block's children are the blocks after it with a greater depth (Notion's
// "indentation is structure"). Column lists and simple tables are the two
// containers that hold blocks (or cells) of their own.
enum class BlockType {
    Paragraph,
    Heading1,
    Heading2,
    Heading3,
    Bullet,
    Numbered,
    Todo,
    Toggle,
    Quote,
    Callout,
    Code,
    Divider,
    Image,
    PageLink, // a sub-page or a link to another page
    Title,    // editor only: block 0 holds the page title
    Heading4,
    Table,      // simple table: rows of rich-text cells
    ColumnList, // side-by-side columns of blocks
    Equation,   // block equation (TeX)
    TableOfContents,
    Breadcrumb,
    Button,
    Bookmark,
    Embed,
    Video,
    Audio,
    File,
    Pdf,
    Database, // an inline database or a linked view of one
    Synced,   // stored form of a synced block: the content lives in the workspace
};

QString blockTypeKey(BlockType t);
BlockType blockTypeFromKey(const QString &key);
QString blockTypeName(BlockType t); // human readable, for menus

// Blocks whose following deeper blocks render as nested children.
bool blockTypeIsList(BlockType t);
// Blocks that hold a single object character instead of editable text.
bool blockTypeIsAtomic(BlockType t);
bool blockTypeIsHeading(BlockType t);
// Blocks drawn around their children (callout, quote, synced).
bool blockTypeIsContainer(BlockType t);
// File-like blocks (video, audio, file, PDF).
bool blockTypeIsFile(BlockType t);

// Gray shades replace Notion's colours. Text shades tint the glyphs,
// highlight shades put a pill behind them.
constexpr int kTextShadeCount = 6;      // 0 = default
constexpr int kHighlightShadeCount = 7; // 0 = none
QString textShadeName(int shade);
QString highlightShadeName(int shade);
// Notion's colour names (gray, brown, orange…) mapped onto the shades, so
// "/red" and imported colours still mean something.
int shadeForNotionColor(const QString &name, bool background);

// A run of uniformly formatted text inside a block.
struct Span {
    QString text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strike = false;
    bool code = false;
    int color = 0;     // text shade
    int highlight = 0; // highlight shade
    QString link = {};      // URL
    QString mention = {};   // page id; text is U+FFFC
    QString mentionBlock = {}; // block id inside the mentioned page (link to block)
    QString equation = {};  // inline equation (TeX); text is U+FFFC
    QString date = {};      // date mention: ISO date or date-time; text is U+FFFC
    bool remind = false; // date mention with a reminder
    QString comment = {};   // comment thread anchored on this text

    bool isObject() const { return !mention.isEmpty() || !equation.isEmpty() || !date.isEmpty(); }
    bool sameStyle(const Span &o) const;
    bool operator==(const Span &o) const = default;
};

using Cell = QList<Span>;

struct Block {
    BlockType type = BlockType::Paragraph;
    QString id;             // stable block id (links to blocks, comments)
    int depth = 0;
    bool checked = false;   // to-do
    bool collapsed = false; // toggle, toggle heading
    bool toggleable = false; // heading that toggles its children
    QString language;       // code
    QString icon;           // callout, button
    int color = 0;          // block text shade
    int background = 0;     // block highlight shade
    QString media;          // image / file in the media folder
    QString url;            // bookmark, embed, linked files
    int width = 0;          // image width in px, 0 = natural
    QString align;          // image: left, center (default), right, full
    QString pageId;         // page link, database
    QString syncId;         // synced block content id
    QList<Span> spans;
    QList<Span> caption;    // image, code, media, bookmark
    QJsonObject data;       // type-specific settings (button actions, equation,
                            // bookmark preview, file info, table options…)
    // ColumnList: one list of blocks per column, with relative widths.
    QList<QList<Block>> columns;
    QList<double> widths;
    // Table: rows of cells.
    QList<QList<Cell>> rows;

    QString plainText() const;
    void setPlainText(const QString &text);
    bool operator==(const Block &o) const;

    static Block make(BlockType type, const QString &text = {}, int depth = 0);
    // A new random block id.
    static QString newId();
};

QJsonArray blocksToJson(const QList<Block> &blocks);
QList<Block> blocksFromJson(const QJsonArray &array);
QString blocksToJsonString(const QList<Block> &blocks);
QList<Block> blocksFromJsonString(const QString &json);
QJsonArray spansToJson(const QList<Span> &spans);
QList<Span> spansFromJson(const QJsonArray &array);

// Merges neighbouring spans with identical formatting and drops empty ones.
QList<Span> normalizeSpans(const QList<Span> &spans);

// Calls `fn` for every block, descending into columns.
void forEachBlock(const QList<Block> &blocks, const std::function<void(const Block &)> &fn);
void forEachBlockMutable(QList<Block> &blocks, const std::function<void(Block &)> &fn);
// Gives every block without one a fresh id (and fresh ids to duplicates).
void ensureBlockIds(QList<Block> &blocks);
// Fresh ids for every block (for copies).
void renewBlockIds(QList<Block> &blocks);

// Searchable text of a page, one line per block.
QString blocksPlainText(const QList<Block> &blocks);
// Pages this content points at (mentions, page links, databases), no duplicates.
QStringList blocksLinkedPages(const QList<Block> &blocks);
// Media files this content uses.
QStringList blocksMedia(const QList<Block> &blocks);
// Headings for a table of contents: (level 1…4, text, block id).
struct HeadingRef {
    int level;
    QString text;
    QString blockId;
};
QList<HeadingRef> blocksHeadings(const QList<Block> &blocks);
// Word and character counts.
QPair<int, int> blocksWordCount(const QList<Block> &blocks);
