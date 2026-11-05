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
#include <QHash>
#include <QPainterPath>
#include <QSvgGenerator>
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

// Bar and line colours: grays by default (the rest of Nebula), or Notion's
// palette, muted for the dark page.
QColor paletteColor(const QString &name)
{
    static const QHash<QString, QColor> colors = {
        {QStringLiteral("gray"), QColor(0x91, 0x91, 0x99)},   {QStringLiteral("brown"), QColor(0xA8, 0x80, 0x6A)},
        {QStringLiteral("orange"), QColor(0xCF, 0x86, 0x48)}, {QStringLiteral("yellow"), QColor(0xCD, 0xAA, 0x4E)},
        {QStringLiteral("green"), QColor(0x6E, 0xAD, 0x7C)},  {QStringLiteral("blue"), QColor(0x64, 0x96, 0xD6)},
        {QStringLiteral("purple"), QColor(0x9B, 0x80, 0xCF)}, {QStringLiteral("pink"), QColor(0xC9, 0x74, 0xA6)},
        {QStringLiteral("red"), QColor(0xD0, 0x6E, 0x66)},
    };
    return colors.value(name);
}

const QStringList &colorfulOrder()
{
    static const QStringList order = {QStringLiteral("blue"),   QStringLiteral("green"), QStringLiteral("orange"),
                                      QStringLiteral("purple"), QStringLiteral("pink"),  QStringLiteral("yellow"),
                                      QStringLiteral("red"),    QStringLiteral("brown"), QStringLiteral("gray")};
    return order;
}

// A round step for an axis: 1, 2, 2.5 or 5 times a power of ten.
double niceStep(double span, int ticks)
{
    if (span <= 0 || ticks <= 0)
        return 1;
    const double raw = span / ticks;
    const double mag = std::pow(10, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double nice = norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 2.5 ? 2.5 : norm <= 5 ? 5 : 10;
    return nice * mag;
}

// "1.5k" for axis ticks that would crowd each other.
QString compactNumber(double v)
{
    const QLocale en(QLocale::English);
    if (std::abs(v) >= 1e6)
        return en.toString(v / 1e6, 'f', std::abs(v) >= 1e7 ? 0 : 1) + QStringLiteral("M");
    if (std::abs(v) >= 1e4)
        return en.toString(v / 1e3, 'f', 0) + QStringLiteral("k");
    return {};
}

double chartHeight(const QString &size, bool number)
{
    if (number)
        return size == QLatin1String("small") ? 150 : size == QLatin1String("large") ? 280 : size == QLatin1String("xlarge") ? 340 : 210;
    return size == QLatin1String("small") ? 260 : size == QLatin1String("large") ? 520 : size == QLatin1String("xlarge") ? 680 : 380;
}

// A dashed line for reference lines.
QPen dashed(const QColor &c)
{
    QPen pen(c, 1);
    pen.setDashPattern({4, 4});
    return pen;
}
} // namespace

ChartView::ChartView(QWidget *parent)
    : DbView(parent)
{
    setMouseTracking(true);
}

void ChartView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    compute();
    double h = chartHeight(m_ctx.view.chartHeight, m_ctx.view.chartType == QLatin1String("number"));
    if (!m_ctx.view.chartCaption.isEmpty())
        h += 30;
    setFixedHeight(int(h));
    update();
}

