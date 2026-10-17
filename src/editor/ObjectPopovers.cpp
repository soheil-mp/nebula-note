#include "ObjectPopovers.h"

#include "core/Workspace.h"
#include "ui/Tex.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace
{
QLabel *heading(const QString &text)
{
    auto *l = new QLabel(text);
    l->setFont(Theme::uiFont(12, QFont::DemiBold));
    l->setStyleSheet(QStringLiteral("color: #9a9aa6; padding-left: 2px;"));
    return l;
}

QString fieldStyle()
{
    return QStringLiteral("QPlainTextEdit, QLineEdit, QComboBox { background: #141418; color: #e6e6ec; border: 1px solid #2c2c34;"
                          " border-radius: 7px; padding: 5px 7px; selection-background-color: #46465a; }"
                          "QComboBox QAbstractItemView { background: #1b1b20; color: #e6e6ec; selection-background-color: #2c2c34; }");
}
} // namespace

// ---- equations ----------------------------------------------------------------------------

class TexPreview : public QWidget
{
public:
    explicit TexPreview(QWidget *parent, bool display) : QWidget(parent), m_display(display) { setMinimumHeight(64); }
    void setTex(const QString &tex)
    {
        m_tex = tex;
        const Tex::Metrics m = Tex::measure(tex, 20, m_display);
        setMinimumHeight(qMax(64, int(m.ascent + m.descent + 24)));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x14, 0x14, 0x18));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        if (m_tex.trimmed().isEmpty()) {
            p.setPen(Theme::colors().dust);
            p.setFont(Theme::uiFont(13));
            p.drawText(rect(), Qt::AlignCenter, QObject::tr("Preview"));
            return;
        }
        const Tex::Metrics m = Tex::measure(m_tex, 20, m_display);
        const double scale = m.width > width() - 20 ? (width() - 20) / m.width : 1.0;
        p.save();
        p.translate(width() / 2.0, height() / 2.0);
        p.scale(scale, scale);
        Tex::paint(p, QPointF(-m.width / 2, (m.ascent - m.descent) / 2), m_tex, 20, Theme::colors().moon, m_display);
        p.restore();
    }

private:
    QString m_tex;
    bool m_display;
};

EquationPopover::EquationPopover(QWidget *parent, const QString &tex, bool display)
    : Popover(parent)
    , m_display(display)
{
    content()->setStyleSheet(fieldStyle());
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    v->addWidget(heading(display ? tr("Block equation") : tr("Inline equation")));
    m_edit = new QPlainTextEdit(tex);
    m_edit->setFont(Theme::monoFont(13));
    m_edit->setPlaceholderText(QStringLiteral("E = mc^2"));
    m_edit->setFixedSize(420, 84);
    m_edit->installEventFilter(this);
    v->addWidget(m_edit);
    m_preview = new TexPreview(content(), display);
    m_preview->setFixedWidth(420);
    v->addWidget(m_preview);
    m_error = new QLabel;
    m_error->setFont(Theme::uiFont(12));
    m_error->setStyleSheet(QStringLiteral("color: #b4b4bf;"));
    m_error->setWordWrap(true);
    v->addWidget(m_error);
    auto *row = new QHBoxLayout;
    auto *help = new QLabel(tr("KaTeX syntax · Shift+Enter for a new line"));
    help->setFont(Theme::uiFont(11.5));
    help->setStyleSheet(QStringLiteral("color: #6e6e7a;"));
    row->addWidget(help);
    row->addStretch();
    auto *done = new FlatButton(QStringLiteral("check"), tr("Done"));
    done->setFilled(true);
    row->addWidget(done);
    v->addLayout(row);
    connect(m_edit, &QPlainTextEdit::textChanged, this, &EquationPopover::updatePreview);
    connect(done, &FlatButton::clicked, this, [this] {
        emit submitted(m_edit->toPlainText().trimmed());
        close();
    });
    updatePreview();
    QTimer::singleShot(0, m_edit, [this] {
        m_edit->setFocus();
        m_edit->selectAll();
    });
}

bool EquationPopover::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && !(ke->modifiers() & Qt::ShiftModifier)) {
            emit submitted(m_edit->toPlainText().trimmed());
            close();
            return true;
        }
        if (ke->key() == Qt::Key_Escape) {
            close();
            return true;
        }
    }
    return Popover::eventFilter(watched, event);
}

void EquationPopover::updatePreview()
{
    const QString tex = m_edit->toPlainText();
    static_cast<TexPreview *>(m_preview)->setTex(tex);
    const QString err = Tex::measure(tex, 20, m_display).error;
    m_error->setText(err);
    m_error->setVisible(!err.isEmpty());
    adjustSize();
}

