#pragma once

// Helpers shared by the database layouts.
#include "core/Schema.h"

#include <QCoreApplication>
#include <QImage>
#include <QRectF>

class QPainter;

namespace DbViewsPrivate
{
inline QString tr(const char *s)
{
    return QCoreApplication::translate("DbViews", s);
}

// A small planet, a line of text and an optional button.
void paintEmptyState(QPainter &p, const QRectF &rect, const QString &text, const QString &button, QRectF *buttonRect);
// Adds a property of a type and makes the view group by it (or date by it).
void addPropertyOfType(const QString &dbId, const QString &viewId, PropType type, bool groupBy);
// A card preview image, scaled to cover (or fit) the size; cached by media.
QImage previewImage(const QString &media, const QSizeF &size, bool fit);
// Paints a preview image into a rounded-top area.
void paintPreview(QPainter &p, const QRectF &area, const QImage &img, bool fit);
// What a card shows as its preview: the media file, or "" for none; page
// text comes back in `text` for the "content" preview.
QString cardMedia(const View &view, const Schema &schema, const RowRef &row, QString *text);
// The board column wash for an option shade.
QColor shadeWash(int shade);
} // namespace DbViewsPrivate
