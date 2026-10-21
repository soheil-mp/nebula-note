#include "Workspace.h"

#include "Ids.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

namespace
{
QPointer<Workspace> g_instance;
int g_connectionCounter = 0;
constexpr int kSchemaVersion = 1;

QString toJsonString(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QJsonObject fromJsonString(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

QString escapeLike(QString s)
{
    s.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    s.replace(QLatin1Char('%'), QStringLiteral("\\%"));
    s.replace(QLatin1Char('_'), QStringLiteral("\\_"));
    return s;
}
}

Workspace::Workspace(QObject *parent)
    : QObject(parent)
{
    g_instance = this;
}

Workspace::~Workspace()
{
    close();
}

Workspace *Workspace::instance()
{
    return g_instance.data();
}

bool Workspace::open(const QString &directory)
{
    close();
    m_dir = directory;
    QDir().mkpath(m_dir);
    QDir().mkpath(mediaDir());
    const QString file = QDir(m_dir).filePath(QStringLiteral("workspace.db"));
    m_fresh = !QFileInfo::exists(file);
    m_connection = QStringLiteral("nebula-%1").arg(++g_connectionCounter);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(file);
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return false;
    }
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    exec(QStringLiteral("PRAGMA busy_timeout=3000"));
    const bool ok = exec(QStringLiteral(R"(CREATE TABLE IF NOT EXISTS pages(
            id TEXT PRIMARY KEY,
            parent_id TEXT NOT NULL DEFAULT '',
            kind INTEGER NOT NULL DEFAULT 0,
            title TEXT NOT NULL DEFAULT '',
            icon TEXT NOT NULL DEFAULT '',
            cover TEXT NOT NULL DEFAULT '',
            cover_pos REAL NOT NULL DEFAULT 0.5,
            content TEXT NOT NULL DEFAULT '[]',
            plain TEXT NOT NULL DEFAULT '',
            props TEXT NOT NULL DEFAULT '{}',
            schema TEXT NOT NULL DEFAULT '',
            options TEXT NOT NULL DEFAULT '{}',
            sort REAL NOT NULL DEFAULT 0,
            favorite REAL NOT NULL DEFAULT 0,
            created INTEGER NOT NULL DEFAULT 0,
            updated INTEGER NOT NULL DEFAULT 0,
            visited INTEGER NOT NULL DEFAULT 0,
            trashed INTEGER NOT NULL DEFAULT 0))"))
                    && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS pages_parent ON pages(parent_id)"))
                    && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS links(src TEXT NOT NULL, dst TEXT NOT NULL, PRIMARY KEY(src, dst))"))
                    && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS links_dst ON links(dst)"))
                    && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT)"))
                    && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS synced(id TEXT PRIMARY KEY, origin TEXT NOT NULL DEFAULT '', "
                                           "content TEXT NOT NULL DEFAULT '[]', updated INTEGER NOT NULL DEFAULT 0)"))
                    && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS comments(id TEXT PRIMARY KEY, thread TEXT NOT NULL, page_id TEXT NOT NULL, "
                                           "block_id TEXT NOT NULL DEFAULT '', quote TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '', "
                                           "created INTEGER NOT NULL DEFAULT 0, edited INTEGER NOT NULL DEFAULT 0, resolved INTEGER NOT NULL DEFAULT 0)"))
                    && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS comments_page ON comments(page_id)"))
                    && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS versions(id INTEGER PRIMARY KEY AUTOINCREMENT, page_id TEXT NOT NULL, "
                                           "created INTEGER NOT NULL, title TEXT NOT NULL DEFAULT '', content TEXT NOT NULL DEFAULT '[]', "
                                           "name TEXT NOT NULL DEFAULT '')"))
                    && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS versions_page ON versions(page_id)"))
                    && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS reminders(id TEXT PRIMARY KEY, page_id TEXT NOT NULL, text TEXT NOT NULL DEFAULT '', "
                                           "at INTEGER NOT NULL, done INTEGER NOT NULL DEFAULT 0)"));
    if (!ok)
        return false;
    if (metaValue(QStringLiteral("schema")).isEmpty())
        setMetaValue(QStringLiteral("schema"), QString::number(kSchemaVersion));
    loadMeta();
    g_instance = this;
    setDbLookup(this);
    return true;
}

void Workspace::close()
{
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connection);
    }
    m_pages.clear();
    m_children.clear();
    m_childIndexDirty = true;
    m_schemaCache.clear();
    m_propsCache.clear();
    if (dbLookup() == this)
        setDbLookup(nullptr);
}

QString Workspace::mediaDir() const
{
    return QDir(m_dir).filePath(QStringLiteral("media"));
}

bool Workspace::exec(const QString &sql, const QVariantList &args) const
{
    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        const_cast<Workspace *>(this)->m_error = q.lastError().text();
        qWarning("Nebula Note: SQL prepare failed: %s (%s)", qPrintable(m_error), qPrintable(sql.left(80)));
        return false;
    }
    for (const QVariant &a : args) {
        // A null QString binds as SQL NULL; the schema wants empty strings.
        if (a.typeId() == QMetaType::QString && a.toString().isNull())
            q.addBindValue(QStringLiteral(""));
        else
            q.addBindValue(a);
    }
    if (!q.exec()) {
        const_cast<Workspace *>(this)->m_error = q.lastError().text();
        qWarning("Nebula Note: SQL failed: %s (%s)", qPrintable(m_error), qPrintable(sql.left(80)));
        return false;
    }
    return true;
}

QString Workspace::metaValue(const QString &key, const QString &fallback) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return fallback;
}

void Workspace::setMetaValue(const QString &key, const QString &value)
{
    exec(QStringLiteral("INSERT INTO meta(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"),
         {key, value});
}

QString Workspace::name() const
{
    return metaValue(QStringLiteral("name"), tr("My Nebula Note"));
}

void Workspace::setName(const QString &name)
{
    setMetaValue(QStringLiteral("name"), name.trimmed().isEmpty() ? tr("My Nebula Note") : name.trimmed());
    emit pageChanged(QString());
}

void Workspace::loadMeta()
{
    m_pages.clear();
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, parent_id, kind, title, icon, cover, cover_pos, sort, favorite, created, updated, "
                          "visited, trashed FROM pages"));
    while (q.next()) {
        PageMeta m;
        m.id = q.value(0).toString();
        m.parentId = q.value(1).toString();
        m.kind = PageKind(q.value(2).toInt());
        m.title = q.value(3).toString();
        m.icon = q.value(4).toString();
        m.cover = q.value(5).toString();
        m.coverPos = q.value(6).toDouble();
        m.sort = q.value(7).toDouble();
        m.favorite = q.value(8).toDouble();
        m.created = q.value(9).toLongLong();
        m.updated = q.value(10).toLongLong();
        m.visited = q.value(11).toLongLong();
        m.trashed = q.value(12).toLongLong();
        m_pages.insert(m.id, m);
    }
    m_childIndexDirty = true;
}

void Workspace::rebuildChildIndex() const
{
    if (!m_childIndexDirty)
        return;
    m_children.clear();
    for (const PageMeta &m : m_pages)
        m_children[m.parentId].append(m.id);
    for (auto it = m_children.begin(); it != m_children.end(); ++it) {
        std::sort(it->begin(), it->end(), [this](const QString &a, const QString &b) {
            const PageMeta &x = *m_pages.constFind(a), &y = *m_pages.constFind(b);
            if (x.sort != y.sort)
                return x.sort < y.sort;
            return x.created < y.created;
        });
    }
    m_childIndexDirty = false;
}

void Workspace::emitLater(const std::function<void()> &signal)
{
    if (m_batchDepth > 0)
        m_pendingSignals.append(signal);
    else
        signal();
}

void Workspace::batch(const std::function<void()> &fn)
{
    if (m_batchDepth++ == 0)
        m_db.transaction();
    fn();
    if (--m_batchDepth == 0) {
        m_db.commit();
        const auto pending = std::move(m_pendingSignals);
        m_pendingSignals.clear();
        for (const auto &s : pending)
            s();
    }
}

