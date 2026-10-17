// Charts (vertical and horizontal bars, lines, donuts, a single number) and
// the feed layout.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "PropertyEditors.h"
#include "core/Blocks.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QtMath>

#include <cmath>

using namespace DbViewsPrivate;

namespace
{
constexpr int kFeedLines = 6;
constexpr double kFeedLineH = 24;

// "Oct 17 · 2:58 PM", with the year when it isn't this one.
QString shortDateTime(const QDateTime &t)
{
    const QLocale en(QLocale::English);
    const bool thisYear = t.date().year() == QDate::currentDate().year();
    return en.toString(t.date(), thisYear ? QStringLiteral("MMM d") : QStringLiteral("MMM d, yyyy")) + QStringLiteral(" · ")
           + en.toString(t.time(), QStringLiteral("h:mm AP"));
}
constexpr double kChartH = 380;

// Series colours: grays from bright to dim, like the rest of Nebula.
QColor seriesColor(int i, int n)
{
    const auto &c = Theme::colors();
    if (n <= 1)
        return c.mist;
    const QColor stops[] = {c.starlight, c.mist, c.fog, c.dust, c.smoke, c.haze, c.moon};
    return stops[i % 7];
}

// A round step for the value axis.
double niceStep(double max, int ticks)
{
    if (max <= 0)
        return 1;
    const double raw = max / ticks;
    const double mag = std::pow(10, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double nice = norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10;
    return nice * mag;
}

QString shortNumber(double v)
{
    if (std::abs(v) >= 1e6)
        return QLocale(QLocale::English).toString(v / 1e6, 'f', v >= 1e7 ? 0 : 1) + QStringLiteral("M");
    if (std::abs(v) >= 1e4)
        return QLocale(QLocale::English).toString(v / 1e3, 'f', 0) + QStringLiteral("k");
    if (v == std::floor(v))
        return QLocale(QLocale::English).toString(qint64(v));
    return QLocale(QLocale::English).toString(v, 'f', 2);
}
} // namespace

ChartView::ChartView(QWidget *parent)
    : DbView(parent)
{
}

void ChartView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    compute();
    setFixedHeight(int(m_ctx.view.chartType == QLatin1String("number") ? 220 : kChartH));
    update();
}

double ChartView::yValue(const QList<int> &rows) const
{
    const QString y = m_ctx.view.chartY;
    if (y.isEmpty() || y == QLatin1String("count"))
        return rows.size();
    const QString fn = y.section(QLatin1Char(':'), 0, 0);
    const Property *p = m_ctx.schema.prop(y.section(QLatin1Char(':'), 1));
    if (!p)
        return rows.size();
    QList<QJsonValue> values;
    for (int r : rows)
        values.append(rowValue(*p, m_ctx.rows[r]));
    const QJsonValue v = aggregate(*p, values, fn);
    return v.isDouble() ? v.toDouble() : 0;
}

