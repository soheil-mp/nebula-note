#pragma once

#include <QScrollArea>

// The Home dashboard: greeting, recently visited, quick starts, upcoming.
class HomeView : public QScrollArea
{
    Q_OBJECT
public:
    explicit HomeView(QWidget *parent = nullptr);
    void refresh();

signals:
    void openPage(const QString &id);
    void newPage();
    void newDatabase();
    void importRequested();
    void searchRequested();

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    class HomeCanvas *m_canvas;
};
