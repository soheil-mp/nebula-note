#include "Exporter.h"

#include "Csv.h"
#include "Markdown.h"
#include "Workspace.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMimeDatabase>
#include <QPdfWriter>
#include <QPageLayout>
#include <QSet>
#include <QTextDocument>
#include <QUrl>

namespace Exporter
{

namespace
{
Renderers &renderers()
{
    static Renderers r;
    return r;
}
} // namespace

void setRenderers(const Renderers &r)
{
    renderers() = r;
}

QString safeFileName(const QString &title)
{
    QString s = title.trimmed();
    static const QString bad = QStringLiteral("/\\:*?\"<>|\n\r\t");
    for (QChar &c : s)
        if (bad.contains(c))
            c = QLatin1Char(' ');
    s = s.simplified();
    if (s.size() > 80)
        s = s.left(80).trimmed();
    if (s.isEmpty() || s == QLatin1String(".") || s == QLatin1String(".."))
        s = QStringLiteral("Untitled");
    return s;
}

namespace
{
QString tr(const char *s)
{
    return QCoreApplication::translate("Exporter", s);
}

bool writeFile(const QString &path, const QByteArray &data, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(data);
    return true;
}

QString relativePath(const QString &fromFile, const QString &toFile)
{
    return QDir(QFileInfo(fromFile).absolutePath()).relativeFilePath(toFile);
}

QString linkEncode(const QString &rel)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(rel, "/()-_.~"));
}

// Front matter Notion-style for database rows: "Property: value" lines.
QString rowPropertyLines(Workspace &ws, const QString &rowId)
{
    const PageMeta *m = ws.page(rowId);
    if (!m || !m->isRow())
        return {};
    const Schema schema = ws.schema(m->parentId);
    RowRef ref{m, ws.props(rowId)};
    QStringList lines;
    for (const Property &p : schema.props) {
        if (p.type == PropType::Title)
            continue;
        const QString v = displayValue(p, rowValue(p, ref));
        if (!v.isEmpty())
            lines.append(QStringLiteral("%1: %2").arg(p.name, v));
    }
    return lines.isEmpty() ? QString() : lines.join(QLatin1Char('\n')) + QStringLiteral("\n\n");
}
}

Result exportMarkdown(Workspace &ws, const QStringList &pageIds, const QString &directory, bool recursive)
{
    Result r;
    QDir().mkpath(directory);
    const QString mediaDir = QDir(directory).filePath(QStringLiteral("media"));

    // Pass 1: decide every file path, so links between pages can be relative.
    QHash<QString, QString> fileOf; // page id → .md or .csv path
    QHash<QString, QSet<QString>> usedNames; // directory → names taken
    std::function<void(const QString &, const QString &)> plan = [&](const QString &id, const QString &dir) {
        const PageMeta *m = ws.page(id);
        if (!m)
            return;
        QString base = safeFileName(ws.title(id));
        QString name = base;
        for (int n = 2; usedNames[dir].contains(name.toLower()); ++n)
            name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        usedNames[dir].insert(name.toLower());
        const QString ext = m->isDatabase() ? QStringLiteral(".csv") : QStringLiteral(".md");
        fileOf.insert(id, QDir(dir).filePath(name + ext));
        if (!recursive)
            return;
        const QString childDir = QDir(dir).filePath(name);
        if (m->isDatabase())
            for (const PageMeta *row : ws.rows(id))
                plan(row->id, childDir);
        for (const PageMeta *c : ws.children(id))
            plan(c->id, childDir);
    };
    for (const QString &id : pageIds)
        plan(id, directory);

    // Pass 2: write.
    for (auto it = fileOf.cbegin(); it != fileOf.cend() && r.error.isEmpty(); ++it) {
        const QString &id = it.key();
        const QString &file = it.value();
        const PageMeta *m = ws.page(id);
        if (m->isDatabase()) {
            const Schema schema = ws.schema(id);
            QList<QStringList> table;
            QStringList header;
            for (const Property &p : schema.props)
                header.append(p.name);
            table.append(header);
            for (const PageMeta *row : ws.rows(id)) {
                RowRef ref{row, ws.props(row->id)};
                QStringList cells;
                for (const Property &p : schema.props)
                    cells.append(p.type == PropType::Title ? row->title : displayValue(p, rowValue(p, ref)));
                table.append(cells);
            }
            if (writeFile(file, Csv::write(table).toUtf8(), &r.error))
                r.files++;
            continue;
        }
        Markdown::ExportOptions opt;
        opt.pageTitle = [&](const QString &pid) { return ws.title(pid); };
        opt.pageLink = [&](const QString &pid) -> QString {
            const QString target = fileOf.value(pid);
            return target.isEmpty() ? QString() : linkEncode(relativePath(file, target));
        };
        opt.mediaLink = [&](const QString &media) {
            const QString target = QDir(mediaDir).filePath(media);
            if (!QFileInfo::exists(target)) {
                QDir().mkpath(mediaDir);
                if (QFile::copy(ws.mediaPath(media), target))
                    r.files++;
            }
            return relativePath(file, target);
        };
        QString md = QStringLiteral("# ") + ws.title(id) + QStringLiteral("\n\n") + rowPropertyLines(ws, id)
                     + Markdown::fromBlocks(ws.content(id), opt);
        if (!md.endsWith(QLatin1Char('\n')))
            md += QLatin1Char('\n');
        if (writeFile(file, md.toUtf8(), &r.error))
            r.files++;
    }
    r.path = pageIds.size() == 1 ? fileOf.value(pageIds.first()) : directory;
    return r;
}

