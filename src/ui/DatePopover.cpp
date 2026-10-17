#include "DatePopover.h"

#include "Theme.h"
#include "core/DateParse.h"
#include "core/Schema.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>

// ---- month grid -----------------------------------------------------------------------

MonthGrid::MonthGrid(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedSize(7 * 34 + 4, 30 + 6 * 32);
}

void MonthGrid::setMonth(const QDate &firstOfMonth)
{
    m_month = firstOfMonth;
    update();
}

void MonthGrid::setSelected(const QDate &start, const QDate &end)
{
    m_start = start;
    m_end = end;
    update();
}

QDate MonthGrid::dateAt(int row, int col) const
{
    const int offset = m_month.dayOfWeek() - 1; // Monday first
    return m_month.addDays(row * 7 + col - offset);
}

QRectF MonthGrid::cell(int row, int col) const
{
    return QRectF(2 + col * 34, 30 + row * 32, 32, 30);
}

void MonthGrid::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QLocale en(QLocale::English);
    p.setFont(Theme::uiFont(11.5, QFont::Medium));
    p.setPen(c.fog);
    for (int col = 0; col < 7; ++col)
        p.drawText(QRectF(2 + col * 34, 4, 32, 22), Qt::AlignCenter, en.dayName(col + 1, QLocale::ShortFormat).left(2));
    const QDate today = QDate::currentDate();
    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 7; ++col) {
            const QDate d = dateAt(row, col);
            const QRectF r = cell(row, col);
            const bool inMonth = d.month() == m_month.month();
            const bool selected = d == m_start || (m_end.isValid() && d == m_end);
            const bool inRange = m_end.isValid() && d > m_start && d < m_end;
            if (inRange) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.graphite);
                p.drawRect(r.adjusted(-1, 3, 1, -3));
            }
            if (selected) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.starlight);
                p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 7, 7);
            } else if (d == m_hoverDate) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.graphite);
                p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 7, 7);
            }
            if (d == today && !selected) {
                p.setPen(QPen(c.mist, 1.2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(r.adjusted(2.5, 2.5, -2.5, -2.5), 7, 7);
            }
            p.setFont(Theme::uiFont(12.5, d == today ? QFont::DemiBold : QFont::Normal));
            p.setPen(selected ? c.basalt : (inMonth ? c.moon : c.dust));
            p.drawText(r, Qt::AlignCenter, QString::number(d.day()));
        }
    }
}

void MonthGrid::mouseMoveEvent(QMouseEvent *e)
{
    QDate h;
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 7; ++col)
            if (cell(row, col).contains(e->position()))
                h = dateAt(row, col);
    if (h != m_hoverDate) {
        m_hoverDate = h;
        update();
    }
}

void MonthGrid::leaveEvent(QEvent *)
{
    m_hoverDate = QDate();
    update();
}

void MonthGrid::mousePressEvent(QMouseEvent *e)
{
    for (int row = 0; row < 6; ++row)
        for (int col = 0; col < 7; ++col)
            if (cell(row, col).contains(e->position()))
                emit picked(dateAt(row, col));
}

// ---- popover ----------------------------------------------------------------------------

namespace
{
QHBoxLayout *switchRow(const QString &label, QWidget *trailing, Switch *sw)
{
    auto *row = new QHBoxLayout;
    auto *l = new QLabel(label);
    l->setFont(Theme::uiFont(13));
    l->setStyleSheet(QStringLiteral("color: #c9c9d0; padding-left: 4px;"));
    row->addWidget(l);
    row->addStretch();
    if (trailing)
        row->addWidget(trailing);
    row->addWidget(sw);
    return row;
}
} // namespace

