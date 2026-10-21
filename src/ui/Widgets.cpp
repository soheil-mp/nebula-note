#include "Widgets.h"

#include "Icons.h"
#include "Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

// ---- Popover ---------------------------------------------------------------------

Popover::Popover(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadow, kShadow - 6, kShadow, kShadow + 6);
    m_content = new QWidget(this);
    outer->addWidget(m_content);
    setFont(Theme::uiFont(13));
}

QRect Popover::allowedArea(const QPoint &globalPos) const
{
    QScreen *screen = QGuiApplication::screenAt(globalPos);
    if (!screen)
        return {};
    QRect avail = screen->availableGeometry();
    // Stay inside the window when the card fits in it, as Notion's do…
    if (QWidget *host = parentWidget() ? parentWidget()->window() : nullptr) {
        // …with a little air between card and window edge.
        const QRect win = QRect(host->mapToGlobal(QPoint(0, 0)), host->size()).adjusted(8, 8, -8, -8);
        if (height() - 2 * kShadow <= win.height() && width() - 2 * kShadow <= win.width())
            avail = avail.intersected(win);
    }
    return avail;
}

void Popover::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    // A card that grew (a settings page, a longer list) moves back inside.
    if (!isVisible())
        return;
    const QRect avail = allowedArea(geometry().center());
    if (avail.isEmpty())
        return;
    QPoint pos = this->pos();
    pos.setX(qBound(avail.left() - kShadow, pos.x(), qMax(avail.left() - kShadow, avail.right() - width() + kShadow)));
    pos.setY(qBound(avail.top() - kShadow + 6, pos.y(), qMax(avail.top() - kShadow + 6, avail.bottom() - height() + kShadow + 6)));
    if (pos != this->pos())
        move(pos);
}

void Popover::popupAt(const QPoint &globalPos)
{
    adjustSize();
    QPoint pos = globalPos - QPoint(kShadow, kShadow - 6);
    if (const QRect avail = allowedArea(globalPos); !avail.isEmpty()) {
        pos.setX(qBound(avail.left() - kShadow, pos.x(), avail.right() - width() + kShadow));
        if (pos.y() + height() > avail.bottom() + kShadow) {
            // Flip above the anchor; if there's no room there either, rise
            // just enough to end at the bottom edge.
            const int above = globalPos.y() - height() - 4 + kShadow;
            pos.setY(above >= avail.top() - kShadow ? above : qMax(avail.top() - kShadow, avail.bottom() + kShadow - height()));
        }
    }
    move(pos);
    show();
    raise();
    activateWindow();
}

void Popover::popupBelow(QWidget *anchor, int dx)
{
    popupAt(anchor->mapToGlobal(QPoint(dx, anchor->height() + 4)));
}

void Popover::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(kShadow, kShadow - 6, -kShadow, -kShadow - 6);
    Theme::paintSoftShadow(p, card, 12, kShadow - 2, 1.5);
    Theme::paintCard(p, card, 12, Theme::colors().slate);
}

void Popover::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}

void Popover::hideEvent(QHideEvent *e)
{
    QWidget::hideEvent(e);
    emit closed();
}

// ---- FlatButton ------------------------------------------------------------------

