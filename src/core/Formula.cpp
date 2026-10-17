#include "Formula.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QRegularExpression>
#include <QJsonArray>

#include <algorithm>
#include <cmath>

// ---- values ----------------------------------------------------------------------

FValue FValue::number(double v)
{
    FValue f;
    if (std::isnan(v))
        return f;
    f.type = Number;
    f.num = v;
    return f;
}

FValue FValue::string(const QString &v)
{
    FValue f;
    f.type = Text;
    f.text = v;
    return f;
}

FValue FValue::boolean(bool v)
{
    FValue f;
    f.type = Bool;
    f.flag = v;
    return f;
}

FValue FValue::dateTime(const QDateTime &v, bool withTime)
{
    FValue f;
    if (!v.isValid())
        return f;
    f.type = Date;
    f.date = v;
    f.hasTime = withTime;
    return f;
}

FValue FValue::dateRange(const QDateTime &start, const QDateTime &end, bool withTime)
{
    FValue f = dateTime(start, withTime);
    if (f.type == Date && end.isValid() && end != start)
        f.end = end;
    return f;
}

FValue FValue::page(const QString &id)
{
    FValue f;
    f.type = Page;
    f.text = id;
    return f;
}

FValue FValue::person(const QString &id)
{
    FValue f;
    f.type = Person;
    f.text = id;
    return f;
}

FValue FValue::makeList(const QList<FValue> &items)
{
    FValue f;
    f.type = List;
    f.list = items;
    return f;
}

bool FValue::isEmpty() const
{
    switch (type) {
    case Empty:
        return true;
    case Number:
        return false;
    case Text:
        return text.isEmpty();
    case Bool:
        return !flag;
    case Date:
        return !date.isValid();
    case List:
        return list.isEmpty();
    case Page:
    case Person:
        return text.isEmpty();
    }
    return true;
}

bool FValue::truthy() const
{
    switch (type) {
    case Bool:
        return flag;
    case Number:
        return num != 0;
    default:
        return !isEmpty();
    }
}

bool FValue::operator==(const FValue &o) const
{
    if (type != o.type) {
        // Empty equals an empty value of any type.
        return isEmpty() && o.isEmpty() && (type == Empty || o.type == Empty);
    }
    switch (type) {
    case Empty:
        return true;
    case Number:
        return num == o.num || std::fabs(num - o.num) < 1e-12 * std::max(1.0, std::fabs(num));
    case Text:
    case Page:
    case Person:
        return text == o.text;
    case Bool:
        return flag == o.flag;
    case Date:
        return date == o.date && end == o.end;
    case List:
        return list == o.list;
    }
    return false;
}

// ---- lexer -------------------------------------------------------------------------

namespace
{
QString tr(const char *s)
{
    return QCoreApplication::translate("Formula", s);
}

struct Token {
    enum Kind { End, Num, Str, Ident, Op } kind = End;
    QString text;
    double num = 0;
    int pos = 0;
};

bool lex(const QString &src, QList<Token> &out, QString *error)
{
    int i = 0;
    const int n = int(src.size());
    while (i < n) {
        const QChar c = src[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        Token t;
        t.pos = i;
        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && src[i + 1].isDigit())) {
            int j = i;
            while (j < n && (src[j].isDigit() || src[j] == QLatin1Char('.')))
                ++j;
            if (j < n && (src[j] == QLatin1Char('e') || src[j] == QLatin1Char('E'))) {
                int k = j + 1;
                if (k < n && (src[k] == QLatin1Char('+') || src[k] == QLatin1Char('-')))
                    ++k;
                if (k < n && src[k].isDigit()) {
                    j = k;
                    while (j < n && src[j].isDigit())
                        ++j;
                }
            }
            t.kind = Token::Num;
            bool ok = false;
            t.num = src.mid(i, j - i).toDouble(&ok);
            if (!ok) {
                *error = tr("Invalid number at %1").arg(i + 1);
                return false;
            }
            i = j;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'') || c == QChar(0x201C)) {
            const QChar close = c == QChar(0x201C) ? QChar(0x201D) : c;
            int j = i + 1;
            QString s;
            while (j < n && src[j] != close) {
                if (src[j] == QLatin1Char('\\') && j + 1 < n) {
                    const QChar e = src[j + 1];
                    s += e == QLatin1Char('n') ? QChar(QLatin1Char('\n')) : (e == QLatin1Char('t') ? QChar(QLatin1Char('\t')) : e);
                    j += 2;
                    continue;
                }
                s += src[j++];
            }
            if (j >= n) {
                *error = tr("Unterminated text at %1").arg(i + 1);
                return false;
            }
            t.kind = Token::Str;
            t.text = s;
            i = j + 1;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (src[j].isLetterOrNumber() || src[j] == QLatin1Char('_')))
                ++j;
            t.kind = Token::Ident;
            t.text = src.mid(i, j - i);
            i = j;
        } else {
            static const char *ops[] = {"==", "!=", "<=", ">=", "&&", "||", "+", "-", "*", "/", "%", "^", "<", ">",
                                        "!",  "?",  ":",  ",",  ".",  "(",  ")", "[", "]", "="};
            bool found = false;
            for (const char *op : ops) {
                const QLatin1String o(op);
                if (src.mid(i, o.size()) == o) {
                    t.kind = Token::Op;
                    t.text = o;
                    i += int(o.size());
                    found = true;
                    break;
                }
            }
            if (!found) {
                if (c == QChar(0x2260)) { // ≠
                    t.kind = Token::Op;
                    t.text = QStringLiteral("!=");
                    ++i;
                } else if (c == QChar(0x2264) || c == QChar(0x2265)) {
                    t.kind = Token::Op;
                    t.text = c == QChar(0x2264) ? QStringLiteral("<=") : QStringLiteral(">=");
                    ++i;
                } else {
                    *error = tr("Unexpected “%1” at %2").arg(c).arg(i + 1);
                    return false;
                }
            }
        }
        out.append(t);
    }
    Token end;
    end.pos = n;
    out.append(end);
    return true;
}

// ---- parser ------------------------------------------------------------------------

struct Node {
    enum Kind { Num, Str, Bool, ListLit, Var, Call, Unary, Binary, Ternary, Index } kind = Num;
    double num = 0;
    QString text; // string literal, variable, function or operator
    bool flag = false;
    QList<std::shared_ptr<Node>> args;
};
using NodeP = std::shared_ptr<Node>;

class Parser
{
public:
    explicit Parser(const QList<Token> &tokens) : m_t(tokens) {}

    NodeP parse(QString *error)
    {
        NodeP n = ternary();
        if (!m_error.isEmpty()) {
            *error = m_error;
            return nullptr;
        }
        if (peek().kind != Token::End) {
            *error = tr("Unexpected “%1” at %2").arg(peek().text).arg(peek().pos + 1);
            return nullptr;
        }
        return n;
    }

private:
    const Token &peek() const { return m_t[m_i]; }
    bool isOp(const char *op) const { return peek().kind == Token::Op && peek().text == QLatin1String(op); }
    bool isWord(const char *w) const { return peek().kind == Token::Ident && peek().text == QLatin1String(w); }
    void next()
    {
        if (m_i < m_t.size() - 1)
            ++m_i;
    }
    void fail(const QString &msg)
    {
        if (m_error.isEmpty())
            m_error = msg;
    }
    void expect(const char *op)
    {
        if (!isOp(op)) {
            fail(peek().kind == Token::End ? tr("Expected “%1” at the end").arg(QLatin1String(op))
                                           : tr("Expected “%1” at %2").arg(QLatin1String(op)).arg(peek().pos + 1));
            return;
        }
        next();
    }
    static NodeP make(Node::Kind k, const QString &text, QList<NodeP> args)
    {
        auto n = std::make_shared<Node>();
        n->kind = k;
        n->text = text;
        n->args = std::move(args);
        return n;
    }