QString ChartView::formatValue(double v) const
{
    const int decimals = m_ctx.view.chartDecimals;
    const QLocale en(QLocale::English);
    if (decimals >= 0)
        return en.toString(v, 'f', decimals);
    // As needed: up to two decimals, no trailing zeros.
    QString s = en.toString(v, 'f', 2);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

QString ChartView::formatTick(double v, double step) const
{
    const QString compact = compactNumber(v);
    if (!compact.isEmpty())
        return compact;
    // As many decimals as the step needs.
    int decimals = 0;
    for (double st = step; decimals < 4 && std::abs(st - std::round(st)) > 1e-9; st *= 10)
        ++decimals;
    return QLocale(QLocale::English).toString(v, 'f', decimals);
}

QColor ChartView::colorFor(int series, int seriesCount, int point, int pointCount) const
{
    const auto &c = Theme::colors();
    const QString scheme = m_ctx.view.chartColor;
    // One series: bars take the colour; colorful gives each bar its own.
    const bool perPoint = seriesCount <= 1 && pointCount > 0;
    const int index = perPoint ? point : series;
    const int count = perPoint ? pointCount : seriesCount;
    if (scheme == QLatin1String("colorful"))
        return paletteColor(colorfulOrder().value(index % colorfulOrder().size()));
    const QColor base = paletteColor(scheme);
    if (base.isValid()) {
        if (seriesCount <= 1)
            return base;
        // Several series of one colour: steps from the colour toward the page.
        const double t = seriesCount > 1 ? double(series) / (seriesCount) : 0;
        return Theme::mix(base, c.basalt, t * 0.75);
    }
    // Grays, bright to dim.
    if (count <= 1 || perPoint)
        return c.mist;
    const QColor stops[] = {c.starlight, c.mist, c.fog, c.dust, c.smoke, c.haze, c.moon};
    return stops[index % 7];
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

QString ChartView::valueName() const
{
    const QString y = m_ctx.view.chartY;
    const Property *p = m_ctx.schema.prop(y.section(QLatin1Char(':'), 1));
    if (y == QLatin1String("count") || !p)
        return tr("Count");
    return QStringLiteral("%1 (%2)").arg(p->name, calcName(y.section(QLatin1Char(':'), 0, 0)));
}

QString ChartView::categoryName() const
{
    const Property *x = m_ctx.schema.prop(m_ctx.view.chartX);
    return x ? x->name : QString();
}

void ChartView::compute()
{
    m_points.clear();
    m_series.clear();
    m_total = 0;
    const View &view = m_ctx.view;
    m_yLabel = valueName();
    QList<int> all;
    for (int i = 0; i < m_ctx.rows.size(); ++i)
        all.append(i);
    m_total = yValue(all);
    const Property *x = m_ctx.schema.prop(view.chartX);
    if (!x || !canGroupBy(*x))
        return;
    // The categories. By name: one per page, in the view's order (pages
    // sharing a name share a bar, like Notion). Otherwise the property's groups.
    QList<RowGroup> groupsX;
    if (x->type == PropType::Title) {
        QHash<QString, int> byName;
        for (int i = 0; i < m_ctx.rows.size(); ++i) {
            const QString name = titleOf(m_ctx.rows[i]);
            auto it = byName.constFind(name);
            if (it == byName.constEnd()) {
                RowGroup g;
                g.key = name;
                g.label = name;
                it = byName.insert(name, int(groupsX.size()));
                groupsX.append(g);
            }
            groupsX[*it].rows.append(i);
        }
    } else {
        const QString mode = resultType(*x) == PropType::Date || x->type == PropType::Created || x->type == PropType::Edited ? QStringLiteral("month")
                                                                                                                           : QString();
        groupsX = groupRows(m_ctx.schema, *x, m_ctx.rows, mode, view.groupStep, false);
    }
    const Property *s = m_ctx.schema.prop(view.chartGroup);
    if (s && (!canGroupBy(*s) || s == x || s->type == PropType::Title))
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
    QPainter p(this);
    paintChart(p, QRectF(rect()));
}

bool ChartView::saveImage(const QString &path)
{
    const QSize size = this->size();
    const QColor background = Theme::colors().basalt;
    bool ok = false;
    if (path.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive)) {
        QSvgGenerator svg;
        svg.setFileName(path);
        svg.setSize(size);
        svg.setViewBox(QRect(QPoint(0, 0), size));
        svg.setTitle(m_ctx.view.name);
        QPainter p(&svg);
        p.fillRect(QRect(QPoint(0, 0), size), background);
        paintChart(p, QRectF(QPointF(0, 0), size));
        ok = p.end();
    } else {
        // Twice the size on screen: sharp in documents and slides.
        QImage image(size * 2, QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(2);
        image.fill(background);
        QPainter p(&image);
        paintChart(p, QRectF(QPointF(0, 0), size));
        p.end();
        ok = image.save(path);
    }
    update(); // the hit areas follow the window again
    return ok;
}

void ChartView::paintChart(QPainter &p, const QRectF &rect)
{
    const auto &c = Theme::colors();
    p.setRenderHint(QPainter::Antialiasing);
    const View &view = m_ctx.view;
    QRectF area = rect.adjusted(4, 8, -4, -8);
    if (!view.chartCaption.isEmpty()) {
        p.setFont(Theme::uiFont(12.5));
        p.setPen(c.fog);
        p.drawText(QRectF(area.left(), area.bottom() - 22, area.width(), 22), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(view.chartCaption, Qt::ElideRight, area.width()));
        area.setBottom(area.bottom() - 30);
    }
    const QString type = view.chartType;
    if (type == QLatin1String("number")) {
        p.setFont(Theme::uiFont(13, QFont::Medium));
        p.setPen(c.fog);
        p.drawText(QRectF(area.left() + 16, area.top() + 10, area.width() - 32, 22), Qt::AlignVCenter, m_yLabel);
        const QColor tint = paletteColor(view.chartColor);
        p.setFont(Theme::displayFont(qMin(72.0, area.height() * 0.36), QFont::DemiBold));
        p.setPen(tint.isValid() ? tint : c.starlight);
        p.drawText(QRectF(area.left() + 16, area.top() + 36, area.width() - 32, area.height() - 80), Qt::AlignVCenter, formatValue(m_total));
        p.setFont(Theme::uiFont(12.5));
        p.setPen(c.haze);
        p.drawText(QRectF(area.left() + 16, area.bottom() - 30, area.width() - 32, 24), Qt::AlignVCenter,
                   tr("%n page(s)", nullptr, int(m_ctx.rows.size())));
        return;
    }
    if (m_points.isEmpty()) {
        QRectF dummy;
        paintEmptyState(p, area.adjusted(0, 40, 0, 0),
                        m_ctx.schema.prop(view.chartX) ? tr("Nothing to chart yet.") : tr("Choose what the chart shows in the view settings."), QString(),
                        &dummy);
        return;
    }
    const int n = int(m_series.size());
    // Legend (several series).
    if (n > 1 && view.chartLegend) {
        p.setFont(Theme::uiFont(12));
        double lx = area.left() + 12;
        for (int k = 0; k < n; ++k) {
            const double w = QFontMetricsF(p.font()).horizontalAdvance(m_series[k]);
            if (lx + w + 20 > area.right())
                break;
            p.setPen(Qt::NoPen);
            p.setBrush(colorFor(k, n, 0, 0));
            p.drawRoundedRect(QRectF(lx, area.top() + 6, 10, 10), 2, 2);
            p.setPen(c.haze);
            p.drawText(QRectF(lx + 16, area.top(), w + 4, 22), Qt::AlignVCenter, m_series[k]);
            lx += w + 34;
        }
        area.setTop(area.top() + 30);
    }
    if (type == QLatin1String("donut")) {
        paintDonut(p, area);
        return;
    }
    paintAxes(p, area);
}

void ChartView::paintDonut(QPainter &p, const QRectF &area)
{
    const auto &c = Theme::colors();
    const View &view = m_ctx.view;
    QList<double> totals;
    double sum = 0;
    for (const Point &pt : m_points) {
        double t = 0;
        for (double v : pt.values)
            t += v;
        totals.append(t);
        sum += t;
    }
    const int count = int(m_points.size());
    const bool legend = view.chartLegend;
    const double R = qMin(area.height() - 30, (legend ? area.width() * 0.45 : area.width()) - 20) / 2;
    const QPointF center(legend ? area.left() + qMin(area.width() * 0.3, R + 50) : area.center().x(), area.center().y());
    const QRectF box(center.x() - R, center.y() - R, 2 * R, 2 * R);
    double angle = 90;
    for (int i = 0; i < count; ++i) {
        const double span = sum > 0 ? -totals[i] / sum * 360 : 0;
        QPainterPath slice;
        slice.moveTo(center);
        slice.arcTo(box, angle, span);
        slice.closeSubpath();
        QColor col = colorFor(0, 1, i, count);
        if (view.chartColor == QLatin1String("auto") || !paletteColor(view.chartColor).isValid()) {
            // Grays from bright to dim around the ring.
            const QColor stops[] = {c.starlight, c.mist, c.fog, c.dust, c.smoke, c.haze, c.moon};
            if (view.chartColor != QLatin1String("colorful"))
                col = stops[i % 7];
        } else {
            col = Theme::mix(paletteColor(view.chartColor), c.basalt, count > 1 ? 0.7 * i / count : 0);
        }
        if (i == m_hover)
            col = col.lighter(118);
        p.setPen(QPen(c.basalt, 2));
        p.setBrush(col);
        p.drawPath(slice);
        // Data labels: the share, on slices big enough to hold it.
        if (view.chartLabels && sum > 0 && totals[i] / sum >= 0.06) {
            const double mid = qDegreesToRadians(angle + span / 2);
            const QPointF at(center.x() + std::cos(mid) * R * 0.8, center.y() - std::sin(mid) * R * 0.8);
            p.setFont(Theme::uiFont(11, QFont::DemiBold));
            p.setPen(col.lightness() > 140 ? c.basalt : c.starlight);
            p.drawText(QRectF(at.x() - 30, at.y() - 9, 60, 18), Qt::AlignCenter, QStringLiteral("%1%").arg(qRound(totals[i] / sum * 100)));
        }
        m_points[i].hit = QRectF(); // slices are found by angle
        angle += span;
    }
    p.setPen(Qt::NoPen);
    p.setBrush(c.basalt);
    p.drawEllipse(center, R * 0.62, R * 0.62);
    p.setFont(Theme::displayFont(qMax(16.0, R * 0.22), QFont::DemiBold));
    p.setPen(c.starlight);
    p.drawText(QRectF(center.x() - R, center.y() - R * 0.22, 2 * R, R * 0.3), Qt::AlignCenter, formatValue(sum));
    p.setFont(Theme::uiFont(11.5));
    p.setPen(c.fog);
    p.drawText(QRectF(center.x() - R * 0.6, center.y() + R * 0.1, R * 1.2, 18), Qt::AlignCenter,
               QFontMetricsF(p.font()).elidedText(m_yLabel, Qt::ElideRight, R * 1.2));
    m_donutCenter = center;
    m_donutRadius = R;
    if (!legend)
        return;
    double y = qMax(area.top(), center.y() - count * 13.0);
    p.setFont(Theme::uiFont(12.5));
    const double x0 = center.x() + R + 40;
    for (int i = 0; i < count && y < area.bottom() - 20; ++i) {
        m_points[i].hit = QRectF(x0 - 6, y - 2, area.right() - x0, 24);
        if (i == m_hover)
            Theme::paintHover(p, m_points[i].hit, 1.0, 5);
        QColor col = colorFor(0, 1, i, count);
        if (view.chartColor == QLatin1String("auto") || !paletteColor(view.chartColor).isValid()) {
            const QColor stops[] = {c.starlight, c.mist, c.fog, c.dust, c.smoke, c.haze, c.moon};
            if (view.chartColor != QLatin1String("colorful"))
                col = stops[i % 7];
        } else {
            col = Theme::mix(paletteColor(view.chartColor), c.basalt, count > 1 ? 0.7 * i / count : 0);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(col);
        p.drawEllipse(QRectF(x0, y + 6, 9, 9));
        p.setPen(c.moon);
        p.drawText(QRectF(x0 + 18, y, area.right() - x0 - 130, 20), Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(m_points[i].label, Qt::ElideRight, area.right() - x0 - 130));
        p.setPen(c.fog);
        const QString val = QStringLiteral("%1  ·  %2%").arg(formatValue(totals[i])).arg(sum > 0 ? qRound(totals[i] / sum * 100) : 0);
        p.drawText(QRectF(x0 + 18, y, area.right() - x0 - 30, 20), Qt::AlignVCenter | Qt::AlignRight, val);
        y += 26;
    }
}

void ChartView::paintAxes(QPainter &p, const QRectF &area)
{
    const auto &c = Theme::colors();
    const View &view = m_ctx.view;
    const QString type = view.chartType;
    const bool horizontal = type == QLatin1String("hbar");
    const bool line = type == QLatin1String("line");
    const bool stacked = !line;
    const int n = int(m_series.size());
    const int count = int(m_points.size());
    const QFont tickFont = Theme::uiFont(11);
    const QFont labelFont = Theme::uiFont(11.5);
    const QFontMetricsF tickFm(tickFont), labelFm(labelFont);
    const bool xName = view.chartAxisNames == QLatin1String("x") || view.chartAxisNames == QLatin1String("both");
    const bool yName = view.chartAxisNames == QLatin1String("y") || view.chartAxisNames == QLatin1String("both");
    // Horizontal bars: categories down the left, values along the bottom.
    // The "X axis" of the settings is whichever runs along the bottom.
    const QString bottomName = horizontal ? m_yLabel : categoryName();
    const QString leftName = horizontal ? categoryName() : m_yLabel;

    // The value range.
    double minV = 0, maxV = 0;
    for (const Point &pt : m_points) {
        double t = 0;
        for (double v : pt.values) {
            t += v;
            minV = qMin(minV, v);
            maxV = qMax(maxV, v);
        }
        if (stacked)
            maxV = qMax(maxV, t);
    }
    for (const View::RefLine &l : view.chartRefLines) {
        minV = qMin(minV, l.value);
        maxV = qMax(maxV, l.value);
    }

    // Room around the plot.
    QRectF plot = area;
    plot.setLeft(area.left() + 8 + (yName ? 22 : 0));
    plot.setBottom(area.bottom() - 22 - (xName ? 22 : 0));
    plot.setRight(area.right() - 12);
    plot.setTop(area.top() + 6);
    // Reference line labels: above the plot, written up the line (bars) or
    // at the line's end (columns and lines).
    double refLabelW = 0;
    for (const View::RefLine &l : view.chartRefLines)
        refLabelW = qMax(refLabelW, tickFm.horizontalAdvance(l.label));
    const double refLabelMax = 120;
    if (horizontal && refLabelW > 0)
        plot.setTop(plot.top() + qMin(refLabelW, refLabelMax) + 8);
    if (!horizontal)
        plot.setTop(plot.top() + 16); // value labels over the tallest column
    double catLabelW = 0;
    if (horizontal) {
        for (const Point &pt : m_points)
            catLabelW = qMax(catLabelW, labelFm.horizontalAdvance(pt.label));
        catLabelW = qMin(catLabelW + 12, area.width() * 0.3);
        plot.setLeft(plot.left() + catLabelW);
        // Room for the value at the end of the longest bar.
        if (view.chartLabels)
            plot.setRight(plot.right() - qMax(28.0, tickFm.horizontalAdvance(formatValue(maxV)) + 10));
    }

    double lo, hi;
    const double length = horizontal ? plot.width() : plot.height();
    const int ticks = qMax(2, int(length / (horizontal ? 44 : 40)));
    double step;
    if (view.chartAutoRange) {
        lo = minV < 0 ? minV : 0;
        hi = maxV > lo ? maxV : lo + 1;
        step = niceStep(hi - lo, ticks);
        lo = std::floor(lo / step) * step;
        hi = std::max(lo + step, std::ceil(hi / step - 1e-9) * step);
    } else {
        lo = view.chartMin;
        hi = view.chartMax > lo ? view.chartMax : lo + 1;
        step = niceStep(hi - lo, ticks);
    }
    if (!horizontal) {
        // The tick labels' width decides the left margin.
        double w = 0;
        for (double v = std::ceil(lo / step) * step; v <= hi + 1e-9; v += step)
            w = qMax(w, tickFm.horizontalAdvance(formatTick(v, step)));
        plot.setLeft(plot.left() + w + 10);
    }
    auto toPos = [&](double v) {
        const double t = (v - lo) / (hi - lo);
        return horizontal ? plot.left() + t * plot.width() : plot.bottom() - t * plot.height();
    };

    // Grid lines and value ticks.
    const QString grid = view.chartGrid;
    const bool valueGrid = grid == QLatin1String("auto") || grid == QLatin1String("both") || (horizontal ? grid == QLatin1String("vertical") : grid == QLatin1String("horizontal"));
    const bool categoryGrid = grid == QLatin1String("both") || (horizontal ? grid == QLatin1String("horizontal") : grid == QLatin1String("vertical"));
    p.setFont(tickFont);
    const int every = horizontal ? qMax(1, int(std::ceil(tickFm.horizontalAdvance(formatTick(hi, step)) + 12) / (plot.width() / ((hi - lo) / step)))) : 1;
    int tickIndex = 0;
    for (double v = std::ceil(lo / step - 1e-9) * step; v <= hi + 1e-9; v += step, ++tickIndex) {
        const double pos = toPos(v);
        const bool zero = std::abs(v) < step * 1e-6;
        if (valueGrid || zero) {
            p.setPen(QPen(zero ? c.ash : c.hairline, 1));
            if (horizontal)
                p.drawLine(QPointF(pos, plot.top()), QPointF(pos, plot.bottom()));
            else
                p.drawLine(QPointF(plot.left(), pos), QPointF(plot.right(), pos));
        }
        if (tickIndex % every)
            continue;
        p.setPen(c.fog);
        if (horizontal)
            p.drawText(QRectF(pos - 40, plot.bottom() + 6, 80, 16), Qt::AlignHCenter | Qt::AlignTop, formatTick(v, step));
        else
            p.drawText(QRectF(area.left(), pos - 8, plot.left() - area.left() - 8, 16), Qt::AlignVCenter | Qt::AlignRight, formatTick(v, step));
    }
    const double band = (horizontal ? plot.height() : plot.width()) / count;
    if (categoryGrid) {
        p.setPen(QPen(c.hairline, 1));
        for (int i = 0; i <= count; ++i) {
            const double pos = (horizontal ? plot.top() : plot.left()) + band * i;
            if (horizontal)
                p.drawLine(QPointF(plot.left(), pos), QPointF(plot.right(), pos));
            else
                p.drawLine(QPointF(pos, plot.top()), QPointF(pos, plot.bottom()));
        }
    }

    // The data, clipped to the plot (a custom range can cut bars short).
    QList<QPair<QPointF, QString>> lineLabels; // drawn after, outside the clip
    p.save();
    p.setClipRect(plot.adjusted(-1, -1, horizontal ? 1 : 1, 1));
    if (line) {
        for (int k = 0; k < n; ++k) {
            QList<QPointF> pts;
            for (int i = 0; i < count; ++i)
                pts.append(QPointF(plot.left() + band * (i + 0.5), toPos(m_points[i].values.value(k))));
            QPainterPath path;
            path.moveTo(pts.first());
            for (int i = 1; i < pts.size(); ++i) {
                if (view.chartSmooth) {
                    // Catmull-Rom through the points, as cubic curves.
                    const QPointF p0 = pts[qMax(0, i - 2)], p1 = pts[i - 1], p2 = pts[i], p3 = pts[qMin(int(pts.size()) - 1, i + 1)];
                    path.cubicTo(p1 + (p2 - p0) / 6, p2 - (p3 - p1) / 6, p2);
                } else {
                    path.lineTo(pts[i]);
                }
            }
            const QColor col = colorFor(k, n, -1, 0);
            if (view.chartGradient && n <= 3) {
                QPainterPath fill = path;
                fill.lineTo(pts.last().x(), toPos(qMax(lo, 0.0)));
                fill.lineTo(pts.first().x(), toPos(qMax(lo, 0.0)));
                fill.closeSubpath();
                QLinearGradient g(QPointF(0, plot.top()), QPointF(0, plot.bottom()));
                QColor a = col, b = col;
                a.setAlpha(n == 1 ? 70 : 40);
                b.setAlpha(0);
                g.setColorAt(0, a);
                g.setColorAt(1, b);
                p.setPen(Qt::NoPen);
                p.setBrush(g);
                p.drawPath(fill);
            }
            p.setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            for (int i = 0; i < count; ++i) {
                const bool hot = i == m_hover;
                if (view.chartDots || hot) {
                    p.setPen(QPen(c.basalt, 2));
                    p.setBrush(col);
                    p.drawEllipse(pts[i], hot ? 5 : 3.5, hot ? 5 : 3.5);
                }
                if (view.chartLabels && n == 1 && count <= 30)
                    lineLabels.append({pts[i], formatValue(m_points[i].values.value(k))});
            }
        }
        for (int i = 0; i < count; ++i)
            m_points[i].hit = QRectF(plot.left() + band * i, plot.top(), band, plot.height());
    } else {
        const double barW = horizontal ? qBound(4.0, band * 0.4, 12.0) : qBound(4.0, band * 0.6, 56.0);
        for (int i = 0; i < count; ++i) {
            const Point &pt = m_points[i];
            double acc = 0;
            for (int k = 0; k < pt.values.size(); ++k) {
                const double v = pt.values[k];
                if (v == 0)
                    continue;
                QColor col = colorFor(k, n, i, count);
                if (i == m_hover)
                    col = col.lighter(118);
                const double from = toPos(acc), to = toPos(acc + v);
                QRectF r;
                if (horizontal) {
                    const double y = plot.top() + band * i + (band - barW) / 2;
                    r = QRectF(QPointF(qMin(from, to), y), QPointF(qMax(from, to), y + barW));
                } else {
                    const double x = plot.left() + band * i + (band - barW) / 2;
                    r = QRectF(QPointF(x, qMin(from, to)), QPointF(x + barW, qMax(from, to)));
                }
                p.setPen(Qt::NoPen);
                p.setBrush(col);
                p.drawRoundedRect(r, qMin(3.0, barW / 3), qMin(3.0, barW / 3));
                acc += v;
            }
            m_points[i].hit = horizontal ? QRectF(area.left(), plot.top() + band * i, plot.right() - area.left(), band)
                                         : QRectF(plot.left() + band * i, plot.top(), band, plot.height());
        }
    }
    p.restore();
    p.setFont(Theme::uiFont(11, QFont::Medium));
    p.setPen(c.haze);
    for (const auto &[at, text] : lineLabels)
        p.drawText(QRectF(at.x() - 40, at.y() - 22, 80, 16), Qt::AlignCenter, text);

    // Values at the end of each bar or column.
    if (!line && view.chartLabels) {
        p.setFont(Theme::uiFont(11, QFont::Medium));
        for (int i = 0; i < count; ++i) {
            double acc = 0;
            for (double v : m_points[i].values)
                acc += v;
            if (acc == 0 && view.chartHideEmpty)
                continue;
            p.setPen(i == m_hover ? c.moon : c.haze);
            const double end = toPos(qBound(lo, acc, hi));
            if (horizontal)
                p.drawText(QRectF(end + 6, plot.top() + band * i, 80, band), Qt::AlignVCenter | Qt::AlignLeft, formatValue(acc));
            else
                p.drawText(QRectF(plot.left() + band * i - 10, end - 18, band + 20, 16), Qt::AlignCenter, formatValue(acc));
        }
    }

    // Reference lines, dashed, with their labels.
    for (const View::RefLine &l : view.chartRefLines) {
        if (l.value < lo || l.value > hi)
            continue;
        const double pos = toPos(l.value);
        p.setPen(dashed(Theme::withAlpha(c.mist, 0.7)));
        if (horizontal)
            p.drawLine(QPointF(pos, plot.top() - 2), QPointF(pos, plot.bottom()));
        else
            p.drawLine(QPointF(plot.left(), pos), QPointF(plot.right(), pos));
        if (l.label.isEmpty())
            continue;
        p.setFont(tickFont);
        p.setPen(c.haze);
        if (horizontal) {
            // Written up the line, above the plot.
            p.save();
            p.translate(pos + 4, plot.top() - 6);
            p.rotate(-90);
            p.drawText(QRectF(0, -tickFm.ascent(), refLabelMax, tickFm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                       tickFm.elidedText(l.label, Qt::ElideRight, refLabelMax));
            p.restore();
        } else {
            const double w = tickFm.horizontalAdvance(l.label);
            p.drawText(QRectF(plot.right() - w - 4, pos - 17, w + 4, 15), Qt::AlignRight | Qt::AlignVCenter, l.label);
        }
    }

    // Category labels.
    p.setFont(labelFont);
    for (int i = 0; i < count; ++i) {
        p.setPen(i == m_hover ? c.moon : c.fog);
        if (horizontal) {
            p.drawText(QRectF(plot.left() - catLabelW - 2, plot.top() + band * i, catLabelW - 8, band), Qt::AlignVCenter | Qt::AlignRight,
                       labelFm.elidedText(m_points[i].label, Qt::ElideRight, catLabelW - 8));
        } else {
            // Skip labels when they would overlap.
            const int skip = qMax(1, int(std::ceil(60.0 / band)));
            if (i % skip)
                continue;
            p.drawText(QRectF(plot.left() + band * i - 10, plot.bottom() + 6, band * skip + 20, 18), Qt::AlignHCenter | Qt::AlignTop,
                       labelFm.elidedText(m_points[i].label, Qt::ElideRight, band * skip + 16));
        }
    }

    // Axis names.
    p.setFont(Theme::uiFont(11.5, QFont::Medium));
    p.setPen(c.fog);
    if (xName && !bottomName.isEmpty())
        p.drawText(QRectF(plot.left(), area.bottom() - 20, plot.width(), 18), Qt::AlignCenter, bottomName);
    if (yName && !leftName.isEmpty()) {
        p.save();
        p.translate(area.left() + 12, plot.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-plot.height() / 2, -9, plot.height(), 18), Qt::AlignCenter, leftName);
        p.restore();
    }
}

int ChartView::pointAt(const QPointF &pos) const
{
    if (m_ctx.view.chartType == QLatin1String("donut") && m_donutRadius > 0) {
        const QPointF d = pos - m_donutCenter;
        const double r = std::hypot(d.x(), d.y());
        if (r >= m_donutRadius * 0.62 && r <= m_donutRadius) {
            double sum = 0;
            QList<double> totals;
            for (const Point &pt : m_points) {
                double t = 0;
                for (double v : pt.values)
                    t += v;
                totals.append(t);
                sum += t;
            }
            // Angle from 12 o'clock, clockwise.
            double a = qRadiansToDegrees(std::atan2(d.x(), -d.y()));
            if (a < 0)
                a += 360;
            double acc = 0;
            for (int i = 0; i < totals.size() && sum > 0; ++i) {
                acc += totals[i] / sum * 360;
                if (a <= acc)
                    return i;
            }
        }
    }
    for (int i = 0; i < m_points.size(); ++i)
        if (m_points[i].hit.contains(pos))
            return i;
    return -1;
}

void ChartView::mouseMoveEvent(QMouseEvent *e)
{
    const int hover = pointAt(e->position());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    if (hover >= 0) {
        const Point &pt = m_points[hover];
        QStringList lines{QStringLiteral("<b>%1</b>").arg(pt.label.toHtmlEscaped())};
        for (int k = 0; k < pt.values.size(); ++k)
            lines.append(QStringLiteral("%1: %2").arg(m_series.value(k).toHtmlEscaped(), formatValue(pt.values[k])));
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
    // The pages behind a bar or slice: one opens, several are listed.
    const Point &pt = m_points[m_hover];
    QStringList ids;
    for (const QStringList &list : pt.rows)
        for (const QString &id : list)
            if (!ids.contains(id))
                ids.append(id);
    if (ids.size() == 1) {
        emit openRow(ids.first());
        return;
    }
    QMenu *menu = Theme::makeMenu(this);
    Theme::addMenuSection(menu, pt.label);
    Workspace *ws = Workspace::instance();
    for (int i = 0; i < ids.size() && i < 30; ++i) {
        const QString id = ids[i];
        const PageMeta *m = ws->page(id);
        menu->addAction(Icons::icon(QStringLiteral("page")), m && !m->title.isEmpty() ? m->title : tr("Untitled"), this, [this, id] { emit openRow(id); });
    }
    if (ids.isEmpty())
        menu->addAction(tr("No pages"))->setEnabled(false);
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