// ---- HTML ----------------------------------------------------------------------

namespace
{
QString esc(const QString &s)
{
    QString e = s.toHtmlEscaped();
    e.replace(QChar::LineSeparator, QStringLiteral("<br>"));
    return e;
}

struct HtmlCtx {
    Workspace &ws;
    bool print;
    std::function<QString(const QString &)> imageSrc;
    // A rendered image as an <img> source (data URL, or a print resource).
    std::function<QString(const QImage &)> renderedSrc;
    QString pageId;
};

QString imgTag(const HtmlCtx &c, const QImage &img, const QString &cls, double scale = 0.5)
{
    if (img.isNull() || !c.renderedSrc)
        return {};
    // Images are rendered at twice the size for sharpness.
    return QStringLiteral("<img class=\"%1\" src=\"%2\" width=\"%3\" height=\"%4\">")
        .arg(cls, c.renderedSrc(img))
        .arg(qRound(img.width() * scale))
        .arg(qRound(img.height() * scale));
}

QString equationHtml(const HtmlCtx &c, const QString &tex, bool display)
{
    const Renderers &r = renderers();
    if (r.equation) {
        const QImage img = r.equation(tex, display, c.print ? QColor(0x1a, 0x1a, 0x1e) : QColor(0xe6, 0xe6, 0xec));
        if (!img.isNull())
            return imgTag(c, img, display ? QStringLiteral("equation") : QStringLiteral("ieq"));
    }
    return display ? QStringLiteral("<pre class=\"tex\">%1</pre>").arg(tex.toHtmlEscaped()) : QStringLiteral("<code>%1</code>").arg(tex.toHtmlEscaped());
}

QString dateLabel(const QString &iso)
{
    const QLocale en(QLocale::English);
    const QDateTime when = QDateTime::fromString(iso, Qt::ISODate);
    const QDate day = when.isValid() ? when.date() : QDate::fromString(iso.left(10), Qt::ISODate);
    QString label = en.toString(day, QStringLiteral("MMMM d, yyyy"));
    if (iso.size() > 10 && when.isValid())
        label += QLatin1Char(' ') + en.toString(when.time(), QStringLiteral("h:mm AP"));
    return label;
}

QString spansHtml(const HtmlCtx &c, const QList<Span> &spans)
{
    QString out;
    for (const Span &s : spans) {
        if (!s.mention.isEmpty()) {
            out += QStringLiteral("<span class=\"mention\">↗ %1</span>").arg(esc(c.ws.title(s.mention)));
            continue;
        }
        if (!s.equation.isEmpty()) {
            out += equationHtml(c, s.equation, false);
            continue;
        }
        if (!s.date.isEmpty()) {
            out += QStringLiteral("<span class=\"date\">@%1%2</span>").arg(esc(dateLabel(s.date)), s.remind ? QStringLiteral(" ⏰") : QString());
            continue;
        }
        QString t = esc(s.text);
        if (s.code)
            t = QStringLiteral("<code>%1</code>").arg(t);
        if (s.bold)
            t = QStringLiteral("<b>%1</b>").arg(t);
        if (s.italic)
            t = QStringLiteral("<i>%1</i>").arg(t);
        if (s.underline)
            t = QStringLiteral("<u>%1</u>").arg(t);
        if (s.strike)
            t = QStringLiteral("<s>%1</s>").arg(t);
        if (s.highlight)
            t = QStringLiteral("<span class=\"hl%1\">%2</span>").arg(s.highlight).arg(t);
        if (s.color)
            t = QStringLiteral("<span class=\"fg%1\">%2</span>").arg(s.color).arg(t);
        if (!s.link.isEmpty())
            t = QStringLiteral("<a href=\"%1\">%2</a>").arg(s.link.toHtmlEscaped(), t);
        out += t;
    }
    return out;
}

QString blocksHtml(const HtmlCtx &c, const QList<Block> &blocks, int &i, int depth);
QString databaseHtml(const HtmlCtx &c, const QString &id, const View *viewOverride = nullptr);

QString blockHtml(const HtmlCtx &c, const QList<Block> &blocks, int &i)
{
    const Block &b = blocks[i];
    const QString text = spansHtml(c, b.spans);
    ++i;
    // Children: the following deeper blocks.
    QString children;
    if (i < blocks.size() && blocks[i].depth > b.depth)
        children = blocksHtml(c, blocks, i, b.depth + 1);
    QString cls = b.background ? QStringLiteral(" class=\"bg%1\"").arg(b.background)
                               : (b.color ? QStringLiteral(" class=\"fg%1\"").arg(b.color) : QString());

    const QString anchor = b.id.isEmpty() ? QString() : QStringLiteral(" id=\"b-%1\"").arg(b.id);
    auto heading = [&](int level) {
        const QString h = QStringLiteral("<h%1%2%3>%4</h%1>").arg(level).arg(anchor, cls, text);
        if (!b.toggleable)
            return h + children;
        if (c.print)
            return h + QStringLiteral("<div class=\"indent\">%1</div>").arg(children);
        return QStringLiteral("<details open class=\"toggle-heading\"><summary>%1</summary>%2</details>").arg(h, children);
    };
    auto captionHtml = [&]() {
        const QString cap = spansHtml(c, b.caption);
        return cap.isEmpty() ? QString() : QStringLiteral("<p class=\"caption\">%1</p>").arg(cap);
    };
    switch (b.type) {
    case BlockType::Heading1:
        return heading(1);
    case BlockType::Heading2:
        return heading(2);
    case BlockType::Heading3:
        return heading(3);
    case BlockType::Heading4:
        return heading(4);
    case BlockType::Equation:
        return QStringLiteral("<div class=\"equation-block\" align=\"center\">%1</div>%2%3")
            .arg(equationHtml(c, b.data.value(QStringLiteral("expr")).toString(), true), captionHtml(), children);
    case BlockType::TableOfContents: {
        QString items;
        for (const Block &h : blocks)
            if (blockTypeIsHeading(h.type) && !h.plainText().trimmed().isEmpty()) {
                const int level = h.type == BlockType::Heading1 ? 0 : h.type == BlockType::Heading2 ? 1 : 2;
                const QString label = esc(h.plainText());
                items += c.print ? QStringLiteral("<p class=\"toc%1\">%2</p>").arg(level).arg(label)
                                 : QStringLiteral("<a class=\"toc%1\" href=\"#b-%2\">%3</a>").arg(level).arg(h.id, label);
            }
        return QStringLiteral("<nav class=\"toc\">%1</nav>%2").arg(items, children);
    }
    case BlockType::Breadcrumb: {
        QStringList path;
        for (const PageMeta *m = c.ws.page(c.pageId); m; m = m->parentId.isEmpty() ? nullptr : c.ws.page(m->parentId))
            path.prepend(esc(c.ws.title(m->id)));
        return QStringLiteral("<p class=\"breadcrumb\">%1</p>%2").arg(path.join(QStringLiteral(" / ")), children);
    }
    case BlockType::Table: {
        const bool headerRow = b.data.value(QStringLiteral("hr")).toBool();
        const bool headerCol = b.data.value(QStringLiteral("hc")).toBool();
        QString rows;
        for (int r = 0; r < b.rows.size(); ++r) {
            rows += QStringLiteral("<tr>");
            for (int col = 0; col < b.rows[r].size(); ++col) {
                const bool head = (headerRow && r == 0) || (headerCol && col == 0);
                rows += QStringLiteral("<%1>%2</%1>").arg(head ? QStringLiteral("th") : QStringLiteral("td"), spansHtml(c, b.rows[r][col]));
            }
            rows += QStringLiteral("</tr>");
        }
        return QStringLiteral("<table class=\"simple\" cellspacing=\"0\" cellpadding=\"7\">%1</table>%2").arg(rows, children);
    }
    case BlockType::ColumnList: {
        double total = 0;
        for (int k = 0; k < b.columns.size(); ++k)
            total += b.widths.value(k, 1.0);
        QString cols;
        for (int k = 0; k < b.columns.size(); ++k) {
            int j = 0;
            QString inner;
            const QList<Block> &col = b.columns[k];
            while (j < col.size())
                inner += blocksHtml(c, col, j, col[j].depth);
            const double share = total > 0 ? b.widths.value(k, 1.0) / total : 1.0 / b.columns.size();
            cols += c.print ? QStringLiteral("<td valign=\"top\" width=\"%1%\">%2</td>").arg(qRound(share * 100)).arg(inner)
                            : QStringLiteral("<div class=\"column\" style=\"flex:%1\">%2</div>").arg(share, 0, 'f', 3).arg(inner);
        }
        return (c.print ? QStringLiteral("<table class=\"columns\" width=\"100%\" cellpadding=\"6\"><tr>%1</tr></table>")
                        : QStringLiteral("<div class=\"columns\">%1</div>"))
                   .arg(cols)
               + children;
    }
    case BlockType::Button:
        return QStringLiteral("<p><span class=\"button\">%1</span></p>%2")
            .arg(esc(b.data.value(QStringLiteral("label")).toString().isEmpty() ? QStringLiteral("Button") : b.data.value(QStringLiteral("label")).toString()),
                 children);
    case BlockType::Bookmark:
    case BlockType::Embed: {
        const QString title = b.data.value(QStringLiteral("title")).toString();
        const QString desc = b.data.value(QStringLiteral("desc")).toString();
        return QStringLiteral("<a class=\"bookmark\" href=\"%1\"><b>%2</b>%3<br><small>%4</small></a>%5%6")
            .arg(b.url.toHtmlEscaped(), esc(title.isEmpty() ? b.url : title), desc.isEmpty() ? QString() : QStringLiteral("<br>") + esc(desc),
                 esc(b.url), captionHtml(), children);
    }
    case BlockType::Video:
    case BlockType::Audio:
    case BlockType::File:
    case BlockType::Pdf: {
        QString name = b.data.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            name = !b.media.isEmpty() ? b.media : b.url;
        const QString glyph = b.type == BlockType::Video ? QStringLiteral("▶") : b.type == BlockType::Audio ? QStringLiteral("♪") : QStringLiteral("📎");
        return QStringLiteral("<p class=\"file\">%1 %2</p>%3%4").arg(glyph, esc(name), captionHtml(), children);
    }
    case BlockType::Database: {
        if (!c.ws.exists(b.pageId))
            return children;
        View view;
        bool linked = false;
        if (b.data.value(QStringLiteral("linked")).toBool()) {
            QJsonObject o;
            o.insert(QStringLiteral("views"), b.data.value(QStringLiteral("views")));
            const QList<View> vs = Schema::fromJson(o).views;
            if (!vs.isEmpty()) {
                view = vs.first();
                linked = true;
            }
        }
        return QStringLiteral("<h3 class=\"dbtitle\">%1</h3>%2%3")
            .arg(esc(c.ws.title(b.pageId)), databaseHtml(c, b.pageId, linked ? &view : nullptr), children);
    }
    case BlockType::Bullet:
    case BlockType::Numbered:
        return QStringLiteral("<li%1>%2%3</li>").arg(cls, text, children);
    case BlockType::Todo:
        if (c.print) // QTextDocument ignores list-style: none; use plain lines.
            return QStringLiteral("<p style=\"margin-left:%1px\"%2>%3 %4</p>%5")
                .arg(b.depth * 18)
                .arg(b.checked ? QStringLiteral(" class=\"done\"") : QString(), b.checked ? QStringLiteral("☑") : QStringLiteral("☐"),
                     b.checked ? QStringLiteral("<s>%1</s>").arg(text) : text, children);
        return QStringLiteral("<li class=\"todo%1\"><span class=\"box\">%2</span> %3%4</li>")
            .arg(b.checked ? QStringLiteral(" done") : QString(), b.checked ? QStringLiteral("☑") : QStringLiteral("☐"),
                 b.checked ? QStringLiteral("<s>%1</s>").arg(text) : text, children);
    case BlockType::Toggle:
        if (c.print)
            return QStringLiteral("<p%1>▾ %2</p><div class=\"indent\">%3</div>").arg(cls, text, children);
        return QStringLiteral("<details open><summary%1>%2</summary>%3</details>").arg(cls, text, children);
    case BlockType::Quote:
        return QStringLiteral("<blockquote%1>%2</blockquote>%3").arg(cls, text, children);
    case BlockType::Callout: {
        const QString icon = b.icon.startsWith(QLatin1String("glyph:")) || b.icon.isEmpty() ? QStringLiteral("✦") : esc(b.icon);
        if (c.print)
            return QStringLiteral("<table class=\"callout\" width=\"100%\" cellpadding=\"10\"><tr><td width=\"28\" valign=\"top\">%1</td>"
                                  "<td>%2%3</td></tr></table>")
                .arg(icon, text, children);
        return QStringLiteral("<div class=\"callout\"><span class=\"icon\">%1</span><div>%2%3</div></div>").arg(icon, text, children);
    }
    case BlockType::Code: {
        QString code = b.plainText();
        code.replace(QChar::LineSeparator, QLatin1Char('\n'));
        if (b.language == QLatin1String("mermaid") && renderers().diagram) {
            QImage img = renderers().diagram(code, 1400);
            if (!img.isNull()) {
                if (c.print)
                    img.invertPixels(); // light lines on dark become dark on paper
                return QStringLiteral("<div class=\"diagram\" align=\"center\">%1</div>%2%3").arg(imgTag(c, img, QStringLiteral("diagram")), captionHtml(), children);
            }
        }
        return QStringLiteral("<pre><code>%1</code></pre>%2%3").arg(code.toHtmlEscaped(), captionHtml(), children);
    }
    case BlockType::Divider:
        return QStringLiteral("<hr>") + children;
    case BlockType::Image: {
        const QString src = c.imageSrc ? c.imageSrc(b.media) : QString();
        if (src.isEmpty())
            return children;
        const QString width = b.width > 0 ? QStringLiteral(" width=\"%1\"").arg(b.width) : QString();
        const QString align = b.align == QLatin1String("left") ? QStringLiteral("left") : b.align == QLatin1String("right") ? QStringLiteral("right") : QStringLiteral("center");
        return QStringLiteral("<p class=\"image\" align=\"%1\"><img src=\"%2\"%3></p>%4%5").arg(align, src, width, captionHtml(), children);
    }
    case BlockType::PageLink:
        return QStringLiteral("<p class=\"pagelink\">▤ %1</p>%2").arg(esc(c.ws.title(b.pageId)), children);
    default:
        return QStringLiteral("<p%1>%2</p>%3").arg(cls, text.isEmpty() ? QStringLiteral("&nbsp;") : text, children);
    }
}

QString blocksHtml(const HtmlCtx &c, const QList<Block> &blocks, int &i, int depth)
{
    QString out;
    while (i < blocks.size() && blocks[i].depth >= depth) {
        const BlockType t = blocks[i].type;
        if (t == BlockType::Bullet || t == BlockType::Numbered || (t == BlockType::Todo && !c.print)) {
            const QString tag = t == BlockType::Numbered ? QStringLiteral("ol") : QStringLiteral("ul");
            const QString cls = t == BlockType::Todo ? QStringLiteral(" class=\"todos\"") : QString();
            out += QStringLiteral("<%1%2>").arg(tag, cls);
            while (i < blocks.size() && blocks[i].depth == depth && blocks[i].type == t)
                out += blockHtml(c, blocks, i);
            out += QStringLiteral("</%1>").arg(tag);
            continue;
        }
        if (blocks[i].depth > depth) {
            // Orphan deeper block: indent it.
            out += QStringLiteral("<div class=\"indent\">") + blocksHtml(c, blocks, i, blocks[i].depth) + QStringLiteral("</div>");
            continue;
        }
        out += blockHtml(c, blocks, i);
    }
    return out;
}

QString databaseHtml(const HtmlCtx &c, const QString &id, const View *viewOverride)
{
    const Schema schema = c.ws.schema(id);
    if (schema.views.isEmpty() && !viewOverride)
        return {};
    const View &view = viewOverride ? *viewOverride : schema.views.first();
    QList<RowRef> rows;
    for (const PageMeta *m : c.ws.rows(id))
        rows.append({m, c.ws.props(m->id)});
    rows = applyView(schema, view, rows);
    const QList<const Property *> props = schema.orderedProps(view);
    QString out = QStringLiteral("<table class=\"db\" width=\"100%\" cellspacing=\"0\" cellpadding=\"6\"><thead><tr>");
    for (const Property *p : props)
        out += QStringLiteral("<th align=\"left\">%1</th>").arg(esc(p->name));
    out += QStringLiteral("</tr></thead><tbody>");
    for (const RowRef &r : rows) {
        out += QStringLiteral("<tr>");
        for (const Property *p : props) {
            const QString v = p->type == PropType::Title ? c.ws.title(r.meta->id) : displayValue(*p, rowValue(*p, r));
            out += QStringLiteral("<td>%1</td>").arg(esc(v));
        }
        out += QStringLiteral("</tr>");
    }
    out += QStringLiteral("</tbody></table>");
    return out;
}

QString css(bool print)
{
    if (print) {
        return QStringLiteral(
            "body{font-family:'Inter',sans-serif;font-size:11pt;color:#1a1a1e;}"
            "h1.title{font-size:26pt;font-weight:700;margin-bottom:12pt;}"
            "h1{font-size:18pt;font-weight:600;margin-top:16pt;} h2{font-size:15pt;font-weight:600;margin-top:12pt;}"
            "h3{font-size:12.5pt;font-weight:600;margin-top:10pt;}"
            "p{margin-top:3pt;margin-bottom:3pt;line-height:140%;}"
            "blockquote{margin-left:12pt;padding-left:10pt;border-left:3px solid #1a1a1e;color:#3a3a42;}"
            "pre{background-color:#f1f1f5;font-family:'JetBrains Mono',monospace;font-size:9pt;padding:8pt;}"
            "code{font-family:'JetBrains Mono',monospace;background-color:#ececf0;}"
            "table.callout{background-color:#f1f1f5;margin-top:6pt;margin-bottom:6pt;}"
            "table.db th{border-bottom:1px solid #c9c9d0;color:#55555e;} table.db td{border-bottom:1px solid #e4e4ea;}"
            ".mention{text-decoration:underline;} .pagelink{text-decoration:underline;font-weight:500;}"
            ".indent{margin-left:18pt;} .meta{color:#6b6b75;font-size:9pt;}"
            "li.done,p.done{color:#8a8a94;} a{color:#1a1a1e;}"
            ".hl1,.hl2,.hl3,.hl4{background-color:#e4e4ea;} .hl5,.hl6{background-color:#c9c9d0;}"
            ".bg1,.bg2,.bg3,.bg4,.bg5,.bg6{background-color:#ececf0;}"
            ".fg2,.fg3{color:#55555e;} .fg4,.fg5{color:#8a8a94;}"
            "h4{font-size:11.5pt;font-weight:600;margin-top:8pt;}"
            "table.simple td,table.simple th{border:1px solid #c9c9d0;} table.simple th{background-color:#ececf0;text-align:left;}"
            ".caption{color:#6b6b75;font-size:9pt;} .breadcrumb{color:#6b6b75;font-size:9pt;}"
            ".toc1{margin-left:12pt;} .toc2{margin-left:24pt;} .date{text-decoration:underline;}"
            ".button{border:1px solid #c9c9d0;padding:3pt 8pt;} .file{border:1px solid #e4e4ea;padding:4pt;}"
            "a.bookmark{text-decoration:none;color:#1a1a1e;} .dbtitle{margin-top:12pt;}");
    }
    return QStringLiteral(
        ":root{color-scheme:dark;}"
        "body{margin:0;background:#131316;color:#c9c9d0;font:16px/1.6 Inter,system-ui,sans-serif;"
        "background-image:radial-gradient(1px 1px at 20% 30%,#ffffff40,transparent),radial-gradient(1px 1px at 70% 80%,#ffffff30,transparent),"
        "radial-gradient(1.5px 1.5px at 45% 12%,#ffffff50,transparent);background-size:420px 420px;}"
        "main{max-width:720px;margin:0 auto;padding:72px 24px 120px;}"
        "h1.title{font-size:40px;line-height:1.2;color:#f1f1f5;margin:0 0 8px;font-weight:700;letter-spacing:-0.01em;}"
        ".meta{color:#6b6b75;font-size:13px;margin-bottom:32px;}"
        "h1,h2,h3{color:#f1f1f5;font-weight:600;line-height:1.3;} h1{font-size:30px;margin:1.6em 0 .3em;}"
        "h2{font-size:24px;margin:1.3em 0 .3em;} h3{font-size:20px;margin:1em 0 .2em;}"
        "p{margin:.25em 0;} a{color:#f1f1f5;text-decoration-color:#4a4a53;}"
        "blockquote{margin:.5em 0;padding:.1em 0 .1em 16px;border-left:3px solid #f1f1f5;color:#a9a9b2;}"
        "pre{background:#0e0e11;border:1px solid #23232a;border-radius:10px;padding:16px;overflow:auto;"
        "font:13.5px/1.55 'JetBrains Mono',monospace;color:#c9c9d0;}"
        "code{font-family:'JetBrains Mono',monospace;font-size:.88em;background:#23232a;padding:.15em .35em;border-radius:5px;}"
        "pre code{background:none;padding:0;}"
        ".callout{display:flex;gap:12px;background:#1a1a1e;border:1px solid #2e2e35;border-radius:12px;padding:14px 16px;margin:.6em 0;}"
        ".callout .icon{font-size:1.2em;}"
        "hr{border:0;border-top:1px solid #2e2e35;margin:1.2em 0;}"
        "ul,ol{padding-left:1.6em;margin:.2em 0;} ul.todos{list-style:none;padding-left:.2em;} li.done{color:#6b6b75;}"
        "details summary{cursor:pointer;} details>*:not(summary){margin-left:1.4em;}"
        ".mention,.pagelink{color:#f1f1f5;border-bottom:1px solid #4a4a53;}"
        ".image img{max-width:100%;border-radius:10px;}"
        ".indent{margin-left:1.6em;}"
        "table.db{border-collapse:collapse;font-size:14px;} table.db th{color:#8a8a94;font-weight:500;border-bottom:1px solid #2e2e35;}"
        "table.db td{border-bottom:1px solid #1e1e23;}"
        ".fg1{color:#f1f1f5;} .fg2{color:#a9a9b2;} .fg3{color:#8a8a94;} .fg4{color:#6b6b75;} .fg5{color:#4a4a53;}"
        ".hl1{background:#23232a;} .hl2{background:#2e2e35;} .hl3{background:#3a3a42;} .hl4{background:#55555e;color:#f1f1f5;}"
        ".hl5{background:#c9c9d0;color:#131316;} .hl6{background:#f1f1f5;color:#09090b;}"
        ".bg1{background:#23232a;} .bg2{background:#2e2e35;} .bg3{background:#3a3a42;} .bg4{background:#55555e;}"
        ".bg5{background:#c9c9d0;color:#131316;} .bg6{background:#f1f1f5;color:#09090b;}"
        "[class^=bg]{border-radius:6px;padding:2px 6px;}"
        "h4{font-size:17px;margin:.9em 0 .2em;color:#f1f1f5;font-weight:600;}"
        ".toggle-heading>summary>h1,.toggle-heading>summary>h2,.toggle-heading>summary>h3,.toggle-heading>summary>h4{display:inline;}"
        ".columns{display:flex;gap:46px;margin:.4em 0;} .column{min-width:0;}"
        "table.simple{border-collapse:collapse;margin:.6em 0;font-size:15px;} table.simple td,table.simple th{border:1px solid #2e2e35;padding:7px 9px;text-align:left;}"
        "table.simple th{background:#222229;color:#f1f1f5;font-weight:600;}"
        ".equation-block{margin:.8em 0;text-align:center;} img.ieq{vertical-align:middle;} .caption{color:#8a8a94;font-size:13px;margin-top:.2em;}"
        "nav.toc{display:flex;flex-direction:column;margin:.6em 0;} nav.toc a{color:#a9a9b2;text-decoration:none;padding:2px 0;border-bottom:1px solid #1e1e23;}"
        ".toc1{padding-left:18px!important;} .toc2{padding-left:36px!important;}"
        ".breadcrumb{color:#8a8a94;font-size:13px;} .date{color:#f1f1f5;border-bottom:1px solid #4a4a53;}"
        ".button{display:inline-block;border:1px solid #2e2e35;background:#1a1a1e;border-radius:7px;padding:4px 12px;font-size:14px;}"
        "a.bookmark{display:block;border:1px solid #2e2e35;border-radius:10px;padding:12px 14px;margin:.5em 0;text-decoration:none;color:#c9c9d0;}"
        "a.bookmark small{color:#6b6b75;} .file{border:1px solid #2e2e35;border-radius:8px;padding:8px 12px;}"
        ".diagram img{max-width:100%;} pre.tex{text-align:center;}");
}
}

