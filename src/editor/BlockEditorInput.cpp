#include "BlockEditor.h"
#include "BlockEditorPrivate.h"
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

// ---- markdown shortcuts ------------------------------------------------------------

bool BlockEditor::applyBlockShortcut()
{
    QTextCursor c = textCursor();
    if (c.hasSelection())
        return false;
    QTextBlock blk = c.block();
    Block props = blockProps(blk);
    if (!isTextType(props.type))
        return false;
    const QString prefix = blk.text().left(c.positionInBlock());
    BlockType to = props.type;
    bool checked = false;
    static const QRegularExpression numbered(QStringLiteral("^(\\d{1,3}|[a-z]|[ivx]{1,4})[.)]$"));
    if (prefix == QLatin1String("#"))
        to = BlockType::Heading1;
    else if (prefix == QLatin1String("##"))
        to = BlockType::Heading2;
    else if (prefix == QLatin1String("###"))
        to = BlockType::Heading3;
    else if (prefix == QLatin1String("####"))
        to = BlockType::Heading4;
    else if (prefix == QLatin1String("-") || prefix == QLatin1String("*") || prefix == QLatin1String("+") || prefix == QStringLiteral("•"))
        to = BlockType::Bullet;
    else if (prefix == QLatin1String("[]") || prefix == QLatin1String("[ ]"))
        to = BlockType::Todo;
    else if (prefix == QLatin1String("[x]") || prefix == QLatin1String("[X]")) {
        to = BlockType::Todo;
        checked = true;
    } else if (prefix == QLatin1String(">"))
        to = BlockType::Toggle;
    else if (prefix == QLatin1String("\"") || prefix == QStringLiteral("“") || prefix == QLatin1String("|"))
        to = BlockType::Quote;
    else if (numbered.match(prefix).hasMatch())
        to = BlockType::Numbered;
    else if (prefix == QLatin1String("$$") && blk.text() == QLatin1String("$$")) {
        // "$$ " on an empty line: a block equation.
        c.beginEditBlock();
        c.setPosition(blk.position());
        c.setPosition(blk.position() + 2, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.endEditBlock();
        setTextCursor(c);
        insertEquation(false);
        return true;
    } else
        return false;
    if (to == props.type && to != BlockType::Todo)
        return false;
    c.beginEditBlock();
    c.setPosition(blk.position());
    c.setPosition(blk.position() + prefix.size(), QTextCursor::KeepAnchor);
    c.removeSelectedText();
    props.type = to;
    props.checked = checked;
    setBlockProps(blk, props);
    c.endEditBlock();
    setTextCursor(c);
    return true;
}

bool BlockEditor::applyInlineShortcut(QChar typed)
{
    QTextCursor c = textCursor();
    QTextBlock blk = c.block();
    const Block props = blockProps(blk);
    if (!isTextType(props.type))
        return false;
    const QString before = blk.text().left(c.positionInBlock());
    // $$x^2$$ becomes an inline equation.
    if (typed == QLatin1Char('$')) {
        static const QRegularExpression eq(QStringLiteral("\\$\\$([^$]+)\\$\\$$"));
        const auto m = eq.match(before);
        if (m.hasMatch()) {
            c.beginEditBlock();
            c.setPosition(blk.position() + int(m.capturedStart()));
            c.setPosition(blk.position() + int(m.capturedEnd()), QTextCursor::KeepAnchor);
            c.removeSelectedText();
            c.insertText(QString(QChar::ObjectReplacementCharacter), inlineEquationFormat(m.captured(1).trimmed()));
            c.endEditBlock();
            setTextCursor(c);
            setCurrentCharFormat(baseCharFormat(props.type, m_ctx.style, props.checked, props.color));
            return true;
        }
        return false;
    }
    struct Rule {
        QChar trigger;
        QRegularExpression re;
        QString what;
    };
    static const QList<Rule> rules = {
        {QLatin1Char('*'), QRegularExpression(QStringLiteral("\\*\\*([^*\\s](?:[^*]*[^*\\s])?)\\*\\*$")), QStringLiteral("bold")},
        {QLatin1Char('_'), QRegularExpression(QStringLiteral("__([^_\\s](?:[^_]*[^_\\s])?)__$")), QStringLiteral("bold")},
        {QLatin1Char('*'), QRegularExpression(QStringLiteral("(?<![*\\w])\\*([^*\\s](?:[^*]*[^*\\s])?)\\*$")), QStringLiteral("italic")},
        {QLatin1Char('_'), QRegularExpression(QStringLiteral("(?<![_\\w])_([^_\\s](?:[^_]*[^_\\s])?)_$")), QStringLiteral("italic")},
        {QLatin1Char('`'), QRegularExpression(QStringLiteral("`([^`]+)`$")), QStringLiteral("code")},
        {QLatin1Char('~'), QRegularExpression(QStringLiteral("~~([^~\\s](?:[^~]*[^~\\s])?)~~$")), QStringLiteral("strike")},
        {QLatin1Char('~'), QRegularExpression(QStringLiteral("(?<!~)~([^~\\s](?:[^~]*[^~\\s])?)~$")), QStringLiteral("strike")},
    };
    for (const Rule &r : rules) {
        if (r.trigger != typed)
            continue;
        const auto m = r.re.match(before);
        if (!m.hasMatch())
            continue;
        const int start = blk.position() + int(m.capturedStart());
        const int innerStart = blk.position() + int(m.capturedStart(1));
        const int innerEnd = innerStart + int(m.capturedLength(1));
        const int end = blk.position() + int(m.capturedEnd());
        // Inner spans keep their formatting, plus the new attribute.
        QList<Span> spans;
        for (auto it = blk.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            const int a = qMax(innerStart, f.position()), b = qMin(innerEnd, f.position() + f.length());
            if (a >= b)
                continue;
            if (f.charFormat().objectType() == MentionObject) {
                for (int i = a; i < b; ++i)
                    spans.append(spanOf(f.charFormat(), QString(), props.type, props.checked));
                continue;
            }
            Span s = spanOf(f.charFormat(), f.text().mid(a - f.position(), b - a), props.type, props.checked);
            if (r.what == QLatin1String("bold"))
                s.bold = true;
            else if (r.what == QLatin1String("italic"))
                s.italic = true;
            else if (r.what == QLatin1String("code"))
                s.code = true;
            else
                s.strike = true;
            spans.append(s);
        }
        c.beginEditBlock();
        c.setPosition(start);
        c.setPosition(end, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        for (const Span &s : spans) {
            if (!s.mention.isEmpty())
                c.insertText(QString(QChar::ObjectReplacementCharacter), mentionFormat(s.mention, m_ctx.style));
            else
                c.insertText(s.text, spanFormat(s, props.type, m_ctx.style, props.checked, props.color));
        }
        c.endEditBlock();
        setTextCursor(c);
        setCurrentCharFormat(baseCharFormat(props.type, m_ctx.style, props.checked, props.color));
        return true;
    }
    return false;
}

// ---- keyboard ------------------------------------------------------------------------

bool BlockEditor::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride) {
        // Leave the window's shortcuts alone (QTextEdit claims Ctrl+K on Linux).
        auto *ke = static_cast<QKeyEvent *>(e);
        const Qt::KeyboardModifiers mods = ke->modifiers() & ~Qt::KeypadModifier;
        const int key = ke->key();
        const bool ctrl = mods == Qt::ControlModifier;
        const bool ctrlShift = mods == (Qt::ControlModifier | Qt::ShiftModifier);
        if ((ctrl && (key == Qt::Key_P || key == Qt::Key_N || key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight
                      || key == Qt::Key_Backslash || key == Qt::Key_Comma || (key == Qt::Key_K && !textCursor().hasSelection())))
            || (ctrlShift && key == Qt::Key_U)) {
            e->ignore();
            return true;
        }
    }
    return QTextEdit::event(e);
}

