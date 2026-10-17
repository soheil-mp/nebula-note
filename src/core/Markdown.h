#pragma once

#include "Blocks.h"

#include <functional>

namespace Markdown
{
struct ExportOptions {
    // Link target for a page id (mentions and page blocks). Empty: plain title.
    std::function<QString(const QString &pageId)> pageLink;
    // Title for a page id.
    std::function<QString(const QString &pageId)> pageTitle;
    // Link target for a media file. Empty: "media/<file>".
    std::function<QString(const QString &media)> mediaLink;
};

QString fromBlocks(const QList<Block> &blocks, const ExportOptions &options = {});
QString spansToMarkdown(const QList<Span> &spans, const ExportOptions &options = {});

struct ImportOptions {
    // Turns a link target into a page id (Notion export links). Empty: keep the link.
    std::function<QString(const QString &href)> resolvePage;
    // Imports an image reference and returns a media file name. Empty: drop it.
    std::function<QString(const QString &src)> importImage;
    // Imports a linked attachment (a PDF, video, any file in the export) into
    // the workspace and returns its media file name, setting the block type
    // to use (File, Pdf, Video, Audio or Image). Empty: not an attachment.
    std::function<QString(const QString &href, BlockType *type)> importFile;
    // A Notion block or page id (32 hex digits, dashes allowed) to the id of
    // the imported page or database. Empty: unknown.
    std::function<QString(const QString &notionId)> resolveNotionId;
};

// Links in `spans` that point at attachments are imported (through
// `importFile`) and returned as file blocks at `depth`; their links are
// removed from the text.
QList<Block> extractAttachments(QList<Span> &spans, const ImportOptions &options, int depth);

QList<Block> toBlocks(const QString &markdown, const ImportOptions &options = {});
QList<Span> parseInline(const QString &text, const ImportOptions &options = {});

// The first "# Heading" of a document, used as a page title by importers.
// Removes it from `markdown` when found.
QString takeTitle(QString &markdown);
}