    NodeP ternary()
    {
        NodeP c = logicalOr();
        if (isOp("?")) {
            next();
            NodeP a = ternary();
            expect(":");
            NodeP b = ternary();
            return make(Node::Ternary, {}, {c, a, b});
        }
        return c;
    }
    NodeP logicalOr()
    {
        NodeP l = logicalAnd();
        while (isOp("||") || isWord("or")) {
            next();
            l = make(Node::Binary, QStringLiteral("or"), {l, logicalAnd()});
        }
        return l;
    }
    NodeP logicalAnd()
    {
        NodeP l = equality();
        while (isOp("&&") || isWord("and")) {
            next();
            l = make(Node::Binary, QStringLiteral("and"), {l, equality()});
        }
        return l;
    }
    NodeP equality()
    {
        NodeP l = comparison();
        while (isOp("==") || isOp("!=") || isOp("=")) {
            const QString op = peek().text == QLatin1String("!=") ? QStringLiteral("!=") : QStringLiteral("==");
            next();
            l = make(Node::Binary, op, {l, comparison()});
        }
        return l;
    }
    NodeP comparison()
    {
        NodeP l = additive();
        while (isOp("<") || isOp(">") || isOp("<=") || isOp(">=")) {
            const QString op = peek().text;
            next();
            l = make(Node::Binary, op, {l, additive()});
        }
        return l;
    }
    NodeP additive()
    {
        NodeP l = multiplicative();
        while (isOp("+") || isOp("-")) {
            const QString op = peek().text;
            next();
            l = make(Node::Binary, op, {l, multiplicative()});
        }
        return l;
    }
    NodeP multiplicative()
    {
        NodeP l = unary();
        while (isOp("*") || isOp("/") || isOp("%")) {
            const QString op = peek().text;
            next();
            l = make(Node::Binary, op, {l, unary()});
        }
        return l;
    }
    NodeP unary()
    {
        if (isOp("-") || isOp("+")) {
            const QString op = peek().text;
            next();
            return make(Node::Unary, op, {unary()});
        }
        if (isOp("!") || isWord("not")) {
            next();
            return make(Node::Unary, QStringLiteral("!"), {unary()});
        }
        return power();
    }
    NodeP power()
    {
        NodeP base = postfix();
        if (isOp("^")) {
            next();
            return make(Node::Binary, QStringLiteral("^"), {base, unary()});
        }
        return base;
    }
    NodeP postfix()
    {
        NodeP n = primary();
        for (;;) {
            if (!m_error.isEmpty())
                return n;
            if (isOp(".")) {
                next();
                if (peek().kind != Token::Ident) {
                    fail(tr("Expected a function name after “.” at %1").arg(peek().pos + 1));
                    return n;
                }
                const QString name = peek().text;
                next();
                QList<NodeP> args{n};
                if (isOp("(")) {
                    next();
                    args += argList();
                }
                n = make(Node::Call, name, args);
            } else if (isOp("[")) {
                next();
                NodeP idx = ternary();
                expect("]");
                n = make(Node::Index, {}, {n, idx});
            } else {
                return n;
            }
        }
    }
    QList<NodeP> argList()
    {
        QList<NodeP> args;
        if (isOp(")")) {
            next();
            return args;
        }
        for (;;) {
            args.append(ternary());
            if (!m_error.isEmpty())
                return args;
            if (isOp(",")) {
                next();
                continue;
            }
            expect(")");
            return args;
        }
    }
    NodeP primary()
    {
        const Token t = peek();
        switch (t.kind) {
        case Token::Num: {
            next();
            auto n = std::make_shared<Node>();
            n->kind = Node::Num;
            n->num = t.num;
            return n;
        }
        case Token::Str: {
            next();
            return make(Node::Str, t.text, {});
        }
        case Token::Ident: {
            next();
            if (t.text == QLatin1String("true") || t.text == QLatin1String("false")) {
                auto n = std::make_shared<Node>();
                n->kind = Node::Bool;
                n->flag = t.text == QLatin1String("true");
                return n;
            }
            if (isOp("(")) {
                next();
                return make(Node::Call, t.text, argList());
            }
            return make(Node::Var, t.text, {});
        }
        case Token::Op:
            if (t.text == QLatin1String("(")) {
                next();
                NodeP n = ternary();
                expect(")");
                return n;
            }
            if (t.text == QLatin1String("[")) {
                next();
                QList<NodeP> items;
                if (isOp("]")) {
                    next();
                } else {
                    for (;;) {
                        items.append(ternary());
                        if (!m_error.isEmpty())
                            break;
                        if (isOp(",")) {
                            next();
                            continue;
                        }
                        expect("]");
                        break;
                    }
                }
                return make(Node::ListLit, {}, items);
            }
            fail(tr("Unexpected “%1” at %2").arg(t.text).arg(t.pos + 1));
            return make(Node::Num, {}, {});
        case Token::End:
            fail(tr("The formula ends too early"));
            return make(Node::Num, {}, {});
        }
        return make(Node::Num, {}, {});
    }

    const QList<Token> &m_t;
    int m_i = 0;
    QString m_error;
};

NodeP parseCached(const QString &src, QString *error)
{
    struct Entry {
        NodeP node;
        QString error;
    };
    static QHash<QString, Entry> cache;
    auto it = cache.constFind(src);
    if (it != cache.constEnd()) {
        *error = it->error;
        return it->node;
    }
    QList<Token> tokens;
    QString err;
    NodeP node;
    if (lex(src, tokens, &err)) {
        Parser p(tokens);
        node = p.parse(&err);
    }
    if (cache.size() > 800)
        cache.clear();
    cache.insert(src, {node, err});
    *error = err;
    return node;
}

// ---- evaluation --------------------------------------------------------------------

struct EvalError {
    QString message;
};

[[noreturn]] void raise(const QString &msg)
{
    throw EvalError{msg};
}

QString numberText(double v)
{
    if (std::isinf(v))
        return v > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    if (v == std::floor(v) && std::fabs(v) < 1e15)
        return QString::number(qint64(v));
    QString s = QString::number(v, 'g', 12);
    if (s.contains(QLatin1Char('e')))
        return s;
    return s;
}

QString dateText(const FValue &v)
{
    const QLocale en(QLocale::English);
    auto one = [&](const QDateTime &d) {
        QString s = en.toString(d.date(), QStringLiteral("MMMM d, yyyy"));
        if (v.hasTime)
            s += QLatin1Char(' ') + en.toString(d.time(), QStringLiteral("h:mm AP"));
        return s;
    };
    QString s = one(v.date);
    if (v.end.isValid())
        s += QStringLiteral(" → ") + one(v.end);
    return s;
}

class Evaluator
{
public:
    explicit Evaluator(const FormulaContext &ctx) : m_ctx(ctx)
    {
        m_now = ctx.now.isValid() ? ctx.now : QDateTime::currentDateTime();
    }

    FValue eval(const NodeP &n)
    {
        if (++m_steps > 200000)
            raise(tr("The formula takes too long"));
        switch (n->kind) {
        case Node::Num:
            return FValue::number(n->num);
        case Node::Str:
            return FValue::string(n->text);
        case Node::Bool:
            return FValue::boolean(n->flag);
        case Node::ListLit: {
            QList<FValue> items;
            for (const NodeP &a : n->args)
                items.append(eval(a));
            return FValue::makeList(items);
        }
        case Node::Var:
            return variable(n->text);
        case Node::Unary: {
            const FValue v = eval(n->args[0]);
            if (n->text == QLatin1String("!"))
                return FValue::boolean(!v.truthy());
            const double x = toNum(v, n->text);
            return FValue::number(n->text == QLatin1String("-") ? -x : x);
        }
        case Node::Binary:
            return binary(n);
        case Node::Ternary:
            return eval(n->args[0]).truthy() ? eval(n->args[1]) : eval(n->args[2]);
        case Node::Index: {
            const FValue l = eval(n->args[0]);
            const FValue i = eval(n->args[1]);
            return at(l, i);
        }
        case Node::Call:
            return call(n->text, n->args);
        }
        return {};
    }

private:
    FValue variable(const QString &name)
    {
        for (int i = int(m_scopes.size()) - 1; i >= 0; --i) {
            auto it = m_scopes[i].constFind(name);
            if (it != m_scopes[i].constEnd())
                return *it;
        }
        if (name == QLatin1String("pi"))
            return FValue::number(M_PI);
        if (name == QLatin1String("e"))
            return FValue::number(M_E);
        // Notion 2.0 shows properties as tokens; typing a bare property name
        // works when it has no spaces.
        if (m_ctx.prop) {
            QString err;
            FValue v = m_ctx.prop(QString(), name, &err);
            if (err.isEmpty())
                return v;
        }
        raise(tr("Unknown name “%1”").arg(name));
    }

