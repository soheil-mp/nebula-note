#include "CodeHighlighter.h"
#include "SpellChecker.h"

#include <QRegularExpression>

#include "BlockFormat.h"
#include "ui/Theme.h"

#include <QHash>
#include <QTextBlock>

namespace
{
struct Lang {
    QSet<QString> keywords;
    QSet<QString> types;
    QString lineComment;
    QString blockOpen, blockClose;
    bool hashComments = false;
    bool tags = false; // html/xml
};

QSet<QString> words(const char *list)
{
    QSet<QString> s;
    for (const QString &w : QString::fromLatin1(list).split(QLatin1Char(' '), Qt::SkipEmptyParts))
        s.insert(w);
    return s;
}

const Lang &langFor(const QString &key)
{
    static const QHash<QString, Lang> langs = [] {
        QHash<QString, Lang> h;
        Lang c;
        c.keywords = words("auto break case catch class const constexpr continue default delete do else enum explicit export extern "
                           "false final for friend goto if inline mutable namespace new noexcept nullptr operator override private "
                           "protected public return sizeof static static_cast struct switch template this throw true try typedef "
                           "typename union using virtual volatile while co_await co_return co_yield concept requires #include "
                           "#define #pragma #if #endif #ifdef #ifndef #else");
        c.types = words("void bool char int long short float double unsigned signed size_t std string vector auto uint8_t "
                        "int32_t int64_t uint32_t uint64_t QString QList");
        c.lineComment = QStringLiteral("//");
        c.blockOpen = QStringLiteral("/*");
        c.blockClose = QStringLiteral("*/");
        h.insert(QStringLiteral("cpp"), c);
        h.insert(QStringLiteral("c"), c);
        Lang js = c;
        js.keywords = words("async await break case catch class const continue debugger default delete do else export extends false "
                            "finally for from function if import in instanceof let new null of return static super switch this "
                            "throw true try typeof undefined var void while with yield interface type implements enum readonly as");
        js.types = words("string number boolean any unknown never object Array Promise Map Set Record");
        h.insert(QStringLiteral("javascript"), js);
        h.insert(QStringLiteral("typescript"), js);
        Lang java = c;
        java.keywords = words("abstract break case catch class continue default do else enum extends final finally for if "
                              "implements import instanceof interface new null package private protected public return static "
                              "super switch synchronized this throw throws true false try void volatile while var fun val when "
                              "object companion data sealed");
        h.insert(QStringLiteral("java"), java);
        h.insert(QStringLiteral("kotlin"), java);
        Lang cs = c;
        cs.keywords = words("abstract as async await base break case catch class const continue default delegate do else enum "
                            "event false finally for foreach if in interface internal is lock namespace new null out override "
                            "params private protected public readonly ref return sealed static struct switch this throw true try "
                            "using var virtual void while");
        h.insert(QStringLiteral("csharp"), cs);
        Lang go = c;
        go.keywords = words("break case chan const continue default defer else fallthrough for func go goto if import interface "
                            "map package range return select struct switch type var true false nil");
        go.types = words("int int64 int32 string bool byte rune float64 float32 error uint any");
        h.insert(QStringLiteral("go"), go);
        Lang rust = c;
        rust.keywords = words("as async await break const continue crate dyn else enum extern false fn for if impl in let loop "
                              "match mod move mut pub ref return self Self static struct super trait true type unsafe use where "
                              "while");
        rust.types = words("i8 i16 i32 i64 u8 u16 u32 u64 f32 f64 usize isize bool char str String Vec Option Result Box");
        h.insert(QStringLiteral("rust"), rust);
        Lang swift = c;
        swift.keywords = words("class struct enum protocol extension func var let if else guard return for in while switch case "
                               "default import true false nil self init throws try catch async await");
        h.insert(QStringLiteral("swift"), swift);
        Lang py;
        py.keywords = words("and as assert async await break class continue def del elif else except False finally for from "
                            "global if import in is lambda None nonlocal not or pass raise return True try while with yield "
                            "match case self");
        py.types = words("int str float bool list dict set tuple bytes object");
        py.hashComments = true;
        h.insert(QStringLiteral("python"), py);
        Lang rb = py;
        rb.keywords = words("def end class module if elsif else unless while until for in do return yield begin rescue ensure "
                            "nil true false self require");
        h.insert(QStringLiteral("ruby"), rb);
        Lang sh;
        sh.keywords = words("if then else elif fi for while until do done case esac in function return local export source "
                            "echo cd exit set unset readonly sudo");
        sh.hashComments = true;
        h.insert(QStringLiteral("bash"), sh);
        Lang yaml;
        yaml.hashComments = true;
        yaml.keywords = words("true false null yes no");
        h.insert(QStringLiteral("yaml"), yaml);
        Lang sql;
        sql.keywords = words("select from where and or not insert into values update set delete create table drop alter join "
                             "left right inner outer on group by order having limit as distinct union all null is in like "
                             "primary key foreign references index view case when then else end SELECT FROM WHERE AND OR NOT "
                             "INSERT INTO VALUES UPDATE SET DELETE CREATE TABLE DROP ALTER JOIN LEFT RIGHT INNER OUTER ON GROUP "
                             "BY ORDER HAVING LIMIT AS DISTINCT UNION ALL NULL IS IN LIKE PRIMARY KEY FOREIGN REFERENCES INDEX");
        sql.types = words("integer int text varchar real boolean date timestamp INTEGER INT TEXT VARCHAR REAL BOOLEAN DATE");
        sql.lineComment = QStringLiteral("--");
        sql.blockOpen = QStringLiteral("/*");
        sql.blockClose = QStringLiteral("*/");
        h.insert(QStringLiteral("sql"), sql);
        Lang json;
        json.keywords = words("true false null");
        h.insert(QStringLiteral("json"), json);
        Lang css;
        css.blockOpen = QStringLiteral("/*");
        css.blockClose = QStringLiteral("*/");
        css.keywords = words("important media import from to");
        h.insert(QStringLiteral("css"), css);
        Lang html;
        html.tags = true;
        html.blockOpen = QStringLiteral("<!--");
        html.blockClose = QStringLiteral("-->");
        h.insert(QStringLiteral("html"), html);
        h.insert(QStringLiteral("xml"), html);
        Lang lua = c;
        lua.keywords = words("and break do else elseif end false for function if in local nil not or repeat return then true "
                             "until while");
        lua.lineComment = QStringLiteral("--");
        h.insert(QStringLiteral("lua"), lua);
        h.insert(QStringLiteral("plain"), Lang{});
        return h;
    }();
    static const Lang none;
    auto it = langs.constFind(key.toLower());
    if (it != langs.constEnd())
        return *it;
    // Common aliases.
    static const QHash<QString, QString> alias = {
        {QStringLiteral("js"), QStringLiteral("javascript")}, {QStringLiteral("ts"), QStringLiteral("typescript")},
        {QStringLiteral("py"), QStringLiteral("python")},     {QStringLiteral("sh"), QStringLiteral("bash")},
        {QStringLiteral("shell"), QStringLiteral("bash")},    {QStringLiteral("zsh"), QStringLiteral("bash")},
        {QStringLiteral("c++"), QStringLiteral("cpp")},       {QStringLiteral("cxx"), QStringLiteral("cpp")},
        {QStringLiteral("rs"), QStringLiteral("rust")},       {QStringLiteral("yml"), QStringLiteral("yaml")},
        {QStringLiteral("cs"), QStringLiteral("csharp")},     {QStringLiteral("kt"), QStringLiteral("kotlin")},
        {QStringLiteral("rb"), QStringLiteral("ruby")},       {QStringLiteral("tsx"), QStringLiteral("typescript")},
        {QStringLiteral("jsx"), QStringLiteral("javascript")}};
    auto a = alias.constFind(key.toLower());
    if (a != alias.constEnd())
        return langFor(*a);
    return none;
}
}

