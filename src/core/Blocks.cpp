#include "Blocks.h"

#include "Types.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

namespace
{
struct TypeInfo {
    BlockType type;
    const char *key;
    const char *name;
};

const TypeInfo kTypes[] = {
    {BlockType::Paragraph, "text", QT_TRANSLATE_NOOP("Blocks", "Text")},
    {BlockType::Heading1, "h1", QT_TRANSLATE_NOOP("Blocks", "Heading 1")},
    {BlockType::Heading2, "h2", QT_TRANSLATE_NOOP("Blocks", "Heading 2")},
    {BlockType::Heading3, "h3", QT_TRANSLATE_NOOP("Blocks", "Heading 3")},
    {BlockType::Heading4, "h4", QT_TRANSLATE_NOOP("Blocks", "Heading 4")},
    {BlockType::Bullet, "bullet", QT_TRANSLATE_NOOP("Blocks", "Bulleted list")},
    {BlockType::Numbered, "number", QT_TRANSLATE_NOOP("Blocks", "Numbered list")},
    {BlockType::Todo, "todo", QT_TRANSLATE_NOOP("Blocks", "To-do list")},
    {BlockType::Toggle, "toggle", QT_TRANSLATE_NOOP("Blocks", "Toggle list")},
    {BlockType::Quote, "quote", QT_TRANSLATE_NOOP("Blocks", "Quote")},
    {BlockType::Callout, "callout", QT_TRANSLATE_NOOP("Blocks", "Callout")},
    {BlockType::Code, "code", QT_TRANSLATE_NOOP("Blocks", "Code")},
    {BlockType::Divider, "divider", QT_TRANSLATE_NOOP("Blocks", "Divider")},
    {BlockType::Image, "image", QT_TRANSLATE_NOOP("Blocks", "Image")},
    {BlockType::PageLink, "page", QT_TRANSLATE_NOOP("Blocks", "Page")},
    {BlockType::Title, "title", QT_TRANSLATE_NOOP("Blocks", "Title")},
    {BlockType::Table, "table", QT_TRANSLATE_NOOP("Blocks", "Table")},
    {BlockType::ColumnList, "columns", QT_TRANSLATE_NOOP("Blocks", "Columns")},
    {BlockType::Equation, "equation", QT_TRANSLATE_NOOP("Blocks", "Block equation")},
    {BlockType::TableOfContents, "toc", QT_TRANSLATE_NOOP("Blocks", "Table of contents")},
    {BlockType::Breadcrumb, "breadcrumb", QT_TRANSLATE_NOOP("Blocks", "Breadcrumb")},
    {BlockType::Button, "button", QT_TRANSLATE_NOOP("Blocks", "Button")},
    {BlockType::Bookmark, "bookmark", QT_TRANSLATE_NOOP("Blocks", "Web bookmark")},
    {BlockType::Embed, "embed", QT_TRANSLATE_NOOP("Blocks", "Embed")},
    {BlockType::Video, "video", QT_TRANSLATE_NOOP("Blocks", "Video")},
    {BlockType::Audio, "audio", QT_TRANSLATE_NOOP("Blocks", "Audio")},
    {BlockType::File, "file", QT_TRANSLATE_NOOP("Blocks", "File")},
    {BlockType::Pdf, "pdf", QT_TRANSLATE_NOOP("Blocks", "PDF")},
    {BlockType::Database, "database", QT_TRANSLATE_NOOP("Blocks", "Database")},
    {BlockType::Synced, "synced", QT_TRANSLATE_NOOP("Blocks", "Synced block")},
};

const QString kObject = QString(QChar::ObjectReplacementCharacter);
}

QJsonObject PageOptions::toJson() const
{
    QJsonObject o;
    if (font != QLatin1String("sans"))
        o[QStringLiteral("font")] = font;
    if (smallText)
        o[QStringLiteral("small")] = true;
    if (fullWidth)
        o[QStringLiteral("wide")] = true;
    if (locked)
        o[QStringLiteral("locked")] = true;
    if (!showToc)
        o[QStringLiteral("toc")] = false;
    if (!showDiscussion)
        o[QStringLiteral("discussion")] = false;
    if (backlinks != QLatin1String("expanded"))
        o[QStringLiteral("backlinks")] = backlinks;
    return o;
}

