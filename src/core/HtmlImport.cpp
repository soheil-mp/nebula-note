#include "HtmlImport.h"

#include "Zip.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUrl>
#include <QXmlStreamReader>

#include <memory>
#include <vector>

namespace
{
// ---- a lenient HTML tree -----------------------------------------------------------

struct Node {
    QString tag; // lower case; empty for text
    QString text;
    QHash<QString, QString> attrs;
    QList<Node *> children;
    Node *parent = nullptr;

    bool isText() const { return tag.isEmpty(); }
    QString attr(const QString &name) const { return attrs.value(name); }
    bool hasClass(const QString &c) const
    {
        return attrs.value(QStringLiteral("class")).split(QLatin1Char(' '), Qt::SkipEmptyParts).contains(c);
    }
    QString classStartingWith(const QString &prefix) const
    {
        for (const QString &c : attrs.value(QStringLiteral("class")).split(QLatin1Char(' '), Qt::SkipEmptyParts))
            if (c.startsWith(prefix))
                return c;
        return {};
    }
};

class Dom
{
public:
    Node *make()
    {
        m_pool.push_back(std::make_unique<Node>());
        return m_pool.back().get();
    }

private:
    std::vector<std::unique_ptr<Node>> m_pool;
};

const QSet<QString> &voidTags()
{
    static const QSet<QString> s{QStringLiteral("br"),   QStringLiteral("img"),  QStringLiteral("hr"),    QStringLiteral("input"),
                                 QStringLiteral("meta"), QStringLiteral("link"), QStringLiteral("col"),   QStringLiteral("source"),
                                 QStringLiteral("area"), QStringLiteral("wbr"),  QStringLiteral("base"),  QStringLiteral("en-media"),
                                 QStringLiteral("en-todo"), QStringLiteral("embed"), QStringLiteral("param"), QStringLiteral("track")};
    return s;
}

const QSet<QString> &blockTags()
{
    static const QSet<QString> s{
        QStringLiteral("#root"),   QStringLiteral("html"),     QStringLiteral("body"),    QStringLiteral("p"),        QStringLiteral("div"),
        QStringLiteral("h1"),      QStringLiteral("h2"),       QStringLiteral("h3"),      QStringLiteral("h4"),       QStringLiteral("h5"),
        QStringLiteral("h6"),      QStringLiteral("ul"),       QStringLiteral("ol"),      QStringLiteral("li"),       QStringLiteral("blockquote"),
        QStringLiteral("pre"),     QStringLiteral("hr"),       QStringLiteral("table"),   QStringLiteral("figure"),   QStringLiteral("details"),
        QStringLiteral("summary"), QStringLiteral("aside"),    QStringLiteral("nav"),     QStringLiteral("header"),   QStringLiteral("footer"),
        QStringLiteral("article"), QStringLiteral("section"),  QStringLiteral("main"),    QStringLiteral("figcaption"), QStringLiteral("dl"),
        QStringLiteral("dt"),      QStringLiteral("dd"),       QStringLiteral("form"),    QStringLiteral("fieldset"), QStringLiteral("address"),
        QStringLiteral("center"),  QStringLiteral("tbody"),    QStringLiteral("thead"),   QStringLiteral("tfoot"),    QStringLiteral("tr"),
        QStringLiteral("td"),      QStringLiteral("th"),       QStringLiteral("en-note"), QStringLiteral("head"),     QStringLiteral("script"),
        QStringLiteral("style"),   QStringLiteral("title"),    QStringLiteral("img"),     QStringLiteral("en-media"), QStringLiteral("caption")};
    return s;
}

QString decodeEntities(const QString &s)
{
    if (!s.contains(QLatin1Char('&')))
        return s;
    static const QHash<QString, QString> named = {
        {QStringLiteral("amp"), QStringLiteral("&")},     {QStringLiteral("lt"), QStringLiteral("<")},       {QStringLiteral("gt"), QStringLiteral(">")},
        {QStringLiteral("quot"), QStringLiteral("\"")},   {QStringLiteral("apos"), QStringLiteral("'")},     {QStringLiteral("nbsp"), QString(QChar(0xA0))},
        {QStringLiteral("mdash"), QString(QChar(0x2014))}, {QStringLiteral("ndash"), QString(QChar(0x2013))}, {QStringLiteral("hellip"), QString(QChar(0x2026))},
        {QStringLiteral("rsquo"), QString(QChar(0x2019))}, {QStringLiteral("lsquo"), QString(QChar(0x2018))}, {QStringLiteral("ldquo"), QString(QChar(0x201C))},
        {QStringLiteral("rdquo"), QString(QChar(0x201D))}, {QStringLiteral("copy"), QString(QChar(0xA9))},    {QStringLiteral("reg"), QString(QChar(0xAE))},
        {QStringLiteral("trade"), QString(QChar(0x2122))}, {QStringLiteral("bull"), QString(QChar(0x2022))},  {QStringLiteral("middot"), QString(QChar(0xB7))},
        {QStringLiteral("times"), QString(QChar(0xD7))},   {QStringLiteral("rarr"), QString(QChar(0x2192))},  {QStringLiteral("larr"), QString(QChar(0x2190))},
        {QStringLiteral("deg"), QString(QChar(0xB0))},     {QStringLiteral("euro"), QString(QChar(0x20AC))},
    };
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        if (s[i] != QLatin1Char('&')) {
            out += s[i];
            continue;
        }
        const int semi = s.indexOf(QLatin1Char(';'), i + 1);
        if (semi < 0 || semi - i > 10) {
            out += s[i];
            continue;
        }
        const QString name = s.mid(i + 1, semi - i - 1);
        if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint code = name.startsWith(QLatin1String("#x"), Qt::CaseInsensitive) ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
            if (ok && code > 0) {
                const char32_t c = code;
                out += QString::fromUcs4(&c, 1);
                i = semi;
                continue;
            }
        } else if (named.contains(name)) {
            out += named.value(name);
            i = semi;
            continue;
        }
        out += s[i];
    }
    return out;
}

