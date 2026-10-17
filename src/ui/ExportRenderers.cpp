#include "ExportRenderers.h"

#include "Mermaid.h"
#include "Tex.h"
#include "core/Exporter.h"

#include <QPainter>

void installExportRenderers()
{
    Exporter::Renderers r;
    // Twice the display size, so the images stay sharp when printed.
    r.equation = [](const QString &tex, bool display, const QColor &color) -> QImage {
        const double px = display ? 38 : 30;
        const Tex::Metrics m = Tex::measure(tex, px, display);
        if (!m.error.isEmpty() || m.width <= 0)
            return {};
        const int pad = 4;
        QImage img(QSize(int(std::ceil(m.width)) + 2 * pad, int(std::ceil(m.ascent + m.descent)) + 2 * pad), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        Tex::paint(p, QPointF(pad, pad + m.ascent), tex, px, color, display);
        return img;
    };
    r.diagram = [](const QString &source, int maxWidth) -> QImage {
        const double px = 28;
        const Mermaid::Info info = Mermaid::measure(source, px, maxWidth);
        if (!info.supported || info.size.isEmpty())
            return {};
        const int pad = 24;
        QImage img((info.size + QSizeF(2 * pad, 2 * pad)).toSize(), QImage::Format_RGB32);
        img.fill(QColor(0x17, 0x17, 0x1b));
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        Mermaid::paint(p, QPointF(pad, pad), source, px, maxWidth);
        return img;
    };
    Exporter::setRenderers(r);
}