// ---- metadata ------------------------------------------------------------------

const PageMeta *Workspace::page(const QString &id) const
{
    if (id.isEmpty())
        return nullptr;
    auto it = m_pages.constFind(id);
    return it == m_pages.constEnd() ? nullptr : &it.value();
}

QString Workspace::title(const QString &id) const
{
    const PageMeta *m = page(id);
    if (!m)
        return tr("Missing page");
    if (!m->title.trimmed().isEmpty())
        return m->title;
    // Like Notion: an untitled page is a "New page"; rows and databases stay "Untitled".
    return m->isRow() || m->isDatabase() ? tr("Untitled") : tr("New page");
}

QList<const PageMeta *> Workspace::children(const QString &parentId) const
{
    rebuildChildIndex();
    QList<const PageMeta *> out;
    for (const QString &id : m_children.value(parentId)) {
        // constFind: operator[] on a const hash would return a temporary copy.
        const PageMeta &m = *m_pages.constFind(id);
        if (!m.isTrashed() && m.kind != PageKind::Row && m.kind != PageKind::Template)
            out.append(&m);
    }
    return out;
}

QList<const PageMeta *> Workspace::rows(const QString &databaseId) const
{
    rebuildChildIndex();
    QList<const PageMeta *> out;
    for (const QString &id : m_children.value(databaseId)) {
        const PageMeta &m = *m_pages.constFind(id);
        if (!m.isTrashed() && m.kind == PageKind::Row)
            out.append(&m);
    }
    return out;
}

bool Workspace::hasChildren(const QString &id) const
{
    rebuildChildIndex();
    for (const QString &c : m_children.value(id)) {
        const PageMeta &m = *m_pages.constFind(c);
        if (!m.isTrashed() && m.kind != PageKind::Row && m.kind != PageKind::Template)
            return true;
    }
    return false;
}

QList<const PageMeta *> Workspace::favorites() const
{
    QList<const PageMeta *> out;
    for (const PageMeta &m : m_pages)
        if (m.favorite > 0 && !m.isTrashed())
            out.append(&m);
    std::sort(out.begin(), out.end(), [](const PageMeta *a, const PageMeta *b) { return a->favorite < b->favorite; });
    return out;
}

QList<const PageMeta *> Workspace::recent(int limit) const
{
    QList<const PageMeta *> out;
    for (const PageMeta &m : m_pages)
        if (m.visited > 0 && !m.isTrashed())
            out.append(&m);
    std::sort(out.begin(), out.end(), [](const PageMeta *a, const PageMeta *b) { return a->visited > b->visited; });
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

QList<const PageMeta *> Workspace::trashedRoots() const
{
    QList<const PageMeta *> out;
    for (const PageMeta &m : m_pages) {
        if (!m.isTrashed())
            continue;
        const PageMeta *parent = page(m.parentId);
        if (!parent || !parent->isTrashed() || parent->trashed != m.trashed)
            out.append(&m);
    }
    std::sort(out.begin(), out.end(), [](const PageMeta *a, const PageMeta *b) { return a->trashed > b->trashed; });
    return out;
}

QList<const PageMeta *> Workspace::allLive() const
{
    QList<const PageMeta *> out;
    for (const PageMeta &m : m_pages)
        if (!m.isTrashed())
            out.append(&m);
    return out;
}

QStringList Workspace::ancestors(const QString &id) const
{
    QStringList chain;
    const PageMeta *m = page(id);
    QSet<QString> seen;
    while (m && !m->parentId.isEmpty() && !seen.contains(m->parentId)) {
        seen.insert(m->parentId);
        const PageMeta *p = page(m->parentId);
        if (!p)
            break;
        chain.prepend(p->id);
        m = p;
    }
    return chain;
}

QStringList Workspace::subtree(const QString &id, bool includeTrashed) const
{
    rebuildChildIndex();
    QStringList out;
    QStringList stack{id};
    QSet<QString> seen;
    while (!stack.isEmpty()) {
        const QString cur = stack.takeLast();
        if (seen.contains(cur) || !m_pages.contains(cur))
            continue;
        seen.insert(cur);
        if (!includeTrashed && m_pages[cur].isTrashed())
            continue;
        out.append(cur);
        const QStringList kids = m_children.value(cur);
        for (auto it = kids.crbegin(); it != kids.crend(); ++it)
            stack.append(*it);
    }
    return out;
}

bool Workspace::isInside(const QString &id, const QString &ancestorId) const
{
    return id == ancestorId || ancestors(id).contains(ancestorId);
}

// ---- create --------------------------------------------------------------------

double Workspace::sortAfter(const QString &parentId, const QString &afterId, bool rowsOnly) const
{
    rebuildChildIndex();
    QList<double> sorts;
    int afterIndex = -1;
    for (const QString &c : m_children.value(parentId)) {
        const PageMeta &m = *m_pages.constFind(c);
        if ((m.kind == PageKind::Row) != rowsOnly)
            continue;
        if (c == afterId)
            afterIndex = int(sorts.size());
        sorts.append(m.sort);
    }
    if (sorts.isEmpty())
        return 1.0;
    if (afterIndex < 0 || afterIndex == sorts.size() - 1)
        return sorts.last() + 1.0;
    return (sorts[afterIndex] + sorts[afterIndex + 1]) / 2.0;
}

QString Workspace::insertPage(const PageMeta &meta, const QString &content, const QJsonObject &props, const Schema *schema)
{
    const QList<Block> blocks = blocksFromJsonString(content);
    const bool ok = exec(QStringLiteral("INSERT INTO pages(id, parent_id, kind, title, icon, cover, cover_pos, content, "
                                        "plain, props, schema, options, sort, favorite, created, updated, visited, trashed) "
                                        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
                         {meta.id, meta.parentId, int(meta.kind), meta.title, meta.icon, meta.cover, meta.coverPos, content,
                          blocksPlainText(blocks), toJsonString(props), schema ? toJsonString(schema->toJson()) : QString(),
                          QStringLiteral("{}"), meta.sort, meta.favorite, meta.created, meta.updated, meta.visited,
                          meta.trashed});
    if (!ok)
        return {};
    m_pages.insert(meta.id, meta);
    m_schemaCache.remove(meta.id);
    m_propsCache.remove(meta.id);
    m_childIndexDirty = true;
    updateLinks(meta.id, blocks);
    return meta.id;
}

QString Workspace::createPage(const QString &parentId, const QString &title, const QList<Block> &content,
                              const QString &afterId)
{
    PageMeta m;
    m.id = Ids::newPageId();
    m.parentId = parentId;
    m.kind = PageKind::Page;
    m.title = title;
    m.sort = sortAfter(parentId, afterId, false);
    m.created = m.updated = Ids::now();
    QList<Block> blocks = content;
    ensureBlockIds(blocks);
    const QString id = insertPage(m, blocksToJsonString(blocks), {}, nullptr);
    if (!id.isEmpty())
        emitLater([this] { emit treeChanged(); });
    return id;
}

QString Workspace::createDatabase(const QString &parentId, const QString &title, const Schema &schema,
                                  const QString &afterId)
{
    PageMeta m;
    m.id = Ids::newPageId();
    m.parentId = parentId;
    m.kind = PageKind::Database;
    m.title = title;
    m.sort = sortAfter(parentId, afterId, false);
    m.created = m.updated = Ids::now();
    const QString id = insertPage(m, QStringLiteral("[]"), {}, &schema);
    if (!id.isEmpty())
        emitLater([this] { emit treeChanged(); });
    return id;
}

QString Workspace::createRow(const QString &databaseId, const QString &title, const QJsonObject &props,
                             const QList<Block> &content)
{
    PageMeta m;
    m.id = Ids::newPageId();
    m.parentId = databaseId;
    m.kind = PageKind::Row;
    m.title = title;
    m.sort = sortAfter(databaseId, {}, true);
    m.created = m.updated = Ids::now();
    QJsonObject values = props;
    QList<QPair<Property, QStringList>> relations;
    if (const Schema *s = lookupSchema(databaseId)) {
        for (const Property &p : s->props) {
            if (p.type == PropType::UniqueId && !values.contains(p.id))
                values[p.id] = nextUniqueId(databaseId, p.id);
            if (p.type == PropType::Relation && !p.relationPair.isEmpty() && values.contains(p.id))
                relations.append({p, relationIds(values.value(p.id))});
        }
    }
    QList<Block> blocks = content;
    ensureBlockIds(blocks);
    const QString id = insertPage(m, blocksToJsonString(blocks), values, nullptr);
    if (id.isEmpty())
        return id;
    for (const auto &r : relations)
        syncRelation(id, r.first, {}, r.second, 0);
    updateRowReminders(id, values);
    emitLater([this, databaseId] {
        emit treeChanged();
        emit propsChanged(databaseId);
    });
    return id;
}

int Workspace::nextUniqueId(const QString &databaseId, const QString &propId) const
{
    rebuildChildIndex();
    int maxId = 0;
    for (const QString &c : m_children.value(databaseId)) {
        const PageMeta &m = *m_pages.constFind(c);
        if (m.kind != PageKind::Row)
            continue;
        maxId = qMax(maxId, props(c).value(propId).toInt());
    }
    return maxId + 1;
}

QString Workspace::duplicate(const QString &id)
{
    const PageMeta *root = page(id);
    if (!root)
        return {};
    const QStringList ids = subtree(id, false);
    QHash<QString, QString> remap;
    for (const QString &old : ids)
        remap.insert(old, Ids::newPageId());

    batch([&] {
        for (const QString &old : ids) {
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("SELECT content, props, schema, options FROM pages WHERE id=?"));
            q.addBindValue(old);
            if (!q.exec() || !q.next())
                continue;
            PageMeta m = m_pages[old];
            m.id = remap[old];
            if (old == id) {
                m.title = m.title.isEmpty() ? m.title : tr("%1 (copy)").arg(m.title);
                m.sort = sortAfter(m.parentId, old, m.kind == PageKind::Row);
                m.favorite = 0;
            } else {
                m.parentId = remap.value(m.parentId, m.parentId);
            }
            m.created = m.updated = Ids::now();
            m.visited = 0;
            QList<Block> blocks = blocksFromJsonString(q.value(0).toString());
            for (Block &b : blocks) {
                if (remap.contains(b.pageId))
                    b.pageId = remap[b.pageId];
                for (Span &s : b.spans)
                    if (remap.contains(s.mention))
                        s.mention = remap[s.mention];
            }
            const QString schemaJson = q.value(2).toString();
            Schema schema;
            if (!schemaJson.isEmpty())
                schema = Schema::fromJson(fromJsonString(schemaJson));
            insertPage(m, blocksToJsonString(blocks), fromJsonString(q.value(1).toString()),
                       schemaJson.isEmpty() ? nullptr : &schema);
            exec(QStringLiteral("UPDATE pages SET options=? WHERE id=?"), {q.value(3).toString(), m.id});
        }
        emitLater([this] { emit treeChanged(); });
    });
    return remap.value(id);
}

// ---- read / write --------------------------------------------------------------

QList<Block> Workspace::content(const QString &id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT content FROM pages WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return expandSynced(blocksFromJsonString(q.value(0).toString()));
    return {};
}

void Workspace::touch(const QString &id)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->updated = Ids::now();
    exec(QStringLiteral("UPDATE pages SET updated=? WHERE id=?"), {it->updated, id});
}

