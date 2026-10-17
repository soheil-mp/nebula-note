// Inline databases, links to blocks, comments and synced blocks.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "DocumentIO.h"
#include "core/Workspace.h"

#include <QApplication>
#include <QClipboard>
#include <QTextBlock>
#include <QTextTable>

using namespace BlockFormat;
using namespace BlockEditorPrivate;

void BlockEditor::insertText(const QString &text)
{
    QTextCursor c = textCursor();
    c.insertText(text);
    setTextCursor(c);
    setFocus();
}

void BlockEditor::createInlineDatabase(int viewType)
{
    if (!m_ws || m_pageId.isEmpty())
        return;
    flush();
    Schema s = Schema::basic();
    Property status;
    status.id = QStringLiteral("status");
    status.name = tr("Status");
    status.type = PropType::Status;
    status.options = {{QStringLiteral("todo"), tr("Not started"), 2, 0},
                      {QStringLiteral("doing"), tr("In progress"), 5, 1},
                      {QStringLiteral("done"), tr("Done"), 8, 2}};
    Property date;
    date.id = QStringLiteral("date");
    date.name = tr("Date");
    date.type = PropType::Date;
    s.props = {s.props[0], status, date, s.props[1]};
    s.views.clear();
    s.views.append(Schema::makeView(s, ViewType(viewType)));
    if (ViewType(viewType) != ViewType::Table)
        s.views.append(Schema::makeView(s, ViewType::Table));
    const QString db = m_ws->createDatabase(m_pageId, QString(), s);
    // A few empty rows to start from, like Notion's.
    if (ViewType(viewType) != ViewType::Form && ViewType(viewType) != ViewType::Chart)
        for (int i = 0; i < 3; ++i)
            m_ws->createRow(db);
    Block b;
    b.type = BlockType::Database;
    b.pageId = db;
    insertObjectBlock(b, false);
    m_linkedPages.insert(db);
    m_dirty = true;
    flush();
}

void BlockEditor::insertLinkedDatabase(const QString &databaseId)
{
    if (!m_ws || !m_ws->exists(databaseId))
        return;
    const Schema s = m_ws->schema(databaseId);
    Block b;
    b.type = BlockType::Database;
    b.pageId = databaseId;
    b.data.insert(QStringLiteral("linked"), true);
    // A linked view keeps views of its own.
    QJsonArray views;
    QJsonObject schemaJson = s.toJson();
    const QJsonArray all = schemaJson.value(QStringLiteral("views")).toArray();
    if (!all.isEmpty())
        views.append(all.first());
    b.data.insert(QStringLiteral("views"), views);
    insertObjectBlock(b, false);
    m_linkedPages.insert(databaseId);
    m_dirty = true;
    flush();
}

void BlockEditor::copyLinkToBlock(int blockNumber)
{
    const QTextBlock blk = document()->findBlockByNumber(blockNumber);
    if (!blk.isValid())
        return;
    QString id = idOf(blk);
    if (QTextTable *t = DocumentIO::wholeTableAt(blk, INT_MAX))
        id = t->format().property(IdProp).toString();
    if (id.isEmpty())
        return;
    QApplication::clipboard()->setText(QStringLiteral("nebula://page/%1#%2").arg(m_pageId, id));
    emit toast(tr("Copied link to block"));
}

void BlockEditor::addComment()
{
    const QTextCursor c = textCursor();
    QTextBlock blk = c.block();
    QString quote = c.selectedText();
    quote.replace(QChar::ParagraphSeparator, QLatin1Char(' ')).replace(QChar::LineSeparator, QLatin1Char(' '));
    quote.remove(QChar::ObjectReplacementCharacter);
    m_commentFrom = c.hasSelection() ? c.selectionStart() : -1;
    m_commentTo = c.hasSelection() ? c.selectionEnd() : -1;
    const QRect caret = cursorRect();
    emit commentRequested(idOf(blk), QString(), quote.trimmed(), viewport()->mapToGlobal(caret.bottomLeft() + QPoint(0, 8)));
}

