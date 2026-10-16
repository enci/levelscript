#!/bin/sh
# Installs the desktop entry and icon for the current user so GNOME/KDE show
# the LevelScript icon (Wayland matches the window's app id to this file).
# Usage: packaging/linux/install_desktop.sh [path/to/levelscript-debugger-binary]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
exe=${1:-levelscript-debugger}
share=${XDG_DATA_HOME:-$HOME/.local/share}
mkdir -p "$share/applications" "$share/icons/hicolor/256x256/apps"
cp "$root/resources/icon/levelscript.png" "$share/icons/hicolor/256x256/apps/levelscript.png"
sed "s|^Exec=.*|Exec=$exe|" "$here/levelscript.desktop" > "$share/applications/levelscript.desktop"
command -v update-desktop-database >/dev/null && update-desktop-database "$share/applications" || true
command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -f -t "$share/icons/hicolor" || true
echo "installed to $share"
