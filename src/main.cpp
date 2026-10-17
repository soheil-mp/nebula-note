#include "core/Exporter.h"
#include "core/Settings.h"
#include "core/Templates.h"
#include "core/Workspace.h"
#include "platform/SingleInstance.h"
#include "platform/WindowEffects.h"
#include "ui/ExportRenderers.h"
#include "ui/NetFetch.h"
#include "ui/Plurals.h"
#include "ui/Theme.h"
#include "views/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QPainter>
#include <QStandardPaths>
#include <QStyleFactory>
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
    const QCommandLineOption actionOpt(QStringLiteral("action"), QStringLiteral("With --shot: quickfind, settings, templates, slash."),
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
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
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
        const QString action = cli.value(actionOpt);
        const QString out = cli.value(shotOpt);
        QTimer::singleShot(300, &window, [&, title, action] {
            if (title == QLatin1String("home"))
                window.goHome();
            else if (!title.isEmpty()) {
                for (const PageMeta *m : ws.allLive())
                    if (m->title == title) {
                        window.navigate(m->id);
                        break;
                    }
            }
            if (!action.isEmpty())
                QTimer::singleShot(250, &window, [&window, action] { window.runAction(action); });
        });
        QTimer::singleShot(1700, &window, [&window, out] {
            window.grab().save(out);
            QApplication::quit();
        });
    }
    return app.exec();
}
