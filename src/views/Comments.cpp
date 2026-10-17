#include "Comments.h"

#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
QString fieldStyle()
{
    return QStringLiteral("QPlainTextEdit { background: #141418; color: #e6e6ec; border: 1px solid #2c2c34; border-radius: 8px;"
                          " padding: 5px 7px; selection-background-color: #46465a; }");
}

QString when(qint64 ms)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(ms);
    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QObject::tr("just now");
    if (secs < 3600)
        return QObject::tr("%n min ago", nullptr, int(secs / 60));
    if (secs < 86400)
        return QObject::tr("%n h ago", nullptr, int(secs / 3600));
    return QLocale(QLocale::English).toString(dt, QStringLiteral("MMM d, h:mm AP"));
}

// A round badge with the author's initial.
class Avatar : public QWidget
{
public:
    Avatar(const QString &name, QWidget *parent = nullptr) : QWidget(parent), m_name(name) { setFixedSize(22, 22); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3A, 0x3A, 0x44));
        p.drawEllipse(rect().adjusted(1, 1, -1, -1));
        p.setPen(Theme::colors().starlight);
        p.setFont(Theme::uiFont(11, QFont::DemiBold));
        p.drawText(rect(), Qt::AlignCenter, m_name.left(1).toUpper());
    }

private:
    QString m_name;
};

QPlainTextEdit *makeInput(const QString &placeholder, QObject *filterOwner)
{
    auto *e = new QPlainTextEdit;
    e->setPlaceholderText(placeholder);
    e->setFont(Theme::uiFont(13.5));
    e->setFixedHeight(56);
    e->setStyleSheet(fieldStyle());
    e->installEventFilter(filterOwner);
    return e;
}

QWidget *commentRow(const Workspace::Comment &c, QWidget *owner, std::function<void()> changed)
{
    Workspace *ws = Workspace::instance();
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 4, 0, 4);
    h->setSpacing(8);
    const QString author = ws->people().value(0).name;
    h->addWidget(new Avatar(author), 0, Qt::AlignTop);
    auto *v = new QVBoxLayout;
    v->setSpacing(2);
    auto *meta = new QLabel(QStringLiteral("<b>%1</b>&nbsp;&nbsp;<span style='color:#7a7a86'>%2%3</span>")
                                .arg(author.toHtmlEscaped(), when(c.created), c.edited ? QObject::tr(" · edited") : QString()));
    meta->setFont(Theme::uiFont(12.5));
    meta->setStyleSheet(QStringLiteral("color: #e6e6ec;"));
    v->addWidget(meta);
    auto *body = new QLabel(c.body.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->setFont(Theme::uiFont(13.5));
    body->setStyleSheet(QStringLiteral("color: #d4d4dc;"));
    body->setMinimumWidth(260);
    v->addWidget(body);
    h->addLayout(v, 1);
    auto *edit = new FlatButton(QStringLiteral("pencil"));
    edit->setToolTip(QObject::tr("Edit"));
    auto *del = new FlatButton(QStringLiteral("trash"));
    del->setToolTip(QObject::tr("Delete"));
    h->addWidget(edit, 0, Qt::AlignTop);
    h->addWidget(del, 0, Qt::AlignTop);
    const QString id = c.id;
    QObject::connect(del, &FlatButton::clicked, owner, [id, changed] {
        Workspace::instance()->deleteComment(id);
        changed();
    });
    QObject::connect(edit, &FlatButton::clicked, owner, [w, v, body, id, c, changed, owner] {
        body->hide();
        auto *e = new QPlainTextEdit(c.body);
        e->setFont(Theme::uiFont(13.5));
        e->setFixedHeight(64);
        e->setStyleSheet(fieldStyle());
        v->addWidget(e);
        auto *save = new FlatButton(QString(), QObject::tr("Save"));
        save->setFilled(true);
        v->addWidget(save, 0, Qt::AlignRight);
        e->setFocus();
        QObject::connect(save, &FlatButton::clicked, owner, [e, id, changed] {
            if (!e->toPlainText().trimmed().isEmpty())
                Workspace::instance()->editComment(id, e->toPlainText().trimmed());
            changed();
        });
        Q_UNUSED(w)
    });
    return w;
}
} // namespace

