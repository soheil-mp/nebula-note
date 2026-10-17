#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QRectF>
#include <QString>
#include <QStringList>

class QPainter;

// Nebula's monochrome line icons (24×24 grid, round strokes), drawn as SVG
// and tinted on demand.
namespace Icons
{
bool has(const QString &name);
QPixmap pixmap(const QString &name, int size, const QColor &color, qreal dpr = 1.0);
QIcon icon(const QString &name, const QColor &color = QColor());
void paint(QPainter &p, const QRectF &rect, const QString &name, const QColor &color);

// Glyphs offered as page and callout icons, grouped for the picker.
struct Group {
    QString title;
    QStringList names;
};
QList<Group> pageGlyphGroups();
QStringList pageGlyphs();

// Paints a page icon ("glyph:<name>" or an emoji) centred in `rect`.
// Empty icons fall back to `fallback` (a glyph name) when given.
void paintPageIcon(QPainter &p, const QRectF &rect, const QString &icon, const QColor &color,
                   const QString &fallback = {});

// Emoji offered in the picker, by category.
QList<Group> emojiGroups();
}
