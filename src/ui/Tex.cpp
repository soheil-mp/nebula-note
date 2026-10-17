#include "Tex.h"

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <functional>
#include <memory>

namespace Tex
{
namespace
{
// ---- fonts ------------------------------------------------------------------------------

QString pickFamily()
{
    const QStringList available = QFontDatabase::families();
    for (const char *f : {"STIX Two Math", "STIX Two Text", "Latin Modern Math", "Latin Modern Roman", "TeX Gyre Termes",
                          "DejaVu Serif", "Noto Serif", "Liberation Serif", "Times New Roman"}) {
        if (available.contains(QLatin1String(f)))
            return QString::fromLatin1(f);
    }
    return QStringLiteral("serif");
}

enum class Shape { Italic, Upright, Bold, BoldItalic };

QFont mathFont(double px, Shape shape)
{
    QFont f(mathFamily());
    f.setPixelSize(qMax(1, qRound(px)));
    f.setItalic(shape == Shape::Italic || shape == Shape::BoldItalic);
    f.setBold(shape == Shape::Bold || shape == Shape::BoldItalic);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// ---- boxes ------------------------------------------------------------------------------

enum class Kind { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };

struct Box {
    double w = 0, a = 0, d = 0;
    Kind kind = Kind::Ord;
    bool limits = false; // big operator taking limits above/below in display style
    virtual ~Box() = default;
    virtual void draw(QPainter &p, double x, double y) const = 0; // y: baseline
};
using BoxP = std::shared_ptr<Box>;

struct GlyphBox : Box {
    QString text;
    QFont font;
    double xOffset = 0;
    void draw(QPainter &p, double x, double y) const override
    {
        p.setFont(font);
        p.drawText(QPointF(x + xOffset, y), text);
    }
};

struct SpaceBox : Box {
    void draw(QPainter &, double, double) const override {}
};

struct HBox : Box {
    QList<BoxP> items;
    QList<double> xs;
    void draw(QPainter &p, double x, double y) const override
    {
        for (int i = 0; i < items.size(); ++i)
            items[i]->draw(p, x + xs[i], y);
    }
};

struct ShiftBox : Box {
    BoxP inner;
    double dx = 0, dy = 0; // dy > 0 raises
    void draw(QPainter &p, double x, double y) const override { inner->draw(p, x + dx, y - dy); }
};

struct RuleBox : Box {
    double thickness = 1;
    double raise = 0; // centre of the rule above the baseline
    void draw(QPainter &p, double x, double y) const override
    {
        p.fillRect(QRectF(x, y - raise - thickness / 2, w, thickness), p.pen().color());
    }
};

struct PathBox : Box {
    std::function<void(QPainter &, double x, double y)> painter;
    void draw(QPainter &p, double x, double y) const override { painter(p, x, y); }
};

BoxP space(double w)
{
    auto b = std::make_shared<SpaceBox>();
    b->w = w;
    return b;
}

BoxP glyph(const QString &text, const QFont &font, Kind kind = Kind::Ord)
{
    auto b = std::make_shared<GlyphBox>();
    b->text = text;
    b->font = font;
    b->kind = kind;
    const QFontMetricsF fm(font);
    b->w = fm.horizontalAdvance(text);
    const QRectF tight = fm.tightBoundingRect(text);
    // Italic letters lean past their advance: a little room on the right
    // (more for the long overhang of f).
    if (font.italic())
        b->w += font.pixelSize() * (text == QLatin1String("f") ? 0.16 : 0.04);
    b->a = qMax(-tight.top(), fm.xHeight() * 0.5);
    b->d = qMax(tight.bottom(), 0.0);
    if (text.trimmed().isEmpty()) {
        b->a = fm.xHeight();
        b->d = 0;
    }
    return b;
}

// Lays out boxes side by side with TeX's inter-atom spacing.
std::shared_ptr<HBox> hbox(const QList<BoxP> &items, double px, bool tight)
{
    auto h = std::make_shared<HBox>();
    double x = 0;
    Kind prev = Kind::Open;
    bool first = true;
    const double thin = px * 3 / 18.0, med = px * 4 / 18.0, thick = px * 5 / 18.0;
    for (BoxP item : items) {
        Kind k = item->kind;
        // A binary operator with nothing to bind to on the left is unary.
        if (k == Kind::Bin && (first || prev == Kind::Bin || prev == Kind::Op || prev == Kind::Rel || prev == Kind::Open
                               || prev == Kind::Punct))
            k = item->kind = Kind::Ord;
        double gap = 0;
        if (!first && !tight) {
            if (k == Kind::Bin || prev == Kind::Bin)
                gap = med;
            else if (k == Kind::Rel || prev == Kind::Rel)
                gap = (k == Kind::Rel && prev == Kind::Rel) ? 0 : thick;
            else if (prev == Kind::Punct)
                gap = thin;
            else if ((k == Kind::Op && prev != Kind::Open) || (prev == Kind::Op && k != Kind::Close && k != Kind::Punct))
                gap = thin;
            else if (k == Kind::Inner || prev == Kind::Inner)
                gap = thin;
        }
        x += gap;
        h->items.append(item);
        h->xs.append(x);
        x += item->w;
        h->a = qMax(h->a, item->a);
        h->d = qMax(h->d, item->d);
        prev = k;
        first = false;
    }
    // A trailing binary operator is unary too (no spacing to fix up).
    h->w = x;
    return h;
}

// ---- delimiters -------------------------------------------------------------------------

// Draws a stretchy delimiter of the given height centred on the math axis.
BoxP delimiter(const QString &which, double height, double axis, double px, Kind kind)
{
    auto b = std::make_shared<PathBox>();
    b->kind = kind;
    if (which.isEmpty() || which == QLatin1String(".")) {
        b->w = px * 0.1;
        return b;
    }
    const double h = qMax(height, px * 1.15);
    const double half = h / 2;
    b->a = axis + half;
    b->d = qMax(0.0, half - axis);
    const double stroke = qMax(1.0, px * 0.065);
    double width = px * 0.42;
    if (which == QLatin1String("|") || which == QLatin1String("\\vert"))
        width = px * 0.3;
    if (which == QLatin1String("\\|") || which == QLatin1String("\\Vert"))
        width = px * 0.45;
    if (which == QLatin1String("\\{") || which == QLatin1String("\\}") || which == QLatin1String("{") || which == QLatin1String("}"))
        width = px * 0.5;
    b->w = width;
    const QString w = which;
    b->painter = [w, half, axis, stroke, width, px](QPainter &p, double x, double y) {
        const double top = y - axis - half, bottom = y - axis + half, mid = y - axis;
        QPainterPath path;
        const double l = x + width * 0.22, r = x + width * 0.78;
        if (w == QLatin1String("(")) {
            path.moveTo(r, top);
            path.cubicTo(l - width * 0.1, top + (bottom - top) * 0.25, l - width * 0.1, bottom - (bottom - top) * 0.25, r, bottom);
        } else if (w == QLatin1String(")")) {
            path.moveTo(l, top);
            path.cubicTo(r + width * 0.1, top + (bottom - top) * 0.25, r + width * 0.1, bottom - (bottom - top) * 0.25, l, bottom);
        } else if (w == QLatin1String("[") || w == QLatin1String("\\lbrack")) {
            path.moveTo(r, top);
            path.lineTo(l, top);
            path.lineTo(l, bottom);
            path.lineTo(r, bottom);
        } else if (w == QLatin1String("]") || w == QLatin1String("\\rbrack")) {
            path.moveTo(l, top);
            path.lineTo(r, top);
            path.lineTo(r, bottom);
            path.lineTo(l, bottom);
        } else if (w == QLatin1String("\\lfloor")) {
            path.moveTo(l, top);
            path.lineTo(l, bottom);
            path.lineTo(r, bottom);
        } else if (w == QLatin1String("\\rfloor")) {
            path.moveTo(r, top);
            path.lineTo(r, bottom);
            path.lineTo(l, bottom);
        } else if (w == QLatin1String("\\lceil")) {
            path.moveTo(r, top);
            path.lineTo(l, top);
            path.lineTo(l, bottom);
        } else if (w == QLatin1String("\\rceil")) {
            path.moveTo(l, top);
            path.lineTo(r, top);
            path.lineTo(r, bottom);
        } else if (w == QLatin1String("\\{") || w == QLatin1String("{") || w == QLatin1String("\\lbrace")) {
            const double c = x + width * 0.5;
            path.moveTo(r, top);
            path.cubicTo(c - width * 0.05, top, c + width * 0.05, top + px * 0.15, c, top + px * 0.35);
            path.lineTo(c, mid - px * 0.3);
            path.cubicTo(c, mid - px * 0.08, l + width * 0.1, mid, l, mid);
            path.cubicTo(l + width * 0.1, mid, c, mid + px * 0.08, c, mid + px * 0.3);
            path.lineTo(c, bottom - px * 0.35);
            path.cubicTo(c + width * 0.05, bottom - px * 0.15, c - width * 0.05, bottom, r, bottom);
        } else if (w == QLatin1String("\\}") || w == QLatin1String("}") || w == QLatin1String("\\rbrace")) {
            const double c = x + width * 0.5;
            path.moveTo(l, top);
            path.cubicTo(c + width * 0.05, top, c - width * 0.05, top + px * 0.15, c, top + px * 0.35);
            path.lineTo(c, mid - px * 0.3);
            path.cubicTo(c, mid - px * 0.08, r - width * 0.1, mid, r, mid);
            path.cubicTo(r - width * 0.1, mid, c, mid + px * 0.08, c, mid + px * 0.3);
            path.lineTo(c, bottom - px * 0.35);
            path.cubicTo(c - width * 0.05, bottom - px * 0.15, c + width * 0.05, bottom, l, bottom);
        } else if (w == QLatin1String("|") || w == QLatin1String("\\vert") || w == QLatin1String("\\lvert") || w == QLatin1String("\\rvert")) {
            const double c = x + width * 0.5;
            path.moveTo(c, top);
            path.lineTo(c, bottom);
        } else if (w == QLatin1String("\\|") || w == QLatin1String("\\Vert") || w == QLatin1String("\\lVert") || w == QLatin1String("\\rVert")) {
            path.moveTo(x + width * 0.32, top);
            path.lineTo(x + width * 0.32, bottom);
            path.moveTo(x + width * 0.68, top);
            path.lineTo(x + width * 0.68, bottom);
        } else if (w == QLatin1String("\\langle") || w == QLatin1String("<")) {
            path.moveTo(r, top);
            path.lineTo(l, mid);
            path.lineTo(r, bottom);
        } else if (w == QLatin1String("\\rangle") || w == QLatin1String(">")) {
            path.moveTo(l, top);
            path.lineTo(r, mid);
            path.lineTo(l, bottom);
        } else if (w == QLatin1String("/")) {
            path.moveTo(r, top);
            path.lineTo(l, bottom);
        } else {
            path.moveTo(x + width * 0.5, top);
            path.lineTo(x + width * 0.5, bottom);
        }
        p.save();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(p.pen().color(), stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
        p.restore();
    };
    return b;
}

// ---- symbols --------------------------------------------------------------------------

struct Sym {
    const char *text;
    Kind kind;
};

const QHash<QString, Sym> &symbols()
{
    static const QHash<QString, Sym> s = [] {
        QHash<QString, Sym> m;
        auto add = [&](const char *name, const char *text, Kind k = Kind::Ord) { m.insert(QString::fromLatin1(name), {text, k}); };
        // Greek
        add("alpha", "α"); add("beta", "β"); add("gamma", "γ"); add("delta", "δ"); add("epsilon", "ϵ"); add("varepsilon", "ε");
        add("zeta", "ζ"); add("eta", "η"); add("theta", "θ"); add("vartheta", "ϑ"); add("iota", "ι"); add("kappa", "κ");
        add("lambda", "λ"); add("mu", "μ"); add("nu", "ν"); add("xi", "ξ"); add("omicron", "ο"); add("pi", "π"); add("varpi", "ϖ");
        add("rho", "ρ"); add("varrho", "ϱ"); add("sigma", "σ"); add("varsigma", "ς"); add("tau", "τ"); add("upsilon", "υ");
        add("phi", "ϕ"); add("varphi", "φ"); add("chi", "χ"); add("psi", "ψ"); add("omega", "ω");
        add("Gamma", "Γ"); add("Delta", "Δ"); add("Theta", "Θ"); add("Lambda", "Λ"); add("Xi", "Ξ"); add("Pi", "Π");
        add("Sigma", "Σ"); add("Upsilon", "Υ"); add("Phi", "Φ"); add("Psi", "Ψ"); add("Omega", "Ω");
        // Binary operators
        add("times", "×", Kind::Bin); add("cdot", "⋅", Kind::Bin); add("pm", "±", Kind::Bin); add("mp", "∓", Kind::Bin);
        add("div", "÷", Kind::Bin); add("ast", "∗", Kind::Bin); add("star", "⋆", Kind::Bin); add("circ", "∘", Kind::Bin);
        add("bullet", "∙", Kind::Bin); add("oplus", "⊕", Kind::Bin); add("ominus", "⊖", Kind::Bin); add("otimes", "⊗", Kind::Bin);
        add("odot", "⊙", Kind::Bin); add("cup", "∪", Kind::Bin); add("cap", "∩", Kind::Bin); add("setminus", "∖", Kind::Bin);
        add("wedge", "∧", Kind::Bin); add("land", "∧", Kind::Bin); add("vee", "∨", Kind::Bin); add("lor", "∨", Kind::Bin);
        add("sqcup", "⊔", Kind::Bin); add("sqcap", "⊓", Kind::Bin); add("dagger", "†", Kind::Bin); add("amalg", "⨿", Kind::Bin);
        // Relations
        add("leq", "≤", Kind::Rel); add("le", "≤", Kind::Rel); add("geq", "≥", Kind::Rel); add("ge", "≥", Kind::Rel);
        add("neq", "≠", Kind::Rel); add("ne", "≠", Kind::Rel); add("approx", "≈", Kind::Rel); add("equiv", "≡", Kind::Rel);
        add("sim", "∼", Kind::Rel); add("simeq", "≃", Kind::Rel); add("cong", "≅", Kind::Rel); add("propto", "∝", Kind::Rel);
        add("ll", "≪", Kind::Rel); add("gg", "≫", Kind::Rel); add("in", "∈", Kind::Rel); add("notin", "∉", Kind::Rel);
        add("ni", "∋", Kind::Rel); add("subset", "⊂", Kind::Rel); add("supset", "⊃", Kind::Rel); add("subseteq", "⊆", Kind::Rel);
        add("supseteq", "⊇", Kind::Rel); add("perp", "⊥", Kind::Rel); add("parallel", "∥", Kind::Rel); add("mid", "∣", Kind::Rel);
        add("to", "→", Kind::Rel); add("rightarrow", "→", Kind::Rel); add("leftarrow", "←", Kind::Rel); add("gets", "←", Kind::Rel);
        add("Rightarrow", "⇒", Kind::Rel); add("Leftarrow", "⇐", Kind::Rel); add("leftrightarrow", "↔", Kind::Rel);
        add("Leftrightarrow", "⇔", Kind::Rel); add("iff", "⟺", Kind::Rel); add("implies", "⟹", Kind::Rel);
        add("mapsto", "↦", Kind::Rel); add("longrightarrow", "⟶", Kind::Rel); add("longleftarrow", "⟵", Kind::Rel);
        add("uparrow", "↑", Kind::Rel); add("downarrow", "↓", Kind::Rel); add("prec", "≺", Kind::Rel); add("succ", "≻", Kind::Rel);
        add("models", "⊨", Kind::Rel); add("vdash", "⊢", Kind::Rel); add("doteq", "≐", Kind::Rel); add("coloneqq", "≔", Kind::Rel);
        // Ordinary symbols
        add("infty", "∞"); add("partial", "∂"); add("nabla", "∇"); add("forall", "∀"); add("exists", "∃"); add("nexists", "∄");
        add("emptyset", "∅"); add("varnothing", "∅"); add("neg", "¬"); add("lnot", "¬"); add("angle", "∠"); add("degree", "°");
        add("prime", "′"); add("hbar", "ℏ"); add("ell", "ℓ"); add("Re", "ℜ"); add("Im", "ℑ"); add("aleph", "ℵ"); add("wp", "℘");
        add("triangle", "△"); add("square", "□"); add("checkmark", "✓"); add("dots", "…"); add("ldots", "…");
        add("cdots", "⋯"); add("vdots", "⋮"); add("ddots", "⋱"); add("therefore", "∴"); add("because", "∵");
        add("top", "⊤"); add("bot", "⊥"); add("clubsuit", "♣"); add("heartsuit", "♡"); add("spadesuit", "♠"); add("diamondsuit", "♢");
        add("imath", "ı"); add("jmath", "ȷ"); add("dagger", "†"); add("S", "§"); add("P", "¶"); add("copyright", "©");
        add("colon", ":", Kind::Punct); add("lbrace", "{", Kind::Open); add("rbrace", "}", Kind::Close);
        add("langle", "⟨", Kind::Open); add("rangle", "⟩", Kind::Close); add("lfloor", "⌊", Kind::Open); add("rfloor", "⌋", Kind::Close);
        add("lceil", "⌈", Kind::Open); add("rceil", "⌉", Kind::Close); add("vert", "|"); add("Vert", "‖"); add("backslash", "\\");
        add("#", "#"); add("$", "$"); add("%", "%"); add("&", "&"); add("_", "_");
        add("{", "{", Kind::Open); add("}", "}", Kind::Close); add("|", "‖");
        return m;
    }();
    return s;
}

// Big operators (∑ ∏ ∫ …): glyph and whether they take limits in display style.
const QHash<QString, QPair<const char *, bool>> &bigOps()
{
    static const QHash<QString, QPair<const char *, bool>> m = {
        {QStringLiteral("sum"), {"∑", true}},      {QStringLiteral("prod"), {"∏", true}},     {QStringLiteral("coprod"), {"∐", true}},
        {QStringLiteral("int"), {"∫", false}},     {QStringLiteral("iint"), {"∬", false}},    {QStringLiteral("iiint"), {"∭", false}},
        {QStringLiteral("oint"), {"∮", false}},    {QStringLiteral("bigcup"), {"⋃", true}},   {QStringLiteral("bigcap"), {"⋂", true}},
        {QStringLiteral("bigoplus"), {"⨁", true}}, {QStringLiteral("bigotimes"), {"⨂", true}}, {QStringLiteral("bigvee"), {"⋁", true}},
        {QStringLiteral("bigwedge"), {"⋀", true}}, {QStringLiteral("bigsqcup"), {"⨆", true}},
    };
    return m;
}

// Named functions set upright; some take limits in display style.
const QHash<QString, bool> &functions()
{
    static const QHash<QString, bool> m = {
        {QStringLiteral("sin"), false},  {QStringLiteral("cos"), false},  {QStringLiteral("tan"), false},  {QStringLiteral("cot"), false},
        {QStringLiteral("sec"), false},  {QStringLiteral("csc"), false},  {QStringLiteral("arcsin"), false}, {QStringLiteral("arccos"), false},
        {QStringLiteral("arctan"), false}, {QStringLiteral("sinh"), false}, {QStringLiteral("cosh"), false}, {QStringLiteral("tanh"), false},
        {QStringLiteral("log"), false},  {QStringLiteral("ln"), false},   {QStringLiteral("lg"), false},   {QStringLiteral("exp"), false},
        {QStringLiteral("deg"), false},  {QStringLiteral("dim"), false},  {QStringLiteral("ker"), false},  {QStringLiteral("hom"), false},
        {QStringLiteral("arg"), false},  {QStringLiteral("lim"), true},   {QStringLiteral("liminf"), true}, {QStringLiteral("limsup"), true},
        {QStringLiteral("max"), true},   {QStringLiteral("min"), true},   {QStringLiteral("sup"), true},   {QStringLiteral("inf"), true},
        {QStringLiteral("det"), true},   {QStringLiteral("gcd"), true},   {QStringLiteral("Pr"), true},    {QStringLiteral("argmax"), true},
        {QStringLiteral("argmin"), true},
    };
    return m;
}

QString blackboard(QChar c)
{
    static const QHash<QChar, QString> special = {{QLatin1Char('C'), QStringLiteral("ℂ")}, {QLatin1Char('H'), QStringLiteral("ℍ")},
                                                  {QLatin1Char('N'), QStringLiteral("ℕ")}, {QLatin1Char('P'), QStringLiteral("ℙ")},
                                                  {QLatin1Char('Q'), QStringLiteral("ℚ")}, {QLatin1Char('R'), QStringLiteral("ℝ")},
                                                  {QLatin1Char('Z'), QStringLiteral("ℤ")}};
    if (special.contains(c))
        return special.value(c);
    char32_t cp = 0;
    if (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
        cp = 0x1D538 + (c.unicode() - 'A');
    else if (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
        cp = 0x1D552 + (c.unicode() - 'a');
    else if (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
        cp = 0x1D7D8 + (c.unicode() - '0');
    return cp ? QString::fromUcs4(&cp, 1) : QString(c);
}

QString calligraphic(QChar c)
{
    static const QHash<QChar, QString> special = {{QLatin1Char('B'), QStringLiteral("ℬ")}, {QLatin1Char('E'), QStringLiteral("ℰ")},
                                                  {QLatin1Char('F'), QStringLiteral("ℱ")}, {QLatin1Char('H'), QStringLiteral("ℋ")},
                                                  {QLatin1Char('I'), QStringLiteral("ℐ")}, {QLatin1Char('L'), QStringLiteral("ℒ")},
                                                  {QLatin1Char('M'), QStringLiteral("ℳ")}, {QLatin1Char('R'), QStringLiteral("ℛ")}};
    if (special.contains(c))
        return special.value(c);
    if (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) {
        const char32_t cp = 0x1D49C + (c.unicode() - 'A');
        return QString::fromUcs4(&cp, 1);
    }
    return QString(c);
}

// ---- parser & layout -------------------------------------------------------------------

enum class Font { Normal, Roman, Bold, Blackboard, Calligraphic, Text, Italic, BoldSymbol };

class Layout
{
public:
    Layout(const QString &src, double px) : m_src(src), m_base(px) {}
    QString error() const { return m_error; }

    BoxP run(bool display)
    {
        m_style = display ? 0 : 1;
        BoxP b = expression({});
        if (m_pos < m_src.size() && m_error.isEmpty())
            fail(QStringLiteral("Unexpected “%1”").arg(m_src.mid(m_pos, 1)));
        return b;
    }

private:
    // ---- scanning ----
    void skipSpaces()
    {
        while (m_pos < m_src.size() && m_src[m_pos].isSpace())
            ++m_pos;
    }
    bool atEnd() const { return m_pos >= m_src.size(); }
    QChar peekChar() const { return atEnd() ? QChar() : m_src[m_pos]; }
    // A command name after a backslash, or a single character token.
    QString peekToken()
    {
        const int save = m_pos;
        const QString t = takeToken();
        m_pos = save;
        return t;
    }
    QString takeToken()
    {
        skipSpaces();
        if (atEnd())
            return {};
        if (m_src[m_pos] == QLatin1Char('\\')) {
            int j = m_pos + 1;
            if (j < m_src.size() && m_src[j].isLetter()) {
                while (j < m_src.size() && m_src[j].isLetter())
                    ++j;
            } else if (j < m_src.size()) {
                ++j;
            }
            const QString t = m_src.mid(m_pos, j - m_pos);
            m_pos = j;
            return t;
        }
        return QString(m_src[m_pos++]);
    }
    void fail(const QString &msg)
    {
        if (m_error.isEmpty())
            m_error = msg;
    }
    QString groupText()
    {
        // Raw text of a {...} group (for \text, \operatorname, \begin…).
        skipSpaces();
        if (peekChar() != QLatin1Char('{')) {
            const QString t = takeToken();
            return t;
        }
        ++m_pos;
        int depth = 1;
        const int start = m_pos;
        while (!atEnd() && depth > 0) {
            if (m_src[m_pos] == QLatin1Char('\\')) {
                m_pos += 2;
                continue;
            }
            if (m_src[m_pos] == QLatin1Char('{'))
                ++depth;
            else if (m_src[m_pos] == QLatin1Char('}'))
                --depth;
            ++m_pos;
        }
        if (depth > 0) {
            fail(QStringLiteral("Missing }"));
            return m_src.mid(start);
        }
        return m_src.mid(start, m_pos - start - 1);
    }

    // ---- sizes ----
    double px() const
    {
        static const double scale[] = {1.0, 1.0, 0.72, 0.55};
        return m_base * scale[qBound(0, m_style, 3)];
    }
    double axis() const { return QFontMetricsF(mathFont(px(), Shape::Upright)).xHeight() * 0.52; }
    double ruleThickness() const { return qMax(1.0, px() * 0.055); }
    bool display() const { return m_style == 0; }
    struct StyleScope {
        Layout &l;
        int saved;
        StyleScope(Layout &layout, int style) : l(layout), saved(layout.m_style) { l.m_style = style; }
        ~StyleScope() { l.m_style = saved; }
    };
    int scriptStyle() const { return m_style <= 1 ? 2 : 3; }
    int fracStyle() const { return m_style == 0 ? 1 : (m_style == 1 ? 2 : 3); }

    // ---- glyphs ----
    BoxP letter(QChar c)
    {
        Shape shape = Shape::Italic;
        QString text(c);
        switch (m_font) {
        case Font::Roman:
        case Font::Text:
            shape = Shape::Upright;
            break;
        case Font::Bold:
            shape = Shape::Bold;
            break;
        case Font::BoldSymbol:
            shape = Shape::BoldItalic;
            break;
        case Font::Blackboard:
            shape = Shape::Upright;
            text = blackboard(c);
            break;
        case Font::Calligraphic:
            shape = Shape::Upright;
            text = calligraphic(c);
            break;
        case Font::Italic:
        case Font::Normal:
            if (c.isDigit())
                shape = Shape::Upright;
            break;
        }
        if (c.isDigit() && m_font == Font::Normal)
            shape = Shape::Upright;
        return glyph(text, mathFont(px(), shape));
    }

    BoxP symbol(const QString &text, Kind kind, bool upright = true)
    {
        return glyph(text, mathFont(px(), upright ? (m_font == Font::Bold ? Shape::Bold : Shape::Upright) : Shape::Italic), kind);
    }

    // ---- structure ----
    BoxP expression(const QStringList &until)
    {
        QList<BoxP> items;
        for (;;) {
            skipSpaces();
            if (atEnd())
                break;
            const QString t = peekToken();
            if (until.contains(t))
                break;
            if (t == QLatin1String("}")) {
                if (until.isEmpty())
                    fail(QStringLiteral("Unexpected }"));
                break;
            }
            BoxP atom = this->atom();
            if (!atom)
                continue;
            atom = scripts(atom);
            items.append(atom);
        }
        return hbox(items, px(), m_font == Font::Text);
    }

    BoxP group()
    {
        skipSpaces();
        if (peekChar() == QLatin1Char('{')) {
            ++m_pos;
            BoxP b = expression({QStringLiteral("}")});
            if (takeToken() != QLatin1String("}"))
                fail(QStringLiteral("Missing }"));
            return b;
        }
        if (atEnd()) {
            fail(QStringLiteral("Missing argument"));
            return space(0);
        }
        BoxP a = atom();
        return a ? a : space(0);
    }

    BoxP scripts(BoxP base)
    {
        BoxP sup, sub;
        for (;;) {
            skipSpaces();
            const QChar c = peekChar();
            if (c == QLatin1Char('^') || c == QLatin1Char('_')) {
                ++m_pos;
                StyleScope s(*this, scriptStyle());
                BoxP g = group();
                if (c == QLatin1Char('^')) {
                    if (sup)
                        fail(QStringLiteral("Double superscript"));
                    sup = g;
                } else {
                    if (sub)
                        fail(QStringLiteral("Double subscript"));
                    sub = g;
                }
                continue;
            }
            if (c == QLatin1Char('\'')) {
                // Primes are superscripts.
                ++m_pos;
                QString primes = QStringLiteral("′");
                while (peekChar() == QLatin1Char('\'')) {
                    ++m_pos;
                    primes += QStringLiteral("′");
                }
                StyleScope s(*this, scriptStyle());
                sup = glyph(primes, mathFont(px() * 1.25, Shape::Upright));
                continue;
            }
            break;
        }
        if (!sup && !sub)
            return base;
        if (base->limits && display())
            return limitsBox(base, sup, sub);
        return scriptBox(base, sup, sub);
    }

    BoxP scriptBox(BoxP base, BoxP sup, BoxP sub)
    {
        auto h = std::make_shared<HBox>();
        h->kind = base->kind;
        const double x = base->w;
        h->items.append(base);
        h->xs.append(0);
        const QFontMetricsF fm(mathFont(px(), Shape::Upright));
        double supShift = 0, subShift = 0;
        // As in TeX, scripts on a single letter sit at a fixed height (so
        // a², b² and c² line up); only built-up bases push them further.
        const auto *glyph = dynamic_cast<const GlyphBox *>(base.get());
        const bool letter = glyph && glyph->text.size() <= 2 && !base->limits;
        if (sup)
            supShift = (letter ? fm.xHeight() * 0.9 : qMax(base->a - fm.xHeight() * 0.45, fm.xHeight() * 0.85)) + qMax(0.0, sup->d * 0.3);
        if (sub)
            subShift = qMax(base->d + fm.xHeight() * 0.1, fm.xHeight() * 0.35);
        if (sup && sub) {
            // Keep a gap between them.
            const double gap = (supShift - sup->d) - (sub->a - subShift);
            if (gap < px() * 0.15)
                supShift += px() * 0.15 - gap;
        }
        double w = x;
        if (sup) {
            auto s = std::make_shared<ShiftBox>();
            s->inner = sup;
            s->dy = supShift;
            s->w = sup->w;
            s->a = sup->a + supShift;
            s->d = qMax(0.0, sup->d - supShift);
            h->items.append(s);
            h->xs.append(x + px() * 0.05);
            w = qMax(w, x + px() * 0.05 + sup->w);
            h->a = qMax(h->a, s->a);
        }
        if (sub) {
            auto s = std::make_shared<ShiftBox>();
            s->inner = sub;
            s->dy = -subShift;
            s->w = sub->w;
            s->a = qMax(0.0, sub->a - subShift);
            s->d = sub->d + subShift;
            h->items.append(s);
            h->xs.append(x - (base->kind == Kind::Op ? px() * 0.15 : 0));
            w = qMax(w, x + sub->w);
            h->d = qMax(h->d, s->d);
        }
        h->a = qMax(h->a, base->a);
        h->d = qMax(h->d, base->d);
        h->w = w + px() * 0.04;
        return h;
    }

    BoxP limitsBox(BoxP op, BoxP over, BoxP under)
    {
        auto h = std::make_shared<HBox>();
        h->kind = Kind::Op;
        double w = op->w;
        if (over)
            w = qMax(w, over->w);
        if (under)
            w = qMax(w, under->w);
        const double gap = px() * 0.15;
        h->items.append(op);
        h->xs.append((w - op->w) / 2);
        h->a = op->a;
        h->d = op->d;
        if (over) {
            auto s = std::make_shared<ShiftBox>();
            s->inner = over;
            s->dy = op->a + gap + over->d;
            s->w = over->w;
            s->a = s->dy + over->a;
            h->items.append(s);
            h->xs.append((w - over->w) / 2);
            h->a = s->a;
        }
        if (under) {
            auto s = std::make_shared<ShiftBox>();
            s->inner = under;
            s->dy = -(op->d + gap + under->a);
            s->w = under->w;
            s->d = -s->dy + under->d;
            h->items.append(s);
            h->xs.append((w - under->w) / 2);
            h->d = s->d;
        }
        h->w = w;
        return h;
    }

    BoxP fraction(BoxP num, BoxP den, bool rule = true)
    {
        auto h = std::make_shared<HBox>();
        h->kind = Kind::Inner;
        const double pad = px() * 0.12;
        const double w = qMax(num->w, den->w) + pad * 2;
        const double ax = axis();
        const double t = rule ? ruleThickness() : 0;
        const double gap = px() * (display() ? 0.18 : 0.12);
        auto n = std::make_shared<ShiftBox>();
        n->inner = num;
        n->dy = ax + t / 2 + gap + num->d;
        n->w = num->w;
        auto d = std::make_shared<ShiftBox>();
        d->inner = den;
        d->dy = -(-ax + t / 2 + gap + den->a);
        d->w = den->w;
        h->items.append(n);
        h->xs.append((w - num->w) / 2);
        h->items.append(d);
        h->xs.append((w - den->w) / 2);
        if (rule) {
            auto r = std::make_shared<RuleBox>();
            r->w = w;
            r->thickness = t;
            r->raise = ax;
            h->items.append(r);
            h->xs.append(0);
        }
        h->a = n->dy + num->a;
        h->d = -d->dy + den->d;
        h->w = w + px() * 0.08;
        return h;
    }

    BoxP radical(BoxP body, BoxP index)
    {
        auto b = std::make_shared<PathBox>();
        const double t = ruleThickness();
        const double gap = px() * 0.14;
        const double h = body->a + body->d + gap + t;
        const double signW = px() * 0.62;
        const double indexW = index ? qMax(0.0, index->w - signW * 0.45) : 0;
        b->w = indexW + signW + body->w + px() * 0.12;
        b->a = body->a + gap + t * 1.5;
        b->d = body->d + px() * 0.05;
        const double bodyA = body->a, bodyD = body->d;
        const double P = px();
        b->painter = [=](QPainter &p, double x, double y) {
            const double top = y - bodyA - gap - t / 2;
            const double bottom = y + bodyD;
            const double sx = x + indexW;
            QPainterPath path;
            path.moveTo(sx, bottom - h * 0.42);
            path.lineTo(sx + signW * 0.22, bottom - h * 0.5);
            path.lineTo(sx + signW * 0.5, bottom);
            path.lineTo(sx + signW * 0.95, top);
            path.lineTo(sx + signW + body->w + P * 0.1, top);
            p.save();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(p.pen().color(), t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
            p.restore();
            body->draw(p, sx + signW, y);
            if (index)
                index->draw(p, x, bottom - h * 0.55 - index->d);
        };
        Q_UNUSED(h)
        return b;
    }

    BoxP accent(const QString &which, BoxP base)
    {
        auto b = std::make_shared<PathBox>();
        const double lift = px() * 0.12;
        const double size = px() * 0.32;
        const bool wide = which == QLatin1String("\\overline") || which == QLatin1String("\\widehat")
                          || which == QLatin1String("\\widetilde") || which == QLatin1String("\\overrightarrow");
        b->w = base->w;
        b->a = base->a + lift + size;
        b->d = base->d;
        b->kind = base->kind;
        const double baseA = base->a;
        const double t = ruleThickness();
        const double P = px();
        b->painter = [=](QPainter &p, double x, double y) {
            base->draw(p, x, y);
            const double top = y - baseA - lift;
            const double cx = x + base->w / 2 + (wide ? 0 : P * 0.06);
            const double half = wide ? base->w / 2 : qMax(P * 0.18, qMin(base->w / 2, P * 0.26));
            p.save();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(p.pen().color(), t, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            QPainterPath path;
            if (which == QLatin1String("\\hat") || which == QLatin1String("\\widehat")) {
                path.moveTo(cx - half, top);
                path.lineTo(cx, top - size * 0.8);
                path.lineTo(cx + half, top);
            } else if (which == QLatin1String("\\check")) {
                path.moveTo(cx - half, top - size * 0.8);
                path.lineTo(cx, top);
                path.lineTo(cx + half, top - size * 0.8);
            } else if (which == QLatin1String("\\bar") || which == QLatin1String("\\overline")) {
                path.moveTo(cx - half, top - size * 0.4);
                path.lineTo(cx + half, top - size * 0.4);
            } else if (which == QLatin1String("\\vec") || which == QLatin1String("\\overrightarrow")) {
                path.moveTo(cx - half, top - size * 0.4);
                path.lineTo(cx + half, top - size * 0.4);
                path.moveTo(cx + half - size * 0.35, top - size * 0.75);
                path.lineTo(cx + half, top - size * 0.4);
                path.lineTo(cx + half - size * 0.35, top - size * 0.05);
            } else if (which == QLatin1String("\\tilde") || which == QLatin1String("\\widetilde")) {
                path.moveTo(cx - half, top - size * 0.2);
                path.cubicTo(cx - half * 0.4, top - size * 0.9, cx + half * 0.4, top + size * 0.3, cx + half, top - size * 0.6);
            } else if (which == QLatin1String("\\dot")) {
                p.setBrush(p.pen().color());
                p.drawEllipse(QPointF(cx, top - size * 0.4), t * 0.9, t * 0.9);
            } else if (which == QLatin1String("\\ddot")) {
                p.setBrush(p.pen().color());
                p.drawEllipse(QPointF(cx - size * 0.35, top - size * 0.4), t * 0.9, t * 0.9);
                p.drawEllipse(QPointF(cx + size * 0.35, top - size * 0.4), t * 0.9, t * 0.9);
            }
            p.drawPath(path);
            p.restore();
        };
        return b;
    }

    BoxP underline(BoxP base)
    {
        auto b = std::make_shared<PathBox>();
        const double t = ruleThickness();
        b->w = base->w;
        b->a = base->a;
        b->d = base->d + px() * 0.18 + t;
        const double baseD = base->d;
        const double P = px();
        b->painter = [=](QPainter &p, double x, double y) {
            base->draw(p, x, y);
            p.fillRect(QRectF(x, y + baseD + P * 0.12, base->w, t), p.pen().color());
        };
        return b;
    }

    BoxP boxed(BoxP base)
    {
        auto b = std::make_shared<PathBox>();
        const double pad = px() * 0.25;
        const double t = ruleThickness();
        b->w = base->w + 2 * pad;
        b->a = base->a + pad;
        b->d = base->d + pad;
        const double baseA = base->a, baseD = base->d;
        b->painter = [=](QPainter &p, double x, double y) {
            base->draw(p, x + pad, y);
            p.save();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(p.pen().color(), t));
            p.drawRect(QRectF(x + t / 2, y - baseA - pad + t / 2, base->w + 2 * pad - t, baseA + baseD + 2 * pad - t));
            p.restore();
        };
        return b;
    }

    BoxP leftRight()
    {
        // After \left: delimiter, body until \right, delimiter.
        const QString open = takeToken();
        BoxP body = expression({QStringLiteral("\\right")});
        QString close;
        if (takeToken() == QLatin1String("\\right"))
            close = takeToken();
        else
            fail(QStringLiteral("Missing \\right"));
        const double ax = axis();
        const double height = 2 * qMax(body->a - ax, body->d + ax) + px() * 0.2;
        BoxP l = delimiter(open, height, ax, px(), Kind::Open);
        BoxP r = delimiter(close, height, ax, px(), Kind::Close);
        auto h = hbox({l, body, r}, px(), true);
        h->kind = Kind::Inner;
        return h;
    }

    BoxP environment(const QString &name)
    {
        // Columns of cells separated by & and rows by \\.
        if (name == QLatin1String("array")) {
            groupText(); // column spec (ignored)
        }
        QList<QList<BoxP>> rows;
        QList<BoxP> row;
        const bool cases = name == QLatin1String("cases") || name == QLatin1String("dcases");
        const bool aligned = name.startsWith(QLatin1String("align")) || name == QLatin1String("split") || name == QLatin1String("gathered");
        const int cellStyle = cases ? (m_style == 0 ? 1 : m_style) : m_style;
        for (;;) {
            StyleScope s(*this, cellStyle);
            BoxP cell = expression({QStringLiteral("&"), QStringLiteral("\\\\"), QStringLiteral("\\end"), QStringLiteral("\\cr")});
            row.append(cell);
            const QString t = takeToken();
            if (t == QLatin1String("&"))
                continue;
            if (t == QLatin1String("\\\\") || t == QLatin1String("\\cr")) {
                rows.append(row);
                row.clear();
                continue;
            }
            if (t == QLatin1String("\\end")) {
                const QString endName = groupText();
                if (endName.trimmed() != name)
                    fail(QStringLiteral("\\begin{%1} ended by \\end{%2}").arg(name, endName));
            } else {
                fail(QStringLiteral("Missing \\end{%1}").arg(name));
            }
            break;
        }
        if (!(row.size() == 1 && row.first()->w == 0) || rows.isEmpty())
            rows.append(row);
        int cols = 0;
        for (const auto &r : rows)
            cols = qMax(cols, int(r.size()));
        QList<double> colW(cols, 0);
        QList<double> rowA(rows.size(), px() * 0.7), rowD(rows.size(), px() * 0.25);
        for (int r = 0; r < rows.size(); ++r)
            for (int c = 0; c < rows[r].size(); ++c) {
                colW[c] = qMax(colW[c], rows[r][c]->w);
                rowA[r] = qMax(rowA[r], rows[r][c]->a);
                rowD[r] = qMax(rowD[r], rows[r][c]->d);
            }
        const double colGap = aligned ? 0 : px() * (cases ? 1.0 : 0.9);
        const double rowGap = px() * 0.35;
        double total = 0;
        for (int r = 0; r < rows.size(); ++r)
            total += rowA[r] + rowD[r] + (r ? rowGap : 0);
        auto grid = std::make_shared<HBox>();
        grid->kind = Kind::Inner;
        const double ax = axis();
        double y = -(total / 2) + ax; // baseline offset of the first row relative to the axis
        double top = total / 2 + ax;
        double cursorY = top;
        double width = 0;
        for (double w : colW)
            width += w;
        width += colGap * qMax(0, cols - 1);
        for (int r = 0; r < rows.size(); ++r) {
            cursorY -= rowA[r];
            double x = 0;
            for (int c = 0; c < rows[r].size(); ++c) {
                BoxP cell = rows[r][c];
                double cx = x;
                if (aligned)
                    cx = c % 2 == 0 ? x + colW[c] - cell->w : x; // right then left
                else if (!cases)
                    cx = x + (colW[c] - cell->w) / 2;
                auto s = std::make_shared<ShiftBox>();
                s->inner = cell;
                s->dy = cursorY;
                s->w = cell->w;
                grid->items.append(s);
                grid->xs.append(cx);
                x += colW[c] + colGap;
            }
            cursorY -= rowD[r] + rowGap;
        }
        Q_UNUSED(y)
        grid->w = width;
        grid->a = top;
        grid->d = total - top;
        QString open, close;
        if (name == QLatin1String("pmatrix"))
            open = QStringLiteral("("), close = QStringLiteral(")");
        else if (name == QLatin1String("bmatrix"))
            open = QStringLiteral("["), close = QStringLiteral("]");
        else if (name == QLatin1String("Bmatrix"))
            open = QStringLiteral("\\{"), close = QStringLiteral("\\}");
        else if (name == QLatin1String("vmatrix"))
            open = QStringLiteral("|"), close = QStringLiteral("|");
        else if (name == QLatin1String("Vmatrix"))
            open = QStringLiteral("\\|"), close = QStringLiteral("\\|");
        else if (cases)
            open = QStringLiteral("\\{");
        if (open.isEmpty() && close.isEmpty())
            return grid;
        const double height = qMax(grid->a - ax, grid->d + ax) * 2 + px() * 0.2;
        QList<BoxP> parts;
        if (!open.isEmpty())
            parts.append(delimiter(open, height, ax, px(), Kind::Open));
        parts.append(space(px() * 0.12));
        parts.append(grid);
        parts.append(space(px() * 0.12));
        if (!close.isEmpty())
            parts.append(delimiter(close, height, ax, px(), Kind::Close));
        auto h = hbox(parts, px(), true);
        h->kind = Kind::Inner;
        return h;
    }

    BoxP withFont(Font f)
    {
        const Font saved = m_font;
        m_font = f;
        BoxP b = group();
        m_font = saved;
        return b;
    }

    BoxP textGroup(Shape shape)
    {
        const QString t = groupText();
        auto b = glyph(t, mathFont(px(), shape));
        return b;
    }

    BoxP atom()
    {
        skipSpaces();
        if (atEnd())
            return nullptr;
        const QChar c = m_src[m_pos];
        if (c == QLatin1Char('{'))
            return group();
        if (c == QLatin1Char('^') || c == QLatin1Char('_')) {
            // A script with no base: attach to an empty box.
            return space(0);
        }
        if (c == QLatin1Char('&')) {
            ++m_pos;
            fail(QStringLiteral("& outside a matrix"));
            return nullptr;
        }
        if (c != QLatin1Char('\\')) {
            ++m_pos;
            if (m_font == Font::Text)
                return glyph(QString(c), mathFont(px(), Shape::Upright));
            if (c.isLetter())
                return letter(c);
            if (c.isDigit() || c == QLatin1Char('.')) {
                QString num(c);
                while (!atEnd() && (m_src[m_pos].isDigit() || (m_src[m_pos] == QLatin1Char('.') && m_pos + 1 < m_src.size() && m_src[m_pos + 1].isDigit())))
                    num += m_src[m_pos++];
                return glyph(num, mathFont(px(), m_font == Font::Bold ? Shape::Bold : Shape::Upright));
            }
            switch (c.unicode()) {
            case '+':
                return symbol(QStringLiteral("+"), Kind::Bin);
            case '-':
                return symbol(QStringLiteral("−"), Kind::Bin);
            case '*':
                return symbol(QStringLiteral("∗"), Kind::Bin);
            case '=':
            case '<':
            case '>':
                return symbol(QString(c), Kind::Rel);
            case ',':
            case ';':
                return symbol(QString(c), Kind::Punct);
            case ':':
                return symbol(QStringLiteral(":"), Kind::Rel);
            case '(':
            case '[':
                return symbol(QString(c), Kind::Open);
            case ')':
            case ']':
                return symbol(QString(c), Kind::Close);
            case '|':
                return symbol(QStringLiteral("|"), Kind::Ord);
            case '!':
                return symbol(QStringLiteral("!"), Kind::Close);
            case '/':
                return symbol(QStringLiteral("/"), Kind::Ord);
            case '~':
                return space(px() * 0.33);
            case '}':
                return nullptr;
            default:
                return symbol(QString(c), Kind::Ord);
            }
        }
        const QString cmd = takeToken();
        const QString name = cmd.mid(1);
        if (cmd == QLatin1String("\\\\"))
            return space(0); // line breaks outside matrices are ignored
        if (name == QLatin1String(",") || name == QLatin1String("thinspace"))
            return space(px() * 3 / 18.0);
        if (name == QLatin1String(":") || name == QLatin1String(">") || name == QLatin1String("medspace"))
            return space(px() * 4 / 18.0);
        if (name == QLatin1String(";") || name == QLatin1String("thickspace"))
            return space(px() * 5 / 18.0);
        if (name == QLatin1String("!"))
            return space(-px() * 3 / 18.0);
        if (name == QLatin1String(" ") || name == QLatin1String("space"))
            return space(px() * 0.3);
        if (name == QLatin1String("quad"))
            return space(px());
        if (name == QLatin1String("qquad"))
            return space(px() * 2);
        if (name == QLatin1String("frac") || name == QLatin1String("dfrac") || name == QLatin1String("tfrac")
            || name == QLatin1String("cfrac")) {
            const int style = name == QLatin1String("dfrac") || name == QLatin1String("cfrac") ? 1
                              : name == QLatin1String("tfrac")                                 ? 2
                                                                                               : fracStyle();
            BoxP num, den;
            {
                StyleScope s(*this, style);
                num = group();
                den = group();
            }
            return fraction(num, den);
        }
        if (name == QLatin1String("binom") || name == QLatin1String("dbinom") || name == QLatin1String("tbinom")) {
            BoxP num, den;
            {
                StyleScope s(*this, fracStyle());
                num = group();
                den = group();
            }
            BoxP f = fraction(num, den, false);
            const double ax = axis();
            const double height = qMax(f->a - ax, f->d + ax) * 2;
            auto h = hbox({delimiter(QStringLiteral("("), height, ax, px(), Kind::Open), f,
                           delimiter(QStringLiteral(")"), height, ax, px(), Kind::Close)},
                          px(), true);
            h->kind = Kind::Inner;
            return h;
        }
        if (name == QLatin1String("sqrt")) {
            BoxP index;
            skipSpaces();
            if (peekChar() == QLatin1Char('[')) {
                ++m_pos;
                StyleScope s(*this, 3);
                index = expression({QStringLiteral("]")});
                if (takeToken() != QLatin1String("]"))
                    fail(QStringLiteral("Missing ]"));
            }
            BoxP body = group();
            return radical(body, index);
        }
        if (name == QLatin1String("left"))
            return leftRight();
        if (name == QLatin1String("right")) {
            fail(QStringLiteral("\\right without \\left"));
            takeToken();
            return nullptr;
        }
        if (name == QLatin1String("big") || name == QLatin1String("Big") || name == QLatin1String("bigg") || name == QLatin1String("Bigg")
            || name == QLatin1String("bigl") || name == QLatin1String("bigr") || name == QLatin1String("Bigl") || name == QLatin1String("Bigr")) {
            const QString d = takeToken();
            const double factor = name.startsWith(QLatin1String("bigg")) || name.startsWith(QLatin1String("Bigg")) ? 2.4
                                  : name.startsWith(QLatin1String("Big"))                                         ? 1.8
                                                                                                                  : 1.2;
            return delimiter(d, px() * factor, axis(), px(), name.endsWith(QLatin1Char('r')) ? Kind::Close : Kind::Open);
        }
        if (name == QLatin1String("begin")) {
            const QString env = groupText().trimmed();
            return environment(env);
        }
        if (name == QLatin1String("end")) {
            fail(QStringLiteral("\\end without \\begin"));
            groupText();
            return nullptr;
        }
        if (name == QLatin1String("text") || name == QLatin1String("textrm") || name == QLatin1String("mbox")
            || name == QLatin1String("textnormal"))
            return textGroup(Shape::Upright);
        if (name == QLatin1String("textbf"))
            return textGroup(Shape::Bold);
        if (name == QLatin1String("textit"))
            return textGroup(Shape::Italic);
        if (name == QLatin1String("operatorname") || name == QLatin1String("mathop")) {
            auto b = glyph(groupText(), mathFont(px(), Shape::Upright), Kind::Op);
            return b;
        }
        if (name == QLatin1String("mathrm") || name == QLatin1String("rm"))
            return withFont(Font::Roman);
        if (name == QLatin1String("mathbf") || name == QLatin1String("bf"))
            return withFont(Font::Bold);
        if (name == QLatin1String("boldsymbol") || name == QLatin1String("bm"))
            return withFont(Font::BoldSymbol);
        if (name == QLatin1String("mathbb"))
            return withFont(Font::Blackboard);
        if (name == QLatin1String("mathcal") || name == QLatin1String("mathscr"))
            return withFont(Font::Calligraphic);
        if (name == QLatin1String("mathit"))
            return withFont(Font::Italic);
        if (name == QLatin1String("mathsf") || name == QLatin1String("mathtt"))
            return withFont(Font::Roman);
        if (name == QLatin1String("displaystyle")) {
            m_style = 0;
            return nullptr;
        }
        if (name == QLatin1String("textstyle")) {
            m_style = 1;
            return nullptr;
        }
        if (name == QLatin1String("hat") || name == QLatin1String("bar") || name == QLatin1String("vec") || name == QLatin1String("dot")
            || name == QLatin1String("ddot") || name == QLatin1String("tilde") || name == QLatin1String("check")
            || name == QLatin1String("overline") || name == QLatin1String("widehat") || name == QLatin1String("widetilde")
            || name == QLatin1String("overrightarrow"))
            return accent(cmd, group());
        if (name == QLatin1String("underline"))
            return underline(group());
        if (name == QLatin1String("boxed") || name == QLatin1String("fbox"))
            return boxed(group());
        if (name == QLatin1String("color") || name == QLatin1String("textcolor")) {
            // Colours become nothing in a gray world; the content stays.
            groupText();
            return name == QLatin1String("textcolor") ? group() : nullptr;
        }
        if (name == QLatin1String("not")) {
            BoxP b = atom();
            if (!b)
                return nullptr;
            auto slash = glyph(QStringLiteral("/"), mathFont(px(), Shape::Upright), b->kind);
            auto h = std::make_shared<HBox>();
            h->kind = b->kind;
            h->items = {b, slash};
            h->xs = {0, (b->w - slash->w) / 2};
            h->w = b->w;
            h->a = qMax(b->a, slash->a);
            h->d = qMax(b->d, slash->d);
            return h;
        }
        if (name == QLatin1String("limits") || name == QLatin1String("nolimits"))
            return nullptr;
        if (bigOps().contains(name)) {
            const auto op = bigOps().value(name);
            // Integral signs are drawn short in text fonts: stretch them more.
            const bool integral = name.contains(QLatin1String("int"));
            const double size = display() ? px() * (integral ? 2.0 : 1.55) : px() * (integral ? 1.3 : 1.15);
            auto b = glyph(QString::fromUtf8(op.first), mathFont(size, Shape::Upright), Kind::Op);
            // Centre on the axis.
            const double shift = (b->a - b->d) / 2 - axis();
            auto s = std::make_shared<ShiftBox>();
            s->inner = b;
            s->dy = -shift;
            s->w = b->w;
            s->a = b->a - shift;
            s->d = b->d + shift;
            s->kind = Kind::Op;
            s->limits = op.second;
            return s;
        }
        if (functions().contains(name)) {
            QString text = name;
            if (name == QLatin1String("liminf"))
                text = QStringLiteral("lim inf");
            else if (name == QLatin1String("limsup"))
                text = QStringLiteral("lim sup");
            else if (name == QLatin1String("argmax"))
                text = QStringLiteral("arg max");
            else if (name == QLatin1String("argmin"))
                text = QStringLiteral("arg min");
            auto b = glyph(text, mathFont(px(), Shape::Upright), Kind::Op);
            b->limits = functions().value(name);
            return b;
        }
        if (symbols().contains(name)) {
            const Sym sym = symbols().value(name);
            const QString text = QString::fromUtf8(sym.text);
            // Lowercase Greek is italic like letters; the rest upright.
            const bool greekLower = text.size() == 1 && text[0].unicode() >= 0x3B1 && text[0].unicode() <= 0x3D6;
            return glyph(text, mathFont(px(), greekLower && m_font != Font::Roman ? Shape::Italic : (m_font == Font::Bold ? Shape::Bold : Shape::Upright)),
                         sym.kind);
        }
        fail(QStringLiteral("Unknown command %1").arg(cmd));
        return glyph(cmd, mathFont(px(), Shape::Upright));
    }

    QString m_src;
    int m_pos = 0;
    double m_base;
    int m_style = 1;
    Font m_font = Font::Normal;
    QString m_error;
};

struct Cached {
    BoxP box;
    QString error;
};

Cached layout(const QString &tex, double px, bool display)
{
    static QHash<QString, Cached> cache;
    const QString key = QStringLiteral("%1|%2|%3").arg(px).arg(display).arg(tex);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return *it;
    Layout l(tex, px);
    Cached c;
    c.box = l.run(display);
    c.error = l.error();
    if (cache.size() > 400)
        cache.clear();
    cache.insert(key, c);
    return c;
}
} // namespace

QString mathFamily()
{
    static const QString family = pickFamily();
    return family;
}

Metrics measure(const QString &tex, double px, bool display)
{
    Metrics m;
    if (tex.trimmed().isEmpty())
        return m;
    const Cached c = layout(tex, px, display);
    m.width = c.box->w;
    m.ascent = c.box->a;
    m.descent = c.box->d;
    m.error = c.error;
    return m;
}

void paint(QPainter &p, const QPointF &origin, const QString &tex, double px, const QColor &color, bool display)
{
    if (tex.trimmed().isEmpty())
        return;
    const Cached c = layout(tex, px, display);
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setPen(color);
    c.box->draw(p, origin.x(), origin.y());
    p.restore();
}
} // namespace Tex
