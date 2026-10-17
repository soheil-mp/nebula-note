#include "Popups.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QHash>
#include <QPainterPath>

namespace
{
constexpr int kShadow = 16;
constexpr int kMaxHeight = 380;
}

// ---- CommandPopup ----------------------------------------------------------------

CommandPopup::CommandPopup(QWidget *host)
    : QWidget(host)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    hide();
    m_fade.setDuration(Theme::animMs(130));
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this] { update(); });
}

QRect CommandPopup::cardRect() const
{
    return rect().adjusted(kShadow, kShadow - 4, -kShadow, -kShadow - 4);
}

void CommandPopup::setItems(const QList<MenuItem> &items, bool showSections)
{
    m_items = items;
    m_sections = showSections;
    m_current = 0;
    m_scroll = 0;
    rebuildRows();
    update();
}

void CommandPopup::rebuildRows()
{
    m_rows.clear();
    int y = 6;
    QString section;
    for (int i = 0; i < m_items.size(); ++i) {
        const MenuItem &it = m_items[i];
        if (m_sections && !it.section.isEmpty() && it.section != section) {
            section = it.section;
            m_rows.append({-1, section, y, 28});
            y += 28;
        }
        const int h = (m_largeIcons && !it.subtitle.isEmpty()) ? 46 : 32;
        m_rows.append({i, QString(), y, h});
        y += h;
    }
    m_contentHeight = y + 6;
}

void CommandPopup::popup(const QRect &anchor)
{
    QWidget *host = parentWidget();
    const int cardH = m_items.isEmpty() ? 44 : qMin(kMaxHeight, m_contentHeight);
    const int w = m_cardWidth + 2 * kShadow;
    const int h = cardH + 2 * kShadow;
    int x = anchor.left() - kShadow - 4;
    x = qBound(4, x, qMax(4, host->width() - w - 4));
    int y = anchor.bottom() + 6 - kShadow + 4;
    if (y + h > host->height() - 4 && anchor.top() - h - 2 > 4)
        y = anchor.top() - h - 2 + kShadow;
    setGeometry(x, y, w, h);
    if (!isVisible()) {
        show();
        raise();
        m_fade.stop();
        m_fade.start();
    }
    update();
}

void CommandPopup::dismiss()
{
    if (!isVisible())
        return;
    hide();
    emit dismissed();
}

void CommandPopup::moveSelection(int delta)
{
    if (m_items.isEmpty())
        return;
    m_current = (m_current + delta + int(m_items.size())) % int(m_items.size());
    ensureVisible(m_current);
    update();
}

QString CommandPopup::currentId() const
{
    return m_current >= 0 && m_current < m_items.size() ? m_items[m_current].id : QString();
}

void CommandPopup::activateCurrent()
{
    const QString id = currentId();
    if (!id.isEmpty())
        emit activated(id);
}

void CommandPopup::ensureVisible(int item)
{
    const int viewH = cardRect().height();
    for (const Row &r : m_rows) {
        if (r.item != item)
            continue;
        int top = r.y;
        // Keep the section header with its first item.
        for (const Row &h : m_rows)
            if (h.item == -1 && h.y + h.h == r.y)
                top = h.y;
        if (top - m_scroll < 0)
            m_scroll = top - 4;
        else if (r.y + r.h - m_scroll > viewH)
            m_scroll = r.y + r.h - viewH + 6;
    }
    m_scroll = qBound(0, m_scroll, qMax(0, m_contentHeight - viewH));
}

int CommandPopup::rowAt(const QPoint &pos) const
{
    const QRect card = cardRect();
    if (!card.contains(pos))
        return -1;
    const int y = pos.y() - card.top() + m_scroll;
    for (const Row &r : m_rows)
        if (r.item >= 0 && y >= r.y && y < r.y + r.h)
            return r.item;
    return -1;
}

