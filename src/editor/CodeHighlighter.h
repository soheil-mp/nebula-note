#pragma once

#include <QSet>
#include <QSyntaxHighlighter>

// Monochrome highlighting for code blocks only: meaning through brightness,
// weight and italics (keywords bright, strings soft, comments dim).
class CodeHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit CodeHighlighter(QTextDocument *doc);

    static QStringList languages(); // offered in the language menu
    static QString languageName(const QString &key);
    // Underline misspelled words in text blocks.
    void setSpellCheck(bool on);
    bool spellCheck() const { return m_spell; }

protected:
    void highlightBlock(const QString &text) override;

private:
    void checkSpelling(const QString &text);
    bool m_spell = false;
};
