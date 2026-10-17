#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>
#include <QRectF>
#include <QString>

class QAction;
class QMenu;
class QPainter;
class QWidget;

// The Nebula design system, shared with the sibling apps: every colour is a
// gray with a faint cool bias. Meaning comes from brightness, weight and shape.
namespace Theme
{
struct Colors {
    QColor voidc;     // deepest: window frame
    QColor abyss;     // sidebar
    QColor basalt;    // page surface
    QColor slate;     // cards, panels, popups
    QColor graphite;  // hover
    QColor ash;       // borders, selection
    QColor smoke;     // strong borders / handles
    QColor dust;      // disabled, faint icons
    QColor fog;       // meta text
    QColor haze;      // secondary text
    QColor mist;      // tertiary emphasis
    QColor moon;      // body text
    QColor starlight; // emphasis
    QColor hairline;  // 1px separators
};
const Colors &colors();

QPalette palette();
QString styleSheet();

// ---- shades (Nebula's replacement for colours) ------------------------------
QColor textShade(int shade); // 0 = default body text
struct Highlight {
    QColor background;
    QColor text; // invalid: keep the text colour
};
Highlight highlightShade(int shade); // 0 = none
struct Pill {
    QColor fill;
    QColor text;
    QColor border;
};
Pill optionPill(int shade);
// A rounded tag; returns its width. `measureOnly` skips painting.
double paintPill(QPainter &p, const QPointF &topLeft, const QString &text, int shade, double height = 22,
                 bool measureOnly = false);
double pillWidth(const QString &text, double height = 22);

// ---- fonts --------------------------------------------------------------------
QString uiFamily();
QString displayFamily();
QString serifFamily();
QString monoFamily();
QFont uiFont(double pixelSize = 13, int weight = QFont::Normal);
QFont displayFont(double pixelSize, int weight = QFont::Bold);
QFont monoFont(double pixelSize);
// The family for a page font option ("sans", "serif", "mono").
QString pageFamily(const QString &option);

// ---- helpers ------------------------------------------------------------------
QColor withAlpha(QColor c, double alpha);
QColor mix(const QColor &a, const QColor &b, double t);
// Word-wraps `text` into at most `maxLines` lines of `width`, eliding the last.
QStringList wrapLines(const QFont &font, const QString &text, double width, int maxLines = 2);

void paintSoftShadow(QPainter &p, const QRectF &rect, double radius, int spread, double strength = 1.0);
// Raised card: slate fill, hairline border and a faint rim light.
void paintCard(QPainter &p, const QRectF &rect, double radius = 12, const QColor &fill = QColor());
// Rounded hover background used by buttons and rows.
void paintHover(QPainter &p, const QRectF &rect, double amount, double radius = 6);

// Rounded, frameless menus with the Nebula look.
void polishMenu(QMenu *menu);
QMenu *makeMenu(QWidget *parent);
// A small caption heading a group of menu items (instead of QMenu::addSection).
QAction *addMenuSection(QMenu *menu, const QString &text);

constexpr int kRadius = 12; // window corners
int animMs(int ms);         // ms, or 0 when reduced motion is on
}
