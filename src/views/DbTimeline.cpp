// The timeline layout: rows as bars over time beside an optional table.
// Bars move by dragging and resize from their ends; "Blocked by"
// dependencies draw as arrows.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "PropertyEditors.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>

#include <algorithm>

using namespace DbViewsPrivate;

namespace
{
constexpr double kControlsH = 38;
constexpr double kAxisH = 44;
constexpr double kTop = kControlsH + kAxisH;
constexpr double kLineH = 36;
constexpr double kGroupH = 40;
constexpr double kTitleW = 240;
constexpr double kPropW = 140;

const char *const kScales[] = {"day", "week", "biweek", "month", "quarter", "year"};
const char *const kScaleNames[] = {QT_TRANSLATE_NOOP("DbViews", "Day"),   QT_TRANSLATE_NOOP("DbViews", "Week"),
                                   QT_TRANSLATE_NOOP("DbViews", "2 weeks"), QT_TRANSLATE_NOOP("DbViews", "Month"),
                                   QT_TRANSLATE_NOOP("DbViews", "Quarter"), QT_TRANSLATE_NOOP("DbViews", "Year")};
} // namespace

TimelineView::TimelineView(QWidget *parent)
    : DbView(parent)
{
}

void TimelineView::setContext(const ViewContext &ctx)
{
    const bool first = m_origin.isNull();
    DbView::setContext(ctx);
    layoutTimeline();
    update();
    if (first)
        QMetaObject::invokeMethod(this, &TimelineView::scrollToToday, Qt::QueuedConnection);
}

double TimelineView::dayWidth() const
{
    const QString s = m_ctx.view.timelineScale;
    if (s == QLatin1String("day"))
        return 120;
    if (s == QLatin1String("week"))
        return 64;
    if (s == QLatin1String("biweek"))
        return 36;
    if (s == QLatin1String("quarter"))
        return 8;
    if (s == QLatin1String("year"))
        return 2.6;
    return 24; // month
}

double TimelineView::tableWidth() const
{
    if (!m_ctx.view.timelineTable)
        return 0;
    int n = 0;
    for (const QString &id : m_ctx.view.timelineTableProps)
        if (m_ctx.schema.prop(id))
            ++n;
    return kTitleW + n * kPropW;
}

double TimelineView::xForDate(const QDate &d) const
{
    return tableWidth() + m_origin.daysTo(d) * dayWidth();
}

QDate TimelineView::dateForX(double x) const
{
    return m_origin.addDays(qint64(std::floor((x - tableWidth()) / dayWidth())));
}

int TimelineView::contentWidth(int available) const
{
    return qMax(available, int(tableWidth() + m_days * dayWidth()));
}

QPair<QDate, QDate> TimelineView::datesOf(const RowRef &r) const
{
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!dp)
        return {};
    const QJsonValue v = rowValue(*dp, r);
    const QDate start = dateOf(v);
    if (!start.isValid())
        return {};
    QDate end = dateEndOf(v).date();
    if (const Property *ep = m_ctx.schema.prop(m_ctx.view.endDateProp); ep && ep != dp) {
        const QDate e = dateOf(rowValue(*ep, r));
        if (e.isValid())
            end = e;
    }
    if (!end.isValid() || end < start)
        end = start;
    return {start, end};
}

