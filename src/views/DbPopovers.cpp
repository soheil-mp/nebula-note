// Database popovers: the advanced filter builder, sorts, the view settings
// menu and bulk editing.
#include "DatabaseBody.h"

#include "PropertyEditors.h"
#include "core/DateParse.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

namespace
{
QString tr(const char *s, const char *c = nullptr, int n = -1)
{
    return QCoreApplication::translate("DbPopovers", s, c, n);
}

QComboBox *combo(int minWidth = 0)
{
    auto *c = new QComboBox;
    c->setFont(Theme::uiFont(13));
    c->setMinimumHeight(30);
    if (minWidth)
        c->setMinimumWidth(minWidth);
    return c;
}

// A settings menu row: icon and label on the left, the current value in gray
// on the right, and a chevron when it opens a page of its own.
class MenuRow : public QAbstractButton
{
public:
    MenuRow(const QString &icon, const QString &label, const QString &value, bool chevron)
        : m_icon(icon)
        , m_value(value)
        , m_chevron(chevron)
    {
        setText(label);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setMinimumWidth(300);
    }
    QSize sizeHint() const override { return {300, 30}; }

protected:
    void paintEvent(QPaintEvent *) override
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
            p.drawText(QRectF(right - valueW, 0, valueW, height()), Qt::AlignVCenter | Qt::AlignRight,
                       fm.elidedText(m_value, Qt::ElideRight, valueW));
            valueW += 12;
        }
        p.setPen(isEnabled() ? (hot ? c.starlight : c.moon) : c.dust);
        p.drawText(QRectF(textX, 0, right - valueW - textX, height()), Qt::AlignVCenter,
                   fm.elidedText(text(), Qt::ElideRight, right - valueW - textX));
    }

private:
    QString m_icon;
    QString m_value;
    bool m_chevron;
};

QLabel *caption(const QString &text)
{
    auto *l = new QLabel(text);
    l->setFont(Theme::uiFont(12, QFont::Medium));
    l->setStyleSheet(QStringLiteral("color: #8a8a94; padding-top: 6px;"));
    return l;
}

void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        if (QLayout *l = item->layout()) {
            clearLayout(l);
            delete l;
            continue;
        }
        delete item;
    }
}

// Rules at a path of group indexes inside a view's filter tree.
QList<FilterRule> *rulesAt(View &v, const QList<int> &path)
{
    QList<FilterRule> *rules = &v.filters;
    for (int i : path) {
        if (i < 0 || i >= rules->size() || !(*rules)[i].group)
            return nullptr;
        rules = &(*rules)[i].rules;
    }
    return rules;
}

bool groupAny(const View &v, const QList<int> &path)
{
    if (path.isEmpty())
        return v.filterAny;
    const QList<FilterRule> *rules = &v.filters;
    const FilterRule *g = nullptr;
    for (int i : path) {
        if (i < 0 || i >= rules->size())
            return false;
        g = &(*rules)[i];
        rules = &g->rules;
    }
    return g && g->any;
}

FilterRule newRule(const Schema &s)
{
    // The first non-title property, like Notion's default.
    const Property *p = s.props.size() > 1 ? &s.props[1] : (s.props.isEmpty() ? nullptr : &s.props[0]);
    FilterRule r;
    if (p) {
        r.prop = p->id;
        const QStringList ops = filterOps(resultType(*p));
        r.op = ops.isEmpty() ? QStringLiteral("notempty") : ops.first();
    }
    return r;
}

QString valueText(const Schema &schema, const Property &p, const QJsonValue &value)
{
    Workspace *ws = Workspace::instance();
    auto one = [&](const QJsonValue &x) -> QString {
        const QString s = x.toString();
        switch (p.type) {
        case PropType::Select:
        case PropType::Status:
        case PropType::MultiSelect:
            return p.option(s) ? p.option(s)->name : QString();
        case PropType::Person:
        case PropType::CreatedBy:
        case PropType::EditedBy:
            return ws ? ws->personName(s) : s;
        case PropType::Relation:
            return ws && ws->exists(s) ? ws->title(s) : QString();
        default:
            break;
        }
        if (s.startsWith(QLatin1Char('@')))
            return relativeDateName(s);
        if (x.isDouble())
            return formatNumber(x.toDouble(), QStringLiteral("number"));
        if (resultType(p) == PropType::Date || p.type == PropType::Created || p.type == PropType::Edited)
            return formatDateValue(s);
        return s;
    };
    Q_UNUSED(schema)
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &x : value.toArray())
            parts.append(one(x));
        return parts.join(QStringLiteral(" → "));
    }
    return one(value);
}

// ---- filters -------------------------------------------------------------------------

class FilterPopover : public Popover
{
public:
    FilterPopover(QWidget *parent, const QString &dbId, DbPopovers::ViewGet get, DbPopovers::ViewEdit edit)
        : Popover(parent)
        , m_db(dbId)
        , m_get(std::move(get))
        , m_edit(std::move(edit))
    {
        setObjectName(QStringLiteral("filterPopover"));
        m_layout = new QVBoxLayout(content());
        m_layout->setContentsMargins(12, 10, 12, 10);
        m_layout->setSpacing(6);
        rebuild();
    }

private:
    void change(const std::function<void(View &)> &fn)
    {
        m_edit(fn);
        QTimer::singleShot(0, this, [this] { rebuild(); });
    }