CodeHighlighter::CodeHighlighter(QTextDocument *doc)
    : QSyntaxHighlighter(doc)
{
}

QStringList CodeHighlighter::languages()
{
    return {QStringLiteral("plain"),  QStringLiteral("bash"),   QStringLiteral("c"),      QStringLiteral("cpp"),
            QStringLiteral("csharp"), QStringLiteral("css"),    QStringLiteral("go"),     QStringLiteral("html"),
            QStringLiteral("java"),   QStringLiteral("javascript"), QStringLiteral("json"), QStringLiteral("kotlin"),
            QStringLiteral("lua"),    QStringLiteral("markdown"), QStringLiteral("mermaid"), QStringLiteral("python"),
            QStringLiteral("ruby"),   QStringLiteral("rust"),
            QStringLiteral("sql"),    QStringLiteral("swift"),  QStringLiteral("typescript"), QStringLiteral("xml"),
            QStringLiteral("yaml")};
}

QString CodeHighlighter::languageName(const QString &key)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("plain"), QStringLiteral("Plain text")}, {QStringLiteral("bash"), QStringLiteral("Bash")},
        {QStringLiteral("c"), QStringLiteral("C")},              {QStringLiteral("cpp"), QStringLiteral("C++")},
        {QStringLiteral("csharp"), QStringLiteral("C#")},        {QStringLiteral("css"), QStringLiteral("CSS")},
        {QStringLiteral("go"), QStringLiteral("Go")},            {QStringLiteral("html"), QStringLiteral("HTML")},
        {QStringLiteral("java"), QStringLiteral("Java")},        {QStringLiteral("javascript"), QStringLiteral("JavaScript")},
        {QStringLiteral("json"), QStringLiteral("JSON")},        {QStringLiteral("kotlin"), QStringLiteral("Kotlin")},
        {QStringLiteral("lua"), QStringLiteral("Lua")},          {QStringLiteral("python"), QStringLiteral("Python")},
        {QStringLiteral("markdown"), QStringLiteral("Markdown")}, {QStringLiteral("mermaid"), QStringLiteral("Mermaid")},
        {QStringLiteral("ruby"), QStringLiteral("Ruby")},        {QStringLiteral("rust"), QStringLiteral("Rust")},
        {QStringLiteral("sql"), QStringLiteral("SQL")},          {QStringLiteral("swift"), QStringLiteral("Swift")},
        {QStringLiteral("typescript"), QStringLiteral("TypeScript")}, {QStringLiteral("xml"), QStringLiteral("XML")},
        {QStringLiteral("yaml"), QStringLiteral("YAML")},        {QStringLiteral("table"), QStringLiteral("Table")}};
    if (key.isEmpty())
        return QStringLiteral("Plain text");
    return names.value(key.toLower(), key);
}