FlatButton::FlatButton(const QString &icon, const QString &text, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(icon)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    m_hover.setDuration(Theme::animMs(120));
    m_hover.setStartValue(0.0);
    m_hover.setEndValue(1.0);
    connect(&m_hover, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
}

void FlatButton::setIconName(const QString &icon)
{
    m_icon = icon;
    update();
}

QSize FlatButton::sizeHint() const
{
    const QFontMetricsF fm(Theme::uiFont(m_textSize, QFont::Medium));
    int w = 0;
    if (!m_icon.isEmpty())
        w += m_iconSize;
    if (!text().isEmpty())
        w += int(fm.horizontalAdvance(text())) + (m_icon.isEmpty() ? 0 : 6);
    const int h = qMax(m_iconSize + 10, int(fm.height()) + 10);
    return {w + (text().isEmpty() ? 10 : 18), h};
}

void FlatButton::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double hover = m_hover.state() == QAbstractAnimation::Running ? m_hover.currentValue().toDouble()
                                                                         : (underMouse() ? 1.0 : 0.0);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (m_filled) {
        p.setPen(QPen(c.ash, 1));
        p.setBrush(Theme::mix(c.graphite, c.smoke, hover * 0.5));
        p.drawRoundedRect(r, m_radius, m_radius);
    } else {
        Theme::paintHover(p, r, isDown() ? 1.6 : (isChecked() ? 1.4 : hover), m_radius);
    }
    const QColor fg = !isEnabled() ? c.dust : (isChecked() || hover > 0.5 || !m_muted ? c.starlight : c.haze);
    double x = text().isEmpty() ? (width() - m_iconSize) / 2.0 : 9;
    if (!m_icon.isEmpty()) {
        Icons::paint(p, QRectF(x, (height() - m_iconSize) / 2.0, m_iconSize, m_iconSize), m_icon, fg);
        x += m_iconSize + 6;
    }
    if (!text().isEmpty()) {
        p.setFont(Theme::uiFont(m_textSize, QFont::Medium));
        p.setPen(fg);
        p.drawText(QRectF(x, 0, width() - x - 4, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
}

void FlatButton::enterEvent(QEnterEvent *e)
{
    m_hover.setDirection(QAbstractAnimation::Forward);
    m_hover.start();
    QAbstractButton::enterEvent(e);
}

void FlatButton::leaveEvent(QEvent *e)
{
    m_hover.setDirection(QAbstractAnimation::Backward);
    m_hover.start();
    QAbstractButton::leaveEvent(e);
}

// ---- Switch ----------------------------------------------------------------------

Switch::Switch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    m_anim.setDuration(Theme::animMs(160));
    m_anim.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_anim, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
}

void Switch::nextCheckState()
{
    QAbstractButton::nextCheckState();
    m_anim.setStartValue(isChecked() ? 0.0 : 1.0);
    m_anim.setEndValue(isChecked() ? 1.0 : 0.0);
    m_anim.start();
}

void Switch::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double t = m_anim.state() == QAbstractAnimation::Running ? m_anim.currentValue().toDouble() : (isChecked() ? 1.0 : 0.0);
    if (!isEnabled())
        p.setOpacity(0.4);
    const QRectF track(1, 2, width() - 2, height() - 4);
    p.setPen(QPen(Theme::mix(c.ash, c.mist, t), 1));
    p.setBrush(Theme::mix(c.graphite, c.moon, t));
    p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const double d = track.height() - 6;
    const double x = track.left() + 3 + (track.width() - d - 6) * t;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::mix(c.haze, c.basalt, t));
    p.drawEllipse(QRectF(x, track.top() + 3, d, d));
}

// ---- MenuRow -----------------------------------------------------------------------

MenuRow::MenuRow(const QString &icon, const QString &label, const QString &value, bool chevron, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(icon)
    , m_value(value)
    , m_chevron(chevron)
{
    setText(label);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

void MenuRow::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const bool hot = underMouse() && isEnabled();
    if (hot)
        Theme::paintHover(p, QRectF(rect()), 1.0, 6);
    const double textX = m_icon.isEmpty() ? 8 : 34;
    if (!m_icon.isEmpty())
        Icons::paint(p, QRectF(8, height() / 2.0 - 8, 16, 16), m_icon, hot ? c.starlight : c.mist);
    double right = width() - 8;
    if (m_chevron) {
        Icons::paint(p, QRectF(right - 13, height() / 2.0 - 6.5, 13, 13), QStringLiteral("chevron-right"), c.fog);
        right -= 19;
    }
    p.setFont(Theme::uiFont(13.5));
    const QFontMetricsF fm(p.font());
    double valueW = 0;
    if (!m_value.isEmpty()) {
        valueW = qMin(fm.horizontalAdvance(m_value), (right - textX) * 0.5);
        p.setPen(c.fog);
        p.drawText(QRectF(right - valueW, 0, valueW, height()), Qt::AlignVCenter | Qt::AlignRight, fm.elidedText(m_value, Qt::ElideRight, valueW));
        valueW += 12;
    }
    p.setPen(isEnabled() ? (hot ? c.starlight : c.moon) : c.dust);
    p.drawText(QRectF(textX, 0, right - valueW - textX, height()), Qt::AlignVCenter, fm.elidedText(text(), Qt::ElideRight, right - valueW - textX));
}

// ---- Segmented -----------------------------------------------------------------------

Segmented::Segmented(const QList<QPair<QString, QString>> &options, const QString &current, QWidget *parent)
    : QWidget(parent)
    , m_options(options)
{
    for (int i = 0; i < m_options.size(); ++i)
        if (m_options[i].first == current)
            m_current = i;
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
}

QSize Segmented::sizeHint() const
{
    const QFontMetricsF fm(Theme::uiFont(12.5, QFont::Medium));
    double widest = 0;
    for (const auto &o : m_options)
        widest = qMax(widest, fm.horizontalAdvance(o.second));
    return {int(m_options.size() * (widest + 20) + 4), 26};
}

QRectF Segmented::segment(int i) const
{
    const double w = (width() - 4.0) / qMax(1, int(m_options.size()));
    return QRectF(2 + i * w, 2, w, height() - 4);
}

void Segmented::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c.ash, 1));
    p.setBrush(QColor(0x18, 0x18, 0x1C));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.setFont(Theme::uiFont(12.5, QFont::Medium));
    for (int i = 0; i < m_options.size(); ++i) {
        const QRectF r = segment(i);
        if (i == m_current) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x34, 0x34, 0x3C));
            p.drawRoundedRect(r, 5, 5);
        } else if (i == m_hover) {
            Theme::paintHover(p, r, 1.0, 5);
        }
        p.setPen(i == m_current ? c.starlight : (i == m_hover ? c.moon : c.haze));
        p.drawText(r, Qt::AlignCenter, m_options[i].second);
    }
}