    double toNum(const FValue &v, const QString &where)
    {
        switch (v.type) {
        case FValue::Number:
            return v.num;
        case FValue::Bool:
            return v.flag ? 1 : 0;
        case FValue::Empty:
            return 0;
        case FValue::Text: {
            bool ok = false;
            const double d = v.text.trimmed().toDouble(&ok);
            if (ok)
                return d;
            break;
        }
        default:
            break;
        }
        raise(tr("“%1” needs a number").arg(where));
    }

    QString toText(const FValue &v) { return Formula::display(v, &m_ctx); }

    FValue binary(const NodeP &n)
    {
        const QString &op = n->text;
        if (op == QLatin1String("and")) {
            if (!eval(n->args[0]).truthy())
                return FValue::boolean(false);
            return FValue::boolean(eval(n->args[1]).truthy());
        }
        if (op == QLatin1String("or")) {
            if (eval(n->args[0]).truthy())
                return FValue::boolean(true);
            return FValue::boolean(eval(n->args[1]).truthy());
        }
        const FValue a = eval(n->args[0]);
        const FValue b = eval(n->args[1]);
        return applyBinary(op, a, b);
    }

    FValue applyBinary(const QString &op, const FValue &a, const FValue &b)
    {
        if (op == QLatin1String("=="))
            return FValue::boolean(a == b);
        if (op == QLatin1String("!="))
            return FValue::boolean(!(a == b));
        if (op == QLatin1String("+")) {
            if (a.type == FValue::Text || b.type == FValue::Text)
                return FValue::string(toText(a) + toText(b));
            if (a.type == FValue::List && b.type == FValue::List)
                return FValue::makeList(a.list + b.list);
            return FValue::number(toNum(a, op) + toNum(b, op));
        }
        if (op == QLatin1String("-"))
            return FValue::number(toNum(a, op) - toNum(b, op));
        if (op == QLatin1String("*"))
            return FValue::number(toNum(a, op) * toNum(b, op));
        if (op == QLatin1String("/")) {
            const double d = toNum(b, op);
            if (d == 0)
                return FValue::number(toNum(a, op) == 0 ? NAN : (toNum(a, op) > 0 ? INFINITY : -INFINITY));
            return FValue::number(toNum(a, op) / d);
        }
        if (op == QLatin1String("%")) {
            const double d = toNum(b, op);
            if (d == 0)
                return {};
            return FValue::number(std::fmod(toNum(a, op), d));
        }
        if (op == QLatin1String("^"))
            return FValue::number(std::pow(toNum(a, op), toNum(b, op)));
        const int c = compare(a, b, op);
        if (op == QLatin1String("<"))
            return FValue::boolean(c < 0);
        if (op == QLatin1String(">"))
            return FValue::boolean(c > 0);
        if (op == QLatin1String("<="))
            return FValue::boolean(c <= 0);
        if (op == QLatin1String(">="))
            return FValue::boolean(c >= 0);
        raise(tr("Unknown operator “%1”").arg(op));
    }

    int compare(const FValue &a, const FValue &b, const QString &op)
    {
        if (a.type == FValue::Date && b.type == FValue::Date)
            return a.date < b.date ? -1 : (a.date > b.date ? 1 : 0);
        if (a.type == FValue::Text && b.type == FValue::Text)
            return a.text.compare(b.text);
        const double x = toNum(a, op), y = toNum(b, op);
        return x < y ? -1 : (x > y ? 1 : 0);
    }

    FValue at(const FValue &l, const FValue &i)
    {
        if (l.type == FValue::List) {
            int idx = int(toNum(i, QStringLiteral("at")));
            if (idx < 0)
                idx += int(l.list.size());
            return idx >= 0 && idx < l.list.size() ? l.list[idx] : FValue();
        }
        if (l.type == FValue::Text) {
            int idx = int(toNum(i, QStringLiteral("at")));
            if (idx < 0)
                idx += int(l.text.size());
            return idx >= 0 && idx < l.text.size() ? FValue::string(QString(l.text[idx])) : FValue();
        }
        return {};
    }

    QList<FValue> listOf(const FValue &v)
    {
        if (v.type == FValue::List)
            return v.list;
        if (v.isEmpty() && v.type != FValue::Number && v.type != FValue::Bool)
            return {};
        return {v};
    }

    // Numbers from arguments, flattening lists (sum(1, [2, 3])).
    QList<double> numbers(const QList<FValue> &args, const QString &fn)
    {
        QList<double> out;
        std::function<void(const FValue &)> add = [&](const FValue &v) {
            if (v.type == FValue::List) {
                for (const FValue &x : v.list)
                    add(x);
            } else if (v.type != FValue::Empty) {
                out.append(toNum(v, fn));
            }
        };
        for (const FValue &a : args)
            add(a);
        return out;
    }

    void arity(const QString &fn, const QList<NodeP> &args, int min, int max = -1)
    {
        if (args.size() < min || (max >= 0 && args.size() > max)) {
            if (min == max)
                raise(min == 1 ? tr("%1() takes 1 argument").arg(fn) : tr("%1() takes %2 arguments").arg(fn).arg(min));
            raise(tr("%1() was given the wrong number of arguments").arg(fn));
        }
    }

    // Runs a lambda body with `current` and `index` bound.
    FValue withCurrent(const NodeP &body, const FValue &current, int index)
    {
        QHash<QString, FValue> scope;
        scope.insert(QStringLiteral("current"), current);
        scope.insert(QStringLiteral("index"), FValue::number(index));
        m_scopes.append(scope);
        struct Pop {
            QList<QHash<QString, FValue>> &s;
            ~Pop() { s.removeLast(); }
        } pop{m_scopes};
        return eval(body);
    }

    QDateTime toDate(const FValue &v, const QString &fn)
    {
        if (v.type == FValue::Date)
            return v.date;
        if (v.type == FValue::Text) {
            const QDateTime d = parseDate(v.text);
            if (d.isValid())
                return d;
        }
        raise(tr("%1() needs a date").arg(fn));
    }

    static QDateTime parseDate(const QString &s)
    {
        const QString t = s.trimmed();
        QDateTime d = QDateTime::fromString(t, Qt::ISODate);
        if (d.isValid())
            return d.toLocalTime();
        d = QDateTime::fromString(t.left(16), QStringLiteral("yyyy-MM-ddTHH:mm"));
        if (d.isValid())
            return d;
        d = QDateTime::fromString(t.left(16), QStringLiteral("yyyy-MM-dd HH:mm"));
        if (d.isValid())
            return d;
        const QDate dd = QDate::fromString(t.left(10), Qt::ISODate);
        if (dd.isValid())
            return QDateTime(dd, QTime(0, 0));
        return {};
    }

    static QString unitKey(QString u)
    {
        u = u.toLower().trimmed();
        if (u.endsWith(QLatin1Char('s')))
            u.chop(1);
        return u;
    }

    static QDateTime addUnits(const QDateTime &d, double amount, const QString &unit)
    {
        const qint64 n = qint64(std::trunc(amount));
        const QString u = unitKey(unit);
        if (u == QLatin1String("year"))
            return d.addYears(int(n));
        if (u == QLatin1String("quarter"))
            return d.addMonths(int(n * 3));
        if (u == QLatin1String("month"))
            return d.addMonths(int(n));
        if (u == QLatin1String("week"))
            return d.addDays(n * 7);
        if (u == QLatin1String("day"))
            return d.addDays(n);
        if (u == QLatin1String("hour"))
            return d.addSecs(n * 3600);
        if (u == QLatin1String("minute"))
            return d.addSecs(n * 60);
        if (u == QLatin1String("second"))
            return d.addSecs(n);
        if (u == QLatin1String("millisecond"))
            return d.addMSecs(n);
        raise(tr("Unknown date unit “%1”").arg(unit));
    }

    static double between(const QDateTime &a, const QDateTime &b, const QString &unit)
    {
        const QString u = unitKey(unit);
        const qint64 ms = b.msecsTo(a);
        auto monthsBetween = [](const QDateTime &x, const QDateTime &y) {
            // Whole months from y to x, like moment's diff.
            int m = (x.date().year() - y.date().year()) * 12 + (x.date().month() - y.date().month());
            const QDateTime anchor = y.addMonths(m);
            if (m > 0 && anchor > x)
                --m;
            else if (m < 0 && anchor < x)
                ++m;
            return m;
        };
        if (u == QLatin1String("year"))
            return std::trunc(monthsBetween(a, b) / 12.0);
        if (u == QLatin1String("quarter"))
            return std::trunc(monthsBetween(a, b) / 3.0);
        if (u == QLatin1String("month"))
            return monthsBetween(a, b);
        if (u == QLatin1String("week"))
            return std::trunc(ms / (7.0 * 86400000.0));
        if (u == QLatin1String("day"))
            return std::trunc(ms / 86400000.0);
        if (u == QLatin1String("hour"))
            return std::trunc(ms / 3600000.0);
        if (u == QLatin1String("minute"))
            return std::trunc(ms / 60000.0);
        if (u == QLatin1String("second"))
            return std::trunc(ms / 1000.0);
        if (u == QLatin1String("millisecond"))
            return double(ms);
        raise(tr("Unknown date unit “%1”").arg(unit));
    }