// ---- one line ------------------------------------------------------------------------------

LinePopover::LinePopover(QWidget *parent, const QString &title, const QString &placeholder, const QString &text, const QString &button)
    : Popover(parent)
{
    content()->setStyleSheet(fieldStyle());
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(8);
    if (!title.isEmpty())
        v->addWidget(heading(title));
    auto *row = new QHBoxLayout;
    m_edit = new QLineEdit(text);
    m_edit->setFont(Theme::uiFont(13.5));
    m_edit->setPlaceholderText(placeholder);
    m_edit->setMinimumWidth(360);
    row->addWidget(m_edit);
    auto *ok = new FlatButton(QString(), button.isEmpty() ? tr("Done") : button);
    ok->setFilled(true);
    row->addWidget(ok);
    v->addLayout(row);
    auto submit = [this] {
        emit submitted(m_edit->text().trimmed());
        close();
    };
    connect(m_edit, &QLineEdit::returnPressed, this, submit);
    connect(ok, &FlatButton::clicked, this, submit);
    QTimer::singleShot(0, m_edit, [this] {
        m_edit->setFocus();
        m_edit->selectAll();
    });
}

// ---- buttons --------------------------------------------------------------------------------

ButtonPopover::ButtonPopover(QWidget *parent, const QJsonObject &data)
    : Popover(parent)
{
    content()->setStyleSheet(fieldStyle());
    Workspace *ws = Workspace::instance();
    auto *v = new QVBoxLayout(content());
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(7);
    v->addWidget(heading(tr("Button name")));
    m_label = new QLineEdit(data.value(QStringLiteral("label")).toString());
    m_label->setPlaceholderText(tr("New button"));
    m_label->setMinimumWidth(380);
    v->addWidget(m_label);
    v->addWidget(heading(tr("When clicked")));
    m_action = new QComboBox;
    m_action->addItem(tr("Insert blocks below"), QStringLiteral("insert"));
    m_action->addItem(tr("Add a page to a database"), QStringLiteral("add"));
    m_action->addItem(tr("Open a page"), QStringLiteral("page"));
    m_action->addItem(tr("Open a link"), QStringLiteral("url"));
    v->addWidget(m_action);
    const QJsonObject action = data.value(QStringLiteral("actions")).toArray().at(0).toObject();
    const QString type = action.value(QStringLiteral("type")).toString(QStringLiteral("insert"));
    m_action->setCurrentIndex(qMax(0, m_action->findData(type)));

    m_blocksRow = new QWidget;
    auto *bl = new QVBoxLayout(m_blocksRow);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(heading(tr("Blocks to insert (Markdown)")));
    m_blocks = new QPlainTextEdit(action.value(QStringLiteral("markdown")).toString());
    m_blocks->setPlaceholderText(QStringLiteral("- [ ] Follow up\n- [ ] Send notes"));
    m_blocks->setFont(Theme::monoFont(12.5));
    m_blocks->setFixedHeight(96);
    bl->addWidget(m_blocks);
    v->addWidget(m_blocksRow);

    m_databaseRow = new QWidget;
    auto *dl = new QVBoxLayout(m_databaseRow);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->addWidget(heading(tr("Database")));
    m_database = new QComboBox;
    m_page = new QComboBox;
    if (ws) {
        for (const PageMeta *m : ws->allLive()) {
            if (m->isDatabase())
                m_database->addItem(ws->title(m->id), m->id);
            if (!m->isRow() && !m->isTemplate())
                m_page->addItem(ws->title(m->id), m->id);
        }
    }
    m_database->setCurrentIndex(qMax(0, m_database->findData(action.value(QStringLiteral("db")).toString())));
    dl->addWidget(m_database);
    dl->addWidget(heading(tr("Title of the new page")));
    m_rowTitle = new QLineEdit(action.value(QStringLiteral("title")).toString());
    m_rowTitle->setPlaceholderText(tr("Untitled — “@today” adds the date"));
    dl->addWidget(m_rowTitle);
    v->addWidget(m_databaseRow);

    m_pageRow = new QWidget;
    auto *pl = new QVBoxLayout(m_pageRow);
    pl->setContentsMargins(0, 0, 0, 0);
    pl->addWidget(heading(tr("Page")));
    m_page->setCurrentIndex(qMax(0, m_page->findData(action.value(QStringLiteral("page")).toString())));
    pl->addWidget(m_page);
    v->addWidget(m_pageRow);

    m_urlRow = new QWidget;
    auto *ul = new QVBoxLayout(m_urlRow);
    ul->setContentsMargins(0, 0, 0, 0);
    ul->addWidget(heading(tr("Link")));
    m_url = new QLineEdit(action.value(QStringLiteral("url")).toString());
    m_url->setPlaceholderText(QStringLiteral("https://"));
    ul->addWidget(m_url);
    v->addWidget(m_urlRow);

    auto *confirmRow = new QHBoxLayout;
    auto *cl = new QLabel(tr("Ask for confirmation"));
    cl->setFont(Theme::uiFont(13));
    cl->setStyleSheet(QStringLiteral("color: #c9c9d0;"));
    m_confirm = new Switch;
    m_confirm->setChecked(!data.value(QStringLiteral("confirm")).toString().isEmpty());
    confirmRow->addWidget(cl);
    confirmRow->addStretch();
    confirmRow->addWidget(m_confirm);
    v->addLayout(confirmRow);
    m_confirmText = new QLineEdit(data.value(QStringLiteral("confirm")).toString());
    m_confirmText->setPlaceholderText(tr("Are you sure?"));
    v->addWidget(m_confirmText);

    auto *done = new FlatButton(QStringLiteral("check"), tr("Done"));
    done->setFilled(true);
    v->addWidget(done, 0, Qt::AlignRight);
    connect(m_action, &QComboBox::currentIndexChanged, this, &ButtonPopover::updateFields);
    connect(m_confirm, &Switch::toggled, this, &ButtonPopover::updateFields);
    connect(done, &FlatButton::clicked, this, [this] {
        QJsonObject out;
        out.insert(QStringLiteral("label"), m_label->text().trimmed());
        QJsonObject a;
        const QString t = m_action->currentData().toString();
        a.insert(QStringLiteral("type"), t);
        if (t == QLatin1String("insert"))
            a.insert(QStringLiteral("markdown"), m_blocks->toPlainText());
        else if (t == QLatin1String("add")) {
            a.insert(QStringLiteral("db"), m_database->currentData().toString());
            a.insert(QStringLiteral("title"), m_rowTitle->text());
        } else if (t == QLatin1String("page"))
            a.insert(QStringLiteral("page"), m_page->currentData().toString());
        else
            a.insert(QStringLiteral("url"), m_url->text().trimmed());
        out.insert(QStringLiteral("actions"), QJsonArray{a});
        if (m_confirm->isChecked())
            out.insert(QStringLiteral("confirm"), m_confirmText->text().trimmed().isEmpty() ? tr("Are you sure?") : m_confirmText->text().trimmed());
        emit submitted(out);
        close();
    });
    updateFields();
    QTimer::singleShot(0, m_label, [this] { m_label->setFocus(); });
}