QString pageHtml(Workspace &ws, const QString &pageId, bool forPrint, QHash<QString, QImage> *resources)
{
    const PageMeta *m = ws.page(pageId);
    if (!m)
        return {};
    HtmlCtx ctx{ws, forPrint, nullptr, nullptr, pageId};
    if (forPrint && resources) {
        ctx.renderedSrc = [resources](const QImage &img) {
            const QString name = QStringLiteral("nebula-res:%1").arg(resources->size());
            resources->insert(name, img);
            return name;
        };
    } else if (!forPrint) {
        ctx.renderedSrc = [](const QImage &img) {
            QByteArray png;
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
        };
    }
    if (forPrint) {
        ctx.imageSrc = [](const QString &media) { return QStringLiteral("nebula-media:") + media; };
    } else {
        ctx.imageSrc = [&ws](const QString &media) -> QString {
            QFile f(ws.mediaPath(media));
            if (!f.open(QIODevice::ReadOnly))
                return {};
            const QString mime = QMimeDatabase().mimeTypeForFile(ws.mediaPath(media)).name();
            return QStringLiteral("data:%1;base64,%2").arg(mime, QString::fromLatin1(f.readAll().toBase64()));
        };
    }
    QString body;
    QString icon = m->icon.startsWith(QLatin1String("glyph:")) ? QString() : m->icon;
    body += QStringLiteral("<h1 class=\"title\">%1%2</h1>")
                .arg(icon.isEmpty() ? QString() : esc(icon) + QLatin1Char(' '), esc(ws.title(pageId)));
    if (m->isRow()) {
        const Schema schema = ws.schema(m->parentId);
        RowRef ref{m, ws.props(pageId)};
        QString props;
        for (const Property &p : schema.props) {
            if (p.type == PropType::Title)
                continue;
            const QString v = displayValue(p, rowValue(p, ref));
            if (!v.isEmpty())
                props += QStringLiteral("<tr><td class=\"meta\" width=\"140\">%1</td><td>%2</td></tr>").arg(esc(p.name), esc(v));
        }
        if (!props.isEmpty())
            body += QStringLiteral("<table cellpadding=\"3\">%1</table><hr>").arg(props);
    }
    if (m->isDatabase()) {
        body += databaseHtml(ctx, pageId);
    } else {
        const QList<Block> blocks = ws.content(pageId);
        int i = 0;
        while (i < blocks.size())
            body += blocksHtml(ctx, blocks, i, blocks[i].depth);
    }
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title><style>%2</style></head>"
                          "<body><main>%3</main></body></html>")
        .arg(esc(ws.title(pageId)), css(forPrint), body);
}

