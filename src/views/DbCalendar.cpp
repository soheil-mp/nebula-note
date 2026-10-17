// The calendar layout: a month of days with items on a date property. Date
// ranges span days as bars; weekends can be hidden.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

using namespace DbViewsPrivate;

namespace
{
constexpr double kChipH = 23;
constexpr double kChipStep = 26;
constexpr double kChipTop = 30;
constexpr int kSlots = 3;

// The start and end day of a row on the calendar's date property(ies).
QPair<QDate, QDate> spanOf(const Schema &schema, const View &view, const Property &dp, const RowRef &row)
{
    const QJsonValue v = rowValue(dp, row);
    const QDate start = dateOf(v);
    if (!start.isValid())
        return {};
    QDate end = dateEndOf(v).date();
    if (const Property *ep = schema.prop(view.endDateProp); ep && ep != &dp) {
        const QDate e = dateOf(rowValue(*ep, row));
        if (e.isValid())
            end = e;
    }
    if (!end.isValid() || end < start)
        end = start;
    return {start, end};
}
} // namespace

CalendarView::CalendarView(QWidget *parent)
    : DbView(parent)
{
    const QDate t = QDate::currentDate();
    m_month = QDate(t.year(), t.month(), 1);
}

void CalendarView::setMonth(const QDate &firstOfMonth)
{
    m_month = QDate(firstOfMonth.year(), firstOfMonth.month(), 1);
    layoutMonth();
    update();
    emit monthChanged();
}

void CalendarView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    layoutMonth();
    update();
}

void CalendarView::resizeEvent(QResizeEvent *)
{
    layoutMonth();
}

QList<int> CalendarView::visibleDays() const
{
    return m_ctx.view.showWeekends ? QList<int>{1, 2, 3, 4, 5, 6, 7} : QList<int>{1, 2, 3, 4, 5};
}

QDate CalendarView::gridStart() const
{
    return m_month.addDays(-(m_month.dayOfWeek() - 1));
}

QRectF CalendarView::cellRect(int week, int dayColumn) const
{
    return QRectF(dayColumn * m_cellW, m_gridTop + week * m_cellH, m_cellW, m_cellH);
}

QDate CalendarView::dateAtPos(const QPointF &pos) const
{
    if (pos.y() < m_gridTop)
        return {};
    const QList<int> days = visibleDays();
    const int col = qBound(0, int(pos.x() / m_cellW), int(days.size()) - 1);
    const int week = int((pos.y() - m_gridTop) / m_cellH);
    if (week < 0 || week > 5)
        return {};
    return gridStart().addDays(week * 7 + days[col] - 1);
}

void CalendarView::layoutMonth()
{
    m_chips.clear();
    m_more.clear();
    const QList<int> days = visibleDays();
    m_cellW = qMax(60.0, width() / double(days.size()));
    m_cellH = 116;
    setFixedHeight(int(m_gridTop + 6 * m_cellH + 12));
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!dp)
        return;
    struct Item {
        int row;
        QDate start, end;
    };
    QList<Item> items;
    for (int i = 0; i < m_ctx.rows.size(); ++i) {
        const auto span = spanOf(m_ctx.schema, m_ctx.view, *dp, m_ctx.rows[i]);
        if (span.first.isValid())
            items.append({i, span.first, span.second});
    }
    // Longer items first so bars stack above single days, like Notion.
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.start.daysTo(a.end) > b.start.daysTo(b.end);
    });
    const QDate start = gridStart();
    for (int week = 0; week < 6; ++week) {
        const QDate ws = start.addDays(week * 7);
        const QDate we = ws.addDays(6);
        // Slot occupancy per visible column.
        QList<QList<bool>> used(days.size(), QList<bool>(kSlots, false));
        for (const Item &it : items) {
            if (it.end < ws || it.start > we)
                continue;
            // Visible columns the item covers this week.
            int first = -1, last = -1;
            for (int col = 0; col < days.size(); ++col) {
                const QDate d = ws.addDays(days[col] - 1);
                if (d >= it.start && d <= it.end) {
                    if (first < 0)
                        first = col;
                    last = col;
                }
            }
            if (first < 0)
                continue;
            int slot = -1;
            for (int s = 0; s < kSlots && slot < 0; ++s) {
                bool free = true;
                for (int col = first; col <= last; ++col)
                    if (used[col][s])
                        free = false;
                if (free)
                    slot = s;
            }
            if (slot < 0) {
                for (int col = first; col <= last; ++col)
                    m_more[week * 7 + col] = m_more.value(week * 7 + col) + 1;
                continue;
            }
            for (int col = first; col <= last; ++col)
                used[col][slot] = true;
            const QRectF a = cellRect(week, first), b = cellRect(week, last);
            Chip chip;
            chip.row = it.row;
            chip.rect = QRectF(a.left() + 5, a.top() + kChipTop + slot * kChipStep, b.right() - a.left() - 10, kChipH);
            chip.continuesLeft = it.start < ws.addDays(days[first] - 1);
            chip.continuesRight = it.end > ws.addDays(days[last] - 1);
            m_chips.append(chip);
        }
    }
}