void Workspace::updateLinks(const QString &id, const QList<Block> &blocks)
{
    exec(QStringLiteral("DELETE FROM links WHERE src=?"), {id});
    for (const QString &dst : blocksLinkedPages(blocks))
        if (dst != id)
            exec(QStringLiteral("INSERT OR IGNORE INTO links(src, dst) VALUES(?, ?)"), {id, dst});
}

void Workspace::setContent(const QString &id, const QList<Block> &blocks, QObject *source)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->updated = Ids::now();
    batch([&] {
        QList<Block> withIds = blocks;
        ensureBlockIds(withIds);
        const QList<Block> stored = collapseSynced(id, withIds);
        exec(QStringLiteral("UPDATE pages SET content=?, plain=?, updated=? WHERE id=?"),
             {blocksToJsonString(stored), blocksPlainText(blocks), it->updated, id});
        updateLinks(id, blocks);
        updateReminders(id, blocks);
    });
    emit contentChanged(id, source);
}

// ---- synced blocks ---------------------------------------------------------------

QList<Block> Workspace::collapseSynced(const QString &pageId, const QList<Block> &blocks)
{
    QList<Block> out;
    for (int i = 0; i < blocks.size();) {
        const Block &b = blocks[i];
        if (!b.syncId.isEmpty() && b.type != BlockType::Synced) {
            const QString syncId = b.syncId;
            const int base = b.depth;
            QList<Block> region;
            int j = i;
            while (j < blocks.size() && blocks[j].syncId == syncId && blocks[j].type != BlockType::Synced) {
                Block r = blocks[j];
                r.syncId.clear();
                r.depth = qMax(0, r.depth - base);
                for (QList<Block> &col : r.columns)
                    col = collapseSynced(pageId, col);
                region.append(r);
                ++j;
            }
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("SELECT origin FROM synced WHERE id=?"));
            q.addBindValue(syncId);
            const bool known = q.exec() && q.next();
            if (known) {
                const QString json = blocksToJsonString(region);
                QSqlQuery cur(m_db);
                cur.prepare(QStringLiteral("SELECT content FROM synced WHERE id=?"));
                cur.addBindValue(syncId);
                const bool changed = !(cur.exec() && cur.next() && cur.value(0).toString() == json);
                if (changed) {
                    exec(QStringLiteral("UPDATE synced SET content=?, updated=? WHERE id=?"), {json, Ids::now(), syncId});
                    // Every other page showing it has new text (search) and
                    // open editors reload it.
                    for (const QString &other : syncedPages(syncId)) {
                        if (other == pageId)
                            continue;
                        exec(QStringLiteral("UPDATE pages SET plain=? WHERE id=?"), {blocksPlainText(content(other)), other});
                        emitLater([this, other] { emit contentChanged(other, this); });
                    }
                }
            } else
                exec(QStringLiteral("INSERT INTO synced(id, origin, content, updated) VALUES(?,?,?,?)"),
                     {syncId, pageId, blocksToJsonString(region), Ids::now()});
            Block placeholder;
            placeholder.type = BlockType::Synced;
            placeholder.syncId = syncId;
            placeholder.depth = base;
            out.append(placeholder);
            i = j;
            continue;
        }
        Block copy = b;
        for (QList<Block> &col : copy.columns)
            col = collapseSynced(pageId, col);
        out.append(copy);
        ++i;
    }
    return out;
}

QList<Block> Workspace::expandSynced(const QList<Block> &blocks, int nesting) const
{
    QList<Block> out;
    for (const Block &b : blocks) {
        if (b.type == BlockType::Synced && nesting < 4) {
            QList<Block> region = expandSynced(syncedContent(b.syncId), nesting + 1);
            if (region.isEmpty())
                region.append(Block{});
            for (Block &r : region) {
                r.depth += b.depth;
                r.syncId = b.syncId;
            }
            out += region;
            continue;
        }
        Block copy = b;
        for (QList<Block> &col : copy.columns)
            col = expandSynced(col, nesting);
        out.append(copy);
    }
    return out;
}

QList<Block> Workspace::syncedContent(const QString &syncId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT content FROM synced WHERE id=?"));
    q.addBindValue(syncId);
    if (q.exec() && q.next())
        return blocksFromJsonString(q.value(0).toString());
    return {};
}

QString Workspace::syncedOrigin(const QString &syncId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT origin FROM synced WHERE id=?"));
    q.addBindValue(syncId);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return {};
}

QStringList Workspace::syncedPages(const QString &syncId) const
{
    QStringList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM pages WHERE trashed=0 AND content LIKE ?"));
    q.addBindValue(QStringLiteral("%\"sync\":\"") + syncId + QStringLiteral("\"%"));
    if (q.exec())
        while (q.next())
            out.append(q.value(0).toString());
    return out;
}

