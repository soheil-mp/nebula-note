#include "Pickers.h"

#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

// ---- IconGrid ----------------------------------------------------------------------

class IconGrid : public QWidget
{
    Q_OBJECT
public:
    struct Section {
        QString title;
        QStringList icons; // "glyph:<name>" or emoji
    };
    explicit IconGrid(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }
    void setSections(const QList<Section> &sections)
    {
        m_sections = sections;
        m_hover = -1;
        relayout();
        update();
    }
    QSize sizeHint() const override { return {kCols * kCell + 8, m_height}; }

signals:
    void picked(const QString &icon);

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (const Header &h : m_headers) {
            p.setFont(Theme::uiFont(11.5, QFont::Medium));
            p.setPen(c.fog);
            p.drawText(QRectF(4, h.y, width() - 8, 22), Qt::AlignVCenter | Qt::AlignLeft, h.title);
        }
        for (int i = 0; i < m_cells.size(); ++i) {
            const Cell &cell = m_cells[i];
            if (i == m_hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.graphite);
                p.drawRoundedRect(cell.rect, 7, 7);
            }
            // Emoji carry their own padding: give them a bigger box than line glyphs.
            const double inset = cell.icon.startsWith(QLatin1String("glyph:")) ? 9 : 6;
            Icons::paintPageIcon(p, cell.rect.adjusted(inset, inset, -inset, -inset), cell.icon, i == m_hover ? c.starlight : c.moon);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int hover = -1;
        for (int i = 0; i < m_cells.size(); ++i)
            if (m_cells[i].rect.contains(e->position()))
                hover = i;
        if (hover != m_hover) {
            m_hover = hover;
            setToolTip(hover >= 0 && m_cells[hover].icon.startsWith(QLatin1String("glyph:")) ? m_cells[hover].icon.mid(6) : QString());
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (const Cell &cell : m_cells)
            if (cell.rect.contains(e->position())) {
                emit picked(cell.icon);
                return;
            }
    }

private:
    static constexpr int kCols = 9;
    static constexpr int kCell = 38;
    struct Cell {
        QRectF rect;
        QString icon;
    };
    struct Header {
        int y;
        QString title;
    };
    void relayout()
    {
        m_cells.clear();
        m_headers.clear();
        int y = 2;
        for (const Section &s : m_sections) {
            if (s.icons.isEmpty())
                continue;
            m_headers.append({y, s.title});
            y += 24;
            for (int i = 0; i < s.icons.size(); ++i) {
                const int col = i % kCols, row = i / kCols;
                m_cells.append({QRectF(4 + col * kCell, y + row * kCell, kCell - 2, kCell - 2), s.icons[i]});
            }
            y += ((int(s.icons.size()) + kCols - 1) / kCols) * kCell + 8;
        }
        m_height = y;
        setFixedHeight(qMax(60, y));
    }
    QList<Section> m_sections;
    QList<Cell> m_cells;
    QList<Header> m_headers;
    int m_hover = -1;
    int m_height = 60;
};

void IconPicker::showEmoji()
{
    m_emoji = true;
    m_emojiTab->setChecked(true);
    rebuild();
}

