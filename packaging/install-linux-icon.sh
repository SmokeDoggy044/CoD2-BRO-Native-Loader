#!/bin/sh
# Adds the bundled icon and a desktop entry to the current user's application
# menu. Run once from an unpacked Linux release:
#
#   ./install-linux-icon.sh
#
# It uses XDG_DATA_HOME (or ~/.local/share) and writes an absolute Exec path so
# the launcher can be started from anywhere, including folders with spaces.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
data_home=${XDG_DATA_HOME:-"$HOME/.local/share"}
applications="$data_home/applications"
icons="$data_home/icons/hicolor/256x256/apps"

if [ ! -f "$here/cod_bro.png" ] || [ ! -f "$here/cod_bro_launcher" ]; then
    echo "Run this script from an unpacked Linux release containing cod_bro.png and cod_bro_launcher." >&2
    exit 1
fi

mkdir -p "$applications" "$icons"
cp -f "$here/cod_bro.png" "$icons/cod_bro.png"

{
    printf '[Desktop Entry]\n'
    printf 'Type=Application\n'
    printf 'Version=1.0\n'
    printf 'Name=Call of Duty 2: Big Red One\n'
    printf 'GenericName=Game launcher\n'
    printf 'Comment=Native loader for the original Xbox version\n'
    printf 'Exec="%s"\n' "$here/cod_bro_launcher"
    printf 'Path="%s"\n' "$here"
    printf 'Icon=cod_bro\n'
    printf 'Terminal=false\n'
    printf 'Categories=Game;ActionGame;\n'
} > "$applications/cod_bro.desktop"
chmod 644 "$applications/cod_bro.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$applications" || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "$data_home/icons/hicolor" || true
fi

echo "Installed the Call of Duty 2: Big Red One icon and desktop entry in $data_home"
