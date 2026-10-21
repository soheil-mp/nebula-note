#include "TextObjects.h"

#include "LinkPreview.h"

#include "core/DateParse.h"
#include "core/MediaLinks.h"
#include "core/Workspace.h"
#include "ui/Icons.h"
#include "ui/Tex.h"
#include "ui/Theme.h"

#include <QFileInfo>
#include <QLocale>
#include <QTextTable>

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QHash>
#include <QImageReader>
#include <QPixmapCache>

#include "ui/AnimatedMedia.h"
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFrame>

#include <cmath>

using namespace BlockFormat;

TextObjects::TextObjects(ObjectContext *context, QObject *parent)
    : QObject(parent)
    , m_ctx(context)
{
}

void TextObjects::install(QTextDocument *doc)
{
    auto *layout = doc->documentLayout();
    for (int type : {int(DividerObject), int(PageLinkObject), int(MentionObject), int(ImageObject), int(BlockObject),
                     int(InlineEquationObject), int(DateObject)})
        layout->registerHandler(type, this);
}

QSize TextObjects::naturalSize(const QString &media)
{
    static QHash<QString, QSize> cache;
    auto it = cache.constFind(media);
    if (it != cache.constEnd())
        return *it;
    QSize s;
    if (Workspace *ws = Workspace::instance()) {
        QImageReader reader(ws->mediaPath(media));
        reader.setAutoTransform(true);
        s = reader.size();
        if (s.isValid() && (reader.transformation() & QImageIOHandler::TransformationRotate90))
            s.transpose();
    }
    cache.insert(media, s);
    return s;
}

double TextObjects::availableWidth(QTextDocument *doc, int pos)
{
    const QTextBlock block = doc->findBlock(pos);
    const QTextBlockFormat bf = block.blockFormat();
    const QTextFrameFormat ff = doc->rootFrame()->frameFormat();
    // Inside a table cell (columns, simple tables), the cell is the limit. It's
    // worked out from the table's column widths: asking the layout here would
    // re-enter it, since objects are measured while the document lays out.
    if (QTextTable *t = QTextCursor(block).currentTable()) {
        const QTextTableFormat tf = t->format();
        const double frame = doc->textWidth() - ff.leftMargin() - ff.rightMargin() - 2 * doc->documentMargin() - tf.leftMargin()
                             - tf.rightMargin() - 2 * tf.border() - tf.cellSpacing() * (t->columns() + 1);
        const QTextTableCell cell = t->cellAt(block.position());
        const int col = cell.isValid() ? cell.column() : 0;
        const QList<QTextLength> constraints = tf.columnWidthConstraints();
        double w = frame / qMax(1, t->columns());
        if (col < constraints.size()) {
            const QTextLength len = constraints[col];
            if (len.type() == QTextLength::PercentageLength)
                w = frame * len.rawValue() / 100.0;
            else if (len.type() == QTextLength::FixedLength)
                w = len.rawValue();
        }
        const QTextTableCellFormat cf = cell.isValid() ? cell.format().toTableCellFormat() : QTextTableCellFormat();
        const double padding = cf.hasProperty(QTextFormat::TableCellLeftPadding) ? cf.leftPadding() + cf.rightPadding() : 2 * tf.cellPadding();
        return qMax(40.0, w - padding - bf.leftMargin() - bf.rightMargin() - 1);
    }
    return qMax(40.0, doc->textWidth() - ff.leftMargin() - ff.rightMargin() - 2 * doc->documentMargin() - bf.leftMargin()
                          - bf.rightMargin() - 1);
}

QList<TocEntry> TextObjects::headings(QTextDocument *doc)
{
    QList<TocEntry> out;
    for (QTextBlock b = doc->begin(); b.isValid(); b = b.next()) {
        const BlockType t = typeOf(b);
        if (!blockTypeIsHeading(t))
            continue;
        QString text = b.text();
        text.remove(QChar::ObjectReplacementCharacter);
        text.replace(QChar::LineSeparator, QLatin1Char(' '));
        if (text.trimmed().isEmpty())
            continue;
        const int level = t == BlockType::Heading1 ? 1 : t == BlockType::Heading2 ? 2 : t == BlockType::Heading3 ? 3 : 4;
        out.append({level, text.trimmed(), b.blockNumber()});
    }
    return out;
}

double TextObjects::tocRowHeight(const Style &style)
{
    return 28 * style.size / 16.0;
}

double TextObjects::tocTop(const Style &style)
{
    return 4 * style.size / 16.0;
}

QRectF TextObjects::videoFrame(const QRectF &objectRect)
{
    const double w = qMin(objectRect.width(), 860.0);
    return QRectF(objectRect.left(), objectRect.top() + 2, w, std::round(w * 9.0 / 16.0));
}

double TextObjects::captionHeight(const Style &style)
{
    return 28 * style.size / 16.0;
}