void TimelineView::layoutTimeline()
{
    m_bars.clear();
    m_lines.clear();
    m_groups = groups();
    // The span drawn: around today and every item, padded.
    const QDate today = QDate::currentDate();
    QDate lo = today.addDays(-60), hi = today.addDays(150);
    for (const RowRef &r : m_ctx.rows) {
        const auto d = datesOf(r);
        if (!d.first.isValid())
            continue;
        lo = qMin(lo, d.first.addDays(-21));
        hi = qMax(hi, d.second.addDays(45));
    }
    const QString scale = m_ctx.view.timelineScale;
    if (scale == QLatin1String("year") || scale == QLatin1String("quarter")) {
        lo = QDate(lo.year(), 1, 1);
        hi = QDate(hi.year(), 12, 31);
    } else {
        lo = QDate(lo.year(), lo.month(), 1);
    }
    m_origin = lo;
    m_days = int(lo.daysTo(hi)) + 1;

    double y = kTop;
    QList<int> unscheduled;
    auto addRows = [&](const QList<int> &indexes, const QString &group, int gi) {
        for (int i : indexes) {
            const auto d = datesOf(m_ctx.rows[i]);
            if (!d.first.isValid()) {
                if (!unscheduled.contains(i))
                    unscheduled.append(i);
                continue;
            }
            Line l{Line::Row, y, i, group, gi};
            m_lines.append(l);
            const double x0 = xForDate(d.first), x1 = xForDate(d.second.addDays(1));
            m_bars.append({i, int(m_lines.size()) - 1, d.first, d.second, QRectF(x0, y + 6, qMax(x1 - x0, 10.0), kLineH - 12)});
            y += kLineH;
        }
    };
    if (m_groups.isEmpty()) {
        QList<int> all;
        for (int i = 0; i < m_ctx.rows.size(); ++i)
            all.append(i);
        addRows(all, QString(), -1);
    } else {
        for (int gi = 0; gi < m_groups.size(); ++gi) {
            Line gl{Line::Group, y + 4};
            gl.group = m_groups[gi].key;
            gl.groupIndex = gi;
            m_lines.append(gl);
            y += kGroupH + 4;
            if (!groupCollapsed(m_groups[gi].key))
                addRows(m_groups[gi].rows, m_groups[gi].key, gi);
            else
                for (int r : m_groups[gi].rows)
                    if (!datesOf(m_ctx.rows[r]).first.isValid() && !unscheduled.contains(r))
                        unscheduled.append(r);
        }
    }
    m_addY = y;
    y += kLineH;
    if (!unscheduled.isEmpty()) {
        Line header{Line::Unscheduled, y + 10};
        m_lines.append(header);
        y += kGroupH + 10;
        for (int i : unscheduled) {
            Line l{Line::Unscheduled, y, i};
            m_lines.append(l);
            y += kLineH;
        }
    }
    setFixedHeight(int(y + 16));
}

void TimelineView::scrollToToday()
{
    for (QWidget *w = parentWidget(); w; w = w->parentWidget())
        if (auto *area = qobject_cast<QScrollArea *>(w)) {
            // Today a third of the way into the visible part of the timeline.
            const double left = x() + tableWidth();
            const double desired = left + (area->viewport()->width() - left) * 0.3;
            area->horizontalScrollBar()->setValue(int(qMax(0.0, x() + xForDate(QDate::currentDate()) - desired)));
            return;
        }
}

