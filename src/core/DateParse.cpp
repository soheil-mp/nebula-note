#include "DateParse.h"

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>

namespace DateParse
{
namespace
{
int weekdayOf(const QString &w)
{
    static const QStringList days = {QStringLiteral("mon"), QStringLiteral("tue"), QStringLiteral("wed"), QStringLiteral("thu"),
                                     QStringLiteral("fri"), QStringLiteral("sat"), QStringLiteral("sun")};
    for (int i = 0; i < days.size(); ++i)
        if (w.startsWith(days[i]))
            return i + 1;
    return 0;
}

int monthOf(const QString &m)
{
    static const QStringList months = {QStringLiteral("jan"), QStringLiteral("feb"), QStringLiteral("mar"), QStringLiteral("apr"),
                                       QStringLiteral("may"), QStringLiteral("jun"), QStringLiteral("jul"), QStringLiteral("aug"),
                                       QStringLiteral("sep"), QStringLiteral("oct"), QStringLiteral("nov"), QStringLiteral("dec")};
    if (m.size() < 3)
        return 0;
    for (int i = 0; i < months.size(); ++i)
        if (m.startsWith(months[i]))
            return i + 1;
    return 0;
}

// Pulls a time ("3pm", "3:30 pm", "15:30", "at 9", "noon") out of the text.
QTime takeTime(QString &text)
{
    static const QRegularExpression ampm(QStringLiteral("(?:\\bat\\s+)?\\b(\\d{1,2})(?::(\\d{2}))?\\s*(am|pm|a\\.m\\.|p\\.m\\.)"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression h24(QStringLiteral("(?:\\bat\\s+)?\\b(\\d{1,2}):(\\d{2})\\b"));
    static const QRegularExpression atHour(QStringLiteral("\\bat\\s+(\\d{1,2})\\b"));
    if (text.contains(QLatin1String("noon"))) {
        text.remove(QLatin1String("noon"));
        return QTime(12, 0);
    }
    if (text.contains(QLatin1String("midnight"))) {
        text.remove(QLatin1String("midnight"));
        return QTime(0, 0);
    }
    if (auto m = ampm.match(text); m.hasMatch()) {
        int h = m.captured(1).toInt() % 12;
        if (m.captured(3).startsWith(QLatin1Char('p')))
            h += 12;
        const QTime t(h, m.captured(2).toInt());
        text.remove(m.capturedStart(), m.capturedLength());
        return t;
    }
    if (auto m = h24.match(text); m.hasMatch()) {
        const QTime t(m.captured(1).toInt(), m.captured(2).toInt());
        if (t.isValid()) {
            text.remove(m.capturedStart(), m.capturedLength());
            return t;
        }
    }
    if (auto m = atHour.match(text); m.hasMatch()) {
        const int h = m.captured(1).toInt();
        if (h >= 0 && h < 24) {
            text.remove(m.capturedStart(), m.capturedLength());
            return QTime(h, 0);
        }
    }
    return {};
}
} // namespace

Result parse(const QString &input, const QDateTime &now)
{
    Result r;
    QString text = input.toLower().simplified();
    if (text.isEmpty())
        return r;
    for (const char *w : {"remind me", "reminder", "remind"}) {
        if (text.startsWith(QLatin1String(w))) {
            r.remind = true;
            text = text.mid(qstrlen(w)).trimmed();
            break;
        }
    }
    text.remove(QRegularExpression(QStringLiteral("^(on|at|by)\\s+")));
    // ISO forms first (exact).
    if (QDateTime dt = QDateTime::fromString(text.left(16).toUpper(), QStringLiteral("yyyy-MM-ddTHH:mm")); dt.isValid()) {
        r.when = dt;
        r.hasTime = true;
        return r;
    }
    if (QDateTime dt = QDateTime::fromString(text.left(16), QStringLiteral("yyyy-MM-dd HH:mm")); dt.isValid()) {
        r.when = dt;
        r.hasTime = true;
        return r;
    }
    const QTime time = takeTime(text);
    text = text.simplified();
    QDate date;
    const QDate today = now.date();
    static const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
    static const QRegularExpression inN(QStringLiteral("^in\\s+(\\d+|a|an|one|two|three)\\s+(day|week|month|year)s?$"));
    static const QRegularExpression ago(QStringLiteral("^(\\d+|a|an|one|two|three)\\s+(day|week|month|year)s?\\s+ago$"));
    static const QRegularExpression monthDay(QStringLiteral("^([a-z]+)\\.?\\s+(\\d{1,2})(?:st|nd|rd|th)?(?:,?\\s+(\\d{4}))?$"));
    static const QRegularExpression dayMonth(QStringLiteral("^(\\d{1,2})(?:st|nd|rd|th)?\\s+([a-z]+)\\.?(?:,?\\s+(\\d{4}))?$"));
    static const QRegularExpression slash(QStringLiteral("^(\\d{1,2})/(\\d{1,2})(?:/(\\d{2,4}))?$"));
    auto number = [](const QString &s) {
        if (s == QLatin1String("a") || s == QLatin1String("an") || s == QLatin1String("one"))
            return 1;
        if (s == QLatin1String("two"))
            return 2;
        if (s == QLatin1String("three"))
            return 3;
        return s.toInt();
    };
    auto shift = [&](int n, const QString &unit) {
        if (unit == QLatin1String("day"))
            return today.addDays(n);
        if (unit == QLatin1String("week"))
            return today.addDays(7 * n);
        if (unit == QLatin1String("month"))
            return today.addMonths(n);
        return today.addYears(n);
    };
    if (text.isEmpty() && time.isValid()) {
        date = today;
    } else if (text == QLatin1String("today") || text == QLatin1String("now") || text == QLatin1String("tonight")) {
        date = today;
    } else if (text == QLatin1String("tomorrow") || text == QLatin1String("tmr") || text == QLatin1String("tmrw")) {
        date = today.addDays(1);
    } else if (text == QLatin1String("yesterday")) {
        date = today.addDays(-1);
    } else if (auto m = iso.match(text); m.hasMatch()) {
        date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    } else if (auto m = inN.match(text); m.hasMatch()) {
        date = shift(number(m.captured(1)), m.captured(2));
    } else if (auto m = ago.match(text); m.hasMatch()) {
        date = shift(-number(m.captured(1)), m.captured(2));
    } else if (text == QLatin1String("next week")) {
        date = today.addDays(8 - today.dayOfWeek());
    } else if (text == QLatin1String("next month")) {
        date = QDate(today.year(), today.month(), 1).addMonths(1);
    } else if (text == QLatin1String("next year")) {
        date = QDate(today.year() + 1, 1, 1);
    } else if (text.startsWith(QLatin1String("next ")) && weekdayOf(text.mid(5))) {
        // "next friday": the one in the coming week, Notion-style (never today).
        const int wd = weekdayOf(text.mid(5));
        int delta = wd - today.dayOfWeek();
        if (delta <= 0)
            delta += 7;
        date = today.addDays(delta);
    } else if (text.startsWith(QLatin1String("last ")) && weekdayOf(text.mid(5))) {
        const int wd = weekdayOf(text.mid(5));
        int delta = today.dayOfWeek() - wd;
        if (delta <= 0)
            delta += 7;
        date = today.addDays(-delta);
    } else if (text.startsWith(QLatin1String("this ")) && weekdayOf(text.mid(5))) {
        date = today.addDays(weekdayOf(text.mid(5)) - today.dayOfWeek());
    } else if (weekdayOf(text) && text.size() <= 9 && !text.contains(QLatin1Char(' '))) {
        // A bare weekday: the next one (today counts).
        int delta = weekdayOf(text) - today.dayOfWeek();
        if (delta < 0)
            delta += 7;
        date = today.addDays(delta);
    } else if (auto m = monthDay.match(text); m.hasMatch() && monthOf(m.captured(1))) {
        const int y = m.captured(3).isEmpty() ? today.year() : m.captured(3).toInt();
        date = QDate(y, monthOf(m.captured(1)), m.captured(2).toInt());
        // A month and day without a year that already passed means next year.
        if (m.captured(3).isEmpty() && date.isValid() && date < today.addDays(-31))
            date = date.addYears(1);
    } else if (auto m = dayMonth.match(text); m.hasMatch() && monthOf(m.captured(2))) {
        const int y = m.captured(3).isEmpty() ? today.year() : m.captured(3).toInt();
        date = QDate(y, monthOf(m.captured(2)), m.captured(1).toInt());
    } else if (auto m = slash.match(text); m.hasMatch()) {
        int y = m.captured(3).isEmpty() ? today.year() : m.captured(3).toInt();
        if (y < 100)
            y += 2000;
        // Month first, as Notion reads US-style input.
        date = QDate(y, m.captured(1).toInt(), m.captured(2).toInt());
        if (!date.isValid())
            date = QDate(y, m.captured(2).toInt(), m.captured(1).toInt());
    } else {
        const QLocale en(QLocale::English);
        for (const QString &f : {QStringLiteral("MMMM d yyyy"), QStringLiteral("MMM d yyyy"), QStringLiteral("d MMMM yyyy"),
                                 QStringLiteral("MMMM yyyy")}) {
            QString t = text;
            t.remove(QLatin1Char(','));
            date = en.toDate(t, f);
            if (date.isValid())
                break;
        }
    }
    if (!date.isValid())
        return r;
    r.when = QDateTime(date, time.isValid() ? time : QTime(0, 0));
    r.hasTime = time.isValid();
    return r;
}

QString relativeLabel(const QDateTime &when, bool hasTime, const QDate &today)
{
    if (!when.isValid())
        return {};
    const QLocale en(QLocale::English);
    const qint64 days = today.daysTo(when.date());
    auto tr = [](const char *s) { return QCoreApplication::translate("DateParse", s); };
    QString label;
    if (days == 0)
        label = tr("Today");
    else if (days == 1)
        label = tr("Tomorrow");
    else if (days == -1)
        label = tr("Yesterday");
    else if (days > 1 && days < 7)
        label = en.dayName(when.date().dayOfWeek());
    else if (days < -1 && days > -7)
        label = tr("Last %1").arg(en.dayName(when.date().dayOfWeek()));
    else
        label = en.toString(when.date(), QStringLiteral("MMMM d, yyyy"));
    if (hasTime)
        label += QLatin1Char(' ') + en.toString(when.time(), QStringLiteral("h:mm AP"));
    return label;
}
} // namespace DateParse