void BlockEditor::keyPressEvent(QKeyEvent *e)
{
    const int key = e->key();
    const Qt::KeyboardModifiers mods = e->modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool shift = mods & Qt::ShiftModifier;
    const bool alt = mods & Qt::AltModifier;

    // Block selection takes the keys first.
    if (m_blockSel.first < 0)
        m_rangeOverride = {-1, -1};
    if (blockSelectionKey(e)) {
        m_rangeOverride = {-1, -1};
        return;
    }
    // Open menus take the navigation keys.
    CommandPopup *menu = (m_slash && m_slash->isVisible()) ? m_slash.data() : (m_mention && m_mention->isVisible()) ? m_mention.data() : nullptr;
    if (menu) {
        if (key == Qt::Key_Down || (key == Qt::Key_N && ctrl) || (key == Qt::Key_Tab && !shift)) {
            menu->moveSelection(1);
            return;
        }
        if (key == Qt::Key_Up || (key == Qt::Key_P && ctrl) || key == Qt::Key_Backtab) {
            menu->moveSelection(-1);
            return;
        }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (!menu->isEmpty()) {
                menu->activateCurrent();
                return;
            }
            menu == m_slash ? closeSlashMenu() : closeMentionMenu();
        }
        if (key == Qt::Key_Escape) {
            menu == m_slash ? closeSlashMenu() : closeMentionMenu();
            return;
        }
    }
    if (key == Qt::Key_Escape && m_formatBar && m_formatBar->isVisible()) {
        m_formatBar->dismiss();
        return;
    }
    // Esc selects the block being edited (or the selected blocks).
    if (key == Qt::Key_Escape && !(e->modifiers() & ~Qt::KeypadModifier)) {
        const auto r = textCursor().hasSelection() ? selectedBlockRange() : QPair<int, int>(textCursor().blockNumber(), textCursor().blockNumber());
        QTextBlock b = document()->findBlockByNumber(r.first);
        if (QTextTable *t = DocumentIO::tableOf(b); DocumentIO::isSimpleTable(t))
            startBlockSelection(DocumentIO::firstBlockNumber(t), DocumentIO::lastBlockNumber(t));
        else
            startBlockSelection(r.first, r.second);
        return;
    }
    // Ctrl+/ turns the block into another type.
    if (key == Qt::Key_Slash && (e->modifiers() & Qt::ControlModifier) && !isReadOnly()) {
        const QRect caret = cursorRect();
        showTurnIntoMenu(viewport()->mapToGlobal(caret.bottomLeft() + QPoint(0, 6)));
        return;
    }

    QTextCursor c = textCursor();
    QTextBlock blk = c.block();
    const BlockType type = typeOf(blk);

    if (isReadOnly()) {
        QTextEdit::keyPressEvent(e);
        return;
    }

    // ---- shortcuts ----
    if (ctrl && !alt) {
        if (shift && key >= Qt::Key_0 && key <= Qt::Key_9) {
            static const BlockType map[] = {BlockType::Paragraph, BlockType::Heading1, BlockType::Heading2, BlockType::Heading3,
                                            BlockType::Todo,      BlockType::Bullet,   BlockType::Numbered, BlockType::Toggle,
                                            BlockType::Code,      BlockType::PageLink};
            turnInto(map[key - Qt::Key_0]);
            return;
        }
        // Ctrl+Shift+digits arrive as their shifted symbols on many layouts.
        if (shift) {
            static const QHash<int, BlockType> sym = {{Qt::Key_ParenRight, BlockType::Paragraph}, {Qt::Key_Exclam, BlockType::Heading1},
                                                      {Qt::Key_At, BlockType::Heading2},          {Qt::Key_NumberSign, BlockType::Heading3},
                                                      {Qt::Key_Dollar, BlockType::Todo},          {Qt::Key_Percent, BlockType::Bullet},
                                                      {Qt::Key_AsciiCircum, BlockType::Numbered}, {Qt::Key_Ampersand, BlockType::Toggle},
                                                      {Qt::Key_Asterisk, BlockType::Code},        {Qt::Key_ParenLeft, BlockType::PageLink}};
            if (sym.contains(key)) {
                turnInto(sym.value(key));
                return;
            }
        }
        switch (key) {
        case Qt::Key_B:
            toggleInline(QStringLiteral("bold"));
            return;
        case Qt::Key_I:
            toggleInline(QStringLiteral("italic"));
            return;
        case Qt::Key_U:
            if (!shift) {
                toggleInline(QStringLiteral("underline"));
                return;
            }
            break;
        case Qt::Key_E:
            if (shift)
                insertEquation(true);
            else
                toggleInline(QStringLiteral("code"));
            return;
        case Qt::Key_M:
            if (shift) {
                addComment();
                return;
            }
            break;
        case Qt::Key_H:
            if (shift) {
                // Repeat the last shade used.
                setShade(m_lastTextShade, m_lastHighlight, !c.hasSelection());
                return;
            }
            break;
        case Qt::Key_S:
            if (shift) {
                toggleInline(QStringLiteral("strike"));
                return;
            }
            break;
        case Qt::Key_K:
            if (c.hasSelection()) {
                updateFormatBar();
                if (m_formatBar && m_formatBar->isVisible())
                    m_formatBar->editLink(c.charFormat().anchorHref());
                return;
            }
            e->ignore(); // Quick Find
            return;
        case Qt::Key_D:
            duplicateBlocks();
            return;
        case Qt::Key_Up:
            if (shift) {
                moveBlocks(-1);
                return;
            }
            break;
        case Qt::Key_Down:
            if (shift) {
                moveBlocks(1);
                return;
            }
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            // Act on the block: check, open/close, follow.
            const Block props = blockProps(blk);
            if (props.type == BlockType::Todo) {
                Block p = props;
                p.checked = !p.checked;
                c.beginEditBlock();
                setBlockProps(blk, p);
                c.endEditBlock();
                if (p.checked) {
                    Spark sp{blk.blockNumber(), {}};
                    sp.clock.start();
                    m_sparks.append(sp);
                    startAnimTick();
                }
            } else if (foldsChildren(blk)) {
                setCollapsed(blk, !isCollapsed(blk));
                updateVisibility();
                scheduleSave();
            } else if (props.type == BlockType::PageLink) {
                emit openPage(props.pageId);
            }
            return;
        }
        case Qt::Key_A:
            if (!shift) {
                // First press: the current block; second: everything.
                const int start = blk.position(), end = blk.position() + blk.length() - 1;
                if (c.selectionStart() == start && c.selectionEnd() == end && blk.length() > 1) {
                    QTextEdit::keyPressEvent(e);
                } else if (blk.length() > 1 && !(c.selectionStart() <= start && c.selectionEnd() >= end && c.hasSelection())) {
                    c.setPosition(start);
                    c.setPosition(end, QTextCursor::KeepAnchor);
                    setTextCursor(c);
                } else {
                    QTextEdit::keyPressEvent(e);
                }
                return;
            }
            break;
        case Qt::Key_P:
        case Qt::Key_N:
        case Qt::Key_BracketLeft:
        case Qt::Key_BracketRight:
            e->ignore(); // window shortcuts
            return;
        default:
            break;
        }
    }
    if (ctrl && alt && !shift && key >= Qt::Key_0 && key <= Qt::Key_9) {
        static const BlockType map[] = {BlockType::Paragraph, BlockType::Heading1, BlockType::Heading2, BlockType::Heading3,
                                        BlockType::Todo,      BlockType::Bullet,   BlockType::Numbered, BlockType::Toggle,
                                        BlockType::Code,      BlockType::PageLink};
        turnInto(map[key - Qt::Key_0]);
        return;
    }
    if (ctrl && alt && key == Qt::Key_T) {
        toggleAllToggles();
        return;
    }

    // ---- structure keys ----
    if (handleTableKey(key, mods))
        return;
    switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (shift) {
            if (type == BlockType::Code || isTextType(type)) {
                c.insertText(QString(QChar::LineSeparator));
                setTextCursor(c);
            }
            return;
        }
        if (type == BlockType::Code) {
            // New line with the current line's indentation.
            const QString text = blk.text().left(c.positionInBlock());
            const int lineStart = qMax(text.lastIndexOf(QChar::LineSeparator) + 1, 0);
            QString indent;
            for (int i = lineStart; i < text.size() && (text[i] == QLatin1Char(' ') || text[i] == QLatin1Char('\t')); ++i)
                indent += text[i];
            // Two empty lines at the end leave the code block.
            if (c.atBlockEnd() && text.endsWith(QString(QChar::LineSeparator) + QChar::LineSeparator)) {
                c.beginEditBlock();
                c.deletePreviousChar();
                c.deletePreviousChar();
                c.endEditBlock();
                Block p;
                p.depth = depthOf(blk);
                insertNewBlockAfter(blk, p);
                return;
            }
            c.insertText(QString(QChar::LineSeparator) + indent);
            setTextCursor(c);
            return;
        }
        splitBlock();
        return;
    case Qt::Key_Tab:
        if (type == BlockType::Code && !c.hasSelection()) {
            c.insertText(QStringLiteral("    "));
            return;
        }
        indentBlocks(1);
        return;
    case Qt::Key_Backtab:
        if (type == BlockType::Code) {
            const QString text = blk.text();
            const int lineStart = text.lastIndexOf(QChar::LineSeparator, qMax(0, c.positionInBlock() - 1)) + 1;
            int n = 0;
            while (n < 4 && lineStart + n < text.size() && text[lineStart + n] == QLatin1Char(' '))
                ++n;
            if (n) {
                QTextCursor d(document());
                d.setPosition(blk.position() + lineStart);
                d.setPosition(blk.position() + lineStart + n, QTextCursor::KeepAnchor);
                d.removeSelectedText();
            }
            return;
        }
        indentBlocks(-1);
        return;
    case Qt::Key_Backspace:
        if (!ctrl && handleBackspaceAtStart())
            return;
        if (blockTypeIsAtomic(type) && !c.hasSelection()) {
            // Remove the whole object block.
            Block p;
            p.depth = depthOf(blk);
            c.beginEditBlock();
            setBlockProps(blk, p);
            c.endEditBlock();
            return;
        }
        break;
    case Qt::Key_Delete:
        if (!ctrl && handleDeleteAtEnd())
            return;
        if (blockTypeIsAtomic(type) && !c.hasSelection()) {
            Block p;
            p.depth = depthOf(blk);
            c.beginEditBlock();
            setBlockProps(blk, p);
            c.endEditBlock();
            return;
        }
        break;
    case Qt::Key_Up:
        if (!shift && !ctrl && cursorOnFirstLine()) {
            emit exitTop();
            return;
        }
        break;
    case Qt::Key_Left:
        if (!shift && !ctrl && c.position() == 0 && !c.hasSelection()) {
            emit exitTop();
            return;
        }
        break;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (m_scrollArea) {
            QScrollBar *bar = m_scrollArea->verticalScrollBar();
            bar->triggerAction(key == Qt::Key_PageUp ? QAbstractSlider::SliderPageStepSub : QAbstractSlider::SliderPageStepAdd);
            const QPoint p = viewport()->mapFrom(m_scrollArea->viewport(), QPoint(gutter() + 10, m_scrollArea->viewport()->height() / 2));
            setTextCursor(cursorForPosition(p));
            return;
        }
        break;
    default:
        break;
    }

    // Typing into an object block starts a paragraph below it.
    const QString text = e->text();
    if (blockTypeIsAtomic(type) && !text.isEmpty() && text.at(0).isPrint() && !ctrl) {
        Block p;
        p.depth = depthOf(blk);
        insertNewBlockAfter(blk, p);
        textCursor().insertText(text);
        return;
    }

    // Markdown block shortcuts fire on the space after the marker.
    if (key == Qt::Key_Space && !ctrl && applyBlockShortcut())
        return;

    QTextEdit::keyPressEvent(e);

    if (text.isEmpty() || ctrl)
        return;
    const QChar ch = text.at(0);
    c = textCursor();
    blk = c.block();
    const QString before = blk.text().left(c.positionInBlock());
    if (ch == QLatin1Char('/') && type != BlockType::Code) {
        if (before.size() == 1 || before.at(before.size() - 2).isSpace())
            openSlashMenu();
        return;
    }
    if (ch == QLatin1Char('@') && type != BlockType::Code) {
        if (before.size() == 1 || before.at(before.size() - 2).isSpace())
            openMentionMenu(QStringLiteral("@"));
        return;
    }
    if (ch == QLatin1Char('[') && before.endsWith(QLatin1String("[[")) && type != BlockType::Code) {
        openMentionMenu(QStringLiteral("[["));
        return;
    }
    // "+" at the start of a block links or creates a page.
    if (ch == QLatin1Char('+') && before == QLatin1String("+") && type == BlockType::Paragraph
        && !DocumentIO::isSimpleTable(DocumentIO::tableOf(blk))) {
        openMentionMenu(QStringLiteral("+"));
        return;
    }
    // ":" starts an emoji shortcode after a space or at the start.
    if (ch == QLatin1Char(':') && type != BlockType::Code && (before.size() == 1 || before.at(before.size() - 2).isSpace())) {
        openMentionMenu(QStringLiteral(":"));
        return;
    }
    if (ch == QLatin1Char('-') && blk.text() == QLatin1String("---") && type == BlockType::Paragraph) {
        Block d;
        d.type = BlockType::Divider;
        d.depth = depthOf(blk);
        c.beginEditBlock();
        setBlockProps(blk, d);
        c.endEditBlock();
        Block p;
        p.depth = d.depth;
        insertNewBlockAfter(blk, p);
        return;
    }
    if (ch == QLatin1Char('`') && blk.text() == QLatin1String("```") && isTextType(type)) {
        c.beginEditBlock();
        c.select(QTextCursor::BlockUnderCursor);
        c.setPosition(blk.position());
        c.setPosition(blk.position() + 3, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        Block props = blockProps(blk);
        props.type = BlockType::Code;
        props.language = QStringLiteral("plain");
        setBlockProps(blk, props);
        c.endEditBlock();
        return;
    }
    if (ch == QLatin1Char('*') || ch == QLatin1Char('_') || ch == QLatin1Char('`') || ch == QLatin1Char('~') || ch == QLatin1Char('$'))
        applyInlineShortcut(ch);
}

void BlockEditor::inputMethodEvent(QInputMethodEvent *e)
{
    QTextEdit::inputMethodEvent(e);
    if (m_slash && m_slash->isVisible())
        updateSlashMenu();
    if (m_mention && (m_mention->isVisible() || m_mentionPos >= 0))
        updateMentionMenu();
}