void TimelineView::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect clip = e->rect();
    const QLocale en(QLocale::English);
    const double dw = dayWidth();
    const double tw = tableWidth();
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    const QString scale = m_ctx.view.timelineScale;

    // Nothing shows left of the frozen table while scrolled sideways.
    p.setClipRect(QRectF(m_scrollX, 0, width(), height()));
    if (!dp) {
        paintEmptyState(p, QRectF(m_scrollX, 20, qMin(width(), 900), 200), tr("A timeline places items on a Date property."),
                        m_ctx.editable ? tr("Add a Date property") : QString(), &m_todayRect);
        return;
    }

    // Day columns: weekends shaded, month boundaries ruled.
    const QDate firstVisible = dateForX(qMax(double(clip.left()), tw + m_scrollX)).addDays(-1);
    const QDate lastVisible = dateForX(clip.right()).addDays(1);
    for (QDate d = firstVisible; d <= lastVisible; d = d.addDays(1)) {
        const double x = xForDate(d);
        if (x + dw < tw + m_scrollX)
            continue;
        if (d.dayOfWeek() > 5 && dw >= 8 && m_ctx.view.showWeekends)
            p.fillRect(QRectF(x, kTop, dw, height() - kTop), QColor(0, 0, 0, 34));
        const bool monthStart = d.day() == 1;
        const bool weekStart = d.dayOfWeek() == 1;
        if (monthStart || (weekStart && dw >= 8) || dw >= 36) {
            p.setPen(QPen(monthStart ? c.ash : c.hairline, 1));
            p.drawLine(QPointF(x - 0.5, kControlsH + (monthStart ? 0 : kAxisH / 2)), QPointF(x - 0.5, height()));
        }
    }
    // Row separators and group headers.
    for (int li = 0; li < m_lines.size(); ++li) {
        const Line &line = m_lines[li];
        if (line.y > clip.bottom() + kGroupH || line.y + kGroupH < clip.top())
            continue;
        if (line.kind == Line::Group) {
            const RowGroup &g = m_groups[line.groupIndex];
            p.fillRect(QRectF(m_scrollX, line.y, width(), kGroupH), c.basalt);
            paintGroupHeader(p, QRectF(m_scrollX + 8, line.y, 520, kGroupH), g, groupProp(), groupCollapsed(g.key), li == m_hoverLine);
            continue;
        }
        if (line.kind == Line::Unscheduled && line.row < 0) {
            p.fillRect(QRectF(m_scrollX, line.y, width(), kGroupH), c.basalt);
            p.setFont(Theme::uiFont(13.5, QFont::DemiBold));
            p.setPen(c.haze);
            int n = 0;
            for (const Line &l : m_lines)
                if (l.kind == Line::Unscheduled && l.row >= 0)
                    ++n;
            p.drawText(QRectF(m_scrollX + 12, line.y, 400, kGroupH), Qt::AlignVCenter, tr("No date  %1").arg(n));
            continue;
        }
        if (li == m_hoverLine && !m_dragging)
            p.fillRect(QRectF(0, line.y, width(), kLineH), QColor(255, 255, 255, 6));
        p.setPen(QPen(c.hairline, 1));
        p.drawLine(QPointF(0, line.y + kLineH - 0.5), QPointF(width(), line.y + kLineH - 0.5));
        if (line.kind == Line::Unscheduled && li == m_hoverLine && !propTypeIsComputed(dp->type)) {
            // Unscheduled rows offer a button to place them today.
            const double x = qMax(tw + m_scrollX + 12, xForDate(QDate::currentDate()) - 40);
            const QRectF btn(x, line.y + 7, 92, kLineH - 14);
            p.setPen(QPen(c.ash, 1));
            p.setBrush(c.graphite);
            p.drawRoundedRect(btn, 6, 6);
            p.setFont(Theme::uiFont(12, QFont::Medium));
            p.setPen(c.moon);
            p.drawText(btn, Qt::AlignCenter, tr("Schedule"));
        }
    }
    // Today.
    const double todayX = xForDate(QDate::currentDate()) + dw / 2;
    if (todayX > tw + m_scrollX) {
        p.setPen(QPen(c.starlight, 1.4));
        p.drawLine(QPointF(todayX, kControlsH + kAxisH - 6), QPointF(todayX, height()));
        p.setPen(Qt::NoPen);
        p.setBrush(c.starlight);
        p.drawEllipse(QPointF(todayX, kControlsH + kAxisH - 6), 3.5, 3.5);
    }
    // Dependencies: an elbow arrow from the blocking bar to the blocked one.
    if (const Property *dep = m_ctx.schema.prop(m_ctx.schema.dependencyProp)) {
        QHash<QString, int> barOf;
        for (int i = 0; i < m_bars.size(); ++i)
            if (m_ctx.rows[m_bars[i].row].meta)
                barOf.insert(m_ctx.rows[m_bars[i].row].meta->id, i);
        for (int i = 0; i < m_bars.size(); ++i) {
            const RowRef &r = m_ctx.rows[m_bars[i].row];
            for (const QString &blocker : relationIds(r.props.value(dep->id))) {
                const int bi = barOf.value(blocker, -1);
                if (bi < 0)
                    continue;
                const QRectF a = m_bars[bi].rect, b = m_bars[i].rect;
                const bool conflict = m_bars[bi].end >= m_bars[i].start;
                // An elbow route with rounded corners.
                QList<QPointF> pts{QPointF(a.right(), a.center().y())};
                if (b.left() - 10 >= a.right() + 10) {
                    const double midX = b.left() - 12;
                    pts << QPointF(midX, a.center().y()) << QPointF(midX, b.center().y());
                } else {
                    // The blocked item starts before its blocker ends: route back
                    // between the rows, then into the start of the bar.
                    const double gapY = a.center().y() < b.center().y() ? b.top() - 5 : b.bottom() + 5;
                    pts << QPointF(a.right() + 12, a.center().y()) << QPointF(a.right() + 12, gapY) << QPointF(b.left() - 14, gapY)
                        << QPointF(b.left() - 14, b.center().y());
                }
                pts << QPointF(b.left() - 2, b.center().y());
                QPainterPath path(pts.first());
                for (int k = 1; k + 1 < pts.size(); ++k) {
                    const QPointF prev = pts[k - 1], at = pts[k], next = pts[k + 1];
                    const double r = std::min({6.0, QLineF(prev, at).length() / 2, QLineF(at, next).length() / 2});
                    const QPointF in = at + (prev - at) * (r / qMax(0.01, QLineF(prev, at).length()));
                    const QPointF out = at + (next - at) * (r / qMax(0.01, QLineF(at, next).length()));
                    path.lineTo(in);
                    path.quadTo(at, out);
                }
                path.lineTo(pts.last());
                QPen pen(conflict ? c.haze : c.dust, 1.3);
                if (conflict)
                    pen.setStyle(Qt::DashLine);
                p.setPen(pen);
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
                QPainterPath head;
                head.moveTo(b.left() - 1, b.center().y());
                head.lineTo(b.left() - 7, b.center().y() - 4);
                head.lineTo(b.left() - 7, b.center().y() + 4);
                head.closeSubpath();
                p.setPen(Qt::NoPen);
                p.setBrush(conflict ? c.haze : c.dust);
                p.drawPath(head);
            }
        }
    }
    // Bars.
    for (int i = 0; i < m_bars.size(); ++i) {
        Bar bar = m_bars[i];
        if (m_dragging && i == m_dragBar) {
            const double x0 = xForDate(m_dragNewStart), x1 = xForDate(m_dragNewEnd.addDays(1));
            bar.rect = QRectF(x0, bar.rect.top(), qMax(x1 - x0, 10.0), bar.rect.height());
        }
        if (bar.rect.right() < clip.left() || bar.rect.left() > clip.right())
            continue;
        const RowRef &r = m_ctx.rows[bar.row];
        const bool hover = i == m_hoverBar || (m_dragging && i == m_dragBar);
        if (m_dragging && i == m_dragBar)
            Theme::paintSoftShadow(p, bar.rect, 7, 10, 1.2);
        p.setPen(QPen(hover ? c.smoke : c.ash, 1));
        p.setBrush(hover ? QColor(0x2A, 0x2A, 0x31) : QColor(0x22, 0x22, 0x28));
        p.drawRoundedRect(bar.rect.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
        // Status-like progress: complete items get a brighter rim.
        double x = bar.rect.left() + 8;
        if (m_ctx.view.showPageIcon && r.meta && !r.meta->icon.isEmpty() && bar.rect.width() > 40) {
            Icons::paintPageIcon(p, QRectF(x, bar.rect.center().y() - 7, 14, 14), r.meta->icon, c.mist);
            x += 18;
        }
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        const QString title = titleOf(r);
        const double tw2 = QFontMetricsF(p.font()).horizontalAdvance(title);
        // Inside the bar when the whole title fits, else beside it.
        if (bar.rect.right() - x - 8 >= tw2) {
            p.setPen(c.starlight);
            p.drawText(QRectF(x, bar.rect.top(), bar.rect.right() - x - 8, bar.rect.height()), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(title, Qt::ElideRight, bar.rect.right() - x - 8));
        } else {
            p.setPen(c.moon);
            p.drawText(QRectF(bar.rect.right() + 8, bar.rect.top(), 300, bar.rect.height()), Qt::AlignVCenter, title);
        }
        if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.mist);
            p.drawRoundedRect(QRectF(bar.rect.left() + 2.5, bar.rect.center().y() - 6, 3, 12), 1.5, 1.5);
            p.drawRoundedRect(QRectF(bar.rect.right() - 5.5, bar.rect.center().y() - 6, 3, 12), 1.5, 1.5);
        }
        if (m_dragging && i == m_dragBar) {
            // Dates while dragging.
            p.setFont(Theme::uiFont(11.5, QFont::Medium));
            p.setPen(c.haze);
            const QString range = m_dragNewStart == m_dragNewEnd
                                      ? en.toString(m_dragNewStart, QStringLiteral("MMM d"))
                                      : en.toString(m_dragNewStart, QStringLiteral("MMM d")) + QStringLiteral(" → ") + en.toString(m_dragNewEnd, QStringLiteral("MMM d"));
            p.drawText(QRectF(bar.rect.left(), bar.rect.top() - 18, 240, 16), Qt::AlignVCenter, range);
        }
    }
    // The axis (frozen at the top while the page scrolls sideways).
    p.fillRect(QRectF(0, 0, width(), kTop), c.basalt);
    bool sticky = true; // the first visible period keeps its label at the left edge
    for (QDate d = firstVisible; d <= lastVisible; d = d.addDays(1)) {
        const double x = xForDate(d);
        if (x + dw < tw + m_scrollX)
            continue;
        const bool first = sticky;
        sticky = false;
        p.setFont(Theme::uiFont(11.5, QFont::Medium));
        // Upper band: months (years on the year scale).
        if (scale == QLatin1String("year")) {
            if (d.dayOfYear() == 1 || first) {
                p.setPen(c.moon);
                p.setFont(Theme::uiFont(12.5, QFont::DemiBold));
                const double lx = qMax(x, tw + m_scrollX) + 6;
                const double room = first ? xForDate(QDate(d.year() + 1, 1, 1)) - lx - 8 : 200;
                if (room > 30)
                    p.drawText(QRectF(lx, kControlsH, qMin(room, 200.0), kAxisH / 2), Qt::AlignVCenter, QString::number(d.year()));
            }
            if (d.day() == 1) {
                p.setFont(Theme::uiFont(11, QFont::Medium));
                p.setPen(c.fog);
                p.drawText(QRectF(x + 3, kControlsH + kAxisH / 2, 80, kAxisH / 2), Qt::AlignVCenter, en.monthName(d.month(), QLocale::ShortFormat));
            }
            continue;
        }
        if (d.day() == 1 || first) {
            p.setPen(c.moon);
            p.setFont(Theme::uiFont(12.5, QFont::DemiBold));
            const double lx = qMax(x, tw + m_scrollX) + 6;
            // The sticky label gives way to the next month's.
            const double room = first ? xForDate(QDate(d.year(), d.month(), 1).addMonths(1)) - lx - 8 : 240;
            if (room > 40)
                p.drawText(QRectF(lx, kControlsH, qMin(room, 240.0), kAxisH / 2), Qt::AlignVCenter,
                           QFontMetricsF(p.font()).elidedText(en.toString(d, QStringLiteral("MMMM yyyy")), Qt::ElideRight, qMin(room, 240.0)));
        }
        p.setFont(Theme::uiFont(11, QFont::Medium));
        const bool isToday = d == QDate::currentDate();
        p.setPen(isToday ? c.starlight : c.fog);
        if (scale == QLatin1String("quarter")) {
            if (d.dayOfWeek() == 1)
                p.drawText(QRectF(x + 2, kControlsH + kAxisH / 2, 7 * dw, kAxisH / 2), Qt::AlignVCenter, QString::number(d.day()));
        } else if (scale == QLatin1String("day") || scale == QLatin1String("week")) {
            p.drawText(QRectF(x, kControlsH + kAxisH / 2, dw, kAxisH / 2), Qt::AlignCenter,
                       en.toString(d, dw >= 100 ? QStringLiteral("ddd d") : QStringLiteral("d")));
        } else {
            p.drawText(QRectF(x, kControlsH + kAxisH / 2, dw, kAxisH / 2), Qt::AlignCenter, QString::number(d.day()));
        }
    }
    p.setPen(QPen(c.ash, 1));
    p.drawLine(QPointF(0, kTop - 0.5), QPointF(width(), kTop - 0.5));

    // The frozen table on the left.
    if (tw > 0) {
        const double left = m_scrollX;
        p.fillRect(QRectF(left, kControlsH, tw, height() - kControlsH), c.basalt);
        QList<const Property *> cols;
        for (const QString &id : m_ctx.view.timelineTableProps)
            if (const Property *pp = m_ctx.schema.prop(id))
                cols.append(pp);
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        p.setPen(c.haze);
        p.drawText(QRectF(left + 12, kControlsH + kAxisH / 2, kTitleW - 20, kAxisH / 2), Qt::AlignVCenter,
                   m_ctx.schema.titleProp() ? m_ctx.schema.titleProp()->name : tr("Name"));
        for (int k = 0; k < cols.size(); ++k)
            p.drawText(QRectF(left + kTitleW + k * kPropW + 8, kControlsH + kAxisH / 2, kPropW - 16, kAxisH / 2), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(cols[k]->name, Qt::ElideRight, kPropW - 16));
        for (int li = 0; li < m_lines.size(); ++li) {
            const Line &line = m_lines[li];
            if (line.row < 0 || line.y > clip.bottom() || line.y + kLineH < clip.top())
                continue;
            const RowRef &r = m_ctx.rows[line.row];
            if (li == m_hoverLine)
                p.fillRect(QRectF(left, line.y, tw, kLineH), QColor(255, 255, 255, 6));
            double x = left + 12;
            if (m_ctx.view.showPageIcon) {
                Icons::paintPageIcon(p, QRectF(x, line.y + kLineH / 2 - 8, 16, 16), r.meta ? r.meta->icon : QString(), c.mist, QStringLiteral("page"));
                x += 24;
            }
            p.setFont(Theme::uiFont(13.5, QFont::Medium));
            p.setPen(r.meta && !r.meta->title.trimmed().isEmpty() ? c.moon : c.haze);
            p.drawText(QRectF(x, line.y, left + kTitleW - x - 8, kLineH), Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(titleOf(r), Qt::ElideRight, left + kTitleW - x - 8));
            for (int k = 0; k < cols.size(); ++k)
                PropertyEditors::paintValue(p, QRectF(left + kTitleW + k * kPropW + 8, line.y, kPropW - 16, kLineH), *cols[k], rowValue(*cols[k], r));
            p.setPen(QPen(c.hairline, 1));
            p.drawLine(QPointF(left, line.y + kLineH - 0.5), QPointF(left + tw, line.y + kLineH - 0.5));
        }
        p.setPen(QPen(c.ash, 1));
        p.drawLine(QPointF(left + tw - 0.5, kControlsH), QPointF(left + tw - 0.5, height()));
    }

    // The "New" line.
    {
        const QRectF add(m_scrollX, m_addY, tw > 0 ? tw : 200, kLineH);
        if (m_hoverAdd)
            Theme::paintHover(p, add.adjusted(2, 2, -2, -2), 1.0, 6);
        Icons::paint(p, QRectF(add.left() + 12, add.center().y() - 7, 14, 14), QStringLiteral("plus"), m_hoverAdd ? c.moon : c.fog);
        p.setFont(Theme::uiFont(13.5));
        p.setPen(m_hoverAdd ? c.moon : c.fog);
        p.drawText(add.adjusted(34, 0, 0, 0), Qt::AlignVCenter, tr("New"));
    }
    // Controls: scale and today, frozen at the top left.
    p.fillRect(QRectF(m_scrollX, 0, width(), kControlsH), c.basalt);
    double x = m_scrollX + 4;
    p.setFont(Theme::uiFont(12.5, QFont::Medium));
    for (int i = 0; i < 6; ++i) {
        const QString name = tr(kScaleNames[i]);
        const double w = QFontMetricsF(p.font()).horizontalAdvance(name) + 20;
        m_scaleRects[i] = QRectF(x, 5, w, 28);
        const bool active = scale == QLatin1String(kScales[i]);
        if (active) {
            p.setPen(QPen(c.ash, 1));
            p.setBrush(c.graphite);
            p.drawRoundedRect(m_scaleRects[i], 6, 6);
        } else if (m_hoverScale == i) {
            Theme::paintHover(p, m_scaleRects[i], 1.0, 6);
        }
        p.setPen(active ? c.starlight : m_hoverScale == i ? c.moon : c.haze);
        p.drawText(m_scaleRects[i], Qt::AlignCenter, name);
        x += w + 2;
    }
    x += 10;
    m_todayRect = QRectF(x, 5, QFontMetricsF(p.font()).horizontalAdvance(tr("Today")) + 22, 28);
    p.setPen(QPen(c.ash, 1));
    p.setBrush(m_hoverScale == 6 ? c.smoke : Qt::transparent);
    p.drawRoundedRect(m_todayRect, 6, 6);
    p.setPen(c.moon);
    p.drawText(m_todayRect, Qt::AlignCenter, tr("Today"));
}

