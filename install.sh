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

if ! command -v cmake >/dev/null; then
    cat >&2 <<'MSG'
CMake is missing. Install the build dependencies first:
  sudo apt install cmake ninja-build g++ qt6-base-dev qt6-svg-dev zlib1g-dev libkf6windowsystem-dev
MSG
    exit 1
fi

generator=()
command -v ninja >/dev/null && generator=(-G Ninja)

echo "==> Configuring"
cmake -S . -B "$BUILD_DIR" "${generator[@]}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" >/dev/null
echo "==> Building"
cmake --build "$BUILD_DIR" --parallel --target nebula-note
echo "==> Installing to $PREFIX"
cmake --install "$BUILD_DIR" >/dev/null
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
