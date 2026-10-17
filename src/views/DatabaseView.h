#pragma once

#include "DatabaseBody.h"

#include <QScrollArea>

class PageHeader;

// A full-page database: the page header above the database body.
class DatabaseView : public QScrollArea
{
    Q_OBJECT
public:
    explicit DatabaseView(QWidget *parent = nullptr);

    void setDatabase(const QString &databaseId);
    QString databaseId() const { return m_body->databaseId(); }
    DatabaseBody *body() const { return m_body; }
    void createRowAndOpen() { m_body->createRowAndOpen(); }
    void selectViewNamed(const QString &name) { m_body->selectViewNamed(name); }
    void selectView(const QString &viewId) { m_body->selectView(viewId); }
    void flush();

signals:
    void openPage(const QString &pageId);
    void toast(const QString &message);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void relayout();

    QWidget *m_canvas;
    PageHeader *m_header;
    DatabaseBody *m_body;
    bool m_relayouting = false;
};
