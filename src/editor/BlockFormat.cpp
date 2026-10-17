#include "BlockFormat.h"
#include <cmath>
#include <QHash>
#include <QFontMetricsF>

#include "ui/Theme.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace BlockFormat
{

bool isCollapsed(const QTextBlock &block)
{
    const int s = block.userState();
    return s > 0 && (s & kCollapsedBit);
}

void setCollapsed(QTextBlock block, bool collapsed)
{
    int s = qMax(0, block.userState());
    s = collapsed ? (s | kCollapsedBit) : (s & ~kCollapsedBit);
    block.setUserState(s);
}

BlockType typeOf(const QTextBlock &block)
{
    const QVariant v = block.blockFormat().property(TypeProp);
    return v.isValid() ? BlockType(v.toInt()) : BlockType::Paragraph;
}

int depthOf(const QTextBlock &block)
{
    return block.blockFormat().property(DepthProp).toInt();
}

bool checkedOf(const QTextBlock &block)
{
    return block.blockFormat().property(CheckedProp).toBool();
}

QJsonObject dataOf(const QTextBlock &block)
{
    const QString s = block.blockFormat().property(DataProp).toString();
    return s.isEmpty() ? QJsonObject() : QJsonDocument::fromJson(s.toUtf8()).object();
}

QString idOf(const QTextBlock &block)
{
    return block.blockFormat().property(IdProp).toString();
}

QString syncOf(const QTextBlock &block)
{
    return block.blockFormat().property(SyncProp).toString();
}

bool toggleableOf(const QTextBlock &block)
{
    return block.blockFormat().property(ToggleableProp).toBool();
}

bool foldsChildren(const QTextBlock &block)
{
    const BlockType t = typeOf(block);
    return t == BlockType::Toggle || (blockTypeIsHeading(t) && toggleableOf(block));
}

QList<Span> captionOf(const QTextBlock &block)
{
    const QString s = block.blockFormat().property(CaptionProp).toString();
    return s.isEmpty() ? QList<Span>() : spansFromJson(QJsonDocument::fromJson(s.toUtf8()).array());
}

double headingSize(BlockType type, const Style &style)
{
    const double k = style.size / 16.0;
    switch (type) {
    case BlockType::Title:
        return 40 * k;
    case BlockType::Heading1:
        return 30 * k;
    case BlockType::Heading2:
        return 24 * k;
    case BlockType::Heading3:
        return 20 * k;
    case BlockType::Heading4:
        return 17.5 * k;
    case BlockType::Quote:
        return style.size * 1.1;
    case BlockType::Code:
        return style.size * 0.875;
    default:
        return style.size;
    }
}

namespace
{
int baseWeight(BlockType type)
{
    switch (type) {
    case BlockType::Title:
        return QFont::Bold;
    case BlockType::Heading1:
    case BlockType::Heading2:
    case BlockType::Heading3:
    case BlockType::Heading4:
        return QFont::DemiBold;
    default:
        return QFont::Normal;
    }
}

// Spacing above a block, and below it (the larger of a block's "above" and
// its predecessor's "below" wins).
double spaceAbove(BlockType t, const Style &s)
{
    const double k = s.size / 16.0;
    switch (t) {
    case BlockType::Heading1:
        return 32 * k;
    case BlockType::Heading2:
        return 24 * k;
    case BlockType::Heading3:
        return 18 * k;
    case BlockType::Heading4:
        return 14 * k;
    case BlockType::Quote:
    case BlockType::Callout:
        return 10 * k;
    case BlockType::Code:
    case BlockType::Image:
    case BlockType::Equation:
    case BlockType::Bookmark:
    case BlockType::Embed:
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf:
    case BlockType::Database:
    case BlockType::TableOfContents:
        return 10 * k;
    case BlockType::Button:
        return 6 * k;
    case BlockType::ColumnList:
    case BlockType::Table:
        return 10 * k;
    case BlockType::Divider:
        return 8 * k;
    default:
        // Text, lists, to-dos: Notion's 3px padding above and below plus 1px.
        return 7 * k;
    }
}

double spaceBelow(BlockType t, const Style &s)
{
    const double k = s.size / 16.0;
    switch (t) {
    case BlockType::Title:
        return 14 * k;
    case BlockType::Heading1:
    case BlockType::Heading2:
    case BlockType::Heading3:
    case BlockType::Heading4:
        return 4 * k;
    case BlockType::Quote:
    case BlockType::Callout:
    case BlockType::Code:
    case BlockType::Image:
    case BlockType::Equation:
    case BlockType::Bookmark:
    case BlockType::Embed:
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf:
    case BlockType::Database:
    case BlockType::TableOfContents:
        return 8 * k;
    case BlockType::Button:
        return 4 * k;
    case BlockType::ColumnList:
    case BlockType::Table:
        return 10 * k;
    default:
        return 0;
    }
}

}

double lineHeightPercent(BlockType type, const Style &style)
{
    // Qt's proportional height is relative to the font's natural line spacing
    // (ascent + descent + leading, about 1.2× the size for Inter), not to the
    // font size: convert the CSS-style multiple into that.
    double multiple = 1.5;
    switch (type) {
    case BlockType::Title:
        multiple = 1.2;
        break;
    case BlockType::Heading1:
    case BlockType::Heading2:
    case BlockType::Heading3:
    case BlockType::Heading4:
        multiple = 1.3;
        break;
    default:
        break;
    }
    QString family = style.family;
    if (type == BlockType::Title && !style.mono && family == Theme::uiFamily())
        family = Theme::displayFamily();
    if (type == BlockType::Code)
        family = Theme::monoFamily();
    const double px = headingSize(type, style);
    static QHash<QString, double> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4").arg(family).arg(px).arg(baseWeight(type)).arg(multiple);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    QFont f(family);
    f.setPixelSize(qMax(1, qRound(px)));
    f.setWeight(QFont::Weight(baseWeight(type)));
    f.setHintingPreference(QFont::PreferNoHinting);
    const QFontMetricsF fm(f);
    const double natural = std::ceil(fm.ascent() + fm.descent() + qMax(0.0, fm.leading()));
    const double percent = natural > 0 ? qBound(80.0, multiple * px / natural * 100.0, 200.0) : multiple * 100.0;
    cache.insert(key, percent);
    return percent;
}

double padTop(BlockType t, const Style &s)
{
    const double k = s.size / 16.0;
    if (t == BlockType::Code)
        return 30 * k;
    if (t == BlockType::Callout)
        return 14 * k;
    return 0;
}

double padBottom(BlockType t, const Style &s)
{
    const double k = s.size / 16.0;
    if (t == BlockType::Code)
        return 16 * k;
    if (t == BlockType::Callout)
        return 14 * k;
    return 0;
}

double blockLeftInset(BlockType t, const Style &s)
{
    const double k = s.size / 16.0;
    switch (t) {
    case BlockType::Heading1:
    case BlockType::Heading2:
    case BlockType::Heading3:
    case BlockType::Heading4:
        return 0; // toggle headings add their arrow's room in blockFormat()
    case BlockType::Bullet:
    case BlockType::Numbered:
    case BlockType::Todo:
    case BlockType::Toggle:
        return s.indent();
    case BlockType::Quote:
        return 18 * k;
    case BlockType::Callout:
        return 52 * k;
    case BlockType::Code:
        return 20 * k;
    default:
        return 0;
    }
}

double blockRightInset(BlockType t, const Style &s)
{
    const double k = s.size / 16.0;
    if (t == BlockType::Callout)
        return 18 * k;
    if (t == BlockType::Code)
        return 20 * k;
    return 0;
}

double topMarginFor(BlockType type, BlockType prev, const Style &s, double extra)
{
    if (type == BlockType::Title)
        return 0;
    return qMax(spaceAbove(type, s), spaceBelow(prev, s)) + padTop(type, s) + padBottom(prev, s) + extra;
}

QTextBlockFormat blockFormat(const Block &b, const Style &style)
{
    QTextBlockFormat f;
    f.setProperty(TypeProp, int(b.type));
    f.setProperty(DepthProp, b.depth);
    if (b.checked)
        f.setProperty(CheckedProp, true);
    if (!b.language.isEmpty())
        f.setProperty(LanguageProp, b.language);
    if (!b.icon.isEmpty())
        f.setProperty(IconProp, b.icon);
    if (b.color)
        f.setProperty(BlockColorProp, b.color);
    if (b.background)
        f.setProperty(BlockBackgroundProp, b.background);
    if (!b.id.isEmpty())
        f.setProperty(IdProp, b.id);
    if (b.toggleable && blockTypeIsHeading(b.type))
        f.setProperty(ToggleableProp, true);
    if (!b.syncId.isEmpty())
        f.setProperty(SyncProp, b.syncId);
    if (!b.url.isEmpty())
        f.setProperty(UrlProp, b.url);
    if (!b.align.isEmpty())
        f.setProperty(AlignProp, b.align);
    if (!b.caption.isEmpty())
        f.setProperty(CaptionProp, QString::fromUtf8(QJsonDocument(spansToJson(b.caption)).toJson(QJsonDocument::Compact)));
    if (!b.data.isEmpty())
        f.setProperty(DataProp, QString::fromUtf8(QJsonDocument(b.data).toJson(QJsonDocument::Compact)));
    if (!b.pageId.isEmpty() && (b.type == BlockType::Database || b.type == BlockType::PageLink))
        f.setProperty(BlockPageProp, b.pageId);
    if (!b.media.isEmpty() && b.type != BlockType::Image)
        f.setProperty(MediaProp, b.media);
    const double toggleRoom = b.toggleable && blockTypeIsHeading(b.type) ? style.indent() : 0;
    f.setLeftMargin(b.depth * style.indent() + blockLeftInset(b.type, style) + toggleRoom);
    f.setRightMargin(blockRightInset(b.type, style));
    f.setBottomMargin(0);
    // The top margin is set by the editor's normalisation, which knows the
    // previous block; this is a sensible default for new blocks.
    f.setTopMargin(topMarginFor(b.type, BlockType::Paragraph, style, 0));
    if (blockTypeIsAtomic(b.type))
        f.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
    else
        f.setLineHeight(lineHeightPercent(b.type, style), QTextBlockFormat::ProportionalHeight);
    if (b.type == BlockType::Code)
        f.setNonBreakableLines(false);
    if (b.type == BlockType::Image) {
        if (b.align == QLatin1String("left"))
            f.setAlignment(Qt::AlignLeft);
        else if (b.align == QLatin1String("right"))
            f.setAlignment(Qt::AlignRight);
        else
            f.setAlignment(Qt::AlignHCenter);
    }
    return f;
}

QTextCharFormat baseCharFormat(BlockType type, const Style &style, bool checked, int blockColor)
{
    const auto &c = Theme::colors();
    QTextCharFormat f;
    QString family = style.family;
    if (type == BlockType::Title && !style.mono && family == Theme::uiFamily())
        family = Theme::displayFamily();
    if (type == BlockType::Code)
        family = Theme::monoFamily();
    f.setFontFamilies({family});
    f.setProperty(QTextFormat::FontPixelSize, headingSize(type, style));
    f.setFontWeight(baseWeight(type));
    f.setFontHintingPreference(QFont::PreferNoHinting);
    if (type == BlockType::Title)
        f.setFontLetterSpacing(99.0);
    QColor fg = c.moon;
    if (type == BlockType::Title || blockTypeIsHeading(type))
        fg = c.starlight;
    if (type == BlockType::Quote)
        fg = c.mist;
    if (blockColor)
        fg = Theme::textShade(blockColor);
    if (type == BlockType::Todo && checked) {
        fg = c.fog;
        f.setFontStrikeOut(true);
    }
    f.setForeground(fg);
    return f;
}

QTextCharFormat spanFormat(const Span &span, BlockType type, const Style &style, bool checked, int blockColor)
{
    const auto &c = Theme::colors();
    QTextCharFormat f = baseCharFormat(type, style, checked, blockColor);
    if (span.bold && type != BlockType::Title)
        f.setFontWeight(qMax(int(QFont::Bold), baseWeight(type) + 200));
    if (span.italic)
        f.setFontItalic(true);
    if (span.underline)
        f.setFontUnderline(true);
    if (span.strike)
        f.setFontStrikeOut(true);
    if (span.code && type != BlockType::Code) {
        f.setFontFamilies({Theme::monoFamily()});
        f.setProperty(QTextFormat::FontPixelSize, headingSize(type, style) * 0.86);
        f.setBackground(QColor(0x28, 0x28, 0x2F));
        f.setForeground(type == BlockType::Paragraph ? QColor(0xDA, 0xDA, 0xE0) : f.foreground().color());
        f.setProperty(CodeProp, true);
    }
    if (span.color) {
        f.setForeground(Theme::textShade(span.color));
        f.setProperty(TextShadeProp, span.color);
    }
    if (span.highlight) {
        const Theme::Highlight hl = Theme::highlightShade(span.highlight);
        f.setBackground(hl.background);
        if (hl.text.isValid())
            f.setForeground(hl.text);
        f.setProperty(HighlightProp, span.highlight);
    }
    if (!span.link.isEmpty()) {
        f.setAnchor(true);
        f.setAnchorHref(span.link);
        if (!span.color && !span.highlight)
            f.setForeground(c.starlight);
        f.setFontUnderline(true);
        f.setUnderlineColor(c.dust);
    }
    if (!span.comment.isEmpty()) {
        f.setProperty(CommentProp, span.comment);
        if (!span.highlight)
            f.setBackground(QColor(0x3A, 0x3A, 0x44, 150));
        f.setFontUnderline(true);
        f.setUnderlineColor(c.mist);
    }
    return f;
}

bool isInlineObject(int objectType)
{
    return objectType == MentionObject || objectType == InlineEquationObject || objectType == DateObject;
}

QTextCharFormat objectSpanFormat(const Span &span, const Style &style)
{
    QTextCharFormat f;
    if (!span.mention.isEmpty())
        f = mentionFormat(span.mention, style, span.mentionBlock);
    else if (!span.equation.isEmpty())
        f = inlineEquationFormat(span.equation);
    else if (!span.date.isEmpty())
        f = dateFormat(span.date, span.remind);
    if (!span.comment.isEmpty())
        f.setProperty(CommentProp, span.comment);
    return f;
}

Span spanOf(const QTextCharFormat &format, const QString &text, BlockType type, bool checked)
{
    Span s;
    s.text = text;
    s.bold = type != BlockType::Title && format.fontWeight() > baseWeight(type) + 50;
    s.italic = format.fontItalic();
    s.underline = format.fontUnderline() && !format.isAnchor();
    s.strike = format.fontStrikeOut() && !(type == BlockType::Todo && checked);
    s.code = format.property(CodeProp).toBool();
    s.color = format.property(TextShadeProp).toInt();
    s.highlight = format.property(HighlightProp).toInt();
    if (format.isAnchor())
        s.link = format.anchorHref();
    s.comment = format.property(CommentProp).toString();
    if (format.objectType() == MentionObject) {
        s = Span{};
        s.text = QString(QChar::ObjectReplacementCharacter);
        s.mention = format.property(PageIdProp).toString();
        s.mentionBlock = format.property(MentionBlockProp).toString();
        s.comment = format.property(CommentProp).toString();
    } else if (format.objectType() == InlineEquationObject) {
        s = Span{};
        s.text = QString(QChar::ObjectReplacementCharacter);
        s.equation = format.property(EquationProp).toString();
        s.comment = format.property(CommentProp).toString();
    } else if (format.objectType() == DateObject) {
        s = Span{};
        s.text = QString(QChar::ObjectReplacementCharacter);
        s.date = format.property(DateProp).toString();
        s.remind = format.property(RemindProp).toBool();
        s.comment = format.property(CommentProp).toString();
    }
    return s;
}

Block blockProps(const QTextBlock &block)
{
    const QTextBlockFormat f = block.blockFormat();
    Block b;
    b.type = typeOf(block);
    b.depth = f.property(DepthProp).toInt();
    b.checked = f.property(CheckedProp).toBool();
    b.language = f.property(LanguageProp).toString();
    b.icon = f.property(IconProp).toString();
    b.color = f.property(BlockColorProp).toInt();
    b.background = f.property(BlockBackgroundProp).toInt();
    b.collapsed = isCollapsed(block);
    b.id = f.property(IdProp).toString();
    b.toggleable = f.property(ToggleableProp).toBool() && blockTypeIsHeading(b.type);
    b.syncId = f.property(SyncProp).toString();
    b.url = f.property(UrlProp).toString();
    b.align = f.property(AlignProp).toString();
    b.caption = captionOf(block);
    b.data = dataOf(block);
    b.pageId = f.property(BlockPageProp).toString();
    if (b.type != BlockType::Image)
        b.media = f.property(MediaProp).toString();
    if (blockTypeIsAtomic(b.type)) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat cf = it.fragment().charFormat();
            if (cf.isImageFormat() || cf.objectType() >= QTextFormat::UserObject) {
                if (b.type == BlockType::Image) {
                    b.media = cf.property(MediaProp).toString();
                    b.width = cf.property(WidthProp).toInt();
                }
                if (b.type == BlockType::PageLink)
                    b.pageId = cf.property(PageIdProp).toString();
                break;
            }
        }
    }
    return b;
}

