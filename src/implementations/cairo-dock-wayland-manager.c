/**
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

#include "gldi-config.h"
#ifdef HAVE_WAYLAND

#include <poll.h>

#include <gdk/gdk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "cairo-dock-struct.h"
#include "cairo-dock-utils.h"
#include "cairo-dock-log.h"
#include "cairo-dock-surface-factory.h"
#include "cairo-dock-applications-manager.h"  // myTaskbarParam.iMinimizedWindowRenderType
#include "cairo-dock-desktop-manager.h"
#include "cairo-dock-class-manager.h"  // gldi_class_startup_notify_end
#include "cairo-dock-windows-manager.h"
#include "cairo-dock-container.h"  // GldiContainerManagerBackend
#include "cairo-dock-foreign-toplevel.h"
#include "cairo-dock-egl.h"
#define _MANAGER_DEF_
#include "cairo-dock-wayland-manager.h"

#include "gldi-config.h"
#ifdef HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell.h>
static gboolean s_bHave_Layer_Shell = FALSE;

gboolean g_bDisableLayerShell = FALSE;

#endif


// public (manager, config, data)
GldiManager myWaylandMgr;
GldiObjectManager myWaylandObjectMgr;

// dependencies
extern GldiContainer *g_pPrimaryContainer;

// private
static struct wl_display *s_pDisplay = NULL;
static struct wl_compositor* s_pCompositor = NULL;

// manage screens -- instead of wl_output, we use GdkDisplay and GdkMonitors
// list of monitors -- corresponds to pScreens in g_desktopGeometry from
// cairo-dock-desktop-manager.c, and kept in sync with that
GdkMonitor **s_pMonitors = NULL;
// we keep track of the primary monitor
GdkMonitor *s_pPrimaryMonitor = NULL;

// signals
typedef enum {
	NB_NOTIFICATIONS_WAYLAND_MANAGER = NB_NOTIFICATIONS_WINDOWS
	} CairoWaylandManagerNotifications;

// data
typedef struct _GldiWaylandWindowActor GldiWaylandWindowActor;
struct _GldiWaylandWindowActor {
	GldiWindowActor actor;
	// Wayland-specific
	struct wl_shell_surface *shell_surface;
};

// refresh s_desktopGeometry.xscreen
static void _calculate_xscreen ()
{
	int i;
	g_desktopGeometry.Xscreen.x = 0;
	g_desktopGeometry.Xscreen.y = 0;
	g_desktopGeometry.Xscreen.width = 0;
	g_desktopGeometry.Xscreen.height = 0;
	
	if (g_desktopGeometry.iNbScreens >= 0)
		for (i = 0; i < g_desktopGeometry.iNbScreens; i++)
		{
			int tmpwidth = g_desktopGeometry.pScreens[i].x + g_desktopGeometry.pScreens[i].width;
			int tmpheight = g_desktopGeometry.pScreens[i].y + g_desktopGeometry.pScreens[i].height;
			if (tmpwidth > g_desktopGeometry.Xscreen.width)
				g_desktopGeometry.Xscreen.width = tmpwidth;
			if (tmpheight > g_desktopGeometry.Xscreen.height)
				g_desktopGeometry.Xscreen.height = tmpheight;
		}
}

// handle changes in screen / monitor configuration -- note: user_data is a boolean that determines if we should emit a signal
static void _monitor_added (GdkDisplay *display, GdkMonitor* monitor, gpointer user_data)
{
	int iNumScreen = 0;
	if (!(g_desktopGeometry.pScreens && s_pMonitors && g_desktopGeometry.iNbScreens))
	{
		if (g_desktopGeometry.iNbScreens || g_desktopGeometry.pScreens || s_pMonitors)
			cd_warning ("_monitor_added() inconsistent state of screens / monitors\n");
		g_desktopGeometry.iNbScreens = 1;
		g_free (s_pMonitors);
		g_free (g_desktopGeometry.pScreens);
		s_pMonitors = g_new0 (GdkMonitor*, g_desktopGeometry.iNbScreens);
		g_desktopGeometry.pScreens = g_new0 (GtkAllocation, g_desktopGeometry.iNbScreens);
	}
	else
	{
		iNumScreen = g_desktopGeometry.iNbScreens++;
		s_pMonitors = g_renew (GdkMonitor*, s_pMonitors, g_desktopGeometry.iNbScreens);
		g_desktopGeometry.pScreens = g_renew (GtkAllocation, g_desktopGeometry.pScreens, g_desktopGeometry.iNbScreens);
	}
	s_pMonitors[iNumScreen] = monitor;
	// note: GtkAllocation is the same as GdkRectangle
	gdk_monitor_get_geometry (monitor, g_desktopGeometry.pScreens + iNumScreen);
	if (monitor == gdk_display_get_primary_monitor (display))
		s_pPrimaryMonitor = monitor;
	_calculate_xscreen ();
	if (user_data) gldi_object_notify (&myDesktopMgr, NOTIFICATION_DESKTOP_GEOMETRY_CHANGED, FALSE);
}

// handle if a monitor was removed
static void _monitor_removed (GdkDisplay* display, GdkMonitor* monitor, G_GNUC_UNUSED gpointer user_data)
{
	int iNumScreen = -1;
	if (!(g_desktopGeometry.pScreens && s_pMonitors && g_desktopGeometry.iNbScreens > 0))
		cd_warning ("_monitor_removed() inconsistent state of screens / monitors\n");
	else
	{
		int i;
		for (i = 0; i < g_desktopGeometry.iNbScreens; i++)
			if (s_pMonitors[i] == monitor) 
			{
				iNumScreen = i;
				break;
			}
		if (iNumScreen == -1)
		{
			cd_warning ("_monitor_removed() inconsistent state of screens / monitors\n");
			return;
		}
		for (; i +1 < g_desktopGeometry.iNbScreens; i++)
		{
			s_pMonitors[i] = s_pMonitors[i+1];
			g_desktopGeometry.pScreens[i] = g_desktopGeometry.pScreens[i+1];
		}
		s_pPrimaryMonitor = gdk_display_get_primary_monitor (display);
		if (!s_pPrimaryMonitor) s_pPrimaryMonitor = s_pMonitors[0];
		g_desktopGeometry.iNbScreens--;
		s_pMonitors = g_renew (GdkMonitor*, s_pMonitors, g_desktopGeometry.iNbScreens);
		g_desktopGeometry.pScreens = g_renew (GtkAllocation, g_desktopGeometry.pScreens, g_desktopGeometry.iNbScreens);
		_calculate_xscreen ();
		gldi_object_notify (&myDesktopMgr, NOTIFICATION_DESKTOP_GEOMETRY_CHANGED, FALSE);
	}
}

// refresh the size of all monitors -- note: user_data is a boolean that determines if we should emit a signal
static void _refresh_monitors_size(G_GNUC_UNUSED GdkScreen *screen, gpointer user_data)
{
	int i;
	for (i = 0; i < g_desktopGeometry.iNbScreens; i++)
		gdk_monitor_get_geometry (s_pMonitors[i], g_desktopGeometry.pScreens + i);
	_calculate_xscreen ();
	if (user_data) gldi_object_notify (&myDesktopMgr, NOTIFICATION_DESKTOP_GEOMETRY_CHANGED, FALSE);
}

// refresh all monitors (if needed) -- note: user_data is a boolean that determines if we should emit a signal
static void _refresh_monitors (GdkScreen *screen, gpointer user_data)
{
	GdkDisplay *display = gdk_screen_get_display (screen);
	int iNumScreen = gdk_display_get_n_monitors (display);
	if (iNumScreen != g_desktopGeometry.iNbScreens)
	{
		// we don't try to guess what has changed, just refresh all monitors
		g_free (s_pMonitors);
		g_free (g_desktopGeometry.pScreens);
		g_desktopGeometry.iNbScreens = iNumScreen;
		s_pMonitors = g_renew (GdkMonitor*, s_pMonitors, g_desktopGeometry.iNbScreens);
		g_desktopGeometry.pScreens = g_renew (GtkAllocation, g_desktopGeometry.pScreens, g_desktopGeometry.iNbScreens);
		int i;
		for (i = 0; i < iNumScreen; i++)
		{
			s_pMonitors[i] = gdk_display_get_monitor (display, i);
			gdk_monitor_get_geometry (s_pMonitors[i], g_desktopGeometry.pScreens + i);
		}
		s_pPrimaryMonitor = gdk_display_get_primary_monitor (display);
		if (!s_pPrimaryMonitor) s_pPrimaryMonitor = s_pMonitors[0];
		_calculate_xscreen ();
		if (user_data) gldi_object_notify (&myDesktopMgr, NOTIFICATION_DESKTOP_GEOMETRY_CHANGED, FALSE);
	}
	else _refresh_monitors_size (screen, user_data);
}


static gboolean s_bInitializing = TRUE;  // each time a callback is called on startup, it will set this to TRUE, and we'll make a roundtrip to the server until no callback is called.

static void _registry_global_cb (G_GNUC_UNUSED void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version)
{
	cd_debug ("got a new global object, instance of %s, id=%d", interface, id);
	if (!strcmp (interface, "wl_shell"))
	{
		// this is the global that should give us info and signals about the desktop, but currently it's pretty useless ...
		
	}
	else if (!strcmp (interface, wl_compositor_interface.name))
	{
		s_pCompositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
	}
	else if (gldi_zwlr_foreign_toplevel_manager_try_bind (registry, id, interface, version))
	{
		cd_debug("Found foreign-toplevel-manager");
	}
#ifdef HAVE_GTK_LAYER_SHELL
	else if (!strcmp (interface, "zwlr_layer_shell_v1"))
	{
		if (!g_bDisableLayerShell)
			s_bHave_Layer_Shell = TRUE;
	}
#endif
	s_bInitializing = TRUE;
}

static void _registry_global_remove_cb (G_GNUC_UNUSED void *data, G_GNUC_UNUSED struct wl_registry *registry, uint32_t id)
{
	cd_debug ("got a global object has disappeared: id=%d", id);
	/// TODO: find it and destroy it...
	
	/// TODO: and if it was a wl_output for instance, update the desktop geometry...
	
}

static const struct wl_registry_listener registry_listener = {
	_registry_global_cb,
	_registry_global_remove_cb
};

// Set the input shape directly for the container's wl_surface.
// This is necessary for some reason on Wayland + EGL.
// Note: gtk_widget_input_shape_combine_region() is already called
// in cairo-dock-container.c, we only need to deal with the
// Wayland-specific stuff here
static void _set_input_shape(GldiContainer *pContainer, cairo_region_t *pShape)
{
	// note: we only do this if this container is using EGL
#ifdef HAVE_EGL
	if (!pContainer->eglwindow) return;
	if (!s_pCompositor)
	{
		cd_warning ("wayland-manager: No valid Wayland compositor interface available!\n");
		return;
	}
	struct wl_surface *wls = gdk_wayland_window_get_wl_surface (
		gldi_container_get_gdk_window (pContainer));
	if (!wls)
	{
		cd_warning ("wayland-manager: cannot get wl_surface for container!\n");
		return;	
	}
	
	struct wl_region* r = NULL;
	if (pShape)
	{
		r = wl_compositor_create_region(s_pCompositor);
		int n = cairo_region_num_rectangles(pShape);
		int i;
		for (i = 0; i < n; i++)
		{
			cairo_rectangle_int_t rect;
			cairo_region_get_rectangle (pShape, i, &rect);
			wl_region_add (r, rect.x, rect.y, rect.width, rect.height);
		}
	}
	wl_surface_set_input_region (wls, r);
	wl_surface_commit (wls);
	wl_display_roundtrip (s_pDisplay);
	if (r) wl_region_destroy (r);
#endif
}


#ifdef HAVE_GTK_LAYER_SHELL
/// reserve space for the dock; note: most parameters are ignored, only a size is calculated based on coordinates
static void _layer_shell_reserve_space (GldiContainer *pContainer, int left, int right, int top, int bottom,
		G_GNUC_UNUSED int left_start_y, G_GNUC_UNUSED int left_end_y, G_GNUC_UNUSED int right_start_y, G_GNUC_UNUSED int right_end_y,
		G_GNUC_UNUSED int top_start_x, G_GNUC_UNUSED int top_end_x, G_GNUC_UNUSED int bottom_start_x, G_GNUC_UNUSED int bottom_end_x)
{
	GtkWindow* window = GTK_WINDOW (pContainer->pWidget);
	gint r = (pContainer->bIsHorizontal) ? (top - bottom) : (right - left);
	if (r < 0) {
		r *= -1;
	}
	gtk_layer_set_exclusive_zone (window, r);
}

static void _set_layer_shell_anchor (GldiContainer *pContainer, CairoDockPositionType iScreenBorder)
{
	GtkWindow* window = GTK_WINDOW (pContainer->pWidget);
	// Reset old anchors
	gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
	gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, FALSE);
	gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
	gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
	// Set new anchor
	switch (iScreenBorder)
	{
		case CAIRO_DOCK_BOTTOM :
			gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
		break;
		case CAIRO_DOCK_TOP :
			gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
		break;
		case CAIRO_DOCK_RIGHT :
			gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
		break;
		case CAIRO_DOCK_LEFT :
			gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
		break;
		case CAIRO_DOCK_INSIDE_SCREEN :
		case CAIRO_DOCK_NB_POSITIONS :
		break;
	}
}

void _set_layer_shell_layer (GldiContainer *pContainer, GldiContainerLayer iLayer)
{
	GtkWindow* window = GTK_WINDOW (pContainer->pWidget);
	gtk_layer_set_layer (window, iLayer == CAIRO_DOCK_LAYER_TOP ? 	
		GTK_LAYER_SHELL_LAYER_TOP : GTK_LAYER_SHELL_LAYER_BOTTOM);
}

void _layer_shell_init_for_window (GldiContainer *pContainer)
{
	GtkWindow* window = GTK_WINDOW (pContainer->pWidget);
	if (gtk_window_get_transient_for (window))
	{
		// subdock or other surface with a parent
		// we need to call gdk_move_to_rect(), but specifically
		// (1) before the window is mapped, but (2) during it is realized
		// A dummy call to gdk_window_move_to_rect() causes the gtk-layer-shell
		// internals related to this window to be initialized properly (note:
		// this is important if the parent is a "proper" layer-shell window).
		// This _has_ to happen before the GtkWindow is mapped first, but also
		// has to happen after some initial setup. Doing this as a response to
		// a "realize" signal seems to be a good way, but there should be some
		// less hacky solution for this as well. Maybe I'm missing something here?
		// g_signal_connect (window, "realize", G_CALLBACK (sublayer_realize_cb), NULL);
		
		// gldi_container_move_to_rect() will ensure this
		GdkRectangle rect = {0, 0, 1, 1};
		gldi_container_move_to_rect (pContainer, &rect, GDK_GRAVITY_SOUTH,
			GDK_GRAVITY_NORTH_WEST, GDK_ANCHOR_SLIDE, 0, 0);
	}
	else
	{
		gtk_layer_init_for_window (window);
		// Note: to enable receiving any keyboard events, we need both the compositor
		// and gtk-layer-shell to support version >= 4 of the layer-shell protocol.
		// Here we test if we are compiling against a version of gtk-layer-shell that
		// has this functionality. Setting keyboard interactivity may still fail at
		// runtime if the compositor does not support this. In this case, the dock
		// will not receive keyboard events at all.
		// TODO: make this optional, so that cairo-dock can be compiled targeting an
		// older version of gtk-layer-shell (even if a newer version is installed;
		// since versions are ABI compatible, this is possible)
#ifdef GTK_LAYER_SHELL_KEYBOARD_MODE_ENTRY_NUMBER
		gtk_layer_set_keyboard_interactivity (window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);
#endif
		gtk_layer_set_namespace (window, "cairo-dock");
	}
}

void _layer_shell_move_to_monitor (GldiContainer *pContainer, int iNumScreen)
{
	GtkWindow *window = GTK_WINDOW (pContainer->pWidget);
	// this only works for parent windows, not popups
	if (gtk_window_get_transient_for (window)) return;
	GdkMonitor *monitor = (iNumScreen >= 0 && iNumScreen < g_desktopGeometry.iNbScreens) ?
		s_pMonitors[iNumScreen] : s_pPrimaryMonitor;
	if (monitor) gtk_layer_set_monitor (window, monitor);
}
#endif

static gboolean _is_wayland() { return TRUE; }

static void init (void)
{
	//\__________________ listen for Wayland events
	
	g_desktopGeometry.iNbDesktops = g_desktopGeometry.iNbViewportX = g_desktopGeometry.iNbViewportY = 1;
	
	
	struct wl_registry *registry = wl_display_get_registry (s_pDisplay);
	wl_registry_add_listener (registry, &registry_listener, NULL);
	do
	{
		s_bInitializing = FALSE;
		wl_display_roundtrip (s_pDisplay);
	}
	while (s_bInitializing);
	
	GldiContainerManagerBackend cmb;
	memset (&cmb, 0, sizeof (GldiContainerManagerBackend));	
#ifdef HAVE_GTK_LAYER_SHELL
	if (s_bHave_Layer_Shell)
	{
		cmb.reserve_space = _layer_shell_reserve_space;
		cmb.set_anchor = _set_layer_shell_anchor;
		cmb.set_layer = _set_layer_shell_layer;
		cmb.init_layer = _layer_shell_init_for_window;
		cmb.set_monitor = _layer_shell_move_to_monitor;
	}
#endif
	cmb.set_input_shape = _set_input_shape;
	cmb.is_wayland = _is_wayland;
	gldi_container_manager_register_backend (&cmb);
	gldi_register_egl_backend ();
}

void gldi_register_wayland_manager (void)
{
	#ifdef GDK_WINDOWING_WAYLAND  // if GTK doesn't support Wayland, there is no point in trying
	GdkDisplay *dsp = gdk_display_get_default ();  // let's GDK do the guess
	if (GDK_IS_WAYLAND_DISPLAY (dsp))
	{
		s_pDisplay = gdk_wayland_display_get_wl_display (dsp);
	}
	if (!s_pDisplay)  // if not an Wayland session
	#endif
	{
		cd_message ("Not an Wayland session");
		return;
	}
	
	// Manager
	memset (&myWaylandMgr, 0, sizeof (GldiManager));
	myWaylandMgr.cModuleName   = "X";
	myWaylandMgr.init          = init;
	myWaylandMgr.load          = NULL;
	myWaylandMgr.unload        = NULL;
	myWaylandMgr.reload        = (GldiManagerReloadFunc)NULL;
	myWaylandMgr.get_config    = (GldiManagerGetConfigFunc)NULL;
	myWaylandMgr.reset_config  = (GldiManagerResetConfigFunc)NULL;
	// Config
	myWaylandMgr.pConfig = (GldiManagerConfigPtr)NULL;
	myWaylandMgr.iSizeOfConfig = 0;
	// data
	myWaylandMgr.iSizeOfData = 0;
	myWaylandMgr.pData = (GldiManagerDataPtr)NULL;
	// register
	gldi_object_init (GLDI_OBJECT(&myWaylandMgr), &myManagerObjectMgr, NULL);
	
	// Object Manager
	memset (&myWaylandObjectMgr, 0, sizeof (GldiObjectManager));
	myWaylandObjectMgr.cName   = "Wayland";
	myWaylandObjectMgr.iObjectSize    = sizeof (GldiWaylandWindowActor);
	// interface
	///myWaylandObjectMgr.init_object    = init_object;
	///myWaylandObjectMgr.reset_object   = reset_object;
	// signals
	gldi_object_install_notifications (&myWaylandObjectMgr, NB_NOTIFICATIONS_WAYLAND_MANAGER);
	// parent object
	gldi_object_set_manager (GLDI_OBJECT (&myWaylandObjectMgr), &myWindowObjectMgr);
	
	// get the properties of screens / monitors, set up signals
	g_desktopGeometry.iNbScreens = 0;
	g_desktopGeometry.pScreens = NULL;
	GdkScreen *screen = gdk_display_get_default_screen (dsp);
	// fill out the list of screens / monitors
	_refresh_monitors (screen, (gpointer)FALSE);
	// set up notifications for screens added / removed
	g_signal_connect (G_OBJECT (screen), "monitors-changed", G_CALLBACK (_refresh_monitors), (gpointer)TRUE);
	g_signal_connect (G_OBJECT (screen), "size-changed", G_CALLBACK (_refresh_monitors_size), (gpointer)TRUE);
	g_signal_connect (G_OBJECT (dsp), "monitor-added", G_CALLBACK (_monitor_added), (gpointer)TRUE);
	g_signal_connect (G_OBJECT (dsp), "monitor-removed", G_CALLBACK (_monitor_removed), (gpointer)TRUE);
}

#else
#include "cairo-dock-log.h"
void gldi_register_wayland_manager (void)
{
	cd_message ("Cairo-Dock was not built with Wayland support");
}
#endif
