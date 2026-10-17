#include "FormulaEditor.h"

#include "core/Formula.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace
{
QString fieldStyle()
{
    return QStringLiteral("QPlainTextEdit { background: #141418; color: #e6e6ec; border: 1px solid #2c2c34; border-radius: 8px;"
                          " padding: 6px 8px; selection-background-color: #46465a; }"
                          "QListWidget { background: transparent; color: #d4d4dc; border: none; outline: none; }"
                          "QListWidget::item { padding: 4px 6px; border-radius: 6px; }"
                          "QListWidget::item:selected, QListWidget::item:hover { background: #26262d; color: #f1f1f5; }");
}
} // namespace

FormulaEditor::FormulaEditor(QWidget *parent, const QString &databaseId, const QString &propId)
    : Popover(parent)
    , m_db(databaseId)
    , m_prop(propId)
{
    Workspace *ws = Workspace::instance();
    const Schema schema = ws->schema(databaseId);
    const Property *prop = schema.prop(propId);
    content()->setStyleSheet(fieldStyle());
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    auto *title = new QLabel(prop ? tr("Formula · %1").arg(prop->name) : tr("Formula"));
    title->setFont(Theme::uiFont(12.5, QFont::DemiBold));
    title->setStyleSheet(QStringLiteral("color: #9a9aa6;"));
    v->addWidget(title);
    m_edit = new QPlainTextEdit(prop ? prop->formula : QString());
    m_edit->setFont(Theme::monoFont(13));
    m_edit->setPlaceholderText(QStringLiteral("prop(\"Price\") * prop(\"Quantity\")"));
    m_edit->setFixedSize(560, 90);
    m_edit->installEventFilter(this);
    v->addWidget(m_edit);
    m_result = new QLabel;
    m_result->setFont(Theme::uiFont(12.5));
    m_result->setWordWrap(true);
    m_result->setStyleSheet(QStringLiteral("color: #b8b8c2; padding: 2px 2px;"));
    v->addWidget(m_result);
    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_list = new QListWidget;
    m_list->setFixedSize(230, 260);
    m_list->setFont(Theme::uiFont(13));
    m_list->setIconSize(QSize(15, 15));
    row->addWidget(m_list);
    m_help = new QLabel;
    m_help->setWordWrap(true);
    m_help->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_help->setFixedSize(320, 260);
    m_help->setFont(Theme::uiFont(13));
    m_help->setStyleSheet(QStringLiteral("color: #c9c9d0; background: #141418; border-radius: 8px; padding: 10px;"));
    m_help->setTextFormat(Qt::RichText);
    row->addWidget(m_help);
    v->addLayout(row);
    auto *buttons = new QHBoxLayout;
    auto *hint = new QLabel(tr("Ctrl+Enter to save"));
    hint->setFont(Theme::uiFont(11.5));
    hint->setStyleSheet(QStringLiteral("color: #6e6e7a;"));
    buttons->addWidget(hint);
    buttons->addStretch();
    auto *done = new FlatButton(QStringLiteral("check"), tr("Done"));
    done->setFilled(true);
    buttons->addWidget(done);
    v->addLayout(buttons);

    // Properties first, then functions by group.
    auto header = [&](const QString &text) {
        auto *item = new QListWidgetItem(text);
        item->setFlags(Qt::NoItemFlags);
        item->setFont(Theme::uiFont(11.5, QFont::DemiBold));
        item->setForeground(QColor(0x7a, 0x7a, 0x86));
        m_list->addItem(item);
    };
    header(tr("Properties"));
    for (const Property &p : schema.props) {
        if (p.id == propId)
            continue;
        auto *item = new QListWidgetItem(Icons::icon(propTypeIcon(p.type)), p.name);
        item->setData(Qt::UserRole, QStringLiteral("prop"));
        item->setData(Qt::UserRole + 1, p.name);
        m_list->addItem(item);
    }
    QString group;
    for (const Formula::FunctionDoc &d : Formula::functions()) {
        if (d.name == QLatin1String("prop"))
            continue;
        if (d.group != group) {
            group = d.group;
            header(group);
        }
        auto *item = new QListWidgetItem(d.name);
        item->setData(Qt::UserRole, QStringLiteral("fn"));
        item->setData(Qt::UserRole + 1, d.name);
        item->setData(Qt::UserRole + 2, d.syntax);
        item->setData(Qt::UserRole + 3, d.help);
        m_list->addItem(item);
    }
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        if (item->data(Qt::UserRole).toString() == QLatin1String("prop")) {
            m_help->setText(tr("<b>%1</b><br><br>Inserts <code>prop(\"%1\")</code>, the value of this property on each row.")
                                .arg(item->text().toHtmlEscaped()));
        } else {
            m_help->setText(QStringLiteral("<b>%1</b><br><br><code>%2</code><br><br>%3")
                                .arg(item->text().toHtmlEscaped(), item->data(Qt::UserRole + 2).toString().toHtmlEscaped(),
                                     item->data(Qt::UserRole + 3).toString().toHtmlEscaped()));
        }
    });
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item->data(Qt::UserRole).toString() == QLatin1String("prop"))
            insert(QStringLiteral("prop(\"%1\")").arg(item->data(Qt::UserRole + 1).toString()));
        else if (item->data(Qt::UserRole).toString() == QLatin1String("fn"))
            insert(item->data(Qt::UserRole + 1).toString() + QStringLiteral("()"), 1);
    });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { emit m_list->itemActivated(item); });
    connect(m_edit, &QPlainTextEdit::textChanged, this, &FormulaEditor::updatePreview);
    connect(done, &FlatButton::clicked, this, [this] {
        save();
        close();
    });
    m_help->setText(tr("<b>Formulas</b><br><br>Combine properties with math, text and date functions. Click a property or a "
                       "function to insert it. Formulas recalculate for every row as values change."));
    updatePreview();
    QTimer::singleShot(0, m_edit, [this] {
        m_edit->setFocus();
        m_edit->moveCursor(QTextCursor::End);
    });
}