void TimelineView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    const double dw = dayWidth();
    if (m_dragBar >= 0 && (e->buttons() & Qt::LeftButton)) {
        if (!m_dragging && qAbs(pos.x() - m_dragStartX) > 3)
            m_dragging = true;
        if (m_dragging) {
            const int delta = int(std::lround((pos.x() - m_dragStartX) / dw));
            if (m_dragEdge < 0) {
                m_dragNewStart = qMin(m_dragOrigStart.addDays(delta), m_dragOrigEnd);
                m_dragNewEnd = m_dragOrigEnd;
            } else if (m_dragEdge > 0) {
                m_dragNewStart = m_dragOrigStart;
                m_dragNewEnd = qMax(m_dragOrigEnd.addDays(delta), m_dragOrigStart);
            } else {
                m_dragNewStart = m_dragOrigStart.addDays(delta);
                m_dragNewEnd = m_dragOrigEnd.addDays(delta);
            }
            update();
        }
        return;
    }
    int scaleHover = -1;
    for (int i = 0; i < 6; ++i)
        if (m_scaleRects[i].contains(pos))
            scaleHover = i;
    if (m_todayRect.contains(pos))
        scaleHover = 6;
    int bar = -1, edge = 0;
    for (int i = 0; i < m_bars.size(); ++i) {
        const QRectF r = m_bars[i].rect;
        if (pos.x() < tableWidth() + m_scrollX)
            break;
        if (r.adjusted(-4, 0, 4, 0).contains(pos)) {
            bar = i;
            edge = pos.x() < r.left() + 7 ? -1 : pos.x() > r.right() - 7 ? 1 : 0;
        }
    }
    int line = -1;
    if (pos.y() >= kTop)
        for (int i = 0; i < m_lines.size(); ++i) {
            const double h = (m_lines[i].kind == Line::Group || m_lines[i].row < 0) ? kGroupH : kLineH;
            if (pos.y() >= m_lines[i].y && pos.y() < m_lines[i].y + h)
                line = i;
        }
    const bool add = pos.y() >= m_addY && pos.y() < m_addY + kLineH;
    if (bar != m_hoverBar || edge != m_hoverEdge || line != m_hoverLine || scaleHover != m_hoverScale || add != m_hoverAdd) {
        m_hoverBar = bar;
        m_hoverEdge = edge;
        m_hoverLine = line;
        m_hoverScale = scaleHover;
        m_hoverAdd = add;
        update();
    }
    setCursor(bar >= 0 && edge != 0 ? Qt::SizeHorCursor
              : bar >= 0 || scaleHover >= 0 || add || line >= 0 ? Qt::PointingHandCursor
                                                                 : Qt::ArrowCursor);
}

