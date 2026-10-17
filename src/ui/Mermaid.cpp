#include "Mermaid.h"

#include "Theme.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QtMath>

#include <cmath>

namespace Mermaid
{
namespace
{
enum class Shape { Rect, Round, Diamond, Circle, Stadium, Flag };
enum class Line { Arrow, Open, Dotted, Thick };

struct Node {
    QString id;
    QString label;
    Shape shape = Shape::Rect;
    int layer = 0;
    int order = 0;
    QRectF rect;
};

struct Edge {
    int from;
    int to;
    QString label;
    Line line = Line::Arrow;
};

struct Graph {
    bool vertical = true; // TD/TB/BT vs LR/RL
    bool reversed = false;
    QList<Node> nodes;
    QList<Edge> edges;
    QSizeF size;
    QString error;
    bool supported = false;
};

int nodeIndex(Graph &g, const QString &id)
{
    for (int i = 0; i < g.nodes.size(); ++i)
        if (g.nodes[i].id == id)
            return i;
    Node n;
    n.id = id;
    n.label = id;
    g.nodes.append(n);
    return int(g.nodes.size() - 1);
}

// "A[Label]", "B(Label)", "C{Label}", "D((Label))", "E([Label])", "F>Label]".
int parseNode(Graph &g, QString text)
{
    text = text.trimmed();
    static const QRegularExpression re(QStringLiteral("^([A-Za-z0-9_\\-]+)\\s*(\\(\\(.*\\)\\)|\\(\\[.*\\]\\)|\\[.*\\]|\\(.*\\)|\\{.*\\}|>.*\\])?$"));
    const auto m = re.match(text);
    if (!m.hasMatch())
        return -1;
    const int i = nodeIndex(g, m.captured(1));
    const QString body = m.captured(2);
    if (body.isEmpty())
        return i;
    Node &n = g.nodes[i];
    auto strip = [](QString s, int a, int b) {
        s = s.mid(a, s.size() - a - b).trimmed();
        if (s.size() >= 2 && (s.startsWith(QLatin1Char('"')) && s.endsWith(QLatin1Char('"'))))
            s = s.mid(1, s.size() - 2);
        return s;
    };
    if (body.startsWith(QLatin1String("((")))
        n.shape = Shape::Circle, n.label = strip(body, 2, 2);
    else if (body.startsWith(QLatin1String("([")))
        n.shape = Shape::Stadium, n.label = strip(body, 2, 2);
    else if (body.startsWith(QLatin1Char('[')))
        n.shape = Shape::Rect, n.label = strip(body, 1, 1);
    else if (body.startsWith(QLatin1Char('(')))
        n.shape = Shape::Round, n.label = strip(body, 1, 1);
    else if (body.startsWith(QLatin1Char('{')))
        n.shape = Shape::Diamond, n.label = strip(body, 1, 1);
    else if (body.startsWith(QLatin1Char('>')))
        n.shape = Shape::Flag, n.label = strip(body, 1, 1);
    n.label.replace(QStringLiteral("<br>"), QStringLiteral("\n")).replace(QStringLiteral("<br/>"), QStringLiteral("\n"));
    return i;
}

Graph parse(const QString &source)
{
    Graph g;
    QStringList lines = source.split(QRegularExpression(QStringLiteral("[\\n\\x{2028};]")), Qt::SkipEmptyParts);
    while (!lines.isEmpty() && (lines.first().trimmed().isEmpty() || lines.first().trimmed().startsWith(QLatin1String("%%"))))
        lines.removeFirst();
    if (lines.isEmpty())
        return g;
    static const QRegularExpression header(QStringLiteral("^\\s*(graph|flowchart)\\s*(TD|TB|BT|LR|RL)?\\s*$"), QRegularExpression::CaseInsensitiveOption);
    const auto hm = header.match(lines.first());
    if (!hm.hasMatch()) {
        g.error = QObject::tr("Only flowcharts (graph TD / LR) are previewed.");
        return g;
    }
    g.supported = true;
    const QString dir = hm.captured(2).toUpper();
    g.vertical = dir.isEmpty() || dir == QLatin1String("TD") || dir == QLatin1String("TB") || dir == QLatin1String("BT");
    g.reversed = dir == QLatin1String("BT") || dir == QLatin1String("RL");
    // Links: -->, ---, -.->, ==>, with optional |label| or -- label -->.
    static const QRegularExpression link(QStringLiteral("\\s*(-->|---|-\\.->|-\\.-|==>|===|--\\s[^-]+?\\s-->|--\\s[^-]+?\\s---)\\s*(?:\\|([^|]*)\\|)?\\s*"));
    for (int li = 1; li < lines.size(); ++li) {
        QString line = lines[li].trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1String("%%")) || line.startsWith(QLatin1String("classDef"))
            || line.startsWith(QLatin1String("class ")) || line.startsWith(QLatin1String("style ")) || line.startsWith(QLatin1String("subgraph"))
            || line == QLatin1String("end") || line.startsWith(QLatin1String("linkStyle")) || line.startsWith(QLatin1String("click ")))
            continue;
        // Split into node groups and links.
        QList<QStringList> groups;
        QList<QPair<Line, QString>> links;
        int pos = 0;
        auto it = link.globalMatch(line);
        while (it.hasNext()) {
            const auto m = it.next();
            groups.append(line.mid(pos, m.capturedStart() - pos).split(QLatin1Char('&')));
            QString op = m.captured(1).trimmed();
            QString label = m.captured(2).trimmed();
            Line type = Line::Arrow;
            if (op.startsWith(QLatin1String("--")) && op.size() > 3 && op.contains(QLatin1Char(' '))) {
                // "-- label -->"
                label = op.mid(2, op.size() - 5).trimmed();
                type = op.endsWith(QLatin1String("---")) ? Line::Open : Line::Arrow;
            } else if (op == QLatin1String("---") || op == QLatin1String("-.-") || op == QLatin1String("==="))
                type = op == QLatin1String("-.-") ? Line::Dotted : (op == QLatin1String("===") ? Line::Thick : Line::Open);
            else if (op == QLatin1String("-.->"))
                type = Line::Dotted;
            else if (op == QLatin1String("==>"))
                type = Line::Thick;
            links.append({type, label});
            pos = int(m.capturedEnd());
        }
        groups.append(line.mid(pos).split(QLatin1Char('&')));
        QList<QList<int>> ids;
        for (const QStringList &grp : groups) {
            QList<int> nodes;
            for (const QString &t : grp) {
                const int n = parseNode(g, t);
                if (n >= 0)
                    nodes.append(n);
                else if (!t.trimmed().isEmpty())
                    g.error = QObject::tr("Couldn't read “%1”").arg(t.trimmed());
            }
            ids.append(nodes);
        }
        for (int i = 0; i < links.size() && i + 1 < ids.size(); ++i)
            for (int a : ids[i])
                for (int b : ids[i + 1])
                    g.edges.append({a, b, links[i].second, links[i].first});
    }
    return g;
}

