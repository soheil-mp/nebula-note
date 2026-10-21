#include "Importer.h"

#include "Csv.h"
#include "HtmlImport.h"
#include "Markdown.h"
#include "Workspace.h"
#include "Zip.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QImage>
#include <QSet>
#include <QMap>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

namespace Importer
{

namespace
{
Fetcher &fetcher()
{
    static Fetcher f;
    return f;
}

Progress &progress()
{
    static Progress p;
    return p;
}

void report(int done, int total)
{
    if (progress())
        progress()(done, total);
}
} // namespace

void setFetcher(const Fetcher &f)
{
    fetcher() = f;
}

void setProgress(const Progress &p)
{
    progress() = p;
}

QString Result::summary() const
{
    QStringList parts;
    if (pages)
        parts.append(QCoreApplication::translate("Importer", "%n page(s)", nullptr, pages));
    if (databases)
        parts.append(QCoreApplication::translate("Importer", "%n database(s)", nullptr, databases));
    if (rows)
        parts.append(QCoreApplication::translate("Importer", "%n row(s)", nullptr, rows));
    if (images)
        parts.append(QCoreApplication::translate("Importer", "%n image(s)", nullptr, images));
    if (files)
        parts.append(QCoreApplication::translate("Importer", "%n attachment(s)", nullptr, files));
    return parts.join(QStringLiteral(", "));
}

QString stripNotionId(const QString &name)
{
    static const QRegularExpression id(QStringLiteral("\\s+[0-9a-f]{32}$"));
    QString s = name;
    s.remove(id);
    return s.trimmed();
}

namespace
{
QString tr(const char *s)
{
    return QCoreApplication::translate("Importer", s);
}

// All files of the import, by relative path with '/' separators.
struct Source {
    QHash<QString, QByteArray> files;
    QString diskBase; // for single-file imports: resolve missing paths on disk

    QByteArray read(const QString &path) const
    {
        auto it = files.constFind(path);
        if (it != files.constEnd())
            return *it;
        if (!diskBase.isEmpty()) {
            QFile f(QDir(diskBase).filePath(path));
            if (f.size() < 64 * 1024 * 1024 && f.open(QIODevice::ReadOnly))
                return f.readAll();
        }
        return {};
    }
};

void addZip(Source &src, const QByteArray &data, int depth = 0)
{
    ZipReader zip;
    if (!zip.openData(data))
        return;
    for (const QString &name : zip.names()) {
        if (name.startsWith(QLatin1String("__MACOSX/")) || name.endsWith(QLatin1String(".DS_Store")))
            continue;
        if (name.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) && depth < 2) {
            // Large Notion exports come as a zip of zips.
            addZip(src, zip.read(name), depth + 1);
            continue;
        }
        src.files.insert(name, zip.read(name));
    }
}

void addFolder(Source &src, const QString &dir, const QString &prefix = {})
{
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir base(dir);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi(path);
        if (fi.size() > 64 * 1024 * 1024 || fi.fileName().startsWith(QLatin1Char('.')))
            continue;
        const QString rel = prefix + base.relativeFilePath(path);
        if (rel.contains(QLatin1String("/.")))
            continue; // hidden folders (.obsidian, .git)
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            src.files.insert(rel, f.readAll());
    }
}

QString dirOf(const QString &path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? QString() : path.left(slash);
}

QString baseOf(const QString &path)
{
    return path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

QString normalizePath(const QString &path)
{
    QStringList out;
    for (const QString &part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (!out.isEmpty())
                out.removeLast();
            continue;
        }
        out.append(part);
    }
    return out.join(QLatin1Char('/'));
}

QString stripExtension(const QString &path)
{
    const int dot = path.lastIndexOf(QLatin1Char('.'));
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return dot > slash ? path.left(dot) : path;
}

bool isMarkdown(const QString &path)
{
    const QString l = path.toLower();
    return l.endsWith(QLatin1String(".md")) || l.endsWith(QLatin1String(".markdown")) || l.endsWith(QLatin1String(".txt"));
}

