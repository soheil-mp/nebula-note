#include "PageHeader.h"

#include "Pickers.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QAbstractTextDocumentLayout>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextLayout>

// ---- TitleEdit -------------------------------------------------------------------

TitleEdit::TitleEdit(QWidget *parent)
    : QTextEdit(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAcceptRichText(false);
    setTabChangesFocus(true);
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    setCursorWidth(2);
    document()->setDocumentMargin(0);
    m_placeholder = tr("Untitled");
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Text, Theme::colors().starlight);
    pal.setColor(QPalette::Highlight, QColor(0x46, 0x46, 0x52));
    pal.setColor(QPalette::HighlightedText, Theme::colors().starlight);
    setPalette(pal);
    setPixelSize(40);
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        updateHeight();
        if (!m_setting)
            emit edited(title());
    });
}

void TitleEdit::setPixelSize(double px)
{
    m_px = px;
    QFont f = Theme::displayFont(px, QFont::Bold);
    f.setLetterSpacing(QFont::PercentageSpacing, 99);
    setFont(f);
    document()->setDefaultFont(f);
    QTextCursor c(document());
    c.select(QTextCursor::Document);
    QTextCharFormat cf;
    cf.setFont(f);
    m_setting = true;
    c.mergeCharFormat(cf);
    QTextBlockFormat bf;
    bf.setLineHeight(118, QTextBlockFormat::ProportionalHeight);
    c.mergeBlockFormat(bf);
    m_setting = false;
    updateHeight();
}

void TitleEdit::setTitle(const QString &title)
{
    if (title == this->title())
        return;
    m_setting = true;
    setPlainText(title);
    QTextCursor c(document());
    c.select(QTextCursor::Document);
    QTextBlockFormat bf;
    bf.setLineHeight(118, QTextBlockFormat::ProportionalHeight);
    c.mergeBlockFormat(bf);
    document()->clearUndoRedoStacks();
    m_setting = false;
    updateHeight();
}

QString TitleEdit::title() const
{
    return toPlainText().simplified();
}

void TitleEdit::focusEnd()
{
    setFocus();
    moveCursor(QTextCursor::End);
}

int TitleEdit::preferredHeight() const
{
    return qMax(qCeil(document()->documentLayout()->documentSize().height()), int(m_px * 1.2)) + 2;
}

void TitleEdit::updateHeight()
{
    const int h = preferredHeight();
    if (h == m_lastHeight)
        return;
    m_lastHeight = h;
    setFixedHeight(h);
    emit heightChanged();
}

void TitleEdit::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    updateHeight();
}

bool TitleEdit::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        const bool ctrl = (ke->modifiers() & ~Qt::KeypadModifier) == Qt::ControlModifier;
        const int key = ke->key();
        if (ctrl && (key == Qt::Key_K || key == Qt::Key_P || key == Qt::Key_N || key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight
                     || key == Qt::Key_Backslash || key == Qt::Key_Comma)) {
            e->ignore();
            return true;
        }
    }
    return QTextEdit::event(e);
}

void TitleEdit::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        emit submitted();
        return;
    }
    if (e->key() == Qt::Key_Down || e->key() == Qt::Key_Tab) {
        const QTextCursor c = textCursor();
        const QTextLayout *layout = c.block().layout();
        const bool lastLine = !layout || layout->lineForTextPosition(c.positionInBlock()).lineNumber() >= layout->lineCount() - 1;
        if (lastLine || e->key() == Qt::Key_Tab) {
            emit exitDown();
            return;
        }
    }
    if (e->matches(QKeySequence::Find) || (e->modifiers() & Qt::ControlModifier && (e->key() == Qt::Key_P || e->key() == Qt::Key_K
                                                                                      || e->key() == Qt::Key_N))) {
        e->ignore();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

void TitleEdit::insertFromMimeData(const QMimeData *source)
{
    QString text = source->text();
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    insertPlainText(text.simplified());
}

void TitleEdit::paintEvent(QPaintEvent *e)
{
    QTextEdit::paintEvent(e);
    if (!document()->isEmpty())
        return;
    QPainter p(viewport());
    p.setFont(font());
    p.setPen(Theme::colors().dust);
    const QFontMetricsF fm(font());
    p.drawText(QPointF(0, fm.ascent()), m_placeholder);
}

void TitleEdit::focusOutEvent(QFocusEvent *e)
{
    QTextEdit::focusOutEvent(e);
    QTextCursor c = textCursor();
    c.clearSelection();
    setTextCursor(c);
}

// ---- CoverWidget -----------------------------------------------------------------

CoverWidget::CoverWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
}