Node *parseHtml(const QString &html, Dom &dom, QString *docTitle)
{
    Node *root = dom.make();
    root->tag = QStringLiteral("#root");
    QList<Node *> stack{root};
    const int n = int(html.size());
    int i = 0;
    auto top = [&] { return stack.last(); };
    auto popTo = [&](const QString &tag, const QStringList &stopAt = {}) {
        // Closes the nearest open `tag` unless a stop tag comes first.
        for (int k = int(stack.size()) - 1; k > 0; --k) {
            if (stopAt.contains(stack[k]->tag))
                return false;
            if (stack[k]->tag == tag) {
                while (stack.size() > k)
                    stack.removeLast();
                return true;
            }
        }
        return false;
    };
    static const QSet<QString> closesP{QStringLiteral("p"),     QStringLiteral("div"),    QStringLiteral("ul"),      QStringLiteral("ol"),
                                       QStringLiteral("table"), QStringLiteral("h1"),     QStringLiteral("h2"),      QStringLiteral("h3"),
                                       QStringLiteral("h4"),    QStringLiteral("h5"),     QStringLiteral("h6"),      QStringLiteral("pre"),
                                       QStringLiteral("blockquote"), QStringLiteral("hr"), QStringLiteral("figure"), QStringLiteral("details"),
                                       QStringLiteral("aside"), QStringLiteral("section"), QStringLiteral("article"), QStringLiteral("nav"),
                                       QStringLiteral("header"), QStringLiteral("footer"), QStringLiteral("dl")};
    while (i < n) {
        if (html[i] == QLatin1Char('<')) {
            if (html.mid(i, 4) == QLatin1String("<!--")) {
                const int end = html.indexOf(QLatin1String("-->"), i + 4);
                i = end < 0 ? n : end + 3;
                continue;
            }
            if (html.mid(i, 9).compare(QLatin1String("<![CDATA["), Qt::CaseInsensitive) == 0) {
                const int end = html.indexOf(QLatin1String("]]>"), i + 9);
                Node *t = dom.make();
                t->text = html.mid(i + 9, (end < 0 ? n : end) - i - 9);
                t->parent = top();
                top()->children.append(t);
                i = end < 0 ? n : end + 3;
                continue;
            }
            if (i + 1 < n && (html[i + 1] == QLatin1Char('!') || html[i + 1] == QLatin1Char('?'))) {
                const int end = html.indexOf(QLatin1Char('>'), i);
                i = end < 0 ? n : end + 1;
                continue;
            }
            if (i + 1 < n && html[i + 1] == QLatin1Char('/')) {
                int j = i + 2;
                while (j < n && (html[j].isLetterOrNumber() || html[j] == QLatin1Char('-') || html[j] == QLatin1Char(':')))
                    ++j;
                const QString name = html.mid(i + 2, j - i - 2).toLower();
                const int end = html.indexOf(QLatin1Char('>'), j);
                i = end < 0 ? n : end + 1;
                popTo(name);
                continue;
            }
            if (i + 1 < n && html[i + 1].isLetter()) {
                int j = i + 1;
                while (j < n && (html[j].isLetterOrNumber() || html[j] == QLatin1Char('-') || html[j] == QLatin1Char(':')))
                    ++j;
                const QString name = html.mid(i + 1, j - i - 1).toLower();
                Node *el = dom.make();
                el->tag = name;
                // Attributes.
                bool selfClosing = false;
                while (j < n && html[j] != QLatin1Char('>')) {
                    if (html[j].isSpace()) {
                        ++j;
                        continue;
                    }
                    if (html[j] == QLatin1Char('/')) {
                        selfClosing = true;
                        ++j;
                        continue;
                    }
                    int k = j;
                    while (k < n && !html[k].isSpace() && html[k] != QLatin1Char('=') && html[k] != QLatin1Char('>') && html[k] != QLatin1Char('/'))
                        ++k;
                    const QString an = html.mid(j, k - j).toLower();
                    QString av;
                    j = k;
                    while (j < n && html[j].isSpace())
                        ++j;
                    if (j < n && html[j] == QLatin1Char('=')) {
                        ++j;
                        while (j < n && html[j].isSpace())
                            ++j;
                        if (j < n && (html[j] == QLatin1Char('"') || html[j] == QLatin1Char('\''))) {
                            const QChar q = html[j];
                            const int e = html.indexOf(q, j + 1);
                            av = html.mid(j + 1, (e < 0 ? n : e) - j - 1);
                            j = e < 0 ? n : e + 1;
                        } else {
                            int e = j;
                            while (e < n && !html[e].isSpace() && html[e] != QLatin1Char('>'))
                                ++e;
                            av = html.mid(j, e - j);
                            j = e;
                        }
                    }
                    if (!an.isEmpty())
                        el->attrs.insert(an, decodeEntities(av));
                }
                i = j + 1;
                // Implied ends.
                if (closesP.contains(name) && top()->tag == QLatin1String("p"))
                    stack.removeLast();
                if (name == QLatin1String("li"))
                    popTo(QStringLiteral("li"), {QStringLiteral("ul"), QStringLiteral("ol")});
                else if (name == QLatin1String("tr"))
                    popTo(QStringLiteral("tr"), {QStringLiteral("table")});
                else if (name == QLatin1String("td") || name == QLatin1String("th")) {
                    if (!popTo(QStringLiteral("td"), {QStringLiteral("tr"), QStringLiteral("table")}))
                        popTo(QStringLiteral("th"), {QStringLiteral("tr"), QStringLiteral("table")});
                } else if (name == QLatin1String("dt") || name == QLatin1String("dd")) {
                    if (!popTo(QStringLiteral("dt"), {QStringLiteral("dl")}))
                        popTo(QStringLiteral("dd"), {QStringLiteral("dl")});
                }
                el->parent = top();
                top()->children.append(el);
                // Raw text elements.
                if (name == QLatin1String("script") || name == QLatin1String("style") || name == QLatin1String("title")
                    || name == QLatin1String("textarea")) {
                    const int end = html.indexOf(QStringLiteral("</") + name, i, Qt::CaseInsensitive);
                    el->text = html.mid(i, (end < 0 ? n : end) - i);
                    if (name == QLatin1String("title") && docTitle && docTitle->isEmpty())
                        *docTitle = decodeEntities(el->text).simplified();
                    const int close = end < 0 ? n : html.indexOf(QLatin1Char('>'), end);
                    i = close < 0 ? n : close + 1;
                    continue;
                }
                if (!selfClosing && !voidTags().contains(name))
                    stack.append(el);
                continue;
            }
        }
        // Text up to the next tag.
        int end = html.indexOf(QLatin1Char('<'), i + 1);
        if (end < 0)
            end = n;
        Node *t = dom.make();
        t->text = decodeEntities(html.mid(i, end - i));
        t->parent = top();
        top()->children.append(t);
        i = end;
    }
    return root;
}

QString textContent(const Node *n, bool keepBreaks = false)
{
    if (n->isText())
        return n->text;
    if (n->tag == QLatin1String("br"))
        return keepBreaks ? QStringLiteral("\n") : QStringLiteral(" ");
    if (n->tag == QLatin1String("script") || n->tag == QLatin1String("style"))
        return {};
    QString out;
    for (const Node *c : n->children)
        out += textContent(c, keepBreaks);
    return out;
}

const Node *findDescendant(const Node *n, const std::function<bool(const Node *)> &pred)
{
    for (const Node *c : n->children) {
        if (!c->isText() && pred(c))
            return c;
        if (const Node *d = findDescendant(c, pred))
            return d;
    }
    return nullptr;
}

// ---- tree → blocks ---------------------------------------------------------------

class Converter
{
public:
    explicit Converter(const Markdown::ImportOptions &opt)
        : m_opt(opt)
    {
    }