DatePopover::DatePopover(QWidget *parent, const QString &value, const Options &options)
    : Popover(parent)
    , m_options(options)
{
    const QDateTime start = dateTimeOf(value);
    m_start = start.isValid() ? start.date() : QDate();
    m_time = value.size() > 10 && value[10] == QLatin1Char('T') ? start.time() : QTime();
    const QDateTime end = dateEndOf(value);
    m_hasEnd = end.isValid() && options.range;
    m_end = m_hasEnd ? end.date() : QDate();
    m_remind = dateHasReminder(value) && options.remind;

    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);
    m_input = new QLineEdit;
    m_input->setFont(Theme::uiFont(13.5));
    m_input->setPlaceholderText(tr("Type a date — “tomorrow 3pm”, “next friday”…"));
    v->addWidget(m_input);
    auto *nav = new QHBoxLayout;
    m_monthLabel = new QLabel;
    m_monthLabel->setFont(Theme::uiFont(13, QFont::DemiBold));
    m_monthLabel->setStyleSheet(QStringLiteral("color: #f1f1f5; padding-left: 4px;"));
    auto *prev = new FlatButton(QStringLiteral("chevron-left"));
    auto *next = new FlatButton(QStringLiteral("chevron-right"));
    auto *todayBtn = new FlatButton(QString(), tr("Today"));
    nav->addWidget(m_monthLabel);
    nav->addStretch();
    nav->addWidget(todayBtn);
    nav->addWidget(prev);
    nav->addWidget(next);
    v->addLayout(nav);
    m_grid = new MonthGrid(content());
    v->addWidget(m_grid);
    if (options.range) {
        m_endSwitch = new Switch;
        m_endSwitch->setChecked(m_hasEnd);
        v->addLayout(switchRow(tr("End date"), nullptr, m_endSwitch));
    }
    m_timeSwitch = new Switch;
    m_timeSwitch->setChecked(m_time.isValid());
    m_timeEdit = new QLineEdit(m_time.isValid() ? m_time.toString(QStringLiteral("HH:mm")) : QStringLiteral("09:00"));
    m_timeEdit->setFixedWidth(64);
    m_timeEdit->setFont(Theme::uiFont(13));
    m_timeEdit->setVisible(m_time.isValid());
    v->addLayout(switchRow(tr("Include time"), m_timeEdit, m_timeSwitch));
    if (options.remind) {
        m_remindSwitch = new Switch;
        m_remindSwitch->setChecked(m_remind);
        v->addLayout(switchRow(tr("Remind me"), nullptr, m_remindSwitch));
    }
    if (options.clear) {
        auto *clear = new FlatButton(QStringLiteral("trash"), tr("Clear"));
        v->addWidget(clear, 0, Qt::AlignLeft);
        connect(clear, &FlatButton::clicked, this, [this] {
            emit changed(QString());
            close();
        });
    }

    const QDate shown = m_start.isValid() ? m_start : QDate::currentDate();
    m_grid->setMonth(QDate(shown.year(), shown.month(), 1));
    refresh();
    connect(prev, &FlatButton::clicked, this, [this] {
        m_grid->setMonth(m_grid->month().addMonths(-1));
        refresh();
    });
    connect(next, &FlatButton::clicked, this, [this] {
        m_grid->setMonth(m_grid->month().addMonths(1));
        refresh();
    });
    connect(todayBtn, &FlatButton::clicked, this, [this] { pick(QDate::currentDate()); });
    connect(m_grid, &MonthGrid::picked, this, &DatePopover::pick);
    if (m_endSwitch)
        connect(m_endSwitch, &Switch::toggled, this, [this](bool on) {
            m_hasEnd = on;
            m_pickingEnd = on;
            if (!on)
                m_end = QDate();
            else if (m_start.isValid() && !m_end.isValid())
                m_end = m_start;
            commit();
        });
    connect(m_timeSwitch, &Switch::toggled, this, [this](bool on) {
        m_timeEdit->setVisible(on);
        m_time = on ? QTime::fromString(m_timeEdit->text(), QStringLiteral("HH:mm")) : QTime();
        if (on && !m_time.isValid())
            m_time = QTime(9, 0);
        commit();
        adjustSize();
    });
    if (m_remindSwitch)
        connect(m_remindSwitch, &Switch::toggled, this, [this](bool on) {
            m_remind = on;
            commit();
        });
    connect(m_timeEdit, &QLineEdit::editingFinished, this, [this] {
        const DateParse::Result r = DateParse::parse(m_timeEdit->text().trimmed());
        const QTime t = r.hasTime ? r.when.time() : QTime::fromString(m_timeEdit->text().trimmed(), QStringLiteral("H:mm"));
        if (t.isValid()) {
            m_time = t;
            commit();
        }
    });
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString text = m_input->text();
        // "start → end" ranges, otherwise one natural date.
        const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s*(→|->| to | – | - )\\s*")));
        const DateParse::Result a = DateParse::parse(parts.value(0));
        if (!a.when.isValid())
            return;
        m_start = a.when.date();
        if (a.hasTime) {
            m_time = a.when.time();
            m_timeSwitch->setChecked(true);
        }
        if (a.remind && m_remindSwitch)
            m_remindSwitch->setChecked(true);
        if (parts.size() > 1 && m_options.range) {
            const DateParse::Result b = DateParse::parse(parts.value(1));
            if (b.when.isValid()) {
                m_end = b.when.date();
                m_hasEnd = true;
                if (m_endSwitch)
                    m_endSwitch->setChecked(true);
            }
        }
        commit();
        close();
    });
}

QString DatePopover::value() const
{
    if (!m_start.isValid())
        return {};
    const QTime t = m_time.isValid() ? m_time : QTime(0, 0);
    QDateTime end;
    if (m_hasEnd && m_end.isValid())
        end = QDateTime(m_end, t);
    QDateTime start(m_start, t);
    if (end.isValid() && end < start)
        std::swap(start, end);
    return makeDateValue(start, m_time.isValid(), end, m_remind);
}

void DatePopover::pick(const QDate &d)
{
    if (m_hasEnd && m_pickingEnd && m_start.isValid()) {
        m_end = d;
        m_pickingEnd = false;
    } else {
        m_start = d;
        if (m_hasEnd) {
            m_pickingEnd = true;
            if (!m_end.isValid() || m_end < d)
                m_end = d;
        }
    }
    if (d.month() != m_grid->month().month() || d.year() != m_grid->month().year())
        m_grid->setMonth(QDate(d.year(), d.month(), 1));
    commit();
    if (!m_hasEnd && !m_timeSwitch->isChecked())
        close();
}

void DatePopover::commit()
{
    if (!m_start.isValid())
        return;
    refresh();
    emit changed(value());
}

void DatePopover::refresh()
{
    m_monthLabel->setText(QLocale(QLocale::English).toString(m_grid->month(), QStringLiteral("MMMM yyyy")));
    m_grid->setSelected(m_start, m_hasEnd ? m_end : QDate());
    Property probe;
    probe.type = PropType::Date;
    m_input->setText(m_start.isValid() ? formatDateFor(probe, value()) : QString());
}
