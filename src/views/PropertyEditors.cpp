#include "PropertyEditors.h"

#include "FormulaEditor.h"
#include "Pickers.h"

#include "core/Ids.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/DatePopover.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QToolTip>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QString tr(const char *s)
{
    return QCoreApplication::translate("PropertyEditors", s);
}

QString statusGlyph(int group)
{
    return group == 2 ? QStringLiteral("check-circle") : group == 1 ? QStringLiteral("status") : QStringLiteral("select");
}

// ---- option editor -----------------------------------------------------------------

class OptionList : public QWidget
{
    Q_OBJECT
public:
    struct Row {
        enum Kind { Header, Option, Create } kind;
        QString text;   // header title / create name
        QString optionId;
        bool selected = false;
    };
    explicit OptionList(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
    }
    void setRows(const QList<Row> &rows, const Property &prop)
    {
        m_rows = rows;
        m_prop = prop;
        m_current = -1;
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].kind != Row::Header) {
                m_current = i;
                break;
            }
        int h = 4;
        for (const Row &r : m_rows)
            h += r.kind == Row::Header ? 26 : 32;
        setFixedHeight(qMax(36, h + 4));
        update();
    }
    void move(int d)
    {
        if (m_rows.isEmpty())
            return;
        int i = m_current;
        for (int n = 0; n < m_rows.size(); ++n) {
            i = (i + d + int(m_rows.size())) % int(m_rows.size());
            if (m_rows[i].kind != Row::Header)
                break;
        }
        m_current = i;
        update();
    }
    int current() const { return m_current; }
    const Row *row(int i) const { return i >= 0 && i < m_rows.size() ? &m_rows[i] : nullptr; }

signals:
    void activated(int index);
    void menuRequested(int index, const QPoint &globalPos);

protected:
    QRectF rowRect(int i) const
    {
        double y = 4;
        for (int k = 0; k < i; ++k)
            y += m_rows[k].kind == Row::Header ? 26 : 32;
        return QRectF(4, y, width() - 8, m_rows[i].kind == Row::Header ? 26 : 32);
    }
    QRectF menuRect(int i) const
    {
        const QRectF r = rowRect(i);
        return QRectF(r.right() - 30, r.top() + 4, 24, 24);
    }
    void paintEvent(QPaintEvent *) override
    {
        const auto &c = Theme::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_rows.isEmpty()) {
            p.setPen(c.fog);
            p.setFont(Theme::uiFont(12.5));
            p.drawText(rect(), Qt::AlignCenter, tr("Type to create an option"));
        }
        for (int i = 0; i < m_rows.size(); ++i) {
            const Row &row = m_rows[i];
            const QRectF r = rowRect(i);
            if (row.kind == Row::Header) {
                p.setPen(c.fog);
                p.setFont(Theme::uiFont(11.5, QFont::Medium));
                p.drawText(r.adjusted(8, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, row.text);
                continue;
            }
            if (i == m_current) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.graphite);
                p.drawRoundedRect(r, 6, 6);
            }
            if (row.kind == Row::Create) {
                p.setPen(c.haze);
                p.setFont(Theme::uiFont(13));
                p.drawText(QRectF(r.left() + 10, r.top(), 60, r.height()), Qt::AlignVCenter, tr("Create"));
                Theme::paintPill(p, QPointF(r.left() + 62, r.center().y() - 11), row.text, int(m_prop.options.size()) % 9);
                continue;
            }
            const Option *o = m_prop.option(row.optionId);
            if (!o)
                continue;
            double x = r.left() + 8;
            if (m_prop.type == PropType::Status) {
                Icons::paint(p, QRectF(x, r.center().y() - 7, 14, 14), statusGlyph(o->group), c.mist);
                x += 20;
            }
            Theme::paintPill(p, QPointF(x, r.center().y() - 11), o->name, o->shade);
            if (row.selected)
                Icons::paint(p, QRectF(r.right() - 54, r.center().y() - 8, 16, 16), QStringLiteral("check"), c.starlight);
            if (i == m_current)
                Icons::paint(p, menuRect(i).adjusted(4, 4, -4, -4), QStringLiteral("more"), c.haze);
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].kind != Row::Header && rowRect(i).contains(e->position()) && i != m_current) {
                m_current = i;
                update();
            }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].kind == Row::Header || !rowRect(i).contains(e->position()))
                continue;
            if (m_rows[i].kind == Row::Option && menuRect(i).contains(e->position()))
                emit menuRequested(i, mapToGlobal(menuRect(i).bottomLeft().toPoint()));
            else
                emit activated(i);
            return;
        }
    }

private:
    QList<Row> m_rows;
    Property m_prop;
    int m_current = -1;
};

class OptionEditor : public Popover
{
    Q_OBJECT
public:
    OptionEditor(QWidget *parent, const QString &dbId, const QString &rowId, const QString &propId, int minWidth)
        : Popover(parent)
        , m_db(dbId)
        , m_row(rowId)
        , m_propId(propId)
    {
        auto *v = new QVBoxLayout(content());
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(6);
        m_input = new QLineEdit;
        m_input->setPlaceholderText(tr("Search for an option…"));
        m_input->setMinimumWidth(qMax(260, minWidth));
        m_input->setFont(Theme::uiFont(13.5));
        v->addWidget(m_input);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameStyle(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMaximumHeight(320);
        m_list = new OptionList(scroll);
        scroll->setWidget(m_list);
        m_list->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        v->addWidget(scroll);
        m_input->installEventFilter(this);
        connect(m_input, &QLineEdit::textChanged, this, &OptionEditor::refresh);
        connect(m_list, &OptionList::activated, this, &OptionEditor::activate);
        connect(m_list, &OptionList::menuRequested, this, &OptionEditor::optionMenu);
        refresh();
        QTimer::singleShot(0, m_input, qOverload<>(&QWidget::setFocus));
    }

protected:
    bool eventFilter(QObject *w, QEvent *e) override
    {
        if (w == m_input && e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if (ke->key() == Qt::Key_Down || ke->key() == Qt::Key_Up) {
                m_list->move(ke->key() == Qt::Key_Down ? 1 : -1);
                return true;
            }
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                activate(m_list->current());
                return true;
            }
            if (ke->key() == Qt::Key_Backspace && m_input->text().isEmpty()) {
                // Remove the last chosen tag.
                Workspace *ws = Workspace::instance();
                QJsonValue v = ws->props(m_row).value(m_propId);
                if (v.isArray() && !v.toArray().isEmpty()) {
                    QJsonArray a = v.toArray();
                    a.removeLast();
                    ws->setProp(m_row, m_propId, a.isEmpty() ? QJsonValue() : QJsonValue(a));
                    refresh();
                } else if (v.isString()) {
                    ws->setProp(m_row, m_propId, QJsonValue());
                    refresh();
                }
                return true;
            }
        }
        return Popover::eventFilter(w, e);
    }

private:
    void refresh()
    {
        Workspace *ws = Workspace::instance();
        m_schema = ws->schema(m_db);
        const Property *prop = m_schema.prop(m_propId);
        if (!prop)
            return;
        const QJsonValue value = ws->props(m_row).value(m_propId);
        QSet<QString> selected;
        if (value.isArray())
            for (const QJsonValue &x : value.toArray())
                selected.insert(x.toString());
        else if (value.isString())
            selected.insert(value.toString());
        const QString q = m_input->text().trimmed();
        QList<OptionList::Row> rows;
        auto addOptions = [&](int group, bool filterGroup) {
            for (const Option &o : prop->options) {
                if (filterGroup && o.group != group)
                    continue;
                if (!q.isEmpty() && !o.name.contains(q, Qt::CaseInsensitive))
                    continue;
                rows.append({OptionList::Row::Option, QString(), o.id, selected.contains(o.id)});
            }
        };
        if (prop->type == PropType::Status) {
            const QStringList groups = {tr("To-do"), tr("In progress"), tr("Complete")};
            for (int g = 0; g < 3; ++g) {
                rows.append({OptionList::Row::Header, groups[g], QString(), false});
                addOptions(g, true);
            }
        } else {
            rows.append({OptionList::Row::Header, prop->type == PropType::MultiSelect ? tr("Select options") : tr("Select an option"),
                         QString(), false});
            addOptions(0, false);
        }
        if (!q.isEmpty() && !prop->optionByName(q))
            rows.append({OptionList::Row::Create, q, QString(), false});
        m_list->setRows(rows, *prop);
        adjustSize();
    }

    void activate(int index)
    {
        const OptionList::Row *row = m_list->row(index);
        if (!row)
            return;
        Workspace *ws = Workspace::instance();
        m_schema = ws->schema(m_db);
        Property *prop = m_schema.prop(m_propId);
        if (!prop)
            return;
        QString optionId = row->optionId;
        if (row->kind == OptionList::Row::Create) {
            parseValue(*prop, row->text, true);
            optionId = prop->optionByName(row->text)->id;
            ws->setSchema(m_db, m_schema);
        }
        if (prop->type == PropType::MultiSelect) {
            QJsonArray a = ws->props(m_row).value(m_propId).toArray();
            bool had = false;
            for (int i = 0; i < a.size(); ++i)
                if (a[i].toString() == optionId) {
                    a.removeAt(i);
                    had = true;
                    break;
                }
            if (!had)
                a.append(optionId);
            ws->setProp(m_row, m_propId, a.isEmpty() ? QJsonValue() : QJsonValue(a));
            m_input->clear();
            refresh();
        } else {
            const bool same = ws->props(m_row).value(m_propId).toString() == optionId;
            ws->setProp(m_row, m_propId, same && row->kind != OptionList::Row::Create ? QJsonValue() : QJsonValue(optionId));
            close();
        }
    }

