#include "PageMenu.h"

#include "core/Blocks.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QRegularExpression>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kWidth = 292;

// One of the three fonts, shown as "Ag" set in it.
class FontTile : public QAbstractButton
{
public:
    FontTile(const QString &key, const QString &name)
        : m_key(key)
    {
        setText(name);
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFixedHeight(66);
    }
    QString key() const { return m_key; }
    QSize sizeHint() const override { return {84, 66}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        if (isChecked()) {
            p.setPen(QPen(Theme::withAlpha(c.mist, 0.55), 1));
            p.setBrush(QColor(0x2A, 0x2A, 0x31));
        } else {
            p.setPen(QPen(underMouse() ? c.smoke : Qt::transparent, 1));
            p.setBrush(underMouse() ? QColor(0x23, 0x23, 0x29) : Qt::transparent);
        }
        p.drawRoundedRect(r, 8, 8);
        QFont f(Theme::pageFamily(m_key));
        f.setPixelSize(24);
        f.setWeight(QFont::DemiBold);
        if (m_key == QLatin1String("sans"))
            f = Theme::displayFont(24, QFont::DemiBold);
        p.setFont(f);
        p.setPen(isChecked() ? c.starlight : c.moon);
        p.drawText(QRectF(0, 6, width(), 34), Qt::AlignCenter, QStringLiteral("Ag"));
        p.setFont(Theme::uiFont(11.5, QFont::Medium));
        p.setPen(isChecked() ? c.moon : c.fog);
        p.drawText(QRectF(0, 40, width(), 18), Qt::AlignCenter, text());
    }

private:
    QString m_key;
};

QLabel *iconLabel(const QString &icon, QWidget *ref)
{
    auto *l = new QLabel;
    l->setPixmap(Icons::pixmap(icon, 16, Theme::colors().mist, ref->devicePixelRatioF()));
    l->setFixedWidth(26);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QLabel *rowText(const QString &text)
{
    auto *l = new QLabel(text);
    l->setFont(Theme::uiFont(13.5));
    l->setStyleSheet(QStringLiteral("color: #e6e6eb;"));
    return l;
}
} // namespace

PageMenu::PageMenu(QWidget *parent, const QString &pageId)
    : Popover(parent)
    , m_pageId(pageId)
{
    setObjectName(QStringLiteral("pageMenu"));
    content()->setFixedWidth(kWidth);
    m_layout = new QVBoxLayout(content());
    m_layout->setContentsMargins(6, 6, 6, 6);
    m_layout->setSpacing(1);
    rebuild();
}

void PageMenu::rebuild()
{
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        } else if (QLayout *l = item->layout()) {
            while (QLayoutItem *inner = l->takeAt(0)) {
                if (inner->widget())
                    inner->widget()->deleteLater();
                delete inner;
            }
        }
        delete item;
    }
    if (m_page == QLatin1String("customize"))
        customizePage();
    else
        mainPage();
    QTimer::singleShot(0, this, [this] {
        content()->layout()->activate();
        layout()->activate();
        resize(sizeHint());
    });
}

void PageMenu::addSeparator()
{
    auto *line = new QFrame;
    line->setFixedHeight(9);
    line->setStyleSheet(QStringLiteral("QFrame { background: transparent; border: none; border-top: 1px solid #2a2a31; margin-top: 4px; }"));
    m_layout->addWidget(line);
}

MenuRow *PageMenu::addRow(const QString &icon, const QString &label, std::function<void()> fn, bool chevron)
{
    auto *row = new MenuRow(icon, label, QString(), chevron);
    connect(row, &MenuRow::clicked, this, std::move(fn));
    m_layout->addWidget(row);
    return row;
}

void PageMenu::run(const QString &id)
{
    close();
    emit action(id);
}

void PageMenu::setOptions(const std::function<void(PageOptions &)> &change)
{
    Workspace *ws = Workspace::instance();
    PageOptions o = ws->options(m_pageId);
    change(o);
    ws->setOptions(m_pageId, o);
}

