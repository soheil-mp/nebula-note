#include "Markdown.h"

#include <QDateTime>
#include <climits>
#include <QLocale>
#include <QRegularExpression>
#include <QUrl>

namespace Markdown
{

// ---- export ------------------------------------------------------------------

namespace
{
QString escapeText(const QString &s)
{
    QString out;
    out.reserve(s.size() + 8);
    for (const QChar c : s) {
        switch (c.unicode()) {
        case '\\':
        case '*':
        case '_':
        case '`':
        case '[':
        case ']':
        case '~':
        case '$':
            out += QLatin1Char('\\');
            out += c;
            break;
        case 0x2028:
            out += QStringLiteral("  \n");
            break;
        default:
            out += c;
        }
    }
    return out;
}

bool isEmojiIcon(const QString &icon)
{
    return !icon.isEmpty() && !icon.startsWith(QLatin1String("glyph:"));
}
}

QString spansToMarkdown(const QList<Span> &spans, const ExportOptions &options)
{
    QString out;
    for (const Span &s : spans) {
        if (!s.equation.isEmpty()) {
            // GitHub's unambiguous inline math: $`…`$.
            out += QStringLiteral("$`%1`$").arg(s.equation);
            continue;
        }
        if (!s.date.isEmpty()) {
            const QDateTime when = QDateTime::fromString(s.date, s.date.size() > 10 ? Qt::ISODate : Qt::ISODate);
            const QDate day = when.isValid() ? when.date() : QDate::fromString(s.date.left(10), Qt::ISODate);
            QString label = QLocale(QLocale::English).toString(day, QStringLiteral("MMMM d, yyyy"));
            if (s.date.size() > 10 && when.isValid())
                label += QLatin1Char(' ') + QLocale(QLocale::English).toString(when.time(), QStringLiteral("h:mm AP"));
            out += QStringLiteral("<time datetime=\"%1\"%2>@%3</time>").arg(s.date, s.remind ? QStringLiteral(" data-remind=\"true\"") : QString(), label);
            continue;
        }
        if (!s.mention.isEmpty()) {
            const QString title = options.pageTitle ? options.pageTitle(s.mention) : QStringLiteral("page");
            const QString link = options.pageLink ? options.pageLink(s.mention) : QString();
            out += link.isEmpty() ? escapeText(title) : QStringLiteral("[%1](%2)").arg(escapeText(title), link);
            continue;
        }
        if (s.text.isEmpty())
            continue;
        if (s.code) {
            QString fence = QStringLiteral("`");
            while (s.text.contains(fence))
                fence += QLatin1Char('`');
            const bool pad = s.text.startsWith(QLatin1Char('`')) || s.text.endsWith(QLatin1Char('`'));
            QString inner = pad ? QLatin1Char(' ') + s.text + QLatin1Char(' ') : s.text;
            QString chunk = fence + inner + fence;
            if (!s.link.isEmpty())
                chunk = QStringLiteral("[%1](%2)").arg(chunk, s.link);
            out += chunk;
            continue;
        }
        // Emphasis markers can't sit next to whitespace: keep it outside.
        const QString text = s.text;
        int lead = 0, trail = 0;
        while (lead < text.size() && text[lead].isSpace())
            ++lead;
        while (trail < text.size() - lead && text[text.size() - 1 - trail].isSpace())
            ++trail;
        const QString core = text.mid(lead, text.size() - lead - trail);
        QString open, close;
        if (s.bold) {
            open += QStringLiteral("**");
            close.prepend(QStringLiteral("**"));
        }
        if (s.italic) {
            open += QLatin1Char('*');
            close.prepend(QLatin1Char('*'));
        }
        if (s.strike) {
            open += QStringLiteral("~~");
            close.prepend(QStringLiteral("~~"));
        }
        if (s.underline) {
            open += QStringLiteral("<u>");
            close.prepend(QStringLiteral("</u>"));
        }
        QString chunk = core.isEmpty() ? QString() : open + escapeText(core) + close;
        if (!s.link.isEmpty() && !core.isEmpty())
            chunk = QStringLiteral("[%1](%2)").arg(chunk, s.link);
        out += escapeText(text.left(lead)) + chunk + escapeText(text.right(trail));
    }
    return out;
}

QString fromBlocks(const QList<Block> &blocks, const ExportOptions &options)
{
    QString out;
    QList<int> numbers(16, 0);
    BlockType prevType = BlockType::Title;
    int prevDepth = -1;
    // Toggles open a <details> element that closes when a block at the same
    // depth or shallower follows.
    QList<int> openToggles;

    auto closeTogglesTo = [&](int depth) {
        while (!openToggles.isEmpty() && openToggles.last() >= depth) {
            const int d = openToggles.takeLast();
            out += QString(d * 4, QLatin1Char(' ')) + QStringLiteral("</details>\n\n");
        }
    };

    for (int i = 0; i < blocks.size(); ++i) {
        const Block &b = blocks[i];
        if (b.type == BlockType::Title)
            continue;
        closeTogglesTo(b.depth);
        const QString indent(b.depth * 4, QLatin1Char(' '));
        const QString text = spansToMarkdown(b.spans, options);

        if (b.type == BlockType::Numbered) {
            if (prevType != BlockType::Numbered || prevDepth < b.depth)
                numbers[qMin(b.depth, 15)] = 0;
            for (int d = b.depth + 1; d < 16; ++d)
                numbers[d] = 0;
        }
        // Consecutive list items stay tight; everything else is separated.
        const bool tight = blockTypeIsList(b.type) && blockTypeIsList(prevType) && b.type != BlockType::Toggle
                           && prevType != BlockType::Toggle;
        if (!out.isEmpty() && !tight && !out.endsWith(QStringLiteral("\n\n")))
            out += QLatin1Char('\n');

        auto prefixLines = [&](const QString &body, const QString &prefix) {
            QStringList lines = body.split(QLatin1Char('\n'));
            for (QString &l : lines)
                l = indent + prefix + l;
            return lines.join(QLatin1Char('\n'));
        };

        const QString toggle = b.toggleable ? QStringLiteral(" {toggle=\"true\"}") : QString();
        auto linkLine = [&](const QString &label, const QString &target) {
            out += indent + (target.isEmpty() ? escapeText(label) : QStringLiteral("[%1](%2)").arg(escapeText(label), target)) + QLatin1Char('\n');
        };
        auto mediaTarget = [&](const QString &media) {
            const QString link = options.mediaLink ? options.mediaLink(media) : QStringLiteral("media/") + media;
            return QString::fromUtf8(QUrl::toPercentEncoding(link, "/"));
        };
        switch (b.type) {
        case BlockType::Heading1:
            out += indent + QStringLiteral("# ") + text + toggle + QLatin1Char('\n');
            break;
        case BlockType::Heading2:
            out += indent + QStringLiteral("## ") + text + toggle + QLatin1Char('\n');
            break;
        case BlockType::Heading3:
            out += indent + QStringLiteral("### ") + text + toggle + QLatin1Char('\n');
            break;
        case BlockType::Heading4:
            out += indent + QStringLiteral("#### ") + text + toggle + QLatin1Char('\n');
            break;
        case BlockType::Equation: {
            const QString expr = b.data.value(QStringLiteral("expr")).toString();
            out += indent + QStringLiteral("$$\n") + prefixLines(expr, QString()) + QLatin1Char('\n') + indent + QStringLiteral("$$\n");
            break;
        }
        case BlockType::TableOfContents:
            out += indent + QStringLiteral("<table_of_contents/>\n");
            break;
        case BlockType::Breadcrumb:
            out += indent + QStringLiteral("<breadcrumb/>\n");
            break;
        case BlockType::Table: {
            // GitHub tables need a header row: the first row serves as one.
            int cols = 0;
            for (const auto &row : b.rows)
                cols = qMax(cols, int(row.size()));
            for (int r = 0; r < b.rows.size(); ++r) {
                QStringList cells;
                for (int c = 0; c < cols; ++c) {
                    QString cell = c < b.rows[r].size() ? spansToMarkdown(b.rows[r][c], options) : QString();
                    cell.replace(QLatin1Char('|'), QStringLiteral("\\|"));
                    cell.replace(QStringLiteral("  \n"), QStringLiteral("<br>"));
                    cells.append(cell);
                }
                out += indent + QStringLiteral("| ") + cells.join(QStringLiteral(" | ")) + QStringLiteral(" |\n");
                if (r == 0)
                    out += indent + QStringLiteral("|") + QStringList(cols, QStringLiteral(" --- ")).join(QLatin1Char('|')) + QStringLiteral("|\n");
            }
            break;
        }
        case BlockType::ColumnList: {
            out += indent + QStringLiteral("<columns>\n");
            for (const QList<Block> &col : b.columns)
                out += indent + QStringLiteral("<column>\n\n") + fromBlocks(col, options) + QStringLiteral("\n\n") + indent + QStringLiteral("</column>\n");
            out += indent + QStringLiteral("</columns>\n");
            break;
        }
        case BlockType::Bookmark:
        case BlockType::Embed: {
            const QString title = b.data.value(QStringLiteral("title")).toString();
            linkLine(title.isEmpty() ? b.url : title, b.url);
            break;
        }
        case BlockType::Video:
        case BlockType::Audio:
        case BlockType::File:
        case BlockType::Pdf: {
            QString name = b.data.value(QStringLiteral("name")).toString();
            if (name.isEmpty())
                name = !b.media.isEmpty() ? b.media : b.url;
            linkLine(name, !b.media.isEmpty() ? mediaTarget(b.media) : b.url);
            break;
        }
        case BlockType::Database:
            linkLine(options.pageTitle ? options.pageTitle(b.pageId) : QStringLiteral("database"), options.pageLink ? options.pageLink(b.pageId) : QString());
            break;
        case BlockType::Button:
        case BlockType::Synced:
            break;
        case BlockType::Bullet:
            out += indent + QStringLiteral("- ") + text + QLatin1Char('\n');
            break;
        case BlockType::Numbered: {
            const int n = ++numbers[qMin(b.depth, 15)];
            out += indent + QString::number(n) + QStringLiteral(". ") + text + QLatin1Char('\n');
            break;
        }
        case BlockType::Todo:
            out += indent + (b.checked ? QStringLiteral("- [x] ") : QStringLiteral("- [ ] ")) + text + QLatin1Char('\n');
            break;
        case BlockType::Toggle:
            out += indent + QStringLiteral("<details>\n") + indent + QStringLiteral("<summary>") + text
                   + QStringLiteral("</summary>\n\n");
            openToggles.append(b.depth);
            break;
        case BlockType::Quote:
            out += prefixLines(text.isEmpty() ? QString() : text, QStringLiteral("> ")) + QLatin1Char('\n');
            break;
        case BlockType::Callout: {
            const QString icon = isEmojiIcon(b.icon) ? b.icon + QLatin1Char(' ') : QString();
            out += indent + QStringLiteral("<aside>\n") + indent + icon + text + QLatin1Char('\n') + indent
                   + QStringLiteral("</aside>\n");
            break;
        }
        case BlockType::Code: {
            QString code = b.plainText();
            code.replace(QChar::LineSeparator, QLatin1Char('\n'));
            QString fence = QStringLiteral("```");
            while (code.contains(fence))
                fence += QLatin1Char('`');
            out += indent + fence + b.language + QLatin1Char('\n') + prefixLines(code, QString()) + QLatin1Char('\n')
                   + indent + fence + QLatin1Char('\n');
            break;
        }
        case BlockType::Divider:
            out += indent + QStringLiteral("---\n");
            break;
        case BlockType::Image: {
            if (b.media.isEmpty())
                break;
            QString alt;
            for (const Span &c : b.caption)
                alt += c.text;
            if (alt.isEmpty())
                alt = b.plainText().remove(QChar::ObjectReplacementCharacter);
            out += indent + QStringLiteral("![%1](%2)\n").arg(escapeText(alt), mediaTarget(b.media));
            break;
        }
        case BlockType::PageLink: {
            const QString title = options.pageTitle ? options.pageTitle(b.pageId) : QStringLiteral("page");
            const QString link = options.pageLink ? options.pageLink(b.pageId) : QString();
            out += indent + (link.isEmpty() ? escapeText(title) : QStringLiteral("[%1](%2)").arg(escapeText(title), link))
                   + QLatin1Char('\n');
            break;
        }
        default:
            out += text.isEmpty() ? QString() : prefixLines(text, QString()) + QLatin1Char('\n');
            if (text.isEmpty())
                out += QStringLiteral("\n");
            break;
        }
        prevType = b.type;
        prevDepth = b.depth;
    }
    closeTogglesTo(0);
    while (out.endsWith(QStringLiteral("\n\n")))
        out.chop(1);
    return out;
}

// ---- import ------------------------------------------------------------------

namespace
{
struct Style {
    bool bold = false, italic = false, underline = false, strike = false;
    QString link;
};

void appendText(QList<Span> &spans, const QString &text, const Style &st)
{
    if (text.isEmpty())
        return;
    Span s;
    s.text = text;
    s.bold = st.bold;
    s.italic = st.italic;
    s.underline = st.underline;
    s.strike = st.strike;
    s.link = st.link;
    spans.append(s);
}

// Finds the closing delimiter for emphasis starting after `from`.
int findClosing(const QString &t, int from, const QString &delim)
{
    int i = from;
    while (i < t.size()) {
        if (t[i] == QLatin1Char('\\')) {
            i += 2;
            continue;
        }
        if (t[i] == QLatin1Char('`')) {
            // Skip code spans: delimiters inside them don't count.
            int ticks = 0;
            while (i + ticks < t.size() && t[i + ticks] == QLatin1Char('`'))
                ++ticks;
            const int end = t.indexOf(QString(ticks, QLatin1Char('`')), i + ticks);
            i = end < 0 ? i + ticks : end + ticks;
            continue;
        }
        if (t.mid(i, delim.size()) == delim && i > from && !t[i - 1].isSpace()) {
            // "**" must not be the start of a longer run when we look for "*".
            if (delim.size() == 1 && i + 1 < t.size() && t[i + 1] == delim[0]) {
                // Accept "***" endings (bold+italic): the single closes first.
                if (i + 2 < t.size() && t[i + 2] == delim[0])
                    return i;
                i += 2;
                continue;
            }
            if (delim == QLatin1String("_") && i + 1 < t.size() && t[i + 1].isLetterOrNumber()) {
                ++i;
                continue;
            }
            return i;
        }
        ++i;
    }
    return -1;
}

void parseInto(QList<Span> &spans, const QString &t, Style st, const ImportOptions &opt)
{
    QString buf;
    auto flush = [&] {
        appendText(spans, buf, st);
        buf.clear();
    };
    int i = 0;
    while (i < t.size()) {
        const QChar c = t[i];
        if (c == QLatin1Char('\\') && i + 1 < t.size() && (t[i + 1].isPunct() || t[i + 1].isSymbol())) {
            buf += t[i + 1];
            i += 2;
            continue;
        }
        if (c == QLatin1Char('`')) {
            int ticks = 0;
            while (i + ticks < t.size() && t[i + ticks] == QLatin1Char('`'))
                ++ticks;
            const int end = t.indexOf(QString(ticks, QLatin1Char('`')), i + ticks);
            if (end > 0) {
                flush();
                QString code = t.mid(i + ticks, end - i - ticks);
                if (code.size() > 2 && code.startsWith(QLatin1Char(' ')) && code.endsWith(QLatin1Char(' ')))
                    code = code.mid(1, code.size() - 2);
                Span s;
                s.text = code;
                s.code = true;
                s.link = st.link;
                s.bold = st.bold;
                s.italic = st.italic;
                spans.append(s);
                i = end + ticks;
                continue;
            }
        }
        if (c == QLatin1Char('$') && i + 1 < t.size()) {
            // Inline math: $`…`$ (GitHub) or $…$ with no spaces just inside.
            QString expr;
            int next = -1;
            if (t[i + 1] == QLatin1Char('`')) {
                const int end = t.indexOf(QStringLiteral("`$"), i + 2);
                if (end > i + 2) {
                    expr = t.mid(i + 2, end - i - 2);
                    next = end + 2;
                }
            } else if (t[i + 1] != QLatin1Char('$') && !t[i + 1].isSpace() && (i == 0 || !t[i - 1].isLetterOrNumber())) {
                int end = i + 1;
                while (end < t.size() && t[end] != QLatin1Char('$'))
                    end += t[end] == QLatin1Char('\\') ? 2 : 1;
                if (end < t.size() && end > i + 1 && !t[end - 1].isSpace() && (end + 1 >= t.size() || !t[end + 1].isDigit())) {
                    expr = t.mid(i + 1, end - i - 1);
                    next = end + 1;
                }
            }
            if (next > 0) {
                flush();
                Span s;
                s.text = QString(QChar::ObjectReplacementCharacter);
                s.equation = expr;
                spans.append(s);
                i = next;
                continue;
            }
        }
        if (c == QLatin1Char('<') && t.mid(i, 5).compare(QLatin1String("<time"), Qt::CaseInsensitive) == 0) {
            // Date mentions: <time datetime="2026-10-20T22:00" data-remind="true">…</time>
            static const QRegularExpression time(QStringLiteral("^<time\\s+datetime=\"([^\"]+)\"([^>]*)>.*?</time>"),
                                                 QRegularExpression::CaseInsensitiveOption);
            const auto m = time.matchView(QStringView(t).mid(i));
            if (m.hasMatch()) {
                flush();
                Span s;
                s.text = QString(QChar::ObjectReplacementCharacter);
                s.date = m.captured(1);
                s.remind = m.captured(2).contains(QLatin1String("data-remind"));
                spans.append(s);
                i += int(m.capturedLength());
                continue;
            }
        }
        if (c == QLatin1Char('!') && i + 1 < t.size() && t[i + 1] == QLatin1Char('[')) {
            // Inline image: keep its alt text.
            const int close = t.indexOf(QStringLiteral("]("), i + 2);
            const int paren = close < 0 ? -1 : t.indexOf(QLatin1Char(')'), close + 2);
            if (close > 0 && paren > 0) {
                buf += t.mid(i + 2, close - i - 2);
                i = paren + 1;
                continue;
            }
        }
        if (c == QLatin1Char('[')) {
            // [text](url): find the matching bracket, allowing nesting.
            int depth = 0, j = i;
            for (; j < t.size(); ++j) {
                if (t[j] == QLatin1Char('\\')) {
                    ++j;
                    continue;
                }
                if (t[j] == QLatin1Char('['))
                    ++depth;
                else if (t[j] == QLatin1Char(']') && --depth == 0)
                    break;
            }
            if (j < t.size() - 1 && t[j + 1] == QLatin1Char('(')) {
                int pd = 0, k = j + 1;
                for (; k < t.size(); ++k) {
                    if (t[k] == QLatin1Char('('))
                        ++pd;
                    else if (t[k] == QLatin1Char(')') && --pd == 0)
                        break;
                }
                if (k < t.size()) {
                    flush();
                    const QString label = t.mid(i + 1, j - i - 1);
                    QString href = t.mid(j + 2, k - j - 2).trimmed();
                    // Drop an optional "title".
                    if (const int sp = href.indexOf(QLatin1String(" \"")); sp > 0)
                        href = href.left(sp);
                    if (href.startsWith(QLatin1Char('<')) && href.endsWith(QLatin1Char('>')))
                        href = href.mid(1, href.size() - 2);
                    const QString page = opt.resolvePage ? opt.resolvePage(href) : QString();
                    if (!page.isEmpty()) {
                        Span s;
                        s.text = QString(QChar::ObjectReplacementCharacter);
                        s.mention = page;
                        spans.append(s);
                    } else {
                        Style inner = st;
                        inner.link = href;
                        parseInto(spans, label, inner, opt);
                    }
                    i = k + 1;
                    continue;
                }
            }
        }
        if (c == QLatin1Char('<')) {
            if (t.mid(i, 3).compare(QLatin1String("<u>"), Qt::CaseInsensitive) == 0) {
                const int end = t.indexOf(QLatin1String("</u>"), i + 3, Qt::CaseInsensitive);
                if (end > 0) {
                    flush();
                    Style inner = st;
                    inner.underline = true;
                    parseInto(spans, t.mid(i + 3, end - i - 3), inner, opt);
                    i = end + 4;
                    continue;
                }
            }
            static const QRegularExpression autolink(QStringLiteral("^<((?:https?|mailto):[^>\\s]+)>"));
            const auto m = autolink.matchView(QStringView(t).mid(i));
            if (m.hasMatch()) {
                flush();
                Style inner = st;
                inner.link = m.captured(1);
                appendText(spans, m.captured(1), inner);
                i += int(m.capturedLength());
                continue;
            }
            static const QRegularExpression tag(QStringLiteral("^</?(?:br|span|strong|em|b|i|mark|s|del)\\b[^>]*>"),
                                                QRegularExpression::CaseInsensitiveOption);
            const auto mt = tag.matchView(QStringView(t).mid(i));
            if (mt.hasMatch()) {
                if (mt.captured().startsWith(QLatin1String("<br"), Qt::CaseInsensitive))
                    buf += QChar::LineSeparator;
                i += int(mt.capturedLength());
                continue;
            }
        }
        if (c == QLatin1Char('*') || c == QLatin1Char('_')) {
            const bool dbl = i + 1 < t.size() && t[i + 1] == c;
            const QString delim = dbl ? QString(2, c) : QString(c);
            const bool leftFlanking = i + delim.size() < t.size() && !t[i + delim.size()].isSpace();
            const bool intraword = c == QLatin1Char('_') && i > 0 && t[i - 1].isLetterOrNumber();
            if (leftFlanking && !intraword) {
                const int end = findClosing(t, i + int(delim.size()), delim);
                if (end > 0) {
                    flush();
                    Style inner = st;
                    if (dbl)
                        inner.bold = true;
                    else
                        inner.italic = true;
                    parseInto(spans, t.mid(i + delim.size(), end - i - delim.size()), inner, opt);
                    i = end + int(delim.size());
                    continue;
                }
            }
        }
        if (c == QLatin1Char('~')) {
            const bool dbl = i + 1 < t.size() && t[i + 1] == c;
            const QString delim = dbl ? QStringLiteral("~~") : QStringLiteral("~");
            if (i + delim.size() < t.size() && !t[i + delim.size()].isSpace()) {
                const int end = findClosing(t, i + int(delim.size()), delim);
                if (end > 0) {
                    flush();
                    Style inner = st;
                    inner.strike = true;
                    parseInto(spans, t.mid(i + delim.size(), end - i - delim.size()), inner, opt);
                    i = end + int(delim.size());
                    continue;
                }
            }
        }
        if ((c == QLatin1Char('h') || c == QLatin1Char('w')) && st.link.isEmpty()) {
            // Bare URLs become links.
            static const QRegularExpression bare(QStringLiteral("^(?:https?://|www\\.)[^\\s<>\\[\\]]+[^\\s<>\\[\\].,;:!?)'\"]"));
            if (i == 0 || !t[i - 1].isLetterOrNumber()) {
                const auto m = bare.matchView(QStringView(t).mid(i));
                if (m.hasMatch()) {
                    flush();
                    Style inner = st;
                    inner.link = m.captured().startsWith(QLatin1String("www.")) ? QStringLiteral("https://") + m.captured()
                                                                              : m.captured();
                    appendText(spans, m.captured(), inner);
                    i += int(m.capturedLength());
                    continue;
                }
            }
        }
        buf += c;
        ++i;
    }
    flush();
}

QString decodeEntities(QString s)
{
    s.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    s.replace(QLatin1String("&lt;"), QLatin1String("<"));
    s.replace(QLatin1String("&gt;"), QLatin1String(">"));
    s.replace(QLatin1String("&quot;"), QLatin1String("\""));
    s.replace(QLatin1String("&#39;"), QLatin1String("'"));
    s.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return s;
}

int indentWidth(const QString &line)
{
    int w = 0;
    for (const QChar c : line) {
        if (c == QLatin1Char(' '))
            ++w;
        else if (c == QLatin1Char('\t'))
            w += 4;
        else
            break;
    }
    return w;
}

// Leading emoji (one grapheme-ish cluster) of a callout.
QString takeLeadingEmoji(QString &text)
{
    if (text.isEmpty())
        return {};
    const char32_t first = text.at(0).isHighSurrogate() && text.size() > 1
                               ? QChar::surrogateToUcs4(text.at(0), text.at(1))
                               : text.at(0).unicode();
    const bool emoji = (first >= 0x1F000 && first <= 0x1FAFF) || (first >= 0x2600 && first <= 0x27BF)
                       || (first >= 0x2B00 && first <= 0x2BFF) || first == 0x2139 || first == 0x203C;
    if (!emoji)
        return {};
    int len = first > 0xFFFF ? 2 : 1;
    // Variation selectors and ZWJ sequences.
    while (len < text.size()) {
        const QChar c = text.at(len);
        if (c.unicode() == 0xFE0F || c.unicode() == 0x200D) {
            ++len;
            if (c.unicode() == 0x200D && len < text.size())
                len += text.at(len).isHighSurrogate() ? 2 : 1;
            continue;
        }
        break;
    }
    const QString icon = text.left(len);
    text = text.mid(len).trimmed();
    return icon;
}

class Parser
{
public:
    Parser(const QStringList &lines, const ImportOptions &opt)
        : m_lines(lines)
        , m_opt(opt)
    {
    }

