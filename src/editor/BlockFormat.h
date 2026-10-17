#pragma once

#include "core/Blocks.h"

#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextFormat>
#include <QJsonObject>

class QTextBlock;
class QTextDocument;

// How Nebula's block model maps onto QTextDocument formats. Block-level
// properties live in QTextBlockFormat user properties; inline meaning in
// QTextCharFormat properties. Visual styling (sizes, weights, margins) is
// always derived from the semantic properties, so changing a block's type
// restyles it without losing inline formatting.
namespace BlockFormat
{
enum Prop {
    TypeProp = QTextFormat::UserProperty + 1,
    DepthProp,
    CheckedProp,
    LanguageProp,
    IconProp,
    BlockColorProp,
    BlockBackgroundProp,
    // char-level
    TextShadeProp,
    HighlightProp,
    CodeProp,
    PageIdProp,
    MediaProp,
    WidthProp,
    // block-level (continued)
    IdProp,          // stable block id
    ToggleableProp,  // toggle heading
    SyncProp,        // synced block id
    UrlProp,         // bookmark, embed, linked file or image
    AlignProp,       // image alignment
    CaptionProp,     // caption spans (JSON)
    DataProp,        // type-specific settings (JSON object)
    BlockPageProp,   // page id of a page link / database block
    // char-level (continued)
    CommentProp,     // comment thread on this text
    EquationProp,    // inline equation TeX
    DateProp,        // date mention (ISO)
    RemindProp,      // date mention with a reminder
    MentionBlockProp, // mention of a block inside a page
    // table formats
    TableKindProp,   // "columns" or "table"
};

enum ObjectType {
    DividerObject = QTextFormat::UserObject + 1,
    PageLinkObject,
    MentionObject,
    ImageObject,
    BlockObject,          // every other atomic block, drawn by its block type
    InlineEquationObject, // $…$ inside text
    DateObject,           // @date mentions
};

// Collapsed toggles keep a bit in QTextBlock::userState (outside undo).
constexpr int kCollapsedBit = 1;
bool isCollapsed(const QTextBlock &block);
void setCollapsed(QTextBlock block, bool collapsed);

// Per-page presentation.
struct Style {
    QString family;          // body font family
    double size = 16.0;      // body pixel size
    double contentWidth = 720;
    bool mono = false;

    double indent() const { return size * 1.625; } // one nesting level
};

BlockType typeOf(const QTextBlock &block);
int depthOf(const QTextBlock &block);
bool checkedOf(const QTextBlock &block);

// The block format for a block (margins, line height, properties).
QTextBlockFormat blockFormat(const Block &b, const Style &style);
// The base character format for text in a block of this type.
QTextCharFormat baseCharFormat(BlockType type, const Style &style, bool checked = false, int blockColor = 0);
// A character format for a span inside a block.
QTextCharFormat spanFormat(const Span &span, BlockType type, const Style &style, bool checked = false, int blockColor = 0);
// Reads a span's inline meaning back out of a character format.
Span spanOf(const QTextCharFormat &format, const QString &text, BlockType type, bool checked);

// Block properties as stored in a block format.
Block blockProps(const QTextBlock &block);

// Re-derives every fragment's format (and the block format) from semantics.
void restyleBlock(QTextBlock block, const Style &style);
// Applies new block-level properties to an existing block and restyles it.
void applyBlockProps(QTextBlock block, const Block &props, const Style &style);

// Object formats.
QTextCharFormat dividerFormat();
QTextCharFormat pageLinkFormat(const QString &pageId);
QTextCharFormat mentionFormat(const QString &pageId, const Style &style, const QString &blockId = {});
QTextCharFormat imageFormat(const QString &media, int width);
QTextCharFormat blockObjectFormat();
QTextCharFormat inlineEquationFormat(const QString &tex);
QTextCharFormat dateFormat(const QString &iso, bool remind);
// The character format of an inline object span (mention, equation, date).
QTextCharFormat objectSpanFormat(const Span &span, const Style &style);
bool isInlineObject(int objectType);

// Block-level data stored as JSON in a block format.
QJsonObject dataOf(const QTextBlock &block);
QString idOf(const QTextBlock &block);
QString syncOf(const QTextBlock &block);
bool toggleableOf(const QTextBlock &block);
// Blocks that fold their children away: toggles and toggle headings.
bool foldsChildren(const QTextBlock &block);
QList<Span> captionOf(const QTextBlock &block);

// Metrics shared by layout and painting code.
double headingSize(BlockType type, const Style &style);
// Line height for a block as Qt's proportional percentage, worked out so the
// line box is a fixed multiple of the font size like CSS line-height (1.5 for
// text, 1.3 for headings, 1.2 for the title) whatever the font's own metrics.
double lineHeightPercent(BlockType type, const Style &style);
double padTop(BlockType type, const Style &style);    // inside backgrounds (code, callout)
double padBottom(BlockType type, const Style &style);
double blockLeftInset(BlockType type, const Style &style); // marker gutter / padding
double blockRightInset(BlockType type, const Style &style);
// Qt collapses neighbouring margins (the larger wins), so all spacing lives
// in top margins computed from the previous block; `extra` adds a gap.
double topMarginFor(BlockType type, BlockType previous, const Style &style, double extra = 0);
}