QList<QPair<QRectF, QString>> TextObjects::breadcrumbParts(const QRectF &body, const QString &pageId, const Style &s)
{
    QList<QPair<QRectF, QString>> out;
    Workspace *ws = Workspace::instance();
    if (!ws || pageId.isEmpty())
        return out;
    QStringList path = ws->ancestors(pageId);
    path.append(pageId);
    const double k = s.size / 16.0;
    const QFontMetricsF fm(Theme::uiFont(14 * k));
    const double iconSize = 16 * k;
    double x = body.left() + 2;
    for (int i = 0; i < path.size(); ++i) {
        const QString t = fm.elidedText(ws->title(path[i]), Qt::ElideRight, 180 * k);
        const double w = iconSize + 6 * k + fm.horizontalAdvance(t);
        out.append({QRectF(x, body.top(), w, body.height()), path[i]});
        x += w + 8 * k + fm.horizontalAdvance(QStringLiteral("/")) + 8 * k;
        if (x > body.right() - 40)
            break;
    }
    return out;
}

QRectF TextObjects::buttonRect(const QRectF &r, const QString &label, const Style &style)
{
    const double k = style.size / 16.0;
    QFont f = Theme::uiFont(14 * k, QFont::Medium);
    const double w = qMax(96 * k, QFontMetricsF(f).horizontalAdvance(label) + 52 * k);
    return QRectF(r.left(), r.top() + 3 * k, qMin(w, r.width()), r.height() - 6 * k);
}