void CoverWidget::setCover(const QString &spec, double position)
{
    m_spec = spec;
    m_pos = position;
    update();
}

QRectF CoverWidget::buttonRect(int i) const
{
    // Right-aligned: [Change cover] [Reposition] [Remove]
    const double h = 28, gap = 6;
    const QList<double> widths = {104, m_spec.startsWith(QLatin1String("media:")) ? 92.0 : 0.0, 72};
    double x = width() - 24;
    for (int k = int(widths.size()) - 1; k >= i; --k) {
        if (widths[k] <= 0)
            continue;
        x -= widths[k] + (k == int(widths.size()) - 1 ? 0 : gap);
        if (k == i)
            return QRectF(x, height() - h - 14, widths[k], h);
    }
    return {};
}

void CoverWidget::paintEvent(QPaintEvent *)
{
    if (m_spec.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Cosmos::paintCover(p, rect(), m_spec, m_pos);
    // Fade into the page below.
    QLinearGradient fade(QPointF(0, height() - 40), QPointF(0, height()));
    fade.setColorAt(0, QColor(19, 19, 22, 0));
    fade.setColorAt(1, QColor(19, 19, 22, 70));
    p.fillRect(rect(), fade);
    if (m_repositioning) {
        p.fillRect(rect(), QColor(0, 0, 0, 60));
        p.setPen(Theme::colors().starlight);
        p.setFont(Theme::uiFont(13, QFont::Medium));
        p.drawText(rect(), Qt::AlignCenter, tr("Drag image to reposition"));
    }
    if ((m_hover || m_repositioning) && m_editable) {
        const QStringList labels = {m_repositioning ? tr("Save position") : tr("Change cover"), tr("Reposition"), tr("Remove")};
        for (int i = 0; i < 3; ++i) {
            const QRectF r = buttonRect(i);
            if (r.isEmpty() || (m_repositioning && i > 0))
                continue;
            p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.12), 1));
            p.setBrush(QColor(14, 14, 17, i == m_hoverButton ? 235 : 200));
            p.drawRoundedRect(r, 7, 7);
            p.setPen(i == m_hoverButton ? Theme::colors().starlight : Theme::colors().moon);
            p.setFont(Theme::uiFont(12.5, QFont::Medium));
            p.drawText(r, Qt::AlignCenter, labels[i]);
        }
    }
}

void CoverWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_editable)
        return;
    for (int i = 0; i < 3; ++i) {
        if (!buttonRect(i).contains(e->position()))
            continue;
        if (m_repositioning) {
            m_repositioning = false;
            emit positionChanged(m_pos);
            update();
            return;
        }
        if (i == 0)
            emit changeRequested(mapToGlobal(QPoint(int(buttonRect(0).left()), int(buttonRect(0).bottom()) + 4)));
        else if (i == 1) {
            m_repositioning = true;
            setCursor(Qt::SizeVerCursor);
            update();
        } else
            emit removeRequested();
        return;
    }
    if (m_repositioning) {
        m_dragging = true;
        m_dragStartY = e->position().y();
        m_dragStartPos = m_pos;
    }
}

void CoverWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        // Dragging down reveals the top of the image.
        m_pos = qBound(0.0, m_dragStartPos - (e->position().y() - m_dragStartY) / qMax(1, height() * 2), 1.0);
        update();
        return;
    }
    int hover = -1;
    for (int i = 0; i < 3; ++i)
        if (buttonRect(i).contains(e->position()))
            hover = i;
    if (hover != m_hoverButton) {
        m_hoverButton = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : (m_repositioning ? Qt::SizeVerCursor : Qt::ArrowCursor));
        update();
    }
}

void CoverWidget::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

void CoverWidget::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void CoverWidget::leaveEvent(QEvent *)
{
    m_hover = false;
    m_hoverButton = -1;
    update();
}

// ---- IconBadge -------------------------------------------------------------------

