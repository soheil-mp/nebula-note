#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

class Workspace;

namespace Exporter
{
// Equations and diagrams are drawn by the UI layer; it installs renderers at
// start-up. Without them, HTML and PDF show the TeX or Mermaid source.
struct Renderers {
    std::function<QImage(const QString &tex, bool display, const QColor &color)> equation;
    std::function<QImage(const QString &mermaid, int maxWidth)> diagram; // light on transparent
};
void setRenderers(const Renderers &renderers);

struct Result {
    QString path; // the main file or folder written
    int files = 0;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

// Markdown: each page becomes "Title.md" with its sub-pages in "Title/";
// databases become "Title.csv" plus a folder with one page per row; images
// go to "media/". With `recursive` false only the page itself is written.
Result exportMarkdown(Workspace &ws, const QStringList &pageIds, const QString &directory, bool recursive = true);

// A self-contained HTML file (images embedded) in the Nebula look.
Result exportHtml(Workspace &ws, const QString &pageId, const QString &filePath);

// A print-friendly PDF (light paper, A4).
Result exportPdf(Workspace &ws, const QString &pageId, const QString &filePath);

// The HTML body for a page; `forPrint` uses a light palette and markup that
// QTextDocument understands.
// Rendered images (equations, diagrams) go into `resources` for print, keyed
// by the names the HTML uses; on screen they are embedded as data URLs.
QString pageHtml(Workspace &ws, const QString &pageId, bool forPrint, QHash<QString, QImage> *resources = nullptr);

// A file name without characters that are awkward on common file systems.
QString safeFileName(const QString &title);
}