PageOptions PageOptions::fromJson(const QJsonObject &o)
{
    PageOptions p;
    p.font = o.value(QStringLiteral("font")).toString(QStringLiteral("sans"));
    p.smallText = o.value(QStringLiteral("small")).toBool();
    p.fullWidth = o.value(QStringLiteral("wide")).toBool();
    p.locked = o.value(QStringLiteral("locked")).toBool();
    p.showToc = o.value(QStringLiteral("toc")).toBool(true);
    p.showDiscussion = o.value(QStringLiteral("discussion")).toBool(true);
    p.backlinks = o.value(QStringLiteral("backlinks")).toString(QStringLiteral("expanded"));
    return p;
}

QString blockTypeKey(BlockType t)
{
    for (const auto &info : kTypes)
        if (info.type == t)
            return QString::fromLatin1(info.key);
    return QStringLiteral("text");
}

BlockType blockTypeFromKey(const QString &key)
{
    for (const auto &info : kTypes)
        if (key == QLatin1String(info.key))
            return info.type;
    return BlockType::Paragraph;
}

QString blockTypeName(BlockType t)
{
    for (const auto &info : kTypes)
        if (info.type == t)
            return QCoreApplication::translate("Blocks", info.name);
    return {};
}

bool blockTypeIsList(BlockType t)
{
    switch (t) {
    case BlockType::Bullet:
    case BlockType::Numbered:
    case BlockType::Todo:
    case BlockType::Toggle:
        return true;
    default:
        return false;
    }
}

bool blockTypeIsAtomic(BlockType t)
{
    switch (t) {
    case BlockType::Divider:
    case BlockType::Image:
    case BlockType::PageLink:
    case BlockType::Equation:
    case BlockType::TableOfContents:
    case BlockType::Breadcrumb:
    case BlockType::Button:
    case BlockType::Bookmark:
    case BlockType::Embed:
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf:
    case BlockType::Database:
        return true;
    default:
        return false;
    }
}

bool blockTypeIsHeading(BlockType t)
{
    return t == BlockType::Heading1 || t == BlockType::Heading2 || t == BlockType::Heading3 || t == BlockType::Heading4;
}

bool blockTypeIsContainer(BlockType t)
{
    return t == BlockType::Callout || t == BlockType::Quote;
}

bool blockTypeIsFile(BlockType t)
{
    return t == BlockType::Video || t == BlockType::Audio || t == BlockType::File || t == BlockType::Pdf;
}

QString textShadeName(int shade)
{
    static const char *names[] = {QT_TRANSLATE_NOOP("Blocks", "Default"), QT_TRANSLATE_NOOP("Blocks", "Starlight"),
                                  QT_TRANSLATE_NOOP("Blocks", "Mist"),    QT_TRANSLATE_NOOP("Blocks", "Haze"),
                                  QT_TRANSLATE_NOOP("Blocks", "Fog"),     QT_TRANSLATE_NOOP("Blocks", "Dust")};
    return QCoreApplication::translate("Blocks", names[qBound(0, shade, kTextShadeCount - 1)]);
}

QString highlightShadeName(int shade)
{
    static const char *names[] = {QT_TRANSLATE_NOOP("Blocks", "None"),     QT_TRANSLATE_NOOP("Blocks", "Graphite"),
                                  QT_TRANSLATE_NOOP("Blocks", "Ash"),      QT_TRANSLATE_NOOP("Blocks", "Smoke"),
                                  QT_TRANSLATE_NOOP("Blocks", "Dust"),     QT_TRANSLATE_NOOP("Blocks", "Moonlight"),
                                  QT_TRANSLATE_NOOP("Blocks", "Starlight")};
    return QCoreApplication::translate("Blocks", names[qBound(0, shade, kHighlightShadeCount - 1)]);
}

