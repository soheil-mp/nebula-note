#include "core/Exporter.h"
#include "core/Settings.h"
#include "core/Templates.h"
#include "core/Workspace.h"
#include "platform/SingleInstance.h"
#include "platform/WindowEffects.h"
#include "ui/ExportRenderers.h"
#include "ui/NetFetch.h"
#include "ui/Plurals.h"
#include "ui/Style.h"
#include "ui/Theme.h"
#include "views/MainWindow.h"

#include <algorithm>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QKeySequence>
#include <QContextMenuEvent>
#include <QCursor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QTimer>
#include <QTranslator>

namespace
{
void loadFonts()
{
    QDirIterator it(QStringLiteral(":/fonts"), {QStringLiteral("*.otf"), QStringLiteral("*.ttf")});
    while (it.hasNext())
        QFontDatabase::addApplicationFont(it.next());
}

int renderIcon(const QString &path, const QSize &size)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QSvgRenderer(QStringLiteral(":/icons/app/nebula.svg")).render(&p, QRectF(QPointF(0, 0), QSizeF(size)));
    p.end();
    return img.save(path) ? 0 : 1;
}

QString dataDirectory(const QString &overrideDir)
{
    if (!overrideDir.isEmpty())
        return overrideDir;
    const QByteArray env = qgetenv("NEBULA_DATA_DIR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(QStringLiteral("nebula"));
}
}

// ---- screenshot driver ---------------------------------------------------------------
// For --shot: actions that click, hover and type at window coordinates, and a
// grab that includes popups (menus, popovers live in windows of their own).
namespace ShotDriver
{
QWidget *targetAt(QWidget &window, const QPoint &globalPos, QPoint *local)
{
    // Popups on top first.
    const QList<QWidget *> tops = QApplication::topLevelWidgets();
    for (auto it = tops.crbegin(); it != tops.crend(); ++it) {
        QWidget *tl = *it;
        if (tl == &window || !tl->isVisible() || !tl->geometry().contains(globalPos))
            continue;
        QWidget *w = tl->childAt(tl->mapFromGlobal(globalPos));
        w = w ? w : tl;
        *local = w->mapFromGlobal(globalPos);
        return w;
    }
    QWidget *w = window.childAt(window.mapFromGlobal(globalPos));
    w = w ? w : &window;
    *local = w->mapFromGlobal(globalPos);
    return w;
}

void mouse(QWidget &window, const QPoint &pos, Qt::MouseButton button, bool click)
{
    const QPoint global = window.mapToGlobal(pos);
    QPoint local;
    QWidget *w = targetAt(window, global, &local);
    QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &move);
    if (!click)
        return;
    QMouseEvent press(QEvent::MouseButtonPress, local, global, button, button, Qt::NoModifier);
    QWidget *const popupBefore = QApplication::activePopupWidget();
    QApplication::sendEvent(w, &press);
    // The press may have replaced the widget. A popup it just opened (a combo
    // list) must not get the release, or it picks the item under the cursor.
    if (QApplication::activePopupWidget() == popupBefore || !QApplication::activePopupWidget())
        w = targetAt(window, global, &local);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, button, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(w, &release);
    if (button == Qt::RightButton) {
        QContextMenuEvent menu(QContextMenuEvent::Mouse, local, global);
        QApplication::sendEvent(w, &menu);
    }
}