void ChartView::compute()
{
    m_points.clear();
    m_series.clear();
    m_total = 0;
    const View &view = m_ctx.view;
    const Property *yProp = m_ctx.schema.prop(view.chartY.section(QLatin1Char(':'), 1));
    m_yLabel = view.chartY == QLatin1String("count") || !yProp ? tr("Count")
                                                                : QStringLiteral("%1 · %2").arg(calcName(view.chartY.section(QLatin1Char(':'), 0, 0)), yProp->name);
    QList<int> all;
    for (int i = 0; i < m_ctx.rows.size(); ++i)
        all.append(i);
    m_total = yValue(all);
    const Property *x = m_ctx.schema.prop(view.chartX);
    if (!x || !canGroupBy(*x))
        return;
    const QString mode = resultType(*x) == PropType::Date || x->type == PropType::Created || x->type == PropType::Edited ? QStringLiteral("month")
                                                                                                                       : QString();
    const QList<RowGroup> groupsX = groupRows(m_ctx.schema, *x, m_ctx.rows, mode, view.groupStep, false);
    const Property *s = m_ctx.schema.prop(view.chartGroup);
    if (s && (!canGroupBy(*s) || s == x))
        s = nullptr;
    QList<RowGroup> seriesGroups;
    if (s) {
        seriesGroups = groupRows(m_ctx.schema, *s, m_ctx.rows, QString(), view.groupStep, true);
        for (const RowGroup &g : seriesGroups)
            m_series.append(g.label);
    } else {
        m_series.append(m_yLabel);
    }
    for (const RowGroup &g : groupsX) {
        Point pt;
        pt.key = g.key;
        pt.label = g.label;
        if (s) {
            for (const RowGroup &sg : seriesGroups) {
                QList<int> rows;
                QStringList ids;
                const QSet<int> in(sg.rows.begin(), sg.rows.end());
                for (int r : g.rows)
                    if (in.contains(r)) {
                        rows.append(r);
                        if (m_ctx.rows[r].meta)
                            ids.append(m_ctx.rows[r].meta->id);
                    }
                pt.values.append(yValue(rows));
                pt.rows.append(ids);
            }
        } else {
            QStringList ids;
            for (int r : g.rows)
                if (m_ctx.rows[r].meta)
                    ids.append(m_ctx.rows[r].meta->id);
            pt.values.append(yValue(g.rows));
            pt.rows.append(ids);
        }
        m_points.append(pt);
    }
    auto total = [](const Point &p) {
        double t = 0;
        for (double v : p.values)
            t += v;
        return t;
    };
    if (view.chartHideEmpty)
        m_points.removeIf([&](const Point &p) { return total(p) == 0 || p.key.isEmpty(); });
    if (view.chartSort == QLatin1String("xasc") || view.chartSort == QLatin1String("xdesc")) {
        const bool desc = view.chartSort == QLatin1String("xdesc");
        std::stable_sort(m_points.begin(), m_points.end(), [desc](const Point &a, const Point &b) {
            const int c = QString::localeAwareCompare(a.label, b.label);
            return desc ? c > 0 : c < 0;
        });
    } else if (view.chartSort == QLatin1String("yasc") || view.chartSort == QLatin1String("ydesc")) {
        const bool desc = view.chartSort == QLatin1String("ydesc");
        std::stable_sort(m_points.begin(), m_points.end(), [&](const Point &a, const Point &b) { return desc ? total(a) > total(b) : total(a) < total(b); });
    }
    if (view.chartCumulative) {
        QList<double> run(m_series.size(), 0);
        for (Point &p : m_points)
            for (int k = 0; k < p.values.size(); ++k) {
                run[k] += p.values[k];
                p.values[k] = run[k];
            }
    }
}