void TimelineView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF pos = e->position();
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!dp) {
        if (m_todayRect.contains(pos) && m_ctx.editable) {
            const QString existing = m_ctx.schema.defaultDateProp();
            if (!existing.isEmpty())
                saveView([existing](View &v) { v.dateProp = existing; });
            else
                addPropertyOfType(m_ctx.databaseId, m_ctx.view.id, PropType::Date, false);
        }
        return;
    }
    if (m_hoverScale >= 0 && m_hoverScale < 6) {
        const QString s = QLatin1String(kScales[m_hoverScale]);
        saveView([s](View &v) { v.timelineScale = s; });
        QMetaObject::invokeMethod(this, &TimelineView::scrollToToday, Qt::QueuedConnection);
        return;
    }
    if (m_hoverScale == 6) {
        scrollToToday();
        return;
    }
    if (m_hoverAdd) {
        QDate day = QDate::currentDate();
        if (pos.x() > tableWidth() + m_scrollX)
            day = dateForX(pos.x());
        emit createRow({{dp->id, day.toString(Qt::ISODate)}}, true);
        return;
    }
    if (m_hoverBar >= 0) {
        const Bar &b = m_bars[m_hoverBar];
        m_dragBar = m_hoverBar;
        m_dragEdge = m_hoverEdge;
        m_dragStartX = pos.x();
        m_dragOrigStart = m_dragNewStart = b.start;
        m_dragOrigEnd = m_dragNewEnd = b.end;
        return;
    }
    if (m_hoverLine >= 0) {
        const Line &line = m_lines[m_hoverLine];
        if (line.kind == Line::Group) {
            toggleGroup(line.group);
            return;
        }
        if (line.row < 0 || !m_ctx.rows[line.row].meta)
            return;
        const QString id = m_ctx.rows[line.row].meta->id;
        if (line.kind == Line::Unscheduled && pos.x() > tableWidth() + m_scrollX && !propTypeIsComputed(dp->type)) {
            Workspace::instance()->setProp(id, dp->id, QDate::currentDate().toString(Qt::ISODate));
            return;
        }
        if (pos.x() < tableWidth() + m_scrollX)
            emit openRow(id);
    }
}

