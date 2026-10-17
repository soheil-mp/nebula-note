#include "History.h"

#include "core/DateParse.h"
#include "core/Workspace.h"
#include "editor/BlockEditor.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
QString when(qint64 msecs)
{
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(msecs);
    const QLocale en(QLocale::English);
    const QDate today = QDate::currentDate();
    if (t.date() == today)
        return QCoreApplication::translate("History", "Today, %1").arg(en.toString(t.time(), QStringLiteral("h:mm AP")));
    if (t.date() == today.addDays(-1))
        return QCoreApplication::translate("History", "Yesterday, %1").arg(en.toString(t.time(), QStringLiteral("h:mm AP")));
    return en.toString(t, QStringLiteral("MMM d, yyyy, h:mm AP"));
}
} // namespace

VersionHistory::VersionHistory(QWidget *host, const QString &pageId)
    : ModalOverlay(host, QSize(qMin(1180, int(host->width() * 0.9)), qMin(860, int(host->height() * 0.88))), 40)
    , m_pageId(pageId)
{
    setObjectName(QStringLiteral("versionHistory"));
    auto *h = new QHBoxLayout(card());
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    // Preview.
    auto *left = new QWidget;
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(48, 32, 32, 16);
    lv->setSpacing(12);
    m_title = new QLabel;
    m_title->setFont(Theme::displayFont(30, QFont::Bold));
    m_title->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    m_title->setWordWrap(true);
    lv->addWidget(m_title);
    auto *scroll = new QScrollArea;
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    m_preview = new BlockEditor;
    m_preview->setObjectName(QStringLiteral("versionPreview"));
    m_preview->setReadOnly(true);
    scroll->setWidget(m_preview);
    lv->addWidget(scroll, 1);
    h->addWidget(left, 1);
    // Versions.
    auto *right = new QWidget;
    right->setFixedWidth(300);
    right->setObjectName(QStringLiteral("historyList"));
    right->setStyleSheet(QStringLiteral("#historyList { background: #141418; border-left: 1px solid #26262d; }"
                                        "QListWidget { background: transparent; border: none; outline: none; color: #d4d4dc; }"
                                        "QListWidget::item { padding: 8px 10px; border-radius: 7px; margin: 1px 0; }"
                                        "QListWidget::item:selected { background: #2a2a31; color: #f1f1f5; }"
                                        "QListWidget::item:hover:!selected { background: #1e1e23; }"));
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(12, 18, 12, 14);
    rv->setSpacing(8);
    auto *head = new QHBoxLayout;
    auto *label = new QLabel(tr("Version history"));
    label->setFont(Theme::uiFont(14, QFont::DemiBold));
    label->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    head->addWidget(label, 1);
    auto *close = new FlatButton(QStringLiteral("close"));
    close->setToolTip(tr("Close (Esc)"));
    connect(close, &FlatButton::clicked, this, &ModalOverlay::dismiss);
    head->addWidget(close);
    rv->addLayout(head);
    auto *hint = new QLabel(tr("A version is kept every ten minutes of editing. Double-click one to name it."));
    hint->setWordWrap(true);
    hint->setFont(Theme::uiFont(12));
    hint->setStyleSheet(QStringLiteral("color: #7a7a86;"));
    rv->addWidget(hint);
    m_list = new QListWidget;
    m_list->setFont(Theme::uiFont(13));
    rv->addWidget(m_list, 1);
    m_restore = new FlatButton(QStringLiteral("restore"), tr("Restore version"));
    m_restore->setFilled(true);
    m_restore->setObjectName(QStringLiteral("restoreVersion"));
    rv->addWidget(m_restore);
    h->addWidget(right);

    connect(m_list, &QListWidget::currentRowChanged, this, &VersionHistory::showSelected);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const qint64 id = item->data(Qt::UserRole).toLongLong();
        if (id <= 0)
            return;
        QString name = item->data(Qt::UserRole + 1).toString();
        if (textDialog(window(), tr("Name this version"), tr("Named versions are never cleaned up."), &name)) {
            Workspace::instance()->nameVersion(id, name.trimmed());
            reload();
        }
    });
    connect(m_restore, &FlatButton::clicked, this, &VersionHistory::restoreSelected);
    reload();
}

void VersionHistory::reload()
{
    Workspace *ws = Workspace::instance();
    const int row = qMax(0, m_list->currentRow());
    m_list->blockSignals(true);
    m_list->clear();
    auto *current = new QListWidgetItem(Icons::icon(QStringLiteral("page")), tr("Current version"));
    current->setData(Qt::UserRole, qint64(0));
    m_list->addItem(current);
    for (const Workspace::Version &v : ws->versions(m_pageId)) {
        QString text = when(v.created);
        if (!v.name.isEmpty())
            text = v.name + QStringLiteral("\n") + text;
        auto *item = new QListWidgetItem(Icons::icon(v.name.isEmpty() ? QStringLiteral("history") : QStringLiteral("bookmark")), text);
        item->setData(Qt::UserRole, v.id);
        item->setData(Qt::UserRole + 1, v.name);
        item->setToolTip(v.title);
        m_list->addItem(item);
    }
    m_list->blockSignals(false);
    m_list->setCurrentRow(qMin(row, m_list->count() - 1));
    showSelected();
}

