#include "HomeView.h"

#include "core/Schema.h"
#include "core/Settings.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>

class HomeCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit HomeCanvas(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }

    struct Target {
        QRectF rect;
        QString action; // "page:<id>", "new", "db", "import", "templates", "search"
    };

    void refresh()
    {
        Workspace *ws = Workspace::instance();
        m_recent.clear();
        m_upcoming.clear();
        if (!ws)
            return;
        for (const PageMeta *m : ws->recent(8))
            m_recent.append(m->id);
        // Upcoming: dated rows in the next two weeks, from every database.
        const QDate today = QDate::currentDate();
        for (const PageMeta *db : ws->allLive()) {
            if (!db->isDatabase())
                continue;
            const Schema s = ws->schema(db->id);
            QString dateProp;
            for (const Property &p : s.props)
                if (p.type == PropType::Date) {
                    dateProp = p.id;
                    break;
                }
            if (dateProp.isEmpty())
                continue;
            QString statusProp;
            for (const Property &p : s.props)
                if (p.type == PropType::Status)
                    statusProp = p.id;
            for (const PageMeta *row : ws->rows(db->id)) {
                const QJsonObject props = ws->props(row->id);
                const QDate d = dateOf(props.value(dateProp));
                if (!d.isValid() || d < today || d > today.addDays(14))
                    continue;
                // Finished work isn't upcoming.
                if (!statusProp.isEmpty()) {
                    const Property *sp = s.prop(statusProp);
                    const Option *o = sp ? sp->option(props.value(statusProp).toString()) : nullptr;
                    if (o && o->group == 2)
                        continue;
                }
                m_upcoming.append({row->id, db->id, d});
            }
        }
        std::sort(m_upcoming.begin(), m_upcoming.end(), [](const Upcoming &a, const Upcoming &b) { return a.date < b.date; });
        if (m_upcoming.size() > 8)
            m_upcoming.resize(8);
        relayout();
    }

    void relayout()
    {
        m_targets.clear();
        const double W = width();
        const double colW = qMin(980.0, W - 2 * qMax(48.0, W / 14));
        const double x0 = (W - colW) / 2;
        double y = 300;
        // Recently visited.
        m_recentTop = y;
        y += 36;
        // One row of cards spanning the column, as wide as the tiles below.
        const double cardH = 142, gap = 14;
        const int perRow = qMax(1, int((colW + gap) / (168 + gap)));
        const double cardW = (colW - (perRow - 1) * gap) / perRow;
        for (int i = 0; i < qMin(int(m_recent.size()), perRow); ++i)
            m_targets.append({QRectF(x0 + i * (cardW + gap), y, cardW, cardH), QStringLiteral("page:") + m_recent[i]});
        y += cardH + gap + 26;
        // Quick start.
        m_quickTop = y;
        y += 36;
        const QStringList actions = {QStringLiteral("new"), QStringLiteral("db"), QStringLiteral("templates"), QStringLiteral("import")};
        const double tileW = (colW - 3 * gap) / 4;
        for (int i = 0; i < actions.size(); ++i)
            m_targets.append({QRectF(x0 + i * (tileW + gap), y, tileW, 76), actions[i]});
        y += 76 + 40;
        // Upcoming.
        m_upcomingTop = y;
        y += 36;
        for (int i = 0; i < m_upcoming.size(); ++i)
            m_targets.append({QRectF(x0, y + i * 40, colW, 38), QStringLiteral("up:") + m_upcoming[i].row});
        y += qMax(1, int(m_upcoming.size())) * 40 + 60;
        m_x0 = x0;
        m_colW = colW;
        setFixedHeight(int(y));
        update();
    }

signals:
    void activated(const QString &action);

