#include "Cosmos.h"

#include "AnimatedMedia.h"

#include "core/Workspace.h"

#include <QApplication>
#include <QHash>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QWidget>
#include <QtMath>

namespace
{
constexpr int kTile = 512;

QPixmap buildTile(qreal dpr)
{
    QPixmap pm(QSize(kTile, kTile) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    QRandomGenerator rng(0x9e6b1a);
    auto rnd = [&](double a, double b) { return a + (b - a) * rng.generateDouble(); };
    // Every star is drawn with its copies across tile edges: seamless tiling.
    auto wrapped = [&](const QPointF &pt, double reach, const auto &draw) {
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy) {
                const QPointF q = pt + QPointF(dx * kTile, dy * kTile);
                if (q.x() + reach < 0 || q.x() - reach > kTile || q.y() + reach < 0 || q.y() - reach > kTile)
                    continue;
                draw(q);
            }
    };
    for (int i = 0; i < 230; ++i) {
        const QPointF pt(rnd(0, kTile), rnd(0, kTile));
        const double r = rnd(0.35, 0.75);
        const int g = int(rnd(190, 245));
        const QColor c(g, g, qMin(255, g + 6), int(rnd(40, 115)));
        wrapped(pt, r, [&](const QPointF &q) {
            p.setBrush(c);
            p.drawEllipse(q, r, r);
        });
    }
    for (int i = 0; i < 42; ++i) {
        const QPointF pt(rnd(0, kTile), rnd(0, kTile));
        const double r = rnd(0.75, 1.15);
        const int g = int(rnd(215, 255));
        const QColor c(g, g, qMin(255, g + 4), int(rnd(120, 200)));
        wrapped(pt, r, [&](const QPointF &q) {
            p.setBrush(c);
            p.drawEllipse(q, r, r);
        });
    }
    for (int i = 0; i < 8; ++i) {
        const QPointF pt(rnd(0, kTile), rnd(0, kTile));
        const double r = rnd(1.0, 1.4);
        const double halo = rnd(3.5, 6.5);
        wrapped(pt, halo, [&](const QPointF &q) {
            QRadialGradient g(q, halo);
            g.setColorAt(0, QColor(255, 255, 255, 70));
            g.setColorAt(1, QColor(255, 255, 255, 0));
            p.setBrush(g);
            p.drawEllipse(q, halo, halo);
            p.setBrush(QColor(250, 250, 255, 235));
            p.drawEllipse(q, r, r);
        });
    }
    return pm;
}

const QPixmap &tile(qreal dpr)
{
    static QHash<int, QPixmap> cache;
    const int key = qRound(dpr * 100);
    auto it = cache.find(key);
    if (it == cache.end())
        it = cache.insert(key, buildTile(dpr));
    return *it;
}

int wrap(int v)
{
    const int m = v % kTile;
    return m < 0 ? m + kTile : m;
}

QColor gray(int v, int a = 255)
{
    v = qBound(0, v, 255);
    return QColor(v, v, qMin(255, v + 4), qBound(0, a, 255));
}

// ---- scene helpers ---------------------------------------------------------------

struct Rng {
    QRandomGenerator g;
    explicit Rng(quint32 seed)
        : g(seed)
    {
    }
    double operator()(double a, double b) { return a + (b - a) * g.generateDouble(); }
};

void fillSky(QPainter &p, const QRectF &r, int top = 12, int bottom = 22)
{
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, gray(top));
    g.setColorAt(1, gray(bottom));
    p.fillRect(r, g);
}

