#include "Theme.h"

#include "core/Settings.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>

namespace Theme
{

const Colors &colors()
{
    static const Colors c = [] {
        Colors c;
        c.voidc = QColor(0x09, 0x09, 0x0B);
        c.abyss = QColor(0x0E, 0x0E, 0x11);
        c.basalt = QColor(0x13, 0x13, 0x16);
        c.slate = QColor(0x1A, 0x1A, 0x1E);
        c.graphite = QColor(0x23, 0x23, 0x28);
        c.ash = QColor(0x2E, 0x2E, 0x35);
        c.smoke = QColor(0x3A, 0x3A, 0x42);
        c.dust = QColor(0x4A, 0x4A, 0x53);
        c.fog = QColor(0x6B, 0x6B, 0x75);
        c.haze = QColor(0x8A, 0x8A, 0x94);
        c.mist = QColor(0xA9, 0xA9, 0xB2);
        c.moon = QColor(0xC9, 0xC9, 0xD0);
        c.starlight = QColor(0xF1, 0xF1, 0xF5);
        c.hairline = QColor(0x1E, 0x1E, 0x23);
        return c;
    }();
    return c;
}

QColor withAlpha(QColor c, double alpha)
{
    c.setAlphaF(float(qBound(0.0, alpha, 1.0)));
    return c;
}

QColor mix(const QColor &a, const QColor &b, double t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t),
                            float(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QPalette palette()
{
    const auto &c = colors();
    QPalette p;
    p.setColor(QPalette::Window, c.basalt);
    p.setColor(QPalette::WindowText, c.moon);
    p.setColor(QPalette::Base, c.basalt);
    p.setColor(QPalette::AlternateBase, c.slate);
    p.setColor(QPalette::Text, c.moon);
    p.setColor(QPalette::PlaceholderText, c.dust);
    p.setColor(QPalette::Button, c.graphite);
    p.setColor(QPalette::ButtonText, c.moon);
    p.setColor(QPalette::BrightText, c.starlight);
    p.setColor(QPalette::Highlight, QColor(0x45, 0x45, 0x50));
    p.setColor(QPalette::HighlightedText, c.starlight);
    p.setColor(QPalette::ToolTipBase, c.slate);
    p.setColor(QPalette::ToolTipText, c.moon);
    p.setColor(QPalette::Link, c.starlight);
    p.setColor(QPalette::LinkVisited, c.mist);
    p.setColor(QPalette::Light, c.smoke);
    p.setColor(QPalette::Midlight, c.ash);
    p.setColor(QPalette::Mid, c.graphite);
    p.setColor(QPalette::Dark, c.abyss);
    p.setColor(QPalette::Shadow, c.voidc);
    p.setColor(QPalette::Accent, c.mist);
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.dust);
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.graphite);
    return p;
}

