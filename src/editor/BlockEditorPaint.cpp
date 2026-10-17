#include "BlockEditor.h"
#include "BlockEditorPrivate.h"
#include "ui/Mermaid.h"
#include <QTextTable>

#include "CodeHighlighter.h"
#include "DocumentIO.h"
#include "Popups.h"
#include "core/Ids.h"
#include "core/Markdown.h"
#include "core/Settings.h"
#include "core/Workspace.h"
#include "ui/Cosmos.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocumentFragment>
#include <QTextFrame>
#include <QTextLayout>
#include <QTextList>
#include <QUrl>
#include <QtMath>

#include <memory>

using namespace BlockFormat;
using namespace BlockEditorPrivate;

// ---- painting ------------------------------------------------------------------------------

void BlockEditor::paintEvent(QPaintEvent *e)
{
    const auto &c = Theme::colors();
    const Style &s = m_ctx.style;
    const double k = s.size / 16.0;
    const QRect rect = e->rect();
    QTextDocument *doc = document();
    const QList<QTextBlock> visible = blocksIn(rect);
    const QTextBlock cursorBlock = textCursor().block();
    const bool focused = hasFocus();
    const QHash<int, int> numbers = listNumbers();
    // The bottom of a block's visible subtree (callout boxes and quote bars
    // wrap their children).
    auto subtreeBottom = [&](const QTextBlock &blk) {
        const int end = subtreeEnd(blk);
        double bottom = blockRect(blk).bottom();
        if (end == blk.blockNumber())
            return bottom;
        for (QTextBlock b = blk.next(); b.isValid() && b.blockNumber() <= end; b = b.next()) {
            if (!b.isVisible())
                continue;
            bottom = qMax(bottom, blockRect(b).bottom());
            if (QTextTable *t = DocumentIO::tableOf(b))
                bottom = qMax(bottom, tableRect(t).bottom());
        }
        return bottom;
    };

    // ---- under the text ----
    {
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing);
        for (const QTextBlock &blk : visible) {
            const QRectF br = blockRect(blk);
            const BlockType t = typeOf(blk);
            const double left = blockLeft(blk);
            const double right = blockRight(blk);
            const int bg = blk.blockFormat().property(BlockBackgroundProp).toInt();
            if (t == BlockType::Code) {
                const QRectF box(left, br.top() - padTop(t, s), right - left, br.height() + padTop(t, s) + padBottom(t, s));
                p.setPen(QPen(QColor(0x22, 0x22, 0x28), 1));
                p.setBrush(bg ? Theme::highlightShade(bg).background.darker(160) : QColor(0x0C, 0x0C, 0x0F));
                p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 10 * k, 10 * k);
                // Mermaid flowcharts preview below the code.
                if (blk.blockFormat().property(LanguageProp).toString() == QLatin1String("mermaid")) {
                    QString src = blk.text();
                    src.replace(QChar::LineSeparator, QLatin1Char('\n'));
                    const Mermaid::Info info = Mermaid::measure(src, 13 * k, box.width() - 32 * k);
                    if (info.supported) {
                        const QRectF area(box.left(), box.bottom() + 8 * k, box.width(), info.size.height() + 24 * k);
                        p.setPen(QPen(QColor(0x22, 0x22, 0x28), 1));
                        p.setBrush(QColor(0x10, 0x10, 0x13));
                        p.drawRoundedRect(area.adjusted(0.5, 0.5, -0.5, -0.5), 10 * k, 10 * k);
                        Mermaid::paint(p, QPointF(area.center().x() - info.size.width() / 2, area.top() + 12 * k), src, 13 * k, box.width() - 32 * k);
                    }
                }
                QString caption;
                for (const Span &sp : captionOf(blk))
                    caption += sp.text;
                if (!caption.isEmpty()) {
                    p.setFont(Theme::uiFont(13.5 * k));
                    p.setPen(c.fog);
                    p.drawText(QRectF(left + 2, box.bottom() + 4 * k, right - left - 4, 20 * k), Qt::AlignLeft | Qt::AlignVCenter,
                               QFontMetricsF(p.font()).elidedText(caption, Qt::ElideRight, right - left - 4));
                }
            } else if (t == BlockType::Callout) {
                const double bottom = subtreeBottom(blk);
                const QRectF box(left, br.top() - padTop(t, s), right - left, bottom - br.top() + padTop(t, s) + padBottom(t, s));
                const Theme::Highlight hl = Theme::highlightShade(bg);
                p.setPen(QPen(bg ? hl.background.lighter(115) : c.ash, 1));
                p.setBrush(bg ? hl.background : QColor(0x1B, 0x1B, 0x20));
                p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 10 * k, 10 * k);
            } else if (bg && !blockTypeIsAtomic(t)) {
                const QRectF box(textLeft(blk) - 6 * k, br.top() - 2 * k, right - textLeft(blk) + 6 * k, br.height() + 4 * k);
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::highlightShade(bg).background);
                p.drawRoundedRect(box, 6 * k, 6 * k);
            }
            if (t == BlockType::Quote) {
                const double bottom = subtreeBottom(blk);
                p.setPen(Qt::NoPen);
                p.setBrush(blk.blockFormat().property(BlockColorProp).toInt() ? Theme::textShade(blk.blockFormat().property(BlockColorProp).toInt())
                                                                              : c.starlight);
                p.drawRoundedRect(QRectF(left + 1, br.top() + 2 * k, 3 * k, bottom - br.top() - 4 * k), 1.5 * k, 1.5 * k);
            }
        }
        // Selected blocks.
        for (const QRectF &r : blockSelectionRects()) {
            p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.16), 1));
            p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.075));
            p.drawRoundedRect(r, 5 * k, 5 * k);
        }
        // A block someone jumped to glows briefly.
        for (const Flash &f : m_flashes) {
            const QTextBlock fb = doc->findBlockByNumber(f.block);
            if (!fb.isValid() || !fb.isVisible())
                continue;
            const double t = f.clock.elapsed() / 1600.0;
            const double alpha = t < 0.15 ? t / 0.15 : qMax(0.0, 1.0 - (t - 0.15) / 0.85);
            const QRectF r = blockRect(fb);
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.09 * alpha));
            p.drawRoundedRect(QRectF(blockLeft(fb) - 6 * k, r.top() - 3 * k, blockRight(fb) - blockLeft(fb) + 12 * k, r.height() + 6 * k), 6 * k, 6 * k);
        }
        // Drop target for dragged blocks.
        if (m_draggingBlocks && m_drop.where != DropTarget::None) {
            const QTextBlock target = doc->findBlockByNumber(m_drop.block);
            if (target.isValid()) {
                QTextBlock vis = target;
                while (vis.isValid() && !vis.isVisible())
                    vis = vis.previous();
                const QRectF tr = blockRect(target.isVisible() ? target : vis);
                p.setPen(Qt::NoPen);
                if (m_drop.where == DropTarget::Left || m_drop.where == DropTarget::Right) {
                    const double x = m_drop.where == DropTarget::Left ? blockLeft(target) - 8 : blockRight(target) + 4;
                    p.setBrush(Theme::withAlpha(c.mist, 0.85));
                    p.drawRoundedRect(QRectF(x - 1.5, tr.top() - 2, 3, tr.height() + 4), 1.5, 1.5);
                } else {
                    double y;
                    if (m_drop.where == DropTarget::Before) {
                        y = tr.top() - target.blockFormat().topMargin() / 2 - 1;
                        // Right after a table: the line sits under it.
                        if (QTextTable *t = DocumentIO::tableOf(previousInContainer(target).isValid() ? doc->findBlockByNumber(target.blockNumber() - 1) : QTextBlock()))
                            y = qMax(y, tableRect(t).bottom() + 3);
                    } else if (!target.isVisible()) {
                        // The hidden helper before a table: above the table.
                        QTextTable *t = DocumentIO::tableOf(target.next());
                        y = t ? tableRect(t).top() - 4 : tr.bottom() + 2;
                    } else {
                        y = tr.bottom() + 2 + (typeOf(target) == BlockType::Code || typeOf(target) == BlockType::Callout ? padBottom(typeOf(target), s) : 0);
                    }
                    const double base = tr.left();
                    const double x = base + m_drop.depth * s.indent();
                    const double right = blockRight(target.isVisible() ? target : vis);
                    p.setBrush(Theme::withAlpha(c.mist, 0.85));
                    p.drawRoundedRect(QRectF(x, y - 1.5, right - x, 3), 1.5, 1.5);
                    p.setBrush(c.starlight);
                    p.drawEllipse(QPointF(x, y), 3.5, 3.5);
                }
            }
        }
    }

    QTextEdit::paintEvent(e);

    // ---- over the text ----
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing);
    for (const QTextBlock &blk : visible) {
        const QRectF br = blockRect(blk);
        const double right = blockRight(blk);
        const BlockType t = typeOf(blk);
        const QTextBlockFormat bf = blk.blockFormat();
        const double left = blockLeft(blk);
        const double mid = firstLineMid(blk);
        const int shade = bf.property(BlockColorProp).toInt();
        const QColor markColor = shade ? Theme::textShade(shade) : c.moon;
        const double inset = s.indent();
        switch (t) {
        case BlockType::Bullet: {
            const int d = depthOf(blk) % 3;
            const QPointF center(left + inset * 0.42, mid);
            p.setPen(Qt::NoPen);
            p.setBrush(markColor);
            if (d == 0)
                p.drawEllipse(center, 2.9 * k, 2.9 * k);
            else if (d == 1) {
                p.setPen(QPen(markColor, 1.3 * k));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(center, 2.8 * k, 2.8 * k);
            } else
                Cosmos::paintSparkle(p, center, 4.4 * k, markColor);
            break;
        }
        case BlockType::Numbered: {
            const int n = numbers.value(blk.blockNumber(), numberFor(blk));
            const int d = depthOf(blk) % 3;
            const QString label = (d == 0 ? QString::number(n) : d == 1 ? letterNumeral(n) : romanNumeral(n)) + QLatin1Char('.');
            QFont f(s.family);
            f.setPixelSize(qRound(s.size));
            f.setHintingPreference(QFont::PreferNoHinting);
            f.setFeature(QFont::Tag("tnum"), 1);
            p.setFont(f);
            p.setPen(markColor);
            const double baseline = firstBaseline(blk);
            const QFontMetricsF fm(f);
            const double w = fm.horizontalAdvance(label);
            p.drawText(QPointF(left + inset - 6 * k - w, baseline), label);
            break;
        }
        case BlockType::Todo: {
            const bool checked = bf.property(CheckedProp).toBool();
            const double box = 16 * k;
            const QRectF r(left + inset * 0.42 - box / 2, mid - box / 2, box, box);
            const bool hover = m_hoverBlock == blk.blockNumber() && m_hoverHit == Hit::Checkbox;
            if (checked) {
                p.setPen(Qt::NoPen);
                p.setBrush(c.starlight);
                p.drawRoundedRect(r, 4 * k, 4 * k);
                QPainterPath tick;
                tick.moveTo(r.left() + box * 0.26, r.top() + box * 0.53);
                tick.lineTo(r.left() + box * 0.43, r.top() + box * 0.7);
                tick.lineTo(r.left() + box * 0.75, r.top() + box * 0.33);
                p.setPen(QPen(c.basalt, 1.9 * k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(tick);
            } else {
                p.setPen(QPen(hover ? c.starlight : c.mist, 1.5 * k));
                p.setBrush(hover ? Theme::withAlpha(QColor(255, 255, 255), 0.07) : QColor(Qt::transparent));
                p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 4 * k, 4 * k);
            }
            for (const Spark &sp : m_sparks)
                if (sp.block == blk.blockNumber())
                    Cosmos::paintSparks(p, r.center(), sp.clock.elapsed() / 700.0, k);
            break;
        }
        case BlockType::Heading1:
        case BlockType::Heading2:
        case BlockType::Heading3:
        case BlockType::Heading4:
            if (!toggleableOf(blk))
                break;
            [[fallthrough]];
        case BlockType::Toggle: {
            const bool collapsed = isCollapsed(blk);
            double angle = collapsed ? 0 : 90;
            for (const ToggleAnim &a : m_toggleAnims) {
                if (a.block != blk.blockNumber())
                    continue;
                const double t = qBound(0.0, a.clock.elapsed() / 200.0, 1.0);
                const double e = 1 - (1 - t) * (1 - t);
                angle = a.opening ? 90 * e : 90 * (1 - e);
            }
            const QPointF center(left + inset * (t == BlockType::Toggle ? 0.42 : 0.4), mid);
            const bool hover = m_hoverBlock == blk.blockNumber() && m_hoverHit == Hit::ToggleArrow;
            if (hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.07));
                p.drawRoundedRect(QRectF(center.x() - 11 * k, center.y() - 11 * k, 22 * k, 22 * k), 5 * k, 5 * k);
            }
            p.save();
            p.translate(center);
            p.rotate(angle);
            QPainterPath tri;
            tri.moveTo(-3.2 * k, -4.6 * k);
            tri.lineTo(4.4 * k, 0);
            tri.lineTo(-3.2 * k, 4.6 * k);
            tri.closeSubpath();
            p.setPen(QPen(markColor, 1.0 * k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(markColor);
            p.drawPath(tri);
            p.restore();
            break;
        }
        case BlockType::Callout: {
            const QString icon = bf.property(IconProp).toString();
            const double size = 22 * k;
            const QRectF ir(left + 16 * k, br.top() + (mid - br.top()) - size / 2, size, size);
            const bool hover = m_hoverBlock == blk.blockNumber() && m_hoverHit == Hit::CalloutIcon;
            if (hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.08));
                p.drawRoundedRect(ir.adjusted(-4, -4, 4, 4), 6, 6);
            }
            Icons::paintPageIcon(p, ir, icon, c.moon, QStringLiteral("bulb"));
            break;
        }
        case BlockType::Code: {
            const double top = br.top() - padTop(t, s);
            const QString lang = CodeHighlighter::languageName(bf.property(LanguageProp).toString());
            p.setFont(Theme::uiFont(11.5 * k, QFont::Medium));
            const bool hoverLang = m_hoverBlock == blk.blockNumber() && m_hoverHit == Hit::CodeLanguage;
            p.setPen(hoverLang ? c.moon : c.fog);
            p.drawText(QRectF(left + 18 * k, top + 6 * k, 200, 20 * k), Qt::AlignLeft | Qt::AlignVCenter,
                       lang + (hoverLang ? QStringLiteral("  ▾") : QString()));
            if (m_hoverBlock == blk.blockNumber()) {
                const bool hoverCopy = m_hoverHit == Hit::CodeCopy;
                const QRectF cr(right - 64 * k, top + 6 * k, 54 * k, 20 * k);
                p.setPen(Qt::NoPen);
                p.setBrush(Theme::withAlpha(QColor(255, 255, 255), hoverCopy ? 0.1 : 0.05));
                p.drawRoundedRect(cr, 5, 5);
                p.setPen(hoverCopy ? c.starlight : c.haze);
                p.drawText(cr, Qt::AlignCenter, tr("Copy"));
            }
            break;
        }
        default:
            break;
        }

        // Placeholders.
        if (blk.length() <= 1 && !blockTypeIsAtomic(t)) {
            QString hint;
            const bool here = focused && blk == cursorBlock;
            switch (t) {
            case BlockType::Heading1:
                hint = tr("Heading 1");
                break;
            case BlockType::Heading2:
                hint = tr("Heading 2");
                break;
            case BlockType::Heading3:
                hint = tr("Heading 3");
                break;
            case BlockType::Heading4:
                hint = tr("Heading 4");
                break;
            case BlockType::Bullet:
            case BlockType::Numbered:
                hint = here ? tr("List") : QString();
                break;
            case BlockType::Todo:
                hint = tr("To-do");
                break;
            case BlockType::Toggle:
                hint = tr("Toggle");
                break;
            case BlockType::Quote:
                hint = here ? tr("Empty quote") : QString();
                break;
            case BlockType::Callout:
                hint = tr("Type something…");
                break;
            case BlockType::Paragraph:
                if (here)
                    hint = tr("Type '/' for commands");
                break;
            default:
                break;
            }
            if (!hint.isEmpty()) {
                QTextCursor bc(blk);
                QFont f = bc.blockCharFormat().font();
                f.setPixelSize(qRound(headingSize(t, s)));
                p.setFont(f);
                p.setPen(c.dust);
                p.drawText(QPointF(textLeft(blk), firstBaseline(blk)), hint);
            }
        }
    }

    // The selection box being dragged in the margin.
    if (m_bandSelecting) {
        const QRectF band = QRectF(m_bandStart, m_bandEnd).normalized();
        p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.35), 1));
        p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.06));
        p.drawRect(band);
    }

    // Column resize bar.
    if ((m_hoverGap.table || m_resizeTable) && !isReadOnly()) {
        QTextTable *t = m_resizeTable ? m_resizeTable : m_hoverGap.table;
        const int col = m_resizeTable ? m_resizeColumn : m_hoverGap.column;
        for (const ColumnGap &g : columnGaps(rect)) {
            if (g.table != t || g.column != col)
                continue;
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(c.mist, 0.55));
            p.drawRoundedRect(QRectF(g.rect.center().x() - 1.5, g.rect.top(), 3, g.rect.height()), 1.5, 1.5);
        }
    }

    // Synced blocks: an outline around each region, stronger with the caret inside.
    {
        QTextBlock runFirst, runLast;
        QString runSync;
        auto flushRun = [&] {
            if (runSync.isEmpty() || !runFirst.isValid())
                return;
            const double top = blockRect(runFirst).top() - 5 * k;
            double bottom = blockRect(runLast).bottom() + 5 * k;
            if (QTextTable *t = DocumentIO::tableOf(runLast))
                bottom = qMax(bottom, tableRect(t).bottom() + 5 * k);
            const double left = blockLeft(runFirst) - 10 * k;
            const double right = blockRight(runFirst) + 6 * k;
            const QRectF box(left, top, right - left, bottom - top);
            const int caret = textCursor().position();
            const bool active = caret >= runFirst.position() && caret <= runLast.position() + runLast.length();
            const bool hover = m_hoverBlock >= runFirst.blockNumber() && m_hoverBlock <= runLast.blockNumber();
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(Theme::withAlpha(c.mist, active ? 0.75 : (hover ? 0.45 : 0.18)), active ? 1.6 : 1.2));
            p.drawRoundedRect(box, 6 * k, 6 * k);
            if (active || hover) {
                const int pages = m_ws ? int(m_ws->syncedPages(runSync).size()) : 1;
                const QString label = pages > 1 ? tr("Editing in %1 pages").arg(pages) : tr("Synced block");
                p.setFont(Theme::uiFont(11 * k, QFont::Medium));
                const double w = QFontMetricsF(p.font()).horizontalAdvance(label) + 30 * k;
                const QRectF tag(box.right() - w - 6 * k, box.top() - 9 * k, w, 18 * k);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0x2A, 0x2A, 0x32));
                p.drawRoundedRect(tag, 9 * k, 9 * k);
                Icons::paint(p, QRectF(tag.left() + 8 * k, tag.center().y() - 6 * k, 12 * k, 12 * k), QStringLiteral("synced"), c.mist);
                p.setPen(c.moon);
                p.drawText(tag.adjusted(24 * k, 0, -6 * k, 0), Qt::AlignLeft | Qt::AlignVCenter, label);
            }
        };
        for (const QTextBlock &b : visible) {
            const QString sync = syncOf(b);
            if (sync != runSync || (runLast.isValid() && !DocumentIO::sameContainer(b, runLast) && !DocumentIO::tableOf(b))) {
                flushRun();
                runSync = sync;
                runFirst = b;
            }
            runLast = b;
        }
        flushRun();
    }

    // Comment bubbles at the right edge.
    for (const QTextBlock &b : visible) {
        const int n = m_openComments.value(idOf(b));
        if (n <= 0)
            continue;
        const QRectF r = commentBubbleRect(b);
        const bool hover = m_hoverHit == Hit::CommentBubble && m_hoverBlock == b.blockNumber();
        p.setPen(Qt::NoPen);
        p.setBrush(hover ? QColor(0x3A, 0x3A, 0x44) : QColor(0x2A, 0x2A, 0x32));
        p.drawRoundedRect(r, 5 * k, 5 * k);
        Icons::paint(p, QRectF(r.left() + 3 * k, r.center().y() - 6 * k, 12 * k, 12 * k), QStringLiteral("comment"), c.mist);
        p.setFont(Theme::uiFont(10.5 * k, QFont::DemiBold));
        p.setPen(c.moon);
        p.drawText(r.adjusted(14 * k, 0, -2 * k, 0), Qt::AlignCenter, QString::number(n));
    }

    // + bars under and beside a hovered simple table.
    if (m_hoverTable && !isReadOnly() && !m_draggingBlocks) {
        const QRectF box = simpleTableBox(m_hoverTable);
        const QRectF rowBar(box.left(), box.bottom() + 3, box.width(), 13);
        const QRectF colBar(box.right() + 3, box.top(), 13, box.height());
        for (const auto &[r, hit] : {std::pair{rowBar, Hit::TableAddRow}, std::pair{colBar, Hit::TableAddColumn}}) {
            const bool hover = m_hoverHit == hit;
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::withAlpha(QColor(255, 255, 255), hover ? 0.12 : 0.05));
            p.drawRoundedRect(r, 4, 4);
            Icons::paint(p, QRectF(r.center().x() - 6, r.center().y() - 6, 12, 12), QStringLiteral("plus"), hover ? c.starlight : c.fog);
        }
    }

    // Handles of the hovered block.
    if (m_hoverBlock >= 0 && !m_draggingBlocks && !isReadOnly()) {
        QTextBlock blk = doc->findBlockByNumber(m_hoverBlock);
        if (blk.isValid() && blk.isVisible()) {
            const double fade = m_handleFade.state() == QAbstractAnimation::Running ? m_handleFade.currentValue().toDouble() : 1.0;
            const BlockType t = typeOf(blk);
            double y = t == BlockType::Code ? blockRect(blk).top() - padTop(t, s) + 18 * k : firstLineMid(blk);
            double left = blockLeft(blk);
            if (QTextTable *table = DocumentIO::tableOf(blk); table && DocumentIO::isSimpleTable(table)) {
                left = tableRect(table).left();
                y = firstLineMid(doc->findBlockByNumber(DocumentIO::firstBlockNumber(table)));
            }
            const QRectF plus(left - 48, y - 11, 22, 22), grip(left - 25, y - 11, 18, 22);
            p.setOpacity(fade);
            for (const auto &[r, hit, icon] : {std::tuple{plus, Hit::Plus, QStringLiteral("plus")}, std::tuple{grip, Hit::Handle, QStringLiteral("grip")}}) {
                const bool hover = m_hoverHit == hit;
                if (hover) {
                    p.setPen(Qt::NoPen);
                    p.setBrush(Theme::withAlpha(QColor(255, 255, 255), 0.08));
                    p.drawRoundedRect(r, 5, 5);
                }
                Icons::paint(p, r.adjusted(3, 3, -3, -3), icon, hover ? c.starlight : c.fog);
            }
            p.setOpacity(1.0);
        }
    }

    // Image resize grips.
    const int imagePos = m_ctx.hoveredImage >= 0 ? m_ctx.hoveredImage : m_ctx.selectedImage;
    if (imagePos >= 0 && !isReadOnly()) {
        QTextBlock blk = doc->findBlock(imagePos);
        if (blk.isValid() && blk.isVisible() && typeOf(blk) == BlockType::Image) {
            QTextCursor ic(doc);
            ic.setPosition(imagePos);
            ic.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            const QSizeF sz = m_objects->intrinsicSize(doc, imagePos, ic.charFormat());
            const QRectF br = blockRect(blk);
            const double cx = (br.left() + br.right()) / 2;
            for (double x : {cx - sz.width() / 2 + 8, cx + sz.width() / 2 - 8}) {
                const QRectF grip(x - 3, br.top() + br.height() / 2 - 20, 6, 40);
                p.setPen(QPen(Theme::withAlpha(QColor(255, 255, 255), 0.85), 1));
                p.setBrush(Theme::withAlpha(QColor(0, 0, 0), 0.6));
                p.drawRoundedRect(grip, 3, 3);
            }
        }
    }
}