protected:
    void resizeEvent(QResizeEvent *) override { relayout(); }

    void paintEvent(QPaintEvent *e) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Q_UNUSED(e)
        Workspace *ws = Workspace::instance();
        const double starfield = Settings::instance().starfield;

        // Sky with a planet rising.
        const QRectF sky(0, 0, width(), 270);
        QLinearGradient g(sky.topLeft(), sky.bottomLeft());
        g.setColorAt(0, QColor(0x0B, 0x0B, 0x0E));
        g.setColorAt(1, c.basalt);
        p.fillRect(sky, g);
        const QDateTime now = QDateTime::currentDateTime();
        const int hour = now.time().hour();
        const QString greeting = hour < 5 ? tr("Good night") : hour < 12 ? tr("Good morning") : hour < 18 ? tr("Good afternoon") : tr("Good evening");
        int pages = 0, dbs = 0;
        if (ws)
            for (const PageMeta *m : ws->allLive()) {
                if (m->isDatabase())
                    ++dbs;
                else if (!m->isRow())
                    ++pages;
            }
        const QString sub = QLocale(QLocale::English).toString(now.date(), QStringLiteral("dddd, MMMM d")) + QStringLiteral("   ·   ")
                            + tr("%n page(s)", nullptr, pages) + QStringLiteral("   ·   ") + tr("%n database(s)", nullptr, dbs);
        // Keep the sky clear right behind the words.
        QRegion clear;
        const auto around = [&](const QFont &f, const QString &text, double cy) {
            const QFontMetricsF fm(f);
            const double w = fm.horizontalAdvance(text) + 24;
            clear += QRectF(width() / 2.0 - w / 2, cy - fm.height() / 2 - 4, w, fm.height() + 8).toAlignedRect();
        };
        around(Theme::displayFont(36, QFont::DemiBold), greeting, 113);
        around(Theme::uiFont(14), sub, 150);
        clear += QRect(width() / 2 - 210, 176, 420, 52);
        Cosmos::paintStars(p, sky.toRect(), this, 0.9 * starfield, clear);
        p.save();
        p.setClipRect(sky);
        Cosmos::paintHorizon(p, QRectF(0, 40, width(), 230), 1.0);
        QLinearGradient fade(QPointF(0, 190), QPointF(0, 270));
        fade.setColorAt(0, Theme::withAlpha(c.basalt, 0));
        fade.setColorAt(1, c.basalt);
        p.fillRect(QRectF(0, 190, width(), 80), fade);
        p.restore();

        p.setFont(Theme::displayFont(36, QFont::DemiBold));
        p.setPen(c.starlight);
        p.drawText(QRectF(0, 88, width(), 50), Qt::AlignHCenter | Qt::AlignVCenter, greeting);
        p.setFont(Theme::uiFont(14));
        p.setPen(c.mist);
        p.drawText(QRectF(0, 138, width(), 24), Qt::AlignHCenter | Qt::AlignVCenter, sub);
        // Search pill.
        const QRectF search(width() / 2.0 - 200, 182, 400, 40);
        const bool hoverSearch = m_hoverAction == QLatin1String("search");
        Theme::paintCard(p, search, 20, hoverSearch ? QColor(0x23, 0x23, 0x29) : QColor(0x1A, 0x1A, 0x1F));
        Icons::paint(p, QRectF(search.left() + 16, search.center().y() - 8, 16, 16), QStringLiteral("search"), c.fog);
        p.setFont(Theme::uiFont(14));
        p.setPen(c.fog);
        p.drawText(search.adjusted(42, 0, -16, 0), Qt::AlignVCenter, tr("Search or jump to a page…"));
        p.setFont(Theme::uiFont(12));
        p.setPen(c.dust);
        p.drawText(search.adjusted(0, 0, -18, 0), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("Ctrl+P"));
        m_searchRect = search;

        auto sectionTitle = [&](double y, const QString &icon, const QString &text) {
            Icons::paint(p, QRectF(m_x0, y + 7, 15, 15), icon, c.fog);
            p.setFont(Theme::uiFont(12.5, QFont::Medium));
            p.setPen(c.haze);
            p.drawText(QRectF(m_x0 + 22, y, 400, 28), Qt::AlignVCenter, text);
        };
        sectionTitle(m_recentTop, QStringLiteral("clock"), tr("Recently visited"));
        if (m_recent.isEmpty()) {
            p.setFont(Theme::uiFont(13.5));
            p.setPen(c.fog);
            p.drawText(QRectF(m_x0, m_recentTop + 40, m_colW, 30), Qt::AlignVCenter, tr("Pages you open show up here."));
        }
        sectionTitle(m_quickTop, QStringLiteral("sparkle"), tr("Start something"));
        sectionTitle(m_upcomingTop, QStringLiteral("calendar"), tr("Upcoming"));
        if (m_upcoming.isEmpty()) {
            p.setFont(Theme::uiFont(13.5));
            p.setPen(c.fog);
            p.drawText(QRectF(m_x0, m_upcomingTop + 40, m_colW, 30), Qt::AlignVCenter,
                       tr("Nothing dated in the next two weeks. Dates in your databases appear here."));
        }

        for (const Target &t : m_targets) {
            const bool hover = t.action == m_hoverAction;
            if (t.action.startsWith(QLatin1String("page:"))) {
                const QString id = t.action.mid(5);
                const PageMeta *m = ws->page(id);
                if (!m)
                    continue;
                const QRectF r = t.rect;
                if (hover)
                    Theme::paintSoftShadow(p, r, 12, 10, 1.0);
                Theme::paintCard(p, r, 12, hover ? QColor(0x21, 0x21, 0x26) : QColor(0x1A, 0x1A, 0x1E));
                const QRectF cover(r.left() + 1, r.top() + 1, r.width() - 2, 50);
                QPainterPath clip;
                clip.addRoundedRect(cover, 11, 11);
                QPainterPath sq;
                sq.addRect(cover.adjusted(0, 20, 0, 0));
                p.save();
                p.setClipPath(clip.united(sq));
                if (!m->cover.isEmpty())
                    Cosmos::paintCover(p, cover, m->cover, m->coverPos);
                else {
                    p.fillRect(cover, QColor(0x15, 0x15, 0x19));
                    Cosmos::paintStars(p, cover.toRect(), this, 0.7);
                }
                p.restore();
                const QRectF iconBox(r.left() + 14, r.top() + 32, 34, 34);
                p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.1), 1));
                p.setBrush(QColor(0x1E, 0x1E, 0x23));
                p.drawRoundedRect(iconBox, 9, 9);
                Icons::paintPageIcon(p, iconBox.adjusted(7, 7, -7, -7), m->icon, c.starlight,
                                     m->isDatabase() ? QStringLiteral("database") : QStringLiteral("page"));
                p.setFont(Theme::uiFont(13.5, QFont::DemiBold));
                p.setPen(c.starlight);
                const QStringList lines = Theme::wrapLines(p.font(), ws->title(id).isEmpty() ? tr("Untitled") : ws->title(id), r.width() - 28);
                const double lh = QFontMetricsF(p.font()).lineSpacing();
                for (int k = 0; k < lines.size(); ++k)
                    p.drawText(QRectF(r.left() + 14, r.top() + 74 + k * lh, r.width() - 28, lh), Qt::AlignLeft | Qt::AlignVCenter, lines[k]);
                p.setFont(Theme::uiFont(11.5));
                p.setPen(c.fog);
                p.drawText(QRectF(r.left() + 14, r.bottom() - 27, r.width() - 28, 18), Qt::AlignVCenter, relativeTime(m->visited));
            } else if (t.action.startsWith(QLatin1String("up:"))) {
                const QString id = t.action.mid(3);
                Upcoming u;
                for (const Upcoming &x : m_upcoming)
                    if (x.row == id)
                        u = x;
                const PageMeta *m = ws->page(id);
                if (!m)
                    continue;
                const QRectF r = t.rect;
                if (hover)
                    Theme::paintHover(p, r, 1.0, 8);
                const QDate today = QDate::currentDate();
                const qint64 days = today.daysTo(u.date);
                const QString when = days == 0 ? tr("Today") : days == 1 ? tr("Tomorrow")
                                                                         : QLocale(QLocale::English).toString(u.date, QStringLiteral("ddd, MMM d"));
                // A small date chip.
                const QRectF chip(r.left() + 8, r.center().y() - 12, 96, 24);
                p.setPen(QPen(days == 0 ? c.mist : c.ash, 1));
                p.setBrush(days == 0 ? QColor(0x2A, 0x2A, 0x31) : QColor(0x1D, 0x1D, 0x22));
                p.drawRoundedRect(chip, 7, 7);
                p.setFont(Theme::uiFont(12, QFont::Medium));
                p.setPen(days == 0 ? c.starlight : c.mist);
                p.drawText(chip, Qt::AlignCenter, when);
                Icons::paintPageIcon(p, QRectF(chip.right() + 14, r.center().y() - 9, 18, 18), m->icon, c.mist, QStringLiteral("page"));
                p.setFont(Theme::uiFont(14, QFont::Medium));
                p.setPen(c.moon);
                const QString dbName = ws->title(u.db);
                const double dbw = QFontMetricsF(Theme::uiFont(12.5)).horizontalAdvance(dbName) + 12;
                p.drawText(QRectF(chip.right() + 40, r.top(), r.right() - chip.right() - 52 - dbw, r.height()), Qt::AlignVCenter,
                           QFontMetricsF(p.font()).elidedText(ws->title(id), Qt::ElideRight, r.right() - chip.right() - 52 - dbw));
                p.setFont(Theme::uiFont(12.5));
                p.setPen(c.fog);
                p.drawText(QRectF(r.right() - dbw - 8, r.top(), dbw, r.height()), Qt::AlignVCenter | Qt::AlignRight, dbName);
            } else {
                const QRectF r = t.rect;
                Theme::paintCard(p, r, 12, hover ? QColor(0x22, 0x22, 0x28) : QColor(0x19, 0x19, 0x1D));
                QString icon, title, sub;
                if (t.action == QLatin1String("new")) {
                    icon = QStringLiteral("page-empty");
                    title = tr("New page");
                    sub = tr("A blank page · Ctrl+N");
                } else if (t.action == QLatin1String("db")) {
                    icon = QStringLiteral("table");
                    title = tr("New database");
                    sub = tr("Table, board, gallery…");
                } else if (t.action == QLatin1String("templates")) {
                    icon = QStringLiteral("template");
                    title = tr("From a template");
                    sub = tr("Journals, plans, trackers");
                } else {
                    icon = QStringLiteral("download");
                    title = tr("Import");
                    sub = tr("Notion, Markdown, CSV");
                }
                const QRectF iconBox(r.left() + 14, r.center().y() - 18, 36, 36);
                p.setPen(QPen(c.ash, 1));
                p.setBrush(QColor(0x22, 0x22, 0x27));
                p.drawRoundedRect(iconBox, 9, 9);
                Icons::paint(p, iconBox.adjusted(9, 9, -9, -9), icon, hover ? c.starlight : c.moon);
                p.setFont(Theme::uiFont(14, QFont::DemiBold));
                p.setPen(c.starlight);
                p.drawText(QRectF(iconBox.right() + 12, r.top() + 16, r.width() - 80, 22), Qt::AlignVCenter, title);
                p.setFont(Theme::uiFont(12));
                p.setPen(c.fog);
                p.drawText(QRectF(iconBox.right() + 12, r.top() + 38, r.width() - 80, 20), Qt::AlignVCenter,
                           QFontMetricsF(p.font()).elidedText(sub, Qt::ElideRight, r.width() - 80));
            }
        }
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        QString hover;
        if (m_searchRect.contains(e->position()))
            hover = QStringLiteral("search");
        for (const Target &t : m_targets)
            if (t.rect.contains(e->position()))
                hover = t.action;
        if (hover != m_hoverAction) {
            m_hoverAction = hover;
            setCursor(hover.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && !m_hoverAction.isEmpty())
            emit activated(m_hoverAction);
    }
    void leaveEvent(QEvent *) override
    {
        m_hoverAction.clear();
        update();
    }