    QList<Block> run(const Node *root)
    {
        blocks(root, 0);
        return m_out;
    }
    QString title;
    HtmlImport::PageInfo info;

private:
    struct Style {
        bool bold = false, italic = false, underline = false, strike = false, code = false;
        QString link;
        int color = 0, highlight = 0;
    };

    static bool isBlock(const Node *n) { return !n->isText() && blockTags().contains(n->tag); }

    Style styleFor(const Node *n, Style st) const
    {
        const QString t = n->tag;
        if (t == QLatin1String("b") || t == QLatin1String("strong"))
            st.bold = true;
        else if (t == QLatin1String("i") || t == QLatin1String("em") || t == QLatin1String("cite"))
            st.italic = true;
        else if (t == QLatin1String("u") || t == QLatin1String("ins"))
            st.underline = true;
        else if (t == QLatin1String("s") || t == QLatin1String("del") || t == QLatin1String("strike"))
            st.strike = true;
        else if (t == QLatin1String("code") || t == QLatin1String("kbd") || t == QLatin1String("samp") || t == QLatin1String("tt"))
            st.code = true;
        else if (t == QLatin1String("mark") && n->classStartingWith(QStringLiteral("highlight-")).isEmpty())
            st.highlight = qMax(st.highlight, 2); // Notion's marks carry a colour class instead
        else if (t == QLatin1String("a") && !n->attr(QStringLiteral("href")).isEmpty())
            st.link = n->attr(QStringLiteral("href"));
        const QString css = n->attr(QStringLiteral("style")).toLower();
        if (css.contains(QLatin1String("font-weight:bold")) || css.contains(QLatin1String("font-weight: bold"))
            || css.contains(QLatin1String("font-weight:600")) || css.contains(QLatin1String("font-weight:700"))
            || css.contains(QLatin1String("font-weight: 600")) || css.contains(QLatin1String("font-weight: 700")))
            st.bold = true;
        if (css.contains(QLatin1String("font-style:italic")) || css.contains(QLatin1String("font-style: italic")))
            st.italic = true;
        if (css.contains(QLatin1String("underline")))
            st.underline = true;
        if (css.contains(QLatin1String("line-through")))
            st.strike = true;
        // Notion's colour classes: highlight-red, highlight-red_background.
        const QString hl = n->classStartingWith(QStringLiteral("highlight-"));
        if (!hl.isEmpty()) {
            QString name = hl.mid(10);
            if (name.endsWith(QLatin1String("_background")))
                st.highlight = shadeForNotionColor(name.chopped(11), true);
            else
                st.color = shadeForNotionColor(name, false);
        }
        return st;
    }

    void addText(QList<Span> &spans, const QString &text, const Style &st)
    {
        if (text.isEmpty())
            return;
        Span s;
        s.text = text;
        s.bold = st.bold;
        s.italic = st.italic;
        s.underline = st.underline;
        s.strike = st.strike;
        s.code = st.code;
        s.link = st.link;
        s.color = st.color;
        s.highlight = st.highlight;
        spans.append(s);
    }

    QString equationIn(const Node *n) const
    {
        if (const Node *a = findDescendant(n, [](const Node *x) {
                return x->tag == QLatin1String("annotation") && x->attr(QStringLiteral("encoding")).contains(QLatin1String("tex"));
            }))
            return textContent(a).trimmed();
        return {};
    }

    void inlineSpans(const Node *n, Style st, QList<Span> &spans, bool pre = false)
    {
        if (n->isText()) {
            QString t = n->text;
            if (!pre) {
                static const QRegularExpression ws(QStringLiteral("[ \\t\\r\\n\\f]+"));
                t.replace(ws, QStringLiteral(" "));
            }
            t.replace(QChar(0xA0), QLatin1Char(' '));
            addText(spans, t, st);
            return;
        }
        const QString tag = n->tag;
        if (tag == QLatin1String("br")) {
            addText(spans, QString(QChar::LineSeparator), st);
            return;
        }
        if (tag == QLatin1String("script") || tag == QLatin1String("style") || tag == QLatin1String("input") || tag == QLatin1String("en-todo")
            || tag == QLatin1String("head") || tag == QLatin1String("title"))
            return;
        if (tag == QLatin1String("img") || tag == QLatin1String("en-media")) {
            m_pendingImages.append(n);
            return;
        }
        // Inline equations (KaTeX markup, Notion's equation tokens).
        if (tag == QLatin1String("math") || n->hasClass(QStringLiteral("notion-text-equation-token")) || n->hasClass(QStringLiteral("katex"))) {
            const QString tex = equationIn(n);
            if (!tex.isEmpty()) {
                Span s;
                s.text = QString(QChar::ObjectReplacementCharacter);
                s.equation = tex;
                spans.append(s);
                return;
            }
        }
        if (tag == QLatin1String("time") && !n->attr(QStringLiteral("datetime")).isEmpty()) {
            Span s;
            s.text = QString(QChar::ObjectReplacementCharacter);
            s.date = n->attr(QStringLiteral("datetime"));
            s.remind = n->attrs.contains(QStringLiteral("data-remind"));
            spans.append(s);
            return;
        }
        if (tag == QLatin1String("a")) {
            const QString href = n->attr(QStringLiteral("href"));
            const QString page = m_opt.resolvePage && !href.isEmpty() ? m_opt.resolvePage(href) : QString();
            if (!page.isEmpty()) {
                Span s;
                s.text = QString(QChar::ObjectReplacementCharacter);
                s.mention = page;
                spans.append(s);
                return;
            }
        }
        const Style inner = styleFor(n, st);
        for (const Node *c : n->children)
            inlineSpans(c, inner, spans, pre || tag == QLatin1String("pre"));
    }

    static QList<Span> trimmed(QList<Span> spans)
    {
        while (!spans.isEmpty()) {
            QString &t = spans.first().text;
            int k = 0;
            while (k < t.size() && (t[k] == QLatin1Char(' ') || t[k] == QChar::LineSeparator))
                ++k;
            t = t.mid(k);
            if (t.isEmpty() && !spans.first().isObject())
                spans.removeFirst();
            else
                break;
        }
        while (!spans.isEmpty()) {
            QString &t = spans.last().text;
            int k = int(t.size());
            while (k > 0 && (t[k - 1] == QLatin1Char(' ') || t[k - 1] == QChar::LineSeparator))
                --k;
            t.truncate(k);
            if (t.isEmpty() && !spans.last().isObject())
                spans.removeLast();
            else
                break;
        }
        return normalizeSpans(spans);
    }

    QList<Span> inlineOf(const Node *n)
    {
        QList<Span> spans;
        for (const Node *c : n->children)
            inlineSpans(c, styleFor(n, Style{}), spans);
        return trimmed(spans);
    }

