#pragma once

#include "Blocks.h"
#include "Schema.h"
#include "Types.h"

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>

// The workspace: every page, database and row, stored in one SQLite file.
// Page metadata is cached in memory; content, properties and schemas are
// read on demand. A page's content is stored block by block, so saving
// writes only the blocks that changed. All writes go through here and
// announce themselves with signals so every view stays in sync.
class Workspace : public QObject, public DbLookup
{
    Q_OBJECT
public:
    explicit Workspace(QObject *parent = nullptr);
    ~Workspace() override;

    // The process-wide workspace (the most recently opened one).
    static Workspace *instance();

    bool open(const QString &directory);
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    QString directory() const { return m_dir; }
    QString mediaDir() const;
    QString lastError() const { return m_error; }
    bool isFresh() const { return m_fresh; } // created by this open()

    QString name() const;
    void setName(const QString &name);
    QString metaValue(const QString &key, const QString &fallback = {}) const;
    void setMetaValue(const QString &key, const QString &value);

    // ---- metadata ------------------------------------------------------------
    // Pointers stay valid until the next call that creates or deletes pages.
    const PageMeta *page(const QString &id) const;
    bool exists(const QString &id) const { return page(id) != nullptr; }
    QString title(const QString &id) const; // "New page" (or "Untitled" for rows and databases) when empty
    QList<const PageMeta *> children(const QString &parentId) const; // live pages and databases, sorted
    QList<const PageMeta *> rows(const QString &databaseId) const;   // live rows, sorted
    bool hasChildren(const QString &id) const;
    QList<const PageMeta *> favorites() const;
    QList<const PageMeta *> recent(int limit = 12) const;
    QList<const PageMeta *> trashedRoots() const;
    QList<const PageMeta *> allLive() const;
    // Ancestors from the top level down to the parent (not including id).
    QStringList ancestors(const QString &id) const;
    // id and everything below it (children, rows, their children…).
    QStringList subtree(const QString &id, bool includeTrashed = true) const;
    bool isInside(const QString &id, const QString &ancestorId) const;

    // ---- create --------------------------------------------------------------
    QString createPage(const QString &parentId, const QString &title = {}, const QList<Block> &content = {},
                       const QString &afterId = {});
    QString createDatabase(const QString &parentId, const QString &title, const Schema &schema,
                           const QString &afterId = {});
    QString createRow(const QString &databaseId, const QString &title = {}, const QJsonObject &props = {},
                      const QList<Block> &content = {});
    QString duplicate(const QString &id);

    // ---- read / write --------------------------------------------------------
    QList<Block> content(const QString &id) const;
    void setContent(const QString &id, const QList<Block> &blocks, QObject *source = nullptr);
    QJsonObject props(const QString &id) const;
    void preloadProps(const QString &databaseId) const; // caches every row's properties in one go
    void setProps(const QString &id, const QJsonObject &props);
    void setProp(const QString &id, const QString &propId, const QJsonValue &value);
    Schema schema(const QString &databaseId) const;
    void setSchema(const QString &databaseId, const Schema &schema);

    // Relations. A two-way relation adds the paired property to the other
    // database; editing either side updates both.
    QString addRelation(const QString &databaseId, const QString &name, const QString &targetDb, bool twoWay,
                        const QString &pairName = {});
    void setRelationTwoWay(const QString &databaseId, const QString &propId, bool twoWay, const QString &pairName = {});
    void removeProperty(const QString &databaseId, const QString &propId);
    // Sub-items ("Parent item" / "Sub-items") and dependencies ("Blocked by" /
    // "Blocking") are self-relations; these create them once.
    void enableSubItems(const QString &databaseId, bool on);
    void enableDependencies(const QString &databaseId, bool on);
    int nextUniqueId(const QString &databaseId, const QString &propId) const;

    // DbLookup
    const PageMeta *lookupPage(const QString &id) const override { return page(id); }
    QJsonObject lookupProps(const QString &id) const override { return props(id); }
    const Schema *lookupSchema(const QString &databaseId) const override;
    QString lookupPersonName(const QString &personId) const override { return personName(personId); }
    QString lookupMe() const override;
    PageOptions options(const QString &id) const;
    void setOptions(const QString &id, const PageOptions &options);
    // Converts an empty page into a database (the "Table / Board…" starters).
    void convertToDatabase(const QString &id, const Schema &schema);

