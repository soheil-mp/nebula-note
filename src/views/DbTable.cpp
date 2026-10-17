// The table layout: columns of properties, groups, sub-items, a calculation
// footer, wrapped cells, frozen columns and bulk selection.
#include "DbViews.h"

#include "PropertyEditors.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QContextMenuEvent>
#include <QFontMetricsF>
#include <QHelpEvent>
#include <QJsonArray>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace
{
constexpr int kGrip = 28;
constexpr int kHeaderH = 34;
constexpr int kRowH = 36;
constexpr int kGroupH = 42;
constexpr int kCalcH = 30;
constexpr int kAddColW = 44;

int defaultWidth(PropType t)
{
    switch (t) {
    case PropType::Title:
        return 280;
    case PropType::Number:
    case PropType::Checkbox:
    case PropType::UniqueId:
        return 110;
    case PropType::Select:
    case PropType::Status:
    case PropType::Date:
    case PropType::Phone:
    case PropType::Button:
        return 160;
    case PropType::Created:
    case PropType::Edited:
        return 190;
    default:
        return 200;
    }
}

bool wraps(PropType t)
{
    return t == PropType::Title || t == PropType::Text || t == PropType::Url || t == PropType::Email || t == PropType::Formula
           || t == PropType::Rollup || t == PropType::Relation;
}
} // namespace

TableView::TableView(QWidget *parent)
    : DbView(parent)
{
}

void TableView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    // Selection only keeps rows that are still shown.
    QSet<QString> live;
    for (const RowRef &r : m_ctx.rows)
        if (r.meta && m_selected.contains(r.meta->id))
            live.insert(r.meta->id);
    if (live != m_selected) {
        m_selected = live;
        emit selectionChanged();
    }
    layoutColumns();
    layoutLines();
    update();
}

QStringList TableView::selectedRows() const
{
    QStringList out;
    for (const RowRef &r : m_ctx.rows)
        if (r.meta && m_selected.contains(r.meta->id))
            out.append(r.meta->id);
    return out;
}

void TableView::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    update();
    emit selectionChanged();
}

void TableView::layoutColumns()
{
    m_cols.clear();
    double x = kGrip;
    const QList<const Property *> props = m_ctx.schema.orderedProps(m_ctx.view);
    for (int i = 0; i < props.size(); ++i) {
        const double w = m_ctx.view.widths.value(props[i]->id, defaultWidth(props[i]->type));
        m_cols.append({props[i], x, w, i < m_ctx.view.frozen});
        x += w;
        if (i < m_ctx.view.frozen)
            m_frozenRight = x;
    }
    if (m_ctx.view.frozen <= 0)
        m_frozenRight = 0;
    m_tableWidth = x - kGrip + kAddColW;
}

double TableView::rowHeight(const RowRef &r) const
{
    if (!m_ctx.view.wrapCells)
        return kRowH;
    double h = kRowH;
    const QFontMetricsF fm(Theme::uiFont(13.5));
    for (const Column &col : m_cols) {
        if (!wraps(col.prop->type))
            continue;
        const QString text = col.prop->type == PropType::Title ? titleOf(r) : displayValue(*col.prop, rowValue(*col.prop, r));
        if (text.isEmpty())
            continue;
        const double w = col.w - 16 - (col.prop->type == PropType::Title ? 24 : 0);
        const QRectF br = fm.boundingRect(QRectF(0, 0, qMax(20.0, w), 2000), Qt::TextWordWrap, text);
        h = qMax(h, br.height() + 16);
    }
    return qMin(h, 320.0);
}