QString Workspace::createSynced(const QString &originPage, const QList<Block> &blocks)
{
    const QString id = Ids::newShortId(12);
    exec(QStringLiteral("INSERT INTO synced(id, origin, content, updated) VALUES(?,?,?,?)"),
         {id, originPage, blocksToJsonString(blocks), Ids::now()});
    return id;
}

// ---- comments ----------------------------------------------------------------------

QList<Workspace::Comment> Workspace::comments(const QString &pageId) const
{
    QList<Comment> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, thread, page_id, block_id, quote, body, created, edited, resolved FROM comments "
                             "WHERE page_id=? ORDER BY created"));
    q.addBindValue(pageId);
    if (q.exec())
        while (q.next())
            out.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                        q.value(4).toString(), q.value(5).toString(), q.value(6).toLongLong(), q.value(7).toLongLong(),
                        q.value(8).toBool()});
    return out;
}

QList<Workspace::Comment> Workspace::recentComments(int limit) const
{
    QList<Comment> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, thread, page_id, block_id, quote, body, created, edited, resolved FROM comments "
                             "ORDER BY created DESC LIMIT ?"));
    q.addBindValue(limit * 2);
    if (q.exec())
        while (q.next() && out.size() < limit) {
            const PageMeta *m = page(q.value(2).toString());
            if (!m || m->isTrashed())
                continue;
            out.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                        q.value(4).toString(), q.value(5).toString(), q.value(6).toLongLong(), q.value(7).toLongLong(),
                        q.value(8).toBool()});
        }
    return out;
}

QString Workspace::addComment(const QString &pageId, const QString &blockId, const QString &quote, const QString &body,
                              const QString &thread)
{
    const QString id = Ids::newShortId(12);
    const QString t = thread.isEmpty() ? id : thread;
    exec(QStringLiteral("INSERT INTO comments(id, thread, page_id, block_id, quote, body, created) VALUES(?,?,?,?,?,?,?)"),
         {id, t, pageId, blockId, quote, body, Ids::now()});
    if (!thread.isEmpty())
        exec(QStringLiteral("UPDATE comments SET resolved=0 WHERE thread=?"), {t});
    emit commentsChanged(pageId);
    return id;
}

void Workspace::editComment(const QString &id, const QString &body)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT page_id FROM comments WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return;
    exec(QStringLiteral("UPDATE comments SET body=?, edited=? WHERE id=?"), {body, Ids::now(), id});
    emit commentsChanged(q.value(0).toString());
}

void Workspace::deleteComment(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT page_id, thread FROM comments WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return;
    const QString page = q.value(0).toString(), thread = q.value(1).toString();
    if (thread == id)
        exec(QStringLiteral("DELETE FROM comments WHERE thread=?"), {thread});
    else
        exec(QStringLiteral("DELETE FROM comments WHERE id=?"), {id});
    emit commentsChanged(page);
}

void Workspace::resolveThread(const QString &thread, bool resolved)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT page_id FROM comments WHERE thread=? LIMIT 1"));
    q.addBindValue(thread);
    if (!q.exec() || !q.next())
        return;
    exec(QStringLiteral("UPDATE comments SET resolved=? WHERE thread=?"), {resolved ? 1 : 0, thread});
    emit commentsChanged(q.value(0).toString());
}

int Workspace::openThreadCount(const QString &pageId) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(DISTINCT thread) FROM comments WHERE page_id=? AND resolved=0"));
    q.addBindValue(pageId);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return 0;
}

// ---- versions ----------------------------------------------------------------------

QList<Workspace::Version> Workspace::versions(const QString &pageId) const
{
    QList<Version> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, created, title, name FROM versions WHERE page_id=? ORDER BY created DESC, id DESC"));
    q.addBindValue(pageId);
    if (q.exec())
        while (q.next())
            out.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(), q.value(3).toString()});
    return out;
}

QList<Block> Workspace::versionContent(qint64 versionId, QString *title) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT content, title FROM versions WHERE id=?"));
    q.addBindValue(versionId);
    if (q.exec() && q.next()) {
        if (title)
            *title = q.value(1).toString();
        return expandSynced(blocksFromJsonString(q.value(0).toString()));
    }
    return {};
}

bool Workspace::snapshot(const QString &pageId, bool force, int minMinutes)
{
    const PageMeta *m = page(pageId);
    if (!m || m->isDatabase())
        return false;
    QSqlQuery cur(m_db);
    cur.prepare(QStringLiteral("SELECT content FROM pages WHERE id=?"));
    cur.addBindValue(pageId);
    if (!cur.exec() || !cur.next())
        return false;
    const QString content = cur.value(0).toString();
    QSqlQuery last(m_db);
    last.prepare(QStringLiteral("SELECT created, content, title FROM versions WHERE page_id=? ORDER BY created DESC, id DESC LIMIT 1"));
    last.addBindValue(pageId);
    if (last.exec() && last.next()) {
        if (last.value(1).toString() == content && last.value(2).toString() == m->title)
            return false;
        if (!force && Ids::now() - last.value(0).toLongLong() < qint64(minMinutes) * 60000)
            return false;
    } else if (content == QLatin1String("[]") && !force) {
        return false; // an empty page isn't worth keeping
    }
    exec(QStringLiteral("INSERT INTO versions(page_id, created, title, content) VALUES(?,?,?,?)"), {pageId, Ids::now(), m->title, content});
    // Keep the newest 120 unnamed versions per page.
    exec(QStringLiteral("DELETE FROM versions WHERE page_id=? AND name='' AND id NOT IN (SELECT id FROM versions WHERE page_id=? "
                        "ORDER BY created DESC, id DESC LIMIT 120)"),
         {pageId, pageId});
    emit versionsChanged(pageId);
    return true;
}

void Workspace::restoreVersion(qint64 versionId)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT page_id FROM versions WHERE id=?"));
    q.addBindValue(versionId);
    if (!q.exec() || !q.next())
        return;
    const QString pageId = q.value(0).toString();
    // The current state becomes a version first, so a restore can be undone.
    snapshot(pageId, true);
    QString title;
    const QList<Block> blocks = versionContent(versionId, &title);
    setTitle(pageId, title);
    setContent(pageId, blocks);
    emit versionsChanged(pageId);
}

void Workspace::nameVersion(qint64 versionId, const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT page_id FROM versions WHERE id=?"));
    q.addBindValue(versionId);
    if (!q.exec() || !q.next())
        return;
    exec(QStringLiteral("UPDATE versions SET name=? WHERE id=?"), {name.trimmed(), versionId});
    emit versionsChanged(q.value(0).toString());
}

// ---- reminders ---------------------------------------------------------------------

void Workspace::updateReminders(const QString &pageId, const QList<Block> &blocks)
{
    QSet<QString> keep;
    forEachBlock(blocks, [&](const Block &b) {
        int n = 0;
        for (const Span &s : b.spans) {
            ++n;
            if (s.date.isEmpty() || !s.remind)
                continue;
            QDateTime at = QDateTime::fromString(s.date.left(16), QStringLiteral("yyyy-MM-ddTHH:mm"));
            if (!at.isValid()) {
                // Date-only reminders go off at 9:00 that day, like Notion.
                const QDate d = QDate::fromString(s.date.left(10), Qt::ISODate);
                if (!d.isValid())
                    continue;
                at = QDateTime(d, QTime(9, 0));
            }
            const QString id = pageId + QLatin1Char(':') + b.id + QLatin1Char(':') + QString::number(n);
            QString text;
            for (const Span &x : b.spans)
                if (!x.isObject())
                    text += x.text;
            keep.insert(id);
            exec(QStringLiteral("INSERT INTO reminders(id, page_id, text, at) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                                "text=excluded.text, done=CASE WHEN reminders.at != excluded.at THEN 0 ELSE reminders.done END, "
                                "at=excluded.at"),
                 {id, pageId, text.simplified(), at.toMSecsSinceEpoch()});
        }
    });
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM reminders WHERE page_id=?"));
    q.addBindValue(pageId);
    QStringList stale;
    if (q.exec())
        while (q.next())
            if (!keep.contains(q.value(0).toString()))
                stale.append(q.value(0).toString());
    for (const QString &id : stale)
        exec(QStringLiteral("DELETE FROM reminders WHERE id=?"), {id});
    emitLater([this] { emit remindersChanged(); });
}