void CalendarView::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QLocale en(QLocale::English);
    const QList<int> days = visibleDays();
    const double gridW = days.size() * m_cellW;
    // Header.
    p.setFont(Theme::displayFont(20, QFont::DemiBold));
    p.setPen(c.starlight);
    p.drawText(QRectF(4, 0, 400, 40), Qt::AlignVCenter, en.toString(m_month, QStringLiteral("MMMM yyyy")));
    m_nextRect = QRectF(width() - 34, 6, 28, 28);
    m_todayRect = QRectF(width() - 104, 6, 64, 28);
    m_prevRect = QRectF(width() - 138, 6, 28, 28);
    for (int i = 0; i < 3; ++i) {
        const QRectF r = i == 0 ? m_prevRect : i == 1 ? m_todayRect : m_nextRect;
        if (m_hoverNav == i)
            Theme::paintHover(p, r, 1.0, 6);
        if (i == 1) {
            p.setFont(Theme::uiFont(13, QFont::Medium));
            p.setPen(m_hoverNav == i ? c.starlight : c.moon);
            p.drawText(r, Qt::AlignCenter, tr("Today"));
        } else {
            Icons::paint(p, r.adjusted(6, 6, -6, -6), i == 0 ? QStringLiteral("chevron-left") : QStringLiteral("chevron-right"),
                         m_hoverNav == i ? c.starlight : c.mist);
        }
    }
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!dp) {
        paintEmptyState(p, QRectF(0, 60, width(), height() - 60), tr("A calendar places items on a Date property."),
                        m_ctx.editable ? tr("Add a Date property") : QString(), &m_emptyButton);
        return;
    }
    p.setFont(Theme::uiFont(11.5, QFont::Medium));
    p.setPen(c.fog);
    for (int col = 0; col < days.size(); ++col)
        p.drawText(QRectF(col * m_cellW + 10, 48, m_cellW - 20, 24), Qt::AlignVCenter | Qt::AlignRight, en.dayName(days[col], QLocale::ShortFormat));
    const QDate start = gridStart();
    const QDate today = QDate::currentDate();
    for (int week = 0; week < 6; ++week) {
        for (int col = 0; col < days.size(); ++col) {
            const QRectF cell = cellRect(week, col);
            const QDate d = start.addDays(week * 7 + days[col] - 1);
            const bool inMonth = d.month() == m_month.month();
            const int index = week * 7 + col;
            p.setPen(Qt::NoPen);
            if (!inMonth || d.dayOfWeek() > 5)
                p.fillRect(cell, QColor(0, 0, 0, inMonth ? 28 : 50));
            if (m_dragging && dateAtPos(m_dragPos) == d)
                p.fillRect(cell, QColor(255, 255, 255, 12));
            else if (index == m_hoverCell)
                p.fillRect(cell, QColor(255, 255, 255, 6));
            const bool monthLabel = d.day() == 1;
            const QRectF num(cell.right() - (monthLabel ? 56 : 32), cell.top() + 5, monthLabel ? 50 : 26, 22);
            if (d == today) {
                p.setBrush(c.starlight);
                p.drawEllipse(QPointF(num.right() - 13, num.center().y()), 11, 11);
            }
            p.setFont(Theme::uiFont(12.5, d == today ? QFont::DemiBold : QFont::Normal));
            p.setPen(d == today ? c.basalt : (inMonth ? c.moon : c.dust));
            p.drawText(monthLabel ? num.adjusted(0, 0, -4, 0) : num, monthLabel ? (Qt::AlignVCenter | Qt::AlignRight) : Qt::AlignCenter,
                       monthLabel ? en.toString(d, QStringLiteral("MMM d")) : QString::number(d.day()));
            if (index == m_hoverCell && !m_dragging) {
                const QRectF plus(cell.left() + 5, cell.top() + 5, 22, 22);
                Theme::paintHover(p, plus, 1.0, 5);
                Icons::paint(p, plus.adjusted(4, 4, -4, -4), QStringLiteral("plus"), c.moon);
            }
            if (m_more.contains(index)) {
                p.setFont(Theme::uiFont(11.5, QFont::Medium));
                p.setPen(c.fog);
                p.drawText(QRectF(cell.left() + 10, cell.top() + kChipTop + kSlots * kChipStep, cell.width() - 20, 18), Qt::AlignVCenter,
                           tr("+%1 more").arg(m_more.value(index)));
            }
        }
    }
    // Grid lines.
    p.setPen(QPen(c.hairline.lighter(115), 1));
    for (int r = 0; r <= 6; ++r)
        p.drawLine(QPointF(0, m_gridTop + r * m_cellH - 0.5), QPointF(gridW, m_gridTop + r * m_cellH - 0.5));
    for (int col = 1; col < days.size(); ++col)
        p.drawLine(QPointF(col * m_cellW - 0.5, m_gridTop), QPointF(col * m_cellW - 0.5, m_gridTop + 6 * m_cellH));
    // Chips: single days and spanning bars.
    auto chipPath = [](const QRectF &r, bool left, bool right) {
        QPainterPath path;
        const double rad = 6;
        path.moveTo(r.left() + (left ? 0 : rad), r.top());
        path.lineTo(r.right() - (right ? 0 : rad), r.top());
        if (right)
            path.lineTo(r.right(), r.top());
        else
            path.quadTo(r.right(), r.top(), r.right(), r.top() + rad);
        path.lineTo(r.right(), r.bottom() - (right ? 0 : rad));
        if (right)
            path.lineTo(r.right(), r.bottom());
        else
            path.quadTo(r.right(), r.bottom(), r.right() - rad, r.bottom());
        path.lineTo(r.left() + (left ? 0 : rad), r.bottom());
        if (left)
            path.lineTo(r.left(), r.bottom());
        else
            path.quadTo(r.left(), r.bottom(), r.left(), r.bottom() - rad);
        path.lineTo(r.left(), r.top() + (left ? 0 : rad));
        if (!left)
            path.quadTo(r.left(), r.top(), r.left() + rad, r.top());
        path.closeSubpath();
        return path;
    };
    for (int i = 0; i < m_chips.size(); ++i) {
        const Chip &chip = m_chips[i];
        if (m_dragging && m_chips[m_dragChip].row == chip.row)
            continue;
        const RowRef &row = m_ctx.rows[chip.row];
        const bool hover = m_hoverChip >= 0 && m_chips[m_hoverChip].row == chip.row;
        const QRectF r = chip.rect.adjusted(chip.continuesLeft ? -5 : 0.5, 0.5, chip.continuesRight ? 5 : -0.5, -0.5);
        p.setPen(QPen(hover ? c.smoke : c.ash, 1));
        p.setBrush(hover ? QColor(0x26, 0x26, 0x2C) : QColor(0x1E, 0x1E, 0x23));
        p.drawPath(chipPath(r, chip.continuesLeft, chip.continuesRight));
        double x = chip.rect.left() + 7;
        if (chip.continuesLeft) {
            Icons::paint(p, QRectF(x - 4, chip.rect.center().y() - 6, 12, 12), QStringLiteral("chevron-left"), c.fog);
            x += 10;
        }
        if (m_ctx.view.showPageIcon && row.meta && !row.meta->icon.isEmpty()) {
            Icons::paintPageIcon(p, QRectF(x, chip.rect.center().y() - 7, 14, 14), row.meta->icon, c.mist);
            x += 18;
        }
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        p.setPen(c.moon);
        QString label = titleOf(row);
        // Times show before the title for single-day timed items.
        const QJsonValue v = rowValue(*dp, row);
        if (v.toString().size() > 10 && !chip.continuesLeft && !chip.continuesRight && dateEndOf(v).date() <= dateOf(v))
            label = QLocale(QLocale::English).toString(dateTimeOf(v).time(), QStringLiteral("h:mm")) + QStringLiteral("  ") + label;
        const double right = chip.rect.right() - (chip.continuesRight ? 16 : 6);
        p.drawText(QRectF(x, chip.rect.top(), right - x, chip.rect.height()), Qt::AlignVCenter,
                   QFontMetricsF(p.font()).elidedText(label, Qt::ElideRight, right - x));
        if (chip.continuesRight)
            Icons::paint(p, QRectF(chip.rect.right() - 14, chip.rect.center().y() - 6, 12, 12), QStringLiteral("chevron-right"), c.fog);
    }
    if (m_dragging && m_dragChip >= 0) {
        const Chip &chip = m_chips[m_dragChip];
        const double w = qMin(chip.rect.width(), m_cellW - 10);
        const QRectF r(m_dragPos - QPointF(w / 2, 12), QSizeF(w, kChipH));
        Theme::paintSoftShadow(p, r, 6, 10, 1.4);
        p.setPen(QPen(c.mist, 1));
        p.setBrush(QColor(0x2A, 0x2A, 0x31));
        p.drawRoundedRect(r, 6, 6);
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        p.setPen(c.starlight);
        p.drawText(r.adjusted(8, 0, -6, 0), Qt::AlignVCenter, QFontMetricsF(p.font()).elidedText(titleOf(m_ctx.rows[chip.row]), Qt::ElideRight, w - 14));
    }
}