void TableView::layoutLines()
{
    m_lines.clear();
    m_groups = groups();
    double y = 0;
    m_lines.append({Line::Header, y, double(kHeaderH)});
    y += kHeaderH;
    const bool anyCalc = std::any_of(m_ctx.view.calcs.begin(), m_ctx.view.calcs.end(), [](const QString &f) { return !f.isEmpty() && f != QLatin1String("none"); });
    const int limit = rowLimit();
    int shown = 0;
    bool truncated = false;
    auto addRows = [&](const QList<int> &indexes, const QString &group, int groupIndex) {
        // Sub-items nest under their parent within the rows given.
        QList<RowRef> subset;
        for (int i : indexes)
            subset.append(m_ctx.rows[i]);
        const QList<NestedRow> nested = nestRows(m_ctx.schema, subset, m_collapsedRows, m_ctx.view.subItems);
        for (const NestedRow &n : nested) {
            if (shown >= limit) {
                truncated = true;
                break;
            }
            const int row = indexes[n.index];
            const double h = rowHeight(m_ctx.rows[row]);
            Line l{Line::Row, y, h, row, n.depth, n.children, group, groupIndex};
            m_lines.append(l);
            y += h;
            ++shown;
        }
        if (m_ctx.editable || true) {
            Line add{Line::Add, y, double(kRowH)};
            add.group = group;
            add.groupIndex = groupIndex;
            m_lines.append(add);
            y += kRowH;
        }
    };
    if (m_groups.isEmpty()) {
        QList<int> all;
        for (int i = 0; i < m_ctx.rows.size(); ++i)
            all.append(i);
        addRows(all, QString(), -1);
    } else {
        for (int gi = 0; gi < m_groups.size(); ++gi) {
            const RowGroup &g = m_groups[gi];
            Line gl{Line::Group, y + 8, double(kGroupH)};
            gl.group = g.key;
            gl.groupIndex = gi;
            m_lines.append(gl);
            y += kGroupH + 8;
            if (groupCollapsed(g.key))
                continue;
            addRows(g.rows, g.key, gi);
            if (anyCalc) {
                Line cl{Line::Calc, y, double(kCalcH)};
                cl.group = g.key;
                cl.groupIndex = gi;
                m_lines.append(cl);
                y += kCalcH;
            }
        }
    }
    if (truncated) {
        m_lines.append({Line::More, y, double(kRowH)});
        y += kRowH;
    }
    // The footer: calculations for the whole view.
    Line footer{Line::Calc, y, double(kCalcH)};
    footer.groupIndex = -1;
    m_lines.append(footer);
    y += kCalcH + 8;
    setFixedHeight(int(y));
}

int TableView::contentWidth(int available) const
{
    return qMax(available, int(kGrip + m_tableWidth + 12));
}

double TableView::colX(const Column &c) const
{
    return c.frozen ? c.x + m_scrollX : c.x;
}

int TableView::columnAt(double x) const
{
    // Frozen columns sit on top of the others.
    for (int i = 0; i < m_cols.size(); ++i)
        if (m_cols[i].frozen && x >= colX(m_cols[i]) && x < colX(m_cols[i]) + m_cols[i].w)
            return i;
    for (int i = 0; i < m_cols.size(); ++i)
        if (!m_cols[i].frozen && x >= m_cols[i].x && x < m_cols[i].x + m_cols[i].w && (m_frozenRight <= 0 || x >= m_frozenRight + m_scrollX))
            return i;
    return -1;
}

int TableView::lineAt(double y) const
{
    for (int i = 0; i < m_lines.size(); ++i)
        if (y >= m_lines[i].y && y < m_lines[i].y + m_lines[i].h)
            return i;
    return -1;
}

QRect TableView::titleRectFor(const QString &rowId) const
{
    for (const Line &l : m_lines)
        if (l.kind == Line::Row && m_ctx.rows[l.row].meta && m_ctx.rows[l.row].meta->id == rowId && !m_cols.isEmpty())
            return QRect(mapToGlobal(QPoint(int(colX(m_cols[0])), int(l.y))), QSize(int(m_cols[0].w), int(l.h)));
    return {};
}

void TableView::saveWidth(const QString &propId, int w)
{
    saveView([propId, w](View &v) { v.widths.insert(propId, w); });
}

void TableView::calcMenu(const Property *prop, const QPoint &globalPos)
{
    QMenu *menu = Theme::makeMenu(this);
    menu->addSection(tr("Calculate"));
    const QString current = m_ctx.view.calcs.value(prop->id, QStringLiteral("none"));
    for (const QString &fn : calcFunctions(*prop)) {
        QAction *a = menu->addAction(calcName(fn));
        a->setCheckable(true);
        a->setChecked(fn == current);
        const QString id = prop->id;
        connect(a, &QAction::triggered, this, [this, id, fn] {
            saveView([id, fn](View &v) {
                if (fn == QLatin1String("none"))
                    v.calcs.remove(id);
                else
                    v.calcs.insert(id, fn);
            });
        });
    }
    menu->popup(globalPos);
}