void applyBlockProps(QTextBlock block, const Block &props, const Style &style)
{
    const Block old = blockProps(block);
    QTextCursor c(block);
    QTextBlockFormat bf = blockFormat(props, style);
    // Keep the normalised top margin unless the type changes.
    if (old.type == props.type)
        bf.setTopMargin(block.blockFormat().topMargin());
    c.setBlockFormat(bf);
    // Re-derive every text fragment from its meaning.
    QList<QPair<int, int>> ranges;
    QList<Span> spans;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment frag = it.fragment();
        const QTextCharFormat cf = frag.charFormat();
        if (cf.objectType() >= QTextFormat::UserObject)
            continue; // objects keep their format (inline ones size from the block)
        ranges.append({frag.position(), frag.length()});
        spans.append(spanOf(cf, frag.text(), old.type, old.checked));
    }
    for (int i = 0; i < ranges.size(); ++i) {
        c.setPosition(ranges[i].first);
        c.setPosition(ranges[i].first + ranges[i].second, QTextCursor::KeepAnchor);
        c.setCharFormat(spanFormat(spans[i], props.type, style, props.checked, props.color));
    }
    c.setPosition(block.position());
    c.setBlockCharFormat(baseCharFormat(props.type, style, props.checked, props.color));
}

void restyleBlock(QTextBlock block, const Style &style)
{
    applyBlockProps(block, blockProps(block), style);
}