    // Moment-style date formats ("MMMM D, YYYY", "HH:mm").
    static QString formatDate(const QDateTime &d, const QString &fmt)
    {
        const QLocale en(QLocale::English);
        QString out;
        int i = 0;
        const int n = int(fmt.size());
        auto starts = [&](const char *tok) { return fmt.mid(i, int(qstrlen(tok))) == QLatin1String(tok); };
        while (i < n) {
            if (fmt[i] == QLatin1Char('[')) {
                const int close = fmt.indexOf(QLatin1Char(']'), i);
                if (close > i) {
                    out += fmt.mid(i + 1, close - i - 1);
                    i = close + 1;
                    continue;
                }
            }
            struct Tok {
                const char *t;
                std::function<QString()> f;
            };
            const QList<Tok> toks = {
                {"YYYY", [&] { return QString::number(d.date().year()); }},
                {"YY", [&] { return QString::number(d.date().year() % 100).rightJustified(2, QLatin1Char('0')); }},
                {"MMMM", [&] { return en.monthName(d.date().month()); }},
                {"MMM", [&] { return en.monthName(d.date().month(), QLocale::ShortFormat); }},
                {"MM", [&] { return QString::number(d.date().month()).rightJustified(2, QLatin1Char('0')); }},
                {"M", [&] { return QString::number(d.date().month()); }},
                {"Do",
                 [&] {
                     const int day = d.date().day();
                     const char *suf = (day % 100 >= 11 && day % 100 <= 13) ? "th"
                                       : day % 10 == 1                       ? "st"
                                       : day % 10 == 2                       ? "nd"
                                       : day % 10 == 3                       ? "rd"
                                                                             : "th";
                     return QString::number(day) + QLatin1String(suf);
                 }},
                {"DD", [&] { return QString::number(d.date().day()).rightJustified(2, QLatin1Char('0')); }},
                {"D", [&] { return QString::number(d.date().day()); }},
                {"dddd", [&] { return en.dayName(d.date().dayOfWeek()); }},
                {"ddd", [&] { return en.dayName(d.date().dayOfWeek(), QLocale::ShortFormat); }},
                {"HH", [&] { return QString::number(d.time().hour()).rightJustified(2, QLatin1Char('0')); }},
                {"H", [&] { return QString::number(d.time().hour()); }},
                {"hh", [&] { return QString::number((d.time().hour() + 11) % 12 + 1).rightJustified(2, QLatin1Char('0')); }},
                {"h", [&] { return QString::number((d.time().hour() + 11) % 12 + 1); }},
                {"mm", [&] { return QString::number(d.time().minute()).rightJustified(2, QLatin1Char('0')); }},
                {"m", [&] { return QString::number(d.time().minute()); }},
                {"ss", [&] { return QString::number(d.time().second()).rightJustified(2, QLatin1Char('0')); }},
                {"A", [&] { return d.time().hour() < 12 ? QStringLiteral("AM") : QStringLiteral("PM"); }},
                {"a", [&] { return d.time().hour() < 12 ? QStringLiteral("am") : QStringLiteral("pm"); }},
                {"Q", [&] { return QString::number((d.date().month() - 1) / 3 + 1); }},
                {"W", [&] { return QString::number(d.date().weekNumber()); }},
                {"X", [&] { return QString::number(d.toSecsSinceEpoch()); }},
            };
            bool matched = false;
            for (const Tok &t : toks) {
                if (starts(t.t)) {
                    out += t.f();
                    i += int(qstrlen(t.t));
                    matched = true;
                    break;
                }
            }
            if (!matched)
                out += fmt[i++];
        }
        return out;
    }

    FValue call(const QString &fn, const QList<NodeP> &args);

    const FormulaContext &m_ctx;
    QDateTime m_now;
    QList<QHash<QString, FValue>> m_scopes;
    int m_steps = 0;
};