void CommandPopup::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double t = m_fade.state() == QAbstractAnimation::Running ? m_fade.currentValue().toDouble() : 1.0;
    p.setOpacity(t);
    const QRectF card = cardRect();
    p.translate(0, (1.0 - t) * -4);
    Theme::paintSoftShadow(p, card, 12, kShadow - 2, 1.4);
    Theme::paintCard(p, card, 12, c.slate);

    if (m_items.isEmpty()) {
        p.setPen(c.fog);
        p.setFont(Theme::uiFont(13));
        p.drawText(card, Qt::AlignCenter, m_emptyText.isEmpty() ? tr("No results") : m_emptyText);
        return;
    }
    p.save();
    QPainterPath clip;
    clip.addRoundedRect(card.adjusted(1, 1, -1, -1), 11, 11);
    p.setClipPath(clip);
    p.translate(card.left(), card.top() - m_scroll);
    const double w = card.width();
    for (const Row &r : m_rows) {
        if (r.y + r.h < m_scroll || r.y > m_scroll + card.height())
            continue;
        if (r.item < 0) {
            p.setPen(c.fog);
            p.setFont(Theme::uiFont(11.5, QFont::Medium));
            p.drawText(QRectF(14, r.y + 8, w - 28, 18), Qt::AlignLeft | Qt::AlignVCenter, r.header);
            continue;
        }
        const MenuItem &it = m_items[r.item];
        const QRectF row(6, r.y, w - 12, r.h);
        if (r.item == m_current) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.graphite);
            p.drawRoundedRect(row, 7, 7);
        }
        const bool large = m_largeIcons && !it.subtitle.isEmpty();
        double x = row.left() + 8;
        if (large) {
            const QRectF tile(x, row.center().y() - 17, 34, 34);
            p.setPen(QPen(c.ash, 1));
            p.setBrush(QColor(0x22, 0x22, 0x27));
            p.drawRoundedRect(tile.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
            if (it.pageIcon)
                Icons::paintPageIcon(p, tile.adjusted(8, 8, -8, -8), it.icon, c.moon, QStringLiteral("page"));
            else
                Icons::paint(p, tile.adjusted(8, 8, -8, -8), it.icon, c.moon);
            x = tile.right() + 11;
        } else if (!it.icon.isEmpty()) {
            const QRectF iconRect(x, row.center().y() - 9, 18, 18);
            if (it.pageIcon)
                Icons::paintPageIcon(p, iconRect, it.icon, c.mist, QStringLiteral("page"));
            else
                Icons::paint(p, iconRect, it.icon, c.mist);
            x = iconRect.right() + 10;
        }
        double right = row.right() - 10;
        if (!it.hint.isEmpty()) {
            p.setFont(Theme::uiFont(11.5));
            const double hw = QFontMetricsF(p.font()).horizontalAdvance(it.hint);
            p.setPen(c.dust);
            p.drawText(QRectF(right - hw, row.top(), hw, row.height()), Qt::AlignVCenter | Qt::AlignRight, it.hint);
            right -= hw + 10;
        }
        const QFont titleFont = Theme::uiFont(13.5, QFont::Medium);
        p.setFont(titleFont);
        p.setPen(r.item == m_current ? c.starlight : c.moon);
        if (large) {
            p.drawText(QRectF(x, row.top() + 6, right - x, 19), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(titleFont).elidedText(it.title, Qt::ElideRight, right - x));
            p.setFont(Theme::uiFont(11.5));
            p.setPen(c.fog);
            p.drawText(QRectF(x, row.top() + 24, right - x, 16), Qt::AlignLeft | Qt::AlignVCenter,
                       QFontMetricsF(p.font()).elidedText(it.subtitle, Qt::ElideRight, right - x));
        } else {
            QString title = it.title;
            const QFontMetricsF fm(titleFont);
            if (!it.subtitle.isEmpty()) {
                const double tw = qMin(fm.horizontalAdvance(title), (right - x) * 0.6);
                p.drawText(QRectF(x, row.top(), tw, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(title, Qt::ElideRight, tw));
                p.setFont(Theme::uiFont(12));
                p.setPen(c.dust);
                const double sx = x + tw + 8;
                p.drawText(QRectF(sx, row.top(), right - sx, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                           QFontMetricsF(p.font()).elidedText(it.subtitle, Qt::ElideLeft, right - sx));
            } else {
                p.drawText(QRectF(x, row.top(), right - x, row.height()), Qt::AlignLeft | Qt::AlignVCenter,
                           fm.elidedText(title, Qt::ElideRight, right - x));
            }
        }
    }
    p.restore();
    // Scroll hint.
    if (m_contentHeight > card.height()) {
        const double viewH = card.height();
        const double barH = qMax(24.0, viewH * viewH / m_contentHeight);
        const double barY = card.top() + (viewH - barH) * m_scroll / qMax(1, m_contentHeight - int(viewH));
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(c.smoke, 0.8));
        p.drawRoundedRect(QRectF(card.right() - 5, barY + 4, 3, barH - 8), 1.5, 1.5);
    }
}

