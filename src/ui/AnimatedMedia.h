#pragma once

#include <QImage>
#include <QRectF>
#include <QString>

class QPainter;

// Animated images (GIF, animated WebP) in covers, page icons and image
// blocks play like Notion's. Each file plays once for the whole app; the
// painted areas are repainted on every frame and stop when nothing paints
// them any more (scrolled away, page closed). With "reduce motion" on they
// stay on their first frame.
namespace AnimatedMedia
{
// Whether a media file (in the workspace's media folder) has several frames.
bool isAnimated(const QString &media);
// The current frame, starting playback if needed. Null when not animated.
QImage frame(const QString &media);
// Asks for `rect` (in the painter's coordinates) to be repainted on the next
// frame of `media`. Call it from the paint code that drew the frame.
void repaintOnNextFrame(QPainter &p, const QRectF &rect, const QString &media);
} // namespace AnimatedMedia
