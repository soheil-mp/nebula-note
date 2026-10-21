#include "Maintenance.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>

namespace Maintenance
{
namespace
{
// A connection of this thread's own (Qt's connections can't cross threads).
class Connection
{
public:
    explicit Connection(const QString &file)
        : m_name(QStringLiteral("maintenance-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
    {
        m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        m_db.setDatabaseName(file);
        m_db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
        m_db.open();
    }
    ~Connection()
    {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_name);
    }
    QSqlDatabase &db() { return m_db; }

private:
    QString m_name;
    QSqlDatabase m_db;
};

// Runs `work` on a new thread and hands its result to `done` on `context`'s.
template<typename Result>
void runAsync(QObject *context, std::function<Result()> work, std::function<void(const Result &)> done)
{
    QPointer<QObject> guard(context);
    QThread *thread = QThread::create([guard, work, done] {
        const Result r = work();
        if (guard)
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, done, r] {
                    if (guard && done)
                        done(r);
                },
                Qt::QueuedConnection);
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start(QThread::LowPriority);
}

const QRegularExpression &mediaName()
{
    // Media files are named by their content: 20 hex digits and a suffix.
    static const QRegularExpression re(QStringLiteral("[0-9a-f]{20}\\.[A-Za-z0-9]{1,10}"));
    return re;
}

bool isMediaName(const QString &name)
{
    static const QRegularExpression re(QRegularExpression::anchoredPattern(QStringLiteral("[0-9a-f]{20}\\.[A-Za-z0-9]{1,10}")));
    return re.match(name).hasMatch();
}

void collectNames(const QString &text, QSet<QString> &into)
{
    auto it = mediaName().globalMatch(text);
    while (it.hasNext())
        into.insert(it.next().captured(0));
}
} // namespace

QString defaultBackupDir(const QString &workspaceDir)
{
    return QDir(workspaceDir).filePath(QStringLiteral("backups"));
}

BackupResult backupNow(const QString &workspaceDir, const QString &backupDir, bool force, int keep)
{
    BackupResult r;
    const QString dir = backupDir.isEmpty() ? defaultBackupDir(workspaceDir) : backupDir;
    if (!QDir().mkpath(dir)) {
        r.error = QObject::tr("Couldn't create %1").arg(dir);
        return r;
    }
    const QString target = QDir(dir).filePath(QStringLiteral("workspace-%1.db").arg(QDate::currentDate().toString(Qt::ISODate)));
    if (QFileInfo::exists(target) && !force)
        return r; // today's copy is there
    Connection c(QDir(workspaceDir).filePath(QStringLiteral("workspace.db")));
    if (!c.db().isOpen()) {
        r.error = c.db().lastError().text();
        return r;
    }
    // A damaged file is reported and left alone, with every older copy kept.
    QSqlQuery check(c.db());
    if (check.exec(QStringLiteral("PRAGMA quick_check")) && check.next()) {
        r.integrity = check.value(0).toString();
        r.intact = r.integrity == QLatin1String("ok");
    }
    check.finish();
    if (!r.intact)
        return r;
    const QString temp = target + QStringLiteral(".part");
    QFile::remove(temp);
    QString escaped = temp;
    escaped.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery vacuum(c.db());
    if (!vacuum.exec(QStringLiteral("VACUUM INTO '%1'").arg(escaped))) {
        r.error = vacuum.lastError().text();
        QFile::remove(temp);
        return r;
    }
    // Replace today's copy only once the new one is complete.
    QFile::remove(target);
    if (!QFile::rename(temp, target)) {
        r.error = QObject::tr("Couldn't write %1").arg(target);
        QFile::remove(temp);
        return r;
    }
    r.path = target;
    QDir d(dir);
    QStringList files = d.entryList({QStringLiteral("workspace-[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9].db")}, QDir::Files, QDir::Name);
    while (files.size() > qMax(1, keep))
        QFile::remove(d.filePath(files.takeFirst()));
    return r;
}

MediaResult sweepMediaNow(const QString &workspaceDir, int keepDays)
{
    MediaResult r;
    const QDir media(QDir(workspaceDir).filePath(QStringLiteral("media")));
    const QString trashPath = QDir(workspaceDir).filePath(QStringLiteral("media-trash"));
    // Everything that may name a media file, as text: blocks, properties
    // (files), covers and icons, synced blocks, and saved versions (so a
    // restored version still has its images). Trashed pages count too.
    QSet<QString> used;
    {
        Connection c(QDir(workspaceDir).filePath(QStringLiteral("workspace.db")));
        if (!c.db().isOpen()) {
            r.error = c.db().lastError().text();
            return r;
        }
        const QStringList queries = {QStringLiteral("SELECT data FROM blocks"), QStringLiteral("SELECT props || ' ' || cover || ' ' || icon FROM pages"),
                                     QStringLiteral("SELECT content FROM synced"), QStringLiteral("SELECT content FROM pages WHERE content != '[]'")};
        for (const QString &sql : queries) {
            QSqlQuery q(c.db());
            q.setForwardOnly(true);
            if (!q.exec(sql)) {
                r.error = q.lastError().text();
                return r; // unsure what's used: touch nothing
            }
            while (q.next())
                collectNames(QString::fromUtf8(q.value(0).toByteArray()), used);
        }
        QSqlQuery v(c.db());
        v.setForwardOnly(true);
        if (!v.exec(QStringLiteral("SELECT content, zip FROM versions"))) {
            r.error = v.lastError().text();
            return r;
        }
        while (v.next()) {
            const QByteArray data = v.value(0).toByteArray();
            collectNames(QString::fromUtf8(v.value(1).toInt() ? qUncompress(data) : data), used);
        }
    }
    const QDateTime dayAgo = QDateTime::currentDateTime().addDays(-1);
    QDir().mkpath(trashPath);
    const QDir trash(trashPath);
    for (const QFileInfo &f : media.entryInfoList(QDir::Files)) {
        // Only files the app named itself; anything else stays put.
        if (!isMediaName(f.fileName()))
            continue;
        if (used.contains(f.fileName()) || f.lastModified() > dayAgo)
            continue;
        const QString to = trash.filePath(f.fileName());
        QFile::remove(to);
        if (QFile::rename(f.filePath(), to)) {
            // The trash keeps it for `keepDays` from now.
            QFile moved(to);
            if (moved.open(QIODevice::ReadWrite))
                moved.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
            ++r.moved;
        }
    }
    const QDateTime expiry = QDateTime::currentDateTime().addDays(-keepDays);
    for (const QFileInfo &f : trash.entryInfoList(QDir::Files)) {
        if (keepDays > 0 && f.lastModified() > expiry)
            continue;
        if (used.contains(f.fileName()))
            continue; // needed again: Workspace::mediaPath() brings it back
        const qint64 size = f.size();
        if (QFile::remove(f.filePath())) {
            ++r.purged;
            r.freedBytes += size;
        }
    }
    return r;
}

void backup(const QString &workspaceDir, const QString &backupDir, bool force, int keep, QObject *context,
            const std::function<void(const BackupResult &)> &done)
{
    runAsync<BackupResult>(context, [=] { return backupNow(workspaceDir, backupDir, force, keep); }, done);
}

void sweepMedia(const QString &workspaceDir, int keepDays, QObject *context, const std::function<void(const MediaResult &)> &done)
{
    runAsync<MediaResult>(context, [=] { return sweepMediaNow(workspaceDir, keepDays); }, done);
}
} // namespace Maintenance