FValue Evaluator::call(const QString &fn, const QList<NodeP> &args)
{
    // ---- lazy forms ------------------------------------------------------
    if (fn == QLatin1String("if")) {
        arity(fn, args, 3, 3);
        return eval(args[0]).truthy() ? eval(args[1]) : eval(args[2]);
    }
    if (fn == QLatin1String("ifs")) {
        if (args.size() < 2)
            arity(fn, args, 2);
        int i = 0;
        for (; i + 1 < args.size(); i += 2)
            if (eval(args[i]).truthy())
                return eval(args[i + 1]);
        return i < args.size() ? eval(args[i]) : FValue();
    }
    if (fn == QLatin1String("and") || fn == QLatin1String("or")) {
        const bool isAnd = fn == QLatin1String("and");
        for (const NodeP &a : args) {
            const bool t = eval(a).truthy();
            if (isAnd && !t)
                return FValue::boolean(false);
            if (!isAnd && t)
                return FValue::boolean(true);
        }
        return FValue::boolean(isAnd);
    }
    if (fn == QLatin1String("let") || fn == QLatin1String("lets")) {
        if (args.size() < 3 || args.size() % 2 == 0)
            raise(tr("%1() needs name, value pairs and an expression").arg(fn));
        QHash<QString, FValue> scope;
        m_scopes.append(scope);
        struct Pop {
            QList<QHash<QString, FValue>> &s;
            ~Pop() { s.removeLast(); }
        } pop{m_scopes};
        for (int i = 0; i + 1 < args.size(); i += 2) {
            if (args[i]->kind != Node::Var)
                raise(tr("%1() needs a variable name").arg(fn));
            const FValue v = eval(args[i + 1]);
            m_scopes.last().insert(args[i]->text, v);
        }
        return eval(args.last());
    }
    if (fn == QLatin1String("prop")) {
        // prop("Name") on this page, or page.prop("Name") on a related page.
        arity(fn, args, 1, 2);
        const NodeP &nameNode = args.last();
        if (nameNode->kind != Node::Str)
            raise(tr("prop() needs a property name in quotes"));
        QString pageId;
        if (args.size() == 2) {
            const FValue page = eval(args[0]);
            if (page.type != FValue::Page)
                raise(tr("prop() can only be read from a page"));
            pageId = page.text;
        }
        if (!m_ctx.prop)
            raise(tr("Property “%1” isn't available here").arg(nameNode->text));
        QString err;
        FValue v = m_ctx.prop(pageId, nameNode->text, &err);
        if (!err.isEmpty())
            raise(err);
        return v;
    }
    static const QStringList lambdas = {QStringLiteral("map"),  QStringLiteral("filter"), QStringLiteral("find"),
                                        QStringLiteral("findIndex"), QStringLiteral("some"), QStringLiteral("every"),
                                        QStringLiteral("count")};
    if (lambdas.contains(fn) && args.size() == 2) {
        const FValue l = eval(args[0]);
        const QList<FValue> items = listOf(l);
        QList<FValue> out;
        for (int i = 0; i < items.size(); ++i) {
            const FValue r = withCurrent(args[1], items[i], i);
            if (fn == QLatin1String("map"))
                out.append(r);
            else if (fn == QLatin1String("filter") || fn == QLatin1String("count")) {
                if (r.truthy())
                    out.append(items[i]);
            } else if (fn == QLatin1String("find")) {
                if (r.truthy())
                    return items[i];
            } else if (fn == QLatin1String("findIndex")) {
                if (r.truthy())
                    return FValue::number(i);
            } else if (fn == QLatin1String("some")) {
                if (r.truthy())
                    return FValue::boolean(true);
            } else if (fn == QLatin1String("every")) {
                if (!r.truthy())
                    return FValue::boolean(false);
            }
        }
        if (fn == QLatin1String("find"))
            return {};
        if (fn == QLatin1String("findIndex"))
            return FValue::number(-1);
        if (fn == QLatin1String("some"))
            return FValue::boolean(false);
        if (fn == QLatin1String("every"))
            return FValue::boolean(true);
        if (fn == QLatin1String("count"))
            return FValue::number(out.size());
        return FValue::makeList(out);
    }

    // ---- eager functions -------------------------------------------------
    QList<FValue> a;
    a.reserve(args.size());
    for (const NodeP &n : args)
        a.append(eval(n));
    auto need = [&](int min, int max = -1) { arity(fn, args, min, max); };
    auto num = [&](int i) { return toNum(a[i], fn); };
    auto text = [&](int i) { return toText(a[i]); };

    // Text --------------------------------------------------------------------
    if (fn == QLatin1String("length")) {
        need(1, 1);
        return FValue::number(a[0].type == FValue::List ? a[0].list.size() : text(0).size());
    }
    if (fn == QLatin1String("concat")) {
        bool allLists = !a.isEmpty();
        for (const FValue &v : a)
            if (v.type != FValue::List)
                allLists = false;
        if (allLists) {
            QList<FValue> out;
            for (const FValue &v : a)
                out += v.list;
            return FValue::makeList(out);
        }
        QString s;
        for (int i = 0; i < a.size(); ++i)
            s += text(i);
        return FValue::string(s);
    }
    if (fn == QLatin1String("join")) {
        need(1, 2);
        const QString sep = a.size() > 1 ? text(1) : QString();
        if (a[0].type == FValue::List) {
            QStringList parts;
            for (const FValue &v : a[0].list)
                parts.append(toText(v));
            return FValue::string(parts.join(sep));
        }
        // 1.0 form: join(separator, text…)
        QStringList parts;
        for (int i = 1; i < a.size(); ++i)
            parts.append(text(i));
        return FValue::string(parts.join(text(0)));
    }
    if (fn == QLatin1String("slice") || fn == QLatin1String("substring")) {
        need(2, 3);
        if (a[0].type == FValue::List) {
            const int size = int(a[0].list.size());
            int s = int(num(1)), e = a.size() > 2 ? int(num(2)) : size;
            if (s < 0)
                s = qMax(0, size + s);
            if (e < 0)
                e = qMax(0, size + e);
            s = qMin(s, size);
            e = qBound(s, e, size);
            return FValue::makeList(a[0].list.mid(s, e - s));
        }
        const QString t = text(0);
        const int size = int(t.size());
        int s = int(num(1)), e = a.size() > 2 ? int(num(2)) : size;
        if (s < 0)
            s = qMax(0, size + s);
        if (e < 0)
            e = qMax(0, size + e);
        s = qMin(s, size);
        e = qBound(s, e, size);
        return FValue::string(t.mid(s, e - s));
    }
    if (fn == QLatin1String("contains")) {
        need(2, 2);
        if (a[0].type == FValue::List)
            return FValue::boolean(a[0].list.contains(a[1]) || [&] {
                for (const FValue &v : a[0].list)
                    if (toText(v) == text(1))
                        return true;
                return false;
            }());
        return FValue::boolean(text(0).contains(text(1)));
    }
    if (fn == QLatin1String("includes")) {
        need(2, 2);
        if (a[0].type == FValue::List)
            return FValue::boolean(a[0].list.contains(a[1]));
        return FValue::boolean(text(0).contains(text(1)));
    }
    if (fn == QLatin1String("test") || fn == QLatin1String("match") || fn == QLatin1String("replace")
        || fn == QLatin1String("replaceAll")) {
        const bool rep = fn.startsWith(QLatin1String("replace"));
        need(rep ? 3 : 2, rep ? 3 : 2);
        const QRegularExpression re(text(1));
        if (!re.isValid())
            raise(tr("Invalid pattern “%1”").arg(text(1)));
        const QString t = text(0);
        if (fn == QLatin1String("test"))
            return FValue::boolean(re.match(t).hasMatch());
        if (fn == QLatin1String("match")) {
            QList<FValue> out;
            auto it = re.globalMatch(t);
            while (it.hasNext())
                out.append(FValue::string(it.next().captured(0)));
            return FValue::makeList(out);
        }
        // JavaScript-style $1 back references.
        const QString with = text(2);
        QString result;
        int last = 0;
        auto it = re.globalMatch(t);
        while (it.hasNext()) {
            const auto m = it.next();
            result += t.mid(last, m.capturedStart() - last);
            QString r = with;
            for (int g = 9; g >= 0; --g)
                r.replace(QLatin1Char('$') + QString::number(g), m.captured(g));
            result += r;
            last = int(m.capturedEnd());
            if (fn == QLatin1String("replace"))
                break;
        }
        result += t.mid(last);
        return FValue::string(result);
    }
    if (fn == QLatin1String("lower")) {
        need(1, 1);
        return FValue::string(text(0).toLower());
    }
    if (fn == QLatin1String("upper")) {
        need(1, 1);
        return FValue::string(text(0).toUpper());
    }
    if (fn == QLatin1String("trim")) {
        need(1, 1);
        return FValue::string(text(0).trimmed());
    }
    if (fn == QLatin1String("repeat")) {
        need(2, 2);
        return FValue::string(text(0).repeated(qBound(0, int(num(1)), 10000)));
    }
    if (fn == QLatin1String("padStart") || fn == QLatin1String("padEnd")) {
        need(3, 3);
        QString t = text(0);
        const int len = qBound(0, int(num(1)), 10000);
        const QString pad = text(2);
        if (pad.isEmpty())
            return FValue::string(t);
        QString fill;
        while (t.size() + fill.size() < len)
            fill += pad;
        fill = fill.left(qMax(0, len - int(t.size())));
        return FValue::string(fn == QLatin1String("padStart") ? fill + t : t + fill);
    }
    if (fn == QLatin1String("split")) {
        need(1, 2);
        const QStringList parts = a.size() > 1 ? text(0).split(text(1)) : QStringList{text(0)};
        QList<FValue> out;
        for (const QString &p : parts)
            out.append(FValue::string(p));
        return FValue::makeList(out);
    }
    if (fn == QLatin1String("link")) {
        need(2, 2);
        return FValue::string(text(0));
    }
    if (fn == QLatin1String("style") || fn == QLatin1String("unstyle")) {
        need(1);
        return FValue::string(text(0));
    }
    if (fn == QLatin1String("format")) {
        need(1, 1);
        return FValue::string(text(0));
    }
    if (fn == QLatin1String("toNumber")) {
        need(1, 1);
        if (a[0].type == FValue::Date)
            return FValue::number(double(a[0].date.toMSecsSinceEpoch()));
        if (a[0].type == FValue::Bool)
            return FValue::number(a[0].flag ? 1 : 0);
        if (a[0].type == FValue::Number)
            return a[0];
        // Leading number of the text, like Notion ("12 apples" → 12).
        static const QRegularExpression lead(QStringLiteral("^\\s*(-?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?)"));
        const auto m = lead.match(text(0));
        return m.hasMatch() ? FValue::number(m.captured(1).toDouble()) : FValue();
    }

    // Logic ------------------------------------------------------------------
    if (fn == QLatin1String("not")) {
        need(1, 1);
        return FValue::boolean(!a[0].truthy());
    }
    if (fn == QLatin1String("empty")) {
        need(0, 1);
        if (a.isEmpty())
            return {};
        // Notion: 0, "", [] and false are empty.
        if (a[0].type == FValue::Number)
            return FValue::boolean(a[0].num == 0);
        return FValue::boolean(a[0].isEmpty());
    }
    if (fn == QLatin1String("equal")) {
        need(2, 2);
        return FValue::boolean(a[0] == a[1]);
    }
    if (fn == QLatin1String("unequal")) {
        need(2, 2);
        return FValue::boolean(!(a[0] == a[1]));
    }

    // Math -------------------------------------------------------------------
    struct Math1 {
        const char *name;
        double (*f)(double);
    };
    static const Math1 math1[] = {
        {"abs", [](double x) { return std::fabs(x); }},   {"ceil", [](double x) { return std::ceil(x); }},
        {"floor", [](double x) { return std::floor(x); }}, {"sqrt", [](double x) { return std::sqrt(x); }},
        {"cbrt", [](double x) { return std::cbrt(x); }},   {"exp", [](double x) { return std::exp(x); }},
        {"ln", [](double x) { return std::log(x); }},      {"log10", [](double x) { return std::log10(x); }},
        {"log2", [](double x) { return std::log2(x); }},
        {"sign", [](double x) { return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0); }},
        {"unaryMinus", [](double x) { return -x; }},       {"unaryPlus", [](double x) { return x; }},
    };
    for (const Math1 &m : math1) {
        if (fn == QLatin1String(m.name)) {
            need(1, 1);
            return FValue::number(m.f(num(0)));
        }
    }
    if (fn == QLatin1String("round")) {
        need(1, 2);
        const int places = a.size() > 1 ? int(num(1)) : 0;
        const double f = std::pow(10.0, places);
        // Notion rounds half up (toward +∞), like JavaScript's Math.round.
        return FValue::number(std::floor(num(0) * f + 0.5) / f);
    }
    if (fn == QLatin1String("pi")) {
        need(0, 0);
        return FValue::number(M_PI);
    }
    if (fn == QLatin1String("e")) {
        need(0, 0);
        return FValue::number(M_E);
    }
    if (fn == QLatin1String("add")) {
        need(2, 2);
        return applyBinary(QStringLiteral("+"), a[0], a[1]);
    }
    if (fn == QLatin1String("subtract") && !(a.size() == 3 && (a[0].type == FValue::Date))) {
        need(2, 2);
        return applyBinary(QStringLiteral("-"), a[0], a[1]);
    }
    if (fn == QLatin1String("multiply")) {
        need(2, 2);
        return applyBinary(QStringLiteral("*"), a[0], a[1]);
    }
    if (fn == QLatin1String("divide")) {
        need(2, 2);
        return applyBinary(QStringLiteral("/"), a[0], a[1]);
    }
    if (fn == QLatin1String("pow")) {
        need(2, 2);
        return applyBinary(QStringLiteral("^"), a[0], a[1]);
    }
    if (fn == QLatin1String("mod")) {
        need(2, 2);
        return applyBinary(QStringLiteral("%"), a[0], a[1]);
    }
    if (fn == QLatin1String("larger") || fn == QLatin1String("smaller")) {
        need(2, 2);
        const bool l = applyBinary(QStringLiteral(">"), a[0], a[1]).flag;
        return (fn == QLatin1String("larger")) == l ? a[0] : a[1];
    }
    if (fn == QLatin1String("max") || fn == QLatin1String("min") || fn == QLatin1String("sum")
        || fn == QLatin1String("mean") || fn == QLatin1String("average") || fn == QLatin1String("median")) {
        const QList<double> xs = numbers(a, fn);
        if (xs.isEmpty())
            return fn == QLatin1String("sum") ? FValue::number(0) : FValue();
        if (fn == QLatin1String("max"))
            return FValue::number(*std::max_element(xs.begin(), xs.end()));
        if (fn == QLatin1String("min"))
            return FValue::number(*std::min_element(xs.begin(), xs.end()));
        double s = 0;
        for (double x : xs)
            s += x;
        if (fn == QLatin1String("sum"))
            return FValue::number(s);
        if (fn == QLatin1String("median")) {
            QList<double> v = xs;
            std::sort(v.begin(), v.end());
            const int n = int(v.size());
            return FValue::number(n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0);
        }
        return FValue::number(s / xs.size());
    }

    // Dates --------------------------------------------------------------------
    if (fn == QLatin1String("now")) {
        need(0, 0);
        return FValue::dateTime(m_now, true);
    }
    if (fn == QLatin1String("today")) {
        need(0, 0);
        return FValue::dateTime(QDateTime(m_now.date(), QTime(0, 0)), false);
    }
    if (fn == QLatin1String("minute") || fn == QLatin1String("hour") || fn == QLatin1String("day")
        || fn == QLatin1String("date") || fn == QLatin1String("week") || fn == QLatin1String("month")
        || fn == QLatin1String("year")) {
        need(1, 1);
        if (a[0].isEmpty())
            return {};
        const QDateTime d = toDate(a[0], fn);
        if (fn == QLatin1String("minute"))
            return FValue::number(d.time().minute());
        if (fn == QLatin1String("hour"))
            return FValue::number(d.time().hour());
        if (fn == QLatin1String("day"))
            return FValue::number(d.date().dayOfWeek()); // 1 Monday … 7 Sunday
        if (fn == QLatin1String("date"))
            return FValue::number(d.date().day());
        if (fn == QLatin1String("week"))
            return FValue::number(d.date().weekNumber());
        if (fn == QLatin1String("month"))
            return FValue::number(d.date().month());
        return FValue::number(d.date().year());
    }
    if (fn == QLatin1String("dateAdd") || fn == QLatin1String("dateSubtract")
        || (fn == QLatin1String("subtract") && a.size() == 3)) {
        need(3, 3);
        if (a[0].isEmpty())
            return {};
        const QDateTime d = toDate(a[0], fn);
        const double amt = fn == QLatin1String("dateAdd") ? num(1) : -num(1);
        FValue out = FValue::dateTime(addUnits(d, amt, text(2)), a[0].hasTime || a[0].type != FValue::Date);
        if (a[0].type == FValue::Date)
            out.hasTime = a[0].hasTime || !QStringList{QStringLiteral("year"), QStringLiteral("quarter"), QStringLiteral("month"),
                                                       QStringLiteral("week"), QStringLiteral("day")}
                                               .contains(unitKey(text(2)));
        if (a[0].end.isValid())
            out.end = addUnits(a[0].end, amt, text(2));
        return out;
    }
    if (fn == QLatin1String("dateBetween")) {
        need(3, 3);
        if (a[0].isEmpty() || a[1].isEmpty())
            return {};
        return FValue::number(between(toDate(a[0], fn), toDate(a[1], fn), text(2)));
    }
    if (fn == QLatin1String("dateRange")) {
        need(2, 2);
        const QDateTime s = toDate(a[0], fn), e = toDate(a[1], fn);
        return FValue::dateRange(s, e, a[0].hasTime || a[1].hasTime);
    }
    if (fn == QLatin1String("dateStart") || fn == QLatin1String("start")) {
        need(1, 1);
        if (a[0].type != FValue::Date)
            return a[0].isEmpty() ? FValue() : FValue::dateTime(toDate(a[0], fn), true);
        return FValue::dateTime(a[0].date, a[0].hasTime);
    }
    if (fn == QLatin1String("dateEnd") || fn == QLatin1String("end")) {
        need(1, 1);
        if (a[0].type != FValue::Date)
            return a[0].isEmpty() ? FValue() : FValue::dateTime(toDate(a[0], fn), true);
        return FValue::dateTime(a[0].end.isValid() ? a[0].end : a[0].date, a[0].hasTime);
    }
    if (fn == QLatin1String("timestamp")) {
        need(1, 1);
        if (a[0].isEmpty())
            return {};
        return FValue::number(double(toDate(a[0], fn).toMSecsSinceEpoch()));
    }
    if (fn == QLatin1String("fromTimestamp")) {
        need(1, 1);
        return FValue::dateTime(QDateTime::fromMSecsSinceEpoch(qint64(num(0))), true);
    }
    if (fn == QLatin1String("formatDate")) {
        need(2, 2);
        if (a[0].isEmpty())
            return FValue::string(QString());
        return FValue::string(formatDate(toDate(a[0], fn), text(1)));
    }
    if (fn == QLatin1String("parseDate")) {
        need(1, 1);
        const QString t = text(0);
        const QDateTime d = parseDate(t);
        if (!d.isValid())
            raise(tr("Can't read “%1” as a date").arg(t));
        return FValue::dateTime(d, t.trimmed().size() > 10);
    }

    // Lists ----------------------------------------------------------------------
    if (fn == QLatin1String("at")) {
        need(2, 2);
        return at(a[0], a[1]);
    }
    if (fn == QLatin1String("first") || fn == QLatin1String("last")) {
        need(1, 1);
        const QList<FValue> l = listOf(a[0]);
        if (l.isEmpty())
            return {};
        return fn == QLatin1String("first") ? l.first() : l.last();
    }
    if (fn == QLatin1String("sort")) {
        need(1, 1);
        QList<FValue> l = listOf(a[0]);
        std::stable_sort(l.begin(), l.end(), [&](const FValue &x, const FValue &y) {
            if (x.type == FValue::Number && y.type == FValue::Number)
                return x.num < y.num;
            if (x.type == FValue::Date && y.type == FValue::Date)
                return x.date < y.date;
            return QString::localeAwareCompare(toText(x), toText(y)) < 0;
        });
        return FValue::makeList(l);
    }
    if (fn == QLatin1String("reverse")) {
        need(1, 1);
        QList<FValue> l = listOf(a[0]);
        std::reverse(l.begin(), l.end());
        return FValue::makeList(l);
    }
    if (fn == QLatin1String("unique")) {
        need(1, 1);
        QList<FValue> out;
        for (const FValue &v : listOf(a[0]))
            if (!out.contains(v))
                out.append(v);
        return FValue::makeList(out);
    }
    if (fn == QLatin1String("flat")) {
        need(1, 1);
        QList<FValue> out;
        for (const FValue &v : listOf(a[0]))
            out += v.type == FValue::List ? v.list : QList<FValue>{v};
        return FValue::makeList(out);
    }
    if (fn == QLatin1String("count")) {
        need(1, 1);
        return FValue::number(listOf(a[0]).size());
    }
    if (fn == QLatin1String("splice")) {
        need(3);
        QList<FValue> l = listOf(a[0]);
        const int start = qBound(0, int(num(1)), int(l.size()));
        const int del = qBound(0, int(num(2)), int(l.size()) - start);
        l.remove(start, del);
        for (int i = int(a.size()) - 1; i >= 3; --i)
            l.insert(start, a[i]);
        return FValue::makeList(l);
    }

    // Other ------------------------------------------------------------------------
    if (fn == QLatin1String("id")) {
        need(0, 1);
        if (!a.isEmpty() && a[0].type == FValue::Page)
            return FValue::string(a[0].text);
        return FValue::string(m_ctx.pageId);
    }
    if (fn == QLatin1String("name")) {
        need(1, 1);
        if (a[0].type == FValue::Person)
            return FValue::string(m_ctx.personName ? m_ctx.personName(a[0].text) : a[0].text);
        if (a[0].type == FValue::Page)
            return FValue::string(m_ctx.pageTitle ? m_ctx.pageTitle(a[0].text) : a[0].text);
        if (a[0].type == FValue::List) {
            QList<FValue> out;
            for (const FValue &v : a[0].list)
                out.append(FValue::string(toText(v)));
            return FValue::makeList(out);
        }
        return FValue::string(text(0));
    }
    if (fn == QLatin1String("email")) {
        need(1, 1);
        return FValue::string(QString());
    }
    if (fn == QLatin1String("toText") || fn == QLatin1String("string")) {
        need(1, 1);
        return FValue::string(text(0));
    }
    raise(tr("Unknown function “%1”").arg(fn));
}