void layout(Graph &g, double px, double maxWidth)
{
    if (g.nodes.isEmpty())
        return;
    // Layers: longest path from the sources. Edges back to an earlier node
    // (cycles) don't push it down.
    const int n = int(g.nodes.size());
    for (int iter = 0; iter < n; ++iter) {
        bool changed = false;
        for (const Edge &e : g.edges) {
            if (e.to <= e.from)
                continue;
            if (g.nodes[e.to].layer < g.nodes[e.from].layer + 1) {
                g.nodes[e.to].layer = g.nodes[e.from].layer + 1;
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    int layers = 0;
    for (const Node &node : g.nodes)
        layers = qMax(layers, node.layer + 1);
    QList<QList<int>> byLayer(layers);
    for (int i = 0; i < n; ++i)
        byLayer[g.nodes[i].layer].append(i);
    // Order within a layer by the average position of parents (one pass).
    for (int l = 1; l < layers; ++l) {
        QHash<int, double> score;
        for (int idx : byLayer[l]) {
            double sum = 0;
            int count = 0;
            for (const Edge &e : g.edges)
                if (e.to == idx && g.nodes[e.from].layer == l - 1) {
                    sum += byLayer[l - 1].indexOf(e.from);
                    ++count;
                }
            score.insert(idx, count ? sum / count : 1e6 + idx);
        }
        std::stable_sort(byLayer[l].begin(), byLayer[l].end(), [&](int a, int b) { return score.value(a) < score.value(b); });
    }
    QFont f = Theme::uiFont(px);
    const QFontMetricsF fm(f);
    for (Node &node : g.nodes) {
        double w = 0;
        const QStringList lines = node.label.split(QLatin1Char('\n'));
        for (const QString &l : lines)
            w = qMax(w, fm.horizontalAdvance(l));
        double h = fm.height() * lines.size();
        w += px * 1.8;
        h += px * 1.1;
        if (node.shape == Shape::Diamond) {
            w *= 1.45;
            h *= 1.6;
        } else if (node.shape == Shape::Circle) {
            w = h = qMax(w - px * 0.6, h);
        }
        node.rect = QRectF(0, 0, qMax(w, px * 4), h);
    }
    // Room after each layer for the curves and any edge labels riding on them.
    QList<double> gapAfter(layers, px * 3.4);
    for (const Edge &e : g.edges) {
        if (e.label.isEmpty())
            continue;
        const int l = qMin(g.nodes[e.from].layer, g.nodes[e.to].layer);
        // Vertical flows stack the label between two stretches of curve.
        const double need = g.vertical ? fm.height() * 2 + px * 3.2 : fm.horizontalAdvance(e.label) + px * 3.4;
        gapAfter[l] = qMax(gapAfter[l], need);
    }
    const double gapCross = px * 2.0;
    // Thickness of each layer along the main axis, and span across.
    QList<double> thick(layers, 0), span(layers, 0);
    for (int l = 0; l < layers; ++l) {
        for (int idx : byLayer[l]) {
            const QRectF r = g.nodes[idx].rect;
            thick[l] = qMax(thick[l], g.vertical ? r.height() : r.width());
            span[l] += (g.vertical ? r.width() : r.height()) + gapCross;
        }
        span[l] -= gapCross;
    }
    double maxSpan = 0;
    for (double s : span)
        maxSpan = qMax(maxSpan, s);
    double mainPos = 0;
    for (int ll = 0; ll < layers; ++ll) {
        const int l = g.reversed ? layers - 1 - ll : ll;
        double cross = (maxSpan - span[l]) / 2;
        for (int idx : byLayer[l]) {
            QRectF &r = g.nodes[idx].rect;
            if (g.vertical) {
                r.moveTo(cross, mainPos + (thick[l] - r.height()) / 2);
                cross += r.width() + gapCross;
            } else {
                r.moveTo(mainPos + (thick[l] - r.width()) / 2, cross);
                cross += r.height() + gapCross;
            }
        }
        mainPos += thick[l] + (ll + 1 < layers ? gapAfter[g.reversed ? l - 1 : l] : 0);
    }
    g.size = g.vertical ? QSizeF(maxSpan, mainPos) : QSizeF(mainPos, maxSpan);
    Q_UNUSED(maxWidth)
}

const Graph &cached(const QString &source, double px, double maxWidth)
{
    static QHash<QString, Graph> cache;
    const QString key = QStringLiteral("%1|%2|").arg(px).arg(maxWidth) + source;
    auto it = cache.find(key);
    if (it != cache.end())
        return *it;
    Graph g = parse(source);
    layout(g, px, maxWidth);
    if (cache.size() > 100)
        cache.clear();
    return *cache.insert(key, g);
}

QPointF edgePoint(const QRectF &r, const QPointF &towards)
{
    // Where the line from the centre towards a point leaves the box.
    const QPointF c = r.center();
    const QPointF d = towards - c;
    if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y()))
        return c;
    const double sx = d.x() != 0 ? (r.width() / 2) / qAbs(d.x()) : 1e9;
    const double sy = d.y() != 0 ? (r.height() / 2) / qAbs(d.y()) : 1e9;
    const double s = qMin(sx, sy);
    return c + d * s;
}
} // namespace

