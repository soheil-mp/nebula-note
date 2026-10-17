// The form layout: a question per visible property; each answer becomes a
// row. While the database is editable, the form's title, description and
// required questions are edited in place.
#include "DbViews.h"
#include "DbViewsPrivate.h"

#include "core/DateParse.h"
#include "core/Workspace.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QVBoxLayout>

using namespace DbViewsPrivate;

namespace
{
bool answerable(PropType t)
{
    switch (t) {
    case PropType::Formula:
    case PropType::Rollup:
    case PropType::Created:
    case PropType::Edited:
    case PropType::CreatedBy:
    case PropType::EditedBy:
    case PropType::UniqueId:
    case PropType::Button:
        return false;
    default:
        return true;
    }
}

QString inputStyle()
{
    return QStringLiteral("QPlainTextEdit { background: #141418; color: #e6e6ec; border: 1px solid #2c2c34; border-radius: 7px; padding: 4px 6px; }"
                          "QPlainTextEdit:focus { border-color: #5a5a66; }");
}
} // namespace

FormView::FormView(QWidget *parent)
    : DbView(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new QWidget;
    card->setObjectName(QStringLiteral("formCard"));
    card->setMaximumWidth(680);
    card->setStyleSheet(QStringLiteral("#formCard { background: #17171b; border: 1px solid #26262d; border-radius: 14px; }") + inputStyle());
    outer->addWidget(card, 0, Qt::AlignLeft | Qt::AlignTop);
    m_layout = new QVBoxLayout(card);
    m_layout->setContentsMargins(32, 28, 32, 28);
    m_layout->setSpacing(10);
    card->setMinimumWidth(520);
}

void FormView::setContext(const ViewContext &ctx)
{
    DbView::setContext(ctx);
    // Rebuild only when the questions change, so typed answers survive edits elsewhere.
    QJsonObject sig;
    QJsonArray props;
    for (const Property *p : m_ctx.schema.orderedProps(m_ctx.view))
        if (answerable(p->type))
            props.append(QStringLiteral("%1|%2|%3|%4|%5|%6")
                             .arg(p->id, p->name, propTypeKey(p->type), p->description)
                             .arg(p->options.size())
                             .arg(m_ctx.view.formRequired.contains(p->id)));
    sig.insert(QStringLiteral("p"), props);
    sig.insert(QStringLiteral("t"), m_ctx.view.formTitle);
    sig.insert(QStringLiteral("d"), m_ctx.view.formDescription);
    sig.insert(QStringLiteral("e"), m_ctx.editable);
    const QString built = QString::fromUtf8(QJsonDocument(sig).toJson(QJsonDocument::Compact));
    if (built == m_builtFor)
        return;
    m_builtFor = built;
    rebuild();
}