IconPicker::IconPicker(QWidget *parent, bool allowRemove, bool allowUpload)
    : Popover(parent)
{
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);
    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(2);
    m_glyphTab = new FlatButton(QString(), tr("Glyphs"));
    m_emojiTab = new FlatButton(QString(), tr("Emoji"));
    for (FlatButton *b : {m_glyphTab, m_emojiTab}) {
        b->setCheckable(true);
        b->setAutoExclusive(true);
        tabs->addWidget(b);
    }
    m_glyphTab->setChecked(true);
    tabs->addStretch();
    auto *random = new FlatButton(QStringLiteral("shuffle"));
    random->setToolTip(tr("Random"));
    tabs->addWidget(random);
    if (allowUpload) {
        auto *upload = new FlatButton(QStringLiteral("upload"), tr("Upload"));
        upload->setToolTip(tr("Use an image as the icon"));
        tabs->addWidget(upload);
        connect(upload, &FlatButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this, tr("Choose an image"), QString(),
                                                              tr("Images (*.png *.jpg *.jpeg *.gif *.webp *.svg)"));
            Workspace *ws = Workspace::instance();
            if (path.isEmpty() || !ws)
                return;
            const QString media = ws->importMedia(path);
            if (!media.isEmpty()) {
                emit picked(QStringLiteral("media:") + media);
                close();
            }
        });
    }
    if (allowRemove) {
        auto *remove = new FlatButton(QString(), tr("Remove"));
        tabs->addWidget(remove);
        connect(remove, &FlatButton::clicked, this, [this] {
            emit picked(QString());
            close();
        });
    }
    v->addLayout(tabs);
    m_search = new SearchField(tr("Filter…"));
    v->addWidget(m_search);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedSize(9 * 38 + 22, 300);
    scroll->setFrameStyle(QFrame::NoFrame);
    m_grid = new IconGrid(scroll);
    scroll->setWidget(m_grid);
    m_grid->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    v->addWidget(scroll);

    connect(m_glyphTab, &FlatButton::clicked, this, [this] {
        m_emoji = false;
        rebuild();
    });
    connect(m_emojiTab, &FlatButton::clicked, this, [this] {
        m_emoji = true;
        rebuild();
    });
    connect(m_search, &QLineEdit::textChanged, this, &IconPicker::rebuild);
    connect(m_grid, &IconGrid::picked, this, [this](const QString &icon) {
        emit picked(icon);
        close();
    });
    connect(random, &FlatButton::clicked, this, [this] {
        const QStringList glyphs = Icons::pageGlyphs();
        QStringList pool;
        if (m_emoji)
            for (const auto &g : Icons::emojiGroups())
                pool += g.names;
        else
            for (const QString &g : glyphs)
                pool.append(QStringLiteral("glyph:") + g);
        emit picked(pool[QRandomGenerator::global()->bounded(int(pool.size()))]);
        close();
    });
    rebuild();
    QTimer::singleShot(0, m_search, qOverload<>(&QWidget::setFocus));
}

void IconPicker::rebuild()
{
    const QString q = m_search->text().trimmed().toLower();
    QList<IconGrid::Section> sections;
    if (!m_emoji) {
        for (const auto &g : Icons::pageGlyphGroups()) {
            IconGrid::Section s{g.title, {}};
            for (const QString &n : g.names)
                if (q.isEmpty() || n.contains(q))
                    s.icons.append(QStringLiteral("glyph:") + n);
            sections.append(s);
        }
    } else {
        for (const auto &g : Icons::emojiGroups()) {
            if (!q.isEmpty() && !g.title.toLower().contains(q))
                continue;
            sections.append({g.title, g.names});
        }
    }
    m_grid->setSections(sections);
}

// ---- CoverGrid ---------------------------------------------------------------------

class CoverGrid : public QWidget
{
    Q_OBJECT
public:
    explicit CoverGrid(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        shuffle();
        setFixedSize(3 * 132 + 8, 4 * 80 + 8);
    }
    void shuffle()
    {
        m_specs.clear();
        for (const QString &scene : Cosmos::coverScenes())
            m_specs.append(Cosmos::coverSpec(scene, QRandomGenerator::global()->bounded(1, 100000)));
        update();
    }

signals:
    void picked(const QString &spec);

protected:
    QRectF cell(int i) const { return QRectF(4 + (i % 3) * 132, 4 + (i / 3) * 80, 126, 74); }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < m_specs.size(); ++i) {
            const QRectF r = cell(i);
            p.save();
            QPainterPath clip;
            clip.addRoundedRect(r, 7, 7);
            p.setClipPath(clip);
            Cosmos::paintCover(p, r, m_specs[i]);
            if (i != m_hover) {
                p.fillRect(r, QColor(0, 0, 0, 30));
            }
            p.restore();
            p.setPen(QPen(i == m_hover ? Theme::colors().mist : Theme::withAlpha(QColor(255, 255, 255), 0.07), i == m_hover ? 1.5 : 1));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
            if (i == m_hover) {
                p.setFont(Theme::uiFont(11, QFont::Medium));
                p.setPen(Theme::colors().starlight);
                p.drawText(r.adjusted(8, 0, -8, -6), Qt::AlignBottom | Qt::AlignLeft,
                           Cosmos::coverSceneName(m_specs[i].section(QLatin1Char(':'), 1, 1)));
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        int h = -1;
        for (int i = 0; i < m_specs.size(); ++i)
            if (cell(i).contains(e->position()))
                h = i;
        if (h != m_hover) {
            m_hover = h;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < m_specs.size(); ++i)
            if (cell(i).contains(e->position()))
                emit picked(m_specs[i]);
    }

private:
    QStringList m_specs;
    int m_hover = -1;
};

