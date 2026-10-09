#!/bin/sh
# Removes LevelScript installed by the .pkg:
#     sudo /usr/local/levelscript/uninstall.sh
set -e
if [ "$(id -u)" -ne 0 ]; then
    echo "Run with sudo: sudo $0" >&2
    exit 1
fi
rm -rf /usr/local/levelscript
rm -f /etc/paths.d/levelscript
pkgutil --forget @PKG_ID@ >/dev/null 2>&1 || true
echo "LevelScript removed. Open a new terminal to refresh PATH."