void VersionHistory::showSelected()
{
    Workspace *ws = Workspace::instance();
    QListWidgetItem *item = m_list->currentItem();
    const qint64 id = item ? item->data(Qt::UserRole).toLongLong() : 0;
    QString title;
    QList<Block> blocks;
    if (id > 0) {
        blocks = ws->versionContent(id, &title);
    } else {
        blocks = ws->content(m_pageId);
        title = ws->title(m_pageId);
    }
    m_title->setText(title.isEmpty() ? tr("Untitled") : title);
    m_preview->setBlocks(blocks);
    m_restore->setEnabled(id > 0);
}

void VersionHistory::restoreSelected()
{
    QListWidgetItem *item = m_list->currentItem();
    const qint64 id = item ? item->data(Qt::UserRole).toLongLong() : 0;
    if (id <= 0)
        return;
    Workspace::instance()->restoreVersion(id);
    emit restored(m_pageId);
    dismiss();
}

// ---- inbox -------------------------------------------------------------------------

InboxPopover::InboxPopover(QWidget *parent)
    : Popover(parent)
{
    setObjectName(QStringLiteral("inbox"));
    m_layout = new QVBoxLayout(content());
    m_layout->setContentsMargins(12, 10, 12, 12);
    m_layout->setSpacing(4);
    rebuild();
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::remindersChanged, this, &InboxPopover::rebuild);
        connect(ws, &Workspace::commentsChanged, this, &InboxPopover::rebuild);
    }
}

void InboxPopover::rebuild()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    m_items = 0;
    Workspace *ws = Workspace::instance();
    auto section = [&](const QString &text) {
        auto *l = new QLabel(text);
        l->setFont(Theme::uiFont(11.5, QFont::DemiBold));
        l->setStyleSheet(QStringLiteral("color: #7a7a86; padding: 10px 4px 2px 6px;"));
        m_layout->addWidget(l);
    };
    auto title = new QLabel(tr("Inbox"));
    title->setFont(Theme::uiFont(15, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #f1f1f5; padding: 2px 4px;"));
    m_layout->addWidget(title);

    // Reminders: overdue first, then the next two weeks.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 horizon = now + 14LL * 24 * 3600 * 1000;
    section(tr("Reminders"));
    int reminders = 0;
    for (const Workspace::Reminder &r : ws->reminders()) {
        if (r.at > horizon)
            continue;
        auto *row = new QWidget;
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(4);
        auto *done = new FlatButton(QStringLiteral("checkbox"));
        done->setToolTip(tr("Mark as done"));
        const QString id = r.id;
        connect(done, &FlatButton::clicked, this, [id] { Workspace::instance()->setReminderDone(id, true); });
        h->addWidget(done);
        const QDateTime at = QDateTime::fromMSecsSinceEpoch(r.at);
        const QString label = QStringLiteral("%1  ·  %2%3")
                                  .arg(r.text.isEmpty() ? ws->title(r.pageId) : r.text.left(60), DateParse::relativeLabel(at, true),
                                       r.at < now ? tr("  · overdue") : QString());
        auto *open = new FlatButton(QStringLiteral("bell"), label);
        open->setTextSize(13);
        open->setMinimumWidth(320);
        open->setToolTip(ws->title(r.pageId));
        const QString page = r.pageId;
        const QString block = r.id.section(QLatin1Char(':'), 1, 1);
        connect(open, &FlatButton::clicked, this, [this, page, block] {
            emit openPage(page, block);
            close();
        });
        h->addWidget(open, 1);
        m_layout->addWidget(row);
        ++reminders;
    }
    if (reminders == 0) {
        auto *none = new QLabel(tr("Nothing due. Type @ and a date with “remind” to set one."));
        none->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 2px 4px;"));
        none->setFont(Theme::uiFont(12.5));
        m_layout->addWidget(none);
    }
    // Comment activity.
    section(tr("Comments"));
    const QList<Workspace::Comment> comments = ws->recentComments(12);
    for (const Workspace::Comment &c : comments) {
        const QString label = QStringLiteral("%1 — %2  ·  %3").arg(ws->title(c.pageId), c.body.simplified().left(50), relativeTime(c.created));
        auto *open = new FlatButton(c.resolved ? QStringLiteral("check-circle") : QStringLiteral("comment"), label);
        open->setTextSize(13);
        open->setMinimumWidth(360);
        const QString page = c.pageId;
        const QString block = c.blockId;
        connect(open, &FlatButton::clicked, this, [this, page, block] {
            emit openPage(page, block);
            close();
        });
        m_layout->addWidget(open);
    }
    if (comments.isEmpty()) {
        auto *none = new QLabel(tr("No comments yet."));
        none->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 2px 4px;"));
        none->setFont(Theme::uiFont(12.5));
        m_layout->addWidget(none);
    }
    m_items = reminders + int(comments.size());
    QTimer::singleShot(0, this, [this] { adjustSize(); });
}
