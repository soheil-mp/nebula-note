#pragma once

#include <QColor>
#include <QPointF>
#include <QSizeF>
#include <QString>

class QPainter;

// Previews Mermaid flowcharts ("graph TD" / "flowchart LR"): rectangles,
// rounded boxes, decisions, circles; arrows, open links, dotted and thick
// links, edge labels and chains — laid out in layers. Other diagram types
// report `supported == false`.
namespace Mermaid
{
struct Info {
    bool supported = false;
    QSizeF size;
    QString error;
};
Info measure(const QString &source, double fontPx, double maxWidth);
void paint(QPainter &p, const QPointF &topLeft, const QString &source, double fontPx, double maxWidth);
} // namespace Mermaid