    void flushImages(int depth)
    {
        const QList<const Node *> images = m_pendingImages;
        m_pendingImages.clear();
        for (const Node *img : images)
            imageBlock(img, depth, {});
    }

    // Appends a block, turning links to attachments into file blocks (copied
    // into the workspace); a paragraph that only links one becomes that block.
    void push(Block b)
    {
        QList<Block> files = Markdown::extractAttachments(b.spans, m_opt, b.depth + (blockTypeIsList(b.type) ? 1 : 0));
        if (b.type == BlockType::Paragraph && files.size() == 1) {
            bool other = false;
            for (const Span &sp : b.spans)
                if (!sp.text.trimmed().isEmpty() && sp.text.trimmed() != files.first().data.value(QStringLiteral("name")).toString())
                    other = true;
            if (!other) {
                files.first().depth = b.depth;
                m_out.append(files.first());
                return;
            }
        }
        m_out.append(b);
        m_out.append(files);
    }

    void addParagraph(QList<Span> spans, int depth)
    {
        spans = trimmed(spans);
        if (spans.isEmpty())
            return;
        Block b;
        b.type = BlockType::Paragraph;
        b.depth = depth;
        b.spans = spans;
        if (b.spans.size() == 1 && !b.spans[0].mention.isEmpty()) {
            b.type = BlockType::PageLink;
            b.pageId = b.spans[0].mention;
            b.spans.clear();
        }
        push(b);
    }

    void imageBlock(const Node *img, int depth, const QList<Span> &caption)
    {
        QString src = img->tag == QLatin1String("en-media") ? QStringLiteral("enres:") + img->attr(QStringLiteral("hash"))
                                                            : img->attr(QStringLiteral("src"));
        if (src.isEmpty())
            return;
        const QString media = m_opt.importImage ? m_opt.importImage(src) : QString();
        if (media.isEmpty()) {
            const QString alt = img->attr(QStringLiteral("alt"));
            if (!alt.isEmpty())
                m_out.append(Block::make(BlockType::Paragraph, alt, depth));
            return;
        }
        Block b;
        b.type = BlockType::Image;
        b.depth = depth;
        b.media = media;
        b.caption = caption;
        if (b.caption.isEmpty() && !img->attr(QStringLiteral("alt")).isEmpty() && img->tag == QLatin1String("img"))
            b.caption = {};
        m_out.append(b);
    }

    // Children of `n` as blocks: runs of inline content become paragraphs.
    void blocks(const Node *n, int depth, const Node *skip = nullptr)
    {
        QList<Span> para;
        auto flush = [&] {
            addParagraph(para, depth);
            para.clear();
            flushImages(depth);
        };
        for (const Node *c : n->children) {
            if (c == skip)
                continue;
            if (!isBlock(c)) {
                inlineSpans(c, Style{}, para);
                continue;
            }
            flush();
            element(c, depth);
        }
        flush();
    }

    void quoteLike(const Node *n, int depth, BlockType type, const QString &icon = {})
    {
        // The leading inline content is the block's text; nested blocks are children.
        QList<Span> spans;
        int i = 0;
        const QList<Node *> &kids = n->children;
        for (; i < kids.size() && !isBlock(kids[i]); ++i)
            inlineSpans(kids[i], Style{}, spans);
        spans = trimmed(spans);
        if (spans.isEmpty() && i < kids.size() && (kids[i]->tag == QLatin1String("p") || kids[i]->tag == QLatin1String("div"))
            && !findDescendant(kids[i], [](const Node *x) { return isBlock(x); })) {
            spans = inlineOf(kids[i]);
            ++i;
        }
        Block b;
        b.type = type;
        b.depth = depth;
        b.spans = spans;
        b.icon = icon;
        m_out.append(b);
        flushImages(depth + 1);
        for (; i < kids.size(); ++i) {
            if (isBlock(kids[i]))
                element(kids[i], depth + 1);
            else {
                QList<Span> more;
                inlineSpans(kids[i], Style{}, more);
                addParagraph(more, depth + 1);
            }
        }
    }

    void listItems(const Node *list, int depth, bool ordered)
    {
        const bool todoList = list->hasClass(QStringLiteral("to-do-list"));
        for (const Node *li : list->children) {
            if (li->isText())
                continue;
            if (li->tag != QLatin1String("li")) {
                if (isBlock(li))
                    element(li, depth);
                continue;
            }
            // Toggles: <li><details>…</details></li>.
            const Node *details = nullptr;
            for (const Node *c : li->children)
                if (c->tag == QLatin1String("details"))
                    details = c;
            if (details) {
                element(details, depth);
                continue;
            }
            bool todo = todoList, checked = false;
            // The item's own checkbox (not one of its sub-items').
            const Node *box = nullptr;
            for (const Node *k : li->children)
                if ((k->tag == QLatin1String("input") && k->attr(QStringLiteral("type")) == QLatin1String("checkbox"))
                    || k->hasClass(QStringLiteral("checkbox")) || k->tag == QLatin1String("en-todo"))
                    box = k;
            if (box) {
                todo = true;
                checked = (box->attrs.contains(QStringLiteral("checked")) && box->attr(QStringLiteral("checked")) != QLatin1String("false"))
                          || box->hasClass(QStringLiteral("checkbox-on"));
            }
            if (li->hasClass(QStringLiteral("task-list-item")))
                todo = true;
            Block b;
            b.type = todo ? BlockType::Todo : ordered ? BlockType::Numbered : BlockType::Bullet;
            b.depth = depth;
            b.checked = checked;
            int i = 0;
            // Checkbox markers aren't content.
            QList<Node *> kids;
            for (Node *k : li->children)
                if (!(k->tag == QLatin1String("input") || k->hasClass(QStringLiteral("checkbox")) || k->tag == QLatin1String("en-todo")))
                    kids.append(k);
            QList<Span> spans;
            for (; i < kids.size() && !isBlock(kids[i]); ++i)
                inlineSpans(kids[i], Style{}, spans);
            spans = trimmed(spans);
            if (spans.isEmpty() && i < kids.size() && (kids[i]->tag == QLatin1String("p") || kids[i]->tag == QLatin1String("div"))
                && !findDescendant(kids[i], [](const Node *x) { return isBlock(x) && x->tag != QLatin1String("img"); })) {
                spans = inlineOf(kids[i]);
                ++i;
            }
            b.spans = spans;
            push(b);
            flushImages(depth + 1);
            for (; i < kids.size(); ++i) {
                if (isBlock(kids[i]))
                    element(kids[i], depth + 1);
                else {
                    QList<Span> more;
                    inlineSpans(kids[i], Style{}, more);
                    addParagraph(more, depth + 1);
                }
            }
        }
    }

