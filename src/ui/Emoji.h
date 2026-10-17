#pragma once

#include <QList>
#include <QPair>
#include <QString>

// Named emoji for ":" autocomplete in the editor.
namespace Emoji
{
// (name, emoji) pairs; names use Slack/GitHub shortcodes.
const QList<QPair<QString, QString>> &all();
// Emoji whose name contains the query, best (prefix) matches first.
QList<QPair<QString, QString>> search(const QString &query, int limit = 12);
}