void TableView::paintCell(QPainter &p, const QRectF &cell, const Column &col, const RowRef &row, const Line &line, bool hoverRow)
{
    const auto &c = Theme::colors();
    if (col.prop->type == PropType::Title) {
        double x = cell.left() + line.depth * 20;
        // Sub-item toggle.
        if (line.children > 0) {
            const bool folded = row.meta && m_collapsedRows.contains(row.meta->id);
            Icons::paint(p, QRectF(x - 2, cell.top() + qMin(cell.height(), double(kRowH)) / 2 - 7, 14, 14),
                         folded ? QStringLiteral("chevron-right") : QStringLiteral("chevron-down"), hoverRow && m_hoverToggle ? c.moon : c.fog);
            x += 16;
        } else if (line.depth > 0) {
            x += 16;
        }
        if (m_ctx.view.showPageIcon) {
            Icons::paintPageIcon(p, QRectF(x, cell.top() + qMin(cell.height(), double(kRowH)) / 2 - 8, 16, 16), row.meta ? row.meta->icon : QString(),
                                 c.mist, QStringLiteral("page"));
            x += 24;
        }
        double textRight = cell.right();
        if (hoverRow) {
            const QRectF open(cell.right() - 52, cell.top() + kRowH / 2.0 - 11, 52, 22);
            p.setPen(QPen(c.ash, 1));
            p.setBrush(m_hoverOpen ? c.smoke : c.graphite);
            p.drawRoundedRect(open, 5, 5);
            Icons::paint(p, QRectF(open.left() + 6, open.center().y() - 6, 12, 12), QStringLiteral("external"), c.mist);
            p.setFont(Theme::uiFont(11.5, QFont::DemiBold));
            p.setPen(c.moon);
            p.drawText(open.adjusted(20, 0, 0, 0), Qt::AlignVCenter, tr("OPEN"));
            textRight = open.left() - 6;
        }
        const bool untitled = !row.meta || row.meta->title.trimmed().isEmpty();
        const QRectF tr(x, cell.top(), textRight - x, m_ctx.view.wrapCells ? cell.height() : double(kRowH));
        if (m_ctx.view.wrapCells && !untitled) {
            p.setFont(Theme::uiFont(13.5, QFont::Medium));
            p.setPen(c.starlight);
            p.drawText(tr.adjusted(0, 8, 0, -4), Qt::TextWordWrap | Qt::AlignTop, titleOf(row));
        } else {
            PropertyEditors::paintValue(p, tr, *col.prop, titleOf(row), untitled);
        }
        return;
    }
    const QJsonValue v = rowValue(*col.prop, row);
    if (m_ctx.view.wrapCells && wraps(col.prop->type) && col.prop->type != PropType::Relation) {
        const QString text = displayValue(*col.prop, v);
        p.setFont(Theme::uiFont(13.5));
        p.setPen(c.moon);
        p.drawText(cell.adjusted(0, 8, 0, -4), Qt::TextWordWrap | Qt::AlignTop, text);
        return;
    }
    const QRectF oneLine(cell.left(), cell.top(), cell.width(), double(kRowH));
    PropertyEditors::paintValue(p, oneLine, *col.prop, v, false,
                                resultType(*col.prop) == PropType::Number ? Qt::AlignRight : Qt::AlignLeft);
}