void CommandPopup::mouseMoveEvent(QMouseEvent *e)
{
    const int r = rowAt(e->position().toPoint());
    if (r >= 0 && r != m_current) {
        m_current = r;
        update();
    }
}

void CommandPopup::mousePressEvent(QMouseEvent *e)
{
    const int r = rowAt(e->position().toPoint());
    if (r >= 0) {
        m_current = r;
        activateCurrent();
    }
}

void CommandPopup::wheelEvent(QWheelEvent *e)
{
    m_scroll = qBound(0, m_scroll - e->angleDelta().y() / 3, qMax(0, m_contentHeight - cardRect().height()));
    update();
}

void CommandPopup::leaveEvent(QEvent *)
{
    update();
}

// ---- FormatBar -------------------------------------------------------------------

FormatBar::FormatBar(QWidget *host)
    : QWidget(host)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    hide();
    m_fade.setDuration(Theme::animMs(120));
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    m_fade.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this] { update(); });

    m_link = new QLineEdit(this);
    m_link->setPlaceholderText(tr("Paste or type a link…"));
    m_link->setFont(Theme::uiFont(13));
    m_link->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0 4px; }"));
    m_link->hide();
    m_link->installEventFilter(this);
}

QRect FormatBar::cardRect() const
{
    return rect().adjusted(kShadow, kShadow - 4, -kShadow, -kShadow - 4);
}

void FormatBar::layoutButtons()
{
    m_buttons.clear();
    auto add = [&](const QString &id, const QString &icon, const QString &text, bool active, bool sep = false) {
        Button b;
        b.id = id;
        b.icon = icon;
        b.text = text;
        b.active = active;
        b.separatorAfter = sep;
        m_buttons.append(b);
    };
    add(QStringLiteral("turn"), QString(), m_state.typeName, false, true);
    add(QStringLiteral("bold"), QString(), QStringLiteral("B"), m_state.bold);
    add(QStringLiteral("italic"), QString(), QStringLiteral("i"), m_state.italic);
    add(QStringLiteral("underline"), QString(), QStringLiteral("U"), m_state.underline);
    add(QStringLiteral("strike"), QString(), QStringLiteral("S"), m_state.strike);
    add(QStringLiteral("code"), QStringLiteral("code"), QString(), m_state.code, true);
    add(QStringLiteral("link"), QStringLiteral("link"), QString(), m_state.link);
    add(QStringLiteral("mention"), QStringLiteral("external"), QString(), false, true);
    add(QStringLiteral("shade"), QString(), QStringLiteral("A"), false);
    double x = kShadow + 6;
    const double y = kShadow - 4 + 5;
    const QFontMetricsF fm(Theme::uiFont(13, QFont::Medium));
    for (Button &b : m_buttons) {
        double w = 30;
        if (b.id == QLatin1String("turn"))
            w = fm.horizontalAdvance(b.text) + 30;
        b.rect = QRectF(x, y, w, 30);
        x += w + (b.separatorAfter ? 9 : 2);
    }
}

void FormatBar::showFor(const QRect &sel, const State &state)
{
    m_state = state;
    if (m_link->isVisible())
        return;
    layoutButtons();
    const double contentW = m_buttons.isEmpty() ? 200 : m_buttons.last().rect.right() + 6 - kShadow;
    const int w = int(contentW) + 2 * kShadow;
    const int h = 40 + 2 * kShadow;
    QWidget *host = parentWidget();
    int x = qBound(4, sel.center().x() - w / 2, qMax(4, host->width() - w - 4));
    int y = sel.top() - h + kShadow - 6;
    if (y < 4)
        y = sel.bottom() + 6 - kShadow + 4;
    setGeometry(x, y, w, h);
    if (!isVisible()) {
        show();
        raise();
        m_fade.stop();
        m_fade.start();
    }
    update();
}

void FormatBar::dismiss()
{
    m_link->hide();
    hide();
}

bool FormatBar::isEditingLink() const
{
    return m_link->isVisible();
}

void FormatBar::editLink(const QString &current)
{
    const QRect card = cardRect();
    const int w = 340 + 2 * kShadow;
    setGeometry(x() + width() / 2 - w / 2, y(), w, height());
    m_link->setGeometry(cardRect().adjusted(12, 6, -12, -6));
    m_link->setText(current);
    m_link->show();
    m_link->setFocus();
    m_link->selectAll();
    Q_UNUSED(card)
    update();
}