    QList<Block> parse(int baseDepth)
    {
        m_base = baseDepth;
        while (m_pos < m_lines.size())
            step();
        flushParagraph();
        return m_out;
    }

private:
    void addBlock(Block b)
    {
        // Linked attachments are copied into the workspace and shown as file
        // blocks. A paragraph that only links an attachment becomes that block.
        QList<Block> files = extractAttachments(b.spans, m_opt, b.depth + (blockTypeIsList(b.type) ? 1 : 0));
        bool onlyLink = b.type == BlockType::Paragraph && files.size() == 1;
        if (onlyLink)
            for (const Span &sp : b.spans)
                if (!sp.text.trimmed().isEmpty() && !sp.link.isEmpty())
                    onlyLink = false;
        if (onlyLink) {
            QString text;
            for (const Span &sp : b.spans)
                text += sp.text;
            files.first().depth = b.depth;
            if (files.first().type == BlockType::Image || text.trimmed() == files.first().data.value(QStringLiteral("name")).toString()
                || text.trimmed().isEmpty()) {
                m_out.append(files.first());
                return;
            }
        }
        m_out.append(std::move(b));
        m_out.append(files);
    }

    int listDepth(int indent)
    {
        // Levels that aren't shallower than this item are closed.
        while (!m_indents.isEmpty() && m_indents.last() >= indent)
            m_indents.removeLast();
        const int depth = m_base + int(m_indents.size());
        m_indents.append(indent);
        return depth;
    }