void TableView::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double right = kGrip + m_tableWidth;
    const QRect clip = e->rect();
    const bool selecting = !m_selected.isEmpty();

    auto drawColumns = [&](bool frozenPass) {
        for (int li = 0; li < m_lines.size(); ++li) {
            const Line &line = m_lines[li];
            if (line.y > clip.bottom() || line.y + line.h < clip.top())
                continue;
            if (line.kind == Line::Header) {
                for (int i = 0; i < m_cols.size(); ++i) {
                    const Column &col = m_cols[i];
                    if (col.frozen != frozenPass)
                        continue;
                    const QRectF hr(colX(col), 0, col.w, kHeaderH - 1);
                    if (frozenPass)
                        p.fillRect(hr.adjusted(0, 0, 0, 1), c.basalt);
                    if (m_hoverLine == 0 && m_hoverCol == i && !m_hoverAddCol && m_resizeCol < 0)
                        Theme::paintHover(p, hr.adjusted(1, 3, -1, -3), 1.0, 5);
                    Icons::paint(p, QRectF(hr.left() + 8, hr.center().y() - 7, 14, 14), propTypeIcon(col.prop->type), c.fog);
                    p.setFont(Theme::uiFont(12.5, QFont::Medium));
                    p.setPen(m_dragCol == i ? c.starlight : c.haze);
                    p.drawText(hr.adjusted(28, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                               QFontMetricsF(p.font()).elidedText(col.prop->name, Qt::ElideRight, col.w - 36));
                }
                continue;
            }
            if (line.kind == Line::Group) {
                if (frozenPass)
                    continue;
                const RowGroup &g = m_groups[line.groupIndex];
                const QRectF gr(kGrip + 4 + m_scrollX, line.y, qMin(560.0, width() - kGrip - 8.0), line.h);
                paintGroupHeader(p, gr, g, groupProp(), groupCollapsed(g.key), li == m_hoverLine);
                continue;
            }
            if (line.kind == Line::Row) {
                const RowRef &row = m_ctx.rows[line.row];
                const bool hover = li == m_hoverLine && m_dragRow < 0;
                const bool selected = row.meta && m_selected.contains(row.meta->id);
                for (int i = 0; i < m_cols.size(); ++i) {
                    const Column &col = m_cols[i];
                    if (col.frozen != frozenPass)
                        continue;
                    const QRectF full(colX(col), line.y, col.w, line.h);
                    if (frozenPass)
                        p.fillRect(full, c.basalt);
                    if (selected)
                        p.fillRect(full, Theme::withAlpha(QColor(255, 255, 255), 0.05));
                    else if (hover)
                        p.fillRect(full, Theme::withAlpha(QColor(255, 255, 255), 0.028));
                    paintCell(p, QRectF(full.left() + 8, full.top(), full.width() - 16, full.height()), col, row, line, hover);
                }
                continue;
            }
            if (line.kind == Line::Calc) {
                for (int i = 0; i < m_cols.size(); ++i) {
                    const Column &col = m_cols[i];
                    if (col.frozen != frozenPass)
                        continue;
                    const QRectF cell(colX(col), line.y, col.w, line.h);
                    if (frozenPass)
                        p.fillRect(cell, c.basalt);
                    const QString fn = m_ctx.view.calcs.value(col.prop->id);
                    QList<RowRef> rows;
                    if (line.groupIndex >= 0) {
                        for (int r : m_groups[line.groupIndex].rows)
                            rows.append(m_ctx.rows[r]);
                    } else {
                        rows = m_ctx.rows;
                    }
                    const bool hoverCell = li == m_hoverLine && m_hoverCol == i;
                    if (fn.isEmpty() || fn == QLatin1String("none")) {
                        if (hoverCell) {
                            p.setFont(Theme::uiFont(11.5, QFont::Medium));
                            p.setPen(c.fog);
                            p.drawText(cell.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, tr("Calculate ▾"));
                        } else if (i == 0 && line.groupIndex < 0) {
                            p.setFont(Theme::uiFont(11.5, QFont::Medium));
                            p.setPen(c.dust);
                            p.drawText(cell.adjusted(10, 0, -8, 0), Qt::AlignVCenter, tr("COUNT  %1").arg(m_ctx.rows.size()));
                        }
                        continue;
                    }
                    if (hoverCell)
                        Theme::paintHover(p, cell.adjusted(2, 3, -2, -3), 1.0, 5);
                    p.setFont(Theme::uiFont(11.5, QFont::Medium));
                    const QString label = calcName(fn).toUpper();
                    const QString value = calculate(*col.prop, rows, fn);
                    p.setPen(c.dust);
                    const double lw = QFontMetricsF(p.font()).horizontalAdvance(label + QStringLiteral("  "));
                    const QFontMetricsF vm(Theme::uiFont(12.5, QFont::Medium));
                    const double vw = vm.horizontalAdvance(value);
                    const double start = cell.right() - 8 - vw - lw;
                    p.drawText(QRectF(qMax(cell.left() + 6, start), cell.top(), lw, cell.height()), Qt::AlignVCenter, label);
                    p.setFont(Theme::uiFont(12.5, QFont::Medium));
                    p.setPen(c.moon);
                    p.drawText(cell.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, value);
                }
            }
        }
    };

    // Grid lines and the gutter under everything.
    for (int li = 0; li < m_lines.size(); ++li) {
        const Line &line = m_lines[li];
        if (line.y > clip.bottom() || line.y + line.h < clip.top())
            continue;
        if (line.kind == Line::Header) {
            p.setPen(QPen(c.ash, 1));
            p.drawLine(QPointF(kGrip, kHeaderH - 0.5), QPointF(right, kHeaderH - 0.5));
            const QRectF addCol(right - kAddColW, 0, kAddColW, kHeaderH - 1);
            if (m_hoverAddCol)
                Theme::paintHover(p, addCol.adjusted(4, 3, -4, -3), 1.0, 5);
            if (m_ctx.editable)
                Icons::paint(p, QRectF(addCol.center().x() - 8, addCol.center().y() - 8, 16, 16), QStringLiteral("plus"), m_hoverAddCol ? c.moon : c.fog);
        } else if (line.kind == Line::Row) {
            p.setPen(QPen(c.hairline, 1));
            p.drawLine(QPointF(kGrip, line.y + line.h - 0.5), QPointF(right, line.y + line.h - 0.5));
            for (int i = 1; i < m_cols.size(); ++i)
                p.drawLine(QPointF(m_cols[i].x - 0.5, line.y), QPointF(m_cols[i].x - 0.5, line.y + line.h));
            if (!m_cols.isEmpty())
                p.drawLine(QPointF(right - kAddColW - 0.5, line.y), QPointF(right - kAddColW - 0.5, line.y + line.h));
        } else if (line.kind == Line::Add) {
            const bool hover = li == m_hoverLine;
            const QRectF add(kGrip, line.y, m_tableWidth - kAddColW, line.h);
            if (hover)
                Theme::paintHover(p, add, 0.6, 0);
            Icons::paint(p, QRectF(add.left() + 10 + m_scrollX, add.center().y() - 7, 14, 14), QStringLiteral("plus"), hover ? c.moon : c.fog);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(hover ? c.moon : c.fog);
            p.drawText(add.adjusted(32 + m_scrollX, 0, 0, 0), Qt::AlignVCenter, tr("New"));
            p.setPen(QPen(c.hairline, 1));
            p.drawLine(QPointF(kGrip, add.bottom() - 0.5), QPointF(right, add.bottom() - 0.5));
        } else if (line.kind == Line::More) {
            const bool hover = li == m_hoverLine;
            const QRectF more(kGrip, line.y, m_tableWidth - kAddColW, line.h);
            if (hover)
                Theme::paintHover(p, more, 0.6, 0);
            Icons::paint(p, QRectF(more.left() + 10 + m_scrollX, more.center().y() - 7, 14, 14), QStringLiteral("chevron-down"), hover ? c.moon : c.fog);
            p.setFont(Theme::uiFont(13.5));
            p.setPen(hover ? c.moon : c.fog);
            p.drawText(more.adjusted(32 + m_scrollX, 0, 0, 0), Qt::AlignVCenter, tr("Load more"));
        }
    }
    drawColumns(false);
    // Frozen columns stay put while the rest scroll under them.
    if (m_frozenRight > 0) {
        drawColumns(true);
        if (m_scrollX > 0) {
            p.setPen(QPen(c.ash, 1));
            p.drawLine(QPointF(m_frozenRight + m_scrollX - 0.5, 0), QPointF(m_frozenRight + m_scrollX - 0.5, height()));
        }
    }
    // Gutter: grips and selection boxes.
    for (int li = 0; li < m_lines.size(); ++li) {
        const Line &line = m_lines[li];
        if (line.kind != Line::Row || line.y > clip.bottom() || line.y + line.h < clip.top())
            continue;
        const RowRef &row = m_ctx.rows[line.row];
        const bool hover = li == m_hoverLine && m_dragRow < 0;
        const bool selected = row.meta && m_selected.contains(row.meta->id);
        const double gx = m_frozenRight > 0 ? m_scrollX : 0;
        if (m_frozenRight > 0)
            p.fillRect(QRectF(gx, line.y, kGrip, line.h), c.basalt);
        if (hover || selecting) {
            const QRectF box(gx + 6, line.y + kRowH / 2.0 - 8, 16, 16);
            if (selected || selecting || (hover && m_hoverCheck)) {
                if (selected) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(c.starlight);
                    p.drawRoundedRect(box, 4, 4);
                    QPainterPath tick;
                    tick.moveTo(box.left() + 4.2, box.top() + 8.5);
                    tick.lineTo(box.left() + 6.9, box.top() + 11.2);
                    tick.lineTo(box.left() + 12, box.top() + 5.3);
                    p.setPen(QPen(c.basalt, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.setBrush(Qt::NoBrush);
                    p.drawPath(tick);
                } else {
                    p.setPen(QPen(c.dust, 1.4));
                    p.setBrush(Qt::NoBrush);
                    p.drawRoundedRect(box.adjusted(0.7, 0.7, -0.7, -0.7), 4, 4);
                }
            } else if (!m_ctx.sorted && m_groups.isEmpty()) {
                Icons::paint(p, QRectF(gx + 6, line.y + kRowH / 2.0 - 8, 16, 16), QStringLiteral("grip"), c.dust);
            }
        }
    }
    // Drop indicator for rows.
    if (m_dragRow >= 0 && m_dropLine >= 0 && m_dropLine < m_lines.size()) {
        const double y = m_lines[m_dropLine].y;
        p.setPen(Qt::NoPen);
        p.setBrush(c.mist);
        p.drawRoundedRect(QRectF(kGrip, y - 1.5, m_tableWidth - kAddColW, 3), 1.5, 1.5);
    }
    // Column being moved.
    if (m_dragCol >= 0) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.mist);
        p.drawRoundedRect(QRectF(m_dragColX - 1.5, 2, 3, height() - 4), 1.5, 1.5);
    }
}