    void table(const Node *t, int depth)
    {
        Block b;
        b.type = BlockType::Table;
        b.depth = depth;
        QList<const Node *> rows;
        std::function<void(const Node *)> collect = [&](const Node *n) {
            for (const Node *c : n->children) {
                if (c->tag == QLatin1String("tr"))
                    rows.append(c);
                else if (c->tag == QLatin1String("thead") || c->tag == QLatin1String("tbody") || c->tag == QLatin1String("tfoot"))
                    collect(c);
            }
        };
        collect(t);
        bool headerRow = false, headerCol = !rows.isEmpty();
        int cols = 0;
        for (int r = 0; r < rows.size(); ++r) {
            QList<Cell> row;
            bool allTh = true;
            int col = 0;
            for (const Node *cell : rows[r]->children) {
                if (cell->tag != QLatin1String("td") && cell->tag != QLatin1String("th"))
                    continue;
                if (cell->tag != QLatin1String("th"))
                    allTh = false;
                if (col == 0 && r > 0 && cell->tag != QLatin1String("th"))
                    headerCol = false;
                // Cell paragraphs join with line breaks.
                QList<Span> spans;
                for (const Node *c : cell->children) {
                    if (isBlock(c) && !spans.isEmpty())
                        addText(spans, QString(QChar::LineSeparator), Style{});
                    inlineSpans(c, Style{}, spans);
                }
                m_pendingImages.clear();
                row.append(trimmed(spans));
                ++col;
            }
            if (r == 0 && allTh && col > 0)
                headerRow = true;
            cols = qMax(cols, col);
            if (!row.isEmpty())
                b.rows.append(row);
        }
        if (b.rows.isEmpty())
            return;
        for (QList<Cell> &row : b.rows)
            while (row.size() < cols)
                row.append(Cell());
        if (headerRow || t->parent->tag == QLatin1String("thead"))
            b.data.insert(QStringLiteral("hr"), true);
        if (headerCol && rows.size() > 1 && !headerRow)
            b.data.insert(QStringLiteral("hc"), true);
        m_out.append(b);
    }