    void setTitle(const QString &id, const QString &title);
    void setIcon(const QString &id, const QString &icon);
    void setCover(const QString &id, const QString &cover, double position = 0.5);
    void setCoverPosition(const QString &id, double position);
    void setFavorite(const QString &id, bool favorite);
    void moveFavorite(const QString &id, int toIndex);
    void markVisited(const QString &id, qint64 when = 0);
    // Re-parents and reorders. `index` is the position among the new
    // parent's children (-1 = last). Refuses to move a page into itself.
    bool move(const QString &id, const QString &newParentId, int index = -1);
    // Places a row before `beforeId` (empty = last) in its database.
    void moveRow(const QString &id, const QString &beforeId);

    // ---- trash ---------------------------------------------------------------
    void trash(const QString &id);
    void restore(const QString &id);
    void deleteForever(const QString &id);
    void emptyTrash();

    // ---- links & search ------------------------------------------------------
    QStringList backlinks(const QString &id) const;
    struct SearchHit {
        QString id;
        QString snippet; // surrounding text for content matches
        int score = 0;
        bool titleMatch = false;
    };
    // Notion's search filters and sorts.
    struct SearchOptions {
        enum Sort { BestMatch, EditedNewest, EditedOldest, CreatedNewest, CreatedOldest };
        bool titleOnly = false;
        QString inPage;          // only pages inside this one
        qint64 editedAfter = 0;  // msecs; 0 = any
        qint64 createdAfter = 0;
        Sort sort = BestMatch;
    };
    QList<SearchHit> search(const QString &query, int limit = 40) const { return search(query, SearchOptions{}, limit); }
    QList<SearchHit> search(const QString &query, const SearchOptions &options, int limit = 40) const;
    static int fuzzyScore(const QString &needle, const QString &haystack);

    // ---- media ---------------------------------------------------------------
    // Copies a file into the media folder (content addressed) and returns its
    // file name, or an empty string on failure.
    QString importMedia(const QString &filePath);
    QString importMediaData(const QByteArray &data, const QString &suffix);
    QString mediaPath(const QString &fileName) const;

    // ---- synced blocks ------------------------------------------------------
    // Synced content lives outside pages; a page stores a placeholder block.
    QList<Block> syncedContent(const QString &syncId) const;
    QString syncedOrigin(const QString &syncId) const;
    QStringList syncedPages(const QString &syncId) const; // live pages showing it
    QString createSynced(const QString &originPage, const QList<Block> &blocks);

    // ---- comments -----------------------------------------------------------
    struct Comment {
        QString id;
        QString thread; // id of the thread's first comment
        QString pageId;
        QString blockId; // empty: page discussion
        QString quote;   // the commented text
        QString body;
        qint64 created = 0;
        qint64 edited = 0;
        bool resolved = false;
    };
    QList<Comment> comments(const QString &pageId) const;
    // The newest comments across the workspace (the inbox), live pages only.
    QList<Comment> recentComments(int limit = 30) const;
    QString addComment(const QString &pageId, const QString &blockId, const QString &quote, const QString &body,
                       const QString &thread = {});
    void editComment(const QString &id, const QString &body);
    void deleteComment(const QString &id); // the first comment takes its thread along
    void resolveThread(const QString &thread, bool resolved);
    int openThreadCount(const QString &pageId) const;

    // ---- version history ----------------------------------------------------
    struct Version {
        qint64 id = 0;
        qint64 created = 0;
        QString title;
        QString name;
    };
    QList<Version> versions(const QString &pageId) const;
    QList<Block> versionContent(qint64 versionId, QString *title = nullptr) const;
    // Stores a version when the content changed since the last one and, unless
    // forced, at least `minMinutes` have passed. Returns true when stored.
    bool snapshot(const QString &pageId, bool force = false, int minMinutes = 10);
    void restoreVersion(qint64 versionId);
    void nameVersion(qint64 versionId, const QString &name);

    // ---- reminders ----------------------------------------------------------
    struct Reminder {
        QString id;
        QString pageId;
        QString text;
        qint64 at = 0;
        bool done = false;
    };
    QList<Reminder> reminders(bool includeDone = false) const;
    void setReminderDone(const QString &id, bool done);