void ChartView::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const View &view = m_ctx.view;
    const QRectF area(0, 0, width(), height());
    Theme::paintCard(p, area.adjusted(1, 1, -1, -1), 12, QColor(0x17, 0x17, 0x1B));
    const QString type = view.chartType;
    if (type == QLatin1String("number")) {
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        p.setPen(c.fog);
        p.drawText(QRectF(24, 22, width() - 48, 20), Qt::AlignVCenter, m_yLabel.toUpper());
        p.setFont(Theme::displayFont(64, QFont::DemiBold));
        p.setPen(c.starlight);
        p.drawText(QRectF(24, 50, width() - 48, 110), Qt::AlignVCenter, shortNumber(m_total));
        p.setFont(Theme::uiFont(12.5));
        p.setPen(c.haze);
        p.drawText(QRectF(24, 160, width() - 48, 24), Qt::AlignVCenter, tr("%n row(s)", nullptr, int(m_ctx.rows.size())));
        return;
    }
    if (m_points.isEmpty()) {
        QRectF dummy;
        paintEmptyState(p, area.adjusted(0, 60, 0, 0),
                        m_ctx.schema.prop(view.chartX) ? tr("Nothing to chart yet.") : tr("Choose a property for the X axis in the view settings."),
                        QString(), &dummy);
        return;
    }
    const int n = int(m_series.size());
    // Legend.
    double lx = 24;
    if (n > 1) {
        p.setFont(Theme::uiFont(12));
        for (int k = 0; k < n; ++k) {
            p.setPen(Qt::NoPen);
            p.setBrush(seriesColor(k, n));
            p.drawRoundedRect(QRectF(lx, 20, 10, 10), 2, 2);
            p.setPen(c.haze);
            const double w = QFontMetricsF(p.font()).horizontalAdvance(m_series[k]);
            p.drawText(QRectF(lx + 16, 14, w + 4, 22), Qt::AlignVCenter, m_series[k]);
            lx += w + 34;
        }
    }
    if (type == QLatin1String("donut")) {
        const double R = qMin(height() - 80.0, width() * 0.4) / 2;
        const QPointF center(qMin(width() * 0.33, R + 60), height() / 2.0 + 10);
        double sum = 0;
        QList<double> totals;
        for (const Point &pt : m_points) {
            double t = 0;
            for (double v : pt.values)
                t += v;
            totals.append(t);
            sum += t;
        }
        double angle = 90 * 16;
        const QRectF box(center.x() - R, center.y() - R, 2 * R, 2 * R);
        for (int i = 0; i < m_points.size(); ++i) {
            const double span = sum > 0 ? -totals[i] / sum * 360 * 16 : 0;
            QPainterPath slice;
            slice.moveTo(center);
            slice.arcTo(box, angle / 16.0, span / 16.0);
            slice.closeSubpath();
            QColor col = seriesColor(i, int(m_points.size()) + 1);
            if (i == m_hover)
                col = col.lighter(125);
            p.setPen(QPen(QColor(0x17, 0x17, 0x1B), 2));
            p.setBrush(col);
            p.drawPath(slice);
            m_points[i].hit = QRectF(); // donut hits are angular
            angle += span;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x17, 0x17, 0x1B));
        p.drawEllipse(center, R * 0.6, R * 0.6);
        p.setFont(Theme::displayFont(26, QFont::DemiBold));
        p.setPen(c.starlight);
        p.drawText(QRectF(center.x() - R, center.y() - 22, 2 * R, 30), Qt::AlignCenter, shortNumber(sum));
        p.setFont(Theme::uiFont(11.5));
        p.setPen(c.fog);
        p.drawText(QRectF(center.x() - R, center.y() + 8, 2 * R, 18), Qt::AlignCenter, m_yLabel);
        // Legend beside the donut.
        double y = 40;
        p.setFont(Theme::uiFont(12.5));
        for (int i = 0; i < m_points.size() && y < height() - 24; ++i) {
            const double x0 = center.x() + R + 40;
            m_points[i].hit = QRectF(x0 - 6, y - 2, width() - x0 - 12, 24);
            if (i == m_hover)
                Theme::paintHover(p, m_points[i].hit, 1.0, 5);
            p.setPen(Qt::NoPen);
            p.setBrush(seriesColor(i, int(m_points.size()) + 1));
            p.drawEllipse(QRectF(x0, y + 6, 9, 9));
            p.setPen(c.moon);
            p.drawText(QRectF(x0 + 18, y, width() - x0 - 120, 20), Qt::AlignVCenter, m_points[i].label);
            p.setPen(c.fog);
            const QString val = view.chartLabels ? QStringLiteral("%1  ·  %2%").arg(shortNumber(totals[i])).arg(sum > 0 ? qRound(totals[i] / sum * 100) : 0)
                                                 : shortNumber(totals[i]);
            p.drawText(QRectF(x0 + 18, y, width() - x0 - 36, 20), Qt::AlignVCenter | Qt::AlignRight, val);
            y += 26;
        }
        return;
    }
    // Axes charts.
    double maxV = 0;
    const bool stacked = type != QLatin1String("line");
    for (const Point &pt : m_points) {
        double t = 0;
        for (double v : pt.values) {
            t += v;
            maxV = qMax(maxV, v);
        }
        if (stacked)
            maxV = qMax(maxV, t);
    }
    const double step = niceStep(maxV, 4);
    const double top = std::max(step * std::ceil(maxV / step), step);
    const bool horizontal = type == QLatin1String("hbar");
    p.setFont(Theme::uiFont(11.5));
    double labelW = 0;
    if (horizontal)
        for (const Point &pt : m_points)
            labelW = qMax(labelW, QFontMetricsF(p.font()).horizontalAdvance(pt.label));
    labelW = qMin(labelW + 8, 180.0);
    const QRectF plot = horizontal ? QRectF(24 + labelW, 50, width() - 48 - labelW - 30, height() - 90) : QRectF(64, 50, width() - 90, height() - 104);
    // Grid and value labels.
    p.setFont(Theme::uiFont(11));
    for (double v = 0; v <= top + 1e-9; v += step) {
        const double t = v / top;
        p.setPen(QPen(v == 0 ? c.ash : c.hairline, 1));
        if (horizontal) {
            const double x = plot.left() + t * plot.width();
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            p.setPen(c.fog);
            p.drawText(QRectF(x - 40, plot.bottom() + 6, 80, 16), Qt::AlignCenter, shortNumber(v));
        } else {
            const double y = plot.bottom() - t * plot.height();
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            p.setPen(c.fog);
            p.drawText(QRectF(4, y - 8, plot.left() - 12, 16), Qt::AlignVCenter | Qt::AlignRight, shortNumber(v));
        }
    }
    const int count = int(m_points.size());
    const double band = (horizontal ? plot.height() : plot.width()) / count;
    const double barW = qMin(band * 0.62, horizontal ? 28.0 : 64.0);
    if (type == QLatin1String("line")) {
        for (int k = 0; k < n; ++k) {
            QPainterPath path, fill;
            for (int i = 0; i < count; ++i) {
                const double x = plot.left() + band * (i + 0.5);
                const double y = plot.bottom() - m_points[i].values.value(k) / top * plot.height();
                if (i == 0) {
                    path.moveTo(x, y);
                    fill.moveTo(x, plot.bottom());
                    fill.lineTo(x, y);
                } else {
                    path.lineTo(x, y);
                    fill.lineTo(x, y);
                }
                if (i == count - 1) {
                    fill.lineTo(x, plot.bottom());
                    fill.closeSubpath();
                }
            }
            QColor col = seriesColor(k, n);
            if (n == 1) {
                QColor wash = col;
                wash.setAlpha(26);
                p.setPen(Qt::NoPen);
                p.setBrush(wash);
                p.drawPath(fill);
            }
            p.setPen(QPen(col, 2));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            for (int i = 0; i < count; ++i) {
                const double x = plot.left() + band * (i + 0.5);
                const double v = m_points[i].values.value(k);
                const double y = plot.bottom() - v / top * plot.height();
                p.setPen(QPen(QColor(0x17, 0x17, 0x1B), 2));
                p.setBrush(col);
                p.drawEllipse(QPointF(x, y), i == m_hover ? 5 : 3.5, i == m_hover ? 5 : 3.5);
                if (view.chartLabels && n == 1 && count <= 24) {
                    p.setFont(Theme::uiFont(11, QFont::Medium));
                    p.setPen(c.haze);
                    p.drawText(QRectF(x - 40, y - 22, 80, 16), Qt::AlignCenter, shortNumber(v));
                }
            }
        }
        for (int i = 0; i < count; ++i)
            m_points[i].hit = QRectF(plot.left() + band * i, plot.top(), band, plot.height());
    } else {
        for (int i = 0; i < count; ++i) {
            const Point &pt = m_points[i];
            double acc = 0;
            for (int k = 0; k < pt.values.size(); ++k) {
                const double v = pt.values[k];
                if (v <= 0)
                    continue;
                QColor col = seriesColor(k, n);
                if (i == m_hover)
                    col = col.lighter(118);
                p.setPen(Qt::NoPen);
                p.setBrush(col);
                QRectF r;
                if (horizontal) {
                    const double y = plot.top() + band * i + (band - barW) / 2;
                    r = QRectF(plot.left() + acc / top * plot.width(), y, v / top * plot.width(), barW);
                } else {
                    const double x = plot.left() + band * i + (band - barW) / 2;
                    const double h = v / top * plot.height();
                    r = QRectF(x, plot.bottom() - acc / top * plot.height() - h, barW, h);
                }
                p.drawRoundedRect(r, 3, 3);
                acc += v;
            }
            if (view.chartLabels && acc > 0) {
                p.setFont(Theme::uiFont(11, QFont::Medium));
                p.setPen(c.haze);
                if (horizontal) {
                    const double y = plot.top() + band * i;
                    p.drawText(QRectF(plot.left() + acc / top * plot.width() + 6, y, 80, band), Qt::AlignVCenter, shortNumber(acc));
                } else {
                    const double x = plot.left() + band * i;
                    p.drawText(QRectF(x, plot.bottom() - acc / top * plot.height() - 20, band, 16), Qt::AlignCenter, shortNumber(acc));
                }
            }
            m_points[i].hit = horizontal ? QRectF(plot.left() - labelW, plot.top() + band * i, plot.width() + labelW, band)
                                         : QRectF(plot.left() + band * i, plot.top(), band, plot.height());
        }
    }
    // Category labels.
    p.setFont(Theme::uiFont(11.5));
    for (int i = 0; i < count; ++i) {
        p.setPen(i == m_hover ? c.moon : c.fog);
        if (horizontal) {
            p.drawText(QRectF(20, plot.top() + band * i, labelW - 4, band), Qt::AlignVCenter | Qt::AlignRight,
                       QFontMetricsF(p.font()).elidedText(m_points[i].label, Qt::ElideRight, labelW - 4));
        } else {
            // Skip labels when they would overlap.
            const int every = qMax(1, int(std::ceil(60.0 / band)));
            if (i % every)
                continue;
            p.drawText(QRectF(plot.left() + band * i - 10, plot.bottom() + 8, band * every + 20, 18), Qt::AlignHCenter | Qt::AlignTop,
                       QFontMetricsF(p.font()).elidedText(m_points[i].label, Qt::ElideRight, band * every + 16));
        }
    }
    p.setFont(Theme::uiFont(11.5, QFont::Medium));
    p.setPen(c.dust);
    if (!horizontal)
        p.drawText(QRectF(plot.left(), height() - 26, plot.width(), 18), Qt::AlignCenter,
                   m_ctx.schema.prop(view.chartX) ? m_ctx.schema.prop(view.chartX)->name : QString());
}

