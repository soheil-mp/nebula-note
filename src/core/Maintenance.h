#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

// Upkeep that runs on a worker thread with its own read-only connection, so
// the window never waits for it: backups, the file's integrity check, and
// setting aside media no page uses any more. Results come back on the
// thread of `context`.
namespace Maintenance
{
struct BackupResult {
    QString path;      // the new copy (empty when none was made)
    QString error;     // why not
    bool intact = true; // PRAGMA quick_check of the workspace said "ok"
    QString integrity; // its report otherwise
};

// Copies workspace.db into `backupDir` once a day (or now when `force`),
// keeping the newest `keep`. A failed integrity check makes no copy and
// deletes no older one.
void backup(const QString &workspaceDir, const QString &backupDir, bool force, int keep, QObject *context,
            const std::function<void(const BackupResult &)> &done);

struct MediaResult {
    int moved = 0;        // set aside in media-trash/ now
    int purged = 0;       // deleted from media-trash/
    qint64 freedBytes = 0;
    QString error;
};

// Moves media files no page, row, cover, synced block or saved version
// refers to (and untouched for a day) into media-trash/, and deletes what
// has been there for `keepDays` (all of it when 0).
void sweepMedia(const QString &workspaceDir, int keepDays, QObject *context, const std::function<void(const MediaResult &)> &done);

// Synchronous forms (tests, and the work the async ones do).
BackupResult backupNow(const QString &workspaceDir, const QString &backupDir, bool force, int keep);
MediaResult sweepMediaNow(const QString &workspaceDir, int keepDays);
QString defaultBackupDir(const QString &workspaceDir);
} // namespace Maintenance