    // ---- people (Person properties, "Created by") ---------------------------
    struct Person {
        QString id;
        QString name;
    };
    QList<Person> people() const;
    void setPeople(const QList<Person> &people);
    QString personName(const QString &id) const;

    // ---- database templates -------------------------------------------------
    QList<const PageMeta *> templates(const QString &databaseId) const;
    QString createTemplate(const QString &databaseId, const QString &title = {});
    // A new row with the template's properties and content.
    QString createRowFromTemplate(const QString &databaseId, const QString &templateId);

    // ---- maintenance ---------------------------------------------------------
    // Copies the database to backups/ once a day, keeping the last `keep`.
    QString backup(bool force = false, int keep = 7);
    // Runs `fn` inside one transaction; signals are sent afterwards.
    void batch(const std::function<void()> &fn);
    // Removes what deleted pages left behind (versions, comments, search
    // entries…). Runs once a day by itself.
    void cleanUp();
    // The database file's format version (PRAGMA user_version).
    static int formatVersion();

signals:
    void treeChanged();                            // pages added, moved, trashed, restored, deleted
    void pageChanged(const QString &id);           // title, icon, cover, favourite, options
    void contentChanged(const QString &id, QObject *source);
    void propsChanged(const QString &id);
    void schemaChanged(const QString &databaseId);
    void trashChanged();
    void favoritesChanged();
    void commentsChanged(const QString &pageId);
    void remindersChanged();
    void versionsChanged(const QString &pageId);

private:
    // One top-level block as stored: its sort key and its JSON.
    struct StoredBlock {
        QString id;
        double sort = 0;
        Block block;
        QByteArray json;
    };
    bool exec(const QString &sql, const QVariantList &args = {}) const;
    bool migrate(bool fresh);
    bool migrateTo2();
    void loadMeta();
    QString insertPage(const PageMeta &meta, const QList<Block> &stored, const QJsonObject &props, const Schema *schema);
    QList<StoredBlock> storedBlocks(const QString &id) const;
    QByteArray storedJson(const QString &id) const; // the page's stored blocks as one JSON array
    void writeBlocks(const QString &id, const QList<Block> &stored);
    void dropBlocks(const QString &id);
    void updateSyncedRefs(const QString &id, const QList<Block> &stored);
    void markSearchDirty(const QString &id);
    void flushSearch() const;
    double sortAfter(const QString &parentId, const QString &afterId, bool rowsOnly) const;
    void touch(const QString &id);
    void updateLinks(const QString &id, const QList<Block> &blocks);
    void writePropsRaw(const QString &id, const QJsonObject &props);
    void syncRelation(const QString &rowId, const Property &p, const QStringList &oldIds, const QStringList &newIds, int depth);
    void updateRowReminders(const QString &id, const QJsonObject &props);
    QList<Block> collapseSynced(const QString &pageId, const QList<Block> &blocks);
    QList<Block> expandSynced(const QList<Block> &blocks, int nesting = 0) const;
    void updateReminders(const QString &pageId, const QList<Block> &blocks);
    void rebuildChildIndex() const;
    void emitLater(const std::function<void()> &signal);
    void emitLater(const QString &key, const std::function<void()> &signal); // once per key in a batch

    QSqlDatabase m_db;
    QString m_connection;
    QString m_dir;
    QString m_error;
    bool m_fresh = false;
    QHash<QString, PageMeta> m_pages;
    mutable QHash<QString, QStringList> m_children; // parent → child ids (all, including trashed)
    mutable bool m_childIndexDirty = true;
    mutable QHash<QString, std::shared_ptr<Schema>> m_schemaCache;
    mutable QHash<QString, QJsonObject> m_propsCache;
    // Stored blocks of recently read or written pages (newest last).
    mutable QHash<QString, QList<StoredBlock>> m_blockCache;
    mutable QStringList m_blockCacheOrder;
    // Pages whose search text is out of date: indexed a moment after
    // editing stops, and before any search.
    mutable QSet<QString> m_searchDirty;
    QTimer *m_searchTimer = nullptr;
    QString m_tokenizer; // "trigram", "unicode61" or "" (no FTS5: plain LIKE)
    int m_batchDepth = 0;
    QList<std::function<void()>> m_pendingSignals;
    QSet<QString> m_pendingKeys;
};