void ButtonPopover::updateFields()
{
    const QString t = m_action->currentData().toString();
    m_blocksRow->setVisible(t == QLatin1String("insert"));
    m_databaseRow->setVisible(t == QLatin1String("add"));
    m_pageRow->setVisible(t == QLatin1String("page"));
    m_urlRow->setVisible(t == QLatin1String("url"));
    m_confirmText->setVisible(m_confirm->isChecked());
    adjustSize();
}

// ---- full-screen images -------------------------------------------------------------------

ImageViewer::ImageViewer(QWidget *window, const QString &path, const QString &caption)
    : QWidget(window)
    , m_caption(caption)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    m_image = reader.read();
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);
    setGeometry(window->rect());
    window->installEventFilter(this);
    show();
    raise();
    setFocus();
}

bool ImageViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return false;
}

void ImageViewer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(rect(), QColor(5, 5, 7, 235));
    if (m_image.isNull())
        return;
    const QRectF area = QRectF(rect()).adjusted(48, 48, -48, m_caption.isEmpty() ? -48 : -84);
    QSizeF size = QSizeF(m_image.size());
    if (size.width() > area.width() || size.height() > area.height())
        size.scale(area.size(), Qt::KeepAspectRatio);
    const QRectF target(area.center().x() - size.width() / 2, area.center().y() - size.height() / 2, size.width(), size.height());
    p.drawImage(target, m_image);
    if (!m_caption.isEmpty()) {
        p.setPen(Theme::colors().mist);
        p.setFont(Theme::uiFont(14));
        p.drawText(QRectF(48, height() - 76, width() - 96, 40), Qt::AlignCenter, m_caption);
    }
    p.setPen(Theme::colors().fog);
    p.setFont(Theme::uiFont(12));
    p.drawText(QRectF(0, 12, width() - 20, 24), Qt::AlignRight | Qt::AlignVCenter, tr("Esc to close"));
}

void ImageViewer::mousePressEvent(QMouseEvent *)
{
    close();
}

void ImageViewer::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Space || e->key() == Qt::Key_Return)
        close();
}
