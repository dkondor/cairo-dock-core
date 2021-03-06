#!/bin/sh
#
# Create links to well-known applications icons.
# This allows to quickly populate a theme with custom icons.
#
# Copyright : (C) 2009 by Fabounet
# E-mail    : fabounet@glx-dock.org
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# http://www.gnu.org/licenses/licenses.html#GPL


if test "x$1" = "x"; then
	t="$HOME/.config/cairo-dock/current_theme/icons"
else
	t="$1"
fi

cd "$t"
find . -type l -delete

for suff in "svg" "png"
do
	echo "  creation of the $suff links ..."
	
	if test -e web-browser.$suff; then
		echo "    towards web-browser.$suff"
		ln -s web-browser.$suff firefox.$suff
		ln -s web-browser.$suff chromium-browser.$suff
		ln -s web-browser.$suff opera.$suff
		ln -s web-browser.$suff epiphany.$suff
		ln -s web-browser.$suff midori.$suff
		ln -s web-browser.$suff rekonq.$suff
	fi
	
	if test -e file-browser.$suff; then
		echo "    towards file-browser.$suff"
		ln -s file-browser.$suff nautilus.$suff
		ln -s file-browser.$suff system-file-manager.$suff
		ln -s file-browser.$suff konqueror.$suff
		ln -s file-browser.$suff dolphin.$suff
		ln -s file-browser.$suff thunar.$suff
		ln -s file-browser.$suff pcmanfm.$suff
	fi
	
	if test -e mail-reader.$suff; then
		echo "    towards mail-reader.$suff"
		ln -s mail-reader.$suff mozilla-thunderbird.$suff
		ln -s mail-reader.$suff thunderbird.$suff
		ln -s mail-reader.$suff kmail.$suff
		ln -s mail-reader.$suff evolution.$suff
		ln -s mail-reader.$suff sylpheed.$suff
	fi
	if test -e image-reader.$suff; then
		echo "    towards image-reader.$suff"
		ln -s image-reader.$suff eog.$suff
		ln -s image-reader.$suff gqview.$suff
		ln -s image-reader.$suff gwenview.$suff
		ln -s image-reader.$suff f-spot.$suff
		ln -s image-reader.$suff shotwell.$suff
		ln -s image-reader.$suff gthumb.$suff
	fi
	if test -e audio-player.$suff; then
		echo "    towards audio-player.$suff"
		ln -s audio-player.$suff xmms.$suff
		ln -s audio-player.$suff bmp.$suff
		ln -s audio-player.$suff beep-media-player.$suff
		ln -s audio-player.$suff rhythmbox.$suff
		ln -s audio-player.$suff amarok.$suff
		ln -s audio-player.$suff banshee.$suff
		ln -s audio-player.$suff audacious.$suff
		ln -s audio-player.$suff clementine.$suff
		ln -s audio-player.$suff exaile.$suff
		ln -s audio-player.$suff gmusicbrowser.$suff
		ln -s audio-player.$suff guayadeque.$suff
		ln -s audio-player.$suff qmmp.$suff
		ln -s audio-player.$suff quotlibet.$suff
		ln -s audio-player.$suff songbird.$suff
	fi
	if test -e video-player.$suff; then
		echo "    towards video-player.$suff"
		ln -s video-player.$suff totem.$suff
		ln -s video-player.$suff mplayer.$suff
		ln -s video-player.$suff vlc.$suff
		ln -s video-player.$suff xine.$suff
		ln -s video-player.$suff kaffeine.$suff
		ln -s video-player.$suff gnome-player.$suff
	fi
	
	if test -e writer.$suff; then
		echo "    towards writer.$suff"
		ln -s writer.$suff gedit.$suff
		ln -s writer.$suff geany.$suff
		ln -s writer.$suff kate.$suff
		ln -s writer.$suff ooo-writer.$suff
		ln -s writer.$suff abiword.$suff
		ln -s writer.$suff emacs.$suff
		ln -s writer.$suff libreoffice-writer.$suff
	fi
	if test -e bittorrent.$suff; then
		echo "    towards bittorrent.$suff"
		ln -s bittorrent.$suff transmission.$suff
		ln -s bittorrent.$suff deluge.$suff
		ln -s bittorrent.$suff bittornado.$suff
		ln -s bittorrent.$suff gnome-btdownload.$suff
		ln -s bittorrent.$suff ktorrent.$suff
	fi
	if test -e download.$suff; then
		echo "    towards download.$suff"
		ln -s download.$suff amule.$suff
		ln -s download.$suff emule.$suff
		ln -s download.$suff jdownloader.$suff
	fi
	if test -e cd-burner.$suff; then
		echo "    towards cd-burner.$suff"
		ln -s cd-burner.$suff nautilus-cd-burner.$suff
		ln -s cd-burner.$suff graveman.$suff
		ln -s cd-burner.$suff gnome-baker.$suff
		ln -s cd-burner.$suff k3b.$suff
		ln -s cd-burner.$suff brasero.$suff
	fi
	if test -e image.$suff; then
		echo "    towards image.$suff"
		ln -s image.$suff gimp.$suff
		ln -s image.$suff inkscape.$suff
		ln -s image.$suff krita.$suff
		ln -s image.$suff mtpaint.$suff
	fi
	
	if test -e messenger.$suff; then
		echo "    towards messenger.$suff"
		ln -s messenger.$suff gaim.$suff
		ln -s messenger.$suff pidgin.$suff
		ln -s messenger.$suff empathy.$suff
		ln -s messenger.$suff kopete.$suff
		ln -s messenger.$suff amsn.$suff
		ln -s messenger.$suff emesene.$suff
	fi
	if test -e irc.$suff; then
		echo "    towards irc.$suff"
		ln -s irc.$suff xchat.$suff
		ln -s irc.$suff konversation.$suff
		ln -s irc.$suff kvirc.$suff
	fi
	
	if test -e terminal.$suff; then
		echo "    towards terminal.$suff"
		ln -s terminal.$suff gnome-terminal.$suff
		ln -s terminal.$suff utilities-terminal.$suff
		ln -s terminal.$suff konsole.$suff
		ln -s terminal.$suff xfce4-terminal.$suff
		ln -s terminal.$suff lxterminal.$suff
	fi
	if test -e packages.$suff; then
		echo "    towards packages.$suff"
		ln -s packages.$suff synaptic.$suff
		ln -s packages.$suff softwarecenter.$suff
		ln -s packages.$suff yast.$suff
		ln -s packages.$suff adept.$suff
		ln -s packages.$suff pacman-g2.$suff
		ln -s packages.$suff yum.$suff
		ln -s packages.$suff ubuntu-software-center.$suff
	fi
	if test -e system-monitor.$suff; then
		echo "    towards system-monitor.$suff"
		ln -s system-monitor.$suff ksysguard.$suff
		ln -s system-monitor.$suff utilities-system-monitor.$suff
	fi
	if test -e calculator.$suff; then
		echo "    towards calculator.$suff"
		ln -s calculator.$suff accessories-calculator.$suff
		ln -s calculator.$suff gcalctool.$suff
		ln -s calculator.$suff kcalc.$suff
		ln -s calculator.$suff gnome-calculator.$suff
		ln -s calculator.$suff crunch.$suff
		ln -s calculator.$suff galculator.$suff
	fi
done;

echo "all links have been generated."
