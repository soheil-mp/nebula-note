#include "Accessible.h"

#include <QAccessibleWidget>
#include <QHash>
#include <QWidget>

namespace
{
class ItemsInterface;

// One painted item: a child with no widget of its own.
class ItemInterface : public QAccessibleInterface, public QAccessibleActionInterface
{
public:
    ItemInterface(ItemsInterface *parent, int index)
        : m_parent(parent)
        , m_index(index)
    {
    }
    bool isValid() const override;
    QObject *object() const override { return nullptr; }
    QWindow *window() const override;
    QAccessibleInterface *parent() const override;
    QAccessibleInterface *child(int) const override { return nullptr; }
    int childCount() const override { return 0; }
    int indexOfChild(const QAccessibleInterface *) const override { return -1; }
    QAccessibleInterface *childAt(int, int) const override { return nullptr; }
    QAccessibleInterface *focusChild() const override { return nullptr; }
    QString text(QAccessible::Text t) const override;
    void setText(QAccessible::Text, const QString &) override {}
    QRect rect() const override;
    QAccessible::Role role() const override;
    QAccessible::State state() const override;
    void *interface_cast(QAccessible::InterfaceType t) override
    {
        return t == QAccessible::ActionInterface ? static_cast<QAccessibleActionInterface *>(this) : nullptr;
    }
    QStringList actionNames() const override { return {pressAction()}; }
    void doAction(const QString &name) override;
    QStringList keyBindingsForAction(const QString &) const override { return {}; }
    int index() const { return m_index; }

private:
    ItemsInterface *m_parent;
    int m_index;
};

class ItemsInterface : public QAccessibleWidget
{
public:
    ItemsInterface(QWidget *w, AccessibleItems *items)
        : QAccessibleWidget(w, items->accessibleRole())
        , m_items(items)
    {
    }
    ~ItemsInterface() override { clearItems(); }

    AccessibleItems *items() const { return m_items; }
    QWidget *owner() const { return widget(); }

    // Items first, then real child widgets (a rename field…).
    int childCount() const override { return m_items->accessibleCount() + QAccessibleWidget::childCount(); }
    QAccessibleInterface *child(int index) const override
    {
        const int n = m_items->accessibleCount();
        if (index < 0)
            return nullptr;
        return index < n ? item(index) : QAccessibleWidget::child(index - n);
    }
    int indexOfChild(const QAccessibleInterface *child) const override
    {
        if (const auto *it = dynamic_cast<const ItemInterface *>(child))
            return it->index();
        const int i = QAccessibleWidget::indexOfChild(child);
        return i < 0 ? -1 : i + m_items->accessibleCount();
    }
    QAccessibleInterface *childAt(int x, int y) const override
    {
        const QPoint local = widget()->mapFromGlobal(QPoint(x, y));
        for (int i = 0; i < m_items->accessibleCount(); ++i)
            if (m_items->accessibleItemRect(i).contains(local))
                return item(i);
        return QAccessibleWidget::childAt(x, y);
    }
    QAccessibleInterface *focusChild() const override
    {
        const int f = m_items->accessibleFocus();
        if (f >= 0 && f < m_items->accessibleCount() && widget()->hasFocus())
            return item(f);
        return QAccessibleWidget::focusChild();
    }

    ItemInterface *item(int index) const
    {
        auto it = m_children.constFind(index);
        if (it != m_children.constEnd())
            return *it;
        auto *child = new ItemInterface(const_cast<ItemsInterface *>(this), index);
        QAccessible::registerAccessibleInterface(child);
        m_children.insert(index, child);
        return child;
    }
    void clearItems()
    {
        for (ItemInterface *child : std::as_const(m_children))
            QAccessible::deleteAccessibleInterface(QAccessible::uniqueId(child));
        m_children.clear();
    }

private:
    AccessibleItems *m_items;
    mutable QHash<int, ItemInterface *> m_children;
};

bool ItemInterface::isValid() const
{
    return m_parent && m_parent->isValid() && m_index < m_parent->items()->accessibleCount();
}

QWindow *ItemInterface::window() const
{
    return m_parent->window();
}

QAccessibleInterface *ItemInterface::parent() const
{
    return m_parent;
}

QString ItemInterface::text(QAccessible::Text t) const
{
    if (!isValid())
        return {};
    switch (t) {
    case QAccessible::Name:
        return m_parent->items()->accessibleItemName(m_index);
    case QAccessible::Description:
        return m_parent->items()->accessibleItemDescription(m_index);
    default:
        return {};
    }
}

QRect ItemInterface::rect() const
{
    if (!isValid())
        return {};
    const QRect r = m_parent->items()->accessibleItemRect(m_index);
    return r.isEmpty() ? QRect() : QRect(m_parent->owner()->mapToGlobal(r.topLeft()), r.size());
}

QAccessible::Role ItemInterface::role() const
{
    return m_parent->items()->accessibleItemRole();
}

QAccessible::State ItemInterface::state() const
{
    QAccessible::State s;
    if (!isValid()) {
        s.invalid = true;
        return s;
    }
    s = m_parent->items()->accessibleItemState(m_index);
    s.focusable = true;
    s.selectable = true;
    if (m_parent->items()->accessibleFocus() == m_index && m_parent->owner()->hasFocus())
        s.focused = true;
    if (m_parent->items()->accessibleItemRect(m_index).isEmpty())
        s.offscreen = true;
    return s;
}

void ItemInterface::doAction(const QString &name)
{
    if (name == pressAction() && isValid())
        m_parent->items()->accessiblePress(m_index);
}

QAccessibleInterface *factory(const QString &, QObject *object)
{
    if (auto *w = qobject_cast<QWidget *>(object))
        if (auto *items = dynamic_cast<AccessibleItems *>(w))
            return new ItemsInterface(w, items);
    return nullptr;
}
} // namespace

namespace Accessible
{
void install()
{
    QAccessible::installFactory(factory);
}

void itemsChanged(QWidget *widget)
{
    if (!QAccessible::isActive() || !widget)
        return;
    if (auto *iface = dynamic_cast<ItemsInterface *>(QAccessible::queryAccessibleInterface(widget)))
        iface->clearItems();
    QAccessibleEvent e(widget, QAccessible::ObjectReorder);
    QAccessible::updateAccessibility(&e);
}

void focusItem(QWidget *widget, int index)
{
    if (!QAccessible::isActive() || !widget || index < 0)
        return;
    QAccessibleEvent e(widget, QAccessible::Focus);
    e.setChild(index);
    QAccessible::updateAccessibility(&e);
}

void announce(QWidget *widget, const QString &text)
{
    if (!QAccessible::isActive() || !widget || text.isEmpty())
        return;
    QAccessibleAnnouncementEvent e(widget, text);
    QAccessible::updateAccessibility(&e);
}
} // namespace Accessible