void FormView::rebuild()
{
    m_inputs.clear();
    m_values.clear();
    m_done = nullptr;
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
    const auto &c = Theme::colors();
    const QString dbTitle = Workspace::instance()->title(m_ctx.databaseId);
    // Title and description.
    if (m_ctx.editable) {
        auto *title = new QLineEdit(m_ctx.view.formTitle.isEmpty() ? dbTitle : m_ctx.view.formTitle);
        title->setFont(Theme::displayFont(24, QFont::Bold));
        title->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; color: #f1f1f5; }"));
        title->setPlaceholderText(tr("Form title"));
        connect(title, &QLineEdit::editingFinished, this, [this, title] {
            const QString t = title->text();
            saveView([t](View &v) { v.formTitle = t; });
        });
        m_layout->addWidget(title);
        auto *desc = new QLineEdit(m_ctx.view.formDescription);
        desc->setFont(Theme::uiFont(14));
        desc->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; color: #a8a8b2; }"));
        desc->setPlaceholderText(tr("Add a description"));
        connect(desc, &QLineEdit::editingFinished, this, [this, desc] {
            const QString t = desc->text();
            saveView([t](View &v) { v.formDescription = t; });
        });
        m_layout->addWidget(desc);
    } else {
        auto *title = new QLabel(m_ctx.view.formTitle.isEmpty() ? dbTitle : m_ctx.view.formTitle);
        title->setFont(Theme::displayFont(24, QFont::Bold));
        title->setStyleSheet(QStringLiteral("color: #f1f1f5;"));
        m_layout->addWidget(title);
        if (!m_ctx.view.formDescription.isEmpty()) {
            auto *desc = new QLabel(m_ctx.view.formDescription);
            desc->setWordWrap(true);
            desc->setFont(Theme::uiFont(14));
            desc->setStyleSheet(QStringLiteral("color: #a8a8b2;"));
            m_layout->addWidget(desc);
        }
    }
    m_layout->addSpacing(10);

    Workspace *ws = Workspace::instance();
    for (const Property *p : m_ctx.schema.orderedProps(m_ctx.view)) {
        if (!answerable(p->type))
            continue;
        const bool required = m_ctx.view.formRequired.contains(p->id);
        auto *head = new QHBoxLayout;
        auto *label = new QLabel(p->name + (required ? QStringLiteral("  *") : QString()));
        label->setFont(Theme::uiFont(14, QFont::DemiBold));
        label->setStyleSheet(QStringLiteral("color: #e6e6ec;"));
        head->addWidget(label);
        head->addStretch();
        if (m_ctx.editable && p->type != PropType::Title) {
            auto *req = new QLabel(tr("Required"));
            req->setFont(Theme::uiFont(12));
            req->setStyleSheet(QStringLiteral("color: #7a7a86;"));
            head->addWidget(req);
            auto *sw = new Switch;
            sw->setChecked(required);
            const QString id = p->id;
            connect(sw, &Switch::toggled, this, [this, id](bool on) {
                saveView([id, on](View &v) {
                    v.formRequired.removeAll(id);
                    if (on)
                        v.formRequired.append(id);
                });
            });
            head->addWidget(sw);
        }
        m_layout->addLayout(head);
        if (!p->description.isEmpty()) {
            auto *d = new QLabel(p->description);
            d->setWordWrap(true);
            d->setFont(Theme::uiFont(12.5));
            d->setStyleSheet(QStringLiteral("color: #8a8a94;"));
            m_layout->addWidget(d);
        }
        QWidget *input = nullptr;
        switch (p->type) {
        case PropType::Text: {
            auto *e = new QPlainTextEdit;
            e->setFixedHeight(72);
            e->setPlaceholderText(tr("Your answer"));
            input = e;
            break;
        }
        case PropType::Number: {
            auto *e = new QLineEdit;
            e->setValidator(new QDoubleValidator(e));
            e->setPlaceholderText(tr("A number"));
            input = e;
            break;
        }
        case PropType::Select:
        case PropType::Status:
        case PropType::Person:
        case PropType::Relation: {
            auto *combo = new QComboBox;
            combo->addItem(tr("Choose…"), QString());
            if (p->type == PropType::Person) {
                for (const Workspace::Person &person : ws->people())
                    combo->addItem(person.name, person.id);
            } else if (p->type == PropType::Relation) {
                for (const PageMeta *row : ws->rows(p->relationDb))
                    combo->addItem(row->title.isEmpty() ? tr("Untitled") : row->title, row->id);
            } else {
                for (const Option &o : p->options)
                    combo->addItem(o.name, o.id);
            }
            input = combo;
            break;
        }
        case PropType::MultiSelect: {
            auto *box = new QWidget;
            auto *grid = new QGridLayout(box);
            grid->setContentsMargins(0, 0, 0, 0);
            grid->setHorizontalSpacing(18);
            for (int i = 0; i < p->options.size(); ++i) {
                auto *cb = new QCheckBox(p->options[i].name);
                cb->setProperty("option", p->options[i].id);
                grid->addWidget(cb, i / 3, i % 3);
            }
            input = box;
            break;
        }
        case PropType::Checkbox:
            input = new QCheckBox(tr("Yes"));
            break;
        case PropType::Date: {
            auto *e = new QLineEdit;
            e->setPlaceholderText(tr("A date, like “next friday” or 2026-10-20"));
            input = e;
            break;
        }
        case PropType::Files: {
            auto *btn = new FlatButton(QStringLiteral("upload"), tr("Upload a file"));
            const QString id = p->id;
            connect(btn, &FlatButton::clicked, this, [this, btn, id] {
                const QString file = QFileDialog::getOpenFileName(window(), tr("Upload a file"));
                if (file.isEmpty())
                    return;
                const QString media = Workspace::instance()->importMedia(file);
                if (media.isEmpty())
                    return;
                QJsonArray files = m_values.value(id).toArray();
                files.append(QJsonObject{{QStringLiteral("n"), QFileInfo(file).fileName()}, {QStringLiteral("m"), media}});
                m_values.insert(id, files);
                btn->setText(tr("%n file(s)", nullptr, int(files.size())));
            });
            input = btn;
            break;
        }
        default: {
            auto *e = new QLineEdit;
            e->setPlaceholderText(p->type == PropType::Title ? tr("Name") : tr("Your answer"));
            input = e;
            break;
        }
        }
        input->setProperty("propType", propTypeKey(p->type));
        m_inputs.insert(p->id, input);
        m_layout->addWidget(input);
        m_layout->addSpacing(8);
    }
    auto *error = new QLabel;
    error->setObjectName(QStringLiteral("formError"));
    error->setStyleSheet(QStringLiteral("color: #c9c9d0;"));
    error->setFont(Theme::uiFont(12.5, QFont::Medium));
    error->hide();
    m_layout->addWidget(error);
    auto *submit = new FlatButton(QStringLiteral("check"), tr("Submit"));
    submit->setFilled(true);
    submit->setObjectName(QStringLiteral("formSubmit"));
    connect(submit, &FlatButton::clicked, this, &FormView::submit);
    m_layout->addWidget(submit, 0, Qt::AlignLeft);
    auto *done = new QWidget;
    auto *dl = new QVBoxLayout(done);
    dl->setContentsMargins(0, 8, 0, 0);
    auto *thanks = new QLabel(tr("Thanks — your response was added to %1.").arg(dbTitle));
    thanks->setFont(Theme::uiFont(14, QFont::Medium));
    thanks->setStyleSheet(QStringLiteral("color: %1;").arg(c.starlight.name()));
    dl->addWidget(thanks);
    auto *again = new FlatButton(QStringLiteral("refresh"), tr("Submit another response"));
    connect(again, &FlatButton::clicked, this, [this] {
        m_builtFor.clear();
        setContext(m_ctx);
    });
    dl->addWidget(again, 0, Qt::AlignLeft);
    done->hide();
    m_done = done;
    m_layout->addWidget(done);
    adjustSize();
    setFixedHeight(qMax(200, layout()->sizeHint().height() + 12));
}