thread_local int t_depth = 0;
} // namespace

namespace Formula
{

FormulaResult evaluate(const QString &expression, const FormulaContext &ctx)
{
    FormulaResult r;
    if (expression.trimmed().isEmpty())
        return r;
    QString error;
    NodeP root = parseCached(expression, &error);
    if (!root) {
        r.error = error;
        return r;
    }
    if (t_depth > 12) {
        r.error = QCoreApplication::translate("Formula", "The formula refers to itself");
        return r;
    }
    ++t_depth;
    struct Down {
        ~Down() { --t_depth; }
    } down;
    try {
        Evaluator ev(ctx);
        r.value = ev.eval(root);
        if (r.value.type == FValue::Number && (std::isnan(r.value.num)))
            r.value = FValue();
    } catch (const EvalError &e) {
        r.error = e.message;
    }
    return r;
}

QString check(const QString &expression)
{
    if (expression.trimmed().isEmpty())
        return {};
    QString error;
    parseCached(expression, &error);
    return error;
}

QStringList referencedProps(const QString &expression)
{
    QStringList out;
    QString error;
    NodeP root = parseCached(expression, &error);
    if (!root)
        return out;
    std::function<void(const NodeP &)> walk = [&](const NodeP &n) {
        if (n->kind == Node::Call && n->text == QLatin1String("prop") && !n->args.isEmpty() && n->args.last()->kind == Node::Str)
            out.append(n->args.last()->text);
        for (const NodeP &c : n->args)
            walk(c);
    };
    walk(root);
    out.removeDuplicates();
    return out;
}

QString display(const FValue &v, const FormulaContext *ctx)
{
    switch (v.type) {
    case FValue::Empty:
        return {};
    case FValue::Number:
        return numberText(v.num);
    case FValue::Text:
        return v.text;
    case FValue::Bool:
        return v.flag ? QStringLiteral("true") : QStringLiteral("false");
    case FValue::Date:
        return dateText(v);
    case FValue::List: {
        QStringList parts;
        for (const FValue &x : v.list)
            parts.append(display(x, ctx));
        return parts.join(QStringLiteral(", "));
    }
    case FValue::Page:
        return ctx && ctx->pageTitle ? ctx->pageTitle(v.text) : v.text;
    case FValue::Person:
        return ctx && ctx->personName ? ctx->personName(v.text) : v.text;
    }
    return {};
}

QJsonValue toJson(const FValue &v, const FormulaContext *ctx)
{
    switch (v.type) {
    case FValue::Empty:
        return QJsonValue();
    case FValue::Number:
        return std::isfinite(v.num) ? QJsonValue(v.num) : QJsonValue(numberText(v.num));
    case FValue::Bool:
        return v.flag;
    case FValue::Date: {
        const QString f = v.hasTime ? QStringLiteral("yyyy-MM-ddTHH:mm") : QStringLiteral("yyyy-MM-dd");
        QString s = v.date.toString(f);
        if (v.end.isValid())
            s += QLatin1Char('/') + v.end.toString(f);
        return s;
    }
    case FValue::List: {
        QJsonArray arr;
        for (const FValue &x : v.list)
            arr.append(display(x, ctx));
        return arr;
    }
    default:
        return display(v, ctx);
    }
}

QString typeName(const FValue &v)
{
    switch (v.type) {
    case FValue::Number:
        return QStringLiteral("number");
    case FValue::Bool:
        return QStringLiteral("bool");
    case FValue::Date:
        return QStringLiteral("date");
    case FValue::List:
        return QStringLiteral("list");
    default:
        return QStringLiteral("text");
    }
}

const QList<FunctionDoc> &functions()
{
    static const QList<FunctionDoc> docs = [] {
        auto t = [](const char *s) { return QCoreApplication::translate("Formula", s); };
        QList<FunctionDoc> d = {
            {QStringLiteral("prop"), QStringLiteral("prop(\"Name\")"), t("The value of a property of this page."), t("Properties")},
            {QStringLiteral("if"), QStringLiteral("if(condition, then, else)"), t("Picks a value by a condition."), t("Logic")},
            {QStringLiteral("ifs"), QStringLiteral("ifs(cond1, value1, cond2, value2, …, else)"), t("The first value whose condition is true."), t("Logic")},
            {QStringLiteral("and"), QStringLiteral("a and b"), t("True when both are true."), t("Logic")},
            {QStringLiteral("or"), QStringLiteral("a or b"), t("True when either is true."), t("Logic")},
            {QStringLiteral("not"), QStringLiteral("not a"), t("The opposite of a."), t("Logic")},
            {QStringLiteral("empty"), QStringLiteral("empty(value)"), t("True for empty text, 0, false and empty lists."), t("Logic")},
            {QStringLiteral("length"), QStringLiteral("length(text or list)"), t("Number of characters or items."), t("Text")},
            {QStringLiteral("concat"), QStringLiteral("concat(list, list…)"), t("Joins lists (or texts) together."), t("Text")},
            {QStringLiteral("join"), QStringLiteral("join(list, separator)"), t("Joins a list into text."), t("Text")},
            {QStringLiteral("slice"), QStringLiteral("slice(text, start, end)"), t("Part of a text or list."), t("Text")},
            {QStringLiteral("contains"), QStringLiteral("contains(text, search)"), t("True when the text contains the search."), t("Text")},
            {QStringLiteral("test"), QStringLiteral("test(text, pattern)"), t("True when the text matches the regular expression."), t("Text")},
            {QStringLiteral("match"), QStringLiteral("match(text, pattern)"), t("Every match of a regular expression."), t("Text")},
            {QStringLiteral("replace"), QStringLiteral("replace(text, pattern, with)"), t("Replaces the first match."), t("Text")},
            {QStringLiteral("replaceAll"), QStringLiteral("replaceAll(text, pattern, with)"), t("Replaces every match."), t("Text")},
            {QStringLiteral("lower"), QStringLiteral("lower(text)"), t("Lowercase."), t("Text")},
            {QStringLiteral("upper"), QStringLiteral("upper(text)"), t("Uppercase."), t("Text")},
            {QStringLiteral("trim"), QStringLiteral("trim(text)"), t("Removes spaces at both ends."), t("Text")},
            {QStringLiteral("repeat"), QStringLiteral("repeat(text, times)"), t("Repeats a text."), t("Text")},
            {QStringLiteral("padStart"), QStringLiteral("padStart(text, length, pad)"), t("Pads the start to a length."), t("Text")},
            {QStringLiteral("padEnd"), QStringLiteral("padEnd(text, length, pad)"), t("Pads the end to a length."), t("Text")},
            {QStringLiteral("split"), QStringLiteral("split(text, separator)"), t("Splits a text into a list."), t("Text")},
            {QStringLiteral("format"), QStringLiteral("format(value)"), t("Any value as text."), t("Text")},
            {QStringLiteral("toNumber"), QStringLiteral("toNumber(value)"), t("Text, dates and booleans as a number."), t("Math")},
            {QStringLiteral("abs"), QStringLiteral("abs(n)"), t("Absolute value."), t("Math")},
            {QStringLiteral("round"), QStringLiteral("round(n, places)"), t("Rounds to a number of decimal places."), t("Math")},
            {QStringLiteral("ceil"), QStringLiteral("ceil(n)"), t("Rounds up."), t("Math")},
            {QStringLiteral("floor"), QStringLiteral("floor(n)"), t("Rounds down."), t("Math")},
            {QStringLiteral("sqrt"), QStringLiteral("sqrt(n)"), t("Square root."), t("Math")},
            {QStringLiteral("cbrt"), QStringLiteral("cbrt(n)"), t("Cube root."), t("Math")},
            {QStringLiteral("exp"), QStringLiteral("exp(n)"), t("e to the power of n."), t("Math")},
            {QStringLiteral("ln"), QStringLiteral("ln(n)"), t("Natural logarithm."), t("Math")},
            {QStringLiteral("log10"), QStringLiteral("log10(n)"), t("Base-10 logarithm."), t("Math")},
            {QStringLiteral("log2"), QStringLiteral("log2(n)"), t("Base-2 logarithm."), t("Math")},
            {QStringLiteral("sign"), QStringLiteral("sign(n)"), t("1, 0 or -1."), t("Math")},
            {QStringLiteral("pow"), QStringLiteral("pow(base, exponent)"), t("Power (also a ^ b)."), t("Math")},
            {QStringLiteral("mod"), QStringLiteral("mod(a, b)"), t("Remainder (also a % b)."), t("Math")},
            {QStringLiteral("min"), QStringLiteral("min(a, b, …)"), t("Smallest number."), t("Math")},
            {QStringLiteral("max"), QStringLiteral("max(a, b, …)"), t("Largest number."), t("Math")},
            {QStringLiteral("sum"), QStringLiteral("sum(a, b, …)"), t("Sum of numbers or a list."), t("Math")},
            {QStringLiteral("mean"), QStringLiteral("mean(a, b, …)"), t("Average."), t("Math")},
            {QStringLiteral("median"), QStringLiteral("median(a, b, …)"), t("Middle value."), t("Math")},
            {QStringLiteral("pi"), QStringLiteral("pi()"), t("π."), t("Math")},
            {QStringLiteral("e"), QStringLiteral("e()"), t("Euler's number."), t("Math")},
            {QStringLiteral("now"), QStringLiteral("now()"), t("The current date and time."), t("Date")},
            {QStringLiteral("today"), QStringLiteral("today()"), t("Today's date."), t("Date")},
            {QStringLiteral("minute"), QStringLiteral("minute(date)"), t("Minute (0–59)."), t("Date")},
            {QStringLiteral("hour"), QStringLiteral("hour(date)"), t("Hour (0–23)."), t("Date")},
            {QStringLiteral("day"), QStringLiteral("day(date)"), t("Day of the week, 1 (Monday) to 7 (Sunday)."), t("Date")},
            {QStringLiteral("date"), QStringLiteral("date(date)"), t("Day of the month."), t("Date")},
            {QStringLiteral("week"), QStringLiteral("week(date)"), t("ISO week number."), t("Date")},
            {QStringLiteral("month"), QStringLiteral("month(date)"), t("Month, 1 to 12."), t("Date")},
            {QStringLiteral("year"), QStringLiteral("year(date)"), t("Year."), t("Date")},
            {QStringLiteral("dateAdd"), QStringLiteral("dateAdd(date, amount, \"days\")"), t("Adds time: years, quarters, months, weeks, days, hours, minutes."), t("Date")},
            {QStringLiteral("dateSubtract"), QStringLiteral("dateSubtract(date, amount, \"days\")"), t("Subtracts time."), t("Date")},
            {QStringLiteral("dateBetween"), QStringLiteral("dateBetween(a, b, \"days\")"), t("Time between two dates (a − b)."), t("Date")},
            {QStringLiteral("dateRange"), QStringLiteral("dateRange(start, end)"), t("A date range."), t("Date")},
            {QStringLiteral("dateStart"), QStringLiteral("dateStart(range)"), t("Start of a date range."), t("Date")},
            {QStringLiteral("dateEnd"), QStringLiteral("dateEnd(range)"), t("End of a date range."), t("Date")},
            {QStringLiteral("timestamp"), QStringLiteral("timestamp(date)"), t("Milliseconds since 1970."), t("Date")},
            {QStringLiteral("fromTimestamp"), QStringLiteral("fromTimestamp(ms)"), t("A date from milliseconds since 1970."), t("Date")},
            {QStringLiteral("formatDate"), QStringLiteral("formatDate(date, \"MMMM D, YYYY\")"), t("A date as text, with Moment-style tokens."), t("Date")},
            {QStringLiteral("parseDate"), QStringLiteral("parseDate(\"2026-10-17\")"), t("An ISO 8601 text as a date."), t("Date")},
            {QStringLiteral("at"), QStringLiteral("at(list, index)"), t("Item at a position (negative counts from the end)."), t("Lists")},
            {QStringLiteral("first"), QStringLiteral("first(list)"), t("First item."), t("Lists")},
            {QStringLiteral("last"), QStringLiteral("last(list)"), t("Last item."), t("Lists")},
            {QStringLiteral("sort"), QStringLiteral("sort(list)"), t("Sorted list."), t("Lists")},
            {QStringLiteral("reverse"), QStringLiteral("reverse(list)"), t("Reversed list."), t("Lists")},
            {QStringLiteral("unique"), QStringLiteral("unique(list)"), t("Without duplicates."), t("Lists")},
            {QStringLiteral("includes"), QStringLiteral("includes(list, value)"), t("True when the list has the value."), t("Lists")},
            {QStringLiteral("find"), QStringLiteral("find(list, current > 1)"), t("First item matching a condition."), t("Lists")},
            {QStringLiteral("findIndex"), QStringLiteral("findIndex(list, condition)"), t("Position of the first match, or -1."), t("Lists")},
            {QStringLiteral("filter"), QStringLiteral("filter(list, condition)"), t("Items matching a condition."), t("Lists")},
            {QStringLiteral("map"), QStringLiteral("map(list, current * 2)"), t("Transforms every item."), t("Lists")},
            {QStringLiteral("some"), QStringLiteral("some(list, condition)"), t("True when any item matches."), t("Lists")},
            {QStringLiteral("every"), QStringLiteral("every(list, condition)"), t("True when every item matches."), t("Lists")},
            {QStringLiteral("count"), QStringLiteral("count(list, condition)"), t("Number of items (matching a condition)."), t("Lists")},
            {QStringLiteral("flat"), QStringLiteral("flat(list)"), t("Flattens nested lists."), t("Lists")},
            {QStringLiteral("let"), QStringLiteral("let(name, value, expression)"), t("Names a value."), t("Other")},
            {QStringLiteral("lets"), QStringLiteral("lets(a, 1, b, 2, expression)"), t("Names several values."), t("Other")},
            {QStringLiteral("id"), QStringLiteral("id()"), t("This page's id."), t("Other")},
            {QStringLiteral("name"), QStringLiteral("name(person)"), t("A person's or page's name."), t("Other")},
            {QStringLiteral("link"), QStringLiteral("link(label, url)"), t("A link."), t("Other")},
            {QStringLiteral("style"), QStringLiteral("style(text, \"b\")"), t("Styled text."), t("Other")},
        };
        return d;
    }();
    return docs;
}
} // namespace Formula
