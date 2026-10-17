// Inline databases and linked views: a live database body over each
// database block. The block reserves the body's height in the layout.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"

#include "core/Workspace.h"
#include "views/DatabaseBody.h"

#include <QAbstractTextDocumentLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScrollBar>
#include <QTextBlock>

using namespace BlockFormat;
using namespace BlockEditorPrivate;

namespace
{
constexpr int kGrips = 28; // table row grips sit left of the text column

QList<View> viewsFromJson(const QJsonArray &arr)
{
    // Views go through a schema so they parse like a database's own.
    QJsonObject o;
    o.insert(QStringLiteral("views"), arr);
    return Schema::fromJson(o).views;
}

QJsonArray viewsToJson(const QList<View> &views)
{
    Schema s;
    s.views = views;
    return s.toJson().value(QStringLiteral("views")).toArray();
}
} // namespace

DatabaseBody *BlockEditor::embedFor(const QString &blockId) const
{
    return m_embeds.value(blockId).data();
}

void BlockEditor::scheduleEmbeds()
{
    if (!m_embedTimer.isActive())
        m_embedTimer.start();
}

void BlockEditor::updateEmbeds()
{
    if (!m_ws)
        return;
    QSet<QString> seen;
    bool relayout = false;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        if (typeOf(b) != BlockType::Database)
            continue;
        const QString id = idOf(b);
        const QString db = b.blockFormat().property(BlockPageProp).toString();
        if (id.isEmpty() || !m_ws->exists(db) || m_ws->page(db)->isTrashed())
            continue;
        seen.insert(id);
        QPointer<DatabaseBody> &body = m_embeds[id];
        if (!body) {
            body = new DatabaseBody(viewport(), true);
            body->setObjectName(QStringLiteral("inlineDatabase"));
            body->setProperty("blockId", id);
            connect(body, &DatabaseBody::heightChanged, this, &BlockEditor::scheduleEmbeds);
            connect(body, &DatabaseBody::openPage, this, [this](const QString &pageId) {
                flush();
                emit openPage(pageId);
            });
            connect(body, &DatabaseBody::toast, this, &BlockEditor::toast);
            body->setDatabase(db);
        } else if (body->databaseId() != db) {
            body->setDatabase(db);
        }
        // A linked view keeps its views in the block.
        const QJsonObject data = dataOf(b);
        if (data.value(QStringLiteral("linked")).toBool()) {
            const QString json = QString::fromUtf8(QJsonDocument(data.value(QStringLiteral("views")).toArray()).toJson(QJsonDocument::Compact));
            if (body->property("linkedJson").toString() != json) {
                body->setProperty("linkedJson", json);
                QPointer<BlockEditor> self(this);
                body->setLinkedViews(viewsFromJson(data.value(QStringLiteral("views")).toArray()), [self, id](const QList<View> &views) {
                    if (!self)
                        return;
                    const QTextBlock blk = self->blockById(id);
                    if (!blk.isValid())
                        return;
                    const QJsonArray arr = viewsToJson(views);
                    if (DatabaseBody *body = self->embedFor(id))
                        body->setProperty("linkedJson", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
                    self->setBlockData(blk.blockNumber(), [&](Block &p) { p.data.insert(QStringLiteral("views"), arr); });
                });
            }
        }
        if (!b.isVisible()) {
            body->hide();
            continue;
        }
        const QRectF r = objectRect(b);
        if (r.isEmpty()) {
            body->hide();
            continue;
        }
        // The body reaches from the grip gutter to the editor's right edge, so
        // wide views scroll sideways across the page like Notion's.
        const int left = qMax(0, int(r.left()) - kGrips);
        const int right = viewport()->width() - 4;
        body->setColumn(int(r.left()) - left, int(r.width()));
        body->setGeometry(left, int(r.top()), qMax(200, right - left), body->height());
        body->show();
        if (m_ctx.embedHeights.value(id) != body->height()) {
            m_ctx.embedHeights.insert(id, body->height());
            document()->markContentsDirty(b.position(), b.length());
            relayout = true;
        }
    }
    for (auto it = m_embeds.begin(); it != m_embeds.end();) {
        if (!seen.contains(it.key()) || !it.value()) {
            if (it.value())
                it.value()->deleteLater();
            m_ctx.embedHeights.remove(it.key());
            it = m_embeds.erase(it);
        } else {
            ++it;
        }
    }
    if (relayout) {
        updateHeight();
        viewport()->update();
        // Blocks below moved: place the bodies again once the layout settles.
        scheduleEmbeds();
    }
}