QFont TextObjects::mentionFont(QTextDocument *doc, int pos, const Style &style)
{
    const BlockType type = typeOf(doc->findBlock(pos));
    QFont f(style.family);
    f.setPixelSize(qRound(headingSize(type, style)));
    f.setWeight(type == BlockType::Heading1 || type == BlockType::Heading2 || type == BlockType::Heading3 ? QFont::DemiBold
                                                                                                        : QFont::Medium);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

namespace
{
QString displayTitle(const QString &pageId)
{
    Workspace *ws = Workspace::instance();
    if (!ws)
        return {};
    const PageMeta *m = ws->page(pageId);
    if (!m || m->isTrashed())
        return QObject::tr("Deleted page");
    return ws->title(pageId);
}

QString fallbackGlyph(const QString &pageId)
{
    Workspace *ws = Workspace::instance();
    const PageMeta *m = ws ? ws->page(pageId) : nullptr;
    if (m && m->isDatabase())
        return QStringLiteral("database");
    return QStringLiteral("page");
}
}

namespace
{
QString humanSize(qint64 bytes)
{
    if (bytes < 1024)
        return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QObject::tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QObject::tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

QString captionText(const QTextBlock &b)
{
    QString s;
    for (const Span &sp : captionOf(b))
        if (!sp.isObject())
            s += sp.text;
    return s;
}

QString fileName(const Block &b)
{
    QString name = b.data.value(QStringLiteral("name")).toString();
    if (name.isEmpty())
        name = (b.url.isEmpty() ? b.media : b.url).section(QLatin1Char('/'), -1);
    return name.isEmpty() ? QObject::tr("Untitled file") : name;
}

QString hostOf(const QString &url)
{
    QString h = url.section(QStringLiteral("://"), 1).section(QLatin1Char('/'), 0, 0);
    if (h.startsWith(QLatin1String("www.")))
        h = h.mid(4);
    return h;
}
} // namespace

QSizeF TextObjects::intrinsicSize(QTextDocument *doc, int pos, const QTextFormat &format)
{
    const Style &s = m_ctx->style;
    switch (format.objectType()) {
    case DividerObject:
        return QSizeF(availableWidth(doc, pos), s.size * 1.0);
    case PageLinkObject:
        // About a text line tall, so a run of links keeps the page's rhythm.
        return QSizeF(availableWidth(doc, pos), s.size * 1.65);
    case MentionObject: {
        const QFont f = mentionFont(doc, pos, s);
        const QFontMetricsF fm(f);
        const QString title = displayTitle(format.property(PageIdProp).toString());
        const double icon = fm.height() * 0.78;
        // Objects sit on the baseline: report the ascent and draw the
        // descender part below the rect.
        return QSizeF(icon + 5 + fm.horizontalAdvance(title) + 3, fm.ascent());
    }
    case ImageObject: {
        const QString media = format.property(MediaProp).toString();
        const QSize natural = naturalSize(media);
        const double avail = availableWidth(doc, pos);
        const QTextBlock block = doc->findBlock(pos);
        const double cap = captionText(block).isEmpty() ? 0 : captionHeight(s);
        if (!natural.isValid() || natural.isEmpty())
            return QSizeF(qMin(avail, 420.0), 120 + cap);
        const int wanted = format.property(WidthProp).toInt();
        const bool full = block.blockFormat().property(AlignProp).toString() == QLatin1String("full");
        double w = full ? avail : (wanted > 0 ? double(wanted) : double(natural.width()));
        w = qBound(48.0, w, avail);
        return QSizeF(w, w * natural.height() / natural.width() + cap);
    }
    case InlineEquationObject: {
        const QString tex = format.property(EquationProp).toString();
        const double px = headingSize(typeOf(doc->findBlock(pos)), s);
        if (tex.trimmed().isEmpty()) {
            const QFontMetricsF fm(mentionFont(doc, pos, s));
            return QSizeF(fm.horizontalAdvance(tr("New equation")) + 30, fm.ascent());
        }
        const Tex::Metrics m = Tex::measure(tex, px * 1.05, false);
        return QSizeF(m.width + 4, m.ascent);
    }
    case DateObject: {
        const QFont f = mentionFont(doc, pos, s);
        const QFontMetricsF fm(f);
        const QString iso = format.property(DateProp).toString();
        const QDateTime dt = dateTimeOf(iso);
        const QString label = QStringLiteral("@") + DateParse::relativeLabel(dt, iso.size() > 10 && iso[10] == QLatin1Char('T'));
        const double bell = format.property(RemindProp).toBool() ? fm.height() * 0.8 + 3 : 0;
        return QSizeF(bell + fm.horizontalAdvance(label) + 3, fm.ascent());
    }
    case BlockObject: {
        const QTextBlock block = doc->findBlock(pos);
        const Block b = blockProps(block);
        const double k = s.size / 16.0;
        const double avail = availableWidth(doc, pos);
        const double cap = captionText(block).isEmpty() ? 0 : captionHeight(s);
        switch (b.type) {
        case BlockType::Equation: {
            const QString tex = b.data.value(QStringLiteral("expr")).toString();
            if (tex.trimmed().isEmpty())
                return QSizeF(avail, 48 * k);
            const Tex::Metrics m = Tex::measure(tex, s.size * 1.3, true);
            return QSizeF(avail, m.ascent + m.descent + 28 * k + cap);
        }
        case BlockType::TableOfContents: {
            const int n = headings(doc).size();
            return QSizeF(avail, n == 0 ? 36 * k : tocTop(s) * 2 + n * tocRowHeight(s));
        }
        case BlockType::Breadcrumb:
            return QSizeF(avail, 30 * k);
        case BlockType::Button:
            return QSizeF(avail, 40 * k);
        case BlockType::Bookmark: {
            const bool desc = !b.data.value(QStringLiteral("desc")).toString().isEmpty();
            return QSizeF(avail, (desc ? 104 : 72) * k + cap);
        }
        case BlockType::Embed:
            if (MediaLinks::isVideoLink(b.url))
                return QSizeF(avail, videoFrame(QRectF(0, 0, avail, 0)).height() + 4 + cap);
            return QSizeF(avail, 72 * k + cap);
        case BlockType::Video:
        case BlockType::Audio:
        case BlockType::File:
        case BlockType::Pdf:
            return QSizeF(avail, 44 * k + cap);
        case BlockType::Database: {
            const int h = m_ctx->embedHeights.value(b.id, 0);
            return QSizeF(avail, h > 0 ? h : 120 * k);
        }
        default:
            return QSizeF(avail, 30 * k);
        }
    }
    default:
        return {};
    }
}

void TextObjects::drawObject(QPainter *p, const QRectF &rect, QTextDocument *doc, int pos, const QTextFormat &format)
{
    const auto &c = Theme::colors();
    const Style &s = m_ctx->style;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::SmoothPixmapTransform);
    switch (format.objectType()) {
    case DividerObject: {
        const double y = qRound(rect.center().y()) + 0.5;
        p->setPen(QPen(c.ash, 1));
        p->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        break;
    }
    case PageLinkObject: {
        const QString id = format.property(PageIdProp).toString();
        const bool hovered = m_ctx->hoveredPageId == id;
        const QRectF row = rect.adjusted(-6, -2, 0, 2);
        if (hovered) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.05));
            p->drawRoundedRect(row, 6, 6);
        }
        Workspace *ws = Workspace::instance();
        const PageMeta *m = ws ? ws->page(id) : nullptr;
        const double iconSize = s.size * 1.2;
        const QRectF iconRect(rect.left() + 1, rect.center().y() - iconSize / 2, iconSize, iconSize);
        Icons::paintPageIcon(*p, iconRect, m ? m->icon : QString(), c.mist, fallbackGlyph(id));
        QFont f(s.family);
        f.setPixelSize(qRound(s.size));
        f.setWeight(QFont::Medium);
        f.setHintingPreference(QFont::PreferNoHinting);
        p->setFont(f);
        const QFontMetricsF fm(f);
        const QString title = fm.elidedText(displayTitle(id), Qt::ElideRight, rect.width() - iconSize - 16);
        const double x = iconRect.right() + s.size * 0.5;
        const double baseline = rect.center().y() + (fm.ascent() - fm.descent()) / 2;
        p->setPen(m && !m->isTrashed() ? c.starlight : c.dust);
        p->drawText(QPointF(x, baseline), title);
        p->setPen(QPen(hovered ? c.mist : c.smoke, 1));
        const double uy = qRound(baseline + fm.descent() * 0.6) + 0.5;
        p->drawLine(QPointF(x, uy), QPointF(x + fm.horizontalAdvance(title), uy));
        break;
    }
    case MentionObject: {
        const QString id = format.property(PageIdProp).toString();
        const bool hovered = m_ctx->hoveredPageId == id;
        const QFont f = mentionFont(doc, pos, s);
        const QFontMetricsF fm(f);
        Workspace *ws = Workspace::instance();
        const PageMeta *m = ws ? ws->page(id) : nullptr;
        const double baseline = rect.bottom();
        if (hovered) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.07));
            p->drawRoundedRect(QRectF(rect.left() - 2, rect.top(), rect.width() + 3, rect.height() + fm.descent()), 4, 4);
        }
        const double icon = fm.height() * 0.78;
        const QRectF iconRect(rect.left(), baseline - fm.capHeight() / 2 - icon / 2, icon, icon);
        Icons::paintPageIcon(*p, iconRect, m ? m->icon : QString(), c.mist, fallbackGlyph(id));
        p->setFont(f);
        const QString title = displayTitle(id);
        const double x = iconRect.right() + 5;
        p->setPen(m && !m->isTrashed() ? c.moon : c.dust);
        p->drawText(QPointF(x, baseline), title);
        p->setPen(QPen(hovered ? c.mist : c.dust, 1));
        const double uy = qRound(baseline + fm.descent() * 0.45) + 0.5;
        p->drawLine(QPointF(x, uy), QPointF(x + fm.horizontalAdvance(title), uy));
        break;
    }
    case ImageObject: {
        const QString media = format.property(MediaProp).toString();
        const QTextBlock block = doc->findBlock(pos);
        const QString caption = captionText(block);
        const double cap = caption.isEmpty() ? 0 : captionHeight(s);
        if (cap > 0) {
            p->setFont(Theme::uiFont(13.5 * s.size / 16.0));
            p->setPen(c.fog);
            p->drawText(QRectF(rect.left(), rect.bottom() - cap + 4, rect.width(), cap - 4), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                        QFontMetricsF(p->font()).elidedText(caption, Qt::ElideRight, rect.width()));
        }
        const QRectF imageRect = rect.adjusted(0, 0, 0, -cap);
        QPainterPath clip;
        clip.addRoundedRect(imageRect, 8, 8);
        Workspace *ws = Workspace::instance();
        const QSize natural = naturalSize(media);
        if (!natural.isValid() || !ws) {
            p->setPen(QPen(c.ash, 1, Qt::DashLine));
            p->setBrush(c.slate);
            p->drawPath(clip);
            Icons::paint(*p, QRectF(imageRect.center().x() - 14, imageRect.center().y() - 24, 28, 28), QStringLiteral("image"), c.dust);
            p->setPen(c.fog);
            p->setFont(Theme::uiFont(12));
            p->drawText(imageRect.adjusted(0, 30, 0, 0), Qt::AlignCenter, tr("Image not found"));
            break;
        }
        const qreal dpr = p->device() ? p->device()->devicePixelRatioF() : 1.0;
        static QHash<QString, QPixmap> cache;
        const QString key = QStringLiteral("%1|%2|%3").arg(media).arg(qRound(imageRect.width())).arg(dpr);
        QPixmap pm = cache.value(key);
        if (pm.isNull()) {
            QImageReader reader(ws->mediaPath(media));
            reader.setAutoTransform(true);
            const QSize target = (imageRect.size() * dpr).toSize();
            if (reader.size().width() > target.width() * 2)
                reader.setScaledSize(reader.size().scaled(target * 2, Qt::KeepAspectRatio));
            const QImage img = reader.read();
            pm = QPixmap::fromImage(img.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            pm.setDevicePixelRatio(dpr);
            if (cache.size() > 120)
                cache.clear();
            cache.insert(key, pm);
        }
        p->setClipPath(clip);
        if (const QImage frame = AnimatedMedia::frame(media); !frame.isNull()) {
            // GIFs play in place.
            p->setRenderHint(QPainter::SmoothPixmapTransform);
            const QSizeF fit = QSizeF(frame.size()).scaled(imageRect.size(), Qt::KeepAspectRatio);
            p->drawImage(QRectF(imageRect.left(), imageRect.top(), fit.width(), fit.height()), frame);
            AnimatedMedia::repaintOnNextFrame(*p, imageRect, media);
        } else {
            p->drawPixmap(imageRect.topLeft(), pm);
        }
        Q_UNUSED(pos)
        p->setClipping(false);
        const bool active = m_ctx->hoveredImage == pos || m_ctx->selectedImage == pos;
        p->setPen(QPen(active ? Theme::withAlpha(c.mist, 0.8) : Theme::withAlpha(QColor(255, 255, 255), 0.06), active ? 1.5 : 1));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(imageRect.adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
        break;
    }
    case InlineEquationObject: {
        const QString tex = format.property(EquationProp).toString();
        const double px = headingSize(typeOf(doc->findBlock(pos)), s);
        const bool hovered = m_ctx->hoveredObject == pos;
        const QFontMetricsF fm(mentionFont(doc, pos, s));
        if (hovered) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.07));
            p->drawRoundedRect(QRectF(rect.left() - 1, rect.top() - 2, rect.width() + 2, rect.height() + fm.descent() + 4), 4, 4);
        }
        if (tex.trimmed().isEmpty()) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.06));
            p->drawRoundedRect(QRectF(rect.left(), rect.top(), rect.width() - 2, rect.height() + fm.descent()), 4, 4);
            Icons::paint(*p, QRectF(rect.left() + 5, rect.bottom() - fm.ascent() * 0.85, fm.ascent() * 0.8, fm.ascent() * 0.8), QStringLiteral("sigma"), c.fog);
            p->setFont(mentionFont(doc, pos, s));
            p->setPen(c.fog);
            p->drawText(QPointF(rect.left() + 8 + fm.ascent() * 0.8, rect.bottom()), tr("New equation"));
            break;
        }
        Tex::paint(*p, QPointF(rect.left() + 2, rect.bottom()), tex, px * 1.05, c.moon, false);
        break;
    }
    case DateObject: {
        const QString iso = format.property(DateProp).toString();
        const QDateTime dt = dateTimeOf(iso);
        const bool remind = format.property(RemindProp).toBool();
        const QFont f = mentionFont(doc, pos, s);
        const QFontMetricsF fm(f);
        const bool hovered = m_ctx->hoveredObject == pos;
        const bool past = remind && dt.isValid() && dt < QDateTime::currentDateTime();
        if (hovered) {
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.07));
            p->drawRoundedRect(QRectF(rect.left() - 2, rect.top(), rect.width() + 3, rect.height() + fm.descent()), 4, 4);
        }
        double x = rect.left();
        if (remind) {
            const double icon = fm.height() * 0.8;
            Icons::paint(*p, QRectF(x, rect.bottom() - fm.capHeight() / 2 - icon / 2, icon, icon), QStringLiteral("bell"),
                         past ? c.starlight : c.mist);
            x += icon + 3;
        }
        p->setFont(f);
        p->setPen(past ? c.starlight : c.mist);
        const QString label = QStringLiteral("@") + DateParse::relativeLabel(dt, iso.size() > 10 && iso[10] == QLatin1Char('T'));
        p->drawText(QPointF(x, rect.bottom()), label);
        break;
    }
    case BlockObject: {
        const QTextBlock block = doc->findBlock(pos);
        const Block b = blockProps(block);
        const double k = s.size / 16.0;
        const bool hovered = m_ctx->hoveredObject == pos;
        const QString caption = captionText(block);
        const double cap = caption.isEmpty() ? 0 : captionHeight(s);
        const QRectF body = rect.adjusted(0, 0, 0, -cap);
        auto card = [&](const QRectF &r) {
            p->setPen(QPen(hovered ? c.smoke : c.ash, 1));
            p->setBrush(hovered ? QColor(0x1E, 0x1E, 0x24) : QColor(0x18, 0x18, 0x1D));
            p->drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 8 * k, 8 * k);
        };
        auto drawCaption = [&] {
            if (cap <= 0)
                return;
            p->setFont(Theme::uiFont(13.5 * k));
            p->setPen(c.fog);
            // An equation's caption sits under it, centred like the formula.
            const Qt::Alignment align = b.type == BlockType::Equation ? Qt::AlignHCenter : Qt::AlignLeft;
            p->drawText(QRectF(rect.left() + 2, body.bottom() + 4, rect.width() - 4, cap - 4), align | Qt::AlignVCenter,
                        QFontMetricsF(p->font()).elidedText(caption, Qt::ElideRight, rect.width() - 4));
        };
        switch (b.type) {
        case BlockType::Equation: {
            const QString tex = b.data.value(QStringLiteral("expr")).toString();
            if (hovered) {
                p->setPen(Qt::NoPen);
                p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.04));
                p->drawRoundedRect(body, 6 * k, 6 * k);
            }
            if (tex.trimmed().isEmpty()) {
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(0x1A, 0x1A, 0x1F));
                p->drawRoundedRect(body.adjusted(0, 3, 0, -3), 6 * k, 6 * k);
                Icons::paint(*p, QRectF(body.left() + 14 * k, body.center().y() - 9 * k, 18 * k, 18 * k), QStringLiteral("sigma"), c.fog);
                p->setFont(Theme::uiFont(14 * k));
                p->setPen(c.fog);
                p->drawText(body.adjusted(42 * k, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, tr("Add a TeX equation"));
                break;
            }
            const Tex::Metrics m = Tex::measure(tex, s.size * 1.3, true);
            const double x = body.center().x() - m.width / 2;
            const double y = body.top() + 14 * k + m.ascent;
            Tex::paint(*p, QPointF(qMax(body.left(), x), y), tex, s.size * 1.3, m.error.isEmpty() ? c.moon : c.fog, true);
            if (!m.error.isEmpty() && hovered) {
                p->setFont(Theme::uiFont(11.5 * k));
                p->setPen(c.fog);
                p->drawText(body.adjusted(8, 0, -8, -2), Qt::AlignRight | Qt::AlignBottom, m.error);
            }
            drawCaption();
            break;
        }
        case BlockType::TableOfContents: {
            const QList<TocEntry> entries = headings(doc);
            if (entries.isEmpty()) {
                p->setFont(Theme::uiFont(14 * k));
                p->setPen(c.dust);
                p->drawText(body.adjusted(4, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, tr("Add headings to create a table of contents."));
                break;
            }
            const double rowH = tocRowHeight(s);
            int minLevel = 4;
            for (const TocEntry &e : entries)
                minLevel = qMin(minLevel, e.level);
            for (int i = 0; i < entries.size(); ++i) {
                const QRectF row(body.left(), body.top() + tocTop(s) + i * rowH, body.width(), rowH);
                const bool hot = hovered && m_ctx->hoveredPart == i;
                if (hot) {
                    p->setPen(Qt::NoPen);
                    p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.05));
                    p->drawRoundedRect(row, 5 * k, 5 * k);
                }
                const double indent = (entries[i].level - minLevel) * 22 * k + 6 * k;
                p->setFont(Theme::uiFont(14.5 * k));
                const QFontMetricsF fm(p->font());
                // Quieter than body text, with a faint underline that firms up on hover.
                p->setPen(hot ? c.starlight : c.haze);
                const QString text = fm.elidedText(entries[i].text, Qt::ElideRight, row.width() - indent - 8);
                p->drawText(QPointF(row.left() + indent, row.center().y() + (fm.ascent() - fm.descent()) / 2), text);
                p->setPen(QPen(hot ? c.mist : Theme::withAlpha(QColor(255, 255, 255), 0.1), 1));
                const double uy = qRound(row.center().y() + (fm.ascent() - fm.descent()) / 2 + fm.descent() * 0.7) + 0.5;
                p->drawLine(QPointF(row.left() + indent, uy), QPointF(row.left() + indent + fm.horizontalAdvance(text), uy));
            }
            break;
        }
        case BlockType::Breadcrumb: {
            Workspace *ws = Workspace::instance();
            QStringList path;
            if (ws && !m_ctx->pageId.isEmpty()) {
                path = ws->ancestors(m_ctx->pageId);
                path.append(m_ctx->pageId);
            }
            double x = body.left() + 2;
            p->setFont(Theme::uiFont(14 * k));
            const QFontMetricsF fm(p->font());
            const double iconSize = 16 * k;
            for (int i = 0; i < path.size(); ++i) {
                const PageMeta *m = ws->page(path[i]);
                const QRectF ir(x, body.center().y() - iconSize / 2, iconSize, iconSize);
                Icons::paintPageIcon(*p, ir, m ? m->icon : QString(), c.mist, m && m->isDatabase() ? QStringLiteral("database") : QStringLiteral("page"));
                x += iconSize + 6 * k;
                const QString t = fm.elidedText(ws->title(path[i]), Qt::ElideRight, 180 * k);
                const bool hot = hovered && m_ctx->hoveredPart == i;
                p->setPen(i == path.size() - 1 || hot ? c.starlight : c.fog);
                p->drawText(QPointF(x, body.center().y() + (fm.ascent() - fm.descent()) / 2), t);
                x += fm.horizontalAdvance(t) + 8 * k;
                if (i + 1 < path.size()) {
                    p->setPen(c.dust);
                    p->drawText(QPointF(x, body.center().y() + (fm.ascent() - fm.descent()) / 2), QStringLiteral("/"));
                    x += fm.horizontalAdvance(QStringLiteral("/")) + 8 * k;
                }
                if (x > body.right() - 40)
                    break;
            }
            break;
        }
        case BlockType::Button: {
            const QString label = b.data.value(QStringLiteral("label")).toString().isEmpty() ? tr("New button")
                                                                                             : b.data.value(QStringLiteral("label")).toString();
            const QRectF br = buttonRect(body, label, s);
            const bool pressed = hovered && m_ctx->hoveredPart == 1;
            p->setPen(QPen(hovered ? c.mist : c.smoke, 1));
            p->setBrush(pressed ? QColor(0x2E, 0x2E, 0x36) : (hovered ? QColor(0x26, 0x26, 0x2D) : QColor(0x1E, 0x1E, 0x24)));
            p->drawRoundedRect(br.adjusted(0.5, 0.5, -0.5, -0.5), 7 * k, 7 * k);
            const QString icon = b.icon.isEmpty() ? QStringLiteral("glyph:cursor") : b.icon;
            Icons::paintPageIcon(*p, QRectF(br.left() + 12 * k, br.center().y() - 8 * k, 16 * k, 16 * k), icon, c.moon, QStringLiteral("cursor"));
            p->setFont(Theme::uiFont(14 * k, QFont::Medium));
            p->setPen(c.starlight);
            p->drawText(br.adjusted(36 * k, 0, -12 * k, 0), Qt::AlignLeft | Qt::AlignVCenter, label);
            break;
        }
        case BlockType::Embed:
            if (MediaLinks::isVideoLink(b.url)) {
                // A video: its thumbnail in a 16:9 frame with a play button
                // (it plays in place, or in the browser without WebEngine).
                const QRectF fr = videoFrame(body);
                QPainterPath clip;
                clip.addRoundedRect(fr, 10 * k, 10 * k);
                p->save();
                p->setClipPath(clip);
                p->fillRect(fr, QColor(0x0C, 0x0C, 0x0F));
                const QString image = b.data.value(QStringLiteral("image")).toString();
                Workspace *ws = Workspace::instance();
                if (!image.isEmpty() && ws) {
                    const QSize px = (fr.size() * p->device()->devicePixelRatioF()).toSize();
                    const QString key = QStringLiteral("video|%1|%2x%3").arg(image).arg(px.width()).arg(px.height());
                    QPixmap pm;
                    if (!QPixmapCache::find(key, &pm)) {
                        QImageReader reader(ws->mediaPath(image));
                        reader.setAutoTransform(true);
                        const QImage img = reader.read();
                        if (!img.isNull()) {
                            // Cover the frame (this also crops the letterbox bars of 4:3 thumbnails).
                            const QImage scaled = img.scaled(px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                            pm = QPixmap::fromImage(scaled.copy((scaled.width() - px.width()) / 2, (scaled.height() - px.height()) / 2, px.width(), px.height()));
                            pm.setDevicePixelRatio(p->device()->devicePixelRatioF());
                            QPixmapCache::insert(key, pm);
                        }
                    }
                    if (!pm.isNull())
                        p->drawPixmap(fr.topLeft(), pm);
                }
                // A shade at the bottom for the title.
                QLinearGradient shade(fr.bottomLeft() - QPointF(0, 90 * k), fr.bottomLeft());
                shade.setColorAt(0, QColor(0, 0, 0, 0));
                shade.setColorAt(1, QColor(0, 0, 0, 190));
                p->fillRect(QRectF(fr.left(), fr.bottom() - 90 * k, fr.width(), 90 * k), shade);
                p->restore();
                p->setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), hovered ? 0.18 : 0.08), 1));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(fr.adjusted(0.5, 0.5, -0.5, -0.5), 10 * k, 10 * k);
                const double pr = 30 * k;
                const QPointF center = fr.center();
                p->setPen(Qt::NoPen);
                p->setBrush(QColor(0x0C, 0x0C, 0x0F, hovered ? 235 : 200));
                p->drawEllipse(center, pr, pr);
                QPainterPath tri;
                tri.moveTo(center + QPointF(-8 * k, -12 * k));
                tri.lineTo(center + QPointF(13 * k, 0));
                tri.lineTo(center + QPointF(-8 * k, 12 * k));
                tri.closeSubpath();
                p->setBrush(c.starlight);
                p->drawPath(tri);
                const QString title = b.data.value(QStringLiteral("title")).toString();
                const QString who = b.data.value(QStringLiteral("site")).toString();
                const QString provider = LinkPreview::providerOf(b.url);
                p->setFont(Theme::uiFont(15 * k, QFont::DemiBold));
                QFontMetricsF tfm(p->font());
                p->setPen(QColor(0xF1, 0xF1, 0xF5));
                const double tx = fr.left() + 16 * k, tw = fr.width() - 32 * k;
                p->drawText(QRectF(tx, fr.bottom() - 52 * k, tw, 24 * k), Qt::AlignLeft | Qt::AlignVCenter,
                            tfm.elidedText(title.isEmpty() ? b.url : title, Qt::ElideRight, tw));
                p->setFont(Theme::uiFont(12.5 * k));
                p->setPen(QColor(0xC9, 0xC9, 0xD0));
                p->drawText(QRectF(tx, fr.bottom() - 30 * k, tw, 20 * k), Qt::AlignLeft | Qt::AlignVCenter,
                            who.isEmpty() ? provider : QStringLiteral("%1 · %2").arg(provider, who));
                drawCaption();
                break;
            }
            [[fallthrough]];
        case BlockType::Bookmark: {
            const QRectF r = body.adjusted(0, 2, 0, -2);
            card(r);
            const QString title = b.data.value(QStringLiteral("title")).toString();
            const QString desc = b.data.value(QStringLiteral("desc")).toString();
            const QString image = b.data.value(QStringLiteral("image")).toString();
            double textRight = r.right() - 14 * k;
            Workspace *ws = Workspace::instance();
            if (!image.isEmpty() && ws && b.type == BlockType::Bookmark) {
                const double iw = qMin(r.width() * 0.32, 220 * k);
                const QRectF ir(r.right() - iw, r.top(), iw, r.height());
                QPainterPath clip;
                clip.addRoundedRect(ir.adjusted(0, 0.5, -0.5, -0.5), 8 * k, 8 * k);
                QImageReader reader(ws->mediaPath(image));
                reader.setAutoTransform(true);
                const QImage img = reader.read();
                if (!img.isNull()) {
                    p->save();
                    p->setClipPath(clip);
                    const QSizeF scaled = QSizeF(img.size()).scaled(ir.size(), Qt::KeepAspectRatioByExpanding);
                    p->drawImage(QRectF(ir.center().x() - scaled.width() / 2, ir.center().y() - scaled.height() / 2, scaled.width(), scaled.height()), img);
                    p->restore();
                    textRight = ir.left() - 14 * k;
                }
            }
            const double x = r.left() + 16 * k;
            double y = r.top() + 14 * k;
            if (b.type == BlockType::Embed) {
                Icons::paint(*p, QRectF(x, r.center().y() - 11 * k, 22 * k, 22 * k), QStringLiteral("embed"), c.mist);
                p->setFont(Theme::uiFont(14.5 * k, QFont::Medium));
                p->setPen(c.starlight);
                const QString provider = b.data.value(QStringLiteral("provider")).toString();
                p->drawText(QRectF(x + 34 * k, r.top() + 12 * k, textRight - x - 34 * k, 22 * k), Qt::AlignLeft | Qt::AlignVCenter,
                            provider.isEmpty() ? tr("Embed") : tr("%1 embed").arg(provider));
                p->setFont(Theme::uiFont(12.5 * k));
                p->setPen(c.fog);
                p->drawText(QRectF(x + 34 * k, r.top() + 36 * k, textRight - x - 34 * k, 20 * k), Qt::AlignLeft | Qt::AlignVCenter,
                            QFontMetricsF(p->font()).elidedText(b.url, Qt::ElideMiddle, textRight - x - 34 * k));
                p->setPen(hovered ? c.moon : c.dust);
                p->drawText(QRectF(r.left(), r.top(), r.width() - 16 * k, r.height()), Qt::AlignRight | Qt::AlignVCenter, tr("Open ↗"));
                drawCaption();
                break;
            }
            p->setFont(Theme::uiFont(14.5 * k, QFont::Medium));
            QFontMetricsF fm(p->font());
            p->setPen(c.starlight);
            p->drawText(QPointF(x, y + fm.ascent()), fm.elidedText(title.isEmpty() ? (b.url.isEmpty() ? tr("Add a link") : hostOf(b.url)) : title,
                                                                    Qt::ElideRight, textRight - x));
            y += fm.height() + 6 * k;
            if (!desc.isEmpty()) {
                p->setFont(Theme::uiFont(13 * k));
                fm = QFontMetricsF(p->font());
                p->setPen(c.fog);
                const QRectF dr(x, y, textRight - x, fm.lineSpacing() * 2);
                QString d = desc;
                // Two lines at most.
                QStringList lines;
                QString line;
                for (const QString &word : d.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                    if (fm.horizontalAdvance(line + QLatin1Char(' ') + word) > dr.width() && !line.isEmpty()) {
                        lines.append(line);
                        line = word;
                        if (lines.size() == 2)
                            break;
                    } else {
                        line = line.isEmpty() ? word : line + QLatin1Char(' ') + word;
                    }
                }
                if (lines.size() < 2 && !line.isEmpty())
                    lines.append(line);
                if (lines.size() == 2)
                    lines[1] = fm.elidedText(lines[1] + QStringLiteral(" …"), Qt::ElideRight, dr.width());
                for (int i = 0; i < lines.size(); ++i)
                    p->drawText(QPointF(x, y + fm.ascent() + i * fm.lineSpacing()), lines[i]);
                y += fm.lineSpacing() * 2 + 6 * k;
            }
            Icons::paint(*p, QRectF(x, y, 14 * k, 14 * k), QStringLiteral("link"), c.dust);
            p->setFont(Theme::uiFont(12.5 * k));
            p->setPen(c.dust);
            p->drawText(QRectF(x + 20 * k, y - 2 * k, textRight - x - 20 * k, 18 * k), Qt::AlignLeft | Qt::AlignVCenter,
                        QFontMetricsF(p->font()).elidedText(b.url, Qt::ElideMiddle, textRight - x - 20 * k));
            drawCaption();
            break;
        }
        case BlockType::Video:
        case BlockType::Audio:
        case BlockType::File:
        case BlockType::Pdf: {
            const QRectF r = body.adjusted(0, 2, 0, -2);
            if (hovered) {
                p->setPen(Qt::NoPen);
                p->setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.05));
                p->drawRoundedRect(r, 6 * k, 6 * k);
            }
            const QString icon = b.type == BlockType::Video ? QStringLiteral("video")
                                 : b.type == BlockType::Audio ? QStringLiteral("audio")
                                 : b.type == BlockType::Pdf   ? QStringLiteral("pdf")
                                                              : QStringLiteral("file");
            Icons::paint(*p, QRectF(r.left() + 8 * k, r.center().y() - 10 * k, 20 * k, 20 * k), icon, c.mist);
            p->setFont(Theme::uiFont(14.5 * k));
            const QFontMetricsF fm(p->font());
            const QString name = fileName(b);
            const QString empty = b.media.isEmpty() && b.url.isEmpty()
                                      ? (b.type == BlockType::Video ? tr("Add a video") : b.type == BlockType::Audio ? tr("Add audio")
                                                                                      : b.type == BlockType::Pdf     ? tr("Add a PDF")
                                                                                                                     : tr("Add a file"))
                                      : QString();
            p->setPen(empty.isEmpty() ? c.moon : c.fog);
            const QString shown = fm.elidedText(empty.isEmpty() ? name : empty, Qt::ElideMiddle, r.width() - 160 * k);
            p->drawText(QPointF(r.left() + 38 * k, r.center().y() + (fm.ascent() - fm.descent()) / 2), shown);
            qint64 size = qint64(b.data.value(QStringLiteral("size")).toDouble());
            if (size <= 0 && !b.media.isEmpty() && Workspace::instance())
                size = QFileInfo(Workspace::instance()->mediaPath(b.media)).size();
            p->setFont(Theme::uiFont(12.5 * k));
            p->setPen(c.dust);
            const double after = r.left() + 38 * k + fm.horizontalAdvance(shown) + 10 * k;
            if (size > 0)
                p->drawText(QPointF(after, r.center().y() + 4 * k), humanSize(size));
            else if (!b.url.isEmpty())
                p->drawText(QPointF(after, r.center().y() + 4 * k), hostOf(b.url));
            if (hovered && empty.isEmpty()) {
                p->setPen(c.mist);
                p->drawText(QRectF(r.left(), r.top(), r.width() - 12 * k, r.height()), Qt::AlignRight | Qt::AlignVCenter, tr("Open ↗"));
            }
            drawCaption();
            break;
        }
        case BlockType::Database: {
            if (m_ctx->embedHeights.contains(b.id))
                break; // the live database is drawn by its own widget
            card(body.adjusted(0, 2, 0, -2));
            Workspace *ws = Workspace::instance();
            Icons::paint(*p, QRectF(body.left() + 16 * k, body.center().y() - 11 * k, 22 * k, 22 * k), QStringLiteral("database"), c.mist);
            p->setFont(Theme::uiFont(14.5 * k, QFont::Medium));
            p->setPen(c.starlight);
            p->drawText(body.adjusted(48 * k, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter,
                        ws && ws->exists(b.pageId) ? ws->title(b.pageId) : tr("Database not found"));
            break;
        }
        default:
            break;
        }
        break;
    }
    default:
        break;
    }
    p->restore();
}