    void element(const Node *c, int depth)
    {
        const QString tag = c->tag;
        if (tag == QLatin1String("head") || tag == QLatin1String("script") || tag == QLatin1String("style") || tag == QLatin1String("title"))
            return;
        if (tag == QLatin1String("header")) {
            // Notion's page header: cover, icon and title.
            if (const Node *cover = findDescendant(c, [](const Node *x) { return x->hasClass(QStringLiteral("page-cover-image")); })) {
                info.cover = cover->attr(QStringLiteral("src"));
                static const QRegularExpression pos(QStringLiteral("object-position:\\s*\\S+\\s+([0-9.]+)%"));
                if (const auto m = pos.match(cover->attr(QStringLiteral("style"))); m.hasMatch())
                    info.coverPosition = qBound(0.0, m.captured(1).toDouble() / 100.0, 1.0);
            }
            if (const Node *icon = findDescendant(c, [](const Node *x) { return x->hasClass(QStringLiteral("page-header-icon")); })) {
                if (const Node *img = findDescendant(icon, [](const Node *x) { return x->tag == QLatin1String("img"); }))
                    info.icon = img->attr(QStringLiteral("src"));
                else
                    info.icon = textContent(icon).trimmed();
            }
            // The title and description; the cover and icon images aren't content.
            if (const Node *t = findDescendant(c, [](const Node *x) { return x->hasClass(QStringLiteral("page-title")); }))
                title = textContent(t).simplified();
            if (const Node *d = findDescendant(c, [](const Node *x) { return x->hasClass(QStringLiteral("page-description")); }))
                addParagraph(inlineOf(d), depth);
            if (info.cover.isEmpty() && info.icon.isEmpty() && title.isEmpty())
                blocks(c, depth); // some other kind of header
            return;
        }
        if (c->hasClass(QStringLiteral("collection-content")) && !c->attr(QStringLiteral("id")).isEmpty() && m_opt.resolveNotionId) {
            // An inline database: show the imported database itself.
            const QString db = m_opt.resolveNotionId(c->attr(QStringLiteral("id")));
            if (!db.isEmpty()) {
                Block b;
                b.type = BlockType::Database;
                b.depth = depth;
                b.pageId = db;
                // The exported table lists the view's visible properties, in order.
                if (const Node *head = findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("thead"); })) {
                    QJsonArray visible;
                    std::function<void(const Node *)> collect = [&](const Node *n) {
                        for (const Node *k : n->children) {
                            if (k->tag == QLatin1String("th"))
                                visible.append(textContent(k).simplified());
                            else if (!k->isText())
                                collect(k);
                        }
                    };
                    collect(head);
                    if (!visible.isEmpty())
                        b.data.insert(QStringLiteral("notionVisible"), visible);
                }
                m_out.append(b);
                return;
            }
        }
        if (tag.size() == 2 && tag[0] == QLatin1Char('h') && tag[1].isDigit()) {
            if (c->hasClass(QStringLiteral("page-title"))) {
                title = textContent(c).simplified();
                return;
            }
            const int level = tag[1].digitValue();
            Block b;
            b.type = level == 1 ? BlockType::Heading1 : level == 2 ? BlockType::Heading2 : level == 3 ? BlockType::Heading3 : BlockType::Heading4;
            b.depth = depth;
            b.spans = inlineOf(c);
            if (!b.spans.isEmpty())
                m_out.append(b);
            flushImages(depth);
            return;
        }
        if (tag == QLatin1String("p") || tag == QLatin1String("dt") || tag == QLatin1String("summary") || tag == QLatin1String("figcaption")
            || tag == QLatin1String("caption")) {
            addParagraph(inlineOf(c), depth);
            flushImages(depth);
            return;
        }
        if (tag == QLatin1String("dd")) {
            blocks(c, depth + 1);
            return;
        }
        if (tag == QLatin1String("ul") || tag == QLatin1String("ol")) {
            listItems(c, depth, tag == QLatin1String("ol"));
            return;
        }
        if (tag == QLatin1String("li")) {
            // A stray list item.
            Node fake;
            fake.tag = QStringLiteral("ul");
            fake.children = {const_cast<Node *>(c)};
            listItems(&fake, depth, false);
            return;
        }
        if (tag == QLatin1String("blockquote")) {
            quoteLike(c, depth, BlockType::Quote);
            return;
        }
        if (tag == QLatin1String("aside")) {
            quoteLike(c, depth, BlockType::Callout);
            // A leading emoji is the callout's icon.
            for (int k = int(m_out.size()) - 1; k >= 0; --k) {
                Block &b = m_out[k];
                if (b.type != BlockType::Callout || b.depth != depth)
                    continue;
                if (!b.spans.isEmpty()) {
                    QString &t = b.spans.first().text;
                    const char32_t first = t.size() > 1 && t[0].isHighSurrogate() ? QChar::surrogateToUcs4(t[0], t[1]) : (t.isEmpty() ? 0 : t[0].unicode());
                    if ((first >= 0x1F000 && first <= 0x1FAFF) || (first >= 0x2600 && first <= 0x27BF)) {
                        int len = first > 0xFFFF ? 2 : 1;
                        while (len < t.size() && (t[len].unicode() == 0xFE0F || t[len].unicode() == 0x200D))
                            ++len;
                        b.icon = t.left(len);
                        t = t.mid(len).trimmed();
                    }
                }
                break;
            }
            return;
        }
        if (tag == QLatin1String("pre")) {
            QString code = textContent(c, true);
            if (code.endsWith(QLatin1Char('\n')))
                code.chop(1);
            Block b;
            b.type = BlockType::Code;
            b.depth = depth;
            QString lang = c->classStartingWith(QStringLiteral("language-"));
            if (const Node *inner = findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("code"); }); inner && lang.isEmpty())
                lang = inner->classStartingWith(QStringLiteral("language-"));
            b.language = lang.mid(9).toLower();
            code.replace(QLatin1Char('\n'), QChar::LineSeparator);
            b.setPlainText(code);
            m_out.append(b);
            return;
        }
        if (tag == QLatin1String("hr")) {
            Block b;
            b.type = BlockType::Divider;
            b.depth = depth;
            m_out.append(b);
            return;
        }
        if (tag == QLatin1String("img") || tag == QLatin1String("en-media")) {
            imageBlock(c, depth, {});
            return;
        }
        if (tag == QLatin1String("table")) {
            // Notion's row pages start with a table of their properties.
            if (!c->hasClass(QStringLiteral("properties")))
                table(c, depth);
            return;
        }
        if (tag == QLatin1String("details")) {
            const Node *summary = nullptr;
            for (const Node *k : c->children)
                if (k->tag == QLatin1String("summary"))
                    summary = k;
            Block b;
            b.type = BlockType::Toggle;
            b.depth = depth;
            if (summary) {
                // A heading inside the summary makes a toggle heading.
                if (const Node *h = findDescendant(summary, [](const Node *x) {
                        return x->tag.size() == 2 && x->tag[0] == QLatin1Char('h') && x->tag[1].isDigit();
                    })) {
                    const int level = h->tag[1].digitValue();
                    b.type = level == 1 ? BlockType::Heading1 : level == 2 ? BlockType::Heading2 : level == 3 ? BlockType::Heading3 : BlockType::Heading4;
                    b.toggleable = true;
                    b.spans = inlineOf(h);
                } else {
                    b.spans = inlineOf(summary);
                }
            }
            m_out.append(b);
            m_pendingImages.clear();
            blocks(c, depth + 1, summary);
            return;
        }
        if (tag == QLatin1String("nav") && (c->hasClass(QStringLiteral("table_of_contents")) || c->hasClass(QStringLiteral("toc")))) {
            Block b;
            b.type = BlockType::TableOfContents;
            b.depth = depth;
            m_out.append(b);
            return;
        }
        if (tag == QLatin1String("figure")) {
            if (c->hasClass(QStringLiteral("callout"))) {
                // Notion: <figure class="callout"><div><span class="icon">💡</span></div><div>text…</div></figure>
                QString icon;
                const Node *content = nullptr;
                for (const Node *k : c->children) {
                    if (k->isText())
                        continue;
                    if (icon.isEmpty() && !content) {
                        const QString t = textContent(k).trimmed();
                        if (t.size() <= 4 && !t.isEmpty()) {
                            icon = t;
                            continue;
                        }
                    }
                    content = k;
                }
                if (content)
                    quoteLike(content, depth, BlockType::Callout, icon);
                else
                    m_out.append(Block::make(BlockType::Callout, QString(), depth));
                return;
            }
            if (c->hasClass(QStringLiteral("equation"))) {
                Block b;
                b.type = BlockType::Equation;
                b.depth = depth;
                QString tex = equationIn(c);
                if (tex.isEmpty())
                    tex = textContent(c).trimmed();
                b.data.insert(QStringLiteral("expr"), tex);
                m_out.append(b);
                return;
            }
            if (const Node *bm = findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("a") && x->hasClass(QStringLiteral("bookmark")); })) {
                Block b;
                b.type = BlockType::Bookmark;
                b.depth = depth;
                b.url = bm->attr(QStringLiteral("href"));
                if (const Node *t = findDescendant(bm, [](const Node *x) { return x->hasClass(QStringLiteral("bookmark-title")); }))
                    b.data.insert(QStringLiteral("title"), textContent(t).simplified());
                if (const Node *d = findDescendant(bm, [](const Node *x) { return x->hasClass(QStringLiteral("bookmark-description")); }))
                    b.data.insert(QStringLiteral("desc"), textContent(d).simplified());
                m_out.append(b);
                return;
            }
            if (c->hasClass(QStringLiteral("link-to-page"))) {
                QList<Span> spans = inlineOf(c);
                addParagraph(spans, depth);
                return;
            }
            // File blocks: <figure><div class="source"><a href="Page/file.pdf">file.pdf</a></div></figure>
            if (const Node *src = findDescendant(c, [](const Node *x) { return x->hasClass(QStringLiteral("source")); });
                src && !findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("img"); })) {
                QList<Span> spans = inlineOf(src);
                const Node *cap = findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("figcaption"); });
                const int before = int(m_out.size());
                addParagraph(spans, depth);
                if (cap && m_out.size() > before && m_out.last().type != BlockType::Paragraph)
                    m_out.last().caption = inlineOf(cap);
                return;
            }
            const Node *img = findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("img"); });
            const Node *cap = findDescendant(c, [](const Node *x) { return x->tag == QLatin1String("figcaption"); });
            if (img) {
                imageBlock(img, depth, cap ? inlineOf(cap) : QList<Span>());
                m_pendingImages.clear();
                return;
            }
            blocks(c, depth);
            return;
        }
        if (tag == QLatin1String("div") && c->hasClass(QStringLiteral("column-list"))) {
            Block b;
            b.type = BlockType::ColumnList;
            b.depth = depth;
            QList<double> ratios;
            for (const Node *col : c->children) {
                if (col->isText())
                    continue;
                Converter sub(m_opt);
                sub.blocks(col, 0);
                b.columns.append(sub.m_out);
                ratios.append(col->attr(QStringLiteral("data-notion-column-ratio")).toDouble());
            }
            if (b.columns.size() >= 2) {
                // Notion's column widths, when given; empty columns keep a place.
                double sum = 0;
                for (double r : ratios)
                    sum += r;
                for (int k = 0; k < b.columns.size(); ++k)
                    b.widths.append(sum > 0.01 ? ratios[k] / sum : 1.0 / b.columns.size());
                for (QList<Block> &col : b.columns)
                    if (col.isEmpty())
                        col.append(Block::make(BlockType::Paragraph));
                m_out.append(b);
            } else {
                for (const QList<Block> &col : b.columns)
                    m_out.append(col);
            }
            return;
        }
        if (tag == QLatin1String("div")) {
            // Evernote to-dos: <div><en-todo checked="true"/>Task</div>
            const Node *first = nullptr;
            for (const Node *k : c->children)
                if (!k->isText() || !k->text.trimmed().isEmpty()) {
                    first = k;
                    break;
                }
            if (first && first->tag == QLatin1String("en-todo")) {
                Block b;
                b.type = BlockType::Todo;
                b.depth = depth;
                b.checked = first->attr(QStringLiteral("checked")) == QLatin1String("true");
                b.spans = inlineOf(c);
                m_out.append(b);
                flushImages(depth);
                return;
            }
            if (c->hasClass(QStringLiteral("page-description"))) {
                addParagraph(inlineOf(c), depth);
                return;
            }
        }
        // Containers: html, body, div, section, article…
        blocks(c, depth);
    }

    const Markdown::ImportOptions &m_opt;
    QList<Block> m_out;
    QList<const Node *> m_pendingImages;
};
} // namespace