    QWidget *valueEditor(const Property &p, const QList<int> &path, int index, const FilterRule &rule)
    {
        const QString op = rule.op;
        if (!filterNeedsValue(op))
            return nullptr;
        auto setValue = [this, path, index](const QJsonValue &value) {
            m_edit([&](View &v) {
                if (QList<FilterRule> *rules = rulesAt(v, path); rules && index < rules->size())
                    (*rules)[index].value = value;
            });
        };
        Workspace *ws = Workspace::instance();
        const PropType type = resultType(p);
        if ((p.type == PropType::Select || p.type == PropType::Status || p.type == PropType::MultiSelect)) {
            auto *box = combo(130);
            if (op == QLatin1String("ingroup")) {
                box->addItem(tr("To-do"), 0);
                box->addItem(tr("In progress"), 1);
                box->addItem(tr("Complete"), 2);
                box->setCurrentIndex(qBound(0, rule.value.toInt(), 2));
                QObject::connect(box, &QComboBox::activated, this, [box, setValue] { setValue(box->currentData().toInt()); });
                return box;
            }
            box->addItem(tr("Choose…"), QString());
            for (const Option &o : p.options)
                box->addItem(o.name, o.id);
            const QString current = rule.value.isArray() ? rule.value.toArray().first().toString() : rule.value.toString();
            box->setCurrentIndex(qMax(0, box->findData(current)));
            QObject::connect(box, &QComboBox::activated, this, [box, setValue] { setValue(box->currentData().toString()); });
            return box;
        }
        if (p.type == PropType::Person || p.type == PropType::CreatedBy || p.type == PropType::EditedBy) {
            auto *box = combo(130);
            box->addItem(tr("Choose…"), QString());
            for (const Workspace::Person &person : ws->people())
                box->addItem(person.id == ws->lookupMe() ? tr("%1 (me)").arg(person.name) : person.name, person.id);
            box->setCurrentIndex(qMax(0, box->findData(rule.value.toString())));
            QObject::connect(box, &QComboBox::activated, this, [box, setValue] { setValue(box->currentData().toString()); });
            return box;
        }
        if (p.type == PropType::Relation) {
            auto *box = combo(150);
            box->addItem(tr("Choose a page…"), QString());
            for (const PageMeta *row : ws->rows(p.relationDb))
                box->addItem(row->title.isEmpty() ? tr("Untitled") : row->title, row->id);
            box->setCurrentIndex(qMax(0, box->findData(rule.value.toString())));
            QObject::connect(box, &QComboBox::activated, this, [box, setValue] { setValue(box->currentData().toString()); });
            return box;
        }
        if (type == PropType::Date || p.type == PropType::Created || p.type == PropType::Edited) {
            auto dateField = [this](const QJsonValue &current, std::function<void(const QJsonValue &)> set) {
                auto *holder = new QWidget;
                auto *h = new QHBoxLayout(holder);
                h->setContentsMargins(0, 0, 0, 0);
                h->setSpacing(4);
                auto *box = combo(120);
                box->addItem(tr("Exact date"), QString());
                for (const QString &k : relativeDateKeys())
                    box->addItem(relativeDateName(k), k);
                const QString cur = current.toString();
                box->setCurrentIndex(cur.startsWith(QLatin1Char('@')) ? qMax(0, box->findData(cur)) : 0);
                auto *edit = new QLineEdit(cur.startsWith(QLatin1Char('@')) ? QString() : cur);
                edit->setFont(Theme::uiFont(13));
                edit->setPlaceholderText(tr("e.g. oct 20"));
                edit->setFixedWidth(110);
                edit->setVisible(box->currentIndex() == 0);
                h->addWidget(box);
                h->addWidget(edit);
                QObject::connect(box, &QComboBox::activated, this, [box, edit, set] {
                    const QString k = box->currentData().toString();
                    edit->setVisible(k.isEmpty());
                    if (!k.isEmpty())
                        set(k);
                });
                QObject::connect(edit, &QLineEdit::editingFinished, this, [edit, set] {
                    const DateParse::Result r = DateParse::parse(edit->text());
                    if (r.when.isValid()) {
                        edit->setText(r.when.date().toString(Qt::ISODate));
                        set(r.when.date().toString(Qt::ISODate));
                    }
                });
                return holder;
            };
            if (op == QLatin1String("between")) {
                auto *holder = new QWidget;
                auto *h = new QHBoxLayout(holder);
                h->setContentsMargins(0, 0, 0, 0);
                h->setSpacing(4);
                const QJsonArray cur = rule.value.toArray();
                for (int k = 0; k < 2; ++k) {
                    h->addWidget(dateField(cur.size() > k ? cur[k] : QJsonValue(), [this, path, index, k](const QJsonValue &x) {
                        m_edit([&](View &v) {
                            if (QList<FilterRule> *rules = rulesAt(v, path); rules && index < rules->size()) {
                                QJsonArray arr = (*rules)[index].value.toArray();
                                while (arr.size() < 2)
                                    arr.append(QJsonValue());
                                arr[k] = x;
                                (*rules)[index].value = arr;
                            }
                        });
                    }));
                    if (k == 0) {
                        auto *and_ = new QLabel(tr("and"));
                        and_->setStyleSheet(QStringLiteral("color: #8a8a94;"));
                        h->addWidget(and_);
                    }
                }
                return holder;
            }
            return dateField(rule.value, setValue);
        }
        auto *val = new QLineEdit(rule.value.isDouble() ? formatNumber(rule.value.toDouble(), QStringLiteral("number")) : rule.value.toString());
        val->setFont(Theme::uiFont(13));
        val->setMinimumWidth(130);
        val->setPlaceholderText(tr("Value"));
        const bool number = type == PropType::Number;
        QObject::connect(val, &QLineEdit::editingFinished, this, [val, number, setValue] {
            if (number) {
                bool ok = false;
                const double d = QLocale::c().toDouble(val->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')), &ok);
                setValue(ok ? QJsonValue(d) : QJsonValue());
            } else {
                setValue(val->text());
            }
        });
        return val;
    }

    void addGroup(QVBoxLayout *layout, const Schema &s, const View &v, const QList<int> &path, int depth)
    {
        View copy = v;
        const QList<FilterRule> *rules = rulesAt(copy, path);
        if (!rules)
            return;
        const QList<FilterRule> list = *rules;
        const bool any = groupAny(v, path);
        for (int i = 0; i < list.size(); ++i) {
            const FilterRule &rule = list[i];
            auto *row = new QHBoxLayout;
            row->setSpacing(6);
            // Connector: Where / And|Or / and|or.
            QWidget *conn;
            if (i == 1) {
                auto *box = combo();
                box->addItem(tr("And"));
                box->addItem(tr("Or"));
                box->setCurrentIndex(any ? 1 : 0);
                box->setFixedWidth(70);
                QObject::connect(box, &QComboBox::activated, this, [this, path](int idx) {
                    change([&](View &v) {
                        if (path.isEmpty()) {
                            v.filterAny = idx == 1;
                            return;
                        }
                        QList<int> parent = path;
                        const int last = parent.takeLast();
                        if (QList<FilterRule> *rules = rulesAt(v, parent); rules && last < rules->size())
                            (*rules)[last].any = idx == 1;
                    });
                });
                conn = box;
            } else {
                auto *l = new QLabel(i == 0 ? tr("Where") : (any ? tr("Or") : tr("And")));
                l->setFont(Theme::uiFont(13));
                l->setStyleSheet(QStringLiteral("color: #9a9aa6;"));
                l->setFixedWidth(70);
                l->setMinimumHeight(30); // as tall as the combos beside it
                l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                conn = l;
            }
            row->addWidget(conn, 0, Qt::AlignVCenter);
            QList<int> here = path;
            here.append(i);
            auto *more = new FlatButton(QStringLiteral("more"));
            more->setToolTip(tr("More"));
            QObject::connect(more, &FlatButton::clicked, this, [this, more, path, i, depth, rule] {
                QMenu *menu = Theme::makeMenu(this);
                menu->addAction(Icons::icon(QStringLiteral("trash")), tr("Remove"), this, [this, path, i] {
                    change([&](View &v) {
                        if (QList<FilterRule> *rules = rulesAt(v, path); rules && i < rules->size())
                            rules->removeAt(i);
                    });
                });
                menu->addAction(Icons::icon(QStringLiteral("copy")), tr("Duplicate"), this, [this, path, i] {
                    change([&](View &v) {
                        if (QList<FilterRule> *rules = rulesAt(v, path); rules && i < rules->size())
                            rules->insert(i + 1, (*rules)[i]);
                    });
                });
                if (!rule.group && depth < 2)
                    menu->addAction(Icons::icon(QStringLiteral("group")), tr("Turn into group"), this, [this, path, i] {
                        change([&](View &v) {
                            if (QList<FilterRule> *rules = rulesAt(v, path); rules && i < rules->size()) {
                                FilterRule g;
                                g.group = true;
                                g.rules = {(*rules)[i]};
                                (*rules)[i] = g;
                            }
                        });
                    });
                menu->popup(more->mapToGlobal(QPoint(0, more->height())));
            });
            if (rule.group) {
                auto *frame = new QFrame;
                frame->setObjectName(QStringLiteral("filterGroup"));
                frame->setStyleSheet(QStringLiteral("#filterGroup { background: rgba(255,255,255,0.025); border: 1px solid #2a2a31; border-radius: 8px; }"));
                auto *inner = new QVBoxLayout(frame);
                inner->setContentsMargins(8, 8, 8, 8);
                inner->setSpacing(6);
                addGroup(inner, s, v, here, depth + 1);
                row->addWidget(frame, 1);
                row->addWidget(more, 0, Qt::AlignTop);
                layout->addLayout(row);
                continue;
            }
            auto *propBox = combo(130);
            for (const Property &p : s.props)
                if (!filterOps(resultType(p)).isEmpty())
                    propBox->addItem(Icons::icon(propTypeIcon(p.type)), p.name, p.id);
            propBox->setCurrentIndex(qMax(0, propBox->findData(rule.prop)));
            QObject::connect(propBox, &QComboBox::activated, this, [this, propBox, path, i] {
                const QString id = propBox->currentData().toString();
                change([&](View &v) {
                    QList<FilterRule> *rules = rulesAt(v, path);
                    const Property *p = Workspace::instance()->schema(m_db).prop(id);
                    if (!rules || i >= rules->size() || !p)
                        return;
                    FilterRule &r = (*rules)[i];
                    r.prop = id;
                    r.op = filterOps(resultType(*p)).value(0, QStringLiteral("notempty"));
                    r.value = QJsonValue();
                });
            });
            row->addWidget(propBox);
            if (const Property *p = s.prop(rule.prop)) {
                auto *opBox = combo();
                for (const QString &o : filterOps(resultType(*p)))
                    opBox->addItem(filterOpName(o), o);
                opBox->setCurrentIndex(qMax(0, opBox->findData(rule.op)));
                QObject::connect(opBox, &QComboBox::activated, this, [this, opBox, path, i] {
                    const QString op = opBox->currentData().toString();
                    change([&](View &v) {
                        if (QList<FilterRule> *rules = rulesAt(v, path); rules && i < rules->size()) {
                            const bool wasArray = (*rules)[i].op == QLatin1String("between") || (*rules)[i].op == QLatin1String("ingroup");
                            (*rules)[i].op = op;
                            if (wasArray || op == QLatin1String("between") || op == QLatin1String("ingroup"))
                                (*rules)[i].value = QJsonValue();
                        }
                    });
                });
                row->addWidget(opBox);
                if (QWidget *val = valueEditor(*p, path, i, rule))
                    row->addWidget(val);
            }
            row->addStretch();
            row->addWidget(more);
            layout->addLayout(row);
        }
        auto *adds = new QHBoxLayout;
        adds->setSpacing(4);
        auto *addRule = new FlatButton(QStringLiteral("plus"), tr("Add filter rule"));
        addRule->setTextSize(12.5);
        QObject::connect(addRule, &FlatButton::clicked, this, [this, path, s] {
            change([&](View &v) {
                if (QList<FilterRule> *rules = rulesAt(v, path))
                    rules->append(newRule(s));
            });
        });
        adds->addWidget(addRule);
        if (depth < 2) {
            auto *addGroupBtn = new FlatButton(QStringLiteral("group"), tr("Add filter group"));
            addGroupBtn->setTextSize(12.5);
            QObject::connect(addGroupBtn, &FlatButton::clicked, this, [this, path, s] {
                change([&](View &v) {
                    if (QList<FilterRule> *rules = rulesAt(v, path)) {
                        FilterRule g;
                        g.group = true;
                        g.rules = {newRule(s)};
                        rules->append(g);
                    }
                });
            });
            adds->addWidget(addGroupBtn);
        }
        adds->addStretch();
        layout->addLayout(adds);
    }

    void rebuild()
    {
        clearLayout(m_layout);
        const Schema s = Workspace::instance()->schema(m_db);
        const View v = m_get();
        auto *title = caption(tr("Filters"));
        m_layout->addWidget(title);
        if (v.filters.isEmpty()) {
            auto *empty = new QLabel(tr("No filters yet. Filters show only the rows that match."));
            empty->setFont(Theme::uiFont(12.5));
            empty->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 2px 0 6px 0;"));
            m_layout->addWidget(empty);
        }
        addGroup(m_layout, s, v, {}, 0);
        if (!v.filters.isEmpty()) {
            auto *clear = new FlatButton(QStringLiteral("trash"), tr("Delete filter"));
            clear->setTextSize(12.5);
            QObject::connect(clear, &FlatButton::clicked, this, [this] { change([](View &v) { v.filters.clear(); v.filterAny = false; }); });
            m_layout->addWidget(clear, 0, Qt::AlignLeft);
        }
        QTimer::singleShot(0, this, [this] { adjustSize(); });
    }

    QString m_db;
    DbPopovers::ViewGet m_get;
    DbPopovers::ViewEdit m_edit;
    QVBoxLayout *m_layout;
};

// ---- sorts ---------------------------------------------------------------------------

class SortPopover : public Popover
{
public:
    SortPopover(QWidget *parent, const QString &dbId, DbPopovers::ViewGet get, DbPopovers::ViewEdit edit)
        : Popover(parent)
        , m_db(dbId)
        , m_get(std::move(get))
        , m_edit(std::move(edit))
    {
        setObjectName(QStringLiteral("sortPopover"));
        m_layout = new QVBoxLayout(content());
        m_layout->setContentsMargins(12, 10, 12, 10);
        m_layout->setSpacing(6);
        rebuild();
    }

private:
    void change(const std::function<void(View &)> &fn)
    {
        m_edit(fn);
        QTimer::singleShot(0, this, [this] { rebuild(); });
    }
    void rebuild()
    {
        clearLayout(m_layout);
        const Schema s = Workspace::instance()->schema(m_db);
        const View v = m_get();
        m_layout->addWidget(caption(tr("Sorts")));
        if (v.sorts.isEmpty()) {
            auto *empty = new QLabel(tr("No sorts yet. Rows keep their manual order."));
            empty->setFont(Theme::uiFont(12.5));
            empty->setStyleSheet(QStringLiteral("color: #6b6b75; padding: 2px 0 6px 0;"));
            m_layout->addWidget(empty);
        }
        for (int i = 0; i < v.sorts.size(); ++i) {
            auto *row = new QHBoxLayout;
            row->setSpacing(6);
            if (v.sorts.size() > 1) {
                auto *up = new FlatButton(QStringLiteral("arrow-up"));
                up->setToolTip(tr("Sort by this first"));
                up->setEnabled(i > 0);
                QObject::connect(up, &FlatButton::clicked, this, [this, i] { change([i](View &v) { v.sorts.swapItemsAt(i, i - 1); }); });
                row->addWidget(up);
            }
            auto *propBox = combo(150);
            for (const Property &p : s.props)
                if (p.type != PropType::Button)
                    propBox->addItem(Icons::icon(propTypeIcon(p.type)), p.name, p.id);
            propBox->setCurrentIndex(qMax(0, propBox->findData(v.sorts[i].prop)));
            QObject::connect(propBox, &QComboBox::activated, this, [this, propBox, i] {
                const QString id = propBox->currentData().toString();
                change([&](View &v) { v.sorts[i].prop = id; });
            });
            row->addWidget(propBox);
            auto *dir = combo();
            dir->addItem(tr("Ascending"));
            dir->addItem(tr("Descending"));
            dir->setCurrentIndex(v.sorts[i].descending ? 1 : 0);
            QObject::connect(dir, &QComboBox::activated, this, [this, i](int idx) { m_edit([&](View &v) { v.sorts[i].descending = idx == 1; }); });
            row->addWidget(dir);
            auto *remove = new FlatButton(QStringLiteral("close"));
            QObject::connect(remove, &FlatButton::clicked, this, [this, i] { change([i](View &v) { v.sorts.removeAt(i); }); });
            row->addWidget(remove);
            m_layout->addLayout(row);
        }
        auto *add = new FlatButton(QStringLiteral("plus"), tr("Add sort"));
        add->setTextSize(12.5);
        QObject::connect(add, &FlatButton::clicked, this, [this, s] {
            change([&](View &v) {
                // The first property not sorted yet.
                QString id = s.props.isEmpty() ? QString() : s.props.first().id;
                for (const Property &p : s.props) {
                    bool used = false;
                    for (const SortRule &r : v.sorts)
                        used |= r.prop == p.id;
                    if (!used) {
                        id = p.id;
                        break;
                    }
                }
                v.sorts.append({id, false});
            });
        });
        m_layout->addWidget(add, 0, Qt::AlignLeft);
        if (!v.sorts.isEmpty()) {
            auto *clear = new FlatButton(QStringLiteral("trash"), tr("Delete sort"));
            clear->setTextSize(12.5);
            QObject::connect(clear, &FlatButton::clicked, this, [this] { change([](View &v) { v.sorts.clear(); }); });
            m_layout->addWidget(clear, 0, Qt::AlignLeft);
        }
        QTimer::singleShot(0, this, [this] { adjustSize(); });
    }
    QString m_db;
    DbPopovers::ViewGet m_get;
    DbPopovers::ViewEdit m_edit;
    QVBoxLayout *m_layout;
};

// ---- settings ------------------------------------------------------------------------

class SettingsPopover : public Popover
{
public:
    SettingsPopover(DatabaseBody *body, const QString &page)
        : Popover(body->window())
        , m_body(body)
        , m_page(page)
    {
        setObjectName(QStringLiteral("settingsPopover"));
        auto *outer = new QVBoxLayout(content());
        outer->setContentsMargins(0, 0, 0, 0);
        m_scroll = new QScrollArea;
        m_scroll->setFrameStyle(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_scroll->setWidgetResizable(true);
        m_scroll->viewport()->setAutoFillBackground(false);
        auto *inner = new QWidget;
        inner->setAutoFillBackground(false);
        m_layout = new QVBoxLayout(inner);
        m_layout->setContentsMargins(10, 10, 10, 10);
        m_layout->setSpacing(2);
        m_scroll->setWidget(inner);
        inner->setAutoFillBackground(false); // setWidget turns it on
        outer->addWidget(m_scroll);
        rebuild();
    }

    void openPage(const QString &page)
    {
        m_page = page;
        QTimer::singleShot(0, this, [this] { rebuild(); });
    }

private:
    View view() const { return m_body ? m_body->currentView() : View(); }
    void edit(const std::function<void(View &)> &fn, bool rebuildAfter = false)
    {
        if (!m_body)
            return;
        m_body->editView(fn);
        if (rebuildAfter)
            QTimer::singleShot(0, this, [this] { rebuild(); });
    }
    Schema schema() const { return Workspace::instance()->schema(m_body->databaseId()); }
    void editSchema(const std::function<void(Schema &)> &fn)
    {
        Workspace *ws = Workspace::instance();
        Schema s = ws->schema(m_body->databaseId());
        fn(s);
        ws->setSchema(m_body->databaseId(), s);
        QTimer::singleShot(0, this, [this] { rebuild(); });
    }

    // A menu row: icon, label and value; `chevron` when it opens a page.
    MenuRow *row(const QString &icon, const QString &label, const QString &value, std::function<void()> fn, bool chevron = false)
    {
        auto *b = new MenuRow(icon, label, value, chevron);
        b->setObjectName(QStringLiteral("settingsRow"));
        QObject::connect(b, &MenuRow::clicked, this, std::move(fn));
        m_layout->addWidget(b);
        return b;
    }
    MenuRow *pageRow(const QString &icon, const QString &label, const QString &value, const QString &page)
    {
        return row(icon, label, value, [this, page] { openPage(page); }, true);
    }
    // A row with a switch on the right; clicking anywhere on it flips it.
    Switch *toggle(const QString &icon, const QString &label, bool on, std::function<void(bool)> fn)
    {
        auto *r = new MenuRow(icon, label, QString(), false);
        auto *sw = new Switch(r);
        sw->setChecked(on);
        auto *h = new QHBoxLayout(r);
        h->setContentsMargins(0, 0, 8, 0);
        h->addStretch();
        h->addWidget(sw, 0, Qt::AlignVCenter);
        QObject::connect(r, &MenuRow::clicked, sw, &Switch::click);
        m_layout->addWidget(r);
        QObject::connect(sw, &Switch::toggled, this, std::move(fn));
        return sw;
    }
    Switch *toggle(const QString &label, bool on, std::function<void(bool)> fn) { return toggle(QString(), label, on, std::move(fn)); }
    QComboBox *choice(const QString &label, const QList<QPair<QString, QString>> &items, const QString &current, std::function<void(const QString &)> fn)
    {
        m_layout->addWidget(caption(label));
        auto *box = combo(280);
        for (const auto &it : items)
            box->addItem(it.second, it.first);
        box->setCurrentIndex(qMax(0, box->findData(current)));
        m_layout->addWidget(box);
        QObject::connect(box, &QComboBox::activated, this, [box, fn] { fn(box->currentData().toString()); });
        return box;
    }
    void back(const QString &title)
    {
        auto *h = new QHBoxLayout;
        auto *b = new FlatButton(QStringLiteral("chevron-left"));
        b->setToolTip(tr("Back"));
        QObject::connect(b, &FlatButton::clicked, this, [this] { openPage(QString()); });
        auto *l = new QLabel(title);
        l->setFont(Theme::uiFont(14, QFont::DemiBold));
        l->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
        h->addWidget(b);
        h->addWidget(l, 1);
        m_layout->addLayout(h);
        m_layout->addSpacing(4);
    }

    QList<QPair<QString, QString>> propsWhere(const Schema &s, const std::function<bool(const Property &)> &pred, bool none = false, const QString &noneLabel = {})
    {
        QList<QPair<QString, QString>> out;
        if (none)
            out.append({QString(), noneLabel.isEmpty() ? tr("None") : noneLabel});
        for (const Property &p : s.props)
            if (pred(p))
                out.append({p.id, p.name});
        return out;
    }

    void mainPage(const Schema &s, const View &v)
    {
        auto *name = new QLineEdit(v.name);
        name->setFont(Theme::uiFont(13.5, QFont::Medium));
        name->setMinimumWidth(300);
        name->setEnabled(!s.locked);
        QObject::connect(name, &QLineEdit::editingFinished, this, [this, name] {
            const QString n = name->text().trimmed();
            if (!n.isEmpty())
                edit([n](View &v) { v.name = n; });
        });
        m_layout->addWidget(name);
        m_layout->addSpacing(6);
        const bool locked = s.locked;
        if (!locked)
            pageRow(viewTypeIcon(v.type), tr("Layout"), viewTypeName(v.type), QStringLiteral("layout"));
        int shown = 0;
        for (const Property *p : s.orderedProps(v))
            if (p->type != PropType::Title)
                ++shown;
        if (!locked && v.type != ViewType::Chart)
            pageRow(QStringLiteral("list"), tr("Properties"), tr("%n shown", nullptr, shown), QStringLiteral("properties"));
        if (v.type != ViewType::Form) {
            row(QStringLiteral("filter"), tr("Filter"), v.filters.isEmpty() ? QString() : tr("%n rule(s)", nullptr, filterRuleCount(v.filters)), [this] {
                const QPoint pos = mapToGlobal(QPoint(20, 40));
                DatabaseBody *body = m_body;
                close();
                if (body)
                    DbPopovers::showFilters(body->window(), pos, body->databaseId(), [body] { return body->currentView(); },
                                            [body](const std::function<void(View &)> &fn) { body->editView(fn); });
            }, true);
            row(QStringLiteral("sort"), tr("Sort"), v.sorts.isEmpty() ? QString() : tr("%n sort(s)", nullptr, int(v.sorts.size())), [this] {
                const QPoint pos = mapToGlobal(QPoint(20, 40));
                DatabaseBody *body = m_body;
                close();
                if (body)
                    DbPopovers::showSorts(body->window(), pos, body->databaseId(), [body] { return body->currentView(); },
                                          [body](const std::function<void(View &)> &fn) { body->editView(fn); });
            }, true);
        }
        const bool groupable = v.type == ViewType::Table || v.type == ViewType::Board || v.type == ViewType::List || v.type == ViewType::Gallery
                               || v.type == ViewType::Timeline;
        if (groupable && !locked) {
            const Property *g = s.prop(v.groupBy);
            pageRow(QStringLiteral("group"), tr("Group"), g ? g->name : tr("None"), QStringLiteral("group"));
        }
        if ((v.type == ViewType::Table || v.type == ViewType::List || v.type == ViewType::Board) && !locked) {
            const QString mode = s.subItemsProp.isEmpty() ? tr("Off")
                                 : v.subItems == QLatin1String("flat")    ? tr("Flattened")
                                 : v.subItems == QLatin1String("parents") ? tr("Parents only")
                                 : v.subItems == QLatin1String("off")     ? tr("Plain rows")
                                                                          : tr("Nested");
            pageRow(QStringLiteral("subitems"), tr("Sub-items"), mode, QStringLiteral("subitems"));
        }
        if (v.type == ViewType::Timeline && !locked)
            toggle(QStringLiteral("dependency"), tr("Dependencies"), !s.dependencyProp.isEmpty(), [this](bool on) {
                Workspace::instance()->enableDependencies(m_body->databaseId(), on);
                QTimer::singleShot(0, this, [this] { rebuild(); });
            });
        if (m_body->isInline() && v.type != ViewType::Calendar && v.type != ViewType::Timeline && v.type != ViewType::Chart && v.type != ViewType::Form)
            pageRow(QStringLiteral("number"), tr("Load limit"), QString::number(v.loadLimit), QStringLiteral("limit"));
        if (v.type != ViewType::Form && v.type != ViewType::Chart) {
            const QString open = v.openIn == QLatin1String("center") ? tr("Center peek") : v.openIn == QLatin1String("full") ? tr("Full page") : tr("Side peek");
            pageRow(v.openIn == QLatin1String("center") ? QStringLiteral("peek-center") : QStringLiteral("peek-side"), tr("Open pages in"), open,
                    QStringLiteral("openin"));
        }
        m_layout->addSpacing(6);
        auto *sep = new QFrame;
        sep->setFixedHeight(1);
        sep->setStyleSheet(QStringLiteral("background: #26262d;"));
        m_layout->addWidget(sep);
        toggle(QStringLiteral("lock"), tr("Lock database"), s.locked, [this](bool on) { editSchema([on](Schema &s) { s.locked = on; }); });
        const QString viewId = v.id;
        row(QStringLiteral("link"), tr("Copy link to view"), QString(), [this, viewId] {
            QApplication::clipboard()->setText(QStringLiteral("nebula://page/%1?v=%2").arg(m_body->databaseId(), viewId));
            close();
        });
        if (!locked) {
            row(QStringLiteral("copy"), tr("Duplicate view"), QString(), [this, viewId] {
                m_body->duplicateView(viewId);
                close();
            });
            if (m_body->views().size() > 1)
                row(QStringLiteral("trash"), tr("Delete view"), QString(), [this, viewId] {
                    m_body->deleteView(viewId);
                    close();
                });
        }
    }

    void layoutPage(const Schema &s, const View &v)
    {
        back(tr("Layout"));
        auto *grid = new QGridLayout;
        grid->setSpacing(6);
        const QList<ViewType> types = {ViewType::Table, ViewType::Board, ViewType::Timeline, ViewType::Calendar, ViewType::List,
                                       ViewType::Gallery, ViewType::Chart, ViewType::Feed, ViewType::Form};
        for (int i = 0; i < types.size(); ++i) {
            const ViewType t = types[i];
            auto *b = new FlatButton(viewTypeIcon(t), viewTypeName(t));
            b->setCheckable(true);
            b->setChecked(v.type == t);
            b->setTextSize(12.5);
            b->setMinimumWidth(96);
            b->setObjectName(QStringLiteral("layout-") + viewTypeKey(t));
            grid->addWidget(b, i / 3, i % 3);
            QObject::connect(b, &FlatButton::clicked, this, [this, t, s] {
                edit(
                    [t, s](View &v) {
                        if (v.name == viewTypeName(v.type))
                            v.name = viewTypeName(t);
                        v.type = t;
                        if (t == ViewType::Board && v.groupBy.isEmpty())
                            v.groupBy = s.defaultGroupProp();
                        if ((t == ViewType::Calendar || t == ViewType::Timeline) && v.dateProp.isEmpty())
                            v.dateProp = s.defaultDateProp();
                        if (t == ViewType::Chart && v.chartX.isEmpty())
                            v.chartX = s.defaultGroupProp();
                    },
                    true);
            });
        }
        m_layout->addLayout(grid);
        m_layout->addSpacing(6);
        auto cardPreviews = [&]() {
            QList<QPair<QString, QString>> items{{QStringLiteral("none"), tr("None")}, {QStringLiteral("cover"), tr("Page cover")},
                                                 {QStringLiteral("content"), tr("Page content")}};
            for (const Property &p : s.props)
                if (p.type == PropType::Files)
                    items.append({p.id, p.name});
            return items;
        };
        auto sizes = QList<QPair<QString, QString>>{{QStringLiteral("small"), tr("Small")}, {QStringLiteral("medium"), tr("Medium")}, {QStringLiteral("large"), tr("Large")}};
        auto isDate = [](const Property &p) { return resultType(p) == PropType::Date || p.type == PropType::Created || p.type == PropType::Edited; };
        switch (v.type) {
        case ViewType::Table: {
            toggle(tr("Wrap all columns"), v.wrapCells, [this](bool on) { edit([on](View &v) { v.wrapCells = on; }); });
            QList<QPair<QString, QString>> freeze{{QStringLiteral("0"), tr("None")}};
            const QList<const Property *> props = s.orderedProps(v);
            for (int i = 0; i < props.size() && i < 4; ++i)
                freeze.append({QString::number(i + 1), i == 0 ? tr("Up to %1").arg(props[i]->name) : tr("Up to %1").arg(props[i]->name)});
            choice(tr("Freeze columns"), freeze, QString::number(v.frozen), [this](const QString &k) { edit([k](View &v) { v.frozen = k.toInt(); }); });
            break;
        }
        case ViewType::Board:
            choice(tr("Group by"), propsWhere(s, [](const Property &p) { return canGroupBy(p); }), v.groupBy,
                   [this](const QString &k) { edit([k](View &v) { v.groupBy = k; }); });
            choice(tr("Sub-group by"), propsWhere(s, [&v](const Property &p) { return canGroupBy(p) && p.id != v.groupBy; }, true), v.subGroupBy,
                   [this](const QString &k) { edit([k](View &v) { v.subGroupBy = k; }); });
            choice(tr("Card preview"), cardPreviews(), v.cardPreview, [this](const QString &k) { edit([k](View &v) { v.cardPreview = k; }); });
            choice(tr("Card size"), sizes, v.cardSize, [this](const QString &k) { edit([k](View &v) { v.cardSize = k; }); });
            toggle(tr("Fit image"), v.cardFit, [this](bool on) { edit([on](View &v) { v.cardFit = on; }); });
            toggle(tr("Color columns"), v.colorColumns, [this](bool on) { edit([on](View &v) { v.colorColumns = on; }); });
            break;
        case ViewType::Gallery:
            choice(tr("Card preview"), cardPreviews(), v.cardPreview, [this](const QString &k) { edit([k](View &v) { v.cardPreview = k; }); });
            choice(tr("Card size"), sizes, v.cardSize, [this](const QString &k) { edit([k](View &v) { v.cardSize = k; }); });
            toggle(tr("Fit image"), v.cardFit, [this](bool on) { edit([on](View &v) { v.cardFit = on; }); });
            break;
        case ViewType::Calendar:
            choice(tr("Show calendar by"), propsWhere(s, isDate), v.dateProp, [this](const QString &k) { edit([k](View &v) { v.dateProp = k; }); });
            choice(tr("End date"), propsWhere(s, [&](const Property &p) { return isDate(p) && p.id != v.dateProp; }, true, tr("Use the date's range")), v.endDateProp,
                   [this](const QString &k) { edit([k](View &v) { v.endDateProp = k; }); });
            toggle(tr("Show weekends"), v.showWeekends, [this](bool on) { edit([on](View &v) { v.showWeekends = on; }); });
            break;
        case ViewType::Timeline: {
            choice(tr("Show timeline by"), propsWhere(s, isDate), v.dateProp, [this](const QString &k) { edit([k](View &v) { v.dateProp = k; }); });
            choice(tr("End date"), propsWhere(s, [&](const Property &p) { return isDate(p) && p.id != v.dateProp; }, true, tr("Use the date's range")), v.endDateProp,
                   [this](const QString &k) { edit([k](View &v) { v.endDateProp = k; }); });
            toggle(tr("Show weekends"), v.showWeekends, [this](bool on) { edit([on](View &v) { v.showWeekends = on; }); });
            toggle(tr("Show table"), v.timelineTable, [this](bool on) { edit([on](View &v) { v.timelineTable = on; }, true); });
            if (v.timelineTable) {
                m_layout->addWidget(caption(tr("Table properties")));
                for (const Property &p : s.props) {
                    if (p.type == PropType::Title)
                        continue;
                    const QString id = p.id;
                    toggle(p.name, v.timelineTableProps.contains(id), [this, id](bool on) {
                        edit([id, on](View &v) {
                            v.timelineTableProps.removeAll(id);
                            if (on)
                                v.timelineTableProps.append(id);
                        });
                    });
                }
            }
            break;
        }
        case ViewType::Chart: {
            choice(tr("Chart type"),
                   {{QStringLiteral("bar"), tr("Vertical bar")}, {QStringLiteral("hbar"), tr("Horizontal bar")}, {QStringLiteral("line"), tr("Line")},
                    {QStringLiteral("donut"), tr("Donut")}, {QStringLiteral("number"), tr("Number")}},
                   v.chartType, [this](const QString &k) { edit([k](View &v) { v.chartType = k; }, true); });
            if (v.chartType != QLatin1String("number"))
                choice(v.chartType == QLatin1String("donut") ? tr("Slices by") : tr("X axis"), propsWhere(s, [](const Property &p) { return canGroupBy(p); }), v.chartX,
                       [this](const QString &k) { edit([k](View &v) { v.chartX = k; }); });
            QList<QPair<QString, QString>> ys{{QStringLiteral("count"), tr("Count")}};
            for (const Property &p : s.props)
                if (resultType(p) == PropType::Number)
                    for (const QString &fn : {QStringLiteral("sum"), QStringLiteral("avg"), QStringLiteral("median"), QStringLiteral("min"), QStringLiteral("max")})
                        ys.append({fn + QLatin1Char(':') + p.id, QStringLiteral("%1 · %2").arg(calcName(fn), p.name)});
            choice(v.chartType == QLatin1String("number") ? tr("Value") : tr("Y axis"), ys, v.chartY, [this](const QString &k) { edit([k](View &v) { v.chartY = k; }); });
            if (v.chartType == QLatin1String("bar") || v.chartType == QLatin1String("hbar") || v.chartType == QLatin1String("line"))
                choice(tr("Group by"), propsWhere(s, [&v](const Property &p) { return canGroupBy(p) && p.id != v.chartX; }, true), v.chartGroup,
                       [this](const QString &k) { edit([k](View &v) { v.chartGroup = k; }); });
            if (v.chartType != QLatin1String("number")) {
                choice(tr("Sort"),
                       {{QStringLiteral("manual"), tr("Property order")}, {QStringLiteral("xasc"), tr("X axis ascending")}, {QStringLiteral("xdesc"), tr("X axis descending")},
                        {QStringLiteral("yasc"), tr("Y axis ascending")}, {QStringLiteral("ydesc"), tr("Y axis descending")}},
                       v.chartSort, [this](const QString &k) { edit([k](View &v) { v.chartSort = k; }); });
                toggle(tr("Cumulative"), v.chartCumulative, [this](bool on) { edit([on](View &v) { v.chartCumulative = on; }); });
                toggle(tr("Data labels"), v.chartLabels, [this](bool on) { edit([on](View &v) { v.chartLabels = on; }); });
                toggle(tr("Hide empty groups"), v.chartHideEmpty, [this](bool on) { edit([on](View &v) { v.chartHideEmpty = on; }); });
            }
            break;
        }
        case ViewType::Form: {
            auto *hint = new QLabel(tr("Edit the form's title, description and required questions in the form itself. "
                                       "Hidden properties don't become questions."));
            hint->setWordWrap(true);
            hint->setFixedWidth(300);
            hint->setStyleSheet(QStringLiteral("color: #8a8a94;"));
            m_layout->addWidget(hint);
            break;
        }
        default:
            break;
        }
        if (v.type != ViewType::Chart && v.type != ViewType::Form)
            toggle(tr("Show page icon"), v.showPageIcon, [this](bool on) { edit([on](View &v) { v.showPageIcon = on; }); });
    }

    void propertiesPage(const Schema &s, const View &v)
    {
        back(tr("Properties"));
        auto *h = new QHBoxLayout;
        auto *hideAll = new FlatButton(QStringLiteral("eye-off"), tr("Hide all"));
        auto *showAll = new FlatButton(QStringLiteral("eye"), tr("Show all"));
        h->addWidget(hideAll);
        h->addWidget(showAll);
        h->addStretch();
        m_layout->addLayout(h);
        QObject::connect(hideAll, &FlatButton::clicked, this, [this, s] {
            edit(
                [s](View &v) {
                    v.hidden.clear();
                    for (const Property &p : s.props)
                        if (p.type != PropType::Title)
                            v.hidden.append(p.id);
                },
                true);
        });
        QObject::connect(showAll, &FlatButton::clicked, this, [this] { edit([](View &v) { v.hidden.clear(); }, true); });
        for (const Property *p : s.orderedProps(v, true)) {
            if (p->type == PropType::Title)
                continue;
            auto *row = new QHBoxLayout;
            auto *icon = new QLabel;
            icon->setPixmap(Icons::pixmap(propTypeIcon(p->type), 16, Theme::colors().fog, devicePixelRatioF()));
            auto *nameLabel = new QLabel(p->name);
            nameLabel->setFont(Theme::uiFont(13.5));
            nameLabel->setStyleSheet(QStringLiteral("color: #c9c9d0;"));
            auto *sw = new Switch;
            sw->setChecked(!v.hidden.contains(p->id));
            row->addWidget(icon);
            row->addWidget(nameLabel, 1);
            row->addWidget(sw);
            m_layout->addLayout(row);
            const QString id = p->id;
            QObject::connect(sw, &Switch::toggled, this, [this, id](bool on) {
                edit([&](View &v) {
                    v.hidden.removeAll(id);
                    if (!on)
                        v.hidden.append(id);
                });
            });
        }
        auto *add = new FlatButton(QStringLiteral("plus"), tr("New property"));
        m_layout->addWidget(add, 0, Qt::AlignLeft);
        QObject::connect(add, &FlatButton::clicked, this, [this, add] {
            PropertyEditors::addProperty(this, add->mapToGlobal(QPoint(0, add->height())), m_body->databaseId(),
                                         m_body->isLinked() ? QString() : m_body->currentViewId());
            QTimer::singleShot(400, this, [this] { rebuild(); });
        });
    }

    void groupPage(const Schema &s, const View &v)
    {
        back(tr("Group"));
        choice(tr("Group by"), propsWhere(s, [](const Property &p) { return canGroupBy(p); }, true), v.groupBy,
               [this](const QString &k) { edit([k](View &v) {
                                              v.groupBy = k;
                                              v.groupMode.clear();
                                              v.hiddenGroups.clear();
                                          },
                                          true); });
        const Property *g = s.prop(v.groupBy);
        if (!g)
            return;
        const PropType t = resultType(*g);
        if (t == PropType::Date || g->type == PropType::Created || g->type == PropType::Edited)
            choice(tr("Date by"),
                   {{QStringLiteral("relative"), tr("Relative")}, {QStringLiteral("day"), tr("Day")}, {QStringLiteral("week"), tr("Week")},
                    {QStringLiteral("month"), tr("Month")}, {QStringLiteral("year"), tr("Year")}},
                   v.groupMode.isEmpty() ? QStringLiteral("relative") : v.groupMode, [this](const QString &k) { edit([k](View &v) { v.groupMode = k; }, true); });
        else if (t == PropType::Text || t == PropType::Title || g->type == PropType::Url || g->type == PropType::Email || g->type == PropType::Phone)
            choice(tr("Text by"), {{QStringLiteral("exact"), tr("Exact")}, {QStringLiteral("alpha"), tr("Alphabetical")}},
                   v.groupMode.isEmpty() ? QStringLiteral("exact") : v.groupMode, [this](const QString &k) { edit([k](View &v) { v.groupMode = k; }, true); });
        else if (t == PropType::Number) {
            m_layout->addWidget(caption(tr("Number range size")));
            auto *step = new QLineEdit(QString::number(v.groupStep));
            step->setFont(Theme::uiFont(13));
            m_layout->addWidget(step);
            QObject::connect(step, &QLineEdit::editingFinished, this, [this, step] {
                bool ok = false;
                const double d = step->text().toDouble(&ok);
                if (ok && d > 0)
                    edit([d](View &v) { v.groupStep = d; });
            });
        }
        toggle(tr("Hide empty groups"), v.hideEmptyGroups, [this](bool on) { edit([on](View &v) { v.hideEmptyGroups = on; }, true); });
        m_layout->addWidget(caption(tr("Groups")));
        QList<RowRef> rows;
        Workspace *ws = Workspace::instance();
        for (const PageMeta *m : ws->rows(m_body->databaseId()))
            rows.append({m, ws->props(m->id)});
        for (const RowGroup &grp : groupRows(s, *g, rows, v.groupMode, v.groupStep, v.hideEmptyGroups)) {
            const QString key = grp.key;
            toggle(QStringLiteral("%1   %2").arg(grp.label).arg(grp.rows.size()), !v.hiddenGroups.contains(key), [this, key](bool on) {
                edit([key, on](View &v) {
                    v.hiddenGroups.removeAll(key);
                    if (!on)
                        v.hiddenGroups.append(key);
                });
            });
        }
    }

    void subItemsPage(const Schema &s, const View &v)
    {
        back(tr("Sub-items"));
        if (s.subItemsProp.isEmpty()) {
            auto *hint = new QLabel(tr("Sub-items nest pages under a parent item, using a “Parent item” relation."));
            hint->setWordWrap(true);
            hint->setFixedWidth(300);
            hint->setStyleSheet(QStringLiteral("color: #8a8a94;"));
            m_layout->addWidget(hint);
            auto *on = new FlatButton(QStringLiteral("subitems"), tr("Turn on sub-items"));
            on->setFilled(true);
            on->setObjectName(QStringLiteral("enableSubItems"));
            QObject::connect(on, &FlatButton::clicked, this, [this] {
                Workspace::instance()->enableSubItems(m_body->databaseId(), true);
                QTimer::singleShot(0, this, [this] { rebuild(); });
            });
            m_layout->addWidget(on, 0, Qt::AlignLeft);
            return;
        }
        choice(tr("Show sub-items"),
               {{QStringLiteral("nested"), tr("Nested in toggle")}, {QStringLiteral("flat"), tr("Flattened list")},
                {QStringLiteral("parents"), tr("Parent items only")}, {QStringLiteral("off"), tr("As plain rows")}},
               v.subItems, [this](const QString &k) { edit([k](View &v) { v.subItems = k; }); });
        auto *off = new FlatButton(QStringLiteral("trash"), tr("Turn off sub-items"));
        QObject::connect(off, &FlatButton::clicked, this, [this] {
            Workspace::instance()->enableSubItems(m_body->databaseId(), false);
            QTimer::singleShot(0, this, [this] { rebuild(); });
        });
        m_layout->addWidget(off, 0, Qt::AlignLeft);
    }

    void listPage(const QString &title, const QList<QPair<QString, QString>> &items, const QString &current, std::function<void(const QString &)> fn)
    {
        back(title);
        for (const auto &it : items) {
            auto *b = new FlatButton(it.first == current ? QStringLiteral("check") : QStringLiteral("dot"), it.second);
            b->setTextSize(13.5);
            b->setMinimumWidth(280);
            const QString key = it.first;
            QObject::connect(b, &FlatButton::clicked, this, [this, fn, key] {
                fn(key);
                openPage(QString());
            });
            m_layout->addWidget(b);
        }
    }

    void rebuild()
    {
        if (!m_body)
            return;
        clearLayout(m_layout);
        const Schema s = schema();
        const View v = view();
        if (m_page == QLatin1String("layout"))
            layoutPage(s, v);
        else if (m_page == QLatin1String("properties"))
            propertiesPage(s, v);
        else if (m_page == QLatin1String("group"))
            groupPage(s, v);
        else if (m_page == QLatin1String("subitems"))
            subItemsPage(s, v);
        else if (m_page == QLatin1String("limit"))
            listPage(tr("Load limit"),
                     {{QStringLiteral("10"), tr("10 pages")}, {QStringLiteral("25"), tr("25 pages")}, {QStringLiteral("50"), tr("50 pages")},
                      {QStringLiteral("100"), tr("100 pages")}, {QStringLiteral("200"), tr("200 pages")}},
                     QString::number(v.loadLimit), [this](const QString &k) { edit([k](View &v) { v.loadLimit = k.toInt(); }); });
        else if (m_page == QLatin1String("openin"))
            listPage(tr("Open pages in"),
                     {{QStringLiteral("side"), tr("Side peek")}, {QStringLiteral("center"), tr("Center peek")}, {QStringLiteral("full"), tr("Full page")}}, v.openIn,
                     [this](const QString &k) { edit([k](View &v) { v.openIn = k; }); });
        else
            mainPage(s, v);
        m_layout->addStretch();
        QTimer::singleShot(0, this, [this] {
            const QSize hint = m_layout->sizeHint();
            m_scroll->setFixedSize(hint.width() + 4, qMin(hint.height() + 2, 560));
            // Grow or shrink with the page (a top-level adjustSize won't shrink).
            content()->layout()->activate();
            layout()->activate();
            resize(sizeHint());
        });
    }

    QPointer<DatabaseBody> m_body;
    QString m_page;
    QScrollArea *m_scroll;
    QVBoxLayout *m_layout;
};
} // namespace

namespace DbPopovers
{
void showFilters(QWidget *parent, const QPoint &globalPos, const QString &dbId, ViewGet get, ViewEdit edit)
{
    (new FilterPopover(parent, dbId, std::move(get), std::move(edit)))->popupAt(globalPos);
}

void showSorts(QWidget *parent, const QPoint &globalPos, const QString &dbId, ViewGet get, ViewEdit edit)
{
    (new SortPopover(parent, dbId, std::move(get), std::move(edit)))->popupAt(globalPos);
}

void showSettings(DatabaseBody *body, const QPoint &globalPos, const QString &page)
{
    (new SettingsPopover(body, page))->popupAt(globalPos);
}

QString describeRule(const Schema &schema, const FilterRule &rule)
{
    if (rule.group)
        return tr("%n rule(s)", nullptr, filterRuleCount(rule.rules));
    const Property *p = schema.prop(rule.prop);
    if (!p)
        return tr("Filter");
    const QString op = rule.op;
    if (op == QLatin1String("checked"))
        return tr("%1: Checked").arg(p->name);
    if (op == QLatin1String("unchecked"))
        return tr("%1: Unchecked").arg(p->name);
    if (!filterNeedsValue(op))
        return QStringLiteral("%1: %2").arg(p->name, filterOpName(op));
    if (op == QLatin1String("ingroup")) {
        const int g = rule.value.toInt();
        return tr("%1: %2").arg(p->name, g == 2 ? tr("Complete") : g == 1 ? tr("In progress") : tr("To-do"));
    }
    const QString value = valueText(schema, *p, rule.value);
    if (value.isEmpty())
        return QStringLiteral("%1: %2 …").arg(p->name, filterOpName(op));
    // "is" reads as just the value, like Notion's chips.
    if (op == QLatin1String("is") || op == QLatin1String("contains"))
        return QStringLiteral("%1: %2").arg(p->name, value);
    return QStringLiteral("%1: %2 %3").arg(p->name, filterOpName(op), value);
}

void bulkEdit(QWidget *parent, const QPoint &globalPos, const QString &dbId, const QStringList &rowIds)
{
    Workspace *ws = Workspace::instance();
    const Schema s = ws->schema(dbId);
    QMenu *menu = Theme::makeMenu(parent);
    Theme::addMenuSection(menu, tr("Edit property for %n page(s)", nullptr, int(rowIds.size())));
    auto setAll = [dbId, rowIds](const QString &propId, const QJsonValue &value) {
        Workspace *ws = Workspace::instance();
        ws->batch([&] {
            for (const QString &id : rowIds)
                ws->setProp(id, propId, value);
        });
    };
    for (const Property &p : s.props) {
        if (propTypeIsComputed(p.type) || p.type == PropType::Title || p.type == PropType::Files || p.type == PropType::Button)
            continue;
        QMenu *sub = menu->addMenu(Icons::icon(propTypeIcon(p.type)), p.name);
        Theme::polishMenu(sub);
        const QString id = p.id;
        switch (p.type) {
        case PropType::Select:
        case PropType::Status:
            for (const Option &o : p.options)
                sub->addAction(o.name, parent, [setAll, id, o] { setAll(id, o.id); });
            break;
        case PropType::MultiSelect:
            for (const Option &o : p.options)
                sub->addAction(tr("Add %1").arg(o.name), parent, [id, o, rowIds] {
                    Workspace *ws = Workspace::instance();
                    ws->batch([&] {
                        for (const QString &row : rowIds) {
                            QJsonArray arr = ws->props(row).value(id).toArray();
                            if (!arr.contains(o.id))
                                arr.append(o.id);
                            ws->setProp(row, id, arr);
                        }
                    });
                });
            break;
        case PropType::Checkbox:
            sub->addAction(tr("Checked"), parent, [setAll, id] { setAll(id, true); });
            sub->addAction(tr("Unchecked"), parent, [setAll, id] { setAll(id, false); });
            break;
        case PropType::Person:
            for (const Workspace::Person &person : ws->people())
                sub->addAction(person.name, parent, [setAll, id, person] { setAll(id, QJsonArray{person.id}); });
            break;
        default:
            sub->addAction(tr("Set value…"), parent, [parent, setAll, id, dbId] {
                QString text;
                if (!textDialog(parent->window(), tr("Set value"), tr("The new value for every selected page."), &text))
                    return;
                Workspace *ws = Workspace::instance();
                Schema s = ws->schema(dbId);
                Property *prop = s.prop(id);
                if (!prop)
                    return;
                const QJsonValue v = parseValue(*prop, text, true);
                ws->setSchema(dbId, s);
                setAll(id, v);
            });
            break;
        }
        sub->addSeparator();
        sub->addAction(Icons::icon(QStringLiteral("close")), tr("Clear"), parent, [setAll, id] { setAll(id, QJsonValue()); });
    }
    menu->popup(globalPos);
}
} // namespace DbPopovers