QList<Workspace::Reminder> Workspace::reminders(bool includeDone) const
{
    QList<Reminder> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, page_id, text, at, done FROM reminders ORDER BY at"));
    while (q.next()) {
        Reminder r{q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toLongLong(), q.value(4).toBool()};
        const PageMeta *m = page(r.pageId);
        if (!m || m->isTrashed() || (!includeDone && r.done))
            continue;
        out.append(r);
    }
    return out;
}

void Workspace::setReminderDone(const QString &id, bool done)
{
    exec(QStringLiteral("UPDATE reminders SET done=? WHERE id=?"), {done ? 1 : 0, id});
    emit remindersChanged();
}

// ---- people --------------------------------------------------------------------------

QList<Workspace::Person> Workspace::people() const
{
    QList<Person> out;
    const QJsonArray arr = QJsonDocument::fromJson(metaValue(QStringLiteral("people")).toUtf8()).array();
    for (const QJsonValue &v : arr)
        out.append({v.toObject().value(QStringLiteral("id")).toString(), v.toObject().value(QStringLiteral("name")).toString()});
    if (out.isEmpty())
        out.append({QStringLiteral("me"), tr("Me")});
    return out;
}

void Workspace::setPeople(const QList<Person> &people)
{
    QJsonArray arr;
    for (const Person &p : people)
        arr.append(QJsonObject{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.name}});
    setMetaValue(QStringLiteral("people"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

QString Workspace::personName(const QString &id) const
{
    for (const Person &p : people())
        if (p.id == id)
            return p.name;
    return id;
}

// ---- database templates ------------------------------------------------------------------

QList<const PageMeta *> Workspace::templates(const QString &databaseId) const
{
    rebuildChildIndex();
    QList<const PageMeta *> out;
    for (const QString &id : m_children.value(databaseId)) {
        const PageMeta &m = *m_pages.constFind(id);
        if (!m.isTrashed() && m.kind == PageKind::Template)
            out.append(&m);
    }
    return out;
}

QString Workspace::createTemplate(const QString &databaseId, const QString &title)
{
    PageMeta m;
    m.id = Ids::newPageId();
    m.parentId = databaseId;
    m.kind = PageKind::Template;
    m.title = title.isEmpty() ? tr("New template") : title;
    m.sort = sortAfter(databaseId, {}, true);
    m.created = m.updated = Ids::now();
    const QString id = insertPage(m, QStringLiteral("[]"), {}, nullptr);
    if (!id.isEmpty())
        emitLater([this, databaseId] {
            emit treeChanged();
            emit schemaChanged(databaseId);
        });
    return id;
}

QString Workspace::createRowFromTemplate(const QString &databaseId, const QString &templateId)
{
    const PageMeta *t = page(templateId);
    if (!t)
        return createRow(databaseId);
    const QString title = t->title == tr("New template") ? QString() : t->title;
    const QString icon = t->icon, cover = t->cover;
    QList<Block> content = this->content(templateId);
    renewBlockIds(content);
    const QJsonObject props = this->props(templateId);
    QString id;
    batch([&] {
        id = createRow(databaseId, title, props, content);
        if (!icon.isEmpty())
            setIcon(id, icon);
        if (!cover.isEmpty())
            setCover(id, cover);
    });
    return id;
}

QJsonObject Workspace::props(const QString &id) const
{
    auto cached = m_propsCache.constFind(id);
    if (cached != m_propsCache.constEnd())
        return *cached;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT props FROM pages WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        const QJsonObject o = fromJsonString(q.value(0).toString());
        if (m_propsCache.size() > 20000)
            m_propsCache.clear();
        m_propsCache.insert(id, o);
        return o;
    }
    return {};
}

void Workspace::writePropsRaw(const QString &id, const QJsonObject &props)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->updated = Ids::now();
    exec(QStringLiteral("UPDATE pages SET props=?, updated=? WHERE id=?"), {toJsonString(props), it->updated, id});
    m_propsCache.insert(id, props);
    const QString parent = it->parentId;
    emitLater([this, id, parent] {
        emit propsChanged(id);
        if (!parent.isEmpty())
            emit propsChanged(parent);
    });
}

void Workspace::setProps(const QString &id, const QJsonObject &props)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    const QJsonObject old = this->props(id);
    batch([&] {
        writePropsRaw(id, props);
        if (const Schema *s = lookupSchema(it->parentId)) {
            // Copy: syncing may reload this schema.
            const QList<Property> propsCopy = s->props;
            for (const Property &p : propsCopy) {
                if (p.type != PropType::Relation || p.relationPair.isEmpty())
                    continue;
                const QStringList before = relationIds(old.value(p.id)), after = relationIds(props.value(p.id));
                if (before != after)
                    syncRelation(id, p, before, after, 0);
            }
        }
        updateRowReminders(id, props);
    });
}

void Workspace::syncRelation(const QString &rowId, const Property &p, const QStringList &oldIds, const QStringList &newIds,
                             int depth)
{
    if (p.relationPair.isEmpty() || depth > 2)
        return;
    const Schema *ts = lookupSchema(p.relationDb);
    if (!ts)
        return;
    const Property *pairPtr = ts->prop(p.relationPair);
    if (!pairPtr || pairPtr->type != PropType::Relation)
        return;
    const Property pair = *pairPtr;
    for (const QString &t : newIds) {
        if (oldIds.contains(t) || t == rowId || !exists(t))
            continue;
        QJsonObject tp = props(t);
        const QStringList before = relationIds(tp.value(pair.id));
        QStringList after = pair.relationOne ? QStringList{rowId} : before;
        if (!after.contains(rowId))
            after.append(rowId);
        if (after == before)
            continue;
        tp[pair.id] = QJsonArray::fromStringList(after);
        writePropsRaw(t, tp);
        // A one-page side drops its previous page, which loses `t` in turn.
        syncRelation(t, pair, before, after, depth + 1);
    }
    for (const QString &t : oldIds) {
        if (newIds.contains(t) || !exists(t))
            continue;
        QJsonObject tp = props(t);
        QStringList ids = relationIds(tp.value(pair.id));
        if (!ids.removeAll(rowId))
            continue;
        if (ids.isEmpty())
            tp.remove(pair.id);
        else
            tp[pair.id] = QJsonArray::fromStringList(ids);
        writePropsRaw(t, tp);
    }
}

