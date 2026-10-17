#pragma once

#include <QList>
#include <QString>

class Workspace;

// Built-in page and database templates, and the workspace new users start with.
namespace Templates
{
struct Info {
    QString id;
    QString name;
    QString description;
    QString icon; // glyph name
    bool database = false;
};

QList<Info> all();
// Fills an empty page with a template (database templates convert the page).
void apply(Workspace &ws, const QString &templateId, const QString &pageId);
// The pages a fresh workspace starts with. Returns the welcome page.
QString createSampleWorkspace(Workspace &ws);
}