void PageMenu::mainPage()
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws->page(m_pageId);
    if (!m)
        return;
    const PageOptions o = ws->options(m_pageId);

    // Font: three tiles.
    auto *fonts = new QWidget;
    auto *fl = new QHBoxLayout(fonts);
    fl->setContentsMargins(2, 2, 2, 4);
    fl->setSpacing(6);
    auto *group = new QButtonGroup(fonts);
    for (const auto &[key, name] : {std::pair{QStringLiteral("sans"), tr("Default")}, std::pair{QStringLiteral("serif"), tr("Serif")},
                                    std::pair{QStringLiteral("mono"), tr("Mono")}}) {
        auto *tile = new FontTile(key, name);
        tile->setChecked(o.font == key);
        group->addButton(tile);
        fl->addWidget(tile, 1);
        connect(tile, &FontTile::clicked, this, [this, key = key] { setOptions([&](PageOptions &po) { po.font = key; }); });
    }
    m_layout->addWidget(fonts);
    addSeparator();

    // Text size and width: always both choices in view, so either way is a click.
    auto choice = [&](const QString &icon, const QString &label, const QList<QPair<QString, QString>> &options, const QString &current,
                      std::function<void(const QString &)> fn) {
        auto *row = new QWidget;
        row->setFixedHeight(34);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 4, 0);
        h->setSpacing(8);
        h->addWidget(iconLabel(icon, this));
        h->addWidget(rowText(label), 1);
        auto *seg = new Segmented(options, current);
        h->addWidget(seg);
        connect(seg, &Segmented::changed, this, std::move(fn));
        m_layout->addWidget(row);
    };
    choice(QStringLiteral("textsize"), tr("Text size"), {{QStringLiteral("default"), tr("Default")}, {QStringLiteral("small"), tr("Small")}},
           o.smallText ? QStringLiteral("small") : QStringLiteral("default"),
           [this](const QString &k) { setOptions([&](PageOptions &po) { po.smallText = k == QLatin1String("small"); }); });
    choice(QStringLiteral("width"), tr("Page width"), {{QStringLiteral("default"), tr("Default")}, {QStringLiteral("full"), tr("Full")}},
           o.fullWidth ? QStringLiteral("full") : QStringLiteral("default"),
           [this](const QString &k) { setOptions([&](PageOptions &po) { po.fullWidth = k == QLatin1String("full"); }); });

    // Lock: a switch; the whole row flips it.
    {
        auto *row = new MenuRow(o.locked ? QStringLiteral("lock") : QStringLiteral("unlock"), tr("Lock page"));
        auto *sw = new Switch(row);
        sw->setChecked(o.locked);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 8, 0);
        h->addStretch();
        h->addWidget(sw, 0, Qt::AlignVCenter);
        connect(row, &MenuRow::clicked, sw, &Switch::click);
        connect(sw, &Switch::toggled, this, [this](bool on) { setOptions([&](PageOptions &po) { po.locked = on; }); });
        m_layout->addWidget(row);
    }
    addRow(QStringLiteral("settings"), tr("Customize page"),
           [this] {
               m_page = QStringLiteral("customize");
               QTimer::singleShot(0, this, [this] { rebuild(); });
           },
           true);
    addSeparator();

    const bool fav = m->favorite > 0;
    addRow(fav ? QStringLiteral("star-filled") : QStringLiteral("star"), fav ? tr("Remove from Favorites") : tr("Add to Favorites"),
           [this] { run(QStringLiteral("favorite")); });
    addRow(QStringLiteral("copy"), tr("Duplicate"), [this] { run(QStringLiteral("duplicate")); });
    if (!m->isRow())
        addRow(QStringLiteral("move"), tr("Move to…"), [this] { run(QStringLiteral("move")); });
    addRow(QStringLiteral("link"), tr("Copy title"), [this] { run(QStringLiteral("copytitle")); });
    addRow(QStringLiteral("history"), tr("Version history"), [this] { run(QStringLiteral("history")); });
    auto *exportRow = new MenuRow(QStringLiteral("upload"), tr("Export"), QString(), true);
    m_layout->addWidget(exportRow);
    connect(exportRow, &MenuRow::clicked, this, [this, exportRow] {
        QMenu *menu = Theme::makeMenu(this);
        menu->addAction(Icons::icon(QStringLiteral("page")), tr("Markdown (with sub-pages)…"), this, [this] { run(QStringLiteral("export:md")); });
        menu->addAction(Icons::icon(QStringLiteral("globe")), tr("HTML…"), this, [this] { run(QStringLiteral("export:html")); });
        menu->addAction(Icons::icon(QStringLiteral("download")), tr("PDF…"), this, [this] { run(QStringLiteral("export:pdf")); });
        menu->popup(exportRow->mapToGlobal(QPoint(exportRow->width() + 4, -6)));
    });
    if (!m->isRow())
        addRow(QStringLiteral("download"), tr("Import into this page…"), [this] { run(QStringLiteral("import")); });
    addSeparator();
    addRow(QStringLiteral("trash"), tr("Move to Trash"), [this] { run(QStringLiteral("trash")); });
    addSeparator();

    int words = 0;
    for (const Block &b : ws->content(m_pageId))
        words += int(b.plainText().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size());
    auto *info = new QLabel(tr("%n word(s) · Edited %1", nullptr, words).arg(relativeTime(m->updated)));
    info->setFont(Theme::uiFont(12));
    info->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 2px 8px 4px 8px;"));
    m_layout->addWidget(info);
}

