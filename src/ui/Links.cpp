#include "Links.h"

#include "Widgets.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QRegularExpression>

namespace Links
{
QUrl normalized(const QString &link)
{
    const QString text = link.trimmed();
    QUrl url(text);
    // "example.com/page" has no scheme: it's meant for the web.
    static const QRegularExpression domain(QStringLiteral("^[\\w-]+(\\.[\\w-]+)+(:\\d+)?(/.*)?$"));
    if (url.scheme().isEmpty() && domain.match(text).hasMatch())
        url = QUrl(QStringLiteral("https://") + text);
    return url;
}

bool opensDirectly(const QUrl &url)
{
    static const QStringList safe = {QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("mailto"), QStringLiteral("tel")};
    return url.isValid() && safe.contains(url.scheme().toLower());
}

void open(QWidget *parent, const QString &link)
{
    const QUrl url = normalized(link);
    if (!url.isValid() || url.isEmpty())
        return;
    if (!opensDirectly(url)) {
        const QString what = url.isLocalFile() ? QCoreApplication::translate("Links", "This opens a file on your computer:")
                                               : QCoreApplication::translate("Links", "This opens another app:");
        if (!confirmDialog(parent, QCoreApplication::translate("Links", "Open this link?"),
                           what + QStringLiteral("\n") + url.toDisplayString(QUrl::PreferLocalFile).left(300),
                           QCoreApplication::translate("Links", "Open")))
            return;
    }
    QDesktopServices::openUrl(url);
}

void openMedia(const QString &path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
} // namespace Links
