#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

// User preferences, stored with QSettings (~/.config/nebula/nebula.conf).
class Settings : public QObject
{
    Q_OBJECT
public:
    static Settings &instance();

    bool reduceMotion = false;
    double starfield = 1.0; // 0 … 1
    bool translucentSidebar = true;
    int sidebarWidth = 260;
    bool sidebarCollapsed = false;
    QString lastPage;
    QByteArray geometry;
    bool maximized = false;
    QString defaultFont = QStringLiteral("sans");
    int textScale = 100; // percent
    bool showBacklinks = true;
    bool spellCheck = true;
    QStringList expanded; // sidebar pages left open
    QString backupDir;    // empty: backups/ in the workspace folder
    int keepBackups = 7;  // daily copies kept

    void load();
    void save();
    // Saves and tells everyone that looks depend on settings.
    void apply();

signals:
    void changed();

private:
    Settings() = default;
};