void Segmented::mouseMoveEvent(QMouseEvent *e)
{
    int h = -1;
    for (int i = 0; i < m_options.size(); ++i)
        if (segment(i).contains(e->position()))
            h = i;
    if (h != m_hover) {
        m_hover = h;
        update();
    }
}

void Segmented::mousePressEvent(QMouseEvent *e)
{
    for (int i = 0; i < m_options.size(); ++i)
        if (segment(i).contains(e->position()) && i != m_current) {
            m_current = i;
            update();
            emit changed(m_options[i].first);
        }
}

void Segmented::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---- SearchField -----------------------------------------------------------------

SearchField::SearchField(const QString &placeholder, QWidget *parent)
    : QLineEdit(parent)
{
    setPlaceholderText(placeholder);
    setFont(Theme::uiFont(13.5));
    setTextMargins(22, 0, 0, 0);
    setClearButtonEnabled(false);
}

void SearchField::paintEvent(QPaintEvent *e)
{
    QLineEdit::paintEvent(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Icons::paint(p, QRectF(m_iconX, (height() - m_iconSize) / 2.0, m_iconSize, m_iconSize), QStringLiteral("search"), Theme::colors().fog);
}

// ---- Toast -----------------------------------------------------------------------

Toast::Toast(QWidget *host)
    : QWidget(host)
{
    hide();
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_fade.setDirection(QAbstractAnimation::Backward);
        m_fade.start();
    });
    m_fade.setDuration(Theme::animMs(180));
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(1.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
    connect(&m_fade, &QVariantAnimation::finished, this, [this] {
        if (m_fade.direction() == QAbstractAnimation::Backward)
            hide();
    });
}

void Toast::show(const QString &message, const QString &action, std::function<void()> onAction)
{
    m_message = message;
    m_action = action;
    m_onAction = std::move(onAction);
    const QFontMetricsF fm(Theme::uiFont(13, QFont::Medium));
    const int w = int(fm.horizontalAdvance(message) + (action.isEmpty() ? 0 : fm.horizontalAdvance(action) + 28) + 48);
    QWidget *host = parentWidget();
    const QRect area = m_area.isValid() ? m_area : host->rect();
    setGeometry(area.left() + (area.width() - w) / 2, area.bottom() - 72, w, 44);
    QWidget::show();
    raise();
    m_fade.setDirection(QAbstractAnimation::Forward);
    m_fade.start();
    m_timer.start(action.isEmpty() ? 2600 : 5200);
}

void Toast::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double t = m_fade.state() == QAbstractAnimation::Running ? m_fade.currentValue().toDouble() : 1.0;
    p.setOpacity(t);
    p.translate(0, (1 - t) * 8);
    const QRectF card = QRectF(rect()).adjusted(4, 4, -4, -4);
    Theme::paintSoftShadow(p, card, card.height() / 2, 4, 1.0);
    Theme::paintCard(p, card, card.height() / 2, QColor(0x22, 0x22, 0x28));
    p.setFont(Theme::uiFont(13, QFont::Medium));
    p.setPen(c.moon);
    const QFontMetricsF fm(p.font());
    const double tw = fm.horizontalAdvance(m_message);
    p.drawText(QRectF(card.left() + 20, card.top(), tw + 2, card.height()), Qt::AlignVCenter, m_message);
    if (!m_action.isEmpty()) {
        m_actionRect = QRectF(card.left() + 20 + tw + 14, card.top() + 6, fm.horizontalAdvance(m_action) + 16, card.height() - 12);
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.08));
        p.drawRoundedRect(m_actionRect, m_actionRect.height() / 2, m_actionRect.height() / 2);
        p.setPen(c.starlight);
        p.drawText(m_actionRect, Qt::AlignCenter, m_action);
    }
}

void Toast::mousePressEvent(QMouseEvent *e)
{
    if (!m_action.isEmpty() && m_actionRect.contains(e->position()) && m_onAction) {
        auto fn = m_onAction;
        m_onAction = {};
        hide();
        fn();
    }
}

// ---- dialogs ---------------------------------------------------------------------

namespace
{
// Dims the window behind a dialog so the question stands out.
class Scrim : public QWidget
{
public:
    explicit Scrim(QWidget *host)
        : QWidget(host)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setGeometry(host->rect());
        raise();
        show();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 105));
        const bool square = window()->isMaximized() || window()->isFullScreen();
        p.drawRoundedRect(QRectF(rect()), square ? 0 : Theme::kRadius, square ? 0 : Theme::kRadius);
    }
};