int shadeForNotionColor(const QString &name, bool background)
{
    // Warm colours read brighter, cool ones dimmer; gray is the quietest.
    static const QStringList order = {QStringLiteral("default"), QStringLiteral("yellow"), QStringLiteral("orange"),
                                      QStringLiteral("red"),     QStringLiteral("pink"),   QStringLiteral("purple"),
                                      QStringLiteral("blue"),    QStringLiteral("green"),  QStringLiteral("brown"),
                                      QStringLiteral("gray")};
    QString n = name.toLower();
    n.remove(QStringLiteral("_background"));
    n.remove(QStringLiteral("_bg"));
    if (n == QLatin1String("grey"))
        n = QStringLiteral("gray");
    const int i = int(order.indexOf(n));
    if (i <= 0)
        return 0;
    if (background) {
        static const int bg[] = {0, 6, 5, 4, 4, 3, 2, 2, 1, 1};
        return bg[i];
    }
    static const int fg[] = {0, 1, 1, 1, 2, 2, 3, 3, 4, 4};
    return fg[i];
}

bool Span::sameStyle(const Span &o) const
{
    return bold == o.bold && italic == o.italic && underline == o.underline && strike == o.strike && code == o.code
           && color == o.color && highlight == o.highlight && link == o.link && comment == o.comment && !isObject()
           && !o.isObject();
}

bool Block::operator==(const Block &o) const
{
    return type == o.type && id == o.id && depth == o.depth && checked == o.checked && collapsed == o.collapsed
           && toggleable == o.toggleable && language == o.language && icon == o.icon && color == o.color
           && background == o.background && media == o.media && url == o.url && width == o.width && align == o.align
           && pageId == o.pageId && syncId == o.syncId && spans == o.spans && caption == o.caption && data == o.data
           && columns == o.columns && widths == o.widths && rows == o.rows;
}

QString Block::plainText() const
{
    QString s;
    for (const Span &sp : spans)
        s += sp.text;
    return s;
}

void Block::setPlainText(const QString &text)
{
    spans.clear();
    if (!text.isEmpty()) {
        Span s;
        s.text = text;
        spans.append(s);
    }
}

Block Block::make(BlockType type, const QString &text, int depth)
{
    Block b;
    b.type = type;
    b.depth = depth;
    b.setPlainText(text);
    return b;
}

QString Block::newId()
{
    static const char alphabet[] = "abcdefghijkmnopqrstuvwxyz23456789";
    QString s;
    for (int i = 0; i < 10; ++i)
        s.append(QLatin1Char(alphabet[QRandomGenerator::global()->bounded(int(sizeof(alphabet) - 1))]));
    return s;
}

QList<Span> normalizeSpans(const QList<Span> &spans)
{
    QList<Span> out;
    for (const Span &s : spans) {
        if (s.text.isEmpty())
            continue;
        if (!out.isEmpty() && out.last().sameStyle(s))
            out.last().text += s.text;
        else
            out.append(s);
    }
    return out;
}

