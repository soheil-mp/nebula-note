#include "WebPlayer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>

namespace
{
constexpr int kAbi = 1;
using Create = QWidget *(*)(QWidget *, const QString &);
using Abi = int (*)();

QString pluginPath()
{
    static const QString path = [] {
        QStringList dirs;
        if (const QByteArray env = qgetenv("NEBULA_PLUGIN_DIR"); !env.isEmpty())
            dirs << QString::fromLocal8Bit(env);
        // Next to the program (a build), or its lib/ folder (installed).
        const QString app = QCoreApplication::applicationDirPath();
        dirs << app << app + QStringLiteral("/../lib/nebula-note") << app + QStringLiteral("/../lib64/nebula-note");
#ifdef NEBULA_PLUGIN_DIR
        dirs << QStringLiteral(NEBULA_PLUGIN_DIR);
#endif
        for (const QString &dir : std::as_const(dirs)) {
            const QString file = QDir(dir).filePath(QStringLiteral("nebula-webplayer.so"));
            if (QFileInfo::exists(file))
                return QFileInfo(file).canonicalFilePath();
        }
        return QString();
    }();
    return path;
}

Create factory()
{
    static Create create = [] () -> Create {
        if (pluginPath().isEmpty())
            return nullptr;
        static QLibrary lib(pluginPath());
        if (!lib.load()) {
            qWarning("Nebula Note: the video player couldn't be loaded: %s", qPrintable(lib.errorString()));
            return nullptr;
        }
        const auto abi = reinterpret_cast<Abi>(lib.resolve("nebula_webplayer_abi"));
        if (!abi || abi() != kAbi) {
            qWarning("Nebula Note: the video player plugin doesn't match this version");
            return nullptr;
        }
        return reinterpret_cast<Create>(lib.resolve("nebula_webplayer_create"));
    }();
    return create;
}
} // namespace

bool WebPlayer::available()
{
    return !pluginPath().isEmpty();
}

QWidget *WebPlayer::create(QWidget *parent, const QString &playerUrl)
{
    const Create make = factory();
    return make ? make(parent, playerUrl) : nullptr;
}