void Workspace::updateRowReminders(const QString &id, const QJsonObject &values)
{
    const PageMeta *m = page(id);
    if (!m || m->kind != PageKind::Row)
        return;
    const Schema *s = lookupSchema(m->parentId);
    if (!s)
        return;
    const QString prefix = id + QStringLiteral(":prop:");
    QSet<QString> keep;
    for (const Property &p : s->props) {
        if (p.type != PropType::Date)
            continue;
        const QJsonValue v = values.value(p.id);
        if (!dateHasReminder(v))
            continue;
        QDateTime at = dateTimeOf(v);
        if (!at.isValid())
            continue;
        if (v.toString().size() <= 10 || v.toString()[10] != QLatin1Char('T'))
            at.setTime(QTime(9, 0));
        const QString rid = prefix + p.id;
        keep.insert(rid);
        exec(QStringLiteral("INSERT INTO reminders(id, page_id, text, at) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET "
                            "text=excluded.text, done=CASE WHEN reminders.at != excluded.at THEN 0 ELSE reminders.done END, "
                            "at=excluded.at"),
             {rid, id, p.name, at.toMSecsSinceEpoch()});
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id FROM reminders WHERE id LIKE ?"));
    q.addBindValue(prefix + QLatin1Char('%'));
    QStringList stale;
    if (q.exec())
        while (q.next())
            if (!keep.contains(q.value(0).toString()))
                stale.append(q.value(0).toString());
    for (const QString &rid : stale)
        exec(QStringLiteral("DELETE FROM reminders WHERE id=?"), {rid});
    emitLater([this] { emit remindersChanged(); });
}

void Workspace::setProp(const QString &id, const QString &propId, const QJsonValue &value)
{
    QJsonObject p = props(id);
    if (value.isNull() || value.isUndefined())
        p.remove(propId);
    else
        p[propId] = value;
    setProps(id, p);
}

const Schema *Workspace::lookupSchema(const QString &databaseId) const
{
    if (databaseId.isEmpty())
        return nullptr;
    auto it = m_schemaCache.constFind(databaseId);
    if (it != m_schemaCache.constEnd())
        return it->get();
    const PageMeta *m = page(databaseId);
    if (!m || m->kind != PageKind::Database)
        return nullptr;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT schema FROM pages WHERE id=?"));
    q.addBindValue(databaseId);
    auto s = std::make_shared<Schema>(q.exec() && q.next() && !q.value(0).toString().isEmpty()
                                          ? Schema::fromJson(fromJsonString(q.value(0).toString()))
                                          : Schema::basic());
    m_schemaCache.insert(databaseId, s);
    return s.get();
}

QString Workspace::lookupMe() const
{
    const QList<Person> ps = people();
    return ps.isEmpty() ? QStringLiteral("me") : ps.first().id;
}

Schema Workspace::schema(const QString &databaseId) const
{
    if (const Schema *s = lookupSchema(databaseId))
        return *s;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT schema FROM pages WHERE id=?"));
    q.addBindValue(databaseId);
    if (q.exec() && q.next() && !q.value(0).toString().isEmpty())
        return Schema::fromJson(fromJsonString(q.value(0).toString()));
    return Schema::basic();
}

void Workspace::setSchema(const QString &databaseId, const Schema &schema)
{
    if (!exists(databaseId))
        return;
    const Schema old = this->schema(databaseId);
    exec(QStringLiteral("UPDATE pages SET schema=? WHERE id=?"), {toJsonString(schema.toJson()), databaseId});
    m_schemaCache.insert(databaseId, std::make_shared<Schema>(schema));
    // New unique id properties number the existing rows by creation order.
    for (const Property &p : schema.props) {
        if (p.type != PropType::UniqueId)
            continue;
        const Property *before = old.prop(p.id);
        if (before && before->type == PropType::UniqueId)
            continue;
        QList<const PageMeta *> all;
        rebuildChildIndex();
        for (const QString &c : m_children.value(databaseId)) {
            const PageMeta &m = *m_pages.constFind(c);
            if (m.kind == PageKind::Row)
                all.append(&m);
        }
        std::sort(all.begin(), all.end(), [](const PageMeta *a, const PageMeta *b) { return a->created < b->created; });
        batch([&] {
            int n = 0;
            for (const PageMeta *m : all) {
                QJsonObject pr = props(m->id);
                pr[p.id] = ++n;
                writePropsRaw(m->id, pr);
            }
        });
    }
    touch(databaseId);
    emitLater([this, databaseId] { emit schemaChanged(databaseId); });
}

QString Workspace::addRelation(const QString &databaseId, const QString &name, const QString &targetDb, bool twoWay,
                               const QString &pairName)
{
    if (!lookupSchema(databaseId) || !lookupSchema(targetDb))
        return {};
    Schema s = schema(databaseId);
    Property p;
    p.id = s.newPropId();
    p.name = name.isEmpty() ? tr("Related to %1").arg(title(targetDb)) : name;
    p.type = PropType::Relation;
    p.relationDb = targetDb;
    s.props.append(p);
    setSchema(databaseId, s);
    if (twoWay)
        setRelationTwoWay(databaseId, p.id, true, pairName);
    return p.id;
}

void Workspace::setRelationTwoWay(const QString &databaseId, const QString &propId, bool twoWay, const QString &pairName)
{
    Schema s = schema(databaseId);
    Property *p = s.prop(propId);
    if (!p || p->type != PropType::Relation)
        return;
    const QString targetDb = p->relationDb;
    if (twoWay == !p->relationPair.isEmpty())
        return;
    if (!twoWay) {
        // The other side stays as a one-way relation of its own.
        const QString pairId = p->relationPair;
        p->relationPair.clear();
        if (targetDb == databaseId) {
            if (Property *q = s.prop(pairId))
                q->relationPair.clear();
            setSchema(databaseId, s);
        } else {
            setSchema(databaseId, s);
            Schema t = schema(targetDb);
            if (Property *q = t.prop(pairId))
                q->relationPair.clear();
            setSchema(targetDb, t);
        }
        return;
    }
    const bool self = targetDb == databaseId;
    Schema t = self ? s : schema(targetDb);
    Property q;
    q.id = t.newPropId();
    if (self && q.id == propId)
        q.id = t.newPropId() + QStringLiteral("x");
    q.name = pairName.isEmpty() ? tr("Related to %1").arg(title(databaseId)) : pairName;
    q.type = PropType::Relation;
    q.relationDb = databaseId;
    q.relationPair = propId;
    t.props.append(q);
    if (self) {
        t.prop(propId)->relationPair = q.id;
        setSchema(databaseId, t);
    } else {
        p->relationPair = q.id;
        setSchema(databaseId, s);
        setSchema(targetDb, t);
    }
    // Existing links appear on the other side.
    batch([&] {
        const Property updated = *lookupSchema(databaseId)->prop(propId);
        rebuildChildIndex();
        for (const QString &c : QStringList(m_children.value(databaseId))) {
            const QStringList ids = relationIds(props(c).value(propId));
            if (!ids.isEmpty())
                syncRelation(c, updated, {}, ids, 0);
        }
    });
}

void Workspace::removeProperty(const QString &databaseId, const QString &propId)
{
    Schema s = schema(databaseId);
    const Property *p = s.prop(propId);
    if (!p || p->type == PropType::Title)
        return;
    if (p->type == PropType::Relation && !p->relationPair.isEmpty())
        setRelationTwoWay(databaseId, propId, false);
    s = schema(databaseId);
    s.props.removeIf([&](const Property &x) { return x.id == propId; });
    for (View &v : s.views) {
        v.hidden.removeAll(propId);
        v.order.removeAll(propId);
        v.widths.remove(propId);
        v.calcs.remove(propId);
        v.sorts.removeIf([&](const SortRule &r) { return r.prop == propId; });
        std::function<void(QList<FilterRule> &)> strip = [&](QList<FilterRule> &rules) {
            rules.removeIf([&](const FilterRule &r) { return !r.group && r.prop == propId; });
            for (FilterRule &r : rules)
                if (r.group)
                    strip(r.rules);
        };
        strip(v.filters);
        if (v.groupBy == propId)
            v.groupBy.clear();
        if (v.subGroupBy == propId)
            v.subGroupBy.clear();
        if (v.dateProp == propId)
            v.dateProp.clear();
        if (v.endDateProp == propId)
            v.endDateProp.clear();
        if (v.chartX == propId)
            v.chartX.clear();
        if (v.chartGroup == propId)
            v.chartGroup.clear();
        if (v.chartY.endsWith(QLatin1Char(':') + propId))
            v.chartY = QStringLiteral("count");
    }
    if (s.subItemsProp == propId)
        s.subItemsProp.clear();
    if (s.dependencyProp == propId)
        s.dependencyProp.clear();
    // Rollups over a removed relation have nothing left to show.
    for (Property &x : s.props)
        if (x.type == PropType::Rollup && x.rollupRelation == propId)
            x.rollupRelation.clear();
    setSchema(databaseId, s);
}

void Workspace::enableSubItems(const QString &databaseId, bool on)
{
    Schema s = schema(databaseId);
    if (on) {
        if (s.prop(s.subItemsProp))
            return;
        const QString parentId = addRelation(databaseId, tr("Parent item"), databaseId, true, tr("Sub-items"));
        s = schema(databaseId);
        if (Property *p = s.prop(parentId))
            p->relationOne = true;
        s.subItemsProp = parentId;
        setSchema(databaseId, s);
    } else {
        s.subItemsProp.clear();
        setSchema(databaseId, s);
    }
}

void Workspace::enableDependencies(const QString &databaseId, bool on)
{
    Schema s = schema(databaseId);
    if (on) {
        if (s.prop(s.dependencyProp))
            return;
        const QString blockedBy = addRelation(databaseId, tr("Blocked by"), databaseId, true, tr("Blocking"));
        s = schema(databaseId);
        s.dependencyProp = blockedBy;
        setSchema(databaseId, s);
    } else {
        s.dependencyProp.clear();
        setSchema(databaseId, s);
    }
}

PageOptions Workspace::options(const QString &id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT options FROM pages WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return PageOptions::fromJson(fromJsonString(q.value(0).toString()));
    return {};
}

void Workspace::setOptions(const QString &id, const PageOptions &options)
{
    if (!exists(id))
        return;
    exec(QStringLiteral("UPDATE pages SET options=? WHERE id=?"), {toJsonString(options.toJson()), id});
    emitLater([this, id] { emit pageChanged(id); });
}

void Workspace::convertToDatabase(const QString &id, const Schema &schema)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || it->kind != PageKind::Page)
        return;
    it->kind = PageKind::Database;
    exec(QStringLiteral("UPDATE pages SET kind=?, schema=?, content='[]', plain='' WHERE id=?"),
         {int(PageKind::Database), toJsonString(schema.toJson()), id});
    m_schemaCache.remove(id);
    touch(id);
    emitLater([this, id] {
        emit treeChanged();
        emit pageChanged(id);
        emit schemaChanged(id);
    });
}