    void optionMenu(int index, const QPoint &global)
    {
        const OptionList::Row *row = m_list->row(index);
        if (!row)
            return;
        const QString optionId = row->optionId;
        QMenu *menu = Theme::makeMenu(this);
        QAction *rename = menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Rename"));
        connect(rename, &QAction::triggered, this, [this, optionId] {
            Workspace *ws = Workspace::instance();
            m_schema = ws->schema(m_db);
            Property *prop = m_schema.prop(m_propId);
            for (Option &o : prop->options)
                if (o.id == optionId) {
                    QString name = o.name;
                    if (textDialog(window(), tr("Rename option"), QString(), &name) && !name.trimmed().isEmpty()) {
                        o.name = name.trimmed();
                        ws->setSchema(m_db, m_schema);
                    }
                }
            refresh();
        });
        Workspace *ws = Workspace::instance();
        const Property *prop = ws->schema(m_db).prop(m_propId);
        if (prop && prop->type == PropType::Status) {
            Theme::addMenuSection(menu, tr("Group"));
            const QStringList groups = {tr("To-do"), tr("In progress"), tr("Complete")};
            for (int g = 0; g < 3; ++g) {
                QAction *a = menu->addAction(Icons::icon(statusGlyph(g)), groups[g]);
                connect(a, &QAction::triggered, this, [this, optionId, g] {
                    Workspace *ws = Workspace::instance();
                    m_schema = ws->schema(m_db);
                    for (Option &o : m_schema.prop(m_propId)->options)
                        if (o.id == optionId)
                            o.group = g;
                    ws->setSchema(m_db, m_schema);
                    refresh();
                });
            }
        }
        Theme::addMenuSection(menu, tr("Shade"));
        for (int s = 0; s < kOptionShadeCount; ++s) {
            QPixmap swatch(28, 16);
            swatch.fill(Qt::transparent);
            QPainter sp(&swatch);
            Theme::paintPill(sp, QPointF(1, 1), QStringLiteral("Aa"), s, 14);
            sp.end();
            QAction *a = menu->addAction(QIcon(swatch), optionShadeName(s));
            connect(a, &QAction::triggered, this, [this, optionId, s] {
                Workspace *ws = Workspace::instance();
                m_schema = ws->schema(m_db);
                for (Option &o : m_schema.prop(m_propId)->options)
                    if (o.id == optionId)
                        o.shade = s;
                ws->setSchema(m_db, m_schema);
                refresh();
            });
        }
        menu->addSeparator();
        QAction *del = menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete option"));
        connect(del, &QAction::triggered, this, [this, optionId] {
            Workspace *ws = Workspace::instance();
            m_schema = ws->schema(m_db);
            Property *p = m_schema.prop(m_propId);
            p->options.removeIf([&](const Option &o) { return o.id == optionId; });
            ws->setSchema(m_db, m_schema);
            refresh();
        });
        menu->popup(global);
    }

    QString m_db, m_row, m_propId;
    Schema m_schema;
    QLineEdit *m_input;
    OptionList *m_list;
};

// ---- text editor -------------------------------------------------------------------

class TextEditor : public Popover
{
    Q_OBJECT
public:
    TextEditor(QWidget *parent, const QString &dbId, const QString &rowId, const QString &propId, int minWidth)
        : Popover(parent)
        , m_db(dbId)
        , m_row(rowId)
        , m_propId(propId)
    {
        Workspace *ws = Workspace::instance();
        const Schema schema = ws->schema(dbId);
        const Property *prop = schema.prop(propId);
        m_type = prop ? prop->type : PropType::Text;
        auto *v = new QVBoxLayout(content());
        v->setContentsMargins(8, 8, 8, 8);
        const QJsonValue value = m_type == PropType::Title ? QJsonValue(ws->page(rowId) ? ws->page(rowId)->title : QString())
                                                           : ws->props(rowId).value(propId);
        QString text = m_type == PropType::Number && value.isDouble() ? formatNumber(value.toDouble(), QStringLiteral("number"))
                                                                        : value.toString();
        if (m_type == PropType::Text) {
            m_multi = new QPlainTextEdit(text);
            m_multi->setFont(Theme::uiFont(13.5));
            m_multi->setMinimumSize(qMax(300, minWidth), 90);
            m_multi->setStyleSheet(QStringLiteral("QPlainTextEdit { background: #23232a; color: #c9c9d0; border: 1px solid #2e2e35; "
                                                  "border-radius: 7px; padding: 4px; }"));
            m_multi->installEventFilter(this);
            v->addWidget(m_multi);
            auto *hint = new QLabel(tr("Enter to save · Shift+Enter for a new line"));
            hint->setFont(Theme::uiFont(11));
            hint->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 2px 4px;"));
            v->addWidget(hint);
            QTimer::singleShot(0, m_multi, [this] {
                m_multi->setFocus();
                m_multi->moveCursor(QTextCursor::End);
            });
        } else {
            m_line = new QLineEdit(text);
            m_line->setFont(Theme::uiFont(13.5));
            m_line->setMinimumWidth(qMax(260, minWidth));
            if (m_type == PropType::Url)
                m_line->setPlaceholderText(QStringLiteral("https://"));
            else if (m_type == PropType::Email)
                m_line->setPlaceholderText(QStringLiteral("name@example.com"));
            else if (m_type == PropType::Number)
                m_line->setPlaceholderText(QStringLiteral("0"));
            v->addWidget(m_line);
            connect(m_line, &QLineEdit::returnPressed, this, &TextEditor::close);
            if ((m_type == PropType::Url || m_type == PropType::Email) && !text.isEmpty()) {
                auto *open = new FlatButton(QStringLiteral("external"), m_type == PropType::Url ? tr("Open link") : tr("Write email"));
                v->addWidget(open, 0, Qt::AlignLeft);
                connect(open, &FlatButton::clicked, this, [this] {
                    const QString t = m_line->text().trimmed();
                    QDesktopServices::openUrl(QUrl(m_type == PropType::Email ? QStringLiteral("mailto:") + t
                                                                             : (t.contains(QLatin1String("://")) ? t : QStringLiteral("https://") + t)));
                });
            }
            QTimer::singleShot(0, m_line, [this] {
                m_line->setFocus();
                m_line->selectAll();
            });
        }
        connect(this, &Popover::closed, this, &TextEditor::commit);
    }

protected:
    bool eventFilter(QObject *w, QEvent *e) override
    {
        if (w == m_multi && e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
                close();
                return true;
            }
        }
        return Popover::eventFilter(w, e);
    }

private:
    void commit()
    {
        if (m_done)
            return;
        m_done = true;
        Workspace *ws = Workspace::instance();
        if (!ws->exists(m_row))
            return;
        const QString text = m_multi ? m_multi->toPlainText() : m_line->text();
        if (m_type == PropType::Title) {
            ws->setTitle(m_row, text.simplified());
            return;
        }
        Schema schema = ws->schema(m_db);
        Property *prop = schema.prop(m_propId);
        if (!prop)
            return;
        const QJsonValue v = parseValue(*prop, text, false);
        if (v != ws->props(m_row).value(m_propId))
            ws->setProp(m_row, m_propId, v);
    }
    QString m_db, m_row, m_propId;
    PropType m_type;
    QLineEdit *m_line = nullptr;
    QPlainTextEdit *m_multi = nullptr;
    bool m_done = false;
};

// ---- relation picker ------------------------------------------------------------------

