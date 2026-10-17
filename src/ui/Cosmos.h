#pragma once

#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QStringList>

class QPainter;
class QPointF;
class QWidget;

// The "space" layer of the design: a seamless starfield, procedural cover
// scenes and small celestial ornaments, all in grayscale.
namespace Cosmos
{
// Stars are anchored to window coordinates so neighbouring widgets that each
// paint the field line up.
void paintStars(QPainter &p, const QRect &rect, const QPoint &originInWindow, double intensity);
void paintStars(QPainter &p, const QRect &rect, const QWidget *widget, double intensity);

// Soft gray haze for large empty surfaces.
void paintNebulaHaze(QPainter &p, const QRectF &rect, double strength = 1.0, int seed = 7);

// ---- covers -------------------------------------------------------------------
// Cover specs: "cosmos:<scene>:<seed>" or "media:<file>".
QStringList coverScenes();
QString coverSceneName(const QString &scene);
QString randomCover();
QString coverSpec(const QString &scene, int seed);
// Paints a cover filling `rect`. `position` (0…1) picks the visible band of
// image covers that are taller than the rect.
void paintCover(QPainter &p, const QRectF &rect, const QString &spec, double position = 0.5);
// Cached rendering of a scene at a size.
QPixmap coverPixmap(const QString &spec, const QSize &size, qreal dpr);

// ---- ornaments ----------------------------------------------------------------
// A burst of star sparks at `center`; t runs 0 → 1.
void paintSparks(QPainter &p, const QPointF &center, double t, double size = 1.0);
// A dark sphere lit from the upper left, with a faint ring.
void paintPlanet(QPainter &p, const QPointF &center, double radius, double light = 1.0, bool ring = true);
// A huge planet rising from the bottom of `rect` with a glowing rim.
void paintHorizon(QPainter &p, const QRectF &rect, double strength = 1.0);
// A four-pointed star.
void paintSparkle(QPainter &p, const QPointF &center, double radius, const QColor &color);
}
