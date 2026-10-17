#pragma once

#include "core/Schema.h"

#include <QJsonValue>
#include <QRectF>
#include <QWidget>

class QPainter;

// Editors and painters for database property values, shared by the row
// property panel, the table, board, gallery, list and calendar views.
namespace PropertyEditors
{
// Opens the right editor for one value; changes go straight to the workspace.
void edit(QWidget *parent, const QPoint &globalPos, const QString &databaseId, const QString &rowId, const QString &propId,
          int minWidth = 0);

// Paints a value inside `rect` (vertically centred). `align` is used for
// numbers and text.
void paintValue(QPainter &p, const QRectF &rect, const Property &prop, const QJsonValue &value, bool dim = false,
                Qt::Alignment align = Qt::AlignLeft);
// Width the painted value needs (pills etc.).
double valueWidth(const Property &prop, const QJsonValue &value);

// Property header menu: rename, type, number format, sort, filter, hide, delete.
void showPropertyMenu(QWidget *parent, const QPoint &globalPos, const QString &databaseId, const QString &propId,
                      const QString &viewId = {});
// Adds a property (asks for the type), optionally visible in a view.
void addProperty(QWidget *parent, const QPoint &globalPos, const QString &databaseId, const QString &viewId = {});
}

// The properties of a database row, shown under the row page's title.
class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget *parent = nullptr);
    void setRow(const QString &rowId);
    QString rowId() const { return m_rowId; }
    void refresh();

signals:
    void heightChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    struct Line {
        QString propId;
        QRectF nameRect;
        QRectF valueRect;
    };
    QString m_rowId;
    QString m_dbId;
    Schema m_schema;
    QJsonObject m_props;
    QList<Line> m_lines;
    QRectF m_addRect;
    int m_hover = -1; // line index, -2 = add row
    bool m_hoverName = false;
};