void glow(QPainter &p, const QPointF &c, double radius, const QColor &color)
{
    QRadialGradient g(c, radius);
    QColor transparent = color;
    transparent.setAlpha(0);
    g.setColorAt(0, color);
    QColor mid = color;
    mid.setAlpha(color.alpha() / 3);
    g.setColorAt(0.35, mid);
    g.setColorAt(1, transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(c, radius, radius);
}

void starfield(QPainter &p, const QRectF &r, Rng &rnd, int count, double brightness = 1.0)
{
    p.setPen(Qt::NoPen);
    const double area = r.width() * r.height() / (900.0 * 260.0);
    const int n = int(count * qMax(0.4, area));
    for (int i = 0; i < n; ++i) {
        const QPointF pt(rnd(r.left(), r.right()), rnd(r.top(), r.bottom()));
        const double t = rnd(0, 1);
        const double radius = t < 0.85 ? rnd(0.35, 0.8) : rnd(0.8, 1.5);
        p.setBrush(gray(int(rnd(190, 255)), int(rnd(50, 220) * brightness)));
        p.drawEllipse(pt, radius, radius);
        if (radius > 1.25)
            glow(p, pt, radius * 4, gray(255, int(60 * brightness)));
    }
}

void brightStar(QPainter &p, const QPointF &c, double size, double alpha = 1.0)
{
    glow(p, c, size * 6, gray(255, int(70 * alpha)));
    QLinearGradient h(c - QPointF(size * 7, 0), c + QPointF(size * 7, 0));
    h.setColorAt(0, gray(255, 0));
    h.setColorAt(0.5, gray(255, int(150 * alpha)));
    h.setColorAt(1, gray(255, 0));
    p.setBrush(h);
    p.drawRect(QRectF(c.x() - size * 7, c.y() - 0.4, size * 14, 0.8));
    QLinearGradient v(c - QPointF(0, size * 7), c + QPointF(0, size * 7));
    v.setColorAt(0, gray(255, 0));
    v.setColorAt(0.5, gray(255, int(150 * alpha)));
    v.setColorAt(1, gray(255, 0));
    p.setBrush(v);
    p.drawRect(QRectF(c.x() - 0.4, c.y() - size * 7, 0.8, size * 14));
    p.setBrush(gray(255, int(240 * alpha)));
    p.drawEllipse(c, size, size);
}

// ---- scenes -----------------------------------------------------------------------

void sceneNebula(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 9, 16);
    starfield(p, r, rnd, 260, 0.8);
    const double w = r.width(), h = r.height();
    // A filament of light along a gentle curve.
    const double y0 = rnd(0.25, 0.75), y1 = rnd(0.25, 0.75), bend = rnd(-0.35, 0.35);
    auto along = [&](double t) {
        const double y = y0 + (y1 - y0) * t + bend * qSin(t * M_PI);
        return QPointF(r.left() + t * w, r.top() + y * h);
    };
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int i = 0; i < 70; ++i) {
        const double t = rnd(-0.05, 1.05);
        const QPointF c = along(t) + QPointF(rnd(-0.06, 0.06) * w, rnd(-0.22, 0.22) * h);
        const double rad = rnd(0.08, 0.38) * h * (1.0 + rnd(0, 1.5));
        glow(p, c, rad, gray(int(rnd(150, 235)), int(rnd(10, 30))));
    }
    // Bright knots where stars are forming.
    for (int i = 0; i < 6; ++i) {
        const QPointF c = along(rnd(0.1, 0.9)) + QPointF(rnd(-0.03, 0.03) * w, rnd(-0.08, 0.08) * h);
        glow(p, c, rnd(0.05, 0.12) * h, gray(255, int(rnd(25, 55))));
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // Dark dust lanes cut through the glow.
    for (int i = 0; i < 26; ++i) {
        const QPointF c = along(rnd(0, 1)) + QPointF(rnd(-0.04, 0.04) * w, rnd(-0.12, 0.12) * h);
        glow(p, c, rnd(0.05, 0.2) * h, QColor(0, 0, 0, int(rnd(40, 110))));
    }
    starfield(p, r, rnd, 40, 1.0);
    for (int i = 0; i < 3; ++i)
        brightStar(p, QPointF(rnd(r.left(), r.right()), rnd(r.top(), r.bottom())), rnd(0.9, 1.5));
}

