#include "core/Blocks.h"
#include "core/Csv.h"
#include "core/Exporter.h"
#include "core/Importer.h"
#include "core/Markdown.h"
#include "core/Schema.h"
#include "core/Workspace.h"
#include "core/Zip.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTest>

#include <zlib.h>

namespace
{
void writeText(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(text.toUtf8());
}

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
}

class CoreTests : public QObject
{
    Q_OBJECT

private slots:
    void blocksJsonRoundTrip()
    {
        QList<Block> blocks;
        Block h = Block::make(BlockType::Heading1, QStringLiteral("Hello"));
        h.color = 2;
        blocks.append(h);
        Block t = Block::make(BlockType::Todo, QString(), 1);
        t.checked = true;
        Span a{QStringLiteral("bold ")};
        a.bold = true;
        Span m;
        m.text = QString(QChar::ObjectReplacementCharacter);
        m.mention = QStringLiteral("abc");
        Span l{QStringLiteral("link")};
        l.link = QStringLiteral("https://example.com");
        l.highlight = 3;
        t.spans = {a, m, l};
        blocks.append(t);
        Block code = Block::make(BlockType::Code, QStringLiteral("int x;") + QChar::LineSeparator + QStringLiteral("x++;"));
        code.language = QStringLiteral("cpp");
        blocks.append(code);
        Block img;
        img.type = BlockType::Image;
        img.media = QStringLiteral("abc.png");
        img.width = 320;
        blocks.append(img);

        const QList<Block> back = blocksFromJsonString(blocksToJsonString(blocks));
        QCOMPARE(back, blocks);
        QCOMPARE(blocksLinkedPages(back), QStringList{QStringLiteral("abc")});
        QCOMPARE(blocksMedia(back), QStringList{QStringLiteral("abc.png")});
        QVERIFY(blocksPlainText(back).contains(QStringLiteral("bold link")));
    }

