#!/usr/bin/env bash
# Builds Nebula Note and installs it for the current user under ~/.local:
#   ~/.local/bin/nebula-note                     the program
#   ~/.local/share/applications/<id>.desktop     app menu entry
#   ~/.local/share/icons/hicolor/...             the icon
#   "./Nebula Note.desktop"                      double-click launcher in this folder
#
# Usage: ./install.sh [--desktop]
#   --desktop   also put a Nebula Note launcher on the desktop
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"
ROOT="$(pwd)"

APP_ID="io.github.soheil_mp.NebulaNote"
OLD_APP_ID="io.github.soheil_mp.Nebula" # before the rename to Nebula Note
PREFIX="${PREFIX:-$HOME/.local}"
BUILD_DIR="${BUILD_DIR:-build-release}"
DESKTOP_ICON=0

for arg in "$@"; do
    case "$arg" in
        --desktop) DESKTOP_ICON=1 ;;
        -h|--help)
            sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) echo "Unknown option: $arg" >&2; exit 2 ;;
    esac
done

deps_hint() {
    echo "Install the build dependencies first:"
    if command -v apt-get >/dev/null; then
        echo "  sudo apt install cmake ninja-build g++ qt6-base-dev qt6-svg-dev zlib1g-dev libkf6windowsystem-dev qt6-webengine-dev"
    elif command -v dnf >/dev/null; then
        echo "  sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtsvg-devel zlib-devel kf6-kwindowsystem-devel qt6-qtwebengine-devel"
    elif command -v pacman >/dev/null; then
        echo "  sudo pacman -S --needed cmake ninja gcc qt6-base qt6-svg zlib kwindowsystem qt6-webengine"
    elif command -v zypper >/dev/null; then
        echo "  sudo zypper install cmake ninja gcc-c++ qt6-base-devel qt6-svg-devel zlib-devel kf6-kwindowsystem-devel qt6-webengine-devel"
    else
        echo "  CMake, Ninja, a C++20 compiler, Qt 6.5+ (Base, Svg; optionally WebEngine), zlib, optionally KF6 WindowSystem"
    fi
    echo "(or build the Flatpak: see packaging/flatpak/io.github.soheil_mp.NebulaNote.yml)"
}

if ! command -v cmake >/dev/null; then
    echo "CMake is missing." >&2
    deps_hint >&2
    exit 1
fi

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

