#pragma once

#include "Markdown.h"

// HTML into blocks: headings, paragraphs, lists (bulleted, numbered, to-do),
// quotes, code, dividers, images with captions, tables, toggles
// (<details>), callouts (<aside>, Notion's callout figures), columns,
// equations (KaTeX annotations), bookmarks, links to pages and tables of
// contents — including the markup of Notion's HTML export and Evernote's
// ENML. A small lenient parser: no web engine needed.
namespace HtmlImport
{
// What a Notion page header carries besides the title.
struct PageInfo {
    QString cover;             // image source (relative path or URL)
    double coverPosition = 0.5;
    QString icon;              // an emoji, or an image source
};
// `title` receives the page title (<title> or Notion's page-title heading).
QList<Block> toBlocks(const QString &html, const Markdown::ImportOptions &options = {}, QString *title = nullptr,
                      PageInfo *info = nullptr);
}

// Word documents (.docx): paragraphs and headings (by style), lists (from
// numbering), bold/italic/underline/strike, hyperlinks, tables and images.
namespace DocxImport
{
struct Result {
    QList<Block> blocks;
    QString title; // the document's title property, if any
    QString error;
};
// `importImage` receives the image bytes and the file suffix and returns a
// media file name (or empty to drop the image).
Result read(const QByteArray &docx, const std::function<QString(const QByteArray &, const QString &)> &importImage = {});
}

// Evernote exports (.enex): every note with its title, dates, content and
// attached images.
namespace EnexImport
{
struct Note {
    QString title;
    QList<Block> blocks;
    qint64 created = 0;
    QStringList tags;
};
QList<Note> read(const QByteArray &enex, const std::function<QString(const QByteArray &, const QString &)> &importImage = {});
}