void sceneEclipse(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 7, 12);
    starfield(p, r, rnd, 180, 0.7);
    const double h = r.height();
    const QPointF c(r.left() + r.width() * rnd(0.55, 0.72), r.top() + h * rnd(0.42, 0.58));
    const double R = h * 0.26;
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    // Corona streamers.
    for (int i = 0; i < 14; ++i) {
        p.save();
        p.translate(c);
        p.rotate(rnd(0, 360));
        QRadialGradient g(QPointF(0, 0), R * rnd(1.8, 3.2));
        g.setColorAt(0.3, gray(230, int(rnd(14, 32))));
        g.setColorAt(1, gray(230, 0));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.scale(1.0, rnd(0.15, 0.35));
        p.drawEllipse(QPointF(0, 0), R * 3.2, R * 3.2);
        p.restore();
    }
    QRadialGradient corona(c, R * 2.6);
    corona.setColorAt(0.36, gray(255, 190));
    corona.setColorAt(0.42, gray(220, 70));
    corona.setColorAt(0.7, gray(200, 16));
    corona.setColorAt(1, gray(200, 0));
    p.setBrush(corona);
    p.drawEllipse(c, R * 2.6, R * 2.6);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // The moon.
    QRadialGradient moon(c - QPointF(R * 0.3, R * 0.3), R * 1.3);
    moon.setColorAt(0, gray(16));
    moon.setColorAt(1, gray(4));
    p.setBrush(moon);
    p.drawEllipse(c, R, R);
    // Diamond ring.
    const double a = qDegreesToRadians(rnd(200, 330));
    const QPointF ring = c + QPointF(qCos(a) * R, qSin(a) * R);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    glow(p, ring, R * 0.9, gray(255, 120));
    brightStar(p, ring, R * 0.07);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void sceneHorizon(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 8, 14);
    starfield(p, r, rnd, 220, 0.9);
    const double w = r.width(), h = r.height();
    const double R = w * rnd(0.85, 1.25);
    const QPointF c(r.left() + w * rnd(0.35, 0.65), r.bottom() + R - h * rnd(0.28, 0.42));
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    QRadialGradient atmo(c, R * 1.12);
    const double edge = R / (R * 1.12);
    atmo.setColorAt(edge - 0.01, gray(255, 0));
    atmo.setColorAt(edge, gray(235, 90));
    atmo.setColorAt(edge + 0.025, gray(200, 30));
    atmo.setColorAt(1, gray(200, 0));
    p.setBrush(atmo);
    p.setPen(Qt::NoPen);
    p.drawEllipse(c, R * 1.12, R * 1.12);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QLinearGradient surface(QPointF(c.x(), c.y() - R), QPointF(c.x(), c.y() - R + h * 0.5));
    surface.setColorAt(0, gray(42));
    surface.setColorAt(0.08, gray(22));
    surface.setColorAt(1, gray(9));
    p.setBrush(surface);
    p.drawEllipse(c, R, R);
    // Faint cloud bands.
    p.save();
    QPainterPath disc;
    disc.addEllipse(c, R, R);
    p.setClipPath(disc);
    for (int i = 0; i < 9; ++i) {
        const double y = c.y() - R + h * rnd(0.02, 0.5);
        QLinearGradient band(QPointF(r.left(), y), QPointF(r.right(), y));
        band.setColorAt(0, gray(255, 0));
        band.setColorAt(rnd(0.3, 0.7), gray(255, int(rnd(4, 12))));
        band.setColorAt(1, gray(255, 0));
        p.setBrush(band);
        p.drawEllipse(QPointF(c.x() + rnd(-w, w) * 0.2, y), R * rnd(0.6, 1.0), h * rnd(0.01, 0.03));
    }
    p.restore();
    // The rising sun at the rim.
    const QPointF sun(c.x() + rnd(-0.25, 0.25) * w, c.y() - qSqrt(qMax(0.0, R * R - qPow(rnd(-0.25, 0.25) * w, 2))));
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    glow(p, QPointF(sun.x(), c.y() - R), h * 0.35, gray(255, 60));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void ringedPlanet(QPainter &p, const QPointF &c, double R, Rng &rnd, double tilt)
{
    auto ring = [&](bool front) {
        p.save();
        p.translate(c);
        p.rotate(tilt);
        QPainterPath clip;
        if (front)
            clip.addRect(QRectF(-R * 3, 0, R * 6, R * 3));
        else
            clip.addRect(QRectF(-R * 3, -R * 3, R * 6, R * 3));
        p.setClipPath(clip);
        p.setBrush(Qt::NoBrush);
        for (int i = 0; i < 26; ++i) {
            const double k = 1.35 + i * 0.033;
            const int a = int((i % 5 == 3 ? 20 : 70 + rnd(0, 60)) * (front ? 1.0 : 0.65));
            p.setPen(QPen(gray(int(rnd(150, 220)), a), R * 0.03));
            p.drawEllipse(QPointF(0, 0), R * k, R * k * 0.24);
        }
        p.restore();
    };
    ring(false);
    QRadialGradient body(c - QPointF(R * 0.45, R * 0.5), R * 1.7);
    body.setColorAt(0, gray(170));
    body.setColorAt(0.45, gray(70));
    body.setColorAt(1, gray(10));
    p.setPen(Qt::NoPen);
    p.setBrush(body);
    p.drawEllipse(c, R, R);
    p.save();
    QPainterPath disc;
    disc.addEllipse(c, R, R);
    p.setClipPath(disc);
    p.translate(c);
    p.rotate(tilt);
    for (int i = 0; i < 9; ++i) {
        const double y = rnd(-R, R);
        p.setBrush(gray(int(rnd(0, 255)), int(rnd(8, 22))));
        p.drawRect(QRectF(-R * 1.2, y, R * 2.4, R * rnd(0.04, 0.16)));
    }
    p.restore();
    ring(true);
}

void sceneRinged(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 9, 15);
    starfield(p, r, rnd, 240, 0.9);
    const double h = r.height();
    ringedPlanet(p, QPointF(r.left() + r.width() * rnd(0.6, 0.78), r.top() + h * rnd(0.45, 0.6)), h * rnd(0.22, 0.3), rnd,
                 rnd(-24, -10));
    // A small moon.
    const QPointF m(r.left() + r.width() * rnd(0.15, 0.4), r.top() + h * rnd(0.25, 0.7));
    Cosmos::paintPlanet(p, m, h * 0.04, 0.9, false);
}