void TimelineView::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragBar < 0)
        return;
    const RowRef row = m_ctx.rows[m_bars[m_dragBar].row];
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!m_dragging) {
        if (row.meta)
            emit openRow(row.meta->id);
    } else if (row.meta && dp && !propTypeIsComputed(dp->type) && (m_dragNewStart != m_dragOrigStart || m_dragNewEnd != m_dragOrigEnd)) {
        Workspace *ws = Workspace::instance();
        const QString id = row.meta->id;
        const QJsonValue v = rowValue(*dp, row);
        const bool withTime = v.toString().section(QLatin1Char('/'), 0, 0).size() > 10;
        const QTime startTime = withTime ? dateTimeOf(v).time() : QTime(0, 0);
        const QTime endTime = withTime && dateEndOf(v).isValid() ? dateEndOf(v).time() : startTime;
        const Property *ep = m_ctx.schema.prop(m_ctx.view.endDateProp);
        ws->batch([&] {
            if (ep && ep != dp && !propTypeIsComputed(ep->type)) {
                ws->setProp(id, dp->id, makeDateValue(QDateTime(m_dragNewStart, startTime), withTime, {}, dateHasReminder(v)));
                ws->setProp(id, ep->id, m_dragNewEnd.toString(Qt::ISODate));
            } else {
                const QDateTime end = m_dragNewEnd != m_dragNewStart ? QDateTime(m_dragNewEnd, endTime) : QDateTime();
                ws->setProp(id, dp->id, makeDateValue(QDateTime(m_dragNewStart, startTime), withTime, end, dateHasReminder(v)));
            }
        });
    }
    m_dragging = false;
    m_dragBar = -1;
    update();
}

void TimelineView::leaveEvent(QEvent *)
{
    m_hoverBar = m_hoverLine = m_hoverScale = -1;
    m_hoverEdge = 0;
    m_hoverAdd = false;
    update();
}

void TimelineView::contextMenuEvent(QContextMenuEvent *e)
{
    for (const Bar &b : m_bars)
        if (b.rect.contains(e->pos()) && m_ctx.rows[b.row].meta) {
            rowMenu(m_ctx.rows[b.row].meta->id, e->globalPos());
            return;
        }
    for (const Line &l : m_lines)
        if (l.row >= 0 && e->pos().y() >= l.y && e->pos().y() < l.y + kLineH && m_ctx.rows[l.row].meta) {
            rowMenu(m_ctx.rows[l.row].meta->id, e->globalPos());
            return;
        }
}
