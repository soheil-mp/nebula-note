// Import and export of the richer blocks: Markdown round trips, HTML
// (including Notion's export markup), Word documents, Evernote exports and
// the HTML/PDF exporter.
#include "core/Exporter.h"
#include "core/HtmlImport.h"
#include "core/Importer.h"
#include "core/Markdown.h"
#include "core/Workspace.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <zlib.h>

namespace
{
// A minimal zip writer (deflate) for building test archives.
QByteArray makeZip(const QList<QPair<QString, QByteArray>> &files)
{
    QByteArray out, central;
    auto put16 = [](QByteArray &b, quint16 v) {
        b.append(char(v & 0xff));
        b.append(char(v >> 8));
    };
    auto put32 = [](QByteArray &b, quint32 v) {
        for (int i = 0; i < 4; ++i)
            b.append(char((v >> (8 * i)) & 0xff));
    };
    for (const auto &[name, data] : files) {
        QByteArray comp(compressBound(uLong(data.size())) + 16, 0);
        z_stream zs{};
        deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
        zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
        zs.avail_in = uInt(data.size());
        zs.next_out = reinterpret_cast<Bytef *>(comp.data());
        zs.avail_out = uInt(comp.size());
        deflate(&zs, Z_FINISH);
        comp.resize(qsizetype(zs.total_out));
        deflateEnd(&zs);
        const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
        const QByteArray n = name.toUtf8();
        const quint32 offset = quint32(out.size());
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x800);
        put16(out, 8);
        put16(out, 0);
        put16(out, 0);
        put32(out, crc);
        put32(out, quint32(comp.size()));
        put32(out, quint32(data.size()));
        put16(out, quint16(n.size()));
        put16(out, 0);
        out += n + comp;
        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x800);
        put16(central, 8);
        put16(central, 0);
        put16(central, 0);
        put32(central, crc);
        put32(central, quint32(comp.size()));
        put32(central, quint32(data.size()));
        put16(central, quint16(n.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += n;
    }
    const quint32 cdOffset = quint32(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(files.size()));
    put16(out, quint16(files.size()));
    put32(out, quint32(central.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}


QString plain(const QList<Span> &spans)
{
    QString t;
    for (const Span &s : spans)
        t += s.text;
    return t;
}

QByteArray png()
{
    QImage img(6, 6, QImage::Format_RGB32);
    img.fill(Qt::darkGray);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

Span text(const QString &t)
{
    Span s;
    s.text = t;
    return s;
}
} // namespace

class IoTests : public QObject
{
    Q_OBJECT

private slots:
    void markdownRoundTripsRichBlocks()
    {
        QList<Block> blocks;
        blocks.append(Block::make(BlockType::Heading4, QStringLiteral("Small heading")));
        Block th = Block::make(BlockType::Heading2, QStringLiteral("Folded section"));
        th.toggleable = true;
        blocks.append(th);
        blocks.append(Block::make(BlockType::Paragraph, QStringLiteral("Inside the toggle"), 1));
        Block eq;
        eq.type = BlockType::Equation;
        eq.data.insert(QStringLiteral("expr"), QStringLiteral("e^{i\\pi} + 1 = 0"));
        blocks.append(eq);
        Block line = Block::make(BlockType::Paragraph, QStringLiteral("Costs $5, see "));
        Span ieq;
        ieq.text = QString(QChar::ObjectReplacementCharacter);
        ieq.equation = QStringLiteral("x^2");
        line.spans.append(ieq);
        line.spans.append(text(QStringLiteral(" due ")));
        Span date;
        date.text = QString(QChar::ObjectReplacementCharacter);
        date.date = QStringLiteral("2026-10-20T09:30");
        date.remind = true;
        line.spans.append(date);
        blocks.append(line);
        Block toc;
        toc.type = BlockType::TableOfContents;
        blocks.append(toc);
        Block crumb;
        crumb.type = BlockType::Breadcrumb;
        blocks.append(crumb);
        Block table;
        table.type = BlockType::Table;
        table.data.insert(QStringLiteral("hr"), true);
        table.rows = {{{text(QStringLiteral("Planet"))}, {text(QStringLiteral("Moons"))}},
                      {{text(QStringLiteral("Mars | red"))}, {text(QStringLiteral("2"))}}};
        blocks.append(table);
        Block cols;
        cols.type = BlockType::ColumnList;
        cols.widths = {0.5, 0.5};
        cols.columns = {{Block::make(BlockType::Heading3, QStringLiteral("Left")), Block::make(BlockType::Bullet, QStringLiteral("one"))},
                        {Block::make(BlockType::Todo, QStringLiteral("right"))}};
        blocks.append(cols);
        Block callout = Block::make(BlockType::Callout, QStringLiteral("Note this"));
        blocks.append(callout);
        blocks.append(Block::make(BlockType::Bullet, QStringLiteral("callout child"), 1));

        const QString md = Markdown::fromBlocks(blocks);
        QVERIFY(md.contains(QStringLiteral("#### Small heading")));
        QVERIFY(md.contains(QStringLiteral("## Folded section {toggle=\"true\"}")));
        QVERIFY(md.contains(QStringLiteral("$$\ne^{i\\pi} + 1 = 0\n$$")));
        QVERIFY(md.contains(QStringLiteral("$`x^2`$")));
        QVERIFY(md.contains(QStringLiteral("\\$5")));
        QVERIFY(md.contains(QStringLiteral("<table_of_contents/>")));
        QVERIFY(md.contains(QStringLiteral("| Planet | Moons |")));
        QVERIFY(md.contains(QStringLiteral("Mars \\| red")));
        QVERIFY(md.contains(QStringLiteral("<columns>")));

        const QList<Block> back = Markdown::toBlocks(md);
        QList<BlockType> types;
        for (const Block &b : back)
            types.append(b.type);
        const QList<BlockType> expected = {BlockType::Heading4, BlockType::Heading2, BlockType::Paragraph, BlockType::Equation,
                                           BlockType::Paragraph, BlockType::TableOfContents, BlockType::Breadcrumb, BlockType::Table,
                                           BlockType::ColumnList, BlockType::Callout, BlockType::Bullet};
        QCOMPARE(types, expected);
        QVERIFY(back[1].toggleable);
        QCOMPARE(back[2].depth, 1);
        QCOMPARE(back[3].data.value(QStringLiteral("expr")).toString(), QStringLiteral("e^{i\\pi} + 1 = 0"));
        const QList<Span> &spans = back[4].spans;
        QCOMPARE(plain({spans.first()}), QStringLiteral("Costs $5, see "));
        bool sawEq = false, sawDate = false;
        for (const Span &s : spans) {
            sawEq |= s.equation == QLatin1String("x^2");
            if (!s.date.isEmpty()) {
                sawDate = true;
                QCOMPARE(s.date, QStringLiteral("2026-10-20T09:30"));
                QVERIFY(s.remind);
            }
        }
        QVERIFY(sawEq && sawDate);
        QCOMPARE(back[7].rows.size(), 2);
        QCOMPARE(plain(back[7].rows[1][0]), QStringLiteral("Mars | red"));
        QVERIFY(back[7].data.value(QStringLiteral("hr")).toBool());
        QCOMPARE(back[8].columns.size(), 2);
        QCOMPARE(back[8].columns[0].size(), 2);
        QCOMPARE(back[8].columns[1].first().type, BlockType::Todo);
        QCOMPARE(back[10].depth, 1); // the callout's child comes back as its child
    }

    void markdownLeavesPricesAlone()
    {
        const QList<Block> b = Markdown::toBlocks(QStringLiteral("It costs $5 and $6 today; math: $a+b$ done."));
        QCOMPARE(b.size(), 1);
        int equations = 0;
        for (const Span &s : b[0].spans)
            equations += !s.equation.isEmpty();
        QCOMPARE(equations, 1);
        QVERIFY(plain(b[0].spans).startsWith(QStringLiteral("It costs $5 and $6 today")));
    }

    void htmlImportCoversCommonMarkup()
    {
        const QString html = QStringLiteral(
            "<html><head><title>Doc</title><style>p{}</style></head><body>"
            "<header><h1 class=\"page-title\">Imported page</h1></header>"
            "<p>Some <b>bold</b>, <em>italic</em>, <code>code</code> and <a href=\"https://example.com\">a link</a>.<br>Next line</p>"
            "<h2>Section</h2><h5>Deep</h5>"
            "<ul><li>One<ul><li>Nested</li></ul></li><li>Two</li></ul>"
            "<ol><li>First</li><li>Second</li></ol>"
            "<ul class=\"to-do-list\"><li><div class=\"checkbox checkbox-on\"></div> <span>Done task</span></li>"
            "<li><input type=\"checkbox\"> Open task</li></ul>"
            "<blockquote>Quoted</blockquote>"
            "<pre><code class=\"language-python\">print(1)\nprint(2)</code></pre>"
            "<hr>"
            "<table><thead><tr><th>A</th><th>B</th></tr></thead><tbody><tr><td>1</td><td>2 &amp; 3</td></tr></tbody></table>"
            "<details><summary>Toggle me</summary><p>Hidden</p></details>"
            "<aside>💡 Callout text</aside>"
            "<figure class=\"image\"><img src=\"pic.png\"><figcaption>A caption</figcaption></figure>"
            "<figure class=\"equation\"><annotation encoding=\"application/x-tex\">a^2+b^2=c^2</annotation></figure>"
            "<p>Inline <span class=\"notion-text-equation-token\"><annotation encoding=\"application/x-tex\">\\pi</annotation></span> here</p>"
            "<div class=\"column-list\"><div class=\"column\"><p>Left</p></div><div class=\"column\"><p>Right</p></div></div>"
            "<nav class=\"block-color-gray table_of_contents\"><a href=\"#x\">Section</a></nav>"
            "<!-- a comment --></body></html>");
        Markdown::ImportOptions opt;
        opt.importImage = [](const QString &src) { return src == QLatin1String("pic.png") ? QStringLiteral("m1.png") : QString(); };
        QString title;
        const QList<Block> b = HtmlImport::toBlocks(html, opt, &title);
        QCOMPARE(title, QStringLiteral("Imported page"));
        QList<BlockType> types;
        for (const Block &x : b)
            types.append(x.type);
        const QList<BlockType> expected = {BlockType::Paragraph, BlockType::Heading2, BlockType::Heading4, BlockType::Bullet, BlockType::Bullet,
                                           BlockType::Bullet,    BlockType::Numbered, BlockType::Numbered, BlockType::Todo,  BlockType::Todo,
                                           BlockType::Quote,     BlockType::Code,     BlockType::Divider,  BlockType::Table, BlockType::Toggle,
                                           BlockType::Paragraph, BlockType::Callout,  BlockType::Image,    BlockType::Equation, BlockType::Paragraph,
                                           BlockType::ColumnList, BlockType::TableOfContents};
        if (types != expected)
            for (const Block &x : b)
                qDebug() << blockTypeKey(x.type) << x.depth << plain(x.spans);
        QCOMPARE(types, expected);
        // Inline styles and the line break.
        const QList<Span> &p = b[0].spans;
        QVERIFY(std::any_of(p.begin(), p.end(), [](const Span &s) { return s.bold && s.text == QLatin1String("bold"); }));
        QVERIFY(std::any_of(p.begin(), p.end(), [](const Span &s) { return s.italic && s.text == QLatin1String("italic"); }));
        QVERIFY(std::any_of(p.begin(), p.end(), [](const Span &s) { return s.code && s.text == QLatin1String("code"); }));
        QVERIFY(std::any_of(p.begin(), p.end(), [](const Span &s) { return s.link == QLatin1String("https://example.com"); }));
        QVERIFY(plain(p).contains(QChar::LineSeparator));
        QCOMPARE(b[4].depth, 1); // nested bullet
        QVERIFY(b[8].checked);
        QVERIFY(!b[9].checked);
        QCOMPARE(plain(b[9].spans), QStringLiteral("Open task"));
        QCOMPARE(b[11].language, QStringLiteral("python"));
        QCOMPARE(b[11].plainText(), QStringLiteral("print(1)") + QChar::LineSeparator + QStringLiteral("print(2)"));
        QCOMPARE(plain(b[13].rows[1][1]), QStringLiteral("2 & 3"));
        QVERIFY(b[13].data.value(QStringLiteral("hr")).toBool());
        QCOMPARE(plain(b[14].spans), QStringLiteral("Toggle me"));
        QCOMPARE(b[16].icon, QStringLiteral("💡"));
        QCOMPARE(plain(b[16].spans), QStringLiteral("Callout text"));
        QCOMPARE(plain(b[8].spans), QStringLiteral("Done task"));
        QCOMPARE(b[15].depth, 1);
        QCOMPARE(b[17].media, QStringLiteral("m1.png"));
        QCOMPARE(plain(b[17].caption), QStringLiteral("A caption"));
        QCOMPARE(b[18].data.value(QStringLiteral("expr")).toString(), QStringLiteral("a^2+b^2=c^2"));
        QVERIFY(std::any_of(b[19].spans.begin(), b[19].spans.end(), [](const Span &s) { return s.equation == QLatin1String("\\pi"); }));
        QCOMPARE(b[20].columns.size(), 2);
    }

    void notionHtmlExportImportsAsPages()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath(QStringLiteral("export"));
        QDir().mkpath(root + QStringLiteral("/Home 0123456789abcdef0123456789abcdef"));
        auto write = [](const QString &path, const QByteArray &data) {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write(root + QStringLiteral("/Home 0123456789abcdef0123456789abcdef.html"),
              QByteArray("<html><head><title>Home</title></head><body><article><header><h1 class=\"page-title\">Home</h1></header>"
                         "<div class=\"page-body\"><p>Welcome</p><figure class=\"link-to-page\">"
                         "<a href=\"Home%200123456789abcdef0123456789abcdef/Child%20fedcba9876543210fedcba9876543210.html\">Child</a></figure>"
                         "<figure class=\"image\"><img src=\"Home%200123456789abcdef0123456789abcdef/pic.png\"></figure>"
                         "</div></article></body></html>"));
        write(root + QStringLiteral("/Home 0123456789abcdef0123456789abcdef/Child fedcba9876543210fedcba9876543210.html"),
              QByteArray("<html><body><header><h1 class=\"page-title\">Child page</h1></header>"
                         "<table class=\"properties\"><tr><th>Status</th><td>Done</td></tr></table><p>Child body</p></body></html>"));
        write(root + QStringLiteral("/Home 0123456789abcdef0123456789abcdef/pic.png"), png());
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result r = Importer::importPath(ws, root);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(ws.title(r.rootId), QStringLiteral("Home"));
        QCOMPARE(r.images, 1);
        const QList<Block> home = ws.content(r.rootId);
        QCOMPARE(home.size(), 3);
        QCOMPARE(home[1].type, BlockType::PageLink);
        QCOMPARE(ws.title(home[1].pageId), QStringLiteral("Child page"));
        QCOMPARE(home[2].type, BlockType::Image);
        const QList<Block> child = ws.content(home[1].pageId);
        QCOMPARE(child.size(), 1); // the property table is dropped
        QCOMPARE(child[0].plainText(), QStringLiteral("Child body"));
    }

    void wordDocumentsImport()
    {
        const QByteArray document = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"\n"
            "            xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"\n"
            "            xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">\n"
            "<w:body>\n"
            "<w:p><w:pPr><w:pStyle w:val=\"Title\"/></w:pPr><w:r><w:t>Report</w:t></w:r></w:p>\n"
            "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Intro</w:t></w:r></w:p>\n"
            "<w:p><w:r><w:t xml:space=\"preserve\">Plain and </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>bold</w:t></w:r><w:r><w:t xml:space=\"preserve\"> and </w:t></w:r><w:hyperlink r:id=\"rId9\"><w:r><w:t>link</w:t></w:r></w:hyperlink></w:p>\n"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>Bullet</w:t></w:r></w:p>\n"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"1\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>Sub bullet</w:t></w:r></w:p>\n"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"2\"/></w:numPr></w:pPr><w:r><w:t>Step</w:t></w:r></w:p>\n"
            "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>H1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>H2</w:t></w:r></w:p></w:tc></w:tr>\n"
            "<w:tr><w:tc><w:p><w:r><w:t>a</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>b</w:t></w:r></w:p></w:tc></w:tr></w:tbl>\n"
            "<w:p><w:r><w:drawing><a:blip r:embed=\"rId5\"/></w:drawing></w:r></w:p>\n"
            "</w:body></w:document>";
        const QByteArray numbering = "<?xml version=\"1.0\"?>\n"
            "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
            "<w:abstractNum w:abstractNumId=\"10\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/></w:lvl><w:lvl w:ilvl=\"1\"><w:numFmt w:val=\"bullet\"/></w:lvl></w:abstractNum>\n"
            "<w:abstractNum w:abstractNumId=\"20\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"decimal\"/></w:lvl></w:abstractNum>\n"
            "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"10\"/></w:num>\n"
            "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"20\"/></w:num>\n"
            "</w:numbering>";
        const QByteArray rels = "<?xml version=\"1.0\"?>\n"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
            "<Relationship Id=\"rId5\" Type=\"image\" Target=\"media/image1.png\"/>\n"
            "<Relationship Id=\"rId9\" Type=\"hyperlink\" Target=\"https://nebula.example\" TargetMode=\"External\"/>\n"
            "</Relationships>";
        const QByteArray docx = makeZip({{QStringLiteral("word/document.xml"), document},
                                         {QStringLiteral("word/numbering.xml"), numbering},
                                         {QStringLiteral("word/_rels/document.xml.rels"), rels},
                                         {QStringLiteral("word/media/image1.png"), png()}});
        int images = 0;
        const DocxImport::Result r = DocxImport::read(docx, [&](const QByteArray &data, const QString &suffix) {
            ++images;
            return !data.isEmpty() && suffix == QLatin1String("png") ? QStringLiteral("doc.png") : QString();
        });
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.title, QStringLiteral("Report"));
        QList<BlockType> types;
        for (const Block &b : r.blocks)
            types.append(b.type);
        const QList<BlockType> expected = {BlockType::Heading1, BlockType::Paragraph, BlockType::Bullet, BlockType::Bullet,
                                           BlockType::Numbered, BlockType::Table, BlockType::Image};
        QCOMPARE(types, expected);
        QCOMPARE(r.blocks[3].depth, 1);
        const QList<Span> &p = r.blocks[1].spans;
        QVERIFY(std::any_of(p.begin(), p.end(), [](const Span &s) { return s.bold && s.text == QLatin1String("bold"); }));
        QVERIFY(std::any_of(p.begin(), p.end(), [](const Span &s) { return s.link == QLatin1String("https://nebula.example"); }));
        QCOMPARE(plain(r.blocks[5].rows[1][1]), QStringLiteral("b"));
        QCOMPARE(r.blocks[6].media, QStringLiteral("doc.png"));
        QCOMPARE(images, 1);

        // Through the importer, as a page.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("report.docx"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(docx);
        f.close();
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result ir = Importer::importPath(ws, path);
        QVERIFY2(ir.ok(), qPrintable(ir.error));
        QCOMPARE(ws.title(ir.rootId), QStringLiteral("Report"));
        QCOMPARE(ir.images, 1);
        QCOMPARE(ws.content(ir.rootId).size(), 7);
    }

    void evernoteExportsImport()
    {
        const QByteArray imageData = png();
        const QString hash = QString::fromLatin1(QCryptographicHash::hash(imageData, QCryptographicHash::Md5).toHex());
        const QByteArray enex = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE en-export SYSTEM \"http://xml.evernote.com/pub/evernote-export3.dtd\">\n"
            "<en-export>\n"
            "<note><title>Groceries</title><created>20261001T120000Z</created><tag>home</tag>\n"
            "<content><![CDATA[<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE en-note SYSTEM \"http://xml.evernote.com/pub/enml2.dtd\">\n"
            "<en-note><div><b>Buy</b> these:</div><div><en-todo checked=\"true\"/>Milk</div><div><en-todo/>Bread</div>\n"
            "<div><en-media hash=\"%1\" type=\"image/png\"/></div></en-note>]]></content>\n"
            "<resource><data encoding=\"base64\">%2</data><mime>image/png</mime></resource>\n"
            "</note>\n"
            "<note><title>Second</title><content><![CDATA[<en-note><ul><li>a</li><li>b</li></ul></en-note>]]></content></note>\n"
            "</en-export>")
                                 .arg(hash, QString::fromLatin1(imageData.toBase64()))
                                 .toUtf8();
        int images = 0;
        const QList<EnexImport::Note> notes = EnexImport::read(enex, [&](const QByteArray &data, const QString &suffix) {
            ++images;
            return data == imageData && suffix == QLatin1String("png") ? QStringLiteral("en.png") : QString();
        });
        QCOMPARE(notes.size(), 2);
        QCOMPARE(notes[0].title, QStringLiteral("Groceries"));
        QCOMPARE(notes[0].tags, QStringList{QStringLiteral("home")});
        QVERIFY(notes[0].created > 0);
        QList<BlockType> types;
        for (const Block &b : notes[0].blocks)
            types.append(b.type);
        QCOMPARE(types, (QList<BlockType>{BlockType::Paragraph, BlockType::Todo, BlockType::Todo, BlockType::Image}));
        QVERIFY(notes[0].blocks[1].checked);
        QVERIFY(!notes[0].blocks[2].checked);
        QCOMPARE(notes[0].blocks[3].media, QStringLiteral("en.png"));
        QCOMPARE(images, 1);
        QCOMPARE(notes[1].blocks.size(), 2);
        // Through the importer: a notebook page holding both notes.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Notebook.enex"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(enex);
        f.close();
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result ir = Importer::importPath(ws, path);
        QVERIFY2(ir.ok(), qPrintable(ir.error));
        QCOMPARE(ws.title(ir.rootId), QStringLiteral("Notebook"));
        QCOMPARE(ws.children(ir.rootId).size(), 2);
        QCOMPARE(ir.pages, 3);
    }

    void notionMarkdownExportWithCalloutsAndAttachments()
    {
        // Newer exports: "Doc <id>.md" with its files in "Doc/" (no id on the folder).
        const QString id1 = QStringLiteral("11111111111111111111111111111111");
        const QString id2 = QStringLiteral("22222222222222222222222222222222");
        QList<QPair<QString, QByteArray>> files;
        files.append({QStringLiteral("Doc %1.md").arg(id1),
                      QStringLiteral("# Doc\n\nIntro text.\n\n![image.png](Doc/image.png)\n\n<aside>\n\n**Callout title**\n\n- first point\n- second point\n\n"
                                     "![image.png](Doc/image%201.png)\n\nMore text inside.\n\n</aside>\n\n"
                                     "[report.pdf](Doc/report.pdf)\n\nSee [the notes](Doc/notes.txt) for details.\n\n"
                                     "[Sub](Doc/Sub%20%1.md)\n")
                          .arg(id2)
                          .toUtf8()});
        files.append({QStringLiteral("Doc/image.png"), png()});
        QImage other(5, 5, QImage::Format_RGB32);
        other.fill(Qt::white);
        QByteArray png2;
        {
            QBuffer buf(&png2);
            buf.open(QIODevice::WriteOnly);
            other.save(&buf, "PNG");
        }
        files.append({QStringLiteral("Doc/image 1.png"), png2});
        files.append({QStringLiteral("Doc/report.pdf"), QByteArray("%PDF-1.4 test")});
        files.append({QStringLiteral("Doc/notes.txt"), QByteArray("plain notes")});
        files.append({QStringLiteral("Doc/Sub %1.md").arg(id2), QByteArray("# Sub\n\nSub body.\n")});
        QTemporaryDir dir;
        const QString zip = dir.filePath(QStringLiteral("export.zip"));
        {
            QFile f(zip);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(makeZip(files));
        }
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result r = Importer::importPath(ws, zip);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(ws.title(r.rootId), QStringLiteral("Doc"));
        QCOMPARE(r.pages, 2); // no extra "Doc" folder page
        QCOMPARE(r.images, 2);
        QCOMPARE(r.files, 2);
        const QList<const PageMeta *> kids = ws.children(r.rootId);
        QCOMPARE(kids.size(), 1);
        QCOMPARE(kids.first()->title, QStringLiteral("Sub"));
        const QList<Block> b = ws.content(r.rootId);
        QList<BlockType> types;
        for (const Block &x : b)
            types.append(x.type);
        const QList<BlockType> expected = {BlockType::Paragraph, BlockType::Image,  BlockType::Callout, BlockType::Bullet,
                                           BlockType::Bullet,    BlockType::Image,  BlockType::Paragraph, BlockType::Pdf,
                                           BlockType::Paragraph, BlockType::File,   BlockType::PageLink};
        QCOMPARE(types, expected);
        // The callout keeps its title, lists, image and text as children.
        QCOMPARE(plain(b[2].spans), QStringLiteral("Callout title"));
        for (int i = 3; i <= 6; ++i)
            QCOMPARE(b[i].depth, 1);
        // Attachments are stored in the workspace, not linked to the export.
        for (int i : {1, 5, 7, 9})
            QVERIFY2(QFileInfo::exists(ws.mediaPath(b[i].media)), qPrintable(b[i].media));
        QCOMPARE(b[7].data.value(QStringLiteral("name")).toString(), QStringLiteral("report.pdf"));
        QCOMPARE(plain(b[8].spans), QStringLiteral("See the notes for details."));
        QVERIFY(b[8].spans.size() >= 1 && std::none_of(b[8].spans.begin(), b[8].spans.end(), [](const Span &s) { return !s.link.isEmpty(); }));
        QFile pdf(ws.mediaPath(b[7].media));
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QCOMPARE(pdf.readAll(), QByteArray("%PDF-1.4 test"));
    }

    void notionHtmlExportWithRemoteImagesAndInlineDatabase()
    {
        const QString pageId = QStringLiteral("33333333333333333333333333333333");
        const QString dbId = QStringLiteral("44444444444444444444444444444444");
        const QByteArray html = QStringLiteral(
            "<html><head><title>Roadmap</title></head><body><article class=\"page sans\"><header>"
            "<img class=\"page-cover-image\" src=\"https://images.example/cover.gif\" style=\"object-position:center 25%\"/>"
            "<div class=\"page-header-icon page-header-icon-with-cover\"><img class=\"icon\" src=\"https://images.example/icon.gif\"/></div>"
            "<h1 class=\"page-title\">Roadmap</h1><p class=\"page-description\"></p></header><div class=\"page-body\">"
            "<h1>Goals</h1><hr/>"
            "<div class=\"column-list\"><div class=\"column\" data-notion-column-ratio=\"0.25\" style=\"width:25%\"><p></p></div>"
            "<div class=\"column\" data-notion-column-ratio=\"0.75\" style=\"width:75%\">"
            "<div id=\"44444444-4444-4444-4444-444444444444\" class=\"collection-content\"><h4 class=\"collection-title\">Progress</h4>"
            "<table class=\"collection-content\"><thead><tr><th>Name</th></tr></thead><tbody><tr><td class=\"cell-title\">"
            "<a href=\"https://app.notion.com/p/Milestone-55555555555555555555555555555555?pvs=21\">Milestone A</a></td></tr></tbody></table></div>"
            "</div></div>"
            "<ul class=\"to-do-list\"><li><input type=\"checkbox\" class=\"checkbox checkbox-on\" disabled=\"\" checked=\"\"/> "
            "<span class=\"to-do-children-checked\">Phase 1</span><div class=\"indented\">"
            "<ul class=\"to-do-list\"><li><input type=\"checkbox\" class=\"checkbox checkbox-off\" disabled=\"\"/> "
            "<span class=\"to-do-children-unchecked\">Sub task <mark class=\"highlight-yellow\">soon</mark></span><div class=\"indented\"></div></li></ul>"
            "</div></li></ul>"
            "<figure><div class=\"source\"><a href=\"Roadmap/brief.pdf\">brief.pdf</a></div></figure>"
            "<p><img src=\"https://images.example/inline.png\"/></p>"
            "</div></article></body></html>")
                                     .toUtf8();
        QList<QPair<QString, QByteArray>> files{
            {QStringLiteral("Roadmap %1.html").arg(pageId), html},
            {QStringLiteral("Roadmap/Progress %1.csv").arg(dbId), QByteArray("Name,Days left,Finish\nMilestone A,120,\"March 3, 2028\"\nMilestone B,80,\"May 5, 2027\"\n")},
            {QStringLiteral("Roadmap/brief.pdf"), QByteArray("%PDF-1.4 brief")}};
        QTemporaryDir dir;
        const QString zip = dir.filePath(QStringLiteral("export.zip"));
        {
            QFile f(zip);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(makeZip(files));
        }
        // Remote images come through the fetcher the UI installs.
        QStringList fetchedUrls;
        Importer::setFetcher([&](const QString &url, QString *mime) {
            fetchedUrls.append(url);
            *mime = QStringLiteral("image/png");
            return png();
        });
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result r = Importer::importPath(ws, zip);
        Importer::setFetcher({});
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(ws.title(r.rootId), QStringLiteral("Roadmap"));
        QCOMPARE(r.databases, 1);
        QCOMPARE(r.rows, 2);
        // Cover and icon are downloaded and stored locally (identical bytes share one file).
        const PageMeta *m = ws.page(r.rootId);
        QVERIFY(m->cover.startsWith(QStringLiteral("media:")));
        QVERIFY(m->icon.startsWith(QStringLiteral("media:")));
        QVERIFY(QFileInfo::exists(ws.mediaPath(m->cover.mid(6))));
        QCOMPARE(m->coverPos, 0.25);
        QCOMPARE(fetchedUrls.size(), 3);
        const QList<Block> b = ws.content(r.rootId);
        QList<BlockType> types;
        for (const Block &x : b)
            types.append(x.type);
        const QList<BlockType> expected = {BlockType::Heading1, BlockType::Divider, BlockType::ColumnList, BlockType::Todo,
                                           BlockType::Todo,     BlockType::Pdf,     BlockType::Image};
        QCOMPARE(types, expected);
        // Column widths follow Notion's ratios; the inline database is the imported one.
        QCOMPARE(b[2].widths.size(), 2);
        QVERIFY(qAbs(b[2].widths[0] - 0.25) < 0.001);
        QCOMPARE(b[2].columns[1].first().type, BlockType::Database);
        const QString db = b[2].columns[1].first().pageId;
        QVERIFY(ws.page(db) && ws.page(db)->isDatabase());
        QCOMPARE(ws.title(db), QStringLiteral("Progress"));
        QCOMPARE(ws.page(db)->parentId, r.rootId);
        QVERIFY(!b[2].columns[1].first().data.contains(QStringLiteral("notionVisible")));
        // The Notion view showed only Name: the other properties start hidden.
        const Schema schema = ws.schema(db);
        QCOMPARE(schema.props.size(), 3);
        QCOMPARE(schema.views.first().hidden.size(), 2);
        QCOMPARE(schema.orderedProps(schema.views.first()).size(), 1);
        // To-dos keep their state and nesting; Notion's colour classes become shades.
        QVERIFY(b[3].checked);
        QVERIFY(!b[4].checked);
        QCOMPARE(b[4].depth, 1);
        QVERIFY(std::any_of(b[4].spans.begin(), b[4].spans.end(), [](const Span &s) { return s.text == QLatin1String("soon") && s.color > 0 && s.highlight == 0; }));
        QVERIFY(QFileInfo::exists(ws.mediaPath(b[5].media)));
        QVERIFY(QFileInfo::exists(ws.mediaPath(b[6].media)));
    }

    void htmlExportCoversRichBlocks()
    {
        QTemporaryDir dir;
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        QList<Block> blocks;
        Block crumb;
        crumb.type = BlockType::Breadcrumb;
        blocks.append(crumb);
        Block toc;
        toc.type = BlockType::TableOfContents;
        blocks.append(toc);
        Block h = Block::make(BlockType::Heading2, QStringLiteral("Folded"));
        h.toggleable = true;
        h.id = Block::newId();
        blocks.append(h);
        blocks.append(Block::make(BlockType::Paragraph, QStringLiteral("Child"), 1));
        Block eq;
        eq.type = BlockType::Equation;
        eq.data.insert(QStringLiteral("expr"), QStringLiteral("\\frac{a}{b}"));
        blocks.append(eq);
        Block t;
        t.type = BlockType::Table;
        t.data.insert(QStringLiteral("hr"), true);
        t.rows = {{{text(QStringLiteral("K"))}, {text(QStringLiteral("V"))}}, {{text(QStringLiteral("x"))}, {text(QStringLiteral("<1>"))}}};
        blocks.append(t);
        Block cols;
        cols.type = BlockType::ColumnList;
        cols.widths = {0.3, 0.7};
        cols.columns = {{Block::make(BlockType::Paragraph, QStringLiteral("narrow"))}, {Block::make(BlockType::Paragraph, QStringLiteral("wide"))}};
        blocks.append(cols);
        Block bm;
        bm.type = BlockType::Bookmark;
        bm.url = QStringLiteral("https://example.com");
        bm.data.insert(QStringLiteral("title"), QStringLiteral("Example"));
        blocks.append(bm);
        Block mermaid = Block::make(BlockType::Code, QStringLiteral("graph TD") + QChar::LineSeparator + QStringLiteral("A-->B"));
        mermaid.language = QStringLiteral("mermaid");
        blocks.append(mermaid);
        const QString page = ws.createPage({}, QStringLiteral("Rich"), blocks);
        // Without renderers (core only): TeX and Mermaid stay as source.
        Exporter::setRenderers({});
        QString html = Exporter::pageHtml(ws, page, false);
        QVERIFY(html.contains(QStringLiteral("<p class=\"breadcrumb\">Rich</p>")));
        QVERIFY(html.contains(QStringLiteral("<nav class=\"toc\">")));
        QVERIFY(html.contains(QStringLiteral("href=\"#b-%1\"").arg(h.id)));
        QVERIFY(html.contains(QStringLiteral("class=\"toggle-heading\"")));
        QVERIFY(html.contains(QStringLiteral("<pre class=\"tex\">\\frac{a}{b}</pre>")));
        QVERIFY(html.contains(QStringLiteral("<th>K</th>")));
        QVERIFY(html.contains(QStringLiteral("<td>&lt;1&gt;</td>")));
        QVERIFY(html.contains(QStringLiteral("class=\"columns\"")));
        QVERIFY(html.contains(QStringLiteral("flex:0.300")));
        QVERIFY(html.contains(QStringLiteral("class=\"bookmark\" href=\"https://example.com\"")));
        QVERIFY(html.contains(QStringLiteral("graph TD")));
        // With renderers, equations and diagrams become images.
        Exporter::Renderers r;
        r.equation = [](const QString &, bool, const QColor &) {
            QImage img(40, 20, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::white);
            return img;
        };
        r.diagram = [](const QString &, int) {
            QImage img(80, 40, QImage::Format_RGB32);
            img.fill(Qt::black);
            return img;
        };
        Exporter::setRenderers(r);
        html = Exporter::pageHtml(ws, page, false);
        QVERIFY(html.contains(QStringLiteral("<img class=\"equation\" src=\"data:image/png;base64,")));
        QVERIFY(html.contains(QStringLiteral("<img class=\"diagram\" src=\"data:image/png;base64,")));
        QHash<QString, QImage> resources;
        const QString print = Exporter::pageHtml(ws, page, true, &resources);
        QCOMPARE(resources.size(), 2);
        QVERIFY(print.contains(QStringLiteral("src=\"nebula-res:0\"")));
        QVERIFY(print.contains(QStringLiteral("<table class=\"columns\"")));
        const Exporter::Result pdf = Exporter::exportPdf(ws, page, dir.filePath(QStringLiteral("rich.pdf")));
        QVERIFY2(pdf.ok(), qPrintable(pdf.error));
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral("rich.pdf"))).size() > 1000);
        Exporter::setRenderers({});
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    IoTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_io.moc"