class StyledDialog : public QDialog
{
public:
    explicit StyledDialog(QWidget *parent)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        if (parent && parent->window())
            m_scrim = new Scrim(parent->window());
    }
    ~StyledDialog() override { delete m_scrim; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(18, 12, -18, -24);
        Theme::paintSoftShadow(p, card, 14, 16, 1.6);
        Theme::paintCard(p, card, 14, Theme::colors().slate);
    }

private:
    QPointer<Scrim> m_scrim;
};

QPushButton *dialogButton(const QString &text, bool primary, bool destructive)
{
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setFont(Theme::uiFont(13, QFont::Medium));
    b->setMinimumHeight(32);
    const QString bg = primary ? (destructive ? QStringLiteral("#f1f1f5") : QStringLiteral("#c9c9d0")) : QStringLiteral("#23232a");
    const QString fg = primary ? QStringLiteral("#0e0e11") : QStringLiteral("#c9c9d0");
    b->setStyleSheet(QStringLiteral("QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 4px 16px; }"
                                    "QPushButton:hover { background: %4; }")
                         .arg(bg, fg, primary ? bg : QStringLiteral("#2e2e35"),
                              primary ? QStringLiteral("#ffffff") : QStringLiteral("#2e2e35")));
    return b;
}
}

bool confirmDialog(QWidget *parent, const QString &title, const QString &text, const QString &primary, bool destructive)
{
    StyledDialog dlg(parent);
    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(42, 34, 42, 46);
    outer->setSpacing(10);
    auto *t = new QLabel(title);
    t->setFont(Theme::uiFont(16, QFont::DemiBold));
    t->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    auto *body = new QLabel(text);
    body->setWordWrap(true);
    body->setFont(Theme::uiFont(13));
    body->setStyleSheet(QStringLiteral("color: #a9a9b2;"));
    body->setMinimumWidth(320);
    outer->addWidget(t);
    outer->addWidget(body);
    outer->addSpacing(10);
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *cancel = dialogButton(QObject::tr("Cancel"), false, false);
    auto *ok = dialogButton(primary, true, destructive);
    row->addWidget(cancel);
    row->addWidget(ok);
    outer->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    ok->setDefault(true);
    return dlg.exec() == QDialog::Accepted;
}

bool textDialog(QWidget *parent, const QString &title, const QString &label, QString *value)
{
    StyledDialog dlg(parent);
    auto *outer = new QVBoxLayout(&dlg);
    outer->setContentsMargins(42, 34, 42, 46);
    outer->setSpacing(10);
    auto *t = new QLabel(title);
    t->setFont(Theme::uiFont(16, QFont::DemiBold));
    t->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    outer->addWidget(t);
    if (!label.isEmpty()) {
        auto *l = new QLabel(label);
        l->setFont(Theme::uiFont(13));
        l->setStyleSheet(QStringLiteral("color: #a9a9b2;"));
        outer->addWidget(l);
    }
    auto *edit = new QLineEdit(*value);
    edit->setMinimumWidth(340);
    edit->setFont(Theme::uiFont(14));
    edit->selectAll();
    outer->addWidget(edit);
    outer->addSpacing(8);
    auto *row = new QHBoxLayout;
    row->addStretch();
    auto *cancel = dialogButton(QObject::tr("Cancel"), false, false);
    auto *ok = dialogButton(QObject::tr("Save"), true, false);
    row->addWidget(cancel);
    row->addWidget(ok);
    outer->addLayout(row);
    QObject::connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    QObject::connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    QObject::connect(edit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted)
        return false;
    *value = edit->text();
    return true;
}

QString relativeTime(qint64 msecs)
{
    if (msecs <= 0)
        return {};
    const QDateTime then = QDateTime::fromMSecsSinceEpoch(msecs);
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secs = then.secsTo(now);
    if (secs < 45)
        return QObject::tr("just now");
    if (secs < 3600)
        return QObject::tr("%n minute(s) ago", nullptr, int(qMax<qint64>(1, secs / 60)));
    if (then.date() == now.date())
        return QObject::tr("%n hour(s) ago", nullptr, int(secs / 3600));
    if (then.date() == now.date().addDays(-1))
        return QObject::tr("Yesterday");
    if (then.date().daysTo(now.date()) < 7)
        return QLocale(QLocale::English).toString(then.date(), QStringLiteral("dddd"));
    if (then.date().year() == now.date().year())
        return QLocale(QLocale::English).toString(then.date(), QStringLiteral("MMM d"));
    return QLocale(QLocale::English).toString(then.date(), QStringLiteral("MMM d, yyyy"));
}
