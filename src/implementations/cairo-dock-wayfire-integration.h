/*
* This file is a part of the Cairo-Dock project
*
* Copyright : (C) see the 'copyright' file.
* E-mail    : see the 'copyright' file.
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 3
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#ifndef __CAIRO_DOCK_WAYFIRE_INTEGRATION__
#define  __CAIRO_DOCK_WAYFIRE_INTEGRATION__

#ifdef HAVE_WAYLAND_PROTOCOLS

#include <gtk/gtk.h>
#include <wayland-client.h>
#include "cairo-dock-struct.h"
G_BEGIN_DECLS

/**
*@file cairo-dock-wayfire-integration.h This class implements the integration of Wayfire inside Cairo-Dock.
*/

/** Init the Wayfire integration backend. Will try to connect Wayfire's IPC
* socket and query the available functions. If successful, it will also
* start the foreign-toplevel-manager (so it should not be started separately).
*
*@param registry the Wayland registry to use for the wlr-foreign-toplevel-management interface
*@return Whether we could successfully connect to both the IPC interface and the toplevel-manager interface.
*/
gboolean cd_init_wayfire_backend (struct wl_registry *registry);

G_END_DECLS
#endif
#endif

