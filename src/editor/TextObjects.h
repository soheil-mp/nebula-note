#pragma once

#include "BlockFormat.h"

#include <QObject>
#include <QTextObjectInterface>

class QTextDocument;

#include <QHash>

#include <functional>

// State the object renderer shares with its editor.
struct ObjectContext {
    BlockFormat::Style style;
    QString pageId;        // the page being edited (breadcrumbs)
    QString hoveredPageId; // page link or mention under the mouse
    int hoveredImage = -1; // document position of the image under the mouse
    int selectedImage = -1;
    int hoveredObject = -1; // document position of the block object under the mouse
    int hoveredPart = -1;   // a part of it (a table-of-contents entry…)
    // Height of an embedded widget (inline databases), by block id.
    QHash<QString, int> embedHeights;
};

// A heading for tables of contents.
struct TocEntry {
    int level;      // 1…4
    QString text;
    int block;      // block number
};

// Draws Nebula's block and inline objects inside a QTextDocument: dividers,
// page links, @mentions and images. Titles and icons are looked up live, so
// renaming a page updates every place that points at it.
class TextObjects : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)
public:
    TextObjects(ObjectContext *context, QObject *parent = nullptr);

    void install(QTextDocument *doc);

    QSizeF intrinsicSize(QTextDocument *doc, int posInDocument, const QTextFormat &format) override;
    void drawObject(QPainter *painter, const QRectF &rect, QTextDocument *doc, int posInDocument,
                    const QTextFormat &format) override;

    // Image size on disk (read from the header, cached).
    static QSize naturalSize(const QString &media);
    // Headings of a document in order (columns included).
    static QList<TocEntry> headings(QTextDocument *doc);
    // Layout of the parts of block objects, for hit testing.
    static double tocRowHeight(const BlockFormat::Style &style);
    static double tocTop(const BlockFormat::Style &style);
    static double captionHeight(const BlockFormat::Style &style);
    // Breadcrumb entries: the rect of each page's title (icon included).
    static QList<QPair<QRectF, QString>> breadcrumbParts(const QRectF &body, const QString &pageId, const BlockFormat::Style &style);
    // The rect of a button block's button inside its object rect.
    static QRectF buttonRect(const QRectF &objectRect, const QString &label, const BlockFormat::Style &style);
    // Width available to a block-level object at `pos`.
    static double availableWidth(QTextDocument *doc, int pos);
    static QFont mentionFont(QTextDocument *doc, int pos, const BlockFormat::Style &style);

private:
    ObjectContext *m_ctx;
};