void PageMenu::customizePage()
{
    const PageOptions o = Workspace::instance()->options(m_pageId);
    // Back to the main page.
    auto *top = new QWidget;
    top->setFixedHeight(34);
    auto *th = new QHBoxLayout(top);
    th->setContentsMargins(2, 0, 0, 0);
    th->setSpacing(4);
    auto *back = new FlatButton(QStringLiteral("chevron-left"));
    back->setToolTip(tr("Back"));
    connect(back, &FlatButton::clicked, this, [this] {
        m_page.clear();
        QTimer::singleShot(0, this, [this] { rebuild(); });
    });
    th->addWidget(back);
    auto *title = new QLabel(tr("Customize page"));
    title->setFont(Theme::uiFont(14, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    th->addWidget(title, 1);
    m_layout->addWidget(top);
    addSeparator();

    auto toggle = [&](const QString &icon, const QString &label, bool on, std::function<void(PageOptions &, bool)> set) {
        auto *row = new MenuRow(icon, label);
        auto *sw = new Switch(row);
        sw->setChecked(on);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 8, 0);
        h->addStretch();
        h->addWidget(sw, 0, Qt::AlignVCenter);
        connect(row, &MenuRow::clicked, sw, &Switch::click);
        connect(sw, &Switch::toggled, this, [this, set](bool v) { setOptions([&](PageOptions &po) { set(po, v); }); });
        m_layout->addWidget(row);
    };
    toggle(QStringLiteral("toc"), tr("Outline in the margin"), o.showToc, [](PageOptions &po, bool v) { po.showToc = v; });
    toggle(QStringLiteral("comment"), tr("Page discussions"), o.showDiscussion, [](PageOptions &po, bool v) { po.showDiscussion = v; });

    auto *row = new QWidget;
    row->setFixedHeight(34);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 4, 0);
    h->setSpacing(8);
    h->addWidget(iconLabel(QStringLiteral("link"), this));
    h->addWidget(rowText(tr("Backlinks")), 1);
    auto *seg = new Segmented({{QStringLiteral("expanded"), tr("List")}, {QStringLiteral("popover"), tr("Count")}, {QStringLiteral("off"), tr("Off")}},
                              o.backlinks);
    seg->setToolTip(tr("Pages linking here: listed at the bottom, a count that opens a menu, or hidden"));
    h->addWidget(seg);
    connect(seg, &Segmented::changed, this, [this](const QString &k) { setOptions([&](PageOptions &po) { po.backlinks = k; }); });
    m_layout->addWidget(row);
}
