# SPDX-FileCopyrightText: 2024 shadPS4 Emulator Project
# SPDX-FileCopyrightText: Copyright 2026 GR2port
# SPDX-License-Identifier: GPL-2.0-or-later

#!/bin/bash

if [[ -z $GITHUB_WORKSPACE ]]; then
	GITHUB_WORKSPACE="${PWD%/*}"
fi

# Prepare Tools for building the AppImage
wget --waitretry=3 --read-timeout=20 --timeout=15 --tries=5 -q https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
wget --waitretry=3 --read-timeout=20 --timeout=15 --tries=5 -q https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
wget --waitretry=3 --read-timeout=20 --timeout=15 --tries=5 -q https://github.com/linuxdeploy/linuxdeploy-plugin-checkrt/releases/download/continuous/linuxdeploy-plugin-checkrt-x86_64.sh

chmod a+x linuxdeploy-x86_64.AppImage
chmod a+x linuxdeploy-plugin-qt-x86_64.AppImage
chmod a+x linuxdeploy-plugin-checkrt-x86_64.sh

# The launcher is a Qt window: the Qt plugin copies Qt's libraries and plugins, the Wayland ones too.
export QMAKE=/usr/bin/qmake6
export EXTRA_QT_MODULES="waylandcompositor"
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so"

# Build AppImage
./linuxdeploy-x86_64.AppImage --appdir AppDir
./linuxdeploy-plugin-checkrt-x86_64.sh --appdir AppDir
# The checkrt plugin's AppRun splits a folder path that holds a space, uses an APPDIR that names
# another folder, and lists its desktop file with ls, whose output exported settings can change.
# The edits quote the path, unset the AppImage variables in that case, and use a glob instead.
sed -i \
	-e 's|-f \(${APPDIR:-$(dirname "$0")}\)|-f "\1"|' \
	-e '/^appdir=/i [ "$APPDIR" -ef "$(dirname "$0")" ] || unset APPDIR APPIMAGE ARGV0 OWD' \
	-e '/^desktopfile=$(ls -1 /c for desktopfile in "$appdir"/*.desktop; do break; done' \
	AppDir/AppRun
for line in 'readlink -f "${APPDIR:-$(dirname "$0")}"' \
	'[ "$APPDIR" -ef "$(dirname "$0")" ] || unset APPDIR APPIMAGE ARGV0 OWD' \
	'for desktopfile in "$appdir"/*.desktop; do break; done'; do
	if ! grep -qF -- "$line" AppDir/AppRun; then
		echo "AppDir/AppRun is missing or lacks $line: look at the checkrt plugin"
		exit 1
	fi
done
./linuxdeploy-x86_64.AppImage --appdir AppDir -d "$GITHUB_WORKSPACE"/src/dist/gr2port.desktop  -e "$GITHUB_WORKSPACE"/build/gr2port -i "$GITHUB_WORKSPACE"/src/dist/gr2port.png --plugin qt --output appimage
mv GR2port-x86_64.AppImage "Gravity Rush 2™.AppImage"
