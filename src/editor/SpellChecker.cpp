#include "SpellChecker.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QSettings>

#include <dlfcn.h>

SpellChecker &SpellChecker::instance()
{
    static SpellChecker s;
    return s;
}

SpellChecker::SpellChecker()
{
    for (const char *name : {"libhunspell-1.7.so.0", "libhunspell-1.7.so", "libhunspell.so", "libhunspell-1.6.so.0"}) {
        m_lib = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
        if (m_lib)
            break;
    }
    if (!m_lib)
        return;
    auto create = reinterpret_cast<CreateFn>(dlsym(m_lib, "Hunspell_create"));
    m_spell = reinterpret_cast<SpellFn>(dlsym(m_lib, "Hunspell_spell"));
    m_suggest = reinterpret_cast<SuggestFn>(dlsym(m_lib, "Hunspell_suggest"));
    m_freeList = reinterpret_cast<FreeListFn>(dlsym(m_lib, "Hunspell_free_list"));
    m_add = reinterpret_cast<AddFn>(dlsym(m_lib, "Hunspell_add"));
    if (!create || !m_spell)
        return;
    // The system language's dictionary, else English.
    const QString system = QLocale::system().name();
    const QStringList dirs = {QStringLiteral("/usr/share/hunspell"), QStringLiteral("/usr/share/myspell"),
                              QStringLiteral("/usr/share/myspell/dicts"), QDir::homePath() + QStringLiteral("/.local/share/hunspell")};
    for (const QString &lang : {system, system.left(2), QStringLiteral("en_US"), QStringLiteral("en_GB")}) {
        for (const QString &dir : dirs) {
            const QString aff = dir + QLatin1Char('/') + lang + QStringLiteral(".aff");
            const QString dic = dir + QLatin1Char('/') + lang + QStringLiteral(".dic");
            if (QFileInfo::exists(aff) && QFileInfo::exists(dic)) {
                m_handle = create(aff.toLocal8Bit().constData(), dic.toLocal8Bit().constData());
                m_language = lang;
                break;
            }
        }
        if (m_handle)
            break;
    }
    const QStringList personal = QSettings().value(QStringLiteral("spelling/words")).toStringList();
    for (const QString &w : personal) {
        m_personal.insert(w.toLower());
        if (m_handle && m_add)
            m_add(m_handle, w.toUtf8().constData());
    }
}

bool SpellChecker::isCorrect(const QString &word)
{
    if (!m_handle || word.size() < 2)
        return true;
    // Numbers, mixed digits and all-caps acronyms aren't checked.
    bool letters = true, upper = true;
    for (const QChar c : word) {
        if (c.isDigit())
            return true;
        letters = letters && (c.isLetter() || c == QLatin1Char('\''));
        upper = upper && (!c.isLetter() || c.isUpper());
    }
    if (!letters || (upper && word.size() <= 6))
        return true;
    if (m_personal.contains(word.toLower()))
        return true;
    // Words of this app's world the stock dictionaries don't know.
    static const QSet<QString> known = {
        QStringLiteral("nebula"),   QStringLiteral("notion"),  QStringLiteral("callout"),  QStringLiteral("callouts"),
        QStringLiteral("ctrl"),     QStringLiteral("esc"),     QStringLiteral("alt"),      QStringLiteral("tex"),
        QStringLiteral("katex"),    QStringLiteral("latex"),   QStringLiteral("markdown"), QStringLiteral("mermaid"),
        QStringLiteral("emoji"),    QStringLiteral("emojis"),  QStringLiteral("todo"),     QStringLiteral("todos"),
        QStringLiteral("kanban"),   QStringLiteral("backlink"), QStringLiteral("backlinks"), QStringLiteral("subpage"),
        QStringLiteral("subpages"), QStringLiteral("sql"),     QStringLiteral("csv"),      QStringLiteral("url"),
        QStringLiteral("urls"),     QStringLiteral("app"),     QStringLiteral("apps"),     QStringLiteral("wiki"),
        QStringLiteral("wikis"),    QStringLiteral("obsidian"), QStringLiteral("evernote"), QStringLiteral("enter"),
    };
    if (known.contains(word.toLower()))
        return true;
    auto it = m_cache.constFind(word);
    if (it != m_cache.constEnd())
        return *it;
    const bool ok = m_spell(m_handle, word.toUtf8().constData()) != 0;
    if (m_cache.size() > 20000)
        m_cache.clear();
    m_cache.insert(word, ok);
    return ok;
}

QStringList SpellChecker::suggestions(const QString &word, int limit)
{
    QStringList out;
    if (!m_handle || !m_suggest)
        return out;
    char **list = nullptr;
    const int n = m_suggest(m_handle, &list, word.toUtf8().constData());
    for (int i = 0; i < n && out.size() < limit; ++i)
        out.append(QString::fromUtf8(list[i]));
    if (list && m_freeList)
        m_freeList(m_handle, &list, n);
    return out;
}

void SpellChecker::addToDictionary(const QString &word)
{
    m_personal.insert(word.toLower());
    m_cache.remove(word);
    if (m_handle && m_add)
        m_add(m_handle, word.toUtf8().constData());
    QSettings s;
    QStringList words = s.value(QStringLiteral("spelling/words")).toStringList();
    if (!words.contains(word))
        words.append(word);
    s.setValue(QStringLiteral("spelling/words"), words);
}
