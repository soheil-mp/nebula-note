#pragma once

#include "Widgets.h"

#include <QDate>
#include <QDateTime>

class QLabel;
class QLineEdit;
class Switch;

// A month of days to pick from; with a range it highlights the span.
class MonthGrid : public QWidget
{
    Q_OBJECT
public:
    explicit MonthGrid(QWidget *parent = nullptr);
    void setMonth(const QDate &firstOfMonth);
    void setSelected(const QDate &start, const QDate &end = {});
    QDate month() const { return m_month; }

signals:
    void picked(const QDate &date);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QDate dateAt(int row, int col) const;
    QRectF cell(int row, int col) const;
    QDate m_month;
    QDate m_start, m_end;
    QDate m_hoverDate;
};

// Picks a date value in the format of database date properties
// ("start[/end][#r]", see Schema.h): a date or date and time, optionally a
// range and a reminder. Typing understands natural dates ("next friday 3pm").
class DatePopover : public Popover
{
    Q_OBJECT
public:
    struct Options {
        bool range = true;
        bool remind = true;
        bool clear = true;
    };
    DatePopover(QWidget *parent, const QString &value, const Options &options);
    DatePopover(QWidget *parent, const QString &value) : DatePopover(parent, value, Options{}) {}
    QString value() const;

signals:
    void changed(const QString &value); // empty: cleared

private:
    void pick(const QDate &d);
    void commit();
    void refresh();

    Options m_options;
    QDate m_start, m_end;
    QTime m_time;
    bool m_hasEnd = false;
    bool m_remind = false;
    bool m_pickingEnd = false;
    QLineEdit *m_input = nullptr;
    QLabel *m_monthLabel = nullptr;
    MonthGrid *m_grid = nullptr;
    Switch *m_endSwitch = nullptr;
    Switch *m_timeSwitch = nullptr;
    Switch *m_remindSwitch = nullptr;
    QLineEdit *m_timeEdit = nullptr;
};
