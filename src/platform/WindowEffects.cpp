#include "WindowEffects.h"

#include <QEvent>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWidget>
#include <QWindow>

#include <vector>

#ifdef NEBULA_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#include <KWindowShadow>
#include <KWindowSystem>
#endif

namespace
{
#ifdef NEBULA_HAVE_KWINDOWSYSTEM

// Three passes of a separable box blur on the alpha channel approximate a
// gaussian closely enough for shadows.
void blurAlpha(QImage &img, int radius)
{
    if (radius < 1)
        return;
    const int w = img.width(), h = img.height();
    std::vector<int> a(size_t(w) * h), tmp(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < w; ++x)
            a[size_t(y) * w + x] = qAlpha(line[x]);
    }
    const int div = 2 * radius + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            const int *row = &a[size_t(y) * w];
            auto at = [&](int x) { return (x < 0 || x >= w) ? 0 : row[x]; };
            int sum = 0;
            for (int x = -radius; x <= radius; ++x)
                sum += at(x);
            for (int x = 0; x < w; ++x) {
                tmp[size_t(y) * w + x] = sum / div;
                sum += at(x + radius + 1) - at(x - radius);
            }
        }
        for (int x = 0; x < w; ++x) {
            auto at = [&](int y) { return (y < 0 || y >= h) ? 0 : tmp[size_t(y) * w + x]; };
            int sum = 0;
            for (int y = -radius; y <= radius; ++y)
                sum += at(y);
            for (int y = 0; y < h; ++y) {
                a[size_t(y) * w + x] = sum / div;
                sum += at(y + radius + 1) - at(y - radius);
            }
        }
    }
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
            line[x] = qRgba(0, 0, 0, qBound(0, a[size_t(y) * w + x], 255));
    }
}

struct ShadowTiles {
    QMargins padding;
    KWindowShadowTile::Ptr topLeft, top, topRight, right, bottomRight, bottom, bottomLeft, left;
};

const ShadowTiles &tilesFor(int cornerRadius)
{
    static QHash<int, ShadowTiles> cache;
    auto it = cache.find(cornerRadius);
    if (it != cache.end())
        return *it;
    const int blur = 32, dy = 12;
    const int corner = cornerRadius + blur;
    const int padL = blur, padR = blur, padT = qMax(0, blur - dy), padB = blur + dy;
    const int inner = 2 * corner + 1;
    const int width = padL + inner + padR, height = padT + inner + padB;
    const QRectF box(padL, padT, inner, inner);
    auto layer = [&](int alpha, double offsetY, int radius, double shrink) {
        QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, alpha));
        p.drawRoundedRect(box.translated(0, offsetY).adjusted(shrink, shrink, -shrink, -shrink), cornerRadius, cornerRadius);
        p.end();
        blurAlpha(img, radius);
        return img;
    };
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.drawImage(0, 0, layer(170, dy, blur / 3, 4)); // ambient
        p.drawImage(0, 0, layer(120, 1.5, 2, 0));       // contact
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        p.drawRoundedRect(box, cornerRadius, cornerRadius);
    }
    auto tile = [&](int x, int y, int w, int h) {
        auto t = KWindowShadowTile::Ptr::create();
        t->setImage(image.copy(x, y, w, h));
        return t;
    };
    const int cx = padL + corner, cy = padT + corner;
    ShadowTiles t;
    t.padding = QMargins(padL, padT, padR, padB);
    t.topLeft = tile(0, 0, cx, cy);
    t.top = tile(cx, 0, 1, padT);
    t.topRight = tile(cx + 1, 0, width - cx - 1, cy);
    t.right = tile(width - padR, cy, padR, 1);
    t.bottomRight = tile(cx + 1, cy + 1, width - cx - 1, height - cy - 1);
    t.bottom = tile(cx, height - padB, 1, padB);
    t.bottomLeft = tile(0, cy + 1, cx, height - cy - 1);
    t.left = tile(0, cy, padL, 1);
    return *cache.insert(cornerRadius, t);
}

class ShadowHelper : public QObject
{
public:
    ShadowHelper(QWidget *widget, int radius)
        : QObject(widget)
        , m_widget(widget)
        , m_radius(radius)
    {
        setObjectName(QStringLiteral("NebulaShadowHelper"));
        widget->installEventFilter(this);
        if (widget->isVisible())
            install(true);
    }