namespace HtmlImport
{
QList<Block> toBlocks(const QString &html, const Markdown::ImportOptions &options, QString *title, PageInfo *info)
{
    Dom dom;
    QString docTitle;
    const Node *root = parseHtml(html, dom, &docTitle);
    Converter conv(options);
    QList<Block> out = conv.run(root);
    if (title)
        *title = !conv.title.isEmpty() ? conv.title : docTitle;
    if (info)
        *info = conv.info;
    return out;
}
} // namespace HtmlImport

// ---- Word ------------------------------------------------------------------------

namespace DocxImport
{
namespace
{
struct Numbering {
    // numId → level → ordered?
    QHash<QString, QHash<int, bool>> ordered;
};

Numbering readNumbering(const QByteArray &xml)
{
    Numbering out;
    QHash<QString, QHash<int, bool>> abstract;
    QHash<QString, QString> numToAbstract;
    QXmlStreamReader r(xml);
    QString currentAbstract;
    int currentLevel = -1;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            const QStringView n = r.name();
            if (n == QLatin1String("abstractNum"))
                currentAbstract = r.attributes().value(QLatin1String("w:abstractNumId")).toString();
            else if (n == QLatin1String("lvl"))
                currentLevel = r.attributes().value(QLatin1String("w:ilvl")).toInt();
            else if (n == QLatin1String("numFmt") && !currentAbstract.isEmpty() && currentLevel >= 0)
                abstract[currentAbstract][currentLevel] = r.attributes().value(QLatin1String("w:val")) != QLatin1String("bullet");
            else if (n == QLatin1String("num")) {
                currentAbstract.clear();
                numToAbstract.insert(r.attributes().value(QLatin1String("w:numId")).toString(), QString());
            }
            else if (n == QLatin1String("abstractNumId")) {
                // Inside <w:num>: link it to its abstract numbering.
                for (auto it = numToAbstract.begin(); it != numToAbstract.end(); ++it)
                    if (it.value().isEmpty())
                        it.value() = r.attributes().value(QLatin1String("w:val")).toString();
            }
        }
    }
    for (auto it = numToAbstract.cbegin(); it != numToAbstract.cend(); ++it)
        out.ordered.insert(it.key(), abstract.value(it.value()));
    return out;
}

QHash<QString, QString> readRels(const QByteArray &xml)
{
    QHash<QString, QString> out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("Relationship"))
            out.insert(r.attributes().value(QLatin1String("Id")).toString(), r.attributes().value(QLatin1String("Target")).toString());
    }
    return out;
}
} // namespace