// The page icon; a child widget so it can overlap the cover.
class IconBadge : public QWidget
{
public:
    explicit IconBadge(QWidget *parent)
        : QWidget(parent)
    {
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
    }
    QString icon;
    bool editable = true;
    std::function<void(const QPoint &)> clicked;

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (icon.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF ir = QRectF(rect()).adjusted(4, 4, -4, -4);
        if (m_hover && editable) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.06));
            p.drawRoundedRect(QRectF(rect()), 12, 12);
        }
        if (icon.startsWith(QLatin1String("glyph:"))) {
            // Glyph icons sit on a soft disc so they read like emoji.
            const QPointF c = ir.center();
            const double r = ir.width() * 0.46;
            QRadialGradient g(c - QPointF(r * 0.4, r * 0.5), r * 1.6);
            g.setColorAt(0, QColor(0x30, 0x30, 0x38));
            g.setColorAt(1, QColor(0x16, 0x16, 0x1A));
            p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.12), 1));
            p.setBrush(g);
            p.drawEllipse(c, r, r);
            Icons::paintPageIcon(p, ir.adjusted(ir.width() * 0.26, ir.height() * 0.26, -ir.width() * 0.26, -ir.height() * 0.26), icon,
                                 Theme::colors().starlight);
        } else {
            Icons::paintPageIcon(p, ir, icon, Theme::colors().starlight);
        }
    }
    void enterEvent(QEnterEvent *) override
    {
        m_hover = true;
        update();
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = false;
        update();
    }
    void mousePressEvent(QMouseEvent *) override
    {
        if (editable && clicked)
            clicked(mapToGlobal(QPoint(0, height() + 4)));
    }

private:
    bool m_hover = false;
};

// ---- PageHeader ------------------------------------------------------------------

PageHeader::PageHeader(QWidget *parent)
    : QWidget(parent)
{
    m_coverWidget = new CoverWidget(this);
    m_iconBadge = new IconBadge(this);
    m_iconBadge->clicked = [this](const QPoint &global) { pickIcon(global); };
    m_title = new TitleEdit(this);
    m_actions = new QWidget(this);
    auto *row = new QHBoxLayout(m_actions);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    m_addIcon = new FlatButton(QStringLiteral("smile"), tr("Add icon"));
    m_addCover = new FlatButton(QStringLiteral("image"), tr("Add cover"));
    m_addComment = new FlatButton(QStringLiteral("comment"), tr("Add comment"));
    for (FlatButton *b : {m_addIcon, m_addCover, m_addComment}) {
        b->setTextSize(13);
        row->addWidget(b);
    }
    row->addStretch();
    auto *effect = new QGraphicsOpacityEffect(m_actions);
    effect->setOpacity(0.0);
    m_actions->setGraphicsEffect(effect);
    m_actionsFade.setDuration(Theme::animMs(140));
    m_actionsFade.setStartValue(0.0);
    m_actionsFade.setEndValue(1.0);
    connect(&m_actionsFade, &QVariantAnimation::valueChanged, this, [effect](const QVariant &v) { effect->setOpacity(v.toDouble()); });

    connect(m_addIcon, &FlatButton::clicked, this, [this] {
        // A random glyph first, like Notion; click it again to choose.
        const QStringList glyphs = {QStringLiteral("planet"), QStringLiteral("moon"), QStringLiteral("sparkle"), QStringLiteral("rocket"),
                                    QStringLiteral("comet"), QStringLiteral("orbit"), QStringLiteral("galaxy"), QStringLiteral("telescope"),
                                    QStringLiteral("constellation"), QStringLiteral("star")};
        if (Workspace *ws = Workspace::instance())
            ws->setIcon(m_pageId, QStringLiteral("glyph:") + glyphs[int(QDateTime::currentMSecsSinceEpoch() % glyphs.size())]);
    });
    connect(m_addCover, &FlatButton::clicked, this, [this] {
        if (Workspace *ws = Workspace::instance())
            ws->setCover(m_pageId, Cosmos::randomCover());
    });
    connect(m_addComment, &FlatButton::clicked, this, &PageHeader::commentRequested);
    connect(m_coverWidget, &CoverWidget::changeRequested, this, &PageHeader::pickCover);
    connect(m_coverWidget, &CoverWidget::removeRequested, this, [this] {
        if (Workspace *ws = Workspace::instance())
            ws->setCover(m_pageId, QString());
    });
    connect(m_coverWidget, &CoverWidget::positionChanged, this, [this](double pos) {
        if (Workspace *ws = Workspace::instance())
            ws->setCoverPosition(m_pageId, pos);
    });
    connect(m_title, &TitleEdit::heightChanged, this, &PageHeader::relayout);
    connect(m_title, &TitleEdit::submitted, this, &PageHeader::titleSubmitted);
    connect(m_title, &TitleEdit::exitDown, this, &PageHeader::titleExitDown);
    m_titleTimer.setSingleShot(true);
    m_titleTimer.setInterval(250);
    connect(m_title, &TitleEdit::edited, this, [this] { m_titleTimer.start(); });
    connect(&m_titleTimer, &QTimer::timeout, this, [this] {
        if (Workspace *ws = Workspace::instance())
            ws->setTitle(m_pageId, m_title->title());
    });
    if (Workspace *ws = Workspace::instance()) {
        connect(ws, &Workspace::pageChanged, this, [this](const QString &id) {
            if (id == m_pageId)
                refresh();
        });
    }
}

