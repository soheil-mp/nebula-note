#pragma once

// Helpers shared by the BlockEditor implementation files.
#include "core/Blocks.h"

#include <QString>
#include <QTextCharFormat>

namespace BlockEditorPrivate
{
inline const char *kBlocksMime = "application/x-nebula-blocks";

inline bool isTextType(BlockType t)
{
    return !blockTypeIsAtomic(t) && t != BlockType::Code;
}

QString romanNumeral(int n);
QString letterNumeral(int n);
}

#include <QTextCursor>
#include <QTextDocument>

#include <functional>

namespace BlockEditorPrivate
{
// Runs several edits as one undo step while letting Qt finish each of them:
// Qt only rescans tables when an edit block ends, so code that inserts or
// removes a table and then looks at the document's structure must not do
// both inside one open edit block.
class EditSteps
{
public:
    // `joinFirst`: the first step joins the previous undo step too.
    explicit EditSteps(QTextDocument *doc, bool joinFirst = false)
        : m_doc(doc)
        , m_started(joinFirst)
    {
    }
    void run(const std::function<void(QTextCursor &)> &edit)
    {
        QTextCursor c(m_doc);
        if (m_started)
            c.joinPreviousEditBlock();
        else
            c.beginEditBlock();
        m_started = true;
        edit(c);
        c.endEditBlock();
    }

private:
    QTextDocument *m_doc;
    bool m_started;
};
}