void FormView::submit()
{
    QJsonObject props;
    QString title;
    QStringList missing;
    for (const Property *p : m_ctx.schema.orderedProps(m_ctx.view)) {
        QWidget *w = m_inputs.value(p->id);
        if (!w)
            continue;
        QJsonValue v;
        switch (p->type) {
        case PropType::Text:
            if (auto *e = qobject_cast<QPlainTextEdit *>(w); e && !e->toPlainText().trimmed().isEmpty())
                v = e->toPlainText().trimmed();
            break;
        case PropType::Number:
            if (auto *e = qobject_cast<QLineEdit *>(w); e && !e->text().trimmed().isEmpty())
                v = QLocale::c().toDouble(e->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')));
            break;
        case PropType::Select:
        case PropType::Status:
            if (auto *cb = qobject_cast<QComboBox *>(w); cb && !cb->currentData().toString().isEmpty())
                v = cb->currentData().toString();
            break;
        case PropType::Person:
        case PropType::Relation:
            if (auto *cb = qobject_cast<QComboBox *>(w); cb && !cb->currentData().toString().isEmpty())
                v = QJsonArray{cb->currentData().toString()};
            break;
        case PropType::MultiSelect: {
            QJsonArray ids;
            for (QCheckBox *cb : w->findChildren<QCheckBox *>())
                if (cb->isChecked())
                    ids.append(cb->property("option").toString());
            if (!ids.isEmpty())
                v = ids;
            break;
        }
        case PropType::Checkbox:
            if (auto *cb = qobject_cast<QCheckBox *>(w))
                v = cb->isChecked();
            break;
        case PropType::Date:
            if (auto *e = qobject_cast<QLineEdit *>(w); e && !e->text().trimmed().isEmpty()) {
                const DateParse::Result r = DateParse::parse(e->text());
                if (!r.when.isValid()) {
                    missing.append(p->name);
                    continue;
                }
                v = makeDateValue(r.when, r.hasTime, {}, r.remind);
            }
            break;
        case PropType::Files:
            if (!m_values.value(p->id).toArray().isEmpty())
                v = m_values.value(p->id);
            break;
        default:
            if (auto *e = qobject_cast<QLineEdit *>(w); e && !e->text().trimmed().isEmpty())
                v = e->text().trimmed();
            break;
        }
        const bool empty = v.isNull() || v.isUndefined() || (p->type == PropType::Checkbox && !v.toBool());
        if (empty && m_ctx.view.formRequired.contains(p->id)) {
            missing.append(p->name);
            continue;
        }
        if (empty)
            continue;
        if (p->type == PropType::Title)
            title = v.toString();
        else
            props.insert(p->id, v);
    }
    auto *error = findChild<QLabel *>(QStringLiteral("formError"));
    if (!missing.isEmpty()) {
        if (error) {
            error->setText(tr("Please answer: %1").arg(missing.join(QStringLiteral(", "))));
            error->show();
        }
        return;
    }
    if (error)
        error->hide();
    Workspace::instance()->createRow(m_ctx.databaseId, title, props);
    // Clear the answers and say thanks.
    for (QWidget *w : std::as_const(m_inputs)) {
        if (auto *e = qobject_cast<QLineEdit *>(w))
            e->clear();
        else if (auto *e = qobject_cast<QPlainTextEdit *>(w))
            e->clear();
        else if (auto *cb = qobject_cast<QComboBox *>(w))
            cb->setCurrentIndex(0);
        else if (auto *cb = qobject_cast<QCheckBox *>(w))
            cb->setChecked(false);
        else
            for (QCheckBox *cb : w->findChildren<QCheckBox *>())
                cb->setChecked(false);
    }
    m_values.clear();
    if (m_done)
        m_done->show();
    setFixedHeight(qMax(200, layout()->sizeHint().height() + 12));
}
