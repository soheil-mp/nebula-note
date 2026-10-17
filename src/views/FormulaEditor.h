#pragma once

#include "ui/Widgets.h"

class QLabel;
class QListWidget;
class QPlainTextEdit;

// Notion's formula editor: the expression, the properties and functions to
// insert, help for the selected function and a live result for the first
// rows. Saving infers the formula's result type from what it produces.
class FormulaEditor : public Popover
{
    Q_OBJECT
public:
    FormulaEditor(QWidget *parent, const QString &databaseId, const QString &propId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updatePreview();
    void save();
    void insert(const QString &text, int cursorBack = 0);

    QString m_db;
    QString m_prop;
    QPlainTextEdit *m_edit;
    QListWidget *m_list;
    QLabel *m_help;
    QLabel *m_result;
};