void run(MainWindow &window, const QString &action)
{
    const QString arg = action.section(QLatin1Char(':'), 1);
    auto point = [&] { return QPoint(arg.section(QLatin1Char(','), 0, 0).toInt(), arg.section(QLatin1Char(','), 1, 1).toInt()); };
    if (action.startsWith(QLatin1String("click:")))
        mouse(window, point(), Qt::LeftButton, true);
    else if (action.startsWith(QLatin1String("rclick:")))
        mouse(window, point(), Qt::RightButton, true);
    else if (action.startsWith(QLatin1String("hoverat:")))
        mouse(window, point(), Qt::NoButton, false);
    else if (action.startsWith(QLatin1String("drag:"))) {
        // drag:x1,y1,x2,y2 — press at the first point and move to the second
        // without letting go, so the screenshot shows the drop guides.
        const QStringList n = arg.split(QLatin1Char(','));
        const QPoint from(n.value(0).toInt(), n.value(1).toInt()), to(n.value(2).toInt(), n.value(3).toInt());
        QPoint local;
        const QPoint g0 = window.mapToGlobal(from);
        QWidget *w = targetAt(window, g0, &local);
        QMouseEvent hover(QEvent::MouseMove, local, g0, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(w, &hover);
        QMouseEvent press(QEvent::MouseButtonPress, local, g0, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(w, &press);
        for (int i = 1; i <= 8; ++i) {
            const QPoint g = window.mapToGlobal(from + (to - from) * i / 8);
            QCursor::setPos(g); // code that re-reads the pointer sees it here
            QMouseEvent move(QEvent::MouseMove, w->mapFromGlobal(QPointF(g)), QPointF(g), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(w, &move);
        }
    } else if (action.startsWith(QLatin1String("enter:"))) {
        // Offscreen has no real pointer: tell the widget under the point, and
        // its parents, that the mouse came in, then move over it.
        const QPoint global = window.mapToGlobal(point());
        QPoint local;
        for (QWidget *w = targetAt(window, global, &local); w; w = w->parentWidget()) {
            w->setAttribute(Qt::WA_UnderMouse, true);
            QEnterEvent enter(w->mapFromGlobal(QPointF(global)), w->mapFromGlobal(QPointF(global)), QPointF(global));
            QApplication::sendEvent(w, &enter);
            if (w->isWindow())
                break;
        }
        mouse(window, point(), Qt::NoButton, false);
    }
    else if (action.startsWith(QLatin1String("type:"))) {
        QWidget *w = QApplication::focusWidget() ? QApplication::focusWidget() : &window;
        for (const QChar ch : arg) {
            // Keys with codes, as a keyboard sends them (shortcuts look at the key).
            const int code = ch == QLatin1Char(' ') ? Qt::Key_Space : ch.isLetterOrNumber() ? int(ch.toUpper().unicode()) : 0;
            QKeyEvent key(QEvent::KeyPress, code, Qt::NoModifier, QString(ch));
            QApplication::sendEvent(QApplication::focusWidget() ? QApplication::focusWidget() : w, &key);
        }
    } else if (action.startsWith(QLatin1String("key:"))) {
        const QKeySequence seq(arg);
        if (seq.isEmpty())
            return;
        const QKeyCombination combo = seq[0];
        QWidget *w = QApplication::focusWidget() ? QApplication::focusWidget() : &window;
        QKeyEvent key(QEvent::KeyPress, combo.key(), combo.keyboardModifiers());
        if (!QApplication::sendEvent(w, &key) || !key.isAccepted()) {
            QKeyEvent shortcut(QEvent::ShortcutOverride, combo.key(), combo.keyboardModifiers());
            QApplication::sendEvent(w, &shortcut);
        }
        QKeyEvent up(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers());
        QApplication::sendEvent(w, &up);
    } else {
        window.runAction(action);
    }
}

QImage grabAll(QWidget &window)
{
    QImage img = window.grab().toImage();
    QPainter p(&img);
    // Paint popups in stacking order: one opened from inside another (a combo
    // list inside a popover) is nested deeper in the parent chain.
    const auto depth = [](QWidget *w) {
        int d = 0;
        for (QWidget *p = w->parentWidget(); p; p = p->parentWidget())
            if (p->isWindow())
                ++d;
        return d;
    };
    QList<QWidget *> popups;
    for (QWidget *tl : QApplication::topLevelWidgets())
        if (tl != &window && tl->isVisible() && tl->width() >= 4)
            popups << tl;
    std::stable_sort(popups.begin(), popups.end(), [&](QWidget *a, QWidget *b) { return depth(a) < depth(b); });
    for (QWidget *tl : popups)
        p.drawPixmap(window.mapFromGlobal(tl->mapToGlobal(QPoint(0, 0))), tl->grab());
    return img;
}
} // namespace ShotDriver

int main(int argc, char **argv)
{
    // The internal name keeps the workspace and settings where they've always
    // been (~/.local/share/nebula, ~/.config/nebula); only the shown name changed.
    QApplication::setApplicationName(QStringLiteral("nebula"));
    QApplication::setOrganizationName(QStringLiteral("nebula"));
    QApplication::setApplicationDisplayName(QStringLiteral("Nebula Note"));
    QApplication::setApplicationVersion(QStringLiteral(NEBULA_VERSION));
    QApplication::setDesktopFileName(QStringLiteral(NEBULA_APP_ID));
    QApplication app(argc, argv);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Nebula Note — notes, docs and databases in deep space"));
    cli.addHelpOption();
    cli.addVersionOption();
    const QCommandLineOption renderIconOpt(QStringLiteral("render-icon"), QStringLiteral("Render the app icon to a PNG and exit."),
                                           QStringLiteral("file"));
    const QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Size for --render-icon or --shot (WxH)."), QStringLiteral("size"),
                                     QStringLiteral("256x256"));
    const QCommandLineOption dataOpt(QStringLiteral("data-dir"), QStringLiteral("Use another workspace folder."), QStringLiteral("dir"));
    const QCommandLineOption shotOpt(QStringLiteral("shot"), QStringLiteral("Save a screenshot of the window and exit."), QStringLiteral("file"));
    const QCommandLineOption openOpt(QStringLiteral("open"), QStringLiteral("With --shot: open the page with this title."), QStringLiteral("title"));
    const QCommandLineOption actionOpt(QStringLiteral("action"),
                                       QStringLiteral("With --shot (repeatable): quickfind, settings, templates, slash, click:x,y, rclick:x,y, "
                                                      "hoverat:x,y, type:text, key:Ctrl+K, wait:ms…"),
                                       QStringLiteral("name"));
    const QCommandLineOption exportOpt(QStringLiteral("export"),
                                       QStringLiteral("Export the page named by --open to a .md, .html or .pdf file and exit."), QStringLiteral("file"));
    cli.addOptions({renderIconOpt, sizeOpt, dataOpt, shotOpt, openOpt, actionOpt, exportOpt});
    cli.addPositionalArgument(QStringLiteral("files"), QStringLiteral("Markdown, HTML, CSV, Word, Evernote or Notion export files to import."), QStringLiteral("[files…]"));
    cli.process(app);

    const QStringList sizeParts = cli.value(sizeOpt).split(QLatin1Char('x'));
    const QSize size(sizeParts.value(0).toInt(), sizeParts.value(1, sizeParts.value(0)).toInt());
    if (cli.isSet(renderIconOpt))
        return renderIcon(cli.value(renderIconOpt), size.isValid() ? size : QSize(256, 256));

    static PluralTranslator plurals;
    QCoreApplication::installTranslator(&plurals);
    installExportRenderers();
    installImportFetcher();
    loadFonts();
    QApplication::setStyle(new NebulaStyle);
    app.setPalette(Theme::palette());
    app.setFont(Theme::uiFont(13));
    app.setStyleSheet(Theme::styleSheet());
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app/nebula.svg")));

    const bool shot = cli.isSet(shotOpt);
    // Screenshots never read or write the user's settings.
    if (shot)
        QStandardPaths::setTestModeEnabled(true);
    Settings::instance().load();
    if (shot) {
        // Screenshots use defaults, not the user's session.
        Settings &s = Settings::instance();
        s.sidebarCollapsed = false;
        s.sidebarWidth = 260;
        s.translucentSidebar = false;
        s.expanded.clear();
    }

    SingleInstance instance(QStringLiteral("nebula-") + QString::fromLocal8Bit(qgetenv("USER")));
    if (!shot && instance.forwardToRunning(QCoreApplication::arguments()))
        return 0;

    Workspace ws;
    const QString dir = dataDirectory(cli.value(dataOpt));
    if (!ws.open(dir)) {
        QMessageBox::critical(nullptr, QStringLiteral("Nebula Note"),
                              QObject::tr("The workspace in %1 couldn't be opened:\n%2").arg(dir, ws.lastError()));
        return 1;
    }
    if (ws.isFresh())
        Settings::instance().lastPage = Templates::createSampleWorkspace(ws);
    if (cli.isSet(exportOpt)) {
        // Command-line export: no window.
        const QString title = cli.value(openOpt);
        const QString out = QFileInfo(cli.value(exportOpt)).absoluteFilePath();
        QString id;
        for (const PageMeta *m : ws.allLive())
            if (m->title == title)
                id = m->id;
        if (id.isEmpty()) {
            fprintf(stderr, "No page titled \"%s\".\n", qPrintable(title));
            return 1;
        }
        const QString ext = QFileInfo(out).suffix().toLower();
        const Exporter::Result r = ext == QLatin1String("pdf")    ? Exporter::exportPdf(ws, id, out)
                                   : ext == QLatin1String("html") ? Exporter::exportHtml(ws, id, out)
                                                                  : Exporter::exportMarkdown(ws, {id}, QFileInfo(out).absolutePath(), false);
        if (!r.ok()) {
            fprintf(stderr, "%s\n", qPrintable(r.error));
            return 1;
        }
        printf("%s\n", qPrintable(r.path));
        return 0;
    }
    if (!shot) {
        instance.listen();
        ws.backup(false);
    }

    MainWindow window;
    if (!shot && !Settings::instance().geometry.isEmpty())
        window.restoreGeometry(Settings::instance().geometry);
    if (shot)
        window.resize(size.isValid() && size.width() > 400 ? size : QSize(1440, 900));
    if (!shot && Settings::instance().maximized)
        window.showMaximized();
    else
        window.show();
    window.restoreSession();
    window.handleArguments(cli.positionalArguments().isEmpty() ? QStringList() : QStringList{QString()} + cli.positionalArguments());

    // Logging out or quitting saves what was typed in the last moment.
    QObject::connect(&app, &QApplication::aboutToQuit, &window, [&window] {
        if (window.isVisible())
            window.close();
    });
    QObject::connect(&instance, &SingleInstance::messageReceived, &window, [&window](const QStringList &args, const QString &token) {
        WindowEffects::activate(&window, token);
        window.handleArguments(args);
    });

    if (shot) {
        const QString title = cli.value(openOpt);
        const QStringList actions = cli.values(actionOpt);
        const QString out = cli.value(shotOpt);
        QTimer::singleShot(300, &window, [&, title, actions, out] {
            if (title == QLatin1String("home"))
                window.goHome();
            else if (!title.isEmpty()) {
                for (const PageMeta *m : ws.allLive())
                    if (m->title == title) {
                        window.navigate(m->id);
                        break;
                    }
            }
            // Actions run one after another; "wait:<ms>" pauses longer.
            int at = 300;
            for (const QString &a : actions) {
                if (a.startsWith(QLatin1String("wait:"))) {
                    at += a.mid(5).toInt();
                    continue;
                }
                QTimer::singleShot(at, &window, [&window, a] { ShotDriver::run(window, a); });
                at += 380;
            }
            QTimer::singleShot(at + 900, &window, [&window, out] {
                ShotDriver::grabAll(window).save(out);
                QApplication::quit();
            });
        });
    }
    return app.exec();
}