    int continuationDepth(int indent) const
    {
        // A non-list line indented under a list item belongs to that item.
        int depth = 0;
        for (int i : m_indents)
            if (indent > i)
                ++depth;
        return m_base + depth;
    }

    // Lines indented under a block (callout, toggle heading) are its children.
    void consumeChildren(int indent, int depth)
    {
        QStringList inner;
        while (m_pos < m_lines.size() && (m_lines[m_pos].trimmed().isEmpty() || indentWidth(m_lines[m_pos]) >= indent + 4)) {
            inner.append(m_lines[m_pos].mid(qMin(indentWidth(m_lines[m_pos]), indent + 4)));
            ++m_pos;
        }
        while (!inner.isEmpty() && inner.last().trimmed().isEmpty()) {
            inner.removeLast();
            --m_pos;
        }
        if (!inner.isEmpty()) {
            Parser child(inner, m_opt);
            m_out.append(child.parse(depth + 1));
        }
    }

    void flushParagraph()
    {
        if (m_para.isEmpty())
            return;
        QString text;
        for (int i = 0; i < m_para.size(); ++i) {
            QString l = m_para[i];
            const bool hardBreak = l.endsWith(QLatin1String("  ")) || l.endsWith(QLatin1Char('\\'));
            l = l.trimmed();
            if (l.endsWith(QLatin1Char('\\')))
                l.chop(1);
            text += l;
            if (i + 1 < m_para.size())
                text += hardBreak ? QString(QChar::LineSeparator) : QStringLiteral(" ");
        }
        Block b;
        b.type = BlockType::Paragraph;
        b.depth = m_paraDepth;
        b.spans = normalizeSpans(parseInline(decodeEntities(text), m_opt));
        // A paragraph that is only a page link is a page block (Notion exports
        // sub-pages this way).
        if (b.spans.size() == 1 && !b.spans[0].mention.isEmpty()) {
            b.type = BlockType::PageLink;
            b.pageId = b.spans[0].mention;
            b.spans.clear();
        }
        addBlock(b);
        m_para.clear();
    }