// ---- composer --------------------------------------------------------------------------

CommentComposer::CommentComposer(QWidget *parent, const QString &quote)
    : Popover(parent)
{
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    if (!quote.isEmpty()) {
        auto *q = new QLabel(QStringLiteral("“%1”").arg(quote.left(160).toHtmlEscaped()));
        q->setWordWrap(true);
        q->setFont(Theme::uiFont(12.5));
        q->setStyleSheet(QStringLiteral("color: #9a9aa6; border-left: 2px solid #4a4a55; padding-left: 8px;"));
        q->setMaximumWidth(380);
        v->addWidget(q);
    }
    m_edit = makeInput(tr("Add a comment…"), this);
    m_edit->setFixedWidth(380);
    v->addWidget(m_edit);
    auto *row = new QHBoxLayout;
    auto *hint = new QLabel(tr("Enter to post · Shift+Enter for a new line"));
    hint->setFont(Theme::uiFont(11.5));
    hint->setStyleSheet(QStringLiteral("color: #6e6e7a;"));
    row->addWidget(hint);
    row->addStretch();
    auto *post = new FlatButton(QStringLiteral("comment"), tr("Comment"));
    post->setFilled(true);
    row->addWidget(post);
    v->addLayout(row);
    connect(post, &FlatButton::clicked, this, [this] {
        if (!m_edit->toPlainText().trimmed().isEmpty())
            emit posted(m_edit->toPlainText().trimmed());
        close();
    });
    QTimer::singleShot(0, m_edit, [this] { m_edit->setFocus(); });
}

bool CommentComposer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
            if (!m_edit->toPlainText().trimmed().isEmpty())
                emit posted(m_edit->toPlainText().trimmed());
            close();
            return true;
        }
    }
    return Popover::eventFilter(watched, event);
}

// ---- threads -------------------------------------------------------------------------------

CommentThreads::CommentThreads(QWidget *parent, const QString &pageId, const Filter &filter)
    : Popover(parent)
    , m_pageId(pageId)
    , m_filter(filter)
{
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 10, 12, 12);
    v->setSpacing(6);
    auto *head = new QHBoxLayout;
    auto *title = new QLabel(filter.all ? tr("Comments") : tr("Discussion"));
    title->setFont(Theme::uiFont(13, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    head->addWidget(title);
    head->addStretch();
    auto *resolvedToggle = new FlatButton(QStringLiteral("check-circle"), tr("Resolved"));
    resolvedToggle->setCheckable(true);
    head->addWidget(resolvedToggle);
    v->addLayout(head);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(420);
    scroll->setMaximumHeight(460);
    m_list = new QWidget;
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 4, 0);
    m_listLayout->setSpacing(10);
    scroll->setWidget(m_list);
    m_list->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    v->addWidget(scroll);
    connect(resolvedToggle, &FlatButton::toggled, this, [this](bool on) {
        m_showResolved = on;
        rebuild();
    });
    rebuild();
}