void FormulaEditor::insert(const QString &text, int cursorBack)
{
    QTextCursor c = m_edit->textCursor();
    c.insertText(text);
    c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, cursorBack);
    m_edit->setTextCursor(c);
    m_edit->setFocus();
}

bool FormulaEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ControlModifier)) {
            save();
            close();
            return true;
        }
    }
    return Popover::eventFilter(watched, event);
}

void FormulaEditor::updatePreview()
{
    Workspace *ws = Workspace::instance();
    const QString expr = m_edit->toPlainText();
    const QString syntax = Formula::check(expr);
    if (!syntax.isEmpty()) {
        m_result->setText(tr("⚠ %1").arg(syntax));
        return;
    }
    if (expr.trimmed().isEmpty()) {
        m_result->setText(tr("Write a formula; its result for the first rows shows here."));
        return;
    }
    // Evaluate against the first rows with this expression in place.
    Schema s = ws->schema(m_db);
    Property *p = s.prop(m_prop);
    if (!p)
        return;
    Property probe = *p;
    probe.formula = expr;
    QStringList shown;
    int n = 0;
    for (const PageMeta *row : ws->rows(m_db)) {
        if (n++ >= 3)
            break;
        const RowRef ref{row, ws->props(row->id)};
        const QJsonValue v = rowValue(probe, ref);
        if (v.isNull()) {
            const QString err = formulaError(probe, ref);
            if (!err.isEmpty()) {
                m_result->setTextFormat(Qt::PlainText);
                m_result->setText(tr("⚠ %1").arg(err));
                return;
            }
        }
        Property disp = probe;
        disp.formulaType = v.isDouble() ? QStringLiteral("number") : v.isBool() ? QStringLiteral("bool") : QStringLiteral("text");
        // One row per line: the page in gray, its result bright.
        const QString title = ws->title(row->id).isEmpty() ? tr("Untitled") : ws->title(row->id);
        shown.append(QStringLiteral("<span style=\"color:#7a7a86\">%1</span>&nbsp;&nbsp;→&nbsp;&nbsp;%2")
                         .arg(QFontMetricsF(m_result->font()).elidedText(title, Qt::ElideRight, 260).toHtmlEscaped(),
                              (v.isNull() ? tr("(empty)") : displayValue(disp, v)).toHtmlEscaped()));
    }
    m_result->setTextFormat(Qt::RichText);
    m_result->setText(shown.isEmpty() ? tr("No rows to preview yet.") : shown.join(QStringLiteral("<br>")));
}

void FormulaEditor::save()
{
    Workspace *ws = Workspace::instance();
    Schema s = ws->schema(m_db);
    Property *p = s.prop(m_prop);
    if (!p)
        return;
    p->formula = m_edit->toPlainText().trimmed();
    // The result type decides filters, sorts and how values look.
    QString type = QStringLiteral("text");
    ws->setSchema(m_db, s);
    const Schema *live = ws->lookupSchema(m_db);
    for (const PageMeta *row : ws->rows(m_db)) {
        const QJsonValue v = rowValue(*live->prop(m_prop), RowRef{row, ws->props(row->id)});
        if (v.isNull() || v.isUndefined())
            continue;
        if (v.isDouble())
            type = QStringLiteral("number");
        else if (v.isBool())
            type = QStringLiteral("bool");
        else if (v.isArray())
            type = QStringLiteral("list");
        else if (QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}(T\\d{2}:\\d{2})?(/.*)?$")).match(v.toString()).hasMatch())
            type = QStringLiteral("date");
        break;
    }
    s = ws->schema(m_db);
    if (Property *q = s.prop(m_prop); q && q->formulaType != type) {
        q->formulaType = type;
        ws->setSchema(m_db, s);
    }
}