    void step()
    {
        const QString raw = m_lines[m_pos];
        const QString line = raw.trimmed();
        const int indent = indentWidth(raw);

        if (line.isEmpty()) {
            flushParagraph();
            ++m_pos;
            return;
        }

        // Fenced code.
        static const QRegularExpression fence(QStringLiteral("^(`{3,}|~{3,})\\s*([\\w+#.-]*)"));
        if (auto m = fence.match(line); m.hasMatch()) {
            flushParagraph();
            const QString marker = m.captured(1);
            Block b;
            b.type = BlockType::Code;
            b.language = m.captured(2).toLower();
            b.depth = continuationDepth(indent);
            QStringList code;
            ++m_pos;
            while (m_pos < m_lines.size() && !m_lines[m_pos].trimmed().startsWith(marker)) {
                QString l = m_lines[m_pos];
                // Remove the fence's own indentation.
                int strip = qMin(indent, indentWidth(l));
                code.append(l.mid(strip));
                ++m_pos;
            }
            ++m_pos; // closing fence
            b.setPlainText(code.join(QChar::LineSeparator));
            addBlock(b);
            return;
        }

        // <details><summary>…</summary> … </details>  → toggle with children.
        if (line.startsWith(QLatin1String("<details"), Qt::CaseInsensitive)) {
            flushParagraph();
            const int depth = continuationDepth(indent);
            QString summary;
            QStringList inner;
            int nesting = 0;
            QString rest = line.mid(line.indexOf(QLatin1Char('>')) + 1);
            ++m_pos;
            auto consume = [&](QString l) {
                static const QRegularExpression sum(QStringLiteral("<summary>(.*?)</summary>"),
                                                    QRegularExpression::CaseInsensitiveOption);
                if (summary.isEmpty()) {
                    if (auto sm = sum.match(l); sm.hasMatch()) {
                        summary = sm.captured(1);
                        l.remove(sm.capturedStart(), sm.capturedLength());
                    }
                }
                return l;
            };
            rest = consume(rest);
            if (!rest.trimmed().isEmpty())
                inner.append(rest);
            while (m_pos < m_lines.size()) {
                QString l = m_lines[m_pos];
                const QString t = l.trimmed();
                if (t.startsWith(QLatin1String("<details"), Qt::CaseInsensitive))
                    ++nesting;
                if (t.startsWith(QLatin1String("</details>"), Qt::CaseInsensitive)) {
                    if (nesting == 0) {
                        ++m_pos;
                        break;
                    }
                    --nesting;
                }
                if (nesting == 0 && summary.isEmpty())
                    l = consume(l);
                inner.append(l.mid(qMin(indentWidth(l), indent)));
                ++m_pos;
            }
            Block b;
            b.type = BlockType::Toggle;
            b.depth = depth;
            b.spans = normalizeSpans(parseInline(decodeEntities(summary.trimmed()), m_opt));
            addBlock(b);
            Parser child(inner, m_opt);
            m_out.append(child.parse(depth + 1));
            return;
        }

        // <aside> … </aside>  → callout (Notion's export format). Its content
        // is blocks: the first paragraph is the callout's text, the rest (lists,
        // images, more paragraphs) its children.
        if (line.startsWith(QLatin1String("<aside"), Qt::CaseInsensitive)) {
            flushParagraph();
            const int depth = continuationDepth(indent);
            QStringList body;
            QString first = line.mid(line.indexOf(QLatin1Char('>')) + 1);
            bool closed = false;
            if (first.contains(QLatin1String("</aside>"), Qt::CaseInsensitive)) {
                first = first.left(first.indexOf(QLatin1String("</aside>"), 0, Qt::CaseInsensitive));
                closed = true;
            }
            if (!first.trimmed().isEmpty())
                body.append(first);
            ++m_pos;
            int nesting = 0;
            while (!closed && m_pos < m_lines.size()) {
                QString l = m_lines[m_pos];
                ++m_pos;
                if (l.trimmed().startsWith(QLatin1String("<aside"), Qt::CaseInsensitive))
                    ++nesting;
                if (l.contains(QLatin1String("</aside>"), Qt::CaseInsensitive)) {
                    if (nesting == 0) {
                        l = l.left(l.indexOf(QLatin1String("</aside>"), 0, Qt::CaseInsensitive));
                        if (!l.trimmed().isEmpty())
                            body.append(l);
                        break;
                    }
                    --nesting;
                }
                body.append(l);
            }
            // The aside's own indentation isn't structure.
            int common = INT_MAX;
            for (const QString &l : body)
                if (!l.trimmed().isEmpty())
                    common = qMin(common, indentWidth(l));
            if (common == INT_MAX)
                common = 0;
            for (QString &l : body)
                l = l.mid(qMin(common, indentWidth(l)));
            Parser inner(body, m_opt);
            QList<Block> blocks = inner.parse(0);
            Block b;
            b.type = BlockType::Callout;
            b.depth = depth;
            if (!blocks.isEmpty() && blocks.first().type == BlockType::Paragraph && blocks.first().depth == 0) {
                b.spans = blocks.takeFirst().spans;
            }
            if (!b.spans.isEmpty() && !b.spans.first().isObject()) {
                QString t = b.spans.first().text;
                b.icon = takeLeadingEmoji(t);
                if (!b.icon.isEmpty())
                    b.spans.first().text = t;
            }
            m_out.append(b);
            for (Block &child : blocks) {
                child.depth += depth + 1;
                m_out.append(child);
            }
            consumeChildren(indent, depth); // children written after it, indented
            return;
        }

        // Block equations: $$ … $$ on their own lines, or $$expr$$ on one.
        if (line.startsWith(QLatin1String("$$"))) {
            flushParagraph();
            QString expr;
            if (line.size() > 4 && line.endsWith(QLatin1String("$$"))) {
                expr = line.mid(2, line.size() - 4);
                ++m_pos;
            } else {
                QStringList body;
                const QString first = line.mid(2).trimmed();
                if (!first.isEmpty())
                    body.append(first);
                ++m_pos;
                while (m_pos < m_lines.size()) {
                    const QString l = m_lines[m_pos].trimmed();
                    ++m_pos;
                    if (l.endsWith(QLatin1String("$$"))) {
                        if (l.size() > 2)
                            body.append(l.left(l.size() - 2));
                        break;
                    }
                    body.append(l);
                }
                expr = body.join(QLatin1Char('\n'));
            }
            Block b;
            b.type = BlockType::Equation;
            b.depth = continuationDepth(indent);
            b.data.insert(QStringLiteral("expr"), expr.trimmed());
            addBlock(b);
            return;
        }

        // Notion's enhanced Markdown tags.
        if (line.compare(QLatin1String("<table_of_contents/>"), Qt::CaseInsensitive) == 0
            || line.compare(QLatin1String("<breadcrumb/>"), Qt::CaseInsensitive) == 0) {
            flushParagraph();
            Block b;
            b.type = line.startsWith(QLatin1String("<table"), Qt::CaseInsensitive) ? BlockType::TableOfContents : BlockType::Breadcrumb;
            b.depth = continuationDepth(indent);
            addBlock(b);
            ++m_pos;
            return;
        }
        if (line.compare(QLatin1String("<columns>"), Qt::CaseInsensitive) == 0) {
            flushParagraph();
            Block b;
            b.type = BlockType::ColumnList;
            b.depth = continuationDepth(indent);
            QStringList current;
            bool inColumn = false;
            int nesting = 0;
            ++m_pos;
            while (m_pos < m_lines.size()) {
                const QString l = m_lines[m_pos];
                const QString tl = l.trimmed();
                ++m_pos;
                if (tl.compare(QLatin1String("<columns>"), Qt::CaseInsensitive) == 0)
                    ++nesting;
                if (tl.compare(QLatin1String("</columns>"), Qt::CaseInsensitive) == 0) {
                    if (nesting == 0)
                        break;
                    --nesting;
                }
                if (nesting == 0 && tl.compare(QLatin1String("<column>"), Qt::CaseInsensitive) == 0) {
                    inColumn = true;
                    current.clear();
                    continue;
                }
                if (nesting == 0 && tl.compare(QLatin1String("</column>"), Qt::CaseInsensitive) == 0) {
                    Parser child(current, m_opt);
                    b.columns.append(child.parse(0));
                    inColumn = false;
                    continue;
                }
                if (inColumn)
                    current.append(l.mid(qMin(indentWidth(l), indent)));
            }
            if (b.columns.size() >= 2) {
                for (int k = 0; k < b.columns.size(); ++k)
                    b.widths.append(1.0 / b.columns.size());
                addBlock(b);
            } else {
                for (const QList<Block> &col : b.columns)
                    m_out.append(col);
            }
            return;
        }

        // Headings, optionally toggleable: "## Title {toggle="true"}".
        static const QRegularExpression heading(QStringLiteral("^(#{1,6})\\s+(.*?)\\s*#*\\s*$"));
        if (auto m = heading.match(line); m.hasMatch() && indent < 4) {
            flushParagraph();
            const int level = int(m.captured(1).size());
            Block b;
            b.type = level == 1 ? BlockType::Heading1 : level == 2 ? BlockType::Heading2 : level == 3 ? BlockType::Heading3 : BlockType::Heading4;
            b.depth = continuationDepth(indent);
            QString text = m.captured(2);
            static const QRegularExpression toggleAttr(QStringLiteral("\\s*\\{toggle=\"true\"\\}$"));
            if (const auto tm = toggleAttr.match(text); tm.hasMatch()) {
                b.toggleable = true;
                text = text.left(tm.capturedStart());
            }
            b.spans = normalizeSpans(parseInline(decodeEntities(text), m_opt));
            m_indents.clear();
            addBlock(b);
            ++m_pos;
            // A toggle heading's children are the indented lines after it.
            if (b.toggleable)
                consumeChildren(indent, b.depth);
            return;
        }

        // Divider.
        static const QRegularExpression rule(QStringLiteral("^(?:\\*\\s*){3,}$|^(?:-\\s*){3,}$|^(?:_\\s*){3,}$"));
        if (rule.match(line).hasMatch()) {
            flushParagraph();
            Block b;
            b.type = BlockType::Divider;
            b.depth = continuationDepth(indent);
            addBlock(b);
            ++m_pos;
            return;
        }

        // Lists.
        static const QRegularExpression todo(QStringLiteral("^[-*+]\\s+\\[([ xX])\\]\\s?(.*)$"));
        static const QRegularExpression bullet(QStringLiteral("^[-*+]\\s+(.*)$"));
        static const QRegularExpression numbered(QStringLiteral("^\\d{1,9}[.)]\\s+(.*)$"));
        if (auto m = todo.match(line); m.hasMatch()) {
            flushParagraph();
            Block b;
            b.type = BlockType::Todo;
            b.depth = listDepth(indent);
            b.checked = m.captured(1).trimmed().size() == 1;
            b.spans = normalizeSpans(parseInline(decodeEntities(m.captured(2)), m_opt));
            addBlock(b);
            ++m_pos;
            return;
        }
        if (auto m = bullet.match(line); m.hasMatch()) {
            flushParagraph();
            Block b;
            b.type = BlockType::Bullet;
            b.depth = listDepth(indent);
            b.spans = normalizeSpans(parseInline(decodeEntities(m.captured(1)), m_opt));
            addBlock(b);
            ++m_pos;
            return;
        }
        if (auto m = numbered.match(line); m.hasMatch()) {
            flushParagraph();
            Block b;
            b.type = BlockType::Numbered;
            b.depth = listDepth(indent);
            b.spans = normalizeSpans(parseInline(decodeEntities(m.captured(1)), m_opt));
            addBlock(b);
            ++m_pos;
            return;
        }

        // Quotes (and GitHub alerts, which become callouts).
        if (line.startsWith(QLatin1Char('>'))) {
            flushParagraph();
            const int depth = continuationDepth(indent);
            QStringList body;
            while (m_pos < m_lines.size() && m_lines[m_pos].trimmed().startsWith(QLatin1Char('>'))) {
                QString l = m_lines[m_pos].trimmed().mid(1);
                if (l.startsWith(QLatin1Char(' ')))
                    l = l.mid(1);
                body.append(l);
                ++m_pos;
            }
            Block b;
            b.type = BlockType::Quote;
            b.depth = depth;
            static const QRegularExpression alert(QStringLiteral("^\\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\\]\\s*$"),
                                                  QRegularExpression::CaseInsensitiveOption);
            if (!body.isEmpty()) {
                if (auto am = alert.match(body.first()); am.hasMatch()) {
                    b.type = BlockType::Callout;
                    const QString kind = am.captured(1).toUpper();
                    b.icon = kind == QLatin1String("WARNING") || kind == QLatin1String("CAUTION") ? QStringLiteral("glyph:alert")
                             : kind == QLatin1String("TIP")                                      ? QStringLiteral("glyph:bulb")
                                                                                                  : QStringLiteral("glyph:info");
                    body.removeFirst();
                }
            }
            b.spans = normalizeSpans(parseInline(decodeEntities(body.join(QChar::LineSeparator)), m_opt));
            addBlock(b);
            return;
        }

        // A standalone image.
        static const QRegularExpression image(QStringLiteral("^!\\[([^\\]]*)\\]\\(([^)]+)\\)$"));
        if (auto m = image.match(line); m.hasMatch()) {
            flushParagraph();
            QString src = m.captured(2).trimmed();
            if (const int sp = src.indexOf(QLatin1String(" \"")); sp > 0)
                src = src.left(sp);
            const QString media = m_opt.importImage ? m_opt.importImage(src) : QString();
            if (!media.isEmpty()) {
                Block b;
                b.type = BlockType::Image;
                b.depth = continuationDepth(indent);
                b.media = media;
                addBlock(b);
            } else if (!m.captured(1).isEmpty()) {
                addBlock(Block::make(BlockType::Paragraph, m.captured(1), continuationDepth(indent)));
            }
            ++m_pos;
            return;
        }

        // GFM tables become simple tables with a header row.
        static const QRegularExpression separator(QStringLiteral("^\\|?\\s*:?-{2,}:?\\s*(\\|\\s*:?-{2,}:?\\s*)*\\|?$"));
        if (line.startsWith(QLatin1Char('|')) && m_pos + 1 < m_lines.size() && separator.match(m_lines[m_pos + 1].trimmed()).hasMatch()) {
            flushParagraph();
            auto splitRow = [](QString row) {
                row = row.trimmed();
                if (row.startsWith(QLatin1Char('|')))
                    row = row.mid(1);
                if (row.endsWith(QLatin1Char('|')) && !row.endsWith(QLatin1String("\\|")))
                    row.chop(1);
                QStringList cells;
                QString cur;
                for (int k = 0; k < row.size(); ++k) {
                    if (row[k] == QLatin1Char('\\') && k + 1 < row.size() && row[k + 1] == QLatin1Char('|')) {
                        cur += QLatin1Char('|');
                        ++k;
                    } else if (row[k] == QLatin1Char('|')) {
                        cells.append(cur.trimmed());
                        cur.clear();
                    } else {
                        cur += row[k];
                    }
                }
                cells.append(cur.trimmed());
                return cells;
            };
            Block b;
            b.type = BlockType::Table;
            b.depth = continuationDepth(indent);
            b.data.insert(QStringLiteral("hr"), true);
            int cols = 0;
            QList<QStringList> raw;
            raw.append(splitRow(line));
            m_pos += 2;
            while (m_pos < m_lines.size() && m_lines[m_pos].trimmed().startsWith(QLatin1Char('|'))) {
                raw.append(splitRow(m_lines[m_pos]));
                ++m_pos;
            }
            for (const QStringList &r : raw)
                cols = qMax(cols, int(r.size()));
            for (const QStringList &r : raw) {
                QList<Cell> row;
                for (int c = 0; c < cols; ++c) {
                    QString cell = c < r.size() ? r[c] : QString();
                    cell.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption), QString(QChar::LineSeparator));
                    row.append(normalizeSpans(parseInline(decodeEntities(cell), m_opt)));
                }
                b.rows.append(row);
            }
            addBlock(b);
            return;
        }

