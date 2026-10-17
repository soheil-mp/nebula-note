#pragma once

#include <QJsonObject>
#include <QString>

enum class PageKind { Page = 0, Database = 1, Row = 2, Template = 3 }; // Template: a database row template

// Everything the sidebar, breadcrumbs and pickers need about a page, kept in
// memory for the whole workspace. Content, properties and schema are loaded
// on demand.
struct PageMeta {
    QString id;
    QString parentId; // empty: top level of the workspace
    PageKind kind = PageKind::Page;
    QString title;
    QString icon;  // "glyph:<name>", an emoji, or empty
    QString cover; // "cosmos:<scene>:<seed>", "media:<file>", or empty
    double coverPos = 0.5;
    double sort = 0;
    double favorite = 0; // > 0: position in Favorites
    qint64 created = 0;
    qint64 updated = 0;
    qint64 visited = 0;
    qint64 trashed = 0; // msecs since epoch when moved to trash, 0 if live

    bool isTrashed() const { return trashed != 0; }
    bool isDatabase() const { return kind == PageKind::Database; }
    bool isRow() const { return kind == PageKind::Row; }
    bool isTemplate() const { return kind == PageKind::Template; }
};

// Per-page presentation settings (the ••• menu in Notion).
struct PageOptions {
    QString font = QStringLiteral("sans"); // sans | serif | mono
    bool smallText = false;
    bool fullWidth = false;
    bool locked = false;
    bool showToc = true;          // table of contents in the margin
    bool showDiscussion = true;   // page comments under the title
    QString backlinks = QStringLiteral("expanded"); // expanded | popover | off

    QJsonObject toJson() const;
    static PageOptions fromJson(const QJsonObject &o);
};