void sceneGalaxy(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 7, 13);
    starfield(p, r, rnd, 200, 0.7);
    const double h = r.height();
    const QPointF c(r.left() + r.width() * rnd(0.4, 0.65), r.top() + h * rnd(0.45, 0.55));
    const double R = h * rnd(0.75, 0.95);
    const double tilt = qDegreesToRadians(rnd(-28, -12));
    const double squash = rnd(0.32, 0.42);
    auto place = [&](double rad, double theta) {
        const double x = rad * qCos(theta), y = rad * qSin(theta) * squash;
        return QPointF(c.x() + x * qCos(tilt) - y * qSin(tilt), c.y() + x * qSin(tilt) + y * qCos(tilt));
    };
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    // The disc's diffuse light.
    p.save();
    p.translate(c);
    p.rotate(qRadiansToDegrees(tilt));
    p.scale(1.0, squash);
    glow(p, QPointF(0, 0), R * 1.05, gray(200, 30));
    p.restore();
    p.setPen(Qt::NoPen);
    for (int arm = 0; arm < 2; ++arm) {
        // Soft clouds along each arm.
        for (int i = 0; i < 70; ++i) {
            const double t = 0.08 + 0.92 * i / 70.0;
            const double theta = arm * M_PI + t * 2.7 * M_PI;
            glow(p, place(R * t, theta), R * (0.16 - 0.08 * t), gray(215, int(16 * (1.1 - t))));
        }
        // Stars, thinning towards the rim.
        for (int i = 0; i < 2200; ++i) {
            const double t = qPow(rnd(0, 1), 0.9);
            const double theta = arm * M_PI + t * 2.7 * M_PI + rnd(-0.45, 0.45) * (1.0 - t * 0.45);
            const double rad = R * t * (1.0 + rnd(-0.06, 0.06));
            const double s = rnd(0.3, 0.95) * (1.15 - t * 0.5);
            p.setBrush(gray(int(rnd(190, 255)), int(rnd(30, 150) * (1.05 - t * 0.65))));
            p.drawEllipse(place(rad, theta), s, s);
        }
    }
    // Bulge.
    p.save();
    p.translate(c);
    p.rotate(qRadiansToDegrees(tilt));
    p.scale(1.0, 0.7);
    glow(p, QPointF(0, 0), R * 0.3, gray(255, 120));
    glow(p, QPointF(0, 0), R * 0.1, gray(255, 210));
    p.restore();
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void sceneLunar(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 8, 12);
    starfield(p, r, rnd, 160, 0.7);
    const double h = r.height();
    const double R = h * rnd(0.95, 1.35);
    const QPointF c(r.left() + r.width() * rnd(0.64, 0.84), r.top() + h * rnd(0.62, 0.95));
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    glow(p, c, R * 1.3, gray(220, 26));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QRadialGradient surface(c - QPointF(R * 0.35, R * 0.4), R * 1.6);
    surface.setColorAt(0, gray(156));
    surface.setColorAt(0.55, gray(104));
    surface.setColorAt(1, gray(46));
    p.setPen(Qt::NoPen);
    p.setBrush(surface);
    p.drawEllipse(c, R, R);
    p.save();
    QPainterPath disc;
    disc.addEllipse(c, R, R);
    p.setClipPath(disc);
    // Maria: big soft dark seas.
    for (int i = 0; i < 10; ++i)
        glow(p, c + QPointF(rnd(-0.8, 0.5) * R, rnd(-0.8, 0.5) * R), R * rnd(0.12, 0.34), QColor(0, 0, 0, int(rnd(26, 52))));
    // Craters lit from the upper left: shadow inside the upper-left wall,
    // light on the lower-right one.
    for (int i = 0; i < 110; ++i) {
        const QPointF pc = c + QPointF(rnd(-1, 1) * R, rnd(-1, 1) * R);
        const double cr = R * (0.006 + 0.07 * qPow(rnd(0, 1), 3.0)) + 0.8;
        QPainterPath crater;
        crater.addEllipse(pc, cr, cr * 0.94);
        p.setBrush(QColor(0, 0, 0, 22));
        p.drawPath(crater);
        QPainterPath lit;
        lit.addEllipse(pc + QPointF(cr * 0.28, cr * 0.28), cr, cr * 0.94);
        p.setBrush(QColor(0, 0, 0, int(rnd(45, 80))));
        p.drawPath(crater.subtracted(lit));
        QPainterPath shade;
        shade.addEllipse(pc - QPointF(cr * 0.22, cr * 0.22), cr, cr * 0.94);
        p.setBrush(gray(240, int(rnd(26, 46))));
        p.drawPath(crater.subtracted(shade));
    }
    // Terminator: night creeps in from the left.
    QLinearGradient night(QPointF(c.x() - R, c.y()), QPointF(c.x() + R * 0.1, c.y()));
    night.setColorAt(0, QColor(0, 0, 0, 225));
    night.setColorAt(0.6, QColor(0, 0, 0, 90));
    night.setColorAt(1, QColor(0, 0, 0, 0));
    p.setBrush(night);
    p.drawRect(QRectF(c.x() - R, c.y() - R, R * 2, R * 2));
    p.restore();
}