void CodeHighlighter::setSpellCheck(bool on)
{
    if (on && !SpellChecker::instance().isAvailable())
        on = false;
    if (m_spell == on)
        return;
    m_spell = on;
    rehighlight();
}

void CodeHighlighter::checkSpelling(const QString &text)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}'’]+"));
    QTextCharFormat wavy;
    wavy.setUnderlineStyle(QTextCharFormat::SpellCheckUnderline);
    wavy.setUnderlineColor(Theme::colors().mist);
    auto it = word.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        QString w = m.captured(0);
        w.replace(QChar(0x2019), QLatin1Char('\''));
        while (w.endsWith(QLatin1Char('\'')))
            w.chop(1);
        while (w.startsWith(QLatin1Char('\'')))
            w.remove(0, 1);
        if (w.isEmpty())
            continue;
        // Links and inline code aren't prose.
        const QTextCharFormat cf = format(int(m.capturedStart()));
        Q_UNUSED(cf)
        if (!SpellChecker::instance().isCorrect(w))
            setFormat(int(m.capturedStart()), int(m.capturedLength()), wavy);
    }
}

void CodeHighlighter::highlightBlock(const QString &text)
{
    if (BlockFormat::typeOf(currentBlock()) != BlockType::Code) {
        if (m_spell && !blockTypeIsAtomic(BlockFormat::typeOf(currentBlock())))
            checkSpelling(text);
        return;
    }
    const QString lang = currentBlock().blockFormat().property(BlockFormat::LanguageProp).toString();
    const Lang &L = langFor(lang);
    const auto &c = Theme::colors();

    QTextCharFormat keyword, type, string, comment, number, tag, punct;
    keyword.setForeground(c.starlight);
    keyword.setFontWeight(QFont::DemiBold);
    type.setForeground(QColor(0xDA, 0xDA, 0xE0));
    type.setFontItalic(true);
    string.setForeground(c.mist);
    comment.setForeground(c.fog);
    comment.setFontItalic(true);
    number.setForeground(QColor(0xE6, 0xE6, 0xEB));
    tag.setForeground(c.starlight);
    punct.setForeground(c.haze);

    const int n = int(text.size());
    auto isLineBreak = [](QChar ch) { return ch == QChar::LineSeparator || ch == QLatin1Char('\n'); };
    int i = 0;
    while (i < n) {
        const QChar ch = text[i];
        // Comments.
        if (!L.lineComment.isEmpty() && text.mid(i, L.lineComment.size()) == L.lineComment) {
            int end = i;
            while (end < n && !isLineBreak(text[end]))
                ++end;
            setFormat(i, end - i, comment);
            i = end;
            continue;
        }
        if (L.hashComments && ch == QLatin1Char('#')) {
            int end = i;
            while (end < n && !isLineBreak(text[end]))
                ++end;
            setFormat(i, end - i, comment);
            i = end;
            continue;
        }
        if (!L.blockOpen.isEmpty() && text.mid(i, L.blockOpen.size()) == L.blockOpen) {
            int end = text.indexOf(L.blockClose, i + L.blockOpen.size());
            end = end < 0 ? n : end + int(L.blockClose.size());
            setFormat(i, end - i, comment);
            i = end;
            continue;
        }
        // Strings.
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'') || ch == QLatin1Char('`')) {
            int end = i + 1;
            while (end < n && text[end] != ch && !(ch != QLatin1Char('`') && isLineBreak(text[end]))) {
                if (text[end] == QLatin1Char('\\'))
                    ++end;
                ++end;
            }
            end = qMin(n, end + 1);
            setFormat(i, end - i, string);
            i = end;
            continue;
        }
        // Markup tags.
        if (L.tags && ch == QLatin1Char('<')) {
            int end = i + 1;
            while (end < n && text[end] != QLatin1Char('>') && !text[end].isSpace())
                ++end;
            setFormat(i, end - i, tag);
            i = end;
            continue;
        }
        // Numbers.
        if (ch.isDigit() && (i == 0 || !text[i - 1].isLetterOrNumber())) {
            int end = i;
            while (end < n && (text[end].isLetterOrNumber() || text[end] == QLatin1Char('.') || text[end] == QLatin1Char('_')))
                ++end;
            setFormat(i, end - i, number);
            i = end;
            continue;
        }
        // Words.
        if (ch.isLetter() || ch == QLatin1Char('_') || ch == QLatin1Char('#') || ch == QLatin1Char('@')) {
            int end = i + 1;
            while (end < n && (text[end].isLetterOrNumber() || text[end] == QLatin1Char('_')))
                ++end;
            const QString word = text.mid(i, end - i);
            if (L.keywords.contains(word))
                setFormat(i, end - i, keyword);
            else if (L.types.contains(word) || (word.size() > 1 && word[0].isUpper() && !L.tags && L.keywords.size() > 4))
                setFormat(i, end - i, type);
            i = end;
            continue;
        }
        if (QStringLiteral("{}[]();,.:=<>+-*/%!&|^~?").contains(ch))
            setFormat(i, 1, punct);
        ++i;
    }
}