namespace
{
QJsonObject spanToJson(const Span &s)
{
    QJsonObject o;
    o[QStringLiteral("x")] = s.text;
    if (s.bold)
        o[QStringLiteral("b")] = true;
    if (s.italic)
        o[QStringLiteral("i")] = true;
    if (s.underline)
        o[QStringLiteral("u")] = true;
    if (s.strike)
        o[QStringLiteral("s")] = true;
    if (s.code)
        o[QStringLiteral("c")] = true;
    if (s.color)
        o[QStringLiteral("fg")] = s.color;
    if (s.highlight)
        o[QStringLiteral("bg")] = s.highlight;
    if (!s.link.isEmpty())
        o[QStringLiteral("a")] = s.link;
    if (!s.mention.isEmpty())
        o[QStringLiteral("m")] = s.mention;
    if (!s.mentionBlock.isEmpty())
        o[QStringLiteral("mb")] = s.mentionBlock;
    if (!s.equation.isEmpty())
        o[QStringLiteral("eq")] = s.equation;
    if (!s.date.isEmpty())
        o[QStringLiteral("dt")] = s.date;
    if (s.remind)
        o[QStringLiteral("rm")] = true;
    if (!s.comment.isEmpty())
        o[QStringLiteral("cm")] = s.comment;
    return o;
}

Span spanFromJson(const QJsonObject &o)
{
    Span s;
    s.text = o.value(QStringLiteral("x")).toString();
    s.bold = o.value(QStringLiteral("b")).toBool();
    s.italic = o.value(QStringLiteral("i")).toBool();
    s.underline = o.value(QStringLiteral("u")).toBool();
    s.strike = o.value(QStringLiteral("s")).toBool();
    s.code = o.value(QStringLiteral("c")).toBool();
    s.color = qBound(0, o.value(QStringLiteral("fg")).toInt(), kTextShadeCount - 1);
    s.highlight = qBound(0, o.value(QStringLiteral("bg")).toInt(), kHighlightShadeCount - 1);
    s.link = o.value(QStringLiteral("a")).toString();
    s.mention = o.value(QStringLiteral("m")).toString();
    s.mentionBlock = o.value(QStringLiteral("mb")).toString();
    s.equation = o.value(QStringLiteral("eq")).toString();
    s.date = o.value(QStringLiteral("dt")).toString();
    s.remind = o.value(QStringLiteral("rm")).toBool();
    s.comment = o.value(QStringLiteral("cm")).toString();
    if (s.isObject())
        s.text = kObject;
    return s;
}
}

QJsonArray spansToJson(const QList<Span> &spans)
{
    QJsonArray arr;
    for (const Span &s : spans)
        arr.append(spanToJson(s));
    return arr;
}

QList<Span> spansFromJson(const QJsonArray &array)
{
    QList<Span> out;
    out.reserve(array.size());
    for (const QJsonValue &v : array)
        out.append(spanFromJson(v.toObject()));
    return out;
}

QJsonArray blocksToJson(const QList<Block> &blocks)
{
    QJsonArray arr;
    for (const Block &b : blocks) {
        QJsonObject o;
        o[QStringLiteral("t")] = blockTypeKey(b.type);
        if (!b.id.isEmpty())
            o[QStringLiteral("id")] = b.id;
        if (b.depth)
            o[QStringLiteral("d")] = b.depth;
        if (b.checked)
            o[QStringLiteral("checked")] = true;
        if (b.collapsed)
            o[QStringLiteral("collapsed")] = true;
        if (b.toggleable)
            o[QStringLiteral("tg")] = true;
        if (!b.language.isEmpty())
            o[QStringLiteral("lang")] = b.language;
        if (!b.icon.isEmpty())
            o[QStringLiteral("icon")] = b.icon;
        if (b.color)
            o[QStringLiteral("fg")] = b.color;
        if (b.background)
            o[QStringLiteral("bg")] = b.background;
        if (!b.media.isEmpty())
            o[QStringLiteral("media")] = b.media;
        if (!b.url.isEmpty())
            o[QStringLiteral("url")] = b.url;
        if (b.width)
            o[QStringLiteral("w")] = b.width;
        if (!b.align.isEmpty())
            o[QStringLiteral("al")] = b.align;
        if (!b.pageId.isEmpty())
            o[QStringLiteral("page")] = b.pageId;
        if (!b.syncId.isEmpty())
            o[QStringLiteral("sync")] = b.syncId;
        if (!b.spans.isEmpty())
            o[QStringLiteral("r")] = spansToJson(b.spans);
        if (!b.caption.isEmpty())
            o[QStringLiteral("cap")] = spansToJson(b.caption);
        if (!b.data.isEmpty())
            o[QStringLiteral("x")] = b.data;
        if (!b.columns.isEmpty()) {
            QJsonArray cols;
            for (const QList<Block> &col : b.columns)
                cols.append(blocksToJson(col));
            o[QStringLiteral("cols")] = cols;
            QJsonArray w;
            for (double x : b.widths)
                w.append(x);
            if (!w.isEmpty())
                o[QStringLiteral("cw")] = w;
        } else if (!b.widths.isEmpty()) {
            QJsonArray w;
            for (double x : b.widths)
                w.append(x);
            o[QStringLiteral("cw")] = w;
        }
        if (!b.rows.isEmpty()) {
            QJsonArray rows;
            for (const QList<Cell> &row : b.rows) {
                QJsonArray cells;
                for (const Cell &cell : row)
                    cells.append(spansToJson(cell));
                rows.append(cells);
            }
            o[QStringLiteral("rows")] = rows;
        }
        arr.append(o);
    }
    return arr;
}