        // Anything else is paragraph text; a change of indentation starts a new one.
        const int depth = continuationDepth(indent);
        if (indent < 2 && !m_indents.isEmpty() && m_para.isEmpty())
            m_indents.clear();
        const int finalDepth = (indent < 2) ? m_base : depth;
        if (!m_para.isEmpty() && finalDepth != m_paraDepth)
            flushParagraph();
        if (m_para.isEmpty())
            m_paraDepth = finalDepth;
        m_para.append(raw);
        ++m_pos;
    }

    QStringList m_lines;
    const ImportOptions &m_opt;
    int m_pos = 0;
    int m_base = 0;
    QList<int> m_indents;
    QStringList m_para;
    int m_paraDepth = 0;
    QList<Block> m_out;
};
}

QList<Block> extractAttachments(QList<Span> &spans, const ImportOptions &options, int depth)
{
    QList<Block> out;
    if (!options.importFile)
        return out;
    QStringList seen;
    for (Span &s : spans) {
        if (s.link.isEmpty() || s.link.startsWith(QLatin1String("mailto:")))
            continue;
        BlockType type = BlockType::File;
        const QString media = options.importFile(s.link, &type);
        if (media.isEmpty())
            continue;
        const QString href = s.link;
        s.link.clear();
        if (seen.contains(media))
            continue;
        seen.append(media);
        Block b;
        b.type = type;
        b.depth = depth;
        b.media = media;
        if (type != BlockType::Image) {
            QString name = QUrl::fromPercentEncoding(href.toUtf8());
            name = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
            b.data.insert(QStringLiteral("name"), s.text.trimmed().isEmpty() ? name : s.text.trimmed());
        }
        out.append(b);
    }
    return out;
}