void PageHeader::flushTitle()
{
    if (!m_titleTimer.isActive())
        return;
    m_titleTimer.stop();
    if (Workspace *ws = Workspace::instance(); ws && ws->exists(m_pageId))
        ws->setTitle(m_pageId, m_title->title());
}

void PageHeader::setPage(const QString &pageId)
{
    flushTitle();
    m_pageId = pageId;
    refresh();
}

void PageHeader::refresh()
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws ? ws->page(m_pageId) : nullptr;
    if (!m)
        return;
    m_icon = m->icon;
    m_cover = m->cover;
    m_coverWidget->setCover(m->cover, m->coverPos);
    if (!m_title->hasFocus() && !m_titleTimer.isActive())
        m_title->setTitle(m->title);
    m_addIcon->setVisible(m->icon.isEmpty() && m_editable);
    m_addCover->setVisible(m->cover.isEmpty() && m_editable);
    m_addComment->setVisible(m_editable && ws->options(m_pageId).showDiscussion);
    relayout();
    update();
}

void PageHeader::setColumn(int x, int width)
{
    if (x == m_colX && width == m_colW)
        return;
    m_colX = x;
    m_colW = width;
    updateTitleSize();
    relayout();
}

void PageHeader::setSmall(bool small)
{
    m_small = small;
    updateTitleSize();
    relayout();
}

void PageHeader::updateTitleSize()
{
    // Small text, and narrow columns (a side peek), get a smaller title.
    const double px = m_small || (m_colW > 0 && m_colW < 560) ? 32 : 40;
    if (!qFuzzyCompare(m_title->pixelSize(), px))
        m_title->setPixelSize(px);
}

void PageHeader::setEditable(bool editable)
{
    m_editable = editable;
    m_title->setReadOnly(!editable);
    m_coverWidget->setEditable(editable);
    refresh();
}

QRectF PageHeader::iconRect() const
{
    const int coverH = !m_coverWidget->isHidden() ? m_coverWidget->height() : 0;
    const double size = m_small ? 64 : 78;
    const double top = coverH > 0 ? coverH - size * 0.5 : 60;
    return QRectF(m_colX - 4, top, size, size);
}

void PageHeader::relayout()
{
    const bool hasCover = !m_cover.isEmpty();
    int coverH = 0;
    if (hasCover) {
        const QWidget *win = window();
        coverH = qBound(180, int((win ? win->height() : 800) * 0.28), 300);
    }
    m_coverWidget->setVisible(hasCover);
    m_coverWidget->setGeometry(0, 0, width(), coverH);
    int y = hasCover ? coverH : 60;
    m_iconBadge->icon = m_icon;
    m_iconBadge->editable = m_editable;
    m_iconBadge->setVisible(!m_icon.isEmpty());
    if (!m_icon.isEmpty()) {
        const QRectF ir = iconRect();
        m_iconBadge->setGeometry(ir.toRect().adjusted(-4, -4, 4, 4));
        m_iconBadge->raise();
        m_iconBadge->update();
        y = int(ir.bottom()) + 6;
    }
    m_actions->setGeometry(m_colX - 8, y, m_colW, 30);
    y += 34;
    m_title->setGeometry(m_colX, y, m_colW, m_title->preferredHeight());
    y += m_title->height() + 4;
    if (height() != y) {
        setFixedHeight(y);
        emit heightChanged();
    }
    update();
}

void PageHeader::resizeEvent(QResizeEvent *)
{
    relayout();
}

void PageHeader::enterEvent(QEnterEvent *)
{
    if (!m_editable)
        return;
    m_actionsFade.setDirection(QAbstractAnimation::Forward);
    m_actionsFade.start();
}

void PageHeader::leaveEvent(QEvent *)
{
    m_actionsFade.setDirection(QAbstractAnimation::Backward);
    m_actionsFade.start();
}

void PageHeader::pickIcon(const QPoint &globalPos)
{
    auto *picker = new IconPicker(window(), true, true);
    connect(picker, &IconPicker::picked, this, [this](const QString &icon) {
        if (Workspace *ws = Workspace::instance())
            ws->setIcon(m_pageId, icon);
    });
    picker->popupAt(globalPos);
}

void PageHeader::pickCover(const QPoint &globalPos)
{
    auto *picker = new CoverPicker(window());
    connect(picker, &CoverPicker::picked, this, [this](const QString &cover) {
        if (Workspace *ws = Workspace::instance())
            ws->setCover(m_pageId, cover, 0.5);
    });
    picker->popupAt(globalPos);
}