void sceneOrbits(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 10, 16);
    starfield(p, r, rnd, 120, 0.6);
    const double w = r.width(), h = r.height();
    const QPointF sun(r.left() + w * rnd(0.12, 0.25), r.top() + h * rnd(0.4, 0.6));
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    glow(p, sun, h * 0.5, gray(240, 70));
    glow(p, sun, h * 0.1, gray(255, 220));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const double tilt = rnd(-12, -4);
    for (int i = 1; i <= 6; ++i) {
        const double rx = w * (0.12 + i * 0.14), ry = rx * 0.32;
        p.save();
        p.translate(sun);
        p.rotate(tilt);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(gray(220, 34), 1.0));
        p.drawEllipse(QPointF(0, 0), rx, ry);
        const double a = qDegreesToRadians(rnd(0, 360));
        const QPointF pos(rx * qCos(a), ry * qSin(a));
        p.restore();
        const double rad = qDegreesToRadians(tilt);
        const QPointF world(sun.x() + pos.x() * qCos(rad) - pos.y() * qSin(rad), sun.y() + pos.x() * qSin(rad) + pos.y() * qCos(rad));
        Cosmos::paintPlanet(p, world, h * rnd(0.018, 0.05), 1.0, i == 4);
    }
}

void sceneAurora(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 6, 14);
    starfield(p, r, rnd, 220, 0.8);
    const double w = r.width(), h = r.height();
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int band = 0; band < 3; ++band) {
        const double baseY = r.top() + h * rnd(0.35, 0.6);
        const double amp = h * rnd(0.06, 0.16), freq = rnd(0.8, 2.0), phase = rnd(0, 6.28);
        const double height = h * rnd(0.25, 0.5);
        for (double x = r.left(); x < r.right(); x += 2.0) {
            const double t = (x - r.left()) / w;
            const double y = baseY + amp * qSin(t * freq * 6.28 + phase) + amp * 0.4 * qSin(t * 13 + phase * 2);
            const double intensity = 0.5 + 0.5 * qSin(t * 9 + phase + band);
            QLinearGradient g(QPointF(x, y - height), QPointF(x, y));
            g.setColorAt(0, gray(255, 0));
            g.setColorAt(0.7, gray(235, int(10 * intensity)));
            g.setColorAt(0.97, gray(255, int(34 * intensity)));
            g.setColorAt(1, gray(255, 0));
            p.fillRect(QRectF(x, y - height, 2.0, height), g);
        }
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    // Mountain silhouettes.
    for (int layer = 0; layer < 2; ++layer) {
        QPainterPath ridge;
        ridge.moveTo(r.bottomLeft());
        double y = r.bottom() - h * (0.12 + layer * 0.06);
        for (double x = r.left(); x <= r.right() + 20; x += w / 14) {
            y = qBound(r.bottom() - h * 0.3, y + rnd(-0.08, 0.08) * h, r.bottom() - h * 0.05);
            ridge.lineTo(x, y);
        }
        ridge.lineTo(r.bottomRight());
        ridge.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(gray(layer == 0 ? 14 : 7));
        p.drawPath(ridge);
    }
}

void sceneComet(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 8, 15);
    starfield(p, r, rnd, 260, 0.85);
    const double w = r.width(), h = r.height();
    const QPointF head(r.left() + w * rnd(0.64, 0.82), r.top() + h * rnd(0.25, 0.42));
    const QPointF tail(head.x() - w * rnd(0.5, 0.7), head.y() + h * rnd(0.35, 0.6));
    const QPointF bend((head.x() + tail.x()) / 2, (head.y() + tail.y()) / 2 + h * rnd(0.05, 0.15));
    auto at = [&](double t) {
        const double u = 1 - t;
        return head * (u * u) + bend * (2 * u * t) + tail * (t * t);
    };
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    // Dust tail: soft light that widens and fades.
    for (int i = 0; i < 90; ++i) {
        const double t = i / 90.0;
        glow(p, at(t), h * (0.015 + 0.14 * t), gray(235, int(26 * qPow(1.0 - t, 1.4))));
    }
    // Ion tail: straight, thin and bright.
    const QPointF ionEnd(head.x() - w * 0.62, head.y() + h * 0.18);
    QLinearGradient ion(head, ionEnd);
    ion.setColorAt(0, gray(255, 110));
    ion.setColorAt(1, gray(255, 0));
    p.setPen(QPen(QBrush(ion), 1.1));
    p.drawLine(head, ionEnd);
    p.setPen(Qt::NoPen);
    glow(p, head, h * 0.14, gray(255, 110));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    brightStar(p, head, h * 0.012);
}

void sceneDeepField(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 5, 9);
    starfield(p, r, rnd, 700, 1.0);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    for (int i = 0; i < 26; ++i) {
        const QPointF c(rnd(r.left(), r.right()), rnd(r.top(), r.bottom()));
        const double s = rnd(3, 14);
        p.save();
        p.translate(c);
        p.rotate(rnd(0, 180));
        p.scale(1.0, rnd(0.25, 0.7));
        glow(p, QPointF(0, 0), s, gray(int(rnd(200, 255)), int(rnd(60, 140))));
        p.restore();
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (int i = 0; i < 4; ++i)
        brightStar(p, QPointF(rnd(r.left(), r.right()), rnd(r.top(), r.bottom())), rnd(0.8, 1.4));
}