void BlockEditor::applyCommentThread(int from, int to, const QString &thread)
{
    if (from < 0 || to <= from)
        return;
    QTextDocument *doc = document();
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock blk = doc->findBlock(from); blk.isValid() && blk.position() < to; blk = blk.next()) {
        const Block props = blockProps(blk);
        QList<QPair<int, int>> ranges;
        QList<QTextCharFormat> formats;
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const int a = qMax(from, f.position()), b = qMin(to, f.position() + f.length());
            if (a >= b)
                continue;
            const QTextCharFormat cf = f.charFormat();
            if (cf.objectType() >= QTextFormat::UserObject && !isInlineObject(cf.objectType()))
                continue;
            QTextCharFormat nf;
            if (isInlineObject(cf.objectType())) {
                nf = cf;
                nf.setProperty(CommentProp, thread);
            } else {
                Span s = spanOf(cf, QString(), props.type, props.checked);
                s.comment = thread;
                nf = spanFormat(s, props.type, m_ctx.style, props.checked, props.color);
            }
            ranges.append({a, b});
            formats.append(nf);
        }
        for (int i = 0; i < ranges.size(); ++i) {
            c.setPosition(ranges[i].first);
            c.setPosition(ranges[i].second, QTextCursor::KeepAnchor);
            c.setCharFormat(formats[i]);
        }
    }
    c.endEditBlock();
}

void BlockEditor::removeCommentThread(const QString &thread)
{
    QTextDocument *doc = document();
    QTextCursor c(doc);
    c.beginEditBlock();
    for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
        const Block props = blockProps(blk);
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            QTextCharFormat cf = f.charFormat();
            if (cf.property(CommentProp).toString() != thread)
                continue;
            c.setPosition(f.position());
            c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            if (isInlineObject(cf.objectType())) {
                cf.clearProperty(CommentProp);
                c.setCharFormat(cf);
            } else {
                Span s = spanOf(cf, QString(), props.type, props.checked);
                s.comment.clear();
                c.setCharFormat(spanFormat(s, props.type, m_ctx.style, props.checked, props.color));
            }
        }
    }
    c.endEditBlock();
}

void BlockEditor::turnIntoSynced()
{
    if (!m_ws || m_pageId.isEmpty())
        return;
    QTextDocument *doc = document();
    const auto range = withChildren(selectedBlockRange());
    const QString syncId = m_ws->createSynced(m_pageId, {});
    EditSteps(doc).run([&](QTextCursor &) {
        for (QTextBlock blk = doc->findBlockByNumber(range.first); blk.isValid() && blk.blockNumber() <= range.second; blk = blk.next()) {
            QTextBlockFormat bf = blk.blockFormat();
            bf.setProperty(SyncProp, syncId);
            QTextCursor(blk).setBlockFormat(bf);
            if (QTextTable *t = DocumentIO::wholeTableAt(blk, range.second)) {
                QTextTableFormat tf = t->format();
                tf.setProperty(SyncProp, syncId);
                t->setFormat(tf);
            }
        }
    });
    m_dirty = true;
    flush();
    viewport()->update();
    emit toast(tr("Synced block created — copy it and paste it on another page."));
}

void BlockEditor::unsyncBlocks(const QString &syncId)
{
    QTextDocument *doc = document();
    EditSteps(doc).run([&](QTextCursor &) {
        for (QTextBlock blk = doc->begin(); blk.isValid(); blk = blk.next()) {
            if (syncOf(blk) != syncId)
                continue;
            QTextBlockFormat bf = blk.blockFormat();
            bf.clearProperty(SyncProp);
            QTextCursor(blk).setBlockFormat(bf);
        }
        std::function<void(QTextFrame *)> walk = [&](QTextFrame *f) {
            for (QTextFrame *child : f->childFrames()) {
                if (auto *t = qobject_cast<QTextTable *>(child); t && t->format().property(SyncProp).toString() == syncId) {
                    QTextTableFormat tf = t->format();
                    tf.clearProperty(SyncProp);
                    t->setFormat(tf);
                }
                walk(child);
            }
        };
        walk(doc->rootFrame());
    });
    m_dirty = true;
    flush();
    viewport()->update();
}

void BlockEditor::refreshComments()
{
    m_openComments.clear();
    if (!m_ws || m_pageId.isEmpty())
        return;
    QSet<QString> counted;
    for (const Workspace::Comment &c : m_ws->comments(m_pageId)) {
        if (c.resolved || c.blockId.isEmpty() || counted.contains(c.thread))
            continue;
        counted.insert(c.thread);
        m_openComments[c.blockId] += 1;
    }
    viewport()->update();
}

QRectF BlockEditor::commentBubbleRect(const QTextBlock &block) const
{
    const double k = m_ctx.style.size / 16.0;
    return QRectF(viewport()->width() - rightMargin() + 10, firstLineMid(block) - 10 * k, 32 * k, 20 * k);
}