void ChartView::mouseMoveEvent(QMouseEvent *e)
{
    int hover = -1;
    for (int i = 0; i < m_points.size(); ++i)
        if (m_points[i].hit.contains(e->position()))
            hover = i;
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    if (hover >= 0) {
        const Point &pt = m_points[hover];
        QStringList lines{QStringLiteral("<b>%1</b>").arg(pt.label.toHtmlEscaped())};
        for (int k = 0; k < pt.values.size(); ++k)
            lines.append(QStringLiteral("%1: %2").arg(m_series.value(k).toHtmlEscaped(), shortNumber(pt.values[k])));
        QToolTip::showText(e->globalPosition().toPoint(), lines.join(QStringLiteral("<br>")), this);
        setCursor(Qt::PointingHandCursor);
    } else {
        QToolTip::hideText();
        setCursor(Qt::ArrowCursor);
    }
}

void ChartView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_hover < 0)
        return;
    // The rows behind a bar or slice.
    const Point &pt = m_points[m_hover];
    QMenu *menu = Theme::makeMenu(this);
    Theme::addMenuSection(menu, pt.label);
    int n = 0;
    Workspace *ws = Workspace::instance();
    for (const QStringList &ids : pt.rows)
        for (const QString &id : ids) {
            if (++n > 30)
                break;
            const PageMeta *m = ws->page(id);
            menu->addAction(Icons::icon(QStringLiteral("page")), m && !m->title.isEmpty() ? m->title : tr("Untitled"), this, [this, id] { emit openRow(id); });
        }
    if (n == 0)
        menu->addAction(tr("No rows"))->setEnabled(false);
    menu->popup(e->globalPosition().toPoint());
}