void Workspace::setTitle(const QString &id, const QString &title)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || it->title == title)
        return;
    it->title = title;
    it->updated = Ids::now();
    exec(QStringLiteral("UPDATE pages SET title=?, updated=? WHERE id=?"), {title, it->updated, id});
    const QString parent = it->parentId;
    const bool row = it->kind == PageKind::Row;
    emitLater([this, id, parent, row] {
        emit pageChanged(id);
        if (row)
            emit propsChanged(parent);
    });
}

void Workspace::setIcon(const QString &id, const QString &icon)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->icon = icon;
    exec(QStringLiteral("UPDATE pages SET icon=? WHERE id=?"), {icon, id});
    touch(id);
    const QString parent = it->parentId;
    const bool row = it->kind == PageKind::Row;
    emitLater([this, id, parent, row] {
        emit pageChanged(id);
        if (row)
            emit propsChanged(parent);
    });
}

void Workspace::setCover(const QString &id, const QString &cover, double position)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->cover = cover;
    it->coverPos = position;
    exec(QStringLiteral("UPDATE pages SET cover=?, cover_pos=? WHERE id=?"), {cover, position, id});
    touch(id);
    const QString parent = it->parentId;
    const bool row = it->kind == PageKind::Row;
    emitLater([this, id, parent, row] {
        emit pageChanged(id);
        if (row)
            emit propsChanged(parent);
    });
}

void Workspace::setCoverPosition(const QString &id, double position)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->coverPos = qBound(0.0, position, 1.0);
    exec(QStringLiteral("UPDATE pages SET cover_pos=? WHERE id=?"), {it->coverPos, id});
    emitLater([this, id] { emit pageChanged(id); });
}

void Workspace::setFavorite(const QString &id, bool favorite)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || (it->favorite > 0) == favorite)
        return;
    double value = 0;
    if (favorite) {
        for (const PageMeta &m : m_pages)
            value = qMax(value, m.favorite);
        value += 1;
    }
    it->favorite = value;
    exec(QStringLiteral("UPDATE pages SET favorite=? WHERE id=?"), {value, id});
    emitLater([this, id] {
        emit pageChanged(id);
        emit favoritesChanged();
    });
}

void Workspace::moveFavorite(const QString &id, int toIndex)
{
    QList<const PageMeta *> favs = favorites();
    int from = -1;
    for (int i = 0; i < favs.size(); ++i)
        if (favs[i]->id == id)
            from = i;
    if (from < 0)
        return;
    favs.move(from, qBound(0, toIndex, int(favs.size()) - 1));
    batch([&] {
        for (int i = 0; i < favs.size(); ++i) {
            m_pages[favs[i]->id].favorite = i + 1;
            exec(QStringLiteral("UPDATE pages SET favorite=? WHERE id=?"), {i + 1, favs[i]->id});
        }
        emitLater([this] { emit favoritesChanged(); });
    });
}

void Workspace::markVisited(const QString &id, qint64 when)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    it->visited = when > 0 ? when : Ids::now();
    exec(QStringLiteral("UPDATE pages SET visited=? WHERE id=?"), {it->visited, id});
}

bool Workspace::move(const QString &id, const QString &newParentId, int index)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return false;
    if (!newParentId.isEmpty() && (newParentId == id || isInside(newParentId, id)))
        return false;
    if (const PageMeta *np = page(newParentId); np && np->kind == PageKind::Database && it->kind != PageKind::Row)
        return false;

    rebuildChildIndex();
    QList<const PageMeta *> siblings = children(newParentId);
    siblings.removeIf([&](const PageMeta *m) { return m->id == id; });
    if (index < 0 || index > siblings.size())
        index = int(siblings.size());
    double sort;
    if (siblings.isEmpty())
        sort = 1.0;
    else if (index == 0)
        sort = siblings.first()->sort - 1.0;
    else if (index == siblings.size())
        sort = siblings.last()->sort + 1.0;
    else
        sort = (siblings[index - 1]->sort + siblings[index]->sort) / 2.0;

    it->parentId = newParentId;
    it->sort = sort;
    exec(QStringLiteral("UPDATE pages SET parent_id=?, sort=? WHERE id=?"), {newParentId, sort, id});
    m_childIndexDirty = true;
    emitLater([this] { emit treeChanged(); });
    return true;
}

void Workspace::moveRow(const QString &id, const QString &beforeId)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end())
        return;
    QList<const PageMeta *> siblings = rows(it->parentId);
    siblings.removeIf([&](const PageMeta *m) { return m->id == id; });
    int index = int(siblings.size());
    for (int i = 0; i < siblings.size(); ++i)
        if (siblings[i]->id == beforeId)
            index = i;
    double sort;
    if (siblings.isEmpty())
        sort = 1.0;
    else if (index == 0)
        sort = siblings.first()->sort - 1.0;
    else if (index == siblings.size())
        sort = siblings.last()->sort + 1.0;
    else
        sort = (siblings[index - 1]->sort + siblings[index]->sort) / 2.0;
    it->sort = sort;
    exec(QStringLiteral("UPDATE pages SET sort=? WHERE id=?"), {sort, id});
    m_childIndexDirty = true;
    const QString parent = it->parentId;
    emitLater([this, parent] { emit propsChanged(parent); });
}

// ---- trash ---------------------------------------------------------------------

void Workspace::trash(const QString &id)
{
    if (!exists(id) || page(id)->isTrashed())
        return;
    const qint64 when = Ids::now();
    const QString parent = page(id)->parentId;
    batch([&] {
        for (const QString &p : subtree(id, false)) {
            m_pages[p].trashed = when;
            exec(QStringLiteral("UPDATE pages SET trashed=? WHERE id=?"), {when, p});
        }
        emitLater([this, parent] {
            emit treeChanged();
            emit trashChanged();
            emit favoritesChanged();
            if (!parent.isEmpty())
                emit propsChanged(parent);
        });
    });
}