CoverPicker::CoverPicker(QWidget *parent)
    : Popover(parent)
{
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 10, 12, 12);
    v->setSpacing(8);
    auto *top = new QHBoxLayout;
    auto *title = new QLabel(tr("Cosmos"));
    title->setFont(Theme::uiFont(13, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
    top->addWidget(title);
    top->addStretch();
    auto *shuffle = new FlatButton(QStringLiteral("shuffle"), tr("Shuffle"));
    auto *upload = new FlatButton(QStringLiteral("upload"), tr("Upload…"));
    auto *remove = new FlatButton(QString(), tr("Remove"));
    top->addWidget(shuffle);
    top->addWidget(upload);
    top->addWidget(remove);
    v->addLayout(top);
    m_grid = new CoverGrid(content());
    v->addWidget(m_grid);
    connect(m_grid, &CoverGrid::picked, this, [this](const QString &spec) {
        emit picked(spec);
        close();
    });
    connect(shuffle, &FlatButton::clicked, m_grid, &CoverGrid::shuffle);
    connect(remove, &FlatButton::clicked, this, [this] {
        emit picked(QString());
        close();
    });
    connect(upload, &FlatButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(parentWidget(), tr("Choose a cover image"), QString(),
                                                          tr("Images (*.png *.jpg *.jpeg *.webp *.gif *.bmp)"));
        if (path.isEmpty() || !Workspace::instance())
            return;
        const QString media = Workspace::instance()->importMedia(path);
        if (!media.isEmpty())
            emit picked(QStringLiteral("media:") + media);
        close();
    });
}

// ---- PageList ----------------------------------------------------------------------

class PageList : public QWidget
{
    Q_OBJECT
public:
    struct Row {
        QString id;
        QString icon;
        QString title;
        QString path;
        bool database = false;
    };
    explicit PageList(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }
    void setRows(const QList<Row> &rows)
    {
        m_rows = rows;
        m_current = rows.isEmpty() ? -1 : 0;
        setFixedHeight(qMax(40, int(rows.size()) * 34 + 4));
        update();
    }
    void move(int d)
    {
        if (m_rows.isEmpty())
            return;
        m_current = (m_current + d + int(m_rows.size())) % int(m_rows.size());
        update();
        emit currentRowChanged(m_current * 34);
    }
    void activate()
    {
        if (m_current >= 0 && m_current < m_rows.size())
            emit picked(m_rows[m_current].id);
    }

signals:
    void picked(const QString &id);
    void currentRowChanged(int y);

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_rows.isEmpty()) {
            p.setPen(c.fog);
            p.setFont(Theme::uiFont(13));
            p.drawText(rect(), Qt::AlignCenter, tr("No pages found"));
            return;
        }
        for (int i = 0; i < m_rows.size(); ++i) {
            const QRectF r(2, 2 + i * 34, width() - 4, 32);
            if (i == m_current) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.graphite);
                p.drawRoundedRect(r, 6, 6);
            }
            Icons::paintPageIcon(p, QRectF(r.left() + 8, r.center().y() - 9, 18, 18), m_rows[i].icon, c.mist,
                                 m_rows[i].database ? QStringLiteral("database") : QStringLiteral("page"));
            p.setFont(Theme::uiFont(13.5, QFont::Medium));
            p.setPen(i == m_current ? c.starlight : c.moon);
            const QFontMetricsF fm(p.font());
            const double tw = qMin(fm.horizontalAdvance(m_rows[i].title), r.width() * 0.6);
            p.drawText(QRectF(r.left() + 34, r.top(), tw + 2, r.height()), Qt::AlignVCenter,
                       fm.elidedText(m_rows[i].title, Qt::ElideRight, tw));
            if (!m_rows[i].path.isEmpty()) {
                p.setFont(Theme::uiFont(12));
                p.setPen(c.dust);
                const double x = r.left() + 34 + tw + 10;
                p.drawText(QRectF(x, r.top(), r.right() - x - 8, r.height()), Qt::AlignVCenter,
                           QFontMetricsF(p.font()).elidedText(m_rows[i].path, Qt::ElideLeft, r.right() - x - 8));
            }
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        const int i = int((e->position().y() - 2) / 34);
        if (i >= 0 && i < m_rows.size() && i != m_current) {
            m_current = i;
            update();
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        const int i = int((e->position().y() - 2) / 34);
        if (i >= 0 && i < m_rows.size())
            emit picked(m_rows[i].id);
    }