void CalendarView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if ((e->buttons() & Qt::LeftButton) && m_dragChip >= 0) {
        if (!m_dragging && (e->position().toPoint() - m_pressPos).manhattanLength() > 5)
            m_dragging = true;
        m_dragPos = pos;
        update();
        return;
    }
    int nav = -1;
    if (m_prevRect.contains(pos))
        nav = 0;
    else if (m_todayRect.contains(pos))
        nav = 1;
    else if (m_nextRect.contains(pos))
        nav = 2;
    int chip = -1;
    for (int i = 0; i < m_chips.size(); ++i)
        if (m_chips[i].rect.contains(pos))
            chip = i;
    int cell = -1;
    const int cols = int(visibleDays().size());
    if (pos.y() >= m_gridTop && pos.y() < m_gridTop + 6 * m_cellH && pos.x() < cols * m_cellW)
        cell = int((pos.y() - m_gridTop) / m_cellH) * 7 + int(pos.x() / m_cellW);
    if (nav != m_hoverNav || chip != m_hoverChip || cell != m_hoverCell) {
        m_hoverNav = nav;
        m_hoverChip = chip;
        m_hoverCell = cell;
        update();
    }
    setCursor(nav >= 0 || chip >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void CalendarView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton)
        return;
    const QPointF pos = e->position();
    if (m_hoverNav == 0) {
        setMonth(m_month.addMonths(-1));
        return;
    }
    if (m_hoverNav == 1) {
        setMonth(QDate::currentDate());
        return;
    }
    if (m_hoverNav == 2) {
        setMonth(m_month.addMonths(1));
        return;
    }
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!dp) {
        if (m_emptyButton.contains(pos) && m_ctx.editable) {
            const QString existing = m_ctx.schema.defaultDateProp();
            if (!existing.isEmpty())
                saveView([existing](View &v) { v.dateProp = existing; });
            else
                addPropertyOfType(m_ctx.databaseId, m_ctx.view.id, PropType::Date, false);
        }
        return;
    }
    if (m_hoverChip >= 0) {
        m_dragChip = m_hoverChip;
        m_pressPos = e->position().toPoint();
        m_dragPos = pos;
        return;
    }
    if (m_hoverCell >= 0) {
        const int week = m_hoverCell / 7, col = m_hoverCell % 7;
        const QRectF cell = cellRect(week, col);
        const QDate day = gridStart().addDays(week * 7 + visibleDays()[col] - 1);
        if (QRectF(cell.left() + 5, cell.top() + 5, 22, 22).contains(pos) && !propTypeIsComputed(dp->type)) {
            emit createRow({{dp->id, day.toString(Qt::ISODate)}}, true);
        } else if (m_more.contains(m_hoverCell)) {
            // Everything on that day.
            QMenu *menu = Theme::makeMenu(this);
            Theme::addMenuSection(menu, QLocale(QLocale::English).toString(day, QStringLiteral("dddd, MMMM d")));
            for (const RowRef &r : m_ctx.rows) {
                const auto span = spanOf(m_ctx.schema, m_ctx.view, *dp, r);
                if (!r.meta || !span.first.isValid() || day < span.first || day > span.second)
                    continue;
                const QString id = r.meta->id;
                menu->addAction(Icons::icon(QStringLiteral("page")), titleOf(r), this, [this, id] { emit openRow(id); });
            }
            menu->popup(e->globalPosition().toPoint());
        }
    }
}