bool TableView::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int li = lineAt(he->pos().y());
        const int col = columnAt(he->pos().x());
        if (li == 0 && col >= 0 && !m_cols[col].prop->description.isEmpty()) {
            QToolTip::showText(he->globalPos(), m_cols[col].prop->description, this);
            return true;
        }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return DbView::event(e);
}

void TableView::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (m_resizeCol >= 0) {
        const double w = qMax(80.0, m_resizeStartW + pos.x() - m_resizeStartX);
        m_cols[m_resizeCol].w = w;
        m_ctx.view.widths.insert(m_cols[m_resizeCol].prop->id, int(w));
        layoutColumns();
        layoutLines();
        emit geometryNeedsUpdate();
        update();
        return;
    }
    if (m_pressOnHeader && (e->buttons() & Qt::LeftButton) && m_ctx.editable) {
        if (m_dragCol < 0 && (e->position().toPoint() - m_pressPos).manhattanLength() > 6) {
            const int col = columnAt(m_pressPos.x());
            if (col > 0) // the title column stays first
                m_dragCol = col;
        }
        if (m_dragCol >= 0) {
            // Snap the marker to the nearest column edge.
            double best = m_cols.value(1).x;
            for (const Column &c : m_cols)
                for (double edge : {c.x, c.x + c.w})
                    if (qAbs(edge - pos.x()) < qAbs(best - pos.x()) && edge > m_cols.first().x + m_cols.first().w - 1)
                        best = edge;
            m_dragColX = best;
            update();
        }
        return;
    }
    if (m_pressOnGrip && (e->buttons() & Qt::LeftButton)) {
        const int li = lineAt(m_pressPos.y());
        if (m_dragRow < 0 && (e->position().toPoint() - m_pressPos).manhattanLength() > 4 && li >= 0 && m_lines[li].kind == Line::Row)
            m_dragRow = li;
        if (m_dragRow >= 0) {
            m_dropLine = -1;
            for (int i = 0; i < m_lines.size(); ++i) {
                const Line &l = m_lines[i];
                if ((l.kind == Line::Row || l.kind == Line::Add) && pos.y() < l.y + l.h / 2) {
                    m_dropLine = i;
                    break;
                }
            }
            update();
        }
        return;
    }
    const int li = lineAt(pos.y());
    const int col = columnAt(pos.x());
    const double right = kGrip + m_tableWidth;
    const bool addCol = li == 0 && pos.x() >= right - kAddColW && pos.x() < right && m_ctx.editable;
    bool open = false, toggle = false, check = false;
    if (li >= 0 && m_lines[li].kind == Line::Row) {
        const double gx = m_frozenRight > 0 ? m_scrollX : 0;
        check = pos.x() >= gx && pos.x() < gx + kGrip;
        if (col >= 0 && m_cols[col].prop->type == PropType::Title) {
            const double cr = colX(m_cols[col]) + m_cols[col].w - 8;
            open = pos.x() >= cr - 52 && pos.x() <= cr && pos.y() < m_lines[li].y + kRowH;
            const Line &l = m_lines[li];
            const double tx = colX(m_cols[col]) + 8 + l.depth * 20;
            toggle = l.children > 0 && pos.x() >= tx - 4 && pos.x() < tx + 14;
        }
    }
    bool edge = false;
    if (li == 0 && m_ctx.editable)
        for (const Column &c : m_cols)
            if (qAbs(pos.x() - (colX(c) + c.w)) < 4)
                edge = true;
    if (li != m_hoverLine || col != m_hoverCol || open != m_hoverOpen || addCol != m_hoverAddCol || toggle != m_hoverToggle || check != m_hoverCheck) {
        m_hoverLine = li;
        m_hoverCol = col;
        m_hoverOpen = open;
        m_hoverAddCol = addCol;
        m_hoverToggle = toggle;
        m_hoverCheck = check;
        update();
    }
    const bool rowLine = li >= 0 && m_lines[li].kind == Line::Row;
    setCursor(edge ? Qt::SplitHCursor
                   : (rowLine && check && m_selected.isEmpty() && !m_ctx.sorted && m_groups.isEmpty() ? Qt::OpenHandCursor
                      : (li >= 0 && m_lines[li].kind != Line::Calc) || addCol ? Qt::PointingHandCursor
                                                                             : Qt::ArrowCursor));
}