private:
    QList<Row> m_rows;
    int m_current = -1;
};

PagePicker::PagePicker(QWidget *parent, const QString &title, const QString &exclude, bool includeTopLevel, bool includeDatabases)
    : Popover(parent)
    , m_exclude(exclude)
    , m_topLevel(includeTopLevel)
    , m_databases(includeDatabases)
{
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(8);
    auto *label = new QLabel(title);
    label->setFont(Theme::uiFont(12, QFont::Medium));
    label->setStyleSheet(QStringLiteral("color: #8a8a94; padding-left: 4px;"));
    v->addWidget(label);
    m_search = new SearchField(tr("Search pages…"));
    m_search->setMinimumWidth(380);
    v->addWidget(m_search);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameStyle(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedHeight(300);
    scroll->setObjectName(QStringLiteral("pagePickerScroll"));
    m_list = new PageList(scroll);
    scroll->setWidget(m_list);
    m_list->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    v->addWidget(scroll);
    connect(m_search, &QLineEdit::textChanged, this, &PagePicker::refresh);
    connect(m_list, &PageList::picked, this, [this](const QString &id) {
        emit picked(id == QLatin1String("@top") ? QString() : id);
        close();
    });
    connect(m_list, &PageList::currentRowChanged, this, [scroll](int y) { scroll->ensureVisible(0, y + 17, 0, 24); });
    // Arrow keys and Enter drive the list from the search field.
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::returnPressed, m_list, &PageList::activate);
    refresh();
    QTimer::singleShot(0, m_search, qOverload<>(&QWidget::setFocus));
}

bool PagePicker::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            m_list->move(key == Qt::Key_Down ? 1 : -1);
            return true;
        }
    }
    return Popover::eventFilter(watched, event);
}

void PagePicker::setDatabasesOnly(bool only)
{
    m_databasesOnly = only;
    m_databases = m_databases || only;
    m_topLevel = m_topLevel && !only;
    refresh();
}

void PagePicker::refresh()
{
    Workspace *ws = Workspace::instance();
    if (!ws)
        return;
    const QString q = m_search->text().trimmed();
    QList<PageList::Row> rows;
    if (m_topLevel && q.isEmpty())
        rows.append({QStringLiteral("@top"), QStringLiteral("glyph:home"), tr("Top level"), tr("No parent"), false});
    QList<const PageMeta *> pages = ws->allLive();
    QList<QPair<int, const PageMeta *>> scored;
    for (const PageMeta *m : pages) {
        if (m->isRow() || m->isTemplate() || (!m_databases && m->isDatabase()) || (m_databasesOnly && !m->isDatabase()))
            continue;
        if (!m_exclude.isEmpty() && ws->isInside(m->id, m_exclude))
            continue;
        const int s = q.isEmpty() ? int(m->visited / 1000 % 100000000) : Workspace::fuzzyScore(q, ws->title(m->id));
        if (s > 0 || q.isEmpty())
            scored.append({s, m});
    }
    std::sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (int i = 0; i < scored.size() && i < 60; ++i) {
        const PageMeta *m = scored[i].second;
        QStringList path;
        for (const QString &a : ws->ancestors(m->id))
            path.append(ws->title(a));
        rows.append({m->id, m->icon, ws->title(m->id), path.join(QStringLiteral(" / ")), m->isDatabase()});
    }
    m_list->setRows(rows);
    // Short lists don't leave a gap under them.
    if (auto *scroll = findChild<QScrollArea *>(QStringLiteral("pagePickerScroll"))) {
        scroll->setFixedHeight(qMin(300, m_list->height()));
        adjustSize();
    }
}

#include "Pickers.moc"