QList<Block> blocksFromJson(const QJsonArray &array)
{
    QList<Block> blocks;
    blocks.reserve(array.size());
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        Block b;
        b.type = blockTypeFromKey(o.value(QStringLiteral("t")).toString());
        b.id = o.value(QStringLiteral("id")).toString();
        b.depth = qBound(0, o.value(QStringLiteral("d")).toInt(), 12);
        b.checked = o.value(QStringLiteral("checked")).toBool();
        b.collapsed = o.value(QStringLiteral("collapsed")).toBool();
        b.toggleable = o.value(QStringLiteral("tg")).toBool();
        b.language = o.value(QStringLiteral("lang")).toString();
        b.icon = o.value(QStringLiteral("icon")).toString();
        b.color = qBound(0, o.value(QStringLiteral("fg")).toInt(), kTextShadeCount - 1);
        b.background = qBound(0, o.value(QStringLiteral("bg")).toInt(), kHighlightShadeCount - 1);
        b.media = o.value(QStringLiteral("media")).toString();
        b.url = o.value(QStringLiteral("url")).toString();
        b.width = qMax(0, o.value(QStringLiteral("w")).toInt());
        b.align = o.value(QStringLiteral("al")).toString();
        b.pageId = o.value(QStringLiteral("page")).toString();
        b.syncId = o.value(QStringLiteral("sync")).toString();
        b.spans = spansFromJson(o.value(QStringLiteral("r")).toArray());
        b.caption = spansFromJson(o.value(QStringLiteral("cap")).toArray());
        b.data = o.value(QStringLiteral("x")).toObject();
        for (const QJsonValue &col : o.value(QStringLiteral("cols")).toArray())
            b.columns.append(blocksFromJson(col.toArray()));
        for (const QJsonValue &w : o.value(QStringLiteral("cw")).toArray())
            b.widths.append(w.toDouble());
        for (const QJsonValue &row : o.value(QStringLiteral("rows")).toArray()) {
            QList<Cell> cells;
            for (const QJsonValue &cell : row.toArray())
                cells.append(spansFromJson(cell.toArray()));
            b.rows.append(cells);
        }
        blocks.append(b);
    }
    return blocks;
}

QString blocksToJsonString(const QList<Block> &blocks)
{
    return QString::fromUtf8(QJsonDocument(blocksToJson(blocks)).toJson(QJsonDocument::Compact));
}

QList<Block> blocksFromJsonString(const QString &json)
{
    return blocksFromJson(QJsonDocument::fromJson(json.toUtf8()).array());
}

void forEachBlock(const QList<Block> &blocks, const std::function<void(const Block &)> &fn)
{
    for (const Block &b : blocks) {
        fn(b);
        for (const QList<Block> &col : b.columns)
            forEachBlock(col, fn);
    }
}

void forEachBlockMutable(QList<Block> &blocks, const std::function<void(Block &)> &fn)
{
    for (Block &b : blocks) {
        fn(b);
        for (QList<Block> &col : b.columns)
            forEachBlockMutable(col, fn);
    }
}

void ensureBlockIds(QList<Block> &blocks)
{
    QSet<QString> seen;
    forEachBlockMutable(blocks, [&](Block &b) {
        if (b.id.isEmpty() || seen.contains(b.id))
            b.id = Block::newId();
        seen.insert(b.id);
    });
}