QTextCharFormat dividerFormat()
{
    QTextCharFormat f;
    f.setObjectType(DividerObject);
    return f;
}

QTextCharFormat pageLinkFormat(const QString &pageId)
{
    QTextCharFormat f;
    f.setObjectType(PageLinkObject);
    f.setProperty(PageIdProp, pageId);
    return f;
}

QTextCharFormat mentionFormat(const QString &pageId, const Style &style, const QString &blockId)
{
    Q_UNUSED(style)
    QTextCharFormat f;
    f.setObjectType(MentionObject);
    f.setProperty(PageIdProp, pageId);
    if (!blockId.isEmpty())
        f.setProperty(MentionBlockProp, blockId);
    return f;
}

QTextCharFormat blockObjectFormat()
{
    QTextCharFormat f;
    f.setObjectType(BlockObject);
    return f;
}

QTextCharFormat inlineEquationFormat(const QString &tex)
{
    QTextCharFormat f;
    f.setObjectType(InlineEquationObject);
    f.setProperty(EquationProp, tex);
    return f;
}

QTextCharFormat dateFormat(const QString &iso, bool remind)
{
    QTextCharFormat f;
    f.setObjectType(DateObject);
    f.setProperty(DateProp, iso);
    if (remind)
        f.setProperty(RemindProp, true);
    return f;
}

QTextCharFormat imageFormat(const QString &media, int width)
{
    QTextCharFormat f;
    f.setObjectType(ImageObject);
    f.setProperty(MediaProp, media);
    f.setProperty(WidthProp, width);
    return f;
}

}