void TableView::mousePressEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (e->button() != Qt::LeftButton)
        return;
    const int li = lineAt(pos.y());
    if (li < 0)
        return;
    const Line &line = m_lines[li];
    m_pressPos = e->position().toPoint();
    if (line.kind == Line::Header) {
        for (int i = 0; i < m_cols.size(); ++i) {
            if (m_ctx.editable && qAbs(pos.x() - (colX(m_cols[i]) + m_cols[i].w)) < 4) {
                m_resizeCol = i;
                m_resizeStartX = pos.x();
                m_resizeStartW = m_cols[i].w;
                return;
            }
        }
        if (m_hoverAddCol) {
            PropertyEditors::addProperty(this, mapToGlobal(QPoint(int(kGrip + m_tableWidth - kAddColW), kHeaderH)), m_ctx.databaseId,
                                         m_ctx.view.id);
            return;
        }
        m_pressOnHeader = true;
        return;
    }
    if (line.kind == Line::Group) {
        toggleGroup(m_groups[line.groupIndex].key);
        return;
    }
    if (line.kind == Line::Add) {
        QJsonObject props;
        if (line.groupIndex >= 0)
            props = groupValues(groupProp(), m_groups[line.groupIndex].key);
        emit createRow(props, false);
        return;
    }
    if (line.kind == Line::More) {
        emit loadMore();
        return;
    }
    if (line.kind == Line::Calc) {
        const int col = columnAt(pos.x());
        if (col >= 0 && m_ctx.editable)
            calcMenu(m_cols[col].prop, mapToGlobal(QPoint(int(colX(m_cols[col])), int(line.y + line.h))));
        return;
    }
    const RowRef &r = m_ctx.rows[line.row];
    if (!r.meta)
        return;
    // The gutter: tick the row (Shift extends), or drag it.
    if (m_hoverCheck) {
        const QString id = r.meta->id;
        if (e->modifiers() & Qt::ShiftModifier && !m_lastSelected.isEmpty()) {
            bool inside = false;
            for (const Line &l : m_lines) {
                if (l.kind != Line::Row || !m_ctx.rows[l.row].meta)
                    continue;
                const QString lid = m_ctx.rows[l.row].meta->id;
                const bool edge = lid == id || lid == m_lastSelected;
                if (edge || inside)
                    m_selected.insert(lid);
                if (edge && id != m_lastSelected)
                    inside = !inside;
            }
        } else if (!m_selected.isEmpty() || !(!m_ctx.sorted && m_groups.isEmpty())) {
            if (m_selected.contains(id))
                m_selected.remove(id);
            else
                m_selected.insert(id);
        } else {
            m_pressOnGrip = true;
            return;
        }
        m_lastSelected = id;
        update();
        emit selectionChanged();
        return;
    }
    const int col = columnAt(pos.x());
    if (col < 0)
        return;
    const Column &column = m_cols[col];
    if (column.prop->type == PropType::Title) {
        if (m_hoverToggle) {
            if (m_collapsedRows.contains(r.meta->id))
                m_collapsedRows.remove(r.meta->id);
            else
                m_collapsedRows.insert(r.meta->id);
            layoutLines();
            emit geometryNeedsUpdate();
            update();
            return;
        }
        if (m_hoverOpen)
            emit openRow(r.meta->id);
        else
            editTitleInline(r.meta->id, QRect(mapToGlobal(QPoint(int(colX(column)), int(line.y))), QSize(int(column.w), kRowH)));
        return;
    }
    PropertyEditors::edit(this, mapToGlobal(QPoint(int(colX(column)), int(line.y))), m_ctx.databaseId, r.meta->id, column.prop->id,
                          int(column.w));
}

