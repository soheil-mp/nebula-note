#include "DatabaseView.h"

#include "PageHeader.h"
#include "core/Workspace.h"

#include <QScrollBar>

DatabaseView::DatabaseView(QWidget *parent)
    : QScrollArea(parent)
{
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidgetResizable(false);
    // Transparent: the window paints the rounded page surface behind it.
    m_canvas = new QWidget;
    setWidget(m_canvas);
    m_canvas->setAutoFillBackground(false); // setWidget turns it on
    viewport()->setAutoFillBackground(false);

    m_header = new PageHeader(m_canvas);
    connect(m_header, &PageHeader::heightChanged, this, &DatabaseView::relayout);
    m_body = new DatabaseBody(m_canvas, false);
    connect(m_body, &DatabaseBody::heightChanged, this, &DatabaseView::relayout);
    connect(m_body, &DatabaseBody::openPage, this, &DatabaseView::openPage);
    connect(m_body, &DatabaseBody::toast, this, &DatabaseView::toast);
}

void DatabaseView::setDatabase(const QString &databaseId)
{
    m_header->setPage(databaseId);
    m_body->setDatabase(databaseId);
    relayout();
    verticalScrollBar()->setValue(0);
}

void DatabaseView::relayout()
{
    if (m_relayouting)
        return;
    const int W = viewport()->width();
    if (W <= 0)
        return; // not laid out yet
    m_relayouting = true;
    const int side = qMax(56, W / 13);
    const int colW = qMax(240, W - 2 * side);
    m_header->setGeometry(0, 0, W, m_header->height());
    m_header->setColumn(side, qMin(colW, 900));
    const int y = m_header->height() + 8;
    m_body->setColumn(side, colW);
    m_body->setGeometry(0, y, W, m_body->height());
    m_canvas->setFixedSize(W, y + m_body->height() + 120);
    m_relayouting = false;
}

void DatabaseView::resizeEvent(QResizeEvent *e)
{
    QScrollArea::resizeEvent(e);
    relayout();
}

void DatabaseView::flush()
{
    m_header->flushTitle();
}