Result read(const QByteArray &docx, const std::function<QString(const QByteArray &, const QString &)> &importImage)
{
    Result res;
    ZipReader zip;
    if (!zip.openData(docx) || !zip.contains(QStringLiteral("word/document.xml"))) {
        res.error = QStringLiteral("This doesn't look like a Word document.");
        return res;
    }
    const Numbering numbering = readNumbering(zip.read(QStringLiteral("word/numbering.xml")));
    const QHash<QString, QString> rels = readRels(zip.read(QStringLiteral("word/_rels/document.xml.rels")));
    {
        QXmlStreamReader core(zip.read(QStringLiteral("docProps/core.xml")));
        while (!core.atEnd()) {
            core.readNext();
            if (core.isStartElement() && core.name() == QLatin1String("title")) {
                res.title = core.readElementText().trimmed();
                break;
            }
        }
    }

    QXmlStreamReader r(zip.read(QStringLiteral("word/document.xml")));
    // Paragraph state.
    Block para;
    bool inPara = false;
    QString style;
    QString numId;
    int ilvl = 0;
    Span run;
    bool inRun = false;
    QString link;
    QList<QString> images;
    // Tables: rows of cells, each cell's paragraphs as spans.
    int tableDepth = 0;
    Block tableBlock;
    QList<Cell> tableRow;
    QList<Span> cellSpans;
    bool cellHasPara = false;

    auto flushPara = [&] {
        if (!inPara)
            return;
        inPara = false;
        para.spans = normalizeSpans(para.spans);
        if (tableDepth > 0) {
            if (cellHasPara && !para.spans.isEmpty())
                cellSpans.append(Span{QString(QChar::LineSeparator)});
            cellSpans += para.spans;
            cellHasPara = true;
            return;
        }
        const QString s = style.toLower();
        if (s == QLatin1String("title")) {
            if (res.title.isEmpty())
                res.title = para.plainText();
        } else {
            if (s.startsWith(QLatin1String("heading")) || s == QLatin1String("subtitle")) {
                const int level = s == QLatin1String("subtitle") ? 2 : s.mid(7).toInt();
                para.type = level <= 1 ? BlockType::Heading1 : level == 2 ? BlockType::Heading2 : level == 3 ? BlockType::Heading3 : BlockType::Heading4;
            } else if (!numId.isEmpty() && numId != QLatin1String("0")) {
                para.type = numbering.ordered.value(numId).value(ilvl, false) ? BlockType::Numbered : BlockType::Bullet;
                para.depth = ilvl;
            } else if (s.contains(QLatin1String("quote"))) {
                para.type = BlockType::Quote;
            } else if (s.contains(QLatin1String("code")) || s.contains(QLatin1String("source"))) {
                para.type = BlockType::Code;
            }
            if (!para.spans.isEmpty() || para.type != BlockType::Paragraph)
                res.blocks.append(para);
        }
        for (const QString &media : images) {
            Block img;
            img.type = BlockType::Image;
            img.media = media;
            res.blocks.append(img);
        }
        images.clear();
    };

    while (!r.atEnd()) {
        r.readNext();
        const QStringView n = r.name();
        if (r.isStartElement()) {
            if (n == QLatin1String("tbl")) {
                flushPara();
                if (tableDepth++ == 0) {
                    tableBlock = Block();
                    tableBlock.type = BlockType::Table;
                }
            } else if (n == QLatin1String("tr") && tableDepth == 1) {
                tableRow.clear();
            } else if (n == QLatin1String("tc") && tableDepth == 1) {
                cellSpans.clear();
                cellHasPara = false;
            } else if (n == QLatin1String("p")) {
                flushPara();
                inPara = true;
                para = Block();
                style.clear();
                numId.clear();
                ilvl = 0;
            } else if (n == QLatin1String("pStyle")) {
                style = r.attributes().value(QLatin1String("w:val")).toString();
            } else if (n == QLatin1String("numId")) {
                numId = r.attributes().value(QLatin1String("w:val")).toString();
            } else if (n == QLatin1String("ilvl")) {
                ilvl = r.attributes().value(QLatin1String("w:val")).toInt();
            } else if (n == QLatin1String("hyperlink")) {
                link = rels.value(r.attributes().value(QLatin1String("r:id")).toString());
            } else if (n == QLatin1String("r")) {
                inRun = true;
                run = Span();
                run.link = link;
            } else if (inRun && (n == QLatin1String("b") || n == QLatin1String("i") || n == QLatin1String("u") || n == QLatin1String("strike")
                                 || n == QLatin1String("dstrike"))) {
                const QStringView val = r.attributes().value(QLatin1String("w:val"));
                const bool on = val.isEmpty() || (val != QLatin1String("0") && val != QLatin1String("false") && val != QLatin1String("none"));
                if (n == QLatin1String("b"))
                    run.bold = on;
                else if (n == QLatin1String("i"))
                    run.italic = on;
                else if (n == QLatin1String("u"))
                    run.underline = on;
                else
                    run.strike = on;
            } else if (inRun && n == QLatin1String("rFonts")) {
                const QString font = r.attributes().value(QLatin1String("w:ascii")).toString().toLower();
                if (font.contains(QLatin1String("mono")) || font.contains(QLatin1String("courier")) || font.contains(QLatin1String("consolas")))
                    run.code = true;
            } else if (inRun && n == QLatin1String("t")) {
                run.text += r.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (inRun && n == QLatin1String("tab")) {
                run.text += QLatin1Char('\t');
            } else if (inRun && (n == QLatin1String("br") || n == QLatin1String("cr"))) {
                run.text += QChar::LineSeparator;
            } else if (n == QLatin1String("blip")) {
                const QString target = rels.value(r.attributes().value(QLatin1String("r:embed")).toString());
                if (!target.isEmpty() && importImage) {
                    const QString path = target.startsWith(QLatin1Char('/')) ? target.mid(1) : QStringLiteral("word/") + target;
                    const QByteArray data = zip.read(path);
                    if (!data.isEmpty()) {
                        const QString media = importImage(data, QFileInfo(path).suffix());
                        if (!media.isEmpty())
                            images.append(media);
                    }
                }
            }
        } else if (r.isEndElement()) {
            if (n == QLatin1String("r") && inRun) {
                inRun = false;
                if (!run.text.isEmpty())
                    para.spans.append(run);
            } else if (n == QLatin1String("hyperlink")) {
                link.clear();
            } else if (n == QLatin1String("p")) {
                flushPara();
            } else if (n == QLatin1String("tc") && tableDepth == 1) {
                flushPara();
                tableRow.append(normalizeSpans(cellSpans));
            } else if (n == QLatin1String("tr") && tableDepth == 1) {
                tableBlock.rows.append(tableRow);
            } else if (n == QLatin1String("tbl")) {
                if (--tableDepth == 0 && !tableBlock.rows.isEmpty()) {
                    int cols = 0;
                    for (const auto &row : tableBlock.rows)
                        cols = qMax(cols, int(row.size()));
                    for (auto &row : tableBlock.rows)
                        while (row.size() < cols)
                            row.append(Cell());
                    tableBlock.data.insert(QStringLiteral("hr"), true);
                    res.blocks.append(tableBlock);
                }
            }
        }
    }
    flushPara();
    if (r.hasError() && res.blocks.isEmpty())
        res.error = r.errorString();
    return res;
}
} // namespace DocxImport

// ---- Evernote --------------------------------------------------------------------

namespace EnexImport
{
QList<Note> read(const QByteArray &enex, const std::function<QString(const QByteArray &, const QString &)> &importImage)
{
    QList<Note> notes;
    QXmlStreamReader r(enex);
    Note note;
    QString content;
    struct Resource {
        QByteArray data;
        QString mime;
    };
    QList<Resource> resources;
    Resource res;
    bool inResource = false;
    auto finish = [&] {
        // Resources are referenced by the MD5 of their data.
        QHash<QString, Resource> byHash;
        for (const Resource &x : resources)
            byHash.insert(QString::fromLatin1(QCryptographicHash::hash(x.data, QCryptographicHash::Md5).toHex()), x);
        Markdown::ImportOptions opt;
        opt.importImage = [&](const QString &src) -> QString {
            if (!src.startsWith(QLatin1String("enres:")) || !importImage)
                return {};
            const Resource x = byHash.value(src.mid(6));
            if (x.data.isEmpty() || !x.mime.startsWith(QLatin1String("image/")))
                return {};
            QString suffix = x.mime.mid(6);
            if (suffix == QLatin1String("jpeg"))
                suffix = QStringLiteral("jpg");
            else if (suffix.contains(QLatin1Char('+')))
                suffix = suffix.left(suffix.indexOf(QLatin1Char('+')));
            return importImage(x.data, suffix);
        };
        note.blocks = HtmlImport::toBlocks(content, opt);
        notes.append(note);
        note = Note();
        content.clear();
        resources.clear();
    };
    while (!r.atEnd()) {
        r.readNext();
        const QStringView n = r.name();
        if (r.isStartElement()) {
            if (n == QLatin1String("note")) {
                note = Note();
                content.clear();
                resources.clear();
            } else if (n == QLatin1String("title") && !inResource) {
                note.title = r.readElementText().trimmed();
            } else if (n == QLatin1String("content")) {
                content = r.readElementText();
            } else if (n == QLatin1String("created")) {
                const QDateTime t = QDateTime::fromString(r.readElementText().trimmed(), QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
                if (t.isValid()) {
                    QDateTime utc = t;
                    utc.setTimeZone(QTimeZone::UTC);
                    note.created = utc.toMSecsSinceEpoch();
                }
            } else if (n == QLatin1String("tag")) {
                note.tags.append(r.readElementText().trimmed());
            } else if (n == QLatin1String("resource")) {
                inResource = true;
                res = Resource();
            } else if (n == QLatin1String("data") && inResource) {
                res.data = QByteArray::fromBase64(r.readElementText().toLatin1(), QByteArray::Base64Encoding);
            } else if (n == QLatin1String("mime") && inResource) {
                res.mime = r.readElementText().trimmed();
            }
        } else if (r.isEndElement()) {
            if (n == QLatin1String("resource")) {
                inResource = false;
                resources.append(res);
            } else if (n == QLatin1String("note")) {
                finish();
            }
        }
    }
    return notes;
}
} // namespace EnexImport