bool FormatBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_link && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            const QString url = m_link->text().trimmed();
            m_link->hide();
            hide();
            emit linkEntered(url);
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            m_link->hide();
            hide();
            emit linkEntered(QStringLiteral("\x1b"));
            return true;
        }
    }
    if (watched == m_link && event->type() == QEvent::FocusOut && m_link->isVisible()) {
        m_link->hide();
        hide();
    }
    return QWidget::eventFilter(watched, event);
}

void FormatBar::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double t = m_fade.state() == QAbstractAnimation::Running ? m_fade.currentValue().toDouble() : 1.0;
    p.setOpacity(t);
    const QRectF card = cardRect();
    Theme::paintSoftShadow(p, card, 10, kShadow - 2, 1.4);
    Theme::paintCard(p, card, 10, c.slate);
    if (m_link->isVisible()) {
        Icons::paint(p, QRectF(card.left() - 4, card.center().y() - 8, 0, 16), QStringLiteral("link"), c.fog);
        return;
    }
    for (int i = 0; i < m_buttons.size(); ++i) {
        const Button &b = m_buttons[i];
        if (i == m_hover || b.active) {
            p.setPen(Qt::NoPen);
            p.setBrush(b.active ? Theme::withAlpha(QColor(255, 255, 255), 0.09) : Theme::withAlpha(QColor(255, 255, 255), 0.06));
            p.drawRoundedRect(b.rect, 6, 6);
        }
        const QColor fg = b.active ? c.starlight : (i == m_hover ? c.starlight : c.moon);
        if (!b.icon.isEmpty()) {
            Icons::paint(p, b.rect.adjusted(7, 7, -7, -7), b.icon, fg);
        } else if (b.id == QLatin1String("turn")) {
            p.setPen(fg);
            p.setFont(Theme::uiFont(13, QFont::Medium));
            p.drawText(b.rect.adjusted(10, 0, -20, 0), Qt::AlignVCenter | Qt::AlignLeft, b.text);
            Icons::paint(p, QRectF(b.rect.right() - 20, b.rect.center().y() - 6, 12, 12), QStringLiteral("chevron-down"), c.fog);
        } else {
            QFont f(Theme::uiFamily());
            f.setPixelSize(15);
            f.setWeight(b.id == QLatin1String("bold") ? QFont::Bold : QFont::Medium);
            f.setItalic(b.id == QLatin1String("italic"));
            f.setUnderline(b.id == QLatin1String("underline"));
            f.setStrikeOut(b.id == QLatin1String("strike"));
            if (b.id == QLatin1String("italic"))
                f.setFamily(Theme::serifFamily());
            p.setFont(f);
            p.setPen(fg);
            p.drawText(b.rect, Qt::AlignCenter, b.text);
            if (b.id == QLatin1String("shade")) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.mist);
                p.drawRoundedRect(QRectF(b.rect.center().x() - 6, b.rect.bottom() - 7, 12, 2.4), 1, 1);
            }
        }
        if (b.separatorAfter) {
            p.setPen(QPen(c.ash, 1));
            const double sx = b.rect.right() + 4.5;
            p.drawLine(QPointF(sx, b.rect.top() + 6), QPointF(sx, b.rect.bottom() - 6));
        }
    }
}

void FormatBar::mouseMoveEvent(QMouseEvent *e)
{
    int hover = -1;
    for (int i = 0; i < m_buttons.size(); ++i)
        if (m_buttons[i].rect.contains(e->position()))
            hover = i;
    if (hover != m_hover) {
        m_hover = hover;
        static const QHash<QString, QString> tips = {
            {QStringLiteral("turn"), tr("Turn into")},          {QStringLiteral("bold"), tr("Bold · Ctrl+B")},
            {QStringLiteral("italic"), tr("Italic · Ctrl+I")},   {QStringLiteral("underline"), tr("Underline · Ctrl+U")},
            {QStringLiteral("strike"), tr("Strikethrough · Ctrl+Shift+S")}, {QStringLiteral("code"), tr("Inline code · Ctrl+E")},
            {QStringLiteral("link"), tr("Link · Ctrl+K")},       {QStringLiteral("mention"), tr("Turn into a sub-page")},
            {QStringLiteral("shade"), tr("Text shade and highlight")}};
        setToolTip(hover >= 0 ? tips.value(m_buttons[hover].id) : QString());
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void FormatBar::mousePressEvent(QMouseEvent *e)
{
    for (const Button &b : m_buttons) {
        if (b.rect.contains(e->position())) {
            emit action(b.id, mapToGlobal(QPoint(int(b.rect.left()), int(b.rect.bottom()) + 4)));
            return;
        }
    }
}

void FormatBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}
