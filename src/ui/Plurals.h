#pragma once

#include <QTranslator>

// English plurals without .qm files: "%n page(s)" → "1 page" / "3 pages".
class PluralTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;
    bool isEmpty() const override { return false; }
    QString translate(const char *, const char *sourceText, const char *, int n) const override
    {
        if (n < 0 || !sourceText)
            return {};
        QString s = QString::fromUtf8(sourceText);
        if (!s.contains(QLatin1String("(s)")))
            return {};
        s.replace(QLatin1String("(s)"), n == 1 ? QString() : QStringLiteral("s"));
        return s;
    }
};
