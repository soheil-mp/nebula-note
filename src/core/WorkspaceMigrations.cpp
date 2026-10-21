// Upgrading a workspace file from one format to the next.
//
// The format version lives in PRAGMA user_version. Each step runs in one
// transaction (a failed upgrade leaves the file as it was), and an existing
// workspace is copied to backups/ before its first step runs.
#include "Workspace.h"

#include "Ids.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace
{
// 'NBUL': marks the file as a Nebula Note workspace (PRAGMA application_id).
constexpr int kApplicationId = 0x4E42554C;
constexpr int kFormatVersion = 2;

bool hasColumn(const QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (q.next())
        if (q.value(1).toString() == column)
            return true;
    return false;
}

// Statements that may fail on purpose (probing for a tokenizer).
bool tryExec(const QSqlDatabase &db, const QString &sql)
{
    QSqlQuery q(db);
    return q.exec(sql);
}
} // namespace

int Workspace::formatVersion()
{
    return kFormatVersion;
}

bool Workspace::migrate(bool fresh)
{
    QSqlQuery q(m_db);
    int version = q.exec(QStringLiteral("PRAGMA user_version")) && q.next() ? q.value(0).toInt() : 0;
    q.finish();
    if (version > kFormatVersion) {
        m_error = tr("This workspace was saved by a newer version of Nebula Note. Update the app to open it.");
        return false;
    }
    if (version == kFormatVersion)
        return true;
    if (!fresh) {
        const QString dir = QDir(m_dir).filePath(QStringLiteral("backups"));
        QDir().mkpath(dir);
        QString target = QDir(dir).filePath(QStringLiteral("workspace-before-format-%1.db").arg(kFormatVersion));
        if (QFileInfo::exists(target))
            target = QDir(dir).filePath(QStringLiteral("workspace-before-format-%1-%2.db")
                                            .arg(kFormatVersion)
                                            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
        QString escaped = target;
        escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
        if (!exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped))) {
            m_error = tr("The workspace couldn't be backed up before upgrading it: %1").arg(m_error);
            return false;
        }
    }
    // Inside the steps, batch() joins the step's transaction.
    ++m_batchDepth;
    bool ok = true;
    for (int v = version + 1; v <= kFormatVersion && ok; ++v) {
        ok = m_db.transaction();
        if (ok && v == 2)
            ok = migrateTo2();
        if (ok)
            ok = exec(QStringLiteral("PRAGMA user_version=%1").arg(v));
        if (ok) {
            ok = m_db.commit();
        } else {
            if (m_error.isEmpty())
                m_error = m_db.lastError().text();
            m_db.rollback();
        }
    }
    --m_batchDepth;
    m_pendingSignals.clear();
    m_pendingKeys.clear();
    // An upgrade rewrites most of the file: give the freed space back once.
    if (ok && !fresh)
        exec(QStringLiteral("VACUUM"));
    return ok;
}