void sceneDunes(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 10, 30);
    const double w = r.width(), h = r.height();
    starfield(p, QRectF(r.left(), r.top(), w, h * 0.55), rnd, 160, 0.75);
    // Light along the horizon.
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    glow(p, QPointF(r.left() + w * rnd(0.3, 0.7), r.top() + h * 0.62), w * 0.45, gray(220, 22));
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    Cosmos::paintPlanet(p, QPointF(r.left() + w * rnd(0.12, 0.85), r.top() + h * rnd(0.14, 0.32)), h * rnd(0.05, 0.08), 1.0,
                        rnd(0, 1) > 0.5);
    for (int layer = 0; layer < 5; ++layer) {
        const double base = r.top() + h * (0.52 + layer * 0.1);
        const double amp = h * rnd(0.03, 0.07), freq = rnd(0.5, 1.4), phase = rnd(0, 6.28);
        QPainterPath crest, dune;
        dune.moveTo(r.left(), r.bottom());
        for (double x = r.left(); x <= r.right() + 4; x += 4) {
            const double t = (x - r.left()) / w;
            const double y = base + amp * qSin(t * freq * 6.28 + phase) + amp * 0.3 * qSin(t * 17 + phase);
            dune.lineTo(x, y);
            if (x == r.left())
                crest.moveTo(x, y);
            else
                crest.lineTo(x, y);
        }
        dune.lineTo(r.right(), r.bottom());
        dune.closeSubpath();
        const int top = 66 - layer * 11;
        QLinearGradient g(QPointF(0, base - amp), QPointF(0, base + h * 0.16));
        g.setColorAt(0, gray(top));
        g.setColorAt(1, gray(qMax(6, top - 30)));
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawPath(dune);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(gray(235, 40 - layer * 6), 1.0));
        p.drawPath(crest);
    }
}

void sceneConstellation(QPainter &p, const QRectF &r, Rng &rnd)
{
    fillSky(p, r, 9, 16);
    starfield(p, r, rnd, 260, 0.75);
    const double w = r.width(), h = r.height();
    for (int k = 0; k < 3; ++k) {
        const QPointF origin(r.left() + w * (0.15 + k * 0.3) + rnd(-0.05, 0.05) * w, r.top() + h * rnd(0.3, 0.7));
        QList<QPointF> pts;
        QPointF cur = origin;
        const int n = int(rnd(4, 7));
        for (int i = 0; i < n; ++i) {
            pts.append(cur);
            cur += QPointF(rnd(-0.09, 0.09) * w, rnd(-0.25, 0.25) * h);
            cur.setY(qBound(r.top() + h * 0.1, cur.y(), r.bottom() - h * 0.1));
        }
        p.setPen(QPen(gray(220, 60), 1.0));
        for (int i = 1; i < pts.size(); ++i)
            p.drawLine(pts[i - 1], pts[i]);
        p.setPen(Qt::NoPen);
        for (const QPointF &pt : pts)
            brightStar(p, pt, rnd(0.8, 1.4), 0.9);
    }
}

using SceneFn = void (*)(QPainter &, const QRectF &, Rng &);
struct Scene {
    const char *key;
    const char *name;
    SceneFn fn;
};
const Scene kScenes[] = {
    {"nebula", QT_TRANSLATE_NOOP("Cosmos", "Nebula"), sceneNebula},
    {"eclipse", QT_TRANSLATE_NOOP("Cosmos", "Eclipse"), sceneEclipse},
    {"horizon", QT_TRANSLATE_NOOP("Cosmos", "Planetrise"), sceneHorizon},
    {"ringed", QT_TRANSLATE_NOOP("Cosmos", "Ringed world"), sceneRinged},
    {"galaxy", QT_TRANSLATE_NOOP("Cosmos", "Spiral galaxy"), sceneGalaxy},
    {"lunar", QT_TRANSLATE_NOOP("Cosmos", "Moonrise"), sceneLunar},
    {"orbits", QT_TRANSLATE_NOOP("Cosmos", "Orbits"), sceneOrbits},
    {"aurora", QT_TRANSLATE_NOOP("Cosmos", "Aurora"), sceneAurora},
    {"comet", QT_TRANSLATE_NOOP("Cosmos", "Comet"), sceneComet},
    {"deepfield", QT_TRANSLATE_NOOP("Cosmos", "Deep field"), sceneDeepField},
    {"dunes", QT_TRANSLATE_NOOP("Cosmos", "Dunes"), sceneDunes},
    {"constellation", QT_TRANSLATE_NOOP("Cosmos", "Constellations"), sceneConstellation},
};
}