    void setEnabled(bool enabled)
    {
        if (m_enabled == enabled)
            return;
        m_enabled = enabled;
        if (enabled)
            install(true);
        else
            uninstall();
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (object != m_widget)
            return false;
        if (event->type() == QEvent::PlatformSurface) {
            auto *pe = static_cast<QPlatformSurfaceEvent *>(event);
            if (pe->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
                install(true);
            else
                uninstall();
        } else if (event->type() == QEvent::Show) {
            install(false);
        }
        return false;
    }

private:
    void install(bool force)
    {
        if (!m_enabled || !m_widget)
            return;
        QWindow *window = m_widget->windowHandle();
        if (!window)
            return;
        if (!m_shadow) {
            m_shadow = new KWindowShadow(this);
            const ShadowTiles &t = tilesFor(m_radius);
            for (const auto &tile : {t.topLeft, t.top, t.topRight, t.right, t.bottomRight, t.bottom, t.bottomLeft, t.left})
                if (!tile->isCreated())
                    tile->create();
            m_shadow->setTopLeftTile(t.topLeft);
            m_shadow->setTopTile(t.top);
            m_shadow->setTopRightTile(t.topRight);
            m_shadow->setRightTile(t.right);
            m_shadow->setBottomRightTile(t.bottomRight);
            m_shadow->setBottomTile(t.bottom);
            m_shadow->setBottomLeftTile(t.bottomLeft);
            m_shadow->setLeftTile(t.left);
            m_shadow->setPadding(t.padding);
        }
        if (m_shadow->isCreated()) {
            if (!force && m_shadow->window() == window)
                return;
            m_shadow->destroy();
        }
        m_shadow->setWindow(window);
        m_shadow->create();
    }

    void uninstall()
    {
        if (m_shadow && m_shadow->isCreated())
            m_shadow->destroy();
    }

    QPointer<QWidget> m_widget;
    KWindowShadow *m_shadow = nullptr;
    int m_radius = 12;
    bool m_enabled = true;
};

ShadowHelper *helperFor(QWidget *window)
{
    if (!window)
        return nullptr;
    for (QObject *child : window->children())
        if (child->objectName() == QLatin1String("NebulaShadowHelper"))
            return static_cast<ShadowHelper *>(child);
    return nullptr;
}
#endif
}

namespace WindowEffects
{

bool blurAvailable()
{
#ifdef NEBULA_HAVE_KWINDOWSYSTEM
    return KWindowEffects::isEffectAvailable(KWindowEffects::BlurBehind);
#else
    return false;
#endif
}

void setBlurBehind(QWidget *window, bool enable, const QRegion &region)
{
#ifdef NEBULA_HAVE_KWINDOWSYSTEM
    if (window && window->windowHandle())
        KWindowEffects::enableBlurBehind(window->windowHandle(), enable, region);
#else
    Q_UNUSED(window)
    Q_UNUSED(enable)
    Q_UNUSED(region)
#endif
}

void attachShadow(QWidget *window, int cornerRadius)
{
#ifdef NEBULA_HAVE_KWINDOWSYSTEM
    if (window && !helperFor(window))
        new ShadowHelper(window, cornerRadius);
#else
    Q_UNUSED(window)
    Q_UNUSED(cornerRadius)
#endif
}

void setShadowEnabled(QWidget *window, bool enabled)
{
#ifdef NEBULA_HAVE_KWINDOWSYSTEM
    if (auto *helper = helperFor(window))
        helper->setEnabled(enabled);
#else
    Q_UNUSED(window)
    Q_UNUSED(enabled)
#endif
}

void activate(QWidget *window, const QString &activationToken)
{
    if (!window)
        return;
    if (window->isMinimized())
        window->setWindowState((window->windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    window->show();
    window->raise();
#ifdef NEBULA_HAVE_KWINDOWSYSTEM
    if (!activationToken.isEmpty())
        KWindowSystem::setCurrentXdgActivationToken(activationToken);
    if (window->windowHandle())
        KWindowSystem::activateWindow(window->windowHandle());
    else
        window->activateWindow();
#else
    if (!activationToken.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", activationToken.toUtf8());
    window->activateWindow();
#endif
}

}