QString styleSheet()
{
    const auto &c = colors();
    auto hex = [](const QColor &col) { return col.name(QColor::HexArgb); };
    return QStringLiteral(R"(
QToolTip { background: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 5px 8px; font-size: 12px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px 2px 2px 0; }
QScrollBar::handle:vertical { background: %4; border-radius: 3px; min-height: 36px; margin: 0 2px; }
QScrollBar::handle:vertical:hover { background: %5; margin: 0 1px; border-radius: 4px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0 2px 2px 2px; }
QScrollBar::handle:horizontal { background: %4; border-radius: 3px; min-width: 36px; margin: 2px 0; }
QScrollBar::handle:horizontal:hover { background: %5; margin: 1px 0; border-radius: 4px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QLineEdit { background: %6; color: %2; border: 1px solid %3; border-radius: 7px; padding: 5px 9px;
            selection-background-color: #45454f; selection-color: %7; }
QLineEdit:focus { border-color: %5; }
QAbstractScrollArea { background: transparent; border: none; }
QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 5px; }
QMenu::item { padding: 6px 26px 6px 10px; border-radius: 6px; margin: 0 1px; }
QMenu::item:selected { background: %6; color: %7; }
QMenu::item:disabled { color: %8; }
QMenu::icon { padding-left: 8px; }
QMenu::separator { height: 1px; background: %3; margin: 5px 6px; }
QMenu::right-arrow { width: 0; }
QCheckBox { color: %2; spacing: 8px; }
QComboBox { background: %6; color: %2; border: 1px solid %3; border-radius: 7px; padding: 4px 10px; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3; selection-background-color: %6; outline: none; }
QSlider::groove:horizontal { height: 4px; background: %3; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %9; border-radius: 2px; }
QSlider::handle:horizontal { background: %7; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }
)")
        .arg(hex(c.slate), hex(c.moon), hex(c.ash), hex(withAlpha(c.smoke, 0.75)), hex(c.dust), hex(c.graphite),
             hex(c.starlight), hex(c.dust), hex(c.mist));
}

// ---- shades -------------------------------------------------------------------

QColor textShade(int shade)
{
    const auto &c = colors();
    switch (shade) {
    case 1:
        return c.starlight;
    case 2:
        return c.mist;
    case 3:
        return c.haze;
    case 4:
        return c.fog;
    case 5:
        return c.dust;
    default:
        return c.moon;
    }
}

Highlight highlightShade(int shade)
{
    switch (shade) {
    case 1:
        return {QColor(0x26, 0x26, 0x2C), QColor()};
    case 2:
        return {QColor(0x32, 0x32, 0x3A), QColor()};
    case 3:
        return {QColor(0x40, 0x40, 0x49), QColor(0xF1, 0xF1, 0xF5)};
    case 4:
        return {QColor(0x5A, 0x5A, 0x64), QColor(0xF1, 0xF1, 0xF5)};
    case 5:
        return {QColor(0xC9, 0xC9, 0xD0), QColor(0x13, 0x13, 0x16)};
    case 6:
        return {QColor(0xF1, 0xF1, 0xF5), QColor(0x09, 0x09, 0x0B)};
    default:
        return {QColor(), QColor()};
    }
}

Pill optionPill(int shade)
{
    switch (shade) {
    case 0:
        return {QColor(0x26, 0x26, 0x2C), QColor(0xC9, 0xC9, 0xD0), QColor(0x33, 0x33, 0x3A)};
    case 1:
        return {QColor(0x31, 0x31, 0x39), QColor(0xD6, 0xD6, 0xDC), QColor(0x3D, 0x3D, 0x46)};
    case 2:
        return {QColor(0x3E, 0x3E, 0x47), QColor(0xE6, 0xE6, 0xEB), QColor(0x4A, 0x4A, 0x54)};
    case 3:
        return {QColor(0x52, 0x52, 0x5B), QColor(0xF1, 0xF1, 0xF5), QColor(0x5E, 0x5E, 0x68)};
    case 4:
        return {QColor(0x70, 0x70, 0x79), QColor(0x0B, 0x0B, 0x0E), QColor(0x7C, 0x7C, 0x86)};
    case 5:
        return {QColor(0x8E, 0x8E, 0x98), QColor(0x0B, 0x0B, 0x0E), QColor(0x9A, 0x9A, 0xA4)};
    case 6:
        return {QColor(0xB0, 0xB0, 0xB9), QColor(0x13, 0x13, 0x16), QColor(0xBC, 0xBC, 0xC5)};
    case 7:
        return {QColor(0xD2, 0xD2, 0xD8), QColor(0x13, 0x13, 0x16), QColor(0xDC, 0xDC, 0xE2)};
    case 8:
        return {QColor(0xF1, 0xF1, 0xF5), QColor(0x09, 0x09, 0x0B), QColor(0xFF, 0xFF, 0xFF)};
    default:
        return {QColor(0, 0, 0, 0), QColor(0xC9, 0xC9, 0xD0), QColor(0x8A, 0x8A, 0x94)};
    }
}

double pillWidth(const QString &text, double height)
{
    const QFont f = uiFont(qMax(11.0, height * 0.56), QFont::Medium);
    return QFontMetricsF(f).horizontalAdvance(text) + height * 0.72;
}

double paintPill(QPainter &p, const QPointF &topLeft, const QString &text, int shade, double height, bool measureOnly)
{
    const double w = pillWidth(text, height);
    if (measureOnly)
        return w;
    const Pill pill = optionPill(shade);
    const QRectF r(topLeft, QSizeF(w, height));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(pill.border, 1));
    p.setBrush(pill.fill);
    if (shade == 9) {
        QPen dashed(pill.border, 1, Qt::DashLine);
        dashed.setDashPattern({3, 2});
        p.setPen(dashed);
    }
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), height * 0.32, height * 0.32);
    p.setPen(pill.text);
    p.setFont(uiFont(qMax(11.0, height * 0.56), QFont::Medium));
    p.drawText(r, Qt::AlignCenter, text);
    p.restore();
    return w;
}