private:
    struct Upcoming {
        QString row, db;
        QDate date;
    };
    QStringList m_recent;
    QList<Upcoming> m_upcoming;
    QList<Target> m_targets;
    QString m_hoverAction;
    QRectF m_searchRect;
    double m_recentTop = 0, m_quickTop = 0, m_upcomingTop = 0, m_x0 = 0, m_colW = 600;
};

HomeView::HomeView(QWidget *parent)
    : QScrollArea(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(false);
    m_canvas = new HomeCanvas(this);
    setWidget(m_canvas);
    m_canvas->setAutoFillBackground(false); // setWidget turns it on
    viewport()->setAutoFillBackground(false);
    connect(m_canvas, &HomeCanvas::activated, this, [this](const QString &action) {
        if (action.startsWith(QLatin1String("page:")))
            emit openPage(action.mid(5));
        else if (action.startsWith(QLatin1String("up:")))
            emit openPage(action.mid(3));
        else if (action == QLatin1String("new"))
            emit newPage();
        else if (action == QLatin1String("db"))
            emit newDatabase();
        else if (action == QLatin1String("import"))
            emit importRequested();
        else if (action == QLatin1String("templates"))
            emit templatesRequested();
        else if (action == QLatin1String("search"))
            emit searchRequested();
    });
}

void HomeView::refresh()
{
    m_canvas->setFixedWidth(qMax(1, viewport()->width()));
    m_canvas->refresh();
}

void HomeView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    m_canvas->setFixedWidth(qMax(1, viewport()->width()));
    m_canvas->relayout();
}

#include "HomeView.moc"