void Workspace::restore(const QString &id)
{
    auto it = m_pages.find(id);
    if (it == m_pages.end() || !it->isTrashed())
        return;
    const qint64 when = it->trashed;
    batch([&] {
        // Pages trashed together come back together.
        for (const QString &p : subtree(id, true)) {
            if (m_pages[p].trashed != when)
                continue;
            m_pages[p].trashed = 0;
            exec(QStringLiteral("UPDATE pages SET trashed=0 WHERE id=?"), {p});
        }
        // A page whose parent is gone or still in the trash returns to the top level.
        const PageMeta *parent = page(it->parentId);
        if (!it->parentId.isEmpty() && (!parent || parent->isTrashed())) {
            if (it->kind == PageKind::Row) {
                // A row without its database can't live on its own: make it a page.
                it->kind = PageKind::Page;
                exec(QStringLiteral("UPDATE pages SET kind=0 WHERE id=?"), {id});
            }
            it->parentId.clear();
            exec(QStringLiteral("UPDATE pages SET parent_id='' WHERE id=?"), {id});
            m_childIndexDirty = true;
        }
        const QString parentId = it->parentId;
        emitLater([this, parentId] {
            emit treeChanged();
            emit trashChanged();
            emit favoritesChanged();
            if (!parentId.isEmpty())
                emit propsChanged(parentId);
        });
    });
}

void Workspace::deleteForever(const QString &id)
{
    if (!exists(id))
        return;
    batch([&] {
        for (const QString &p : subtree(id, true)) {
            exec(QStringLiteral("DELETE FROM pages WHERE id=?"), {p});
            exec(QStringLiteral("DELETE FROM links WHERE src=?"), {p});
            m_pages.remove(p);
        }
        m_childIndexDirty = true;
        emitLater([this] {
            emit treeChanged();
            emit trashChanged();
        });
    });
}

void Workspace::emptyTrash()
{
    // Collect ids first: deleting from the hash may move its elements.
    QStringList ids;
    for (const PageMeta *m : trashedRoots())
        ids.append(m->id);
    batch([&] {
        for (const QString &id : ids)
            deleteForever(id);
    });
}

// ---- links & search ------------------------------------------------------------

QStringList Workspace::backlinks(const QString &id) const
{
    QStringList out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT src FROM links WHERE dst=?"));
    q.addBindValue(id);
    if (q.exec())
        while (q.next()) {
            const QString src = q.value(0).toString();
            const PageMeta *m = page(src);
            if (m && !m->isTrashed())
                out.append(src);
        }
    std::sort(out.begin(), out.end(), [this](const QString &a, const QString &b) {
        return title(a).localeAwareCompare(title(b)) < 0;
    });
    return out;
}

int Workspace::fuzzyScore(const QString &needle, const QString &haystack)
{
    if (needle.isEmpty())
        return 1;
    const QString n = needle.toLower(), h = haystack.toLower();
    if (h == n)
        return 1000;
    if (h.startsWith(n))
        return 800 - int(h.size());
    const int idx = h.indexOf(n);
    if (idx >= 0) {
        // Word starts score higher than matches inside a word.
        const bool wordStart = idx == 0 || !h[idx - 1].isLetterOrNumber();
        return (wordStart ? 600 : 400) - idx;
    }
    // Subsequence: every needle character in order.
    int score = 0, hi = 0, streak = 0;
    for (const QChar c : n) {
        if (c.isSpace())
            continue;
        bool found = false;
        while (hi < h.size()) {
            if (h[hi] == c) {
                found = true;
                streak++;
                score += 2 + streak * 2 + ((hi == 0 || !h[hi - 1].isLetterOrNumber()) ? 6 : 0);
                hi++;
                break;
            }
            streak = 0;
            hi++;
        }
        if (!found)
            return 0;
    }
    return qMin(score, 300);
}

QList<Workspace::SearchHit> Workspace::search(const QString &query, const SearchOptions &options, int limit) const
{
    const QString q = query.trimmed();
    QList<SearchHit> hits;
    QSet<QString> seen;
    if (q.isEmpty())
        return hits;
    const auto allowed = [&](const PageMeta &m) {
        if (m.isTrashed() || m.kind == PageKind::Template)
            return false;
        if (!options.inPage.isEmpty() && m.id != options.inPage && !isInside(m.id, options.inPage))
            return false;
        if (options.editedAfter && m.updated < options.editedAfter)
            return false;
        if (options.createdAfter && m.created < options.createdAfter)
            return false;
        return true;
    };
    for (const PageMeta &m : m_pages) {
        if (!allowed(m))
            continue;
        const int s = fuzzyScore(q, m.title.isEmpty() ? title(m.id) : m.title);
        if (s > 0) {
            hits.append({m.id, QString(), s + 1000, true});
            seen.insert(m.id);
        }
    }
    QSqlQuery sql(m_db);
    sql.prepare(QStringLiteral("SELECT id, plain FROM pages WHERE trashed=0 AND plain LIKE ? ESCAPE '\\' LIMIT 400"));
    sql.addBindValue(QLatin1Char('%') + escapeLike(q) + QLatin1Char('%'));
    if (sql.exec()) {
        while (sql.next()) {
            const QString id = sql.value(0).toString();
            const PageMeta *pm = page(id);
            if (!pm || !allowed(*pm))
                continue;
            const QString plain = sql.value(1).toString();
            const int idx = plain.indexOf(q, 0, Qt::CaseInsensitive);
            if (idx < 0)
                continue;
            // A snippet of the line around the match.
            int start = plain.lastIndexOf(QLatin1Char('\n'), idx);
            start = start < 0 ? 0 : start + 1;
            int end = plain.indexOf(QLatin1Char('\n'), idx);
            if (end < 0)
                end = int(plain.size());
            if (idx - start > 40)
                start = idx - 40;
            if (end - idx > 80)
                end = idx + 80;
            QString snippet = plain.mid(start, end - start).simplified();
            if (seen.contains(id)) {
                for (SearchHit &h : hits)
                    if (h.id == id)
                        h.snippet = snippet;
                continue;
            }
            if (!options.titleOnly)
                hits.append({id, snippet, 200 - qMin(idx, 150), false});
        }
    }
    std::sort(hits.begin(), hits.end(), [this, &options](const SearchHit &a, const SearchHit &b) {
        const PageMeta *pa = page(a.id), *pb = page(b.id);
        switch (options.sort) {
        case SearchOptions::EditedNewest:
            if (pa->updated != pb->updated)
                return pa->updated > pb->updated;
            break;
        case SearchOptions::EditedOldest:
            if (pa->updated != pb->updated)
                return pa->updated < pb->updated;
            break;
        case SearchOptions::CreatedNewest:
            if (pa->created != pb->created)
                return pa->created > pb->created;
            break;
        case SearchOptions::CreatedOldest:
            if (pa->created != pb->created)
                return pa->created < pb->created;
            break;
        case SearchOptions::BestMatch:
            break;
        }
        if (a.score != b.score)
            return a.score > b.score;
        const PageMeta *x = page(a.id), *y = page(b.id);
        return (x ? x->visited : 0) > (y ? y->visited : 0);
    });
    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}

// ---- media ---------------------------------------------------------------------

QString Workspace::importMediaData(const QByteArray &data, const QString &suffix)
{
    if (data.isEmpty())
        return {};
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(20));
    QString ext = suffix.toLower();
    if (ext.isEmpty())
        ext = QStringLiteral("png");
    const QString name = hash + QLatin1Char('.') + ext;
    const QString path = mediaPath(name);
    if (!QFileInfo::exists(path)) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(data);
    }
    return name;
}

QString Workspace::importMedia(const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return importMediaData(f.readAll(), QFileInfo(filePath).suffix());
}

QString Workspace::mediaPath(const QString &fileName) const
{
    return QDir(mediaDir()).filePath(QFileInfo(fileName).fileName());
}

// ---- maintenance ---------------------------------------------------------------

QString Workspace::backup(bool force, int keep)
{
    const QString dir = QDir(m_dir).filePath(QStringLiteral("backups"));
    QDir().mkpath(dir);
    const QString stamp = QDate::currentDate().toString(Qt::ISODate);
    const QString target = QDir(dir).filePath(QStringLiteral("workspace-%1.db").arg(stamp));
    if (QFileInfo::exists(target)) {
        if (!force)
            return target;
        QFile::remove(target);
    }
    QString escaped = target;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    if (!exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped)))
        return {};
    QDir d(dir);
    QStringList files = d.entryList({QStringLiteral("workspace-*.db")}, QDir::Files, QDir::Name);
    while (files.size() > keep)
        QFile::remove(d.filePath(files.takeFirst()));
    return target;
}