// Format 2: content block by block, a full-text index, compressed version
// history, and page revisions.
bool Workspace::migrateTo2()
{
    bool ok = exec(QStringLiteral("CREATE TABLE IF NOT EXISTS blocks(page_id TEXT NOT NULL, id TEXT NOT NULL, sort REAL NOT NULL, "
                                  "data BLOB NOT NULL, PRIMARY KEY(page_id, id)) WITHOUT ROWID"))
              && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS synced_refs(sync_id TEXT NOT NULL, page_id TEXT NOT NULL, "
                                     "PRIMARY KEY(sync_id, page_id)) WITHOUT ROWID"))
              && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS synced_refs_page ON synced_refs(page_id)"))
              && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS search_map(rowid INTEGER PRIMARY KEY, page_id TEXT NOT NULL UNIQUE)"))
              && exec(QStringLiteral("CREATE TABLE IF NOT EXISTS search_dirty(page_id TEXT PRIMARY KEY) WITHOUT ROWID"))
              && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS reminders_page ON reminders(page_id)"))
              && exec(QStringLiteral("CREATE INDEX IF NOT EXISTS versions_page_created ON versions(page_id, created)"));
    if (!ok)
        return false;
    // The search index: trigrams find any part of a word (and CJK text);
    // older SQLite builds fall back to word prefixes, or to a plain table.
    QString tokenizer;
    if (tryExec(m_db, QStringLiteral("CREATE VIRTUAL TABLE search USING fts5(body, tokenize='trigram remove_diacritics 1')"))
        || tryExec(m_db, QStringLiteral("CREATE VIRTUAL TABLE search USING fts5(body, tokenize='trigram')")))
        tokenizer = QStringLiteral("trigram");
    else if (tryExec(m_db, QStringLiteral("CREATE VIRTUAL TABLE search USING fts5(body, tokenize='unicode61 remove_diacritics 2')")))
        tokenizer = QStringLiteral("unicode61");
    else if (!exec(QStringLiteral("CREATE TABLE search(rowid INTEGER PRIMARY KEY, body TEXT NOT NULL DEFAULT '')")))
        return false;
    setMetaValue(QStringLiteral("searchTokenizer"), tokenizer);
    if (!hasColumn(m_db, QStringLiteral("pages"), QStringLiteral("rev")) && !exec(QStringLiteral("ALTER TABLE pages ADD COLUMN rev INTEGER NOT NULL DEFAULT 0")))
        return false;
    if (!hasColumn(m_db, QStringLiteral("versions"), QStringLiteral("rev"))
        && !exec(QStringLiteral("ALTER TABLE versions ADD COLUMN rev INTEGER NOT NULL DEFAULT 0")))
        return false;
    if (!hasColumn(m_db, QStringLiteral("versions"), QStringLiteral("zip"))
        && !exec(QStringLiteral("ALTER TABLE versions ADD COLUMN zip INTEGER NOT NULL DEFAULT 0")))
        return false;

    // Pages: their blocks, synced-block references and search text.
    QSqlQuery pages(m_db);
    pages.setForwardOnly(true);
    if (!pages.exec(QStringLiteral("SELECT id, content, plain, title FROM pages")))
        return false;
    QSqlQuery insertBlock(m_db), insertRef(m_db), insertMap(m_db), insertText(m_db), lastVersion(m_db), markVersion(m_db);
    insertBlock.prepare(QStringLiteral("INSERT OR REPLACE INTO blocks(page_id, id, sort, data) VALUES(?,?,?,?)"));
    insertRef.prepare(QStringLiteral("INSERT OR IGNORE INTO synced_refs(sync_id, page_id) VALUES(?,?)"));
    insertMap.prepare(QStringLiteral("INSERT INTO search_map(page_id) VALUES(?)"));
    insertText.prepare(QStringLiteral("INSERT INTO search(rowid, body) VALUES(?,?)"));
    lastVersion.prepare(QStringLiteral("SELECT id, content, title FROM versions WHERE page_id=? ORDER BY created DESC, id DESC LIMIT 1"));
    markVersion.prepare(QStringLiteral("UPDATE versions SET rev=1 WHERE id=?"));
    while (pages.next()) {
        const QString id = pages.value(0).toString();
        const QString content = pages.value(1).toString();
        QList<Block> blocks = blocksFromJsonString(content);
        QSet<QString> seen;
        for (int i = 0; i < blocks.size(); ++i) {
            Block &b = blocks[i];
            if (b.id.isEmpty() || seen.contains(b.id))
                b.id = Block::newId();
            seen.insert(b.id);
            insertBlock.addBindValue(id);
            insertBlock.addBindValue(b.id);
            insertBlock.addBindValue(double(i + 1));
            insertBlock.addBindValue(QJsonDocument(blockToJson(b)).toJson(QJsonDocument::Compact));
            if (!insertBlock.exec()) {
                m_error = insertBlock.lastError().text();
                return false;
            }
        }
        forEachBlock(blocks, [&](const Block &b) {
            if (b.type == BlockType::Synced && !b.syncId.isEmpty()) {
                insertRef.addBindValue(b.syncId);
                insertRef.addBindValue(id);
                insertRef.exec();
            }
        });
        insertMap.addBindValue(id);
        if (!insertMap.exec()) {
            m_error = insertMap.lastError().text();
            return false;
        }
        insertText.addBindValue(insertMap.lastInsertId());
        insertText.addBindValue(pages.value(2).toString());
        if (!insertText.exec()) {
            m_error = insertText.lastError().text();
            return false;
        }
        // The newest version matching the page as it is counts as current.
        lastVersion.addBindValue(id);
        if (lastVersion.exec() && lastVersion.next() && lastVersion.value(1).toString() == content
            && lastVersion.value(2).toString() == pages.value(3).toString()) {
            markVersion.addBindValue(lastVersion.value(0));
            markVersion.exec();
        }
        lastVersion.finish();
    }
    pages.finish();
    // The old columns stay (older files keep their shape) but hold nothing.
    if (!exec(QStringLiteral("UPDATE pages SET rev=1, content='[]', plain=''")))
        return false;

    // Version history, compressed.
    QSqlQuery versions(m_db), pack(m_db);
    versions.setForwardOnly(true);
    pack.prepare(QStringLiteral("UPDATE versions SET content=?, zip=1 WHERE id=?"));
    if (!versions.exec(QStringLiteral("SELECT id, content FROM versions WHERE zip=0")))
        return false;
    QList<QPair<qint64, QByteArray>> packed;
    while (versions.next())
        packed.append({versions.value(0).toLongLong(), qCompress(versions.value(1).toString().toUtf8(), 1)});
    versions.finish();
    for (const auto &[vid, data] : packed) {
        pack.addBindValue(data);
        pack.addBindValue(vid);
        if (!pack.exec()) {
            m_error = pack.lastError().text();
            return false;
        }
    }
    cleanUp();
    return exec(QStringLiteral("PRAGMA application_id=%1").arg(kApplicationId));
}