namespace Cosmos
{

void paintStars(QPainter &p, const QRect &rect, const QPoint &originInWindow, double intensity)
{
    if (intensity <= 0.001 || rect.isEmpty())
        return;
    const qreal dpr = p.device() ? p.device()->devicePixelRatio() : 1.0;
    const QPixmap &pm = tile(dpr);
    const QPoint offset(wrap(rect.x() + originInWindow.x()), wrap(rect.y() + originInWindow.y()));
    p.save();
    p.setOpacity(p.opacity() * qMin(1.0, intensity));
    p.drawTiledPixmap(rect, pm, offset);
    p.restore();
}

void paintStars(QPainter &p, const QRect &rect, const QWidget *widget, double intensity)
{
    const QPoint origin = widget ? widget->mapTo(widget->window(), QPoint(0, 0)) : QPoint();
    paintStars(p, rect, origin, intensity);
}

void paintNebulaHaze(QPainter &p, const QRectF &rect, double strength, int seed)
{
    if (strength <= 0.001)
        return;
    Rng rnd{quint32(seed)};
    p.save();
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 7; ++i) {
        const QPointF c(rect.left() + rnd(0, 1) * rect.width(), rect.top() + rnd(0, 1) * rect.height());
        glow(p, c, rnd(0.2, 0.5) * qMax(rect.width(), rect.height()), gray(200, int(rnd(4, 9) * strength)));
    }
    p.restore();
}

QStringList coverScenes()
{
    QStringList out;
    for (const Scene &s : kScenes)
        out.append(QString::fromLatin1(s.key));
    return out;
}

QString coverSceneName(const QString &scene)
{
    for (const Scene &s : kScenes)
        if (scene == QLatin1String(s.key))
            return QApplication::translate("Cosmos", s.name);
    return scene;
}

QString coverSpec(const QString &scene, int seed)
{
    return QStringLiteral("cosmos:%1:%2").arg(scene).arg(seed);
}

QString randomCover()
{
    const QStringList scenes = coverScenes();
    return coverSpec(scenes[QRandomGenerator::global()->bounded(int(scenes.size()))],
                     QRandomGenerator::global()->bounded(1, 100000));
}

