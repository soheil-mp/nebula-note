#pragma once

#include <QString>
#include <QStringList>

#include <functional>

class Workspace;

// Imports a Notion export ("Markdown & CSV" or "HTML", zipped or not), a
// folder of Markdown or HTML files, a single .md / .txt / .html / .csv
// file, a Word document (.docx) or an Evernote export (.enex).
namespace Importer
{
struct Result {
    QString rootId; // the page to open afterwards
    int pages = 0;
    int databases = 0;
    int rows = 0;
    int images = 0;
    int files = 0; // other attachments (PDFs, videos…) copied into the workspace
    QString error;

    bool ok() const { return error.isEmpty() && !rootId.isEmpty(); }
    QString summary() const;
};

Result importPath(Workspace &ws, const QString &path, const QString &parentId = {});

// Downloads remote images (covers, icons, pictures linked by URL) so they're
// stored in the workspace like everything else. The UI installs one; without
// it remote images stay links. Returns the bytes and their MIME type.
using Fetcher = std::function<QByteArray(const QString &url, QString *mimeType)>;
void setFetcher(const Fetcher &fetcher);

// Strips Notion's " <32 hex>" id suffix from a file or folder name.
QString stripNotionId(const QString &name);
}