bool isHtml(const QString &path)
{
    const QString l = path.toLower();
    return l.endsWith(QLatin1String(".html")) || l.endsWith(QLatin1String(".htm"));
}

bool isImage(const QString &path)
{
    static const QStringList exts = {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                                     QStringLiteral("webp"), QStringLiteral("svg"), QStringLiteral("bmp")};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

struct Item {
    QString key;     // path without extension
    QString mdPath;  // page body (Markdown or HTML)
    bool html = false;
    QString csvPath; // database rows
    QString parentKey;
    QString id;
    QString title;
    bool isRow = false;
    int depth = 0;
};

// Rows of a Notion database page start with "Property: value" lines.
QString stripPropertyLines(const QString &md)
{
    QStringList lines = md.split(QLatin1Char('\n'));
    int i = 0;
    while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
    static const QRegularExpression prop(QStringLiteral("^[^:#>\\-*`|][^:]{0,60}: .*$"));
    int j = i;
    while (j < lines.size() && prop.match(lines[j]).hasMatch())
        ++j;
    if (j > i && (j == lines.size() || lines[j].trimmed().isEmpty()))
        lines = lines.mid(j);
    return lines.join(QLatin1Char('\n'));
}
}

Result importPath(Workspace &ws, const QString &path, const QString &parentId)
{
    Result result;
    const QFileInfo fi(path);
    if (!fi.exists()) {
        result.error = tr("The file doesn't exist.");
        return result;
    }

    // Word documents and Evernote exports are single files with their own formats.
    const QString suffix = fi.suffix().toLower();
    if (suffix == QLatin1String("docx") || suffix == QLatin1String("enex")) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            result.error = f.errorString();
            return result;
        }
        const QByteArray data = f.readAll();
        auto importImage = [&](const QByteArray &bytes, const QString &ext) {
            const QString media = ws.importMediaData(bytes, ext);
            if (!media.isEmpty())
                result.images++;
            return media;
        };
        if (suffix == QLatin1String("docx")) {
            const DocxImport::Result doc = DocxImport::read(data, importImage);
            if (!doc.error.isEmpty()) {
                result.error = doc.error;
                return result;
            }
            ws.batch([&] {
                result.rootId = ws.createPage(parentId, doc.title.isEmpty() ? fi.completeBaseName() : doc.title, doc.blocks);
                result.pages = 1;
            });
            return result;
        }
        const QList<EnexImport::Note> notes = EnexImport::read(data, importImage);
        if (notes.isEmpty()) {
            result.error = tr("No notes were found in the Evernote export.");
            return result;
        }
        ws.batch([&] {
            QString parent = parentId;
            if (notes.size() > 1) {
                parent = ws.createPage(parentId, fi.completeBaseName());
                ws.setIcon(parent, QStringLiteral("glyph:notebook"));
                result.rootId = parent;
                result.pages++;
            }
            for (const EnexImport::Note &n : notes) {
                QList<Block> blocks = n.blocks;
                if (!n.tags.isEmpty()) {
                    // Evernote tags become a line of inline code tags at the top.
                    Block tags = Block::make(BlockType::Paragraph);
                    for (const QString &t : n.tags) {
                        Span sp;
                        sp.text = QLatin1Char('#') + t;
                        sp.code = true;
                        tags.spans.append(sp);
                        tags.spans.append(Span{QStringLiteral(" ")});
                    }
                    blocks.prepend(tags);
                }
                const QString id = ws.createPage(parent, n.title.isEmpty() ? tr("Untitled note") : n.title, blocks);
                result.pages++;
                if (result.rootId.isEmpty())
                    result.rootId = id;
            }
        });
        return result;
    }

    Source src;
    QString containerTitle;
    if (fi.isDir()) {
        addFolder(src, fi.absoluteFilePath());
        containerTitle = stripNotionId(fi.fileName());
    } else if (fi.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            result.error = f.errorString();
            return result;
        }
        addZip(src, f.readAll());
        containerTitle = tr("Notion import");
    } else {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            result.error = f.errorString();
            return result;
        }
        src.files.insert(fi.fileName(), f.readAll());
        src.diskBase = fi.absolutePath();
        // A Notion page's sub-pages sit in a folder with the same name.
        const QString sibling = QDir(fi.absolutePath()).filePath(fi.completeBaseName());
        if (QFileInfo(sibling).isDir())
            addFolder(src, sibling, fi.completeBaseName() + QLatin1Char('/'));
    }
    if (src.files.isEmpty()) {
        result.error = tr("Nothing to import was found.");
        return result;
    }

    // ---- items -------------------------------------------------------------------
    // Text files that a page links to are its attachments, not pages.
    QSet<QString> linkedText;
    if (src.files.size() > 1) {
        static const QRegularExpression link(QStringLiteral("(?:\\]\\(|href=\")([^)\"]+\\.txt)[)\"]"), QRegularExpression::CaseInsensitiveOption);
        for (auto it = src.files.cbegin(); it != src.files.cend(); ++it) {
            const QString &p = it.key();
            if (!(p.endsWith(QLatin1String(".md"), Qt::CaseInsensitive) || isHtml(p)))
                continue;
            const QString text = QString::fromUtf8(it.value());
            for (auto m = link.globalMatch(text); m.hasNext();) {
                const QString rel = QUrl::fromPercentEncoding(m.next().captured(1).toUtf8());
                const QString dir = dirOf(p);
                linkedText.insert(normalizePath(dir.isEmpty() ? rel : dir + QLatin1Char('/') + rel));
            }
        }
    }
    QMap<QString, Item> items;
    for (auto it = src.files.cbegin(); it != src.files.cend(); ++it) {
        const QString &p = it.key();
        if (linkedText.contains(p))
            continue;
        if (isMarkdown(p) || isHtml(p)) {
            Item &item = items[stripExtension(p)];
            item.key = stripExtension(p);
            item.mdPath = p;
            item.html = isHtml(p);
        } else if (p.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) {
            QString key = stripExtension(p);
            const bool all = key.endsWith(QLatin1String("_all"));
            if (all)
                key.chop(4);
            Item &item = items[key];
            item.key = key;
            // Prefer the "_all" file: it isn't limited to one view's filter.
            if (item.csvPath.isEmpty() || all)
                item.csvPath = p;
        }
    }
    if (items.isEmpty()) {
        result.error = tr("No Markdown, HTML or CSV files were found.");
        return result;
    }
    // Newer Notion exports name a page's folder without its id ("DNA/" beside
    // "DNA <id>.md"): such a folder belongs to that page.
    QHash<QString, QString> folderOf; // folder path → item key
    {
        QSet<QString> dirs;
        for (const QString &k : items.keys())
            for (QString d = dirOf(k); !d.isEmpty(); d = dirOf(d))
                dirs.insert(d);
        for (const QString &d : std::as_const(dirs)) {
            if (items.contains(d))
                continue;
            const QString parent = dirOf(d), name = baseOf(d);
            for (const Item &item : std::as_const(items))
                if (dirOf(item.key) == parent && stripNotionId(baseOf(item.key)) == name && baseOf(item.key) != name) {
                    folderOf.insert(d, item.key);
                    break;
                }
        }
    }
    auto ownerOf = [&](const QString &dir) { return folderOf.value(dir, dir); };
    // Folders that hold pages but have no page of their own become pages too.
    const QStringList keys = items.keys();
    for (const QString &k : keys) {
        QString dir = dirOf(k);
        while (!dir.isEmpty() && !items.contains(ownerOf(dir))) {
            Item folder;
            folder.key = dir;
            items.insert(dir, folder);
            dir = dirOf(dir);
        }
    }
    // Notion ids (the 32 hex digits ending a name) → items.
    QHash<QString, QString> byNotionId;
    for (const Item &item : std::as_const(items)) {
        static const QRegularExpression idRe(QStringLiteral("\\s([0-9a-f]{32})$"));
        if (const auto m = idRe.match(baseOf(item.key)); m.hasMatch())
            byNotionId.insert(m.captured(1), item.key);
    }
    for (Item &item : items) {
        item.parentKey = ownerOf(dirOf(item.key));
        item.depth = int(item.key.count(QLatin1Char('/')));
        item.title = stripNotionId(baseOf(item.key));
    }
    for (Item &item : items) {
        auto parent = items.constFind(item.parentKey);
        item.isRow = parent != items.constEnd() && !parent->csvPath.isEmpty() && item.csvPath.isEmpty();
    }

    // A single top-level item goes straight under `parentId`; several get a
    // container page so the import can be found (and trashed) in one place.
    QStringList topLevel;
    for (const Item &item : items)
        if (item.parentKey.isEmpty())
            topLevel.append(item.key);

    ws.batch([&] {
        QString rootParent = parentId;
        if (topLevel.size() > 1) {
            rootParent = ws.createPage(parentId, containerTitle.isEmpty() ? tr("Import") : containerTitle);
            ws.setIcon(rootParent, QStringLiteral("glyph:inbox"));
            result.rootId = rootParent;
            result.pages++;
        }

        QList<Item *> ordered;
        for (Item &item : items)
            ordered.append(&item);
        std::sort(ordered.begin(), ordered.end(), [](const Item *a, const Item *b) {
            if (a->depth != b->depth)
                return a->depth < b->depth;
            return a->key.localeAwareCompare(b->key) < 0;
        });

        // Titles of CSV rows waiting for their page: database key → [(title, row id)].
        QHash<QString, QList<QPair<QString, QString>>> pendingRows;
        // Progress: every item is created, then filled in.
        const int steps = int(ordered.size()) * 2;
        int step = 0;

        for (Item *item : ordered) {
            report(++step, steps);
            if (item->isRow)
                continue;
            const QString parent = item->parentKey.isEmpty() ? rootParent : items[item->parentKey].id;
            if (!item->csvPath.isEmpty()) {
                const QList<QStringList> table = Csv::parse(QString::fromUtf8(src.read(item->csvPath)));
                QList<QJsonObject> rowProps;
                QStringList titles;
                const Schema schema = Csv::schemaFromCsv(table, &rowProps, &titles, item->title);
                item->id = ws.createDatabase(parent, item->title, schema);
                result.databases++;
                for (int r = 0; r < titles.size(); ++r) {
                    const QString rowId = ws.createRow(item->id, titles[r], rowProps.value(r));
                    pendingRows[item->key].append({titles[r], rowId});
                    result.rows++;
                }
            } else {
                item->id = ws.createPage(parent, item->title);
                result.pages++;
            }
            if (result.rootId.isEmpty())
                result.rootId = item->id;
        }
        for (Item *item : ordered) {
            if (!item->isRow)
                continue;
            auto &pending = pendingRows[item->parentKey];
            for (int i = 0; i < pending.size(); ++i) {
                if (pending[i].first.compare(item->title, Qt::CaseInsensitive) == 0) {
                    item->id = pending[i].second;
                    pending.removeAt(i);
                    break;
                }
            }
            if (item->id.isEmpty()) {
                item->id = ws.createRow(items[item->parentKey].id, item->title);
                result.rows++;
            }
        }

        // Titles for Obsidian-style [[wiki links]].
        QHash<QString, QString> byTitle;
        for (const Item &item : items)
            if (!item.id.isEmpty())
                byTitle.insert(item.title.toLower(), item.id);

        // Images: files from the export, or downloaded when they're on the web.
        QHash<QString, QString> fetched; // URL → media
        auto importImage = [&](const QString &dir, const QString &href) -> QString {
            if (href.startsWith(QLatin1String("http://")) || href.startsWith(QLatin1String("https://"))) {
                auto hit = fetched.constFind(href);
                if (hit != fetched.constEnd())
                    return *hit;
                QString media;
                if (fetcher()) {
                    QString mime;
                    const QByteArray data = fetcher()(href, &mime);
                    QImage probe;
                    if (!data.isEmpty() && (mime.startsWith(QLatin1String("image/")) || probe.loadFromData(data))) {
                        QString ext = mime.startsWith(QLatin1String("image/")) ? mime.mid(6).section(QLatin1Char('+'), 0, 0).section(QLatin1Char(';'), 0, 0)
                                                                              : QFileInfo(QUrl(href).path()).suffix();
                        if (ext == QLatin1String("jpeg"))
                            ext = QStringLiteral("jpg");
                        media = ws.importMediaData(data, ext.isEmpty() ? QStringLiteral("png") : ext);
                        if (!media.isEmpty())
                            result.images++;
                    }
                }
                fetched.insert(href, media);
                return media;
            }
            if (href.contains(QLatin1String("://")) || href.startsWith(QLatin1String("data:")))
                return {};
            const QString rel = QUrl::fromPercentEncoding(href.toUtf8());
            const QString full = normalizePath(dir.isEmpty() ? rel : dir + QLatin1Char('/') + rel);
            if (!isImage(full))
                return {};
            const QByteArray data = src.read(full);
            if (data.isEmpty())
                return {};
            const QString media = ws.importMediaData(data, QFileInfo(full).suffix());
            if (!media.isEmpty())
                result.images++;
            return media;
        };

        // ---- content -----------------------------------------------------------------
        for (Item *item : ordered) {
            report(++step, steps);
            if (item->mdPath.isEmpty() || item->id.isEmpty())
                continue;
            QString md = QString::fromUtf8(src.read(item->mdPath));
            QString title;
            if (!item->html) {
                title = Markdown::takeTitle(md);
                if (!title.isEmpty() && title != item->title)
                    ws.setTitle(item->id, title);
                if (item->isRow)
                    md = stripPropertyLines(md);
            }
            static const QRegularExpression wiki(QStringLiteral("\\[\\[([^\\]|#]+)(?:#[^\\]|]*)?(?:\\|([^\\]]+))?\\]\\]"));
            for (auto m = wiki.match(md); m.hasMatch(); m = wiki.match(md, m.capturedStart())) {
                // Unresolved links keep their label (the alias, else the target).
                const QString label = m.captured(2).isEmpty() ? m.captured(1) : m.captured(2);
                md.replace(m.capturedStart(), m.capturedLength(),
                           QStringLiteral("[%1](<wikilink:%2>)").arg(label, m.captured(1).trimmed()));
            }

            const QString dir = dirOf(item->mdPath);
            Markdown::ImportOptions opt;
            auto notionItem = [&](QString id) -> QString {
                id.remove(QLatin1Char('-'));
                id = id.toLower();
                auto it = byNotionId.constFind(id);
                return it == byNotionId.constEnd() ? QString() : items.value(*it).id;
            };
            opt.resolveNotionId = notionItem;
            opt.resolvePage = [&](const QString &href) -> QString {
                if (href.startsWith(QLatin1String("wikilink:")))
                    return byTitle.value(href.mid(9).trimmed().toLower());
                // Links to notion.so / notion.com pages that came along in the export.
                if (href.contains(QLatin1String("notion.so/")) || href.contains(QLatin1String("notion.com/"))) {
                    static const QRegularExpression tail(QStringLiteral("([0-9a-f]{32})(?:[?#].*)?$"));
                    const auto m = tail.match(QUrl(href).path());
                    return m.hasMatch() ? notionItem(m.captured(1)) : QString();
                }
                if (href.contains(QLatin1String("://")) || href.startsWith(QLatin1String("mailto:")))
                    return {};
                QString target = QUrl::fromPercentEncoding(href.toUtf8());
                target = target.left(target.indexOf(QLatin1Char('#')) < 0 ? target.size() : target.indexOf(QLatin1Char('#')));
                const QString full = normalizePath(dir.isEmpty() ? target : dir + QLatin1Char('/') + target);
                QString key = stripExtension(full);
                if (key.endsWith(QLatin1String("_all")))
                    key.chop(4);
                auto found = items.constFind(key);
                if (found != items.constEnd())
                    return found->id;
                return {};
            };
            opt.importImage = [&](const QString &href) -> QString { return importImage(dir, href); };
            opt.importFile = [&](const QString &href, BlockType *type) -> QString {
                // Only files that came with the export: pages and databases are links.
                if (href.contains(QLatin1String("://")) || href.startsWith(QLatin1Char('#')))
                    return {};
                QString rel = QUrl::fromPercentEncoding(href.toUtf8());
                rel = rel.left(rel.indexOf(QLatin1Char('#')) < 0 ? rel.size() : rel.indexOf(QLatin1Char('#')));
                const QString full = normalizePath(dir.isEmpty() ? rel : dir + QLatin1Char('/') + rel);
                if ((isMarkdown(full) && !linkedText.contains(full)) || isHtml(full) || full.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive))
                    return {};
                if (isImage(full)) {
                    *type = BlockType::Image;
                    return importImage(dir, href);
                }
                const QByteArray data = src.read(full);
                if (data.isEmpty())
                    return {};
                const QString ext = QFileInfo(full).suffix().toLower();
                static const QStringList video{QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("webm"), QStringLiteral("mkv"),
                                               QStringLiteral("avi"), QStringLiteral("m4v")};
                static const QStringList audio{QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("ogg"), QStringLiteral("m4a"),
                                               QStringLiteral("flac"), QStringLiteral("aac"), QStringLiteral("opus")};
                *type = ext == QLatin1String("pdf") ? BlockType::Pdf
                        : video.contains(ext)       ? BlockType::Video
                        : audio.contains(ext)       ? BlockType::Audio
                                                    : BlockType::File;
                const QString media = ws.importMediaData(data, ext);
                if (!media.isEmpty())
                    result.files++;
                return media;
            };
            if (item->html) {
                HtmlImport::PageInfo info;
                QList<Block> blocks = HtmlImport::toBlocks(md, opt, &title, &info);
                // Inline databases show the properties the Notion view showed.
                std::function<void(QList<Block> &)> applyViews = [&](QList<Block> &list) {
                    for (Block &b : list) {
                        for (QList<Block> &col : b.columns)
                            applyViews(col);
                        if (b.type != BlockType::Database || !b.data.contains(QStringLiteral("notionVisible")))
                            continue;
                        QStringList names;
                        for (const QJsonValue &v : b.data.take(QStringLiteral("notionVisible")).toArray())
                            names.append(v.toString());
                        Schema schema = ws.schema(b.pageId);
                        if (schema.views.isEmpty())
                            continue;
                        View &view = schema.views.first();
                        view.hidden.clear();
                        view.order.clear();
                        for (const QString &name : names)
                            for (const Property &p : schema.props)
                                if (p.name == name && !view.order.contains(p.id))
                                    view.order.append(p.id);
                        for (const Property &p : schema.props)
                            if (p.type != PropType::Title && !view.order.contains(p.id))
                                view.hidden.append(p.id);
                        ws.setSchema(b.pageId, schema);
                    }
                };
                applyViews(blocks);
                if (!title.isEmpty() && title != item->title)
                    ws.setTitle(item->id, title);
                ws.setContent(item->id, blocks);
                // The page's cover and icon are stored locally too.
                if (!info.cover.isEmpty()) {
                    const QString media = importImage(dir, info.cover);
                    if (!media.isEmpty())
                        ws.setCover(item->id, QStringLiteral("media:") + media, info.coverPosition);
                }
                if (!info.icon.isEmpty()) {
                    if (info.icon.contains(QLatin1Char('/')) || info.icon.contains(QLatin1Char('.'))) {
                        const QString media = importImage(dir, info.icon);
                        if (!media.isEmpty())
                            ws.setIcon(item->id, QStringLiteral("media:") + media);
                    } else if (info.icon.size() <= 8) {
                        ws.setIcon(item->id, info.icon); // an emoji
                    }
                }
            } else {
                ws.setContent(item->id, Markdown::toBlocks(md, opt));
            }
        }
    });

    if (result.rootId.isEmpty())
        result.error = tr("Nothing could be imported.");
    return result;
}

}
