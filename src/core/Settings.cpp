#include "Settings.h"

#include <QSettings>

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

void Settings::load()
{
    QSettings q;
    reduceMotion = q.value(QStringLiteral("appearance/reduceMotion"), false).toBool();
    starfield = qBound(0.0, q.value(QStringLiteral("appearance/starfield"), 1.0).toDouble(), 1.0);
    translucentSidebar = q.value(QStringLiteral("appearance/translucentSidebar"), true).toBool();
    defaultFont = q.value(QStringLiteral("appearance/defaultFont"), QStringLiteral("sans")).toString();
    textScale = qBound(80, q.value(QStringLiteral("appearance/textScale"), 100).toInt(), 140);
    showBacklinks = q.value(QStringLiteral("editor/showBacklinks"), true).toBool();
    spellCheck = q.value(QStringLiteral("editor/spellCheck"), true).toBool();
    sidebarWidth = qBound(200, q.value(QStringLiteral("window/sidebarWidth"), 260).toInt(), 480);
    sidebarCollapsed = q.value(QStringLiteral("window/sidebarCollapsed"), false).toBool();
    geometry = q.value(QStringLiteral("window/geometry")).toByteArray();
    maximized = q.value(QStringLiteral("window/maximized"), false).toBool();
    lastPage = q.value(QStringLiteral("session/lastPage")).toString();
    expanded = q.value(QStringLiteral("session/expanded")).toStringList();
}

void Settings::save()
{
    QSettings q;
    q.setValue(QStringLiteral("appearance/reduceMotion"), reduceMotion);
    q.setValue(QStringLiteral("appearance/starfield"), starfield);
    q.setValue(QStringLiteral("appearance/translucentSidebar"), translucentSidebar);
    q.setValue(QStringLiteral("appearance/defaultFont"), defaultFont);
    q.setValue(QStringLiteral("appearance/textScale"), textScale);
    q.setValue(QStringLiteral("editor/showBacklinks"), showBacklinks);
    q.setValue(QStringLiteral("editor/spellCheck"), spellCheck);
    q.setValue(QStringLiteral("window/sidebarWidth"), sidebarWidth);
    q.setValue(QStringLiteral("window/sidebarCollapsed"), sidebarCollapsed);
    q.setValue(QStringLiteral("window/geometry"), geometry);
    q.setValue(QStringLiteral("window/maximized"), maximized);
    q.setValue(QStringLiteral("session/lastPage"), lastPage);
    q.setValue(QStringLiteral("session/expanded"), expanded);
}

void Settings::apply()
{
    save();
    emit changed();
}
