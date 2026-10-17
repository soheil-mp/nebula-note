#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

// Spell checking through the system's Hunspell (loaded at run time, so
// Nebula works without it) and its installed dictionaries.
class SpellChecker
{
public:
    static SpellChecker &instance();
    bool isAvailable() const { return m_handle != nullptr; }
    QString language() const { return m_language; }
    bool isCorrect(const QString &word);
    QStringList suggestions(const QString &word, int limit = 6);
    // Remembered in the settings and accepted from now on.
    void addToDictionary(const QString &word);

private:
    SpellChecker();
    void *m_lib = nullptr;
    void *m_handle = nullptr;
    QString m_language;
    QSet<QString> m_personal;
    QHash<QString, bool> m_cache;
    using CreateFn = void *(*)(const char *, const char *);
    using SpellFn = int (*)(void *, const char *);
    using SuggestFn = int (*)(void *, char ***, const char *);
    using FreeListFn = void (*)(void *, char ***, int);
    using AddFn = int (*)(void *, const char *);
    SpellFn m_spell = nullptr;
    SuggestFn m_suggest = nullptr;
    FreeListFn m_freeList = nullptr;
    AddFn m_add = nullptr;
};
