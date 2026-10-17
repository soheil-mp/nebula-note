#pragma once

#include <QDateTime>
#include <QString>

// Dates as people type them in "@" mentions and date fields: "today",
// "tomorrow 3pm", "next friday", "in 3 days", "oct 20", "20 October 2026",
// "2026-10-20 14:30", "remind me monday at 9am".
namespace DateParse
{
struct Result {
    QDateTime when;       // invalid when nothing was understood
    bool hasTime = false;
    bool remind = false;  // the text asked for a reminder ("remind …")
};
// `now` is the reference moment (tests pass a fixed one).
Result parse(const QString &text, const QDateTime &now = QDateTime::currentDateTime());

// "Today", "Tomorrow", "Yesterday", "Friday" (this week), "Last Monday", or
// "October 20, 2026" — Notion's style for date mentions — plus the time.
QString relativeLabel(const QDateTime &when, bool hasTime, const QDate &today = QDate::currentDate());
} // namespace DateParse