void CalendarView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragChip < 0)
        return;
    const RowRef row = m_ctx.rows[m_chips[m_dragChip].row];
    const Property *dp = m_ctx.schema.prop(m_ctx.view.dateProp);
    if (!m_dragging) {
        if (row.meta)
            emit openRow(row.meta->id);
    } else if (dp && row.meta && !propTypeIsComputed(dp->type)) {
        const QDate from = dateAtPos(QPointF(m_pressPos));
        const QDate to = dateAtPos(e->position());
        if (from.isValid() && to.isValid() && from != to) {
            // Move the whole range, keeping times and the reminder.
            const qint64 delta = from.daysTo(to);
            const QJsonValue v = rowValue(*dp, row);
            const bool withTime = v.toString().section(QLatin1Char('/'), 0, 0).size() > 10;
            const QDateTime start = dateTimeOf(v).addDays(delta);
            const QDateTime end = dateEndOf(v).isValid() ? dateEndOf(v).addDays(delta) : QDateTime();
            Workspace *ws = Workspace::instance();
            const QString id = row.meta->id;
            ws->batch([&] {
                ws->setProp(id, dp->id, makeDateValue(start, withTime, end, dateHasReminder(v)));
                if (const Property *ep = m_ctx.schema.prop(m_ctx.view.endDateProp); ep && ep != dp && !propTypeIsComputed(ep->type)) {
                    const QJsonValue ev = rowValue(*ep, row);
                    if (dateOf(ev).isValid())
                        ws->setProp(id, ep->id, makeDateValue(dateTimeOf(ev).addDays(delta), ev.toString().size() > 10, {}, dateHasReminder(ev)));
                }
            });
        }
    }
    m_dragging = false;
    m_dragChip = -1;
    update();
}

void CalendarView::leaveEvent(QEvent *)
{
    m_hoverCell = m_hoverChip = m_hoverNav = -1;
    update();
}