class RelationEditor : public Popover
{
    Q_OBJECT
public:
    RelationEditor(QWidget *parent, const QString &dbId, const QString &rowId, const QString &propId)
        : Popover(parent)
        , m_db(dbId)
        , m_row(rowId)
        , m_propId(propId)
    {
        auto *v = new QVBoxLayout(content());
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(6);
        m_title = new QLabel;
        m_title->setFont(Theme::uiFont(12, QFont::Medium));
        m_title->setStyleSheet(QStringLiteral("color: #8a8a94; padding-left: 4px;"));
        v->addWidget(m_title);
        m_search = new QLineEdit;
        m_search->setPlaceholderText(tr("Search for a page…"));
        m_search->setMinimumWidth(320);
        m_search->setFont(Theme::uiFont(13.5));
        v->addWidget(m_search);
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameStyle(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFixedHeight(280);
        m_list = new QWidget;
        m_listLayout = new QVBoxLayout(m_list);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(1);
        scroll->setWidget(m_list);
        m_list->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        v->addWidget(scroll);
        connect(m_search, &QLineEdit::textChanged, this, &RelationEditor::refresh);
        connect(m_search, &QLineEdit::returnPressed, this, [this] {
            if (!m_firstId.isEmpty())
                toggle(m_firstId);
        });
        refresh();
        QTimer::singleShot(0, m_search, qOverload<>(&QWidget::setFocus));
    }

private:
    void toggle(const QString &id)
    {
        Workspace *ws = Workspace::instance();
        const Property *prop = ws->lookupSchema(m_db) ? ws->lookupSchema(m_db)->prop(m_propId) : nullptr;
        if (!prop)
            return;
        QStringList ids = relationIds(ws->props(m_row).value(m_propId));
        if (ids.contains(id))
            ids.removeAll(id);
        else if (prop->relationOne)
            ids = {id};
        else
            ids.append(id);
        ws->setProp(m_row, m_propId, ids.isEmpty() ? QJsonValue() : QJsonValue(QJsonArray::fromStringList(ids)));
        if (prop->relationOne && !ids.isEmpty()) {
            close();
            return;
        }
        refresh();
    }
    void refresh()
    {
        while (QLayoutItem *item = m_listLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        Workspace *ws = Workspace::instance();
        const Schema *schema = ws->lookupSchema(m_db);
        const Property *prop = schema ? schema->prop(m_propId) : nullptr;
        if (!prop)
            return;
        const QString target = prop->relationDb;
        m_title->setText(ws->exists(target) ? tr("Link to pages in %1").arg(ws->title(target)) : tr("Choose a database in the property menu"));
        const QStringList selected = relationIds(ws->props(m_row).value(m_propId));
        const QString q = m_search->text().trimmed();
        m_firstId.clear();
        auto addRow = [&](const QString &id, bool checked) {
            auto *b = new FlatButton(QString(), ws->title(id));
            b->setIconName(checked ? QStringLiteral("check") : QStringLiteral("page"));
            b->setTextSize(13.5);
            b->setMinimumHeight(30);
            b->setMuted(!checked);
            connect(b, &FlatButton::clicked, this, [this, id] { toggle(id); });
            m_listLayout->addWidget(b);
            if (m_firstId.isEmpty())
                m_firstId = id;
        };
        // Linked pages first, then the rest.
        for (const QString &id : selected)
            if (ws->exists(id) && (q.isEmpty() || ws->title(id).contains(q, Qt::CaseInsensitive)))
                addRow(id, true);
        int shown = 0;
        for (const PageMeta *m : ws->rows(target)) {
            if (selected.contains(m->id) || (target == m_db && m->id == m_row))
                continue;
            if (!q.isEmpty() && !ws->title(m->id).contains(q, Qt::CaseInsensitive))
                continue;
            addRow(m->id, false);
            if (++shown > 80)
                break;
        }
        if (!q.isEmpty() && ws->exists(target)) {
            auto *create = new FlatButton(QStringLiteral("plus"), tr("New page “%1”").arg(q));
            create->setTextSize(13.5);
            create->setMinimumHeight(30);
            connect(create, &FlatButton::clicked, this, [this, target, q] {
                const QString id = Workspace::instance()->createRow(target, q);
                m_search->clear();
                toggle(id);
            });
            m_listLayout->addWidget(create);
        }
        m_listLayout->addStretch();
    }
    QString m_db, m_row, m_propId, m_firstId;
    QLabel *m_title;
    QLineEdit *m_search;
    QWidget *m_list;
    QVBoxLayout *m_listLayout;
};

// ---- people -----------------------------------------------------------------------------

class PersonEditor : public Popover
{
    Q_OBJECT
public:
    PersonEditor(QWidget *parent, const QString &rowId, const QString &propId)
        : Popover(parent)
        , m_row(rowId)
        , m_propId(propId)
    {
        auto *v = new QVBoxLayout(content());
        v->setContentsMargins(8, 8, 8, 8);
        v->setSpacing(6);
        m_input = new QLineEdit;
        m_input->setPlaceholderText(tr("Search or add a person…"));
        m_input->setMinimumWidth(280);
        m_input->setFont(Theme::uiFont(13.5));
        v->addWidget(m_input);
        m_list = new QWidget;
        m_listLayout = new QVBoxLayout(m_list);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(1);
        v->addWidget(m_list);
        connect(m_input, &QLineEdit::textChanged, this, &PersonEditor::refresh);
        connect(m_input, &QLineEdit::returnPressed, this, [this] {
            const QString name = m_input->text().trimmed();
            if (name.isEmpty())
                return;
            Workspace *ws = Workspace::instance();
            for (const Workspace::Person &p : ws->people())
                if (p.name.compare(name, Qt::CaseInsensitive) == 0) {
                    toggle(p.id);
                    return;
                }
            QList<Workspace::Person> people = ws->people();
            const Workspace::Person added{Ids::newShortId(8), name};
            people.append(added);
            ws->setPeople(people);
            m_input->clear();
            toggle(added.id);
        });
        refresh();
        QTimer::singleShot(0, m_input, qOverload<>(&QWidget::setFocus));
    }

private:
    void toggle(const QString &id)
    {
        Workspace *ws = Workspace::instance();
        QStringList ids;
        for (const QJsonValue &v : ws->props(m_row).value(m_propId).toArray())
            ids.append(v.toString());
        if (ids.contains(id))
            ids.removeAll(id);
        else
            ids.append(id);
        ws->setProp(m_row, m_propId, ids.isEmpty() ? QJsonValue() : QJsonValue(QJsonArray::fromStringList(ids)));
        refresh();
    }
    void refresh()
    {
        while (QLayoutItem *item = m_listLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        Workspace *ws = Workspace::instance();
        QStringList selected;
        for (const QJsonValue &v : ws->props(m_row).value(m_propId).toArray())
            selected.append(v.toString());
        const QString q = m_input->text().trimmed();
        for (const Workspace::Person &p : ws->people()) {
            if (!q.isEmpty() && !p.name.contains(q, Qt::CaseInsensitive))
                continue;
            auto *b = new FlatButton(selected.contains(p.id) ? QStringLiteral("check") : QStringLiteral("person"), p.name);
            b->setTextSize(13.5);
            b->setMinimumHeight(30);
            b->setMuted(!selected.contains(p.id));
            const QString id = p.id;
            connect(b, &FlatButton::clicked, this, [this, id] { toggle(id); });
            m_listLayout->addWidget(b);
        }
        if (!q.isEmpty()) {
            auto *hint = new QLabel(tr("Enter to add “%1”").arg(q));
            hint->setFont(Theme::uiFont(12));
            hint->setStyleSheet(QStringLiteral("color: #7a7a86; padding: 4px 6px;"));
            m_listLayout->addWidget(hint);
        }
        adjustSize();
    }
    QString m_row, m_propId;
    QLineEdit *m_input;
    QWidget *m_list;
    QVBoxLayout *m_listLayout;
};

// ---- files & media -----------------------------------------------------------------------

class FilesEditor : public Popover
{
    Q_OBJECT
public:
    FilesEditor(QWidget *parent, const QString &rowId, const QString &propId)
        : Popover(parent)
        , m_row(rowId)
        , m_propId(propId)
    {
        auto *v = new QVBoxLayout(content());
        v->setContentsMargins(10, 10, 10, 10);
        v->setSpacing(6);
        m_list = new QWidget;
        m_listLayout = new QVBoxLayout(m_list);
        m_listLayout->setContentsMargins(0, 0, 0, 0);
        m_listLayout->setSpacing(1);
        v->addWidget(m_list);
        auto *row = new QHBoxLayout;
        auto *upload = new FlatButton(QStringLiteral("upload"), tr("Upload"));
        upload->setFilled(true);
        row->addWidget(upload);
        m_link = new QLineEdit;
        m_link->setPlaceholderText(tr("…or paste a link"));
        m_link->setFont(Theme::uiFont(13));
        m_link->setMinimumWidth(220);
        row->addWidget(m_link);
        v->addLayout(row);
        connect(upload, &FlatButton::clicked, this, [this] {
            const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Upload files"));
            Workspace *ws = Workspace::instance();
            QJsonArray files = ws->props(m_row).value(m_propId).toArray();
            for (const QString &path : paths) {
                const QString media = ws->importMedia(path);
                if (!media.isEmpty())
                    files.append(QJsonObject{{QStringLiteral("n"), QFileInfo(path).fileName()}, {QStringLiteral("m"), media}});
            }
            ws->setProp(m_row, m_propId, files.isEmpty() ? QJsonValue() : QJsonValue(files));
            refresh();
        });
        connect(m_link, &QLineEdit::returnPressed, this, [this] {
            QString url = m_link->text().trimmed();
            if (url.isEmpty())
                return;
            if (!url.contains(QStringLiteral("://")))
                url.prepend(QStringLiteral("https://"));
            Workspace *ws = Workspace::instance();
            QJsonArray files = ws->props(m_row).value(m_propId).toArray();
            files.append(QJsonObject{{QStringLiteral("n"), url.section(QLatin1Char('/'), -1).isEmpty() ? url : url.section(QLatin1Char('/'), -1)},
                                     {QStringLiteral("u"), url}});
            ws->setProp(m_row, m_propId, files);
            m_link->clear();
            refresh();
        });
        refresh();
    }

private:
    void refresh()
    {
        while (QLayoutItem *item = m_listLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
        Workspace *ws = Workspace::instance();
        const QList<FileRef> files = filesOf(ws->props(m_row).value(m_propId));
        if (files.isEmpty()) {
            auto *empty = new QLabel(tr("No files yet"));
            empty->setFont(Theme::uiFont(12.5));
            empty->setStyleSheet(QStringLiteral("color: #7a7a86; padding: 4px;"));
            m_listLayout->addWidget(empty);
        }
        for (int i = 0; i < files.size(); ++i) {
            auto *line = new QWidget;
            auto *h = new QHBoxLayout(line);
            h->setContentsMargins(0, 0, 0, 0);
            auto *open = new FlatButton(QStringLiteral("paperclip"), files[i].name);
            open->setTextSize(13.5);
            const FileRef f = files[i];
            connect(open, &FlatButton::clicked, this, [f] {
                if (!f.media.isEmpty())
                    QDesktopServices::openUrl(QUrl::fromLocalFile(Workspace::instance()->mediaPath(f.media)));
                else
                    QDesktopServices::openUrl(QUrl(f.url));
            });
            auto *remove = new FlatButton(QStringLiteral("close"));
            connect(remove, &FlatButton::clicked, this, [this, i] {
                Workspace *ws = Workspace::instance();
                QJsonArray a = ws->props(m_row).value(m_propId).toArray();
                if (i < a.size())
                    a.removeAt(i);
                ws->setProp(m_row, m_propId, a.isEmpty() ? QJsonValue() : QJsonValue(a));
                refresh();
            });
            h->addWidget(open, 1);
            h->addWidget(remove);
            m_listLayout->addWidget(line);
        }
        adjustSize();
    }
    QString m_row, m_propId;
    QWidget *m_list;
    QVBoxLayout *m_listLayout;
    QLineEdit *m_link;
};

// ---- property buttons -------------------------------------------------------------------

class ButtonPropertyPopover : public Popover
{
    Q_OBJECT
public:
    ButtonPropertyPopover(QWidget *parent, const QString &dbId, const QString &propId)
        : Popover(parent)
        , m_db(dbId)
        , m_prop(propId)
    {
        Workspace *ws = Workspace::instance();
        const Schema s = ws->schema(dbId);
        const Property *p = s.prop(propId);
        const QJsonObject button = p ? p->button : QJsonObject();
        const QJsonObject action = button.value(QStringLiteral("actions")).toArray().at(0).toObject();
        auto *v = new QVBoxLayout(content());
        v->setContentsMargins(12, 12, 12, 12);
        v->setSpacing(6);
        auto label = [&](const QString &t) {
            auto *l = new QLabel(t);
            l->setFont(Theme::uiFont(12, QFont::DemiBold));
            l->setStyleSheet(QStringLiteral("color: #9a9aa6;"));
            v->addWidget(l);
        };
        label(tr("Button label"));
        m_label = new QLineEdit(button.value(QStringLiteral("label")).toString());
        m_label->setPlaceholderText(tr("Click me"));
        m_label->setMinimumWidth(340);
        v->addWidget(m_label);
        label(tr("When clicked"));
        m_type = new QComboBox;
        m_type->addItem(tr("Edit a property of this page"), QStringLiteral("set"));
        m_type->addItem(tr("Open a link"), QStringLiteral("url"));
        m_type->addItem(tr("Open a page"), QStringLiteral("page"));
        m_type->addItem(tr("Add a page to a database"), QStringLiteral("add"));
        m_type->setCurrentIndex(qMax(0, m_type->findData(action.value(QStringLiteral("type")).toString(QStringLiteral("set")))));
        v->addWidget(m_type);
        m_target = new QComboBox;
        m_value = new QLineEdit;
        v->addWidget(m_target);
        v->addWidget(m_value);
        auto *done = new FlatButton(QStringLiteral("check"), tr("Done"));
        done->setFilled(true);
        v->addWidget(done, 0, Qt::AlignRight);
        auto fill = [this, action] {
            Workspace *ws = Workspace::instance();
            const QString t = m_type->currentData().toString();
            m_target->clear();
            m_target->setVisible(t != QLatin1String("url"));
            if (t == QLatin1String("set")) {
                for (const Property &x : ws->schema(m_db).props)
                    if (!propTypeIsComputed(x.type) && x.type != PropType::Title && x.type != PropType::Relation && x.type != PropType::Files)
                        m_target->addItem(Icons::icon(propTypeIcon(x.type)), x.name, x.id);
                m_value->setPlaceholderText(tr("Value — e.g. Done, today, now, toggle"));
            } else if (t == QLatin1String("page") || t == QLatin1String("add")) {
                for (const PageMeta *m : ws->allLive())
                    if ((t == QLatin1String("add")) == m->isDatabase() && !m->isRow())
                        m_target->addItem(ws->title(m->id), m->id);
                m_value->setPlaceholderText(t == QLatin1String("add") ? tr("Title of the new page") : QString());
            } else {
                m_value->setPlaceholderText(QStringLiteral("https://"));
            }
            m_value->setVisible(t != QLatin1String("page"));
            const int idx = m_target->findData(action.value(QStringLiteral("target")).toString());
            if (idx >= 0)
                m_target->setCurrentIndex(idx);
            if (m_value->text().isEmpty())
                m_value->setText(action.value(QStringLiteral("value")).toString());
            adjustSize();
        };
        connect(m_type, &QComboBox::currentIndexChanged, this, fill);
        fill();
        connect(done, &FlatButton::clicked, this, [this] {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(m_db);
            Property *p = s.prop(m_prop);
            if (!p)
                return;
            QJsonObject a{{QStringLiteral("type"), m_type->currentData().toString()},
                          {QStringLiteral("target"), m_target->currentData().toString()},
                          {QStringLiteral("value"), m_value->text().trimmed()}};
            p->button = QJsonObject{{QStringLiteral("label"), m_label->text().trimmed()}, {QStringLiteral("actions"), QJsonArray{a}}};
            ws->setSchema(m_db, s);
            close();
        });
    }

private:
    QString m_db, m_prop;
    QLineEdit *m_label;
    QComboBox *m_type;
    QComboBox *m_target;
    QLineEdit *m_value;
};

void runPropertyButton(QWidget *parent, const QString &dbId, const QString &rowId, const QString &propId)
{
    Workspace *ws = Workspace::instance();
    const Schema s = ws->schema(dbId);
    const Property *p = s.prop(propId);
    if (!p)
        return;
    const QJsonArray actions = p->button.value(QStringLiteral("actions")).toArray();
    if (actions.isEmpty()) {
        (new ButtonPropertyPopover(parent->window(), dbId, propId))->popupAt(QCursor::pos());
        return;
    }
    for (const QJsonValue &v : actions) {
        const QJsonObject a = v.toObject();
        const QString type = a.value(QStringLiteral("type")).toString();
        const QString target = a.value(QStringLiteral("target")).toString();
        const QString value = a.value(QStringLiteral("value")).toString();
        if (type == QLatin1String("set")) {
            Schema live = ws->schema(dbId);
            Property *tp = live.prop(target);
            if (!tp)
                continue;
            QJsonValue nv;
            const QString lower = value.trimmed().toLower();
            if (tp->type == PropType::Checkbox && (lower == QLatin1String("toggle") || lower.isEmpty()))
                nv = !ws->props(rowId).value(target).toBool();
            else if (tp->type == PropType::Date && (lower == QLatin1String("now")))
                nv = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
            else if (tp->type == PropType::Date && (lower == QLatin1String("today")))
                nv = QDate::currentDate().toString(Qt::ISODate);
            else
                nv = parseValue(*tp, value, true);
            ws->batch([&] {
                ws->setSchema(dbId, live);
                ws->setProp(rowId, target, nv);
            });
        } else if (type == QLatin1String("url")) {
            QString url = value;
            if (!url.contains(QStringLiteral("://")))
                url.prepend(QStringLiteral("https://"));
            QDesktopServices::openUrl(QUrl(url));
        } else if (type == QLatin1String("add") && ws->exists(target)) {
            ws->createRow(target, value);
        } else if (type == QLatin1String("page") && ws->exists(target)) {
            if (auto *w = parent->window())
                QMetaObject::invokeMethod(w, "navigateTo", Qt::QueuedConnection, Q_ARG(QString, target));
        }
    }
}
}

namespace PropertyEditors
{

void edit(QWidget *parent, const QPoint &globalPos, const QString &databaseId, const QString &rowId, const QString &propId, int minWidth)
{
    Workspace *ws = Workspace::instance();
    if (!ws || !ws->exists(rowId))
        return;
    const Schema schema = ws->schema(databaseId);
    const Property *prop = schema.prop(propId);
    if (!prop)
        return;
    QWidget *host = parent->window();
    if (prop->type == PropType::Button) {
        runPropertyButton(parent, databaseId, rowId, propId);
        return;
    }
    if (prop->type == PropType::Formula) {
        // Formulas show their value; a click explains them.
        const QString err = formulaError(*prop, RowRef{ws->page(rowId), ws->props(rowId)});
        if (!err.isEmpty())
            QToolTip::showText(globalPos, err);
        return;
    }
    if (propTypeIsComputed(prop->type))
        return;
    switch (prop->type) {
    case PropType::Relation:
        if (!ws->exists(prop->relationDb)) {
            QToolTip::showText(globalPos, tr("Choose the related database in the property menu first."));
            return;
        }
        (new RelationEditor(host, databaseId, rowId, propId))->popupAt(globalPos);
        return;
    case PropType::Person:
        (new PersonEditor(host, rowId, propId))->popupAt(globalPos);
        return;
    case PropType::Files:
        (new FilesEditor(host, rowId, propId))->popupAt(globalPos);
        return;
    case PropType::Checkbox:
        ws->setProp(rowId, propId, !ws->props(rowId).value(propId).toBool());
        return;
    case PropType::Select:
    case PropType::MultiSelect:
    case PropType::Status:
        (new OptionEditor(host, databaseId, rowId, propId, minWidth))->popupAt(globalPos);
        return;
    case PropType::Date:
        {
            auto *dp = new DatePopover(host, Workspace::instance()->props(rowId).value(propId).toString());
            QObject::connect(dp, &DatePopover::changed, dp, [rowId, propId](const QString &value) {
                Workspace::instance()->setProp(rowId, propId, value.isEmpty() ? QJsonValue() : QJsonValue(value));
            });
            dp->popupAt(globalPos);
        }
        return;
    default:
        (new TextEditor(host, databaseId, rowId, propId, minWidth))->popupAt(globalPos);
        return;
    }
}

double valueWidth(const Property &prop, const QJsonValue &value)
{
    switch (prop.type) {
    case PropType::Select:
    case PropType::Status: {
        const Option *o = prop.option(value.toString());
        return o ? Theme::pillWidth(o->name) + (prop.type == PropType::Status ? 18 : 0) : 0;
    }
    case PropType::MultiSelect: {
        double w = 0;
        for (const QJsonValue &x : value.toArray())
            if (const Option *o = prop.option(x.toString()))
                w += Theme::pillWidth(o->name) + 5;
        return w;
    }
    case PropType::Checkbox:
        return 18;
    case PropType::Number:
        if (prop.numberShow != QLatin1String("number"))
            return QFontMetricsF(Theme::uiFont(13.5)).horizontalAdvance(displayValue(prop, value)) + 90;
        return QFontMetricsF(Theme::uiFont(13.5)).horizontalAdvance(displayValue(prop, value));
    case PropType::Formula:
        if (value.isBool())
            return 18;
        return QFontMetricsF(Theme::uiFont(13.5)).horizontalAdvance(displayValue(prop, value));
    case PropType::Button:
        return QFontMetricsF(Theme::uiFont(12.5, QFont::Medium)).horizontalAdvance(displayValue(prop, value).isEmpty() ? tr("Button") : displayValue(prop, value)) + 34;
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy:
        return QFontMetricsF(Theme::uiFont(13.5)).horizontalAdvance(displayValue(prop, value)) + 26 * qMax<qsizetype>(1, value.toArray().size());
    case PropType::Relation:
    case PropType::Files:
        return QFontMetricsF(Theme::uiFont(13.5)).horizontalAdvance(displayValue(prop, value)) + 22 * qMax<qsizetype>(1, value.toArray().size());
    default:
        return QFontMetricsF(Theme::uiFont(13.5)).horizontalAdvance(displayValue(prop, value));
    }
}

namespace
{
void paintCheck(QPainter &p, const QRectF &box, bool on)
{
    const auto &c = Theme::colors();
    if (on) {
        p.setPen(Qt::NoPen);
        p.setBrush(c.starlight);
        p.drawRoundedRect(box, 4, 4);
        QPainterPath tick;
        tick.moveTo(box.left() + 4.2, box.top() + 8.5);
        tick.lineTo(box.left() + 6.9, box.top() + 11.2);
        tick.lineTo(box.left() + 12, box.top() + 5.3);
        p.setPen(QPen(c.basalt, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(tick);
    } else {
        p.setPen(QPen(c.dust, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(box.adjusted(0.7, 0.7, -0.7, -0.7), 4, 4);
    }
}

// A number drawn as a bar or a ring (Notion's "Show as").
void paintNumberVisual(QPainter &p, const QRectF &rect, const Property &prop, double value, const QString &text)
{
    const auto &c = Theme::colors();
    const double frac = prop.numberDivide > 0 ? qBound(0.0, value / prop.numberDivide, 1.0) : 0;
    const double cy = rect.center().y();
    p.setFont(Theme::uiFont(13));
    const double tw = QFontMetricsF(p.font()).horizontalAdvance(text) + 8;
    if (prop.numberShow == QLatin1String("ring")) {
        const QRectF ring(rect.left() + 1, cy - 8, 16, 16);
        p.setPen(QPen(c.ash, 2.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
        p.setPen(QPen(c.starlight, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, 90 * 16, -int(frac * 360 * 16));
        p.setPen(c.moon);
        p.drawText(QRectF(ring.right() + 8, rect.top(), rect.width() - 24, rect.height()), Qt::AlignVCenter, text);
        return;
    }
    const double barW = qMax(30.0, rect.width() - tw - 6);
    const QRectF track(rect.left(), cy - 3, barW, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(c.graphite);
    p.drawRoundedRect(track, 3, 3);
    p.setBrush(c.mist);
    p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * frac, track.height()), 3, 3);
    p.setPen(c.moon);
    p.drawText(QRectF(track.right() + 8, rect.top(), tw, rect.height()), Qt::AlignVCenter, text);
}

void paintPeople(QPainter &p, const QRectF &rect, const QJsonValue &value)
{
    const auto &c = Theme::colors();
    Workspace *ws = Workspace::instance();
    double x = rect.left();
    const double cy = rect.center().y();
    for (const QJsonValue &v : value.toArray()) {
        const QString name = ws ? ws->personName(v.toString()) : v.toString();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x3A, 0x3A, 0x44));
        p.drawEllipse(QRectF(x, cy - 9, 18, 18));
        p.setPen(c.starlight);
        p.setFont(Theme::uiFont(10, QFont::DemiBold));
        p.drawText(QRectF(x, cy - 9, 18, 18), Qt::AlignCenter, name.left(1).toUpper());
        x += 23;
        p.setFont(Theme::uiFont(13.5));
        p.setPen(c.moon);
        const double w = QFontMetricsF(p.font()).horizontalAdvance(name);
        p.drawText(QRectF(x, rect.top(), qMin(w + 2, rect.right() - x), rect.height()), Qt::AlignVCenter, name);
        x += w + 12;
        if (x > rect.right())
            break;
    }
}
} // namespace

void paintValue(QPainter &p, const QRectF &rect, const Property &prop, const QJsonValue &value, bool dim, Qt::Alignment align)
{
    const auto &c = Theme::colors();
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setClipRect(rect.adjusted(-2, -2, 2, 2));
    const double cy = rect.center().y();
    switch (prop.type) {
    case PropType::Select:
    case PropType::Status: {
        const Option *o = prop.option(value.toString());
        if (!o)
            break;
        double x = rect.left();
        if (prop.type == PropType::Status) {
            Icons::paint(p, QRectF(x, cy - 7, 14, 14), statusGlyph(o->group), o->group == 2 ? c.starlight : c.mist);
            x += 18;
        }
        Theme::paintPill(p, QPointF(x, cy - 11), o->name, o->shade);
        break;
    }
    case PropType::MultiSelect: {
        double x = rect.left();
        for (const QJsonValue &v : value.toArray()) {
            const Option *o = prop.option(v.toString());
            if (!o)
                continue;
            const double w = Theme::pillWidth(o->name);
            if (x + w > rect.right() && x > rect.left()) {
                p.setPen(c.fog);
                p.setFont(Theme::uiFont(12));
                p.drawText(QRectF(x, rect.top(), rect.right() - x, rect.height()), Qt::AlignVCenter, QStringLiteral("…"));
                break;
            }
            Theme::paintPill(p, QPointF(x, cy - 11), o->name, o->shade);
            x += w + 5;
        }
        break;
    }
    case PropType::Checkbox: {
        const bool on = value.toBool();
        const QRectF box(rect.left() + 1, cy - 8, 16, 16);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(c.starlight);
            p.drawRoundedRect(box, 4, 4);
            QPainterPath tick;
            tick.moveTo(box.left() + 4.2, box.top() + 8.5);
            tick.lineTo(box.left() + 6.9, box.top() + 11.2);
            tick.lineTo(box.left() + 12, box.top() + 5.3);
            p.setPen(QPen(c.basalt, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(tick);
        } else {
            p.setPen(QPen(c.dust, 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(box.adjusted(0.7, 0.7, -0.7, -0.7), 4, 4);
        }
        break;
    }
    case PropType::Created:
    case PropType::Edited: {
        const QDateTime dt = dateTimeOf(value);
        p.setFont(Theme::uiFont(13));
        p.setPen(c.fog);
        p.drawText(rect, Qt::AlignVCenter | align,
                   dt.isValid() ? QLocale(QLocale::English).toString(dt, QStringLiteral("MMM d, yyyy h:mm AP")) : QString());
        break;
    }
    case PropType::Number:
    case PropType::UniqueId: {
        if (!value.isDouble())
            break;
        const QString text = displayValue(prop, value);
        if (prop.type == PropType::Number && prop.numberShow != QLatin1String("number")) {
            paintNumberVisual(p, rect, prop, value.toDouble(), text);
            break;
        }
        QFont f = Theme::uiFont(13.5);
        f.setFeature(QFont::Tag("tnum"), 1);
        p.setFont(f);
        p.setPen(dim ? c.haze : (prop.type == PropType::UniqueId ? c.mist : c.moon));
        p.drawText(rect, Qt::AlignVCenter | align, QFontMetricsF(f).elidedText(text, Qt::ElideRight, rect.width()));
        break;
    }
    case PropType::Formula: {
        if (value.isBool()) {
            paintCheck(p, QRectF(rect.left() + 1, cy - 8, 16, 16), value.toBool());
            break;
        }
        if (value.isDouble() && prop.numberShow != QLatin1String("number")) {
            paintNumberVisual(p, rect, prop, value.toDouble(), displayValue(prop, value));
            break;
        }
        const QString text = displayValue(prop, value);
        QFont f = Theme::uiFont(13.5);
        if (value.isDouble())
            f.setFeature(QFont::Tag("tnum"), 1);
        p.setFont(f);
        p.setPen(dim ? c.haze : c.moon);
        p.drawText(rect, Qt::AlignVCenter | (value.isDouble() ? align : Qt::AlignLeft),
                   QFontMetricsF(f).elidedText(text.simplified(), Qt::ElideRight, rect.width()));
        break;
    }
    case PropType::Rollup: {
        const QString text = displayValue(prop, value);
        if (prop.rollupFn.startsWith(QLatin1String("pct")) && value.isDouble()) {
            Property bar = prop;
            bar.numberShow = QStringLiteral("bar");
            bar.numberDivide = 1;
            paintNumberVisual(p, rect, bar, value.toDouble(), text);
            break;
        }
        QFont f = Theme::uiFont(13.5);
        p.setFont(f);
        p.setPen(dim ? c.haze : c.moon);
        p.drawText(rect, Qt::AlignVCenter | (value.isDouble() ? align : Qt::AlignLeft), QFontMetricsF(f).elidedText(text, Qt::ElideRight, rect.width()));
        break;
    }
    case PropType::Relation: {
        Workspace *ws = Workspace::instance();
        double x = rect.left();
        for (const QString &id : relationIds(value)) {
            const PageMeta *m = ws ? ws->page(id) : nullptr;
            Icons::paintPageIcon(p, QRectF(x, cy - 8, 16, 16), m ? m->icon : QString(), c.mist, QStringLiteral("page"));
            x += 20;
            QFont f = Theme::uiFont(13.5);
            f.setUnderline(true);
            p.setFont(f);
            p.setPen(c.moon);
            const QString t = ws ? ws->title(id) : id;
            const double w = QFontMetricsF(f).horizontalAdvance(t);
            p.drawText(QRectF(x, rect.top(), qMin(w + 2, rect.right() - x), rect.height()), Qt::AlignVCenter, t);
            x += w + 12;
            if (x > rect.right())
                break;
        }
        break;
    }
    case PropType::Person:
    case PropType::CreatedBy:
    case PropType::EditedBy:
        paintPeople(p, rect, value);
        break;
    case PropType::Files: {
        double x = rect.left();
        for (const FileRef &f : filesOf(value)) {
            Icons::paint(p, QRectF(x, cy - 7, 14, 14), QStringLiteral("paperclip"), c.mist);
            x += 18;
            p.setFont(Theme::uiFont(13.5));
            p.setPen(c.moon);
            const double w = QFontMetricsF(p.font()).horizontalAdvance(f.name);
            p.drawText(QRectF(x, rect.top(), qMin(w + 2, rect.right() - x), rect.height()), Qt::AlignVCenter, f.name);
            x += w + 12;
            if (x > rect.right())
                break;
        }
        break;
    }
    case PropType::Button: {
        const QString label = prop.button.value(QStringLiteral("label")).toString().isEmpty() ? tr("Button")
                                                                                               : prop.button.value(QStringLiteral("label")).toString();
        p.setFont(Theme::uiFont(12.5, QFont::Medium));
        const double w = QFontMetricsF(p.font()).horizontalAdvance(label) + 30;
        const QRectF b(rect.left(), cy - 12, qMin(w, rect.width()), 24);
        p.setPen(QPen(c.smoke, 1));
        p.setBrush(QColor(0x22, 0x22, 0x29));
        p.drawRoundedRect(b.adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        Icons::paint(p, QRectF(b.left() + 7, cy - 6, 12, 12), QStringLiteral("cursor"), c.mist);
        p.setPen(c.starlight);
        p.drawText(b.adjusted(22, 0, -6, 0), Qt::AlignVCenter, label);
        break;
    }
    case PropType::Title: {
        p.setFont(Theme::uiFont(13.5, QFont::Medium));
        p.setPen(dim ? c.haze : c.starlight);
        p.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(p.font()).elidedText(value.toString(), Qt::ElideRight, rect.width()));
        break;
    }
    default: {
        const QString text = displayValue(prop, value);
        if (text.isEmpty())
            break;
        QFont f = Theme::uiFont(13.5);
        if (prop.type == PropType::Number)
            f.setFeature(QFont::Tag("tnum"), 1);
        const bool link = prop.type == PropType::Url || prop.type == PropType::Email;
        f.setUnderline(link);
        p.setFont(f);
        p.setPen(dim ? c.haze : (link ? c.starlight : c.moon));
        QString shown = text;
        shown.replace(QLatin1Char('\n'), QLatin1Char(' '));
        p.drawText(rect, Qt::AlignVCenter | align, QFontMetricsF(f).elidedText(shown, Qt::ElideRight, rect.width()));
        break;
    }
    }
    p.restore();
}

void showPropertyMenu(QWidget *parent, const QPoint &globalPos, const QString &databaseId, const QString &propId, const QString &viewId)
{
    Workspace *ws = Workspace::instance();
    const Schema schema = ws->schema(databaseId);
    const Property *prop = schema.prop(propId);
    if (!prop)
        return;
    QMenu *menu = Theme::makeMenu(parent);
    Theme::addMenuSection(menu, prop->name);
    QAction *rename = menu->addAction(Icons::icon(QStringLiteral("pencil")), tr("Rename"));
    QObject::connect(rename, &QAction::triggered, parent, [parent, databaseId, propId] {
        Workspace *ws = Workspace::instance();
        Schema s = ws->schema(databaseId);
        Property *p = s.prop(propId);
        QString name = p->name;
        if (textDialog(parent->window(), tr("Rename property"), QString(), &name) && !name.trimmed().isEmpty()) {
            p->name = name.trimmed();
            ws->setSchema(databaseId, s);
        }
    });
    if (prop->type != PropType::Title) {
        QMenu *types = menu->addMenu(Icons::icon(propTypeIcon(prop->type)), tr("Type: %1").arg(propTypeName(prop->type)));
        Theme::polishMenu(types);
        for (PropType t : userPropTypes()) {
            QAction *a = types->addAction(Icons::icon(propTypeIcon(t)), propTypeName(t));
            a->setCheckable(true);
            a->setChecked(t == prop->type);
            QObject::connect(a, &QAction::triggered, parent, [parent, databaseId, propId, t] {
                Workspace *ws = Workspace::instance();
                Schema s = ws->schema(databaseId);
                Property *p = s.prop(propId);
                if (!p || p->type == t)
                    return;
                if (t == PropType::Relation) {
                    // A relation needs the database it points at.
                    auto *picker = new PagePicker(parent->window(), QObject::tr("Relate to a database"), QString(), false, true);
                    picker->setDatabasesOnly(true);
                    QObject::connect(picker, &PagePicker::picked, parent, [databaseId, propId](const QString &target) {
                        if (target.isEmpty())
                            return;
                        Workspace *ws = Workspace::instance();
                        Schema s = ws->schema(databaseId);
                        Property *p = s.prop(propId);
                        if (!p)
                            return;
                        p->type = PropType::Relation;
                        p->relationDb = target;
                        p->options.clear();
                        ws->batch([&] {
                            for (const PageMeta *row : ws->rows(databaseId)) {
                                QJsonObject props = ws->props(row->id);
                                if (props.remove(propId), true)
                                    ws->setProps(row->id, props);
                            }
                            ws->setSchema(databaseId, s);
                        });
                    });
                    picker->popupAt(QCursor::pos());
                    return;
                }
                const Property old = *p;
                p->type = t;
                if (propTypeIsComputed(t) || t == PropType::Relation) {
                    // Computed values don't come from stored text.
                    ws->setSchema(databaseId, s);
                    if (t == PropType::Formula)
                        (new FormulaEditor(parent->window(), databaseId, propId))->popupAt(QCursor::pos());
                    return;
                }
                if (!propTypeHasOptions(t))
                    p->options.clear();
                if (t == PropType::Status && p->options.isEmpty())
                    p->options = {{QStringLiteral("todo"), tr("Not started"), 2, 0},
                                  {QStringLiteral("doing"), tr("In progress"), 5, 1},
                                  {QStringLiteral("done"), tr("Done"), 8, 2}};
                // Convert every row's value through its displayed text.
                ws->batch([&] {
                    for (const PageMeta *row : ws->rows(databaseId)) {
                        QJsonObject props = ws->props(row->id);
                        if (!props.contains(propId))
                            continue;
                        const QString text = displayValue(old, props.value(propId));
                        const QJsonValue v = parseValue(*p, text, true);
                        if (v.isNull() || v.isUndefined())
                            props.remove(propId);
                        else
                            props[propId] = v;
                        ws->setProps(row->id, props);
                    }
                    ws->setSchema(databaseId, s);
                });
            });
        }
    }
    auto saveProp = [databaseId, propId](const std::function<void(Property &)> &fn) {
        Workspace *ws = Workspace::instance();
        Schema s = ws->schema(databaseId);
        if (Property *p = s.prop(propId)) {
            fn(*p);
            ws->setSchema(databaseId, s);
        }
    };
    const PropType shown = resultType(*prop);
    if (prop->type == PropType::Formula) {
        menu->addAction(Icons::icon(QStringLiteral("formula")), tr("Edit formula…"), parent,
                        [parent, databaseId, propId] { (new FormulaEditor(parent->window(), databaseId, propId))->popupAt(QCursor::pos()); });
    }
    if (prop->type == PropType::Relation) {
        const QString target = prop->relationDb;
        menu->addAction(Icons::icon(QStringLiteral("database")), tr("Related to: %1").arg(ws->exists(target) ? ws->title(target) : tr("none")), parent,
                        [parent, databaseId, propId, saveProp] {
                            auto *picker = new PagePicker(parent->window(), QObject::tr("Relate to a database"), QString(), false, true);
                            picker->setDatabasesOnly(true);
                            QObject::connect(picker, &PagePicker::picked, parent, [databaseId, propId, saveProp](const QString &t) {
                                if (t.isEmpty())
                                    return;
                                Workspace::instance()->setRelationTwoWay(databaseId, propId, false);
                                saveProp([&](Property &p) { p.relationDb = t; });
                            });
                            picker->popupAt(QCursor::pos());
                        });
        QAction *twoWay = menu->addAction(Icons::icon(QStringLiteral("relation")),
                                          ws->exists(target) ? tr("Show on %1").arg(ws->title(target)) : tr("Two-way relation"));
        twoWay->setCheckable(true);
        twoWay->setChecked(!prop->relationPair.isEmpty());
        twoWay->setEnabled(ws->exists(target));
        QObject::connect(twoWay, &QAction::toggled, parent, [databaseId, propId](bool on) {
            Workspace::instance()->setRelationTwoWay(databaseId, propId, on);
        });
        QAction *one = menu->addAction(tr("Limit to one page"));
        one->setCheckable(true);
        one->setChecked(prop->relationOne);
        QObject::connect(one, &QAction::toggled, parent, [saveProp](bool on) { saveProp([&](Property &p) { p.relationOne = on; }); });
    }
    if (prop->type == PropType::Rollup) {
        QMenu *rel = menu->addMenu(Icons::icon(QStringLiteral("relation")), tr("Relation"));
        Theme::polishMenu(rel);
        for (const Property &r : schema.props) {
            if (r.type != PropType::Relation)
                continue;
            QAction *a = rel->addAction(r.name);
            a->setCheckable(true);
            a->setChecked(prop->rollupRelation == r.id);
            QObject::connect(a, &QAction::triggered, parent, [saveProp, id = r.id] {
                saveProp([&](Property &p) {
                    p.rollupRelation = id;
                    p.rollupProp.clear();
                });
            });
        }
        if (rel->isEmpty())
            rel->addAction(tr("Add a relation property first"))->setEnabled(false);
        const Property *relProp = schema.prop(prop->rollupRelation);
        if (relProp) {
            const Schema target = ws->schema(relProp->relationDb);
            QMenu *of = menu->addMenu(Icons::icon(QStringLiteral("rollup")), tr("Property"));
            Theme::polishMenu(of);
            for (const Property &t : target.props) {
                QAction *a = of->addAction(Icons::icon(propTypeIcon(t.type)), t.name);
                a->setCheckable(true);
                a->setChecked(prop->rollupProp == t.id || (prop->rollupProp.isEmpty() && t.type == PropType::Title));
                QObject::connect(a, &QAction::triggered, parent, [saveProp, id = t.id] { saveProp([&](Property &p) { p.rollupProp = id; }); });
            }
            const Property *tp = target.prop(prop->rollupProp);
            if (!tp)
                tp = target.titleProp();
            if (tp) {
                QMenu *calc = menu->addMenu(Icons::icon(QStringLiteral("calc")), tr("Calculate: %1").arg(calcName(prop->rollupFn)));
                Theme::polishMenu(calc);
                for (const QString &fn : calcFunctions(*tp, true)) {
                    QAction *a = calc->addAction(calcName(fn));
                    a->setCheckable(true);
                    a->setChecked(prop->rollupFn == fn);
                    QObject::connect(a, &QAction::triggered, parent, [saveProp, fn] { saveProp([&](Property &p) { p.rollupFn = fn; }); });
                }
            }
        }
    }
    if (prop->type == PropType::UniqueId) {
        menu->addAction(Icons::icon(QStringLiteral("hash")), prop->idPrefix.isEmpty() ? tr("Add a prefix…") : tr("Prefix: %1").arg(prop->idPrefix),
                        parent, [parent, saveProp, prefix = prop->idPrefix] {
                            QString text = prefix;
                            if (textDialog(parent->window(), tr("ID prefix"), tr("IDs look like PREFIX-12."), &text))
                                saveProp([&](Property &p) { p.idPrefix = text.trimmed().toUpper(); });
                        });
    }
    if (prop->type == PropType::Button) {
        menu->addAction(Icons::icon(QStringLiteral("cursor")), tr("Edit button…"), parent,
                        [parent, databaseId, propId] { (new ButtonPropertyPopover(parent->window(), databaseId, propId))->popupAt(QCursor::pos()); });
    }
    if (shown == PropType::Date || shown == PropType::Created || shown == PropType::Edited) {
        QMenu *df = menu->addMenu(Icons::icon(QStringLiteral("calendar")), tr("Date format"));
        Theme::polishMenu(df);
        const QList<QPair<QString, QString>> formats = {{QStringLiteral("default"), tr("Default (Oct 17, 2026)")},
                                                        {QStringLiteral("full"), tr("Full date (October 17, 2026)")},
                                                        {QStringLiteral("mdy"), tr("Month/Day/Year")},
                                                        {QStringLiteral("dmy"), tr("Day/Month/Year")},
                                                        {QStringLiteral("ymd"), tr("Year/Month/Day")},
                                                        {QStringLiteral("relative"), tr("Relative (Today, Friday…)")}};
        for (const auto &[key, name] : formats) {
            QAction *a = df->addAction(name);
            a->setCheckable(true);
            a->setChecked(prop->dateFormat == key);
            QObject::connect(a, &QAction::triggered, parent, [saveProp, key = key] { saveProp([&](Property &p) { p.dateFormat = key; }); });
        }
        QAction *t24 = menu->addAction(Icons::icon(QStringLiteral("clock")), tr("24-hour time"));
        t24->setCheckable(true);
        t24->setChecked(prop->time24);
        QObject::connect(t24, &QAction::toggled, parent, [saveProp](bool on) { saveProp([&](Property &p) { p.time24 = on; }); });
    }
    if (shown == PropType::Number && prop->type != PropType::UniqueId && prop->type != PropType::Rollup) {
        QMenu *showAs = menu->addMenu(Icons::icon(QStringLiteral("number-bar")), tr("Show as"));
        Theme::polishMenu(showAs);
        for (const auto &[key, name, icon] : {std::tuple{QStringLiteral("number"), tr("Number"), QStringLiteral("number")},
                                              std::tuple{QStringLiteral("bar"), tr("Bar"), QStringLiteral("number-bar")},
                                              std::tuple{QStringLiteral("ring"), tr("Ring"), QStringLiteral("number-ring")}}) {
            QAction *a = showAs->addAction(Icons::icon(icon), name);
            a->setCheckable(true);
            a->setChecked(prop->numberShow == key);
            QObject::connect(a, &QAction::triggered, parent, [saveProp, key = key] { saveProp([&](Property &p) { p.numberShow = key; }); });
        }
        if (prop->numberShow != QLatin1String("number")) {
            showAs->addSeparator();
            showAs->addAction(tr("Divide by %1…").arg(formatNumber(prop->numberDivide, QStringLiteral("number"))), parent,
                              [parent, saveProp, divide = prop->numberDivide] {
                                  QString text = formatNumber(divide, QStringLiteral("number"));
                                  if (textDialog(parent->window(), tr("Divide by"), tr("The value that fills the bar or ring."), &text)) {
                                      const double d = text.toDouble();
                                      if (d > 0)
                                          saveProp([&](Property &p) { p.numberDivide = d; });
                                  }
                              });
        }
        QMenu *dec = menu->addMenu(tr("Decimal places"));
        Theme::polishMenu(dec);
        for (int d = -1; d <= 4; ++d) {
            QAction *a = dec->addAction(d < 0 ? tr("Default") : QString::number(d));
            a->setCheckable(true);
            a->setChecked(prop->decimals == d);
            QObject::connect(a, &QAction::triggered, parent, [saveProp, d] { saveProp([&](Property &p) { p.decimals = d; }); });
        }
    }
    if (shown == PropType::Number && prop->type != PropType::UniqueId) {
        QMenu *formats = menu->addMenu(Icons::icon(QStringLiteral("number")), tr("Number format"));
        Theme::polishMenu(formats);
        const QList<QPair<QString, QString>> list = {{QStringLiteral("number"), tr("Number")},  {QStringLiteral("comma"), tr("Number with commas")},
                                                     {QStringLiteral("percent"), tr("Percent")}, {QStringLiteral("dollar"), tr("Dollar")},
                                                     {QStringLiteral("euro"), tr("Euro")},       {QStringLiteral("pound"), tr("Pound")},
                                                     {QStringLiteral("yen"), tr("Yen")},         {QStringLiteral("rupee"), tr("Rupee")},
                                                     {QStringLiteral("franc"), tr("Franc")}};
        for (const auto &[key, name] : list) {
            QAction *a = formats->addAction(name);
            a->setCheckable(true);
            a->setChecked(prop->numberFormat == key);
            QObject::connect(a, &QAction::triggered, parent, [databaseId, propId, key = key] {
                Workspace *ws = Workspace::instance();
                Schema s = ws->schema(databaseId);
                s.prop(propId)->numberFormat = key;
                ws->setSchema(databaseId, s);
            });
        }
    }
    menu->addAction(Icons::icon(QStringLiteral("info")), prop->description.isEmpty() ? tr("Add a description…") : tr("Edit description…"), parent,
                    [parent, saveProp, desc = prop->description] {
                        QString text = desc;
                        if (textDialog(parent->window(), tr("Property description"), tr("Shown when hovering the property's name."), &text))
                            saveProp([&](Property &p) { p.description = text.trimmed(); });
                    });
    if (!viewId.isEmpty()) {
        menu->addSeparator();
        auto addSort = [&](bool desc) {
            QAction *a = menu->addAction(Icons::icon(desc ? QStringLiteral("arrow-down") : QStringLiteral("arrow-up")),
                                         desc ? tr("Sort descending") : tr("Sort ascending"));
            QObject::connect(a, &QAction::triggered, parent, [databaseId, propId, viewId, desc] {
                Workspace *ws = Workspace::instance();
                Schema s = ws->schema(databaseId);
                View *v = s.view(viewId);
                if (!v)
                    return;
                v->sorts.removeIf([&](const SortRule &r) { return r.prop == propId; });
                v->sorts.prepend({propId, desc});
                ws->setSchema(databaseId, s);
            });
        };
        addSort(false);
        addSort(true);
        QAction *filter = menu->addAction(Icons::icon(QStringLiteral("filter")), tr("Filter"));
        QObject::connect(filter, &QAction::triggered, parent, [databaseId, propId, viewId] {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(databaseId);
            View *v = s.view(viewId);
            const Property *p = s.prop(propId);
            if (!v || !p)
                return;
            v->filters.append({propId, filterOps(p->type).first(), QJsonValue()});
            ws->setSchema(databaseId, s);
        });
        if (prop->type != PropType::Title) {
            QAction *hide = menu->addAction(Icons::icon(QStringLiteral("eye-off")), tr("Hide in view"));
            QObject::connect(hide, &QAction::triggered, parent, [databaseId, propId, viewId] {
                Workspace *ws = Workspace::instance();
                Schema s = ws->schema(databaseId);
                if (View *v = s.view(viewId); v && !v->hidden.contains(propId)) {
                    v->hidden.append(propId);
                    ws->setSchema(databaseId, s);
                }
            });
        }
    }
    if (prop->type != PropType::Title) {
        menu->addSeparator();
        QAction *dup = menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate property"));
        QObject::connect(dup, &QAction::triggered, parent, [databaseId, propId] {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(databaseId);
            Property copy = *s.prop(propId);
            copy.id = s.newPropId();
            copy.name = tr("%1 (copy)").arg(copy.name);
            s.props.insert(s.propIndex(propId) + 1, copy);
            ws->batch([&] {
                for (const PageMeta *row : ws->rows(databaseId)) {
                    QJsonObject props = ws->props(row->id);
                    if (props.contains(propId)) {
                        props[copy.id] = props.value(propId);
                        ws->setProps(row->id, props);
                    }
                }
                ws->setSchema(databaseId, s);
            });
        });
        QAction *del = menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Delete property"));
        QObject::connect(del, &QAction::triggered, parent, [parent, databaseId, propId] {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(databaseId);
            const Property *p = s.prop(propId);
            if (!p || !confirmDialog(parent->window(), tr("Delete property “%1”?").arg(p->name),
                                     tr("Its values are removed from every row in this database."), tr("Delete"), true))
                return;
            ws->batch([&] {
                for (const PageMeta *row : ws->rows(databaseId)) {
                    QJsonObject props = ws->props(row->id);
                    if (props.contains(propId)) {
                        props.remove(propId);
                        ws->setProps(row->id, props);
                    }
                }
                ws->removeProperty(databaseId, propId);
            });
        });
    }
    menu->popup(globalPos);
}

void addProperty(QWidget *parent, const QPoint &globalPos, const QString &databaseId, const QString &viewId)
{
    QMenu *menu = Theme::makeMenu(parent);
    Theme::addMenuSection(menu, tr("New property"));
    for (PropType t : userPropTypes()) {
        QAction *a = menu->addAction(Icons::icon(propTypeIcon(t)), propTypeName(t));
        QObject::connect(a, &QAction::triggered, parent, [parent, databaseId, viewId, t] {
            Workspace *ws = Workspace::instance();
            Schema s = ws->schema(databaseId);
            Property p;
            p.id = s.newPropId();
            p.type = t;
            p.name = propTypeName(t);
            // Unique names: "Date", "Date 2"…
            QString base = p.name;
            for (int n = 2; std::any_of(s.props.begin(), s.props.end(), [&](const Property &x) { return x.name == p.name; }); ++n)
                p.name = QStringLiteral("%1 %2").arg(base).arg(n);
            if (t == PropType::Status)
                p.options = {{QStringLiteral("todo"), tr("Not started"), 2, 0},
                             {QStringLiteral("doing"), tr("In progress"), 5, 1},
                             {QStringLiteral("done"), tr("Done"), 8, 2}};
            if (t == PropType::Button)
                p.button = QJsonObject{{QStringLiteral("label"), tr("Done")},
                                       {QStringLiteral("actions"), QJsonArray{}}};
            auto finish = [databaseId, viewId](Schema &s, const QString &id) {
                if (View *v = s.view(viewId))
                    v->hidden.removeAll(id);
                // Other card views stay calm: new properties start hidden there.
                for (View &v : s.views)
                    if (v.id != viewId && v.type != ViewType::Table)
                        v.hidden.append(id);
            };
            if (t == PropType::Relation) {
                auto *picker = new PagePicker(parent->window(), QObject::tr("Relate to a database"), QString(), false, true);
                picker->setDatabasesOnly(true);
                QObject::connect(picker, &PagePicker::picked, parent, [databaseId, finish](const QString &target) {
                    if (target.isEmpty())
                        return;
                    Workspace *ws = Workspace::instance();
                    const QString id = ws->addRelation(databaseId, QString(), target, false);
                    Schema s = ws->schema(databaseId);
                    finish(s, id);
                    ws->setSchema(databaseId, s);
                });
                picker->popupAt(QCursor::pos());
                return;
            }
            s.props.append(p);
            finish(s, p.id);
            ws->setSchema(databaseId, s);
            if (t == PropType::Formula)
                (new FormulaEditor(parent->window(), databaseId, p.id))->popupAt(QCursor::pos());
            else if (t == PropType::Button)
                (new ButtonPropertyPopover(parent->window(), databaseId, p.id))->popupAt(QCursor::pos());
            else if (t == PropType::Rollup)
                showPropertyMenu(parent, QCursor::pos(), databaseId, p.id, viewId);
        });
    }
    menu->popup(globalPos);
}

}

// ---- PropertyPanel -----------------------------------------------------------------

PropertyPanel::PropertyPanel(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::propsChanged, this, [this](const QString &id) {
            if (id == m_rowId)
                refresh();
        });
        connect(ws, &Workspace::schemaChanged, this, [this](const QString &id) {
            if (id == m_dbId)
                refresh();
        });
    }
}

void PropertyPanel::setRow(const QString &rowId)
{
    m_rowId = rowId;
    refresh();
}

void PropertyPanel::refresh()
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws ? ws->page(m_rowId) : nullptr;
    m_lines.clear();
    if (!m || !m->isRow()) {
        setVisible(false);
        setFixedHeight(0);
        emit heightChanged();
        return;
    }
    setVisible(true);
    m_dbId = m->parentId;
    m_schema = ws->schema(m_dbId);
    m_props = ws->props(m_rowId);
    double y = 4;
    for (const Property &p : m_schema.props) {
        if (p.type == PropType::Title)
            continue;
        m_lines.append({p.id, QRectF(0, y, 170, 34), QRectF(178, y, width() - 178, 34)});
        y += 34;
    }
    m_addRect = QRectF(0, y, 190, 32);
    y += 32 + 10;
    setFixedHeight(int(y));
    update();
    emit heightChanged();
}

void PropertyPanel::paintEvent(QPaintEvent *)
{
    const auto &c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Workspace *ws = Workspace::instance();
    const PageMeta *meta = ws ? ws->page(m_rowId) : nullptr;
    for (int i = 0; i < m_lines.size(); ++i) {
        const Line &line = m_lines[i];
        const Property *prop = m_schema.prop(line.propId);
        if (!prop)
            continue;
        if (i == m_hover) {
            const QRectF r = m_hoverName ? line.nameRect : line.valueRect.adjusted(-6, 0, 0, 0);
            Theme::paintHover(p, r.adjusted(0, 2, 0, -2), 1.0, 6);
        }
        Icons::paint(p, QRectF(line.nameRect.left() + 6, line.nameRect.center().y() - 8, 16, 16), propTypeIcon(prop->type), c.fog);
        p.setFont(Theme::uiFont(13.5));
        p.setPen(c.haze);
        p.drawText(line.nameRect.adjusted(30, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QFontMetricsF(p.font()).elidedText(prop->name, Qt::ElideRight, line.nameRect.width() - 36));
        const RowRef ref{meta, m_props};
        const QJsonValue v = rowValue(*prop, ref);
        const QRectF vr(line.valueRect.left(), line.valueRect.top(), width() - line.valueRect.left() - 8, line.valueRect.height());
        if (isEmptyValue(*prop, v) && prop->type != PropType::Checkbox) {
            p.setFont(Theme::uiFont(13.5));
            p.setPen(c.dust);
            p.drawText(vr, Qt::AlignVCenter, tr("Empty"));
        } else {
            PropertyEditors::paintValue(p, vr, *prop, v);
        }
    }
    if (m_hover == -2)
        Theme::paintHover(p, m_addRect.adjusted(0, 2, 0, -2), 1.0, 6);
    Icons::paint(p, QRectF(m_addRect.left() + 6, m_addRect.center().y() - 8, 16, 16), QStringLiteral("plus"), c.fog);
    p.setFont(Theme::uiFont(13.5));
    p.setPen(m_hover == -2 ? c.moon : c.fog);
    p.drawText(m_addRect.adjusted(30, 0, 0, 0), Qt::AlignVCenter, tr("Add a property"));
    // A hairline under the panel separates it from the page body.
    p.setPen(QPen(c.hairline.lighter(130), 1));
    p.drawLine(QPointF(0, height() - 5.5), QPointF(width(), height() - 5.5));
}

void PropertyPanel::mouseMoveEvent(QMouseEvent *e)
{
    int hover = -1;
    bool name = false;
    for (int i = 0; i < m_lines.size(); ++i) {
        if (m_lines[i].nameRect.contains(e->position())) {
            hover = i;
            name = true;
        } else if (QRectF(m_lines[i].valueRect.left(), m_lines[i].valueRect.top(), width(), m_lines[i].valueRect.height()).contains(e->position())) {
            hover = i;
        }
    }
    if (m_addRect.contains(e->position()))
        hover = -2;
    if (hover != m_hover || name != m_hoverName) {
        m_hover = hover;
        m_hoverName = name;
        setCursor(hover == -1 ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void PropertyPanel::mousePressEvent(QMouseEvent *e)
{
    if (m_hover == -2) {
        PropertyEditors::addProperty(this, mapToGlobal(m_addRect.bottomLeft().toPoint()), m_dbId);
        return;
    }
    if (m_hover < 0 || m_hover >= m_lines.size())
        return;
    const Line &line = m_lines[m_hover];
    if (m_hoverName)
        PropertyEditors::showPropertyMenu(this, mapToGlobal(line.nameRect.bottomLeft().toPoint()), m_dbId, line.propId);
    else
        PropertyEditors::edit(this, mapToGlobal(QPoint(int(line.valueRect.left()) - 8, int(line.valueRect.top()))), m_dbId, m_rowId,
                              line.propId, int(width() - line.valueRect.left()));
}

void PropertyPanel::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

#include "PropertyEditors.moc"