# Videos play inside pages through a plugin built on Qt WebEngine (loaded only
# when a video plays). Its libraries usually come with the desktop; when the
# headers (qt6-webengine-dev) aren't installed, fetch them into a cache (no
# root needed) and build against those.
extra=()
multiarch="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)"
syslib="/usr/lib/$multiarch"
if [[ ! -e "$syslib/cmake/Qt6WebEngineWidgets/Qt6WebEngineWidgetsConfig.cmake" ]] \
   && ls "$syslib"/libQt6WebEngineWidgets.so.6 >/dev/null 2>&1 && command -v apt-get >/dev/null && command -v dpkg-deb >/dev/null; then
    cache="${XDG_CACHE_HOME:-$HOME/.cache}/nebula-note/qt6-webengine-dev"
    want="$(dpkg-query -W -f='${Version}' libqt6webenginewidgets6 2>/dev/null || true)"
    # Fetch again when the system's WebEngine has been updated since.
    if [[ ! -e "$cache/usr/lib/$multiarch/cmake/Qt6WebEngineWidgets/Qt6WebEngineWidgetsConfig.cmake" || "$(cat "$cache/.version" 2>/dev/null)" != "$want" ]]; then
        echo "==> Fetching the Qt WebEngine headers (for videos inside pages)"
        tmp="$(mktemp -d)"
        if (cd "$tmp" && apt-get download qt6-webengine-dev qt6-webengine-dev-tools qt6-webchannel-dev qt6-positioning-dev >/dev/null 2>&1); then
            rm -rf "$cache" && mkdir -p "$cache"
            for deb in "$tmp"/*.deb; do dpkg-deb -x "$deb" "$cache"; done
            # The dev links point at the system's own libraries.
            for lib in "$syslib"/libQt6WebEngine*.so.6* "$syslib"/libQt6WebChannel*.so.6* "$syslib"/libQt6Positioning*.so.6*; do
                [[ -e "$lib" ]] && ln -sf "$lib" "$cache/usr/lib/$multiarch/$(basename "$lib")"
            done
            echo "$want" > "$cache/.version"
        else
            echo "    (couldn't download them; videos will open in the browser instead)"
        fi
        rm -rf "$tmp"
    fi
    if [[ -e "$cache/usr/lib/$multiarch/cmake/Qt6WebEngineWidgets/Qt6WebEngineWidgetsConfig.cmake" ]]; then
        extra=(-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH="$cache/usr" -DCMAKE_PREFIX_PATH="$cache/usr")
    fi
fi

echo "==> Configuring"
if ! cmake -S . -B "$BUILD_DIR" "${generator[@]}" "${extra[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null; then
    echo "Configuring failed (is Qt 6 installed?)." >&2
    deps_hint >&2
    exit 1
fi
echo "==> Building"
cmake --build "$BUILD_DIR" --parallel --target nebula-note
echo "==> Installing to $PREFIX"
# Stripped: the symbol tables are only for debuggers.
cmake --install "$BUILD_DIR" --strip >/dev/null
BIN="$PREFIX/bin/nebula-note"

# The app used to be called Nebula: remove its old program, menu entry,
# icons and launchers (the workspace in ~/.local/share/nebula is kept).
rm -f "$PREFIX/bin/nebula" "$PREFIX/share/applications/$OLD_APP_ID.desktop" "$ROOT/Nebula.desktop"
rm -f "$PREFIX/share/icons/hicolor/scalable/apps/$OLD_APP_ID.svg"
for size in 16 22 24 32 48 64 128 256 512; do
    rm -f "$PREFIX/share/icons/hicolor/${size}x${size}/apps/$OLD_APP_ID.png"
done
old_desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
rm -f "$old_desktop_dir/Nebula.desktop"

# Raster icons for the places that don't render SVG, drawn by the app itself.
ICON_256=""
for size in 16 22 24 32 48 64 128 256 512; do
    dir="$PREFIX/share/icons/hicolor/${size}x${size}/apps"
    mkdir -p "$dir"
    QT_QPA_PLATFORM=offscreen "$BIN" --render-icon "$dir/$APP_ID.png" --size "${size}x${size}" >/dev/null 2>&1 || true
    [[ $size == 256 ]] && ICON_256="$dir/$APP_ID.png"
done

# App menu entry.
mkdir -p "$PREFIX/share/applications"
menu_entry="$PREFIX/share/applications/$APP_ID.desktop"
sed -e "s|@EXEC@|$BIN|" -e "s|@ICON@|$APP_ID|" packaging/nebula.desktop.in > "$menu_entry"
chmod 644 "$menu_entry"
update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
kbuildsycoca6 >/dev/null 2>&1 || true

# The single file you double-click. Absolute paths, so it works from anywhere.
make_launcher() {
    local target="$1"
    sed -e "s|@EXEC@|$BIN|" -e "s|@ICON@|${ICON_256:-$APP_ID}|" packaging/nebula.desktop.in > "$target"
    chmod 755 "$target"
    # Plasma trusts executable launchers; GNOME-style desktops also want this flag.
    gio set "$target" metadata::trusted true >/dev/null 2>&1 || true
}
make_launcher "$ROOT/Nebula Note.desktop"
echo "==> Launcher: $ROOT/Nebula Note.desktop  (double-click it to start Nebula Note)"

if [[ $DESKTOP_ICON == 1 ]]; then
    desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
    mkdir -p "$desktop_dir"
    make_launcher "$desktop_dir/Nebula Note.desktop"
    echo "==> Desktop launcher: $desktop_dir/Nebula Note.desktop"
fi

echo
echo "Nebula Note is installed. Start it by double-clicking \"Nebula Note.desktop\", from the app menu, or with:"
echo "  $BIN"