QList<Span> parseInline(const QString &text, const ImportOptions &options)
{
    QList<Span> spans;
    parseInto(spans, text, Style{}, options);
    return normalizeSpans(spans);
}

QList<Block> toBlocks(const QString &markdown, const ImportOptions &options)
{
    QString md = markdown;
    md.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    md.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // Front matter is metadata, not content.
    if (md.startsWith(QLatin1String("---\n"))) {
        const int end = md.indexOf(QLatin1String("\n---"), 4);
        if (end > 0 && md.left(end).contains(QLatin1Char(':')))
            md = md.mid(md.indexOf(QLatin1Char('\n'), end + 1) + 1);
    }
    Parser p(md.split(QLatin1Char('\n')), options);
    return p.parse(0);
}

QString takeTitle(QString &markdown)
{
    QStringList lines = markdown.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString l = lines[i].trimmed();
        if (l.isEmpty())
            continue;
        if (l.startsWith(QLatin1String("# "))) {
            QString title = l.mid(2).trimmed();
            lines.removeAt(i);
            markdown = lines.join(QLatin1Char('\n'));
            // The title is plain text: strip inline markup.
            QString plain;
            for (const Span &s : parseInline(title))
                plain += s.text;
            return plain.remove(QChar::ObjectReplacementCharacter).trimmed();
        }
        break;
    }
    return {};
}

}
