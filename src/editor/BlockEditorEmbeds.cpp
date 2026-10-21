// Inline databases and linked views: a live database body over each
// database block (the block reserves the body's height in the layout); and
// video embeds playing in place, a web player over the frame.
#include "BlockEditor.h"
#include "BlockEditorPrivate.h"
#include "TextObjects.h"

#include "core/MediaLinks.h"
#include "core/Workspace.h"
#include "ui/WebPlayer.h"
#include "views/DatabaseBody.h"

#include <QAbstractTextDocumentLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

#include <cmath>

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
        // On the page, the body reaches from the grip gutter to the editor's
        // right edge, so wide views scroll sideways across the page like
        // Notion's. Inside a column it stays within the column and scrolls there.
        const bool inColumn = QTextCursor(b).currentTable() != nullptr;
        const int left = qMax(0, int(r.left()) - kGrips);
        const int right = inColumn ? int(std::ceil(r.right())) : viewport()->width() - 4;
        body->setColumn(int(r.left()) - left, int(r.width()));
        body->setGeometry(left, int(r.top()), qMax(inColumn ? 120 : 200, right - left), body->height());
        body->show();
        if (m_ctx.embedHeights.value(id) != body->height()) {
            m_ctx.embedHeights.insert(id, body->height());
            document()->markContentsDirty(b.position(), b.length());
            relayout = true;
        }
    }
    // Videos playing in place sit over their frame.
    QSet<QString> playing;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) {
        if (typeOf(b) != BlockType::Embed)
            continue;
        const QString id = idOf(b);
        if (!m_playing.contains(id))
            continue;
        const QString url = blockProps(b).url;
        if (!MediaLinks::isVideoLink(url))
            continue;
        playing.insert(id);
        QPointer<QWidget> &player = m_players[id];
        if (!player || player->property("videoUrl").toString() != url) {
            delete player;
            player = WebPlayer::create(viewport(), MediaLinks::playerUrl(url, true));
            if (!player)
                continue;
            player->setProperty("videoUrl", url);
        }
        const QRectF frame = TextObjects::videoFrame(objectRect(b));
        player->setGeometry(frame.toAlignedRect());
        player->setVisible(b.isVisible() && !frame.isEmpty());
        player->raise();
    }
    m_playing = playing;
    for (auto it = m_players.begin(); it != m_players.end();) {
        if (!playing.contains(it.key()) || !it.value()) {
            if (it.value())
                it.value()->deleteLater();
            it = m_players.erase(it);
        } else {
            ++it;
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
