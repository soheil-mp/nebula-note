#!/usr/bin/env bash
# Removes Nebula Note from ~/.local and deletes the launchers.
# Your notes (~/.local/share/nebula) and settings are kept unless --purge.
#
# Usage: ./uninstall.sh [--purge]
set -euo pipefail

cd "$(dirname "$(readlink -f "$0")")"

APP_ID="io.github.soheil_mp.NebulaNote"
PREFIX="${PREFIX:-$HOME/.local}"
PURGE=0
[[ "${1:-}" == "--purge" ]] && PURGE=1

rm -f "$PREFIX/bin/nebula-note" "$PREFIX/share/applications/$APP_ID.desktop"
rm -f "$PREFIX/share/icons/hicolor/scalable/apps/$APP_ID.svg"
for size in 16 22 24 32 48 64 128 256 512; do
    rm -f "$PREFIX/share/icons/hicolor/${size}x${size}/apps/$APP_ID.png"
done
rm -f "./Nebula Note.desktop"
desktop_dir="$(xdg-user-dir DESKTOP 2>/dev/null || echo "$HOME/Desktop")"
rm -f "$desktop_dir/Nebula Note.desktop"

update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
kbuildsycoca6 >/dev/null 2>&1 || true

if [[ $PURGE == 1 ]]; then
    rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/nebula" "${XDG_CONFIG_HOME:-$HOME/.config}/nebula"
    echo "Nebula Note and all of its data were removed."
else
    echo "Nebula Note was removed. Your workspace is still in ${XDG_DATA_HOME:-$HOME/.local/share}/nebula (use --purge to delete it)."
fi