void ChartView::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---- FeedView -------------------------------------------------------------------

FeedView::FeedView(QWidget *parent)
    : DbView(parent)
{
}

void FeedView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    layoutPosts();
    update();
}

void FeedView::resizeEvent(QResizeEvent *)
{
    layoutPosts();
}

void FeedView::layoutPosts()
{
    m_posts.clear();
    const double w = qMin(720.0, width() - 8.0);
    const double left = 4;
    double y = 0;
    m_addRect = QRectF(left, 0, w, 44);
    y += 56;
    Workspace *ws = Workspace::instance();
    int props = 0;
    for (const Property *p : m_ctx.schema.orderedProps(m_ctx.view))
        if (p->type != PropType::Title)
            ++props;
    const int limit = rowLimit();
    for (int i = 0; i < m_ctx.rows.size() && i < limit; ++i) {
        const RowRef &r = m_ctx.rows[i];
        Post post;
        post.row = i;
        if (r.meta && ws) {
            const QList<Block> blocks = ws->content(r.meta->id);
            int number = 0;
            for (const Block &b : blocks) {
                if (b.type == BlockType::Image && !b.media.isEmpty() && post.image.isEmpty())
                    post.image = b.media;
                const QString text = b.plainText().simplified();
                number = b.type == BlockType::Numbered ? number + 1 : 0;
                if (text.isEmpty())
                    continue;
                if (post.lines.size() == kFeedLines) {
                    post.more = true;
                    break;
                }
                PreviewLine line;
                line.text = text;
                line.depth = qMin(b.depth, 3);
                switch (b.type) {
                case BlockType::Heading1:
                case BlockType::Heading2:
                case BlockType::Heading3:
                case BlockType::Heading4:
                    line.kind = PreviewLine::Heading;
                    break;
                case BlockType::Bullet:
                    line.kind = PreviewLine::Bullet;
                    break;
                case BlockType::Numbered:
                    line.kind = PreviewLine::Number;
                    line.number = number;
                    break;
                case BlockType::Todo:
                    line.kind = PreviewLine::Todo;
                    line.checked = b.checked;
                    break;
                case BlockType::Quote:
                case BlockType::Callout:
                    line.kind = PreviewLine::Quote;
                    break;
                default:
                    break;
                }
                post.lines.append(line);
            }
        }
        double h = 20 + 26 + 8; // author line, title
        h += 30;                // title
        if (props)
            h += 30;
        if (!post.lines.isEmpty())
            h += post.lines.size() * kFeedLineH + 10;
        if (!post.image.isEmpty())
            h += 260;
        h += 20;
        post.rect = QRectF(left, y, w, h);
        m_posts.append(post);
        y += h + 16;
    }
    setFixedHeight(int(y + 10));
}

