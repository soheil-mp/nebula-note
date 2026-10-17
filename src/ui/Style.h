#pragma once

#include <QProxyStyle>

// Fusion with Nebula's menu items: a rounded hover, icons in a column, and
// shortcuts dimmed and right-aligned the way Notion shows them. Menus are
// drawn here entirely: a style sheet rule for QMenu would take over.
class NebulaStyle : public QProxyStyle
{
    Q_OBJECT
public:
    NebulaStyle();
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size, const QWidget *widget = nullptr) const override;
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget = nullptr) const override;
    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
};