void renewBlockIds(QList<Block> &blocks)
{
    forEachBlockMutable(blocks, [](Block &b) { b.id = Block::newId(); });
}

namespace
{
QString spansText(const QList<Span> &spans)
{
    QString line;
    for (const Span &s : spans) {
        if (!s.equation.isEmpty())
            line += s.equation;
        else if (!s.isObject())
            line += s.text;
    }
    return line;
}
}

QString blocksPlainText(const QList<Block> &blocks)
{
    QStringList lines;
    forEachBlock(blocks, [&](const Block &b) {
        QString line = spansText(b.spans);
        if (b.type == BlockType::Equation)
            line = b.data.value(QStringLiteral("expr")).toString();
        if (b.type == BlockType::Bookmark || b.type == BlockType::Embed)
            line = b.data.value(QStringLiteral("title")).toString() + QLatin1Char(' ') + b.url;
        if (blockTypeIsFile(b.type))
            line = b.data.value(QStringLiteral("name")).toString();
        if (b.type == BlockType::Button)
            line = b.data.value(QStringLiteral("label")).toString();
        for (const QList<Cell> &row : b.rows) {
            QStringList cells;
            for (const Cell &c : row)
                cells.append(spansText(c));
            line += cells.join(QStringLiteral(" · ")) + QLatin1Char('\n');
        }
        const QString cap = spansText(b.caption);
        if (!cap.isEmpty())
            line += QLatin1Char(' ') + cap;
        line.replace(QChar::LineSeparator, QLatin1Char('\n'));
        if (!line.trimmed().isEmpty())
            lines.append(line.trimmed());
    });
    return lines.join(QLatin1Char('\n'));
}

QStringList blocksLinkedPages(const QList<Block> &blocks)
{
    QStringList ids;
    auto add = [&](const QString &id) {
        if (!id.isEmpty() && !ids.contains(id))
            ids.append(id);
    };
    auto addSpans = [&](const QList<Span> &spans) {
        for (const Span &s : spans)
            add(s.mention);
    };
    forEachBlock(blocks, [&](const Block &b) {
        if (b.type == BlockType::PageLink || b.type == BlockType::Database)
            add(b.pageId);
        addSpans(b.spans);
        addSpans(b.caption);
        for (const QList<Cell> &row : b.rows)
            for (const Cell &c : row)
                addSpans(c);
    });
    return ids;
}

QStringList blocksMedia(const QList<Block> &blocks)
{
    QStringList files;
    forEachBlock(blocks, [&](const Block &b) {
        if (!b.media.isEmpty() && !files.contains(b.media))
            files.append(b.media);
        const QString thumb = b.data.value(QStringLiteral("image")).toString();
        if (!thumb.isEmpty() && !files.contains(thumb))
            files.append(thumb);
    });
    return files;
}

QList<HeadingRef> blocksHeadings(const QList<Block> &blocks)
{
    QList<HeadingRef> out;
    forEachBlock(blocks, [&](const Block &b) {
        int level = 0;
        switch (b.type) {
        case BlockType::Heading1:
            level = 1;
            break;
        case BlockType::Heading2:
            level = 2;
            break;
        case BlockType::Heading3:
            level = 3;
            break;
        case BlockType::Heading4:
            level = 4;
            break;
        default:
            break;
        }
        // Headings nested under other blocks are left out, like Notion.
        if (level && b.depth == 0) {
            QString t = spansText(b.spans).simplified();
            if (!t.isEmpty())
                out.append({level, t, b.id});
        }
    });
    return out;
}

QPair<int, int> blocksWordCount(const QList<Block> &blocks)
{
    const QString text = blocksPlainText(blocks);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    const int words = int(text.split(ws, Qt::SkipEmptyParts).size());
    int chars = 0;
    for (const QChar c : text)
        if (!c.isSpace())
            ++chars;
    return {words, chars};
}