void FeedView::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QLocale en(QLocale::English);
    if (!m_addRect.isNull()) {
        p.setPen(QPen(m_hoverAdd ? c.smoke : c.ash, 1));
        p.setBrush(m_hoverAdd ? c.graphite : QColor(0x17, 0x17, 0x1B));
        p.drawRoundedRect(m_addRect.adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
        Icons::paint(p, QRectF(m_addRect.left() + 16, m_addRect.center().y() - 8, 16, 16), QStringLiteral("pencil"), c.fog);
        p.setFont(Theme::uiFont(14));
        p.setPen(c.fog);
        p.drawText(m_addRect.adjusted(44, 0, 0, 0), Qt::AlignVCenter, tr("Write a new post…"));
    }
    Workspace *ws = Workspace::instance();
    for (int i = 0; i < m_posts.size(); ++i) {
        const Post &post = m_posts[i];
        if (!post.rect.intersects(e->rect()))
            continue;
        const RowRef &r = m_ctx.rows[post.row];
        const QRectF rect = post.rect;
        Theme::paintCard(p, rect, 12, i == m_hover ? QColor(0x1D, 0x1D, 0x22) : QColor(0x19, 0x19, 0x1D));
        double y = rect.top() + 18;
        // Author and time.
        const QString author = ws ? ws->personName(ws->lookupMe()) : QString();
        p.setPen(Qt::NoPen);
        p.setBrush(c.ash);
        p.drawEllipse(QRectF(rect.left() + 20, y, 24, 24));
        p.setFont(Theme::uiFont(11, QFont::DemiBold));
        p.setPen(c.starlight);
        p.drawText(QRectF(rect.left() + 20, y, 24, 24), Qt::AlignCenter, author.left(1).toUpper());
        p.setFont(Theme::uiFont(13, QFont::Medium));
        p.setPen(c.moon);
        p.drawText(QRectF(rect.left() + 52, y, 300, 24), Qt::AlignVCenter, author);
        p.setFont(Theme::uiFont(12));
        p.setPen(c.fog);
        if (r.meta)
            p.drawText(QRectF(rect.left() + 52, y, rect.width() - 72, 24), Qt::AlignVCenter | Qt::AlignRight,
                       shortDateTime(QDateTime::fromMSecsSinceEpoch(r.meta->created)));
        y += 34;
        double x = rect.left() + 20;
        if (m_ctx.view.showPageIcon && r.meta && !r.meta->icon.isEmpty()) {
            Icons::paintPageIcon(p, QRectF(x, y + 3, 20, 20), r.meta->icon, c.mist);
            x += 28;
        }
        p.setFont(Theme::displayFont(19, QFont::DemiBold));
        p.setPen(r.meta && !r.meta->title.trimmed().isEmpty() ? c.starlight : c.haze);
        p.drawText(QRectF(x, y, rect.right() - 20 - x, 28), Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(titleOf(r), Qt::ElideRight, rect.right() - 20 - x));
        y += 34;
        // Properties in a row.
        double px = rect.left() + 20;
        bool anyProps = false;
        for (const Property *prop : m_ctx.schema.orderedProps(m_ctx.view)) {
            if (prop->type == PropType::Title)
                continue;
            anyProps = true;
            const QJsonValue v = rowValue(*prop, r);
            if (isEmptyValue(*prop, v))
                continue;
            const double w = qMin(200.0, PropertyEditors::valueWidth(*prop, v) + 4);
            if (px + w > rect.right() - 20)
                break;
            PropertyEditors::paintValue(p, QRectF(px, y, w, 24), *prop, v, true);
            px += w + 12;
        }
        if (anyProps)
            y += 30;
        for (int k = 0; k < post.lines.size(); ++k) {
            const PreviewLine &line = post.lines[k];
            const QRectF row(rect.left() + 20 + line.depth * 22, y, rect.width() - 40 - line.depth * 22, kFeedLineH);
            double tx = row.left();
            QFont f = Theme::uiFont(14, line.kind == PreviewLine::Heading ? QFont::DemiBold : QFont::Normal);
            QColor pen = line.kind == PreviewLine::Heading ? c.starlight : c.moon;
            switch (line.kind) {
            case PreviewLine::Bullet:
                p.setPen(Qt::NoPen);
                p.setBrush(c.mist);
                p.drawEllipse(QPointF(tx + 6, row.center().y()), 2.5, 2.5);
                tx += 22;
                break;
            case PreviewLine::Number:
                p.setFont(Theme::uiFont(14));
                p.setPen(c.haze);
                p.drawText(QRectF(tx, row.top(), 20, row.height()), Qt::AlignVCenter, QStringLiteral("%1.").arg(line.number));
                tx += 22;
                break;
            case PreviewLine::Todo: {
                const QRectF box(tx + 1, row.center().y() - 7, 14, 14);
                p.setPen(QPen(line.checked ? c.moon : c.dust, 1.2));
                p.setBrush(line.checked ? QBrush(c.moon) : QBrush(Qt::NoBrush));
                p.drawRoundedRect(box, 3.5, 3.5);
                if (line.checked)
                    Icons::paint(p, box.adjusted(2, 2, -2, -2), QStringLiteral("check"), c.basalt);
                tx += 22;
                if (line.checked) {
                    f.setStrikeOut(true);
                    pen = c.fog;
                }
                break;
            }
            case PreviewLine::Quote:
                p.setPen(Qt::NoPen);
                p.setBrush(c.smoke);
                p.drawRoundedRect(QRectF(tx, row.top() + 3, 2.5, row.height() - 6), 1, 1);
                tx += 12;
                pen = c.mist;
                break;
            default:
                break;
            }
            // The last line fades out when there's more below.
            if (post.more && k == post.lines.size() - 1)
                pen.setAlphaF(0.45);
            p.setFont(f);
            p.setPen(pen);
            p.drawText(QRectF(tx, row.top(), row.right() - tx, row.height()), Qt::AlignVCenter,
                       QFontMetricsF(f).elidedText(line.text, Qt::ElideRight, row.right() - tx));
            y += kFeedLineH;
        }
        if (!post.lines.isEmpty())
            y += 10;
        if (!post.image.isEmpty()) {
            const QRectF img(rect.left() + 20, y, rect.width() - 40, 250);
            QPainterPath clip;
            clip.addRoundedRect(img, 8, 8);
            p.save();
            p.setClipPath(clip);
            p.fillRect(img, QColor(0x12, 0x12, 0x15));
            paintPreview(p, img, previewImage(post.image, img.size(), false), false);
            p.restore();
        }
    }
}

void FeedView::mouseMoveEvent(QMouseEvent *e)
{
    int hover = -1;
    for (int i = 0; i < m_posts.size(); ++i)
        if (m_posts[i].rect.contains(e->position()))
            hover = i;
    const bool add = m_addRect.contains(e->position());
    if (hover != m_hover || add != m_hoverAdd) {
        m_hover = hover;
        m_hoverAdd = add;
        update();
    }
    setCursor(hover >= 0 || add ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void FeedView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    if (m_hoverAdd) {
        emit createRow({}, true);
        return;
    }
    if (m_hover >= 0 && m_ctx.rows[m_posts[m_hover].row].meta)
        emit openRow(m_ctx.rows[m_posts[m_hover].row].meta->id);
}

void FeedView::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_hoverAdd = false;
    update();
}
