#include "Style.h"

#include "Icons.h"
#include "Theme.h"

#include <QPainter>
#include <QStyleFactory>
#include <QStyleOptionMenuItem>

namespace
{
constexpr int kItemH = 30;
constexpr int kPadX = 10;     // inside the rounded hover
constexpr int kIconCol = 26;  // icon column, when the menu has icons
constexpr int kCheckCol = 22; // check column, when it has checkable items
constexpr int kShortcutGap = 32;
constexpr int kArrowCol = 22;

QFont shortcutFont(const QFont &base)
{
    QFont f = base;
    f.setPixelSize(qMax(1, (base.pixelSize() > 0 ? base.pixelSize() : 13) - 1));
    return f;
}
} // namespace

NebulaStyle::NebulaStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion")))
{
}

void NebulaStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
    if (element != CE_MenuItem || !mi || mi->menuItemType == QStyleOptionMenuItem::Scroller
        || mi->menuItemType == QStyleOptionMenuItem::TearOff || mi->menuItemType == QStyleOptionMenuItem::EmptyArea) {
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }
    const auto &c = Theme::colors();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRect r = mi->rect.adjusted(1, 0, -1, 0);
    if (mi->menuItemType == QStyleOptionMenuItem::Separator) {
        painter->setPen(QPen(c.ash, 1));
        const double y = r.center().y() + 0.5;
        painter->drawLine(QPointF(r.left() + 7, y), QPointF(r.right() - 7, y));
        painter->restore();
        return;
    }
    const bool enabled = mi->state & State_Enabled;
    const bool selected = enabled && (mi->state & State_Selected);
    if (selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(c.graphite);
        painter->drawRoundedRect(QRectF(r).adjusted(0, 1, 0, -1), 6, 6);
    }
    const QColor text = !enabled ? c.dust : selected ? c.starlight : c.moon;
    int x = r.left() + kPadX;
    if (mi->menuHasCheckableItems) {
        if (mi->checked)
            Icons::paint(*painter, QRectF(x, r.center().y() - 7, 14, 14), QStringLiteral("check"), text);
        x += kCheckCol;
    }
    if (mi->maxIconWidth > 0) {
        if (!mi->icon.isNull()) {
            const QRect ir(x, r.center().y() - 8, 16, 16);
            mi->icon.paint(painter, ir, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
        }
        x += kIconCol;
    }
    QString label = mi->text;
    QString shortcut;
    if (const int tab = label.indexOf(QLatin1Char('\t')); tab >= 0) {
        shortcut = label.mid(tab + 1);
        label = label.left(tab);
    }
    int right = r.right() - kPadX;
    if (mi->menuItemType == QStyleOptionMenuItem::SubMenu) {
        Icons::paint(*painter, QRectF(right - 12, r.center().y() - 6, 12, 12), QStringLiteral("chevron-right"), selected ? c.mist : c.fog);
        right -= kArrowCol - 4;
    }
    if (!shortcut.isEmpty()) {
        const QFont sf = shortcutFont(mi->font);
        painter->setFont(sf);
        painter->setPen(enabled ? c.fog : c.ash);
        const int sw = QFontMetrics(sf).horizontalAdvance(shortcut);
        painter->drawText(QRect(right - sw, r.top(), sw, r.height()), Qt::AlignVCenter | Qt::AlignRight, shortcut);
        right -= sw + 16;
    }
    painter->setFont(mi->font);
    painter->setPen(text);
    const QFontMetrics fm(mi->font);
    int flags = Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine;
    flags |= styleHint(SH_UnderlineShortcut, mi, widget) ? Qt::TextShowMnemonic : Qt::TextHideMnemonic;
    painter->drawText(QRect(x, r.top(), right - x, r.height()), flags, fm.elidedText(label, Qt::ElideRight, right - x, Qt::TextShowMnemonic));
    painter->restore();
}

QSize NebulaStyle::sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget) const
{
    const auto *mi = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
    if (type != CT_MenuItem || !mi)
        return QProxyStyle::sizeFromContents(type, option, size, widget);
    if (mi->menuItemType == QStyleOptionMenuItem::Separator)
        return {qMax(size.width(), 40), 9};
    QString label = mi->text;
    QString shortcut;
    if (const int tab = label.indexOf(QLatin1Char('\t')); tab >= 0) {
        shortcut = label.mid(tab + 1);
        label = label.left(tab);
    }
    const QFontMetrics fm(mi->font);
    int w = 2 * kPadX + 8 + fm.horizontalAdvance(QString(label).remove(QLatin1Char('&')));
    if (mi->menuHasCheckableItems)
        w += kCheckCol;
    if (mi->maxIconWidth > 0)
        w += kIconCol;
    // Shortcut column: as wide as the widest in the menu, so labels line up.
    const int sw = qMax(mi->reservedShortcutWidth, shortcut.isEmpty() ? 0 : QFontMetrics(shortcutFont(mi->font)).horizontalAdvance(shortcut));
    if (sw > 0)
        w += kShortcutGap + sw;
    if (mi->menuItemType == QStyleOptionMenuItem::SubMenu)
        w += kArrowCol;
    return {qMax(w, 120), kItemH};
}

void NebulaStyle::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
    if (element == PE_PanelMenu || element == PE_FrameMenu) {
        // A rounded card; the corners show through where the menu window is
        // translucent (Theme::polishMenu).
        if (element == PE_FrameMenu)
            return;
        const auto &c = Theme::colors();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(c.ash, 1));
        painter->setBrush(c.slate);
        const bool translucent = widget && widget->testAttribute(Qt::WA_TranslucentBackground);
        const QRectF r = QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        if (translucent)
            painter->drawRoundedRect(r, 10, 10);
        else
            painter->drawRect(r);
        painter->restore();
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

int NebulaStyle::pixelMetric(PixelMetric metric, const QStyleOption *option, const QWidget *widget) const
{
    switch (metric) {
    case PM_MenuPanelWidth:
        return 1;
    case PM_MenuHMargin:
    case PM_MenuVMargin:
        return 4;
    case PM_SubMenuOverlap:
        return -2;
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}