void CommentThreads::rebuild()
{
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    Workspace *ws = Workspace::instance();
    const QList<Workspace::Comment> all = ws->comments(m_pageId);
    // Threads in order of their first comment.
    QStringList order;
    QHash<QString, QList<Workspace::Comment>> threads;
    for (const Workspace::Comment &c : all) {
        if (!threads.contains(c.thread))
            order.append(c.thread);
        threads[c.thread].append(c);
    }
    int shown = 0;
    for (const QString &t : order) {
        const QList<Workspace::Comment> &cs = threads[t];
        const Workspace::Comment &first = cs.first();
        if (!m_filter.thread.isEmpty() && t != m_filter.thread)
            continue;
        if (!m_filter.blockId.isEmpty() && first.blockId != m_filter.blockId)
            continue;
        if (first.resolved != m_showResolved && m_filter.thread.isEmpty())
            continue;
        ++shown;
        auto *card = new QWidget;
        card->setObjectName(QStringLiteral("thread"));
        card->setAttribute(Qt::WA_StyledBackground);
        card->setStyleSheet(QStringLiteral("QWidget#thread { background: #1a1a1f; border: 1px solid #2a2a31; border-radius: 10px; }"));
        auto *cv = new QVBoxLayout(card);
        cv->setContentsMargins(10, 8, 10, 10);
        cv->setSpacing(2);
        if (!first.quote.isEmpty()) {
            auto *q = new QLabel(QStringLiteral("“%1”").arg(first.quote.left(140).toHtmlEscaped()));
            q->setWordWrap(true);
            q->setFont(Theme::uiFont(12));
            q->setStyleSheet(QStringLiteral("color: #8a8a96; border-left: 2px solid #4a4a55; padding-left: 8px; margin-bottom: 4px;"));
            cv->addWidget(q);
        }
        auto refresh = [this] { QTimer::singleShot(0, this, &CommentThreads::rebuild); };
        for (const Workspace::Comment &c : cs)
            cv->addWidget(commentRow(c, this, refresh));
        auto *reply = makeInput(first.resolved ? tr("Reply to reopen…") : tr("Reply…"), this);
        reply->setFixedHeight(40);
        cv->addWidget(reply);
        auto *actions = new QHBoxLayout;
        actions->addStretch();
        auto *resolve = new FlatButton(first.resolved ? QStringLiteral("refresh") : QStringLiteral("check"), first.resolved ? tr("Reopen") : tr("Resolve"));
        auto *send = new FlatButton(QStringLiteral("comment"), tr("Reply"));
        send->setFilled(true);
        actions->addWidget(resolve);
        actions->addWidget(send);
        cv->addLayout(actions);
        const QString page = m_pageId, block = first.blockId, quote = first.quote;
        const bool wasResolved = first.resolved;
        connect(resolve, &FlatButton::clicked, this, [this, t, wasResolved, refresh] {
            Workspace::instance()->resolveThread(t, !wasResolved);
            emit resolved(t, !wasResolved);
            refresh();
        });
        connect(send, &FlatButton::clicked, this, [reply, page, block, quote, t, refresh] {
            if (reply->toPlainText().trimmed().isEmpty())
                return;
            Workspace::instance()->addComment(page, block, quote, reply->toPlainText().trimmed(), t);
            refresh();
        });
        m_listLayout->addWidget(card);
    }
    if (shown == 0) {
        auto *empty = new QLabel(m_showResolved ? tr("No resolved comments.") : tr("No open comments."));
        empty->setFont(Theme::uiFont(13));
        empty->setStyleSheet(QStringLiteral("color: #7a7a86; padding: 12px 4px;"));
        m_listLayout->addWidget(empty);
    }
    m_listLayout->addStretch();
    adjustSize();
}

// ---- discussion ------------------------------------------------------------------------------

Discussion::Discussion(QWidget *parent)
    : QWidget(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 4, 0, 4);
    m_layout->setSpacing(4);
    m_items = new QWidget;
    m_itemsLayout = new QVBoxLayout(m_items);
    m_itemsLayout->setContentsMargins(0, 0, 0, 0);
    m_itemsLayout->setSpacing(2);
    m_layout->addWidget(m_items);
    m_input = makeInput(tr("Add a comment…"), this);
    m_input->setFixedHeight(38);
    m_layout->addWidget(m_input);
    if (Workspace *ws = Workspace::instance())
        connect(ws, &Workspace::commentsChanged, this, [this](const QString &page) {
            if (page == m_pageId)
                refresh();
        });
}

void Discussion::setPage(const QString &pageId)
{
    m_pageId = pageId;
    refresh();
}

void Discussion::refresh()
{
    while (QLayoutItem *item = m_itemsLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    Workspace *ws = Workspace::instance();
    if (!ws)
        return;
    int n = 0;
    for (const Workspace::Comment &c : ws->comments(m_pageId)) {
        if (!c.blockId.isEmpty() || c.resolved)
            continue;
        m_itemsLayout->addWidget(commentRow(c, this, [this] { QTimer::singleShot(0, this, &Discussion::refresh); }));
        ++n;
    }
    m_items->setVisible(n > 0);
    const int h = sizeHint().height();
    if (h != height()) {
        setFixedHeight(h);
        emit heightChanged();
    }
}

bool Discussion::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
            const QString body = m_input->toPlainText().trimmed();
            if (!body.isEmpty() && Workspace::instance()) {
                Workspace::instance()->addComment(m_pageId, QString(), QString(), body);
                m_input->clear();
            }
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            m_input->clearFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
