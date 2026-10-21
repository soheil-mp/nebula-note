#pragma once

#include <QAccessible>
#include <QRect>
#include <QString>

class QWidget;

// Screen readers see widgets that paint their own items (the sidebar's
// pages, Quick Find's results, tabs, segmented choices, database rows…)
// through this: the widget says how many items it has and what each one is.
// Widgets inherit it next to QWidget; Accessible::install() does the rest.
class AccessibleItems
{
public:
    virtual ~AccessibleItems() = default;
    virtual QAccessible::Role accessibleRole() const = 0;     // the widget's
    virtual QAccessible::Role accessibleItemRole() const = 0; // each item's
    virtual int accessibleCount() const = 0;
    virtual QString accessibleItemName(int index) const = 0;
    virtual QString accessibleItemDescription(int index) const
    {
        Q_UNUSED(index)
        return {};
    }
    virtual QAccessible::State accessibleItemState(int index) const
    {
        Q_UNUSED(index)
        return {};
    }
    virtual QRect accessibleItemRect(int index) const = 0; // widget coordinates; empty when off screen
    virtual void accessiblePress(int index) { Q_UNUSED(index) }
    virtual int accessibleFocus() const { return -1; } // the item with keyboard focus
};

namespace Accessible
{
// Registers the interfaces for AccessibleItems widgets (once, at start).
void install();
// The items changed (added, removed, renamed).
void itemsChanged(QWidget *widget);
// The keyboard focus moved to an item of the widget.
void focusItem(QWidget *widget, int index);
// Says something without moving the focus (toasts, results).
void announce(QWidget *widget, const QString &text);
} // namespace Accessible
