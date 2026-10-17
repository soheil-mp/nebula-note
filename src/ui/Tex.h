#pragma once

#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QString>

class QPainter;

// A small TeX math typesetter for equation blocks and inline equations
// (Notion uses KaTeX syntax): fractions, roots, scripts, big operators with
// limits, \left…\right delimiters, matrices and cases, accents, fonts
// (\mathbf, \mathbb, \mathcal, \text…), Greek letters and the usual
// symbols, with TeX's spacing between atoms.
namespace Tex
{
struct Metrics {
    double width = 0;
    double ascent = 0;  // above the baseline
    double descent = 0; // below it
    QString error;      // empty when the source parsed cleanly
};

// `px` is the base font size; `display` lays out like $$…$$ (limits above
// and below sums, full-size fractions).
Metrics measure(const QString &tex, double px, bool display);
// Draws with the baseline starting at `origin`.
void paint(QPainter &p, const QPointF &origin, const QString &tex, double px, const QColor &color, bool display);
// The font family used for math.
QString mathFamily();
} // namespace Tex