Result exportHtml(Workspace &ws, const QString &pageId, const QString &filePath)
{
    Result r;
    r.path = filePath;
    if (writeFile(filePath, pageHtml(ws, pageId, false).toUtf8(), &r.error))
        r.files = 1;
    return r;
}

Result exportPdf(Workspace &ws, const QString &pageId, const QString &filePath)
{
    Result r;
    r.path = filePath;
    QTextDocument doc;
    doc.setDocumentMargin(0);
    QFont f(QStringLiteral("Inter"));
    f.setPointSizeF(11);
    doc.setDefaultFont(f);
    const QList<Block> blocks = ws.content(pageId);
    for (const QString &media : blocksMedia(blocks)) {
        QImage img(ws.mediaPath(media));
        if (!img.isNull()) {
            // Fit the printable width (A4 minus margins ≈ 680 px at 96 dpi).
            if (img.width() > 680)
                img = img.scaledToWidth(680, Qt::SmoothTransformation);
            doc.addResource(QTextDocument::ImageResource, QUrl(QStringLiteral("nebula-media:") + media), img);
        }
    }
    QHash<QString, QImage> rendered;
    const QString html = pageHtml(ws, pageId, true, &rendered);
    for (auto it = rendered.cbegin(); it != rendered.cend(); ++it) {
        // Rendered at twice the size: the HTML gives the display size.
        doc.addResource(QTextDocument::ImageResource, QUrl(it.key()), it.value());
    }
    doc.setHtml(html);
    QPdfWriter writer(filePath);
    writer.setTitle(ws.title(pageId));
    writer.setCreator(QStringLiteral("Nebula Note"));
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(18, 18, 18, 18), QPageLayout::Millimeter);
    writer.setResolution(96);
    doc.setPageSize(writer.pageLayout().paintRectPixels(writer.resolution()).size());
    doc.print(&writer);
    if (!QFileInfo::exists(filePath))
        r.error = tr("The PDF could not be written.");
    else
        r.files = 1;
    return r;
}

}