void TableView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_resizeCol >= 0) {
        saveWidth(m_cols[m_resizeCol].prop->id, int(m_cols[m_resizeCol].w));
        m_resizeCol = -1;
        return;
    }
    if (m_pressOnHeader) {
        m_pressOnHeader = false;
        if (m_dragCol >= 0) {
            // New order: the column goes before the one at the marker.
            QStringList order;
            for (const Column &c : m_cols)
                order.append(c.prop->id);
            const QString moved = order.takeAt(m_dragCol);
            int at = 0;
            for (int i = 0; i < m_cols.size(); ++i)
                if (m_cols[i].x + m_cols[i].w / 2 < m_dragColX)
                    at = i + 1;
            if (at > m_dragCol)
                --at;
            order.insert(qBound(1, at, int(order.size())), moved);
            m_dragCol = -1;
            saveView([order](View &v) { v.order = order; });
            update();
            return;
        }
        const int col = columnAt(e->position().x());
        if (col >= 0 && m_ctx.editable)
            PropertyEditors::showPropertyMenu(this, mapToGlobal(QPoint(int(colX(m_cols[col])), kHeaderH)), m_ctx.databaseId, m_cols[col].prop->id,
                                              m_ctx.view.id);
        return;
    }
    // A click on the gutter without dragging ticks the row.
    if (m_pressOnGrip && m_dragRow < 0) {
        const int li = lineAt(m_pressPos.y());
        if (li >= 0 && m_lines[li].kind == Line::Row && m_ctx.rows[m_lines[li].row].meta) {
            const QString id = m_ctx.rows[m_lines[li].row].meta->id;
            if (m_selected.contains(id))
                m_selected.remove(id);
            else
                m_selected.insert(id);
            m_lastSelected = id;
            m_pressOnGrip = false;
            update();
            emit selectionChanged();
            return;
        }
    }
    if (m_dragRow >= 0 && m_dropLine >= 0 && m_dropLine != m_dragRow && m_dropLine != m_dragRow + 1) {
        const QString id = m_ctx.rows[m_lines[m_dragRow].row].meta->id;
        QString before;
        if (m_lines[m_dropLine].kind == Line::Row)
            before = m_ctx.rows[m_lines[m_dropLine].row].meta->id;
        Workspace::instance()->moveRow(id, before);
    }
    m_pressOnGrip = false;
    m_dragRow = -1;
    m_dropLine = -1;
    update();
}

