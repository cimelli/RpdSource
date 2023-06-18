
Debian
====================
This directory contains files used to package rpdchaind/rpdchain-qt
for Debian-based Linux systems. If you compile rpdchaind/rpdchain-qt yourself, there are some useful files here.

## rpdchain: URI support ##


rpdchain-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install rpdchain-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your rpdchain-qt binary to `/usr/bin`
and the `../../share/pixmaps/rpdchain128.png` to `/usr/share/pixmaps`

rpdchain-qt.protocol (KDE)