QPixmap coverPixmap(const QString &spec, const QSize &size, qreal dpr)
{
    static QHash<QString, QPixmap> cache;
    static QStringList order;
    const QString key = QStringLiteral("%1|%2x%3|%4").arg(spec).arg(size.width()).arg(size.height()).arg(dpr);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;

    QPixmap pm(size * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(QColor(12, 12, 15));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r(QPointF(0, 0), QSizeF(size));
    const QStringList parts = spec.split(QLatin1Char(':'));
    if (parts.value(0) == QLatin1String("media")) {
        const QString path = Workspace::instance() ? Workspace::instance()->mediaPath(parts.mid(1).join(QLatin1Char(':'))) : QString();
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        if (!img.isNull()) {
            // Fill the width; the stored position picks the vertical band at paint time.
            const QImage scaled = img.scaledToWidth(qRound(size.width() * dpr), Qt::SmoothTransformation);
            QPixmap full = QPixmap::fromImage(scaled);
            full.setDevicePixelRatio(dpr);
            p.end();
            cache.insert(key, full);
            order.append(key);
            return full;
        }
    } else {
        const QString scene = parts.value(1);
        Rng rnd{quint32(parts.value(2).toUInt() * 2654435761u + 17)};
        bool drawn = false;
        for (const Scene &s : kScenes) {
            if (scene == QLatin1String(s.key)) {
                s.fn(p, r, rnd);
                drawn = true;
                break;
            }
        }
        if (!drawn)
            sceneNebula(p, r, rnd);
    }
    p.end();
    cache.insert(key, pm);
    order.append(key);
    while (order.size() > 40)
        cache.remove(order.takeFirst());
    return pm;
}

void paintCover(QPainter &p, const QRectF &rect, const QString &spec, double position)
{
    if (spec.isEmpty() || rect.isEmpty())
        return;
    const qreal dpr = p.device() ? p.device()->devicePixelRatio() : 1.0;
    const QSize size = rect.size().toSize();
    if (spec.startsWith(QLatin1String("media:")) && AnimatedMedia::isAnimated(spec.mid(6))) {
        // Animated covers (GIFs) play: draw the current frame the same way.
        const QImage frame = AnimatedMedia::frame(spec.mid(6));
        if (!frame.isNull()) {
            const double h = rect.width() * frame.height() / qMax(1, frame.width());
            p.save();
            p.setClipRect(rect);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            if (h < rect.height()) {
                const double w = rect.height() * frame.width() / qMax(1, frame.height());
                p.drawImage(QRectF(rect.center().x() - w / 2, rect.top(), w, rect.height()), frame);
            } else {
                const double y = rect.top() - (h - rect.height()) * qBound(0.0, position, 1.0);
                p.drawImage(QRectF(rect.left(), y, rect.width(), h), frame);
            }
            p.restore();
            AnimatedMedia::repaintOnNextFrame(p, rect, spec.mid(6));
            return;
        }
    }
    if (spec.startsWith(QLatin1String("media:"))) {
        // Image covers are scaled to the width once; pick the band here.
        const QPixmap pm = coverPixmap(spec, QSize(size.width(), size.height()), dpr);
        const double h = pm.height() / pm.devicePixelRatio();
        if (h <= 0)
            return;
        p.save();
        p.setClipRect(rect);
        if (h < rect.height()) {
            // Too short: scale up to cover.
            const double s = rect.height() / h;
            const double w = rect.width() * s;
            p.drawPixmap(QRectF(rect.center().x() - w / 2, rect.top(), w, rect.height()), pm, QRectF(pm.rect()));
        } else {
            const double y = rect.top() - (h - rect.height()) * qBound(0.0, position, 1.0);
            p.drawPixmap(QRectF(rect.left(), y, rect.width(), h), pm, QRectF(pm.rect()));
        }
        p.restore();
        return;
    }
    p.drawPixmap(rect.topLeft(), coverPixmap(spec, size, dpr));
}

void paintSparkle(QPainter &p, const QPointF &c, double radius, const QColor &color)
{
    QPainterPath star;
    const double k = radius * 0.22;
    star.moveTo(c.x(), c.y() - radius);
    star.quadTo(c.x() + k * 0.4, c.y() - k * 0.4, c.x() + radius, c.y());
    star.quadTo(c.x() + k * 0.4, c.y() + k * 0.4, c.x(), c.y() + radius);
    star.quadTo(c.x() - k * 0.4, c.y() + k * 0.4, c.x() - radius, c.y());
    star.quadTo(c.x() - k * 0.4, c.y() - k * 0.4, c.x(), c.y() - radius);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawPath(star);
    p.restore();
}

void paintSparks(QPainter &p, const QPointF &center, double t, double size)
{
    if (t <= 0 || t >= 1)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double ease = 1.0 - qPow(1.0 - t, 3);
    const double fade = 1.0 - t;
    for (int i = 0; i < 8; ++i) {
        const double a = i * M_PI / 4 + 0.3;
        const double dist = (6 + 14 * ease) * size;
        const QPointF pt = center + QPointF(qCos(a) * dist, qSin(a) * dist);
        const double r = (i % 2 ? 2.2 : 3.2) * size * (1.0 - t * 0.6);
        paintSparkle(p, pt, r, QColor(241, 241, 245, int(230 * fade)));
    }
    glow(p, center, 14 * size * (0.6 + ease), gray(255, int(70 * fade)));
    p.restore();
}

void paintPlanet(QPainter &p, const QPointF &c, double R, double light, bool ring)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    if (ring) {
        p.save();
        p.translate(c);
        p.rotate(-18);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(gray(200, int(70 * light)), qMax(0.8, R * 0.08)));
        QPainterPath back;
        back.addRect(QRectF(-R * 3, -R * 3, R * 6, R * 3));
        p.setClipPath(back);
        p.drawEllipse(QPointF(0, 0), R * 1.75, R * 0.45);
        p.restore();
    }
    QRadialGradient body(c - QPointF(R * 0.45, R * 0.5), R * 1.8);
    body.setColorAt(0, gray(int(185 * light)));
    body.setColorAt(0.45, gray(int(80 * light)));
    body.setColorAt(1, gray(12));
    p.setBrush(body);
    p.drawEllipse(c, R, R);
    if (ring) {
        p.translate(c);
        p.rotate(-18);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(gray(220, int(130 * light)), qMax(0.8, R * 0.08)));
        QPainterPath front;
        front.addRect(QRectF(-R * 3, 0, R * 6, R * 3));
        p.setClipPath(front);
        p.drawEllipse(QPointF(0, 0), R * 1.75, R * 0.45);
    }
    p.restore();
}

void paintHorizon(QPainter &p, const QRectF &rect, double strength)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const double R = rect.width() * 1.6;
    const QPointF c(rect.center().x(), rect.bottom() + R - rect.height() * 0.32);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    QRadialGradient atmo(c, R * 1.1);
    const double edge = 1.0 / 1.1;
    atmo.setColorAt(edge - 0.005, gray(255, 0));
    atmo.setColorAt(edge, gray(235, int(70 * strength)));
    atmo.setColorAt(edge + 0.02, gray(200, int(22 * strength)));
    atmo.setColorAt(1, gray(200, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(atmo);
    p.drawEllipse(c, R * 1.1, R * 1.1);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QLinearGradient surface(QPointF(c.x(), c.y() - R), QPointF(c.x(), c.y() - R + rect.height() * 0.5));
    surface.setColorAt(0, gray(36));
    surface.setColorAt(0.1, gray(20));
    surface.setColorAt(1, gray(13));
    p.setBrush(surface);
    p.drawEllipse(c, R, R);
    p.restore();
}

}