    void normalizeMergesSpans()
    {
        Span a{QStringLiteral("a")}, b{QStringLiteral("b")}, c{QStringLiteral("c")};
        c.bold = true;
        const QList<Span> out = normalizeSpans({a, Span{}, b, c});
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].text, QStringLiteral("ab"));
    }

    void markdownImport()
    {
        const QString md = QStringLiteral(
            "# Big\n\nSome **bold** and *italic* and `code` and ~~gone~~ and [a link](https://x.org).\n\n"
            "- one\n  - nested\n- two\n\n1. first\n2. second\n\n- [ ] open\n- [x] done\n\n"
            "> quoted\n> more\n\n```python\nprint('hi')\n\nx = 1\n```\n\n---\n\n"
            "<aside>\n💡 Remember this\n</aside>\n\n<details>\n<summary>More</summary>\n\nHidden text\n\n</details>\n\n"
            "> [!WARNING]\n> Careful\n");
        const QList<Block> b = Markdown::toBlocks(md);
        int i = 0;
        QCOMPARE(b[i].type, BlockType::Heading1);
        QCOMPARE(b[++i].type, BlockType::Paragraph);
        const QList<Span> &s = b[i].spans;
        QVERIFY(std::any_of(s.begin(), s.end(), [](const Span &x) { return x.bold && x.text == QLatin1String("bold"); }));
        QVERIFY(std::any_of(s.begin(), s.end(), [](const Span &x) { return x.italic && x.text == QLatin1String("italic"); }));
        QVERIFY(std::any_of(s.begin(), s.end(), [](const Span &x) { return x.code && x.text == QLatin1String("code"); }));
        QVERIFY(std::any_of(s.begin(), s.end(), [](const Span &x) { return x.strike && x.text == QLatin1String("gone"); }));
        QVERIFY(std::any_of(s.begin(), s.end(), [](const Span &x) { return x.link == QLatin1String("https://x.org"); }));
        QCOMPARE(b[++i].type, BlockType::Bullet);
        QCOMPARE(b[++i].depth, 1);
        QCOMPARE(b[++i].depth, 0);
        QCOMPARE(b[++i].type, BlockType::Numbered);
        QCOMPARE(b[++i].type, BlockType::Numbered);
        QCOMPARE(b[++i].type, BlockType::Todo);
        QVERIFY(!b[i].checked);
        QVERIFY(b[++i].checked);
        QCOMPARE(b[++i].type, BlockType::Quote);
        QVERIFY(b[i].plainText().contains(QChar::LineSeparator));
        QCOMPARE(b[++i].type, BlockType::Code);
        QCOMPARE(b[i].language, QStringLiteral("python"));
        QCOMPARE(b[i].plainText(), QStringLiteral("print('hi')") + QChar::LineSeparator + QChar::LineSeparator + QStringLiteral("x = 1"));
        QCOMPARE(b[++i].type, BlockType::Divider);
        QCOMPARE(b[++i].type, BlockType::Callout);
        QCOMPARE(b[i].icon, QStringLiteral("💡"));
        QCOMPARE(b[i].plainText(), QStringLiteral("Remember this"));
        QCOMPARE(b[++i].type, BlockType::Toggle);
        QCOMPARE(b[i].plainText(), QStringLiteral("More"));
        QCOMPARE(b[++i].depth, 1);
        QCOMPARE(b[i].plainText(), QStringLiteral("Hidden text"));
        QCOMPARE(b[++i].type, BlockType::Callout);
        QCOMPARE(b[i].icon, QStringLiteral("glyph:alert"));
    }

    void markdownRoundTrip()
    {
        QList<Block> blocks;
        blocks.append(Block::make(BlockType::Heading2, QStringLiteral("Section")));
        Block p;
        Span bold{QStringLiteral("strong")};
        bold.bold = true;
        Span plain{QStringLiteral(" words ")};
        Span it{QStringLiteral("slanted")};
        it.italic = true;
        p.spans = {bold, plain, it};
        blocks.append(p);
        blocks.append(Block::make(BlockType::Bullet, QStringLiteral("a")));
        blocks.append(Block::make(BlockType::Bullet, QStringLiteral("b"), 1));
        Block todo = Block::make(BlockType::Todo, QStringLiteral("task"));
        todo.checked = true;
        blocks.append(todo);
        blocks.append(Block::make(BlockType::Toggle, QStringLiteral("Open me")));
        blocks.append(Block::make(BlockType::Paragraph, QStringLiteral("inside"), 1));
        blocks.append(Block::make(BlockType::Quote, QStringLiteral("wise")));
        Block code = Block::make(BlockType::Code, QStringLiteral("a") + QChar::LineSeparator + QStringLiteral("b"));
        code.language = QStringLiteral("js");
        blocks.append(code);
        blocks.append(Block::make(BlockType::Divider));
        blocks.append(Block::make(BlockType::Paragraph, QStringLiteral("2 * 3 = 6 [sic]")));

        const QString md = Markdown::fromBlocks(blocks);
        const QList<Block> back = Markdown::toBlocks(md);
        QCOMPARE(back.size(), blocks.size());
        for (int i = 0; i < blocks.size(); ++i) {
            QCOMPARE(back[i].type, blocks[i].type);
            QCOMPARE(back[i].depth, blocks[i].depth);
            QCOMPARE(back[i].plainText(), blocks[i].plainText());
        }
        QVERIFY(back[1].spans[0].bold);
        QVERIFY(back[1].spans[2].italic);
        QVERIFY(back[4].checked);
    }

    void markdownTakeTitle()
    {
        QString md = QStringLiteral("\n# My **Page**\n\nBody");
        QCOMPARE(Markdown::takeTitle(md), QStringLiteral("My Page"));
        QVERIFY(!md.contains(QLatin1String("# My")));
    }

    void csvParseAndInfer()
    {
        const QString csv = QStringLiteral("\xEF\xBB\xBFName,Status,Tags,Due,Done,Score,Site\n"
                                           "Alpha,Done,\"a, b\",\"October 16, 2026\",Yes,3,https://a.io\n"
                                           "Beta,In progress,b,2026-10-20,No,4.5,https://b.io\n"
                                           "\"Gam\"\"ma\",Not started,\"a, c\",,No,10,https://c.io\n");
        const QList<QStringList> t = Csv::parse(QString::fromUtf8(csv.toLatin1()));
        QCOMPARE(t.size(), 4);
        QCOMPARE(t[3][0], QStringLiteral("Gam\"ma"));
        QList<QJsonObject> rows;
        QStringList titles;
        const Schema s = Csv::schemaFromCsv(t, &rows, &titles);
        QCOMPARE(s.props[0].type, PropType::Title);
        QCOMPARE(s.props[1].type, PropType::Status);
        QCOMPARE(s.props[2].type, PropType::MultiSelect);
        QCOMPARE(s.props[3].type, PropType::Date);
        QCOMPARE(s.props[4].type, PropType::Checkbox);
        QCOMPARE(s.props[5].type, PropType::Number);
        QCOMPARE(s.props[6].type, PropType::Url);
        QCOMPARE(titles, (QStringList{QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Gam\"ma")}));
        QCOMPARE(rows[0][s.props[3].id].toString(), QStringLiteral("2026-10-16"));
        QCOMPARE(rows[0][s.props[2].id].toArray().size(), 2);
        // Status groups come from option names.
        QCOMPARE(s.props[1].optionByName(QStringLiteral("Done"))->group, 2);
        QCOMPARE(s.props[1].optionByName(QStringLiteral("In progress"))->group, 1);
        QCOMPARE(Csv::parse(Csv::write(t)), t);
    }

    void schemaFiltersAndSorts()
    {
        Schema s = Schema::basic();
        Property n;
        n.id = QStringLiteral("n");
        n.name = QStringLiteral("Points");
        n.type = PropType::Number;
        s.props.append(n);
        Property st;
        st.id = QStringLiteral("st");
        st.name = QStringLiteral("Stage");
        st.type = PropType::Select;
        s.props.append(st);
        Property &stage = *s.prop(QStringLiteral("st"));
        const QString a = parseValue(stage, QStringLiteral("Alpha")).toString();
        const QString b = parseValue(stage, QStringLiteral("Beta")).toString();
        QCOMPARE(stage.options.size(), 2);

        PageMeta m1, m2, m3;
        m1.id = QStringLiteral("1");
        m1.title = QStringLiteral("one");
        m1.sort = 1;
        m2.id = QStringLiteral("2");
        m2.title = QStringLiteral("two");
        m2.sort = 2;
        m3.id = QStringLiteral("3");
        m3.title = QStringLiteral("three");
        m3.sort = 3;
        QList<RowRef> rows = {{&m1, QJsonObject{{QStringLiteral("n"), 5}, {QStringLiteral("st"), a}}},
                              {&m2, QJsonObject{{QStringLiteral("n"), 2}, {QStringLiteral("st"), b}}},
                              {&m3, QJsonObject{{QStringLiteral("st"), a}}}};
        View v = s.views.first();
        v.sorts = {{QStringLiteral("n"), false}};
        QList<RowRef> out = applyView(s, v, rows);
        QCOMPARE(out[0].meta->id, QStringLiteral("2"));
        QCOMPARE(out[2].meta->id, QStringLiteral("3")); // empty sinks
        v.sorts = {{QStringLiteral("n"), true}};
        out = applyView(s, v, rows);
        QCOMPARE(out[0].meta->id, QStringLiteral("1"));
        QCOMPARE(out[2].meta->id, QStringLiteral("3"));

        v.sorts.clear();
        v.filters = {{QStringLiteral("st"), QStringLiteral("is"), a}};
        QCOMPARE(applyView(s, v, rows).size(), 2);
        v.filters = {{QStringLiteral("n"), QStringLiteral("gt"), 3}};
        QCOMPARE(applyView(s, v, rows).size(), 1);
        v.filters = {{QStringLiteral("n"), QStringLiteral("empty"), QJsonValue()}};
        QCOMPARE(applyView(s, v, rows).size(), 1);
        v.filters.clear();
        QCOMPARE(applyView(s, v, rows, QStringLiteral("tw")).size(), 1);

        const Schema back = Schema::fromJson(s.toJson());
        QCOMPARE(back.props.size(), s.props.size());
        QCOMPARE(back.prop(QStringLiteral("st"))->options.size(), 2);
    }

    void numberAndDateFormatting()
    {
        QCOMPARE(formatNumber(1234.5, QStringLiteral("number")), QStringLiteral("1234.5"));
        QCOMPARE(formatNumber(1234.5, QStringLiteral("comma")), QStringLiteral("1,234.5"));
        QCOMPARE(formatNumber(0.25, QStringLiteral("percent")), QStringLiteral("25%"));
        QCOMPARE(formatNumber(3, QStringLiteral("dollar")), QStringLiteral("$3.00"));
        QCOMPARE(formatDateValue(QStringLiteral("2026-10-16")), QStringLiteral("Oct 16, 2026"));
        QCOMPARE(formatDateValue(QStringLiteral("2026-10-16T14:30")), QStringLiteral("Oct 16, 2026 2:30 PM"));
        Property d;
        d.type = PropType::Date;
        QCOMPARE(parseValue(d, QStringLiteral("October 16, 2026 2:30 PM")).toString(), QStringLiteral("2026-10-16T14:30"));
        QCOMPARE(parseValue(d, QStringLiteral("October 16, 2026 → October 18, 2026")).toString(), QStringLiteral("2026-10-16/2026-10-18"));
    }

    void workspaceCrudTrashAndLinks()
    {
        QTemporaryDir dir;
        Workspace ws;
        QVERIFY(ws.open(dir.path()));
        QVERIFY(ws.isFresh());
        const QString a = ws.createPage({}, QStringLiteral("A"));
        const QString b = ws.createPage({}, QStringLiteral("B"));
        const QString a1 = ws.createPage(a, QStringLiteral("A1"));
        QCOMPARE(ws.children({}).size(), 2);
        QCOMPARE(ws.children(a).size(), 1);
        QCOMPARE(ws.ancestors(a1), QStringList{a});

        // Links feed backlinks.
        Block blk;
        Span m;
        m.text = QString(QChar::ObjectReplacementCharacter);
        m.mention = a1;
        blk.spans = {m};
        ws.setContent(b, {blk});
        QCOMPARE(ws.backlinks(a1), QStringList{b});

        // Moving refuses cycles; reordering works.
        QVERIFY(!ws.move(a, a1));
        QVERIFY(ws.move(b, {}, 0));
        QCOMPARE(ws.children({}).first()->id, b);

        // Trash takes the subtree; restore brings it back.
        ws.trash(a);
        QCOMPARE(ws.children({}).size(), 1);
        QVERIFY(ws.page(a1)->isTrashed());
        QCOMPARE(ws.trashedRoots().size(), 1);
        QVERIFY(ws.backlinks(a1).contains(b)); // b is still live
        ws.restore(a);
        QVERIFY(!ws.page(a1)->isTrashed());
        ws.trash(a1);
        ws.deleteForever(a1);
        QVERIFY(!ws.exists(a1));

        // Favorites and search.
        ws.setFavorite(b, true);
        QCOMPARE(ws.favorites().size(), 1);
        ws.setContent(a, {Block::make(BlockType::Paragraph, QStringLiteral("The nebula glows softly"))});
        const auto hits = ws.search(QStringLiteral("glows"));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().id, a);
        QVERIFY(hits.first().snippet.contains(QStringLiteral("glows")));
        QCOMPARE(ws.search(QStringLiteral("B")).first().id, b);

        // Duplicate copies the subtree with new ids.
        const QString a2 = ws.createPage(a, QStringLiteral("Child"));
        const QString copy = ws.duplicate(a);
        QVERIFY(!copy.isEmpty() && copy != a);
        QCOMPARE(ws.children(copy).size(), 1);
        QVERIFY(ws.children(copy).first()->id != a2);
        QCOMPARE(ws.content(copy).first().plainText(), QStringLiteral("The nebula glows softly"));

        // Reopening reads everything back.
        ws.close();
        Workspace again;
        QVERIFY(again.open(dir.path()));
        QVERIFY(!again.isFresh());
        QCOMPARE(again.title(a), QStringLiteral("A"));
        QCOMPARE(again.favorites().size(), 1);
    }

    void databasesAndRows()
    {
        QTemporaryDir dir;
        Workspace ws;
        QVERIFY(ws.open(dir.path()));
        Schema s = Schema::basic();
        const QString db = ws.createDatabase({}, QStringLiteral("Tasks"), s);
        const QString r1 = ws.createRow(db, QStringLiteral("Write"));
        const QString r2 = ws.createRow(db, QStringLiteral("Read"));
        QCOMPARE(ws.rows(db).size(), 2);
        QCOMPARE(ws.children(db).size(), 0); // rows aren't sidebar children
        ws.setProp(r1, QStringLiteral("tags"), QJsonArray{QStringLiteral("x")});
        QCOMPARE(ws.props(r1).value(QStringLiteral("tags")).toArray().size(), 1);
        ws.moveRow(r2, r1);
        QCOMPARE(ws.rows(db).first()->id, r2);
        Schema back = ws.schema(db);
        QCOMPARE(back.props.size(), 2);

        // Trashing the database takes its rows; restoring a lone row makes it a page.
        ws.trash(r1);
        ws.trash(db);
        ws.restore(r1);
        QCOMPARE(ws.page(r1)->kind, PageKind::Page);
        QVERIFY(ws.page(r1)->parentId.isEmpty());
    }

    void zipReader()
    {
        const QByteArray big(100000, 'z');
        const QByteArray zip = makeZip({{QStringLiteral("a.txt"), QByteArray("hello")}, {QStringLiteral("dir/b.bin"), big}});
        ZipReader r;
        QVERIFY(r.openData(zip));
        QCOMPARE(r.names().size(), 2);
        QCOMPARE(r.read(QStringLiteral("a.txt")), QByteArray("hello"));
        QCOMPARE(r.read(QStringLiteral("dir/b.bin")), big);
    }

    void notionImport()
    {
        const QString id1 = QStringLiteral("0123456789abcdef0123456789abcdef");
        const QString id2 = QStringLiteral("fedcba9876543210fedcba9876543210");
        const QString id3 = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        const QString id4 = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        QList<QPair<QString, QByteArray>> files;
        files.append({QStringLiteral("Home %1.md").arg(id1),
                      QStringLiteral("# Home\n\nWelcome!\n\n[Tasks](Home%20%1/Tasks%20%2.csv)\n\n"
                                     "See [Notes](Home%20%1/Notes%20%3.md) too.\n\n![pic](Home%20%1/pic.png)\n")
                          .arg(id1, id2, id3)
                          .toUtf8()});
        files.append({QStringLiteral("Home %1/Notes %2.md").arg(id1, id3), QByteArray("# Notes\n\n- a\n- b\n")});
        files.append({QStringLiteral("Home %1/Tasks %2.csv").arg(id1, id2),
                      QByteArray("Name,Status,Due\nShip it,Done,\"October 1, 2026\"\nPlan,Not started,\n")});
        files.append({QStringLiteral("Home %1/Tasks %2_all.csv").arg(id1, id2),
                      QByteArray("Name,Status,Due\nShip it,Done,\"October 1, 2026\"\nPlan,Not started,\nExtra,Done,\n")});
        files.append({QStringLiteral("Home %1/Tasks %2/Ship it %3.md").arg(id1, id2, id4),
                      QByteArray("# Ship it\n\nStatus: Done\nDue: October 1, 2026\n\nThe body of the row.\n")});
        QImage img(4, 4, QImage::Format_RGB32);
        img.fill(Qt::gray);
        QByteArray png;
        {
            QBuffer buf(&png);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
        }
        files.append({QStringLiteral("Home %1/pic.png").arg(id1), png});

        QTemporaryDir dir;
        const QString zipPath = dir.filePath(QStringLiteral("export.zip"));
        {
            QFile f(zipPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(makeZip(files));
        }
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result r = Importer::importPath(ws, zipPath);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(ws.title(r.rootId), QStringLiteral("Home"));
        QCOMPARE(r.databases, 1);
        QCOMPARE(r.rows, 3); // the _all file wins
        QCOMPARE(r.images, 1);

        const auto kids = ws.children(r.rootId);
        QCOMPARE(kids.size(), 2);
        QString dbId, notesId;
        for (const PageMeta *k : kids)
            (k->isDatabase() ? dbId : notesId) = k->id;
        QCOMPARE(ws.title(notesId), QStringLiteral("Notes"));
        QCOMPARE(ws.content(notesId).size(), 2);

        const QList<Block> home = ws.content(r.rootId);
        // "[Tasks](…csv)" alone on a line is a page block; the inline link is a mention.
        QVERIFY(std::any_of(home.begin(), home.end(), [&](const Block &b) { return b.type == BlockType::PageLink && b.pageId == dbId; }));
        QVERIFY(std::any_of(home.begin(), home.end(), [&](const Block &b) {
            return std::any_of(b.spans.begin(), b.spans.end(), [&](const Span &s) { return s.mention == notesId; });
        }));
        QVERIFY(std::any_of(home.begin(), home.end(), [](const Block &b) { return b.type == BlockType::Image && !b.media.isEmpty(); }));
        QVERIFY(ws.backlinks(notesId).contains(r.rootId));

        const Schema schema = ws.schema(dbId);
        QCOMPARE(schema.props.size(), 3);
        QCOMPARE(schema.props[1].type, PropType::Status);
        QString shipId;
        for (const PageMeta *row : ws.rows(dbId))
            if (row->title == QLatin1String("Ship it"))
                shipId = row->id;
        QVERIFY(!shipId.isEmpty());
        const QList<Block> body = ws.content(shipId);
        QCOMPARE(body.size(), 1); // property lines are stripped
        QCOMPARE(body.first().plainText(), QStringLiteral("The body of the row."));
        QCOMPARE(ws.props(shipId).value(schema.props[2].id).toString(), QStringLiteral("2026-10-01"));
    }

    void markdownFolderWithWikiLinks()
    {
        QTemporaryDir dir;
        writeText(dir.filePath(QStringLiteral("vault/Index.md")), QStringLiteral("Go to [[Ideas]] or [[Missing|nowhere]]."));
        writeText(dir.filePath(QStringLiteral("vault/sub/Ideas.md")), QStringLiteral("# Ideas\n\nStuff"));
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const Importer::Result r = Importer::importPath(ws, dir.filePath(QStringLiteral("vault")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QString indexId;
        for (const PageMeta *m : ws.allLive())
            if (m->title == QLatin1String("Index"))
                indexId = m->id;
        QVERIFY(!indexId.isEmpty());
        const QList<Block> c = ws.content(indexId);
        QCOMPARE(c.size(), 1);
        bool mention = false;
        QString text;
        for (const Span &s : c.first().spans) {
            mention |= !s.mention.isEmpty();
            text += s.text;
        }
        QVERIFY(mention);
        QVERIFY(text.contains(QStringLiteral("nowhere")));
    }

    void exportMarkdownTreeAndHtml()
    {
        QTemporaryDir dir;
        Workspace ws;
        QVERIFY(ws.open(dir.filePath(QStringLiteral("ws"))));
        const QString root = ws.createPage({}, QStringLiteral("Root / Page"));
        const QString child = ws.createPage(root, QStringLiteral("Child"));
        Block link;
        link.type = BlockType::PageLink;
        link.pageId = child;
        ws.setContent(root, {Block::make(BlockType::Heading1, QStringLiteral("Hi")), link});
        const QString db = ws.createDatabase(root, QStringLiteral("Table"), Schema::basic());
        ws.createRow(db, QStringLiteral("Row 1"));

        const QString out = dir.filePath(QStringLiteral("out"));
        const Exporter::Result r = Exporter::exportMarkdown(ws, {root}, out);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(QFile::exists(out + QStringLiteral("/Root Page.md")));
        QVERIFY(QFile::exists(out + QStringLiteral("/Root Page/Child.md")));
        QVERIFY(QFile::exists(out + QStringLiteral("/Root Page/Table.csv")));
        QVERIFY(QFile::exists(out + QStringLiteral("/Root Page/Table/Row 1.md")));
        QFile f(out + QStringLiteral("/Root Page.md"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString md = QString::fromUtf8(f.readAll());
        QVERIFY(md.contains(QStringLiteral("[Child](Root%20Page/Child.md)")));

        // Exported Markdown imports back into the same shape.
        Workspace ws2;
        QVERIFY(ws2.open(dir.filePath(QStringLiteral("ws2"))));
        const Importer::Result back = Importer::importPath(ws2, out + QStringLiteral("/Root Page.md"));
        QVERIFY2(back.ok(), qPrintable(back.error));
        QCOMPARE(ws2.children(back.rootId).size(), 2);

        const QString html = Exporter::pageHtml(ws, root, false);
        QVERIFY(html.contains(QRegularExpression(QStringLiteral("<h1( id=\"b-[^\"]+\")?>Hi</h1>")))); // headings carry anchors for the TOC
        const Exporter::Result pdf = Exporter::exportPdf(ws, root, dir.filePath(QStringLiteral("p.pdf")));
        QVERIFY2(pdf.ok(), qPrintable(pdf.error));
        QVERIFY(QFileInfo(dir.filePath(QStringLiteral("p.pdf"))).size() > 500);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    // Settings go to Qt's test locations, never the user's config.
    QStandardPaths::setTestModeEnabled(true);
    CoreTests t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_core.moc"