// ---- fonts --------------------------------------------------------------------

namespace
{
QString firstAvailable(const QStringList &candidates)
{
    const QStringList families = QFontDatabase::families();
    for (const QString &c : candidates)
        if (families.contains(c, Qt::CaseInsensitive))
            return c;
    return QApplication::font().family();
}
}

QString uiFamily()
{
    static const QString f = firstAvailable({QStringLiteral("Inter"), QStringLiteral("Inter Variable"), QStringLiteral("Cantarell"),
                                             QStringLiteral("Noto Sans")});
    return f;
}

QString displayFamily()
{
    static const QString f = firstAvailable({QStringLiteral("Inter Display"), QStringLiteral("Inter"), uiFamily()});
    return f;
}

QString serifFamily()
{
    static const QString f = firstAvailable({QStringLiteral("Source Serif 4"), QStringLiteral("Source Serif Pro"),
                                             QStringLiteral("PT Serif"), QStringLiteral("Noto Serif"),
                                             QStringLiteral("Liberation Serif"), QStringLiteral("DejaVu Serif")});
    return f;
}

QString monoFamily()
{
    static const QString f = firstAvailable({QStringLiteral("JetBrains Mono"), QStringLiteral("JetBrainsMono Nerd Font"),
                                             QStringLiteral("JetBrainsMono NF"), QStringLiteral("Fira Code"),
                                             QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Monospace")});
    return f;
}

QString pageFamily(const QString &option)
{
    if (option == QLatin1String("serif"))
        return serifFamily();
    if (option == QLatin1String("mono"))
        return monoFamily();
    return uiFamily();
}

QFont uiFont(double pixelSize, int weight)
{
    QFont f(uiFamily());
    f.setPixelSize(qMax(1, qRound(pixelSize)));
    f.setWeight(QFont::Weight(weight));
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont displayFont(double pixelSize, int weight)
{
    QFont f(displayFamily());
    f.setPixelSize(qMax(1, qRound(pixelSize)));
    f.setWeight(QFont::Weight(weight));
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

QFont monoFont(double pixelSize)
{
    QFont f(monoFamily());
    f.setPixelSize(qMax(1, qRound(pixelSize)));
    f.setStyleHint(QFont::Monospace);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// ---- helpers ------------------------------------------------------------------

int animMs(int ms)
{
    return Settings::instance().reduceMotion ? 0 : ms;
}

void paintSoftShadow(QPainter &p, const QRectF &rect, double radius, int spread, double strength)
{
    p.save();
    p.setPen(Qt::NoPen);
    p.setRenderHint(QPainter::Antialiasing);
    const double dy = spread * 0.3;
    for (int i = spread; i >= 1; --i) {
        const double t = double(i) / spread;
        const double a = strength * 0.075 * (1.0 - t) * (1.0 - t) + 0.004 * strength;
        QPainterPath path;
        path.addRoundedRect(rect.adjusted(-i, -i + dy, i, i + dy), radius + i, radius + i);
        p.setBrush(QColor(0, 0, 0, int(qBound(0.0, a, 1.0) * 255)));
        p.drawPath(path);
    }
    p.restore();
}

void paintCard(QPainter &p, const QRectF &rect, double radius, const QColor &fill)
{
    const auto &c = colors();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(withAlpha(c.ash, 0.9), 1));
    p.setBrush(fill.isValid() ? fill : c.slate);
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    // A faint top highlight, like light catching the rim.
    QLinearGradient g(rect.topLeft(), rect.bottomLeft());
    g.setColorAt(0, QColor(255, 255, 255, 10));
    g.setColorAt(0.35, QColor(255, 255, 255, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(rect.adjusted(1, 1, -1, -1), radius - 1, radius - 1);
    p.restore();
}

void paintHover(QPainter &p, const QRectF &rect, double amount, double radius)
{
    if (amount <= 0.001)
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(withAlpha(QColor(255, 255, 255), 0.055 * amount));
    p.drawRoundedRect(rect, radius, radius);
    p.restore();
}

void polishMenu(QMenu *menu)
{
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setFont(uiFont(13));
}

QMenu *makeMenu(QWidget *parent)
{
    auto *m = new QMenu(parent);
    polishMenu(m);
    m->setAttribute(Qt::WA_DeleteOnClose);
    return m;
}

}