Info measure(const QString &source, double fontPx, double maxWidth)
{
    const Graph &g = cached(source, fontPx, maxWidth);
    Info info;
    info.supported = g.supported && !g.nodes.isEmpty();
    info.error = g.error;
    if (info.supported) {
        const double scale = g.size.width() > maxWidth && g.size.width() > 0 ? maxWidth / g.size.width() : 1.0;
        info.size = g.size * scale;
    }
    return info;
}

void paint(QPainter &p, const QPointF &topLeft, const QString &source, double fontPx, double maxWidth)
{
    const Graph &g = cached(source, fontPx, maxWidth);
    if (!g.supported || g.nodes.isEmpty())
        return;
    const auto &c = Theme::colors();
    const double scale = g.size.width() > maxWidth && g.size.width() > 0 ? maxWidth / g.size.width() : 1.0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(topLeft);
    p.scale(scale, scale);
    QFont f = Theme::uiFont(fontPx);
    p.setFont(f);
    const QFontMetricsF fm(f);
    // Edges under the nodes: smooth curves from side to facing side, like
    // Mermaid's own renderer; anything else (same layer, cycles) goes straight.
    for (const Edge &e : g.edges) {
        const QRectF a = g.nodes[e.from].rect, b = g.nodes[e.to].rect;
        QPointF p1, p2, c1, c2;
        if (!g.vertical && (b.left() >= a.right() - 0.5 || a.left() >= b.right() - 0.5)) {
            const bool right = b.left() >= a.right() - 0.5;
            p1 = QPointF(right ? a.right() : a.left(), a.center().y());
            p2 = QPointF(right ? b.left() : b.right(), b.center().y());
            const double d = (p2.x() - p1.x()) * 0.5;
            c1 = p1 + QPointF(d, 0);
            c2 = p2 - QPointF(d, 0);
        } else if (g.vertical && (b.top() >= a.bottom() - 0.5 || a.top() >= b.bottom() - 0.5)) {
            const bool down = b.top() >= a.bottom() - 0.5;
            p1 = QPointF(a.center().x(), down ? a.bottom() : a.top());
            p2 = QPointF(b.center().x(), down ? b.top() : b.bottom());
            const double d = (p2.y() - p1.y()) * 0.5;
            c1 = p1 + QPointF(0, d);
            c2 = p2 - QPointF(0, d);
        } else {
            p1 = edgePoint(a, b.center());
            p2 = edgePoint(b, a.center());
            c1 = p1 + (p2 - p1) / 3;
            c2 = p1 + (p2 - p1) * 2 / 3;
        }
        QPainterPath path(p1);
        path.cubicTo(c1, c2, p2);
        QPen pen(c.haze, e.line == Line::Thick ? 2.4 : 1.3);
        pen.setCapStyle(Qt::RoundCap);
        if (e.line == Line::Dotted)
            pen.setDashPattern({3, 3});
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
        if (e.line != Line::Open) {
            QPointF tangent = p2 - c2;
            if (std::hypot(tangent.x(), tangent.y()) < 0.01)
                tangent = p2 - p1;
            const QPointF dir = tangent / qMax(0.01, std::hypot(tangent.x(), tangent.y()));
            const QPointF normal(-dir.y(), dir.x());
            const double s = fontPx * 0.5;
            QPainterPath head;
            head.moveTo(p2);
            head.lineTo(p2 - dir * s + normal * s * 0.5);
            head.lineTo(p2 - dir * s * 0.75);
            head.lineTo(p2 - dir * s - normal * s * 0.5);
            head.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(c.haze);
            p.drawPath(head);
        }
        if (!e.label.isEmpty()) {
            const QPointF mid = path.pointAtPercent(0.5);
            const double w = fm.horizontalAdvance(e.label) + fontPx * 0.8;
            const QRectF lr(mid.x() - w / 2, mid.y() - fm.height() / 2 - 1, w, fm.height() + 2);
            p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.08), 1));
            p.setBrush(QColor(0x17, 0x17, 0x1C));
            p.drawRoundedRect(lr, lr.height() / 2, lr.height() / 2);
            p.setPen(c.mist);
            p.drawText(lr, Qt::AlignCenter, e.label);
        }
    }
    for (const Node &n : g.nodes) {
        const QRectF r = n.rect;
        p.setPen(QPen(c.smoke, 1.2));
        p.setBrush(QColor(0x1E, 0x1E, 0x25));
        switch (n.shape) {
        case Shape::Rect:
            p.drawRoundedRect(r, 3, 3);
            break;
        case Shape::Round:
            p.drawRoundedRect(r, fontPx * 0.6, fontPx * 0.6);
            break;
        case Shape::Stadium:
            p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
            break;
        case Shape::Circle:
            p.drawEllipse(r);
            break;
        case Shape::Diamond: {
            QPolygonF poly({QPointF(r.center().x(), r.top()), QPointF(r.right(), r.center().y()), QPointF(r.center().x(), r.bottom()),
                            QPointF(r.left(), r.center().y())});
            p.drawPolygon(poly);
            break;
        }
        case Shape::Flag: {
            QPolygonF poly({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), QPointF(r.left() + fontPx * 0.6, r.center().y())});
            p.drawPolygon(poly);
            break;
        }
        }
        p.setPen(c.starlight);
        p.drawText(r, Qt::AlignCenter, n.label);
    }
    p.restore();
}
} // namespace Mermaid
