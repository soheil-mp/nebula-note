#include "AnimatedMedia.h"

#include "Theme.h"
#include "core/Workspace.h"

#include <QHash>
#include <QImageReader>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QWidget>

namespace
{
struct Player {
    QMovie *movie = nullptr;
    QList<QPair<QPointer<QWidget>, QRect>> waiting; // areas painted with the current frame
};

QHash<QString, bool> &animatedCache()
{
    static QHash<QString, bool> cache;
    return cache;
}

QHash<QString, Player> &players()
{
    static QHash<QString, Player> map;
    return map;
}

bool motionAllowed()
{
    return Theme::animMs(100) > 0;
}

Player *playerFor(const QString &media)
{
    Workspace *ws = Workspace::instance();
    if (!ws)
        return nullptr;
    Player &pl = players()[media];
    if (!pl.movie) {
        pl.movie = new QMovie(ws->mediaPath(media));
        pl.movie->setCacheMode(QMovie::CacheAll);
        QObject::connect(pl.movie, &QMovie::frameChanged, pl.movie, [media](int) {
            Player &p = players()[media];
            const auto waiting = p.waiting;
            p.waiting.clear();
            for (const auto &[w, r] : waiting)
                if (w)
                    w->update(r);
            // Nobody painted it since the last frame: pause until someone does.
            if (waiting.isEmpty() && p.movie)
                p.movie->setPaused(true);
        });
        pl.movie->start();
    } else if (pl.movie->state() == QMovie::Paused && motionAllowed()) {
        pl.movie->setPaused(false);
    }
    return &pl;
}
} // namespace

namespace AnimatedMedia
{
bool isAnimated(const QString &media)
{
    if (media.isEmpty())
        return false;
    auto it = animatedCache().constFind(media);
    if (it != animatedCache().constEnd())
        return *it;
    bool animated = false;
    if (Workspace *ws = Workspace::instance()) {
        QImageReader reader(ws->mediaPath(media));
        animated = reader.supportsAnimation() && reader.imageCount() != 1;
    }
    animatedCache().insert(media, animated);
    return animated;
}

QImage frame(const QString &media)
{
    if (!isAnimated(media))
        return {};
    Player *pl = playerFor(media);
    if (!pl || !pl->movie)
        return {};
    if (!motionAllowed()) {
        // First frame only.
        if (pl->movie->currentFrameNumber() != 0)
            pl->movie->jumpToFrame(0);
        pl->movie->setPaused(true);
    }
    return pl->movie->currentImage();
}

void repaintOnNextFrame(QPainter &p, const QRectF &rect, const QString &media)
{
    if (!motionAllowed())
        return;
    auto *w = dynamic_cast<QWidget *>(p.device());
    if (!w)
        return;
    Player *pl = playerFor(media);
    if (!pl)
        return;
    const QRect area = p.transform().mapRect(rect).toAlignedRect().adjusted(-1, -1, 1, 1).intersected(w->rect());
    if (area.isEmpty())
        return;
    for (auto &entry : pl->waiting)
        if (entry.first == w) {
            entry.second = entry.second.united(area);
            return;
        }
    pl->waiting.append({w, area});
}
} // namespace AnimatedMedia