void TableView::mouseDoubleClickEvent(QMouseEvent *e)
{
    const int li = lineAt(e->position().y());
    if (li >= 0 && m_lines[li].kind == Line::Row && m_ctx.rows[m_lines[li].row].meta)
        emit openRow(m_ctx.rows[m_lines[li].row].meta->id);
}

void TableView::leaveEvent(QEvent *)
{
    m_hoverLine = m_hoverCol = -1;
    m_hoverAddCol = m_hoverOpen = m_hoverToggle = m_hoverCheck = false;
    update();
}

void TableView::contextMenuEvent(QContextMenuEvent *e)
{
    const int li = lineAt(e->pos().y());
    if (li >= 0 && m_lines[li].kind == Line::Row && m_ctx.rows[m_lines[li].row].meta)
        rowMenu(m_ctx.rows[m_lines[li].row].meta->id, e->globalPos());
    else if (li == 0) {
        const int col = columnAt(e->pos().x());
        if (col >= 0 && m_ctx.editable) {
            // Header context menu: freeze, wrap, and the property menu.
            QMenu *menu = Theme::makeMenu(this);
            const int index = col;
            QAction *freeze = menu->addAction(Icons::icon(QStringLiteral("freeze")),
                                              m_ctx.view.frozen == index + 1 ? tr("Unfreeze columns") : tr("Freeze up to this column"));
            connect(freeze, &QAction::triggered, this, [this, index] {
                const int n = m_ctx.view.frozen == index + 1 ? 0 : index + 1;
                saveView([n](View &v) { v.frozen = n; });
            });
            QAction *wrap = menu->addAction(Icons::icon(QStringLiteral("wrap")), tr("Wrap cells"));
            wrap->setCheckable(true);
            wrap->setChecked(m_ctx.view.wrapCells);
            connect(wrap, &QAction::toggled, this, [this](bool on) { saveView([on](View &v) { v.wrapCells = on; }); });
            const QString propId = m_cols[col].prop->id;
            menu->addAction(Icons::icon(QStringLiteral("settings")), tr("Property settings…"), this, [this, propId, e] {
                PropertyEditors::showPropertyMenu(this, QCursor::pos(), m_ctx.databaseId, propId, m_ctx.view.id);
                Q_UNUSED(e)
            });
            menu->popup(e->globalPos());
        }
    }
}
