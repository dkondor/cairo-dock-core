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


#include "cairo-dock-log.h"
#include "cairo-dock-dbus.h"
#include "cairo-dock-desktop-manager.h"
#include "cairo-dock-windows-manager.h"  // bIsHidden
#include "cairo-dock-icon-factory.h"  // pAppli
#include "cairo-dock-container.h"  // gldi_container_get_gdk_window
#include "cairo-dock-class-manager.h"
#include "cairo-dock-utils.h"  // cairo_dock_launch_command_sync
#include "cairo-dock-wayfire-integration.h"

static DBusGProxy *s_pProxy = NULL;

#define CD_WF_BUS "org.wayland.compositor"
#define CD_WF_OBJECT "/org/wayland/compositor"
#define CD_WF_INTERFACE "org.wayland.compositor"


static gboolean _present_windows (void)
{
	gboolean bSuccess = FALSE;
	if (s_pProxy != NULL)
	{
		GError *erreur = NULL;
		bSuccess = dbus_g_proxy_call (s_pProxy, "scale", &erreur,
			G_TYPE_BOOLEAN, TRUE,
			G_TYPE_STRING, "",
			G_TYPE_INVALID,
			G_TYPE_INVALID);
		if (erreur)
		{
			cd_warning ("wayfire scale error: %s", erreur->message);
			g_error_free (erreur);
			bSuccess = FALSE;
		}
	}
	return bSuccess;
}

static gboolean _present_class (const gchar *cClass)
{
	cd_debug ("%s (%s)", __func__, cClass);
	const GList *pIcons = cairo_dock_list_existing_appli_with_class (cClass);
	if (pIcons == NULL)
		return FALSE;
	
	gboolean bAllHidden = TRUE;
	Icon *pOneIcon;
	const GList *ic;
	for (ic = pIcons; ic != NULL; ic = ic->next)
	{
		pOneIcon = ic->data;
		bAllHidden &= pOneIcon->pAppli->bIsHidden;
	}
	if (bAllHidden)
		return FALSE;
	
	gboolean bSuccess = FALSE;
	if (s_pProxy != NULL)
	{
		GError *erreur = NULL;
		const gchar *cWmClass = cairo_dock_get_class_wm_class (cClass);
		if (!cWmClass) cWmClass = cClass;
		cd_message ("Wayfire scale: match '%s'", cWmClass);
		bSuccess = dbus_g_proxy_call (s_pProxy, "scale", &erreur,
			G_TYPE_BOOLEAN, TRUE,
			G_TYPE_STRING, cWmClass,
			G_TYPE_INVALID,
			G_TYPE_INVALID);
		if (erreur)
		{
			cd_warning ("wayfire scale error: %s", erreur->message);
			g_error_free (erreur);
			bSuccess = FALSE;
		}
	}
	return bSuccess;
}

static gboolean _set_current_desktop (G_GNUC_UNUSED int iDesktopNumber, int iViewportNumberX, int iViewportNumberY)
{
	// note: iDesktopNumber is always ignored, we only have one desktop
	gboolean bSuccess = FALSE;
	if (s_pProxy != NULL)
	{
		GError *erreur = NULL;
		bSuccess = dbus_g_proxy_call (s_pProxy, "change_workspace_all_outputs", &erreur,
			G_TYPE_INT, iViewportNumberX,
			G_TYPE_INT, iViewportNumberY,
			G_TYPE_INVALID,
			G_TYPE_INVALID);
		if (erreur)
		{
			cd_warning ("wayfire change workspace error: %s", erreur->message);
			g_error_free (erreur);
			bSuccess = FALSE;
		}
	}
	return bSuccess;
}

static void _workspace_changed (G_GNUC_UNUSED DBusGProxy *proxy, G_GNUC_UNUSED uint32_t output, int32_t x, int32_t y, G_GNUC_UNUSED gpointer data)
{
	// TODO: Wayfire can have independent workspaces on different outputs, this cannot be handled in the current scenario
	g_desktopGeometry.iCurrentViewportX = x;
	g_desktopGeometry.iCurrentViewportY = y;
	gldi_object_notify (&myDesktopMgr, NOTIFICATION_DESKTOP_CHANGED);
}


static void _register_wayfire_backend (void)
{
	GldiDesktopManagerBackend p;
	memset (&p, 0, sizeof (GldiDesktopManagerBackend));
	
	p.present_class = _present_class;
	p.present_windows = _present_windows;
	// p.present_desktops = present_desktops;
	p.set_current_desktop = _set_current_desktop;
	
	gldi_desktop_manager_register_backend (&p);
}

static void _unregister_wayfire_backend (void)
{
	// ??
}


static void _on_wayfire_owner_changed (G_GNUC_UNUSED const gchar *cName, gboolean bOwned, G_GNUC_UNUSED gpointer data)
{
	cd_debug ("Wayfire is on the bus (%d)", bOwned);
	
	if (bOwned)  // set up the proxies
	{
		g_return_if_fail (s_pProxy == NULL);
		
		s_pProxy = cairo_dock_create_new_session_proxy (
			CD_WF_BUS, CD_WF_OBJECT, CD_WF_INTERFACE);
		
		// get the number of workspaces + set up signals for changing workspaces
		GError *erreur = NULL;
		dbus_g_proxy_call (s_pProxy, "query_workspace_grid_size", &erreur,
			G_TYPE_INVALID,
			G_TYPE_INT, &g_desktopGeometry.iNbViewportX,
			G_TYPE_INT, &g_desktopGeometry.iNbViewportY,
			G_TYPE_INVALID);
		
		if (erreur)
		{
			cd_warning ("error getting the number of viewports: %s", erreur->message);
			g_error_free (erreur);
			erreur = NULL;
		}
		
		uint32_t current_output;
		dbus_g_proxy_call (s_pProxy, "query_active_output", &erreur,
			G_TYPE_INVALID,
			G_TYPE_UINT, &current_output,
			G_TYPE_INVALID);
		
		if (erreur)
		{
			cd_warning ("error getting the current output: %s", erreur->message);
			g_error_free (erreur);
		}
		else
		{
			dbus_g_proxy_call (s_pProxy, "query_output_workspace", &erreur,
				G_TYPE_UINT, current_output,
				G_TYPE_INVALID,
				G_TYPE_INT, &g_desktopGeometry.iCurrentViewportX,
				G_TYPE_INT, &g_desktopGeometry.iCurrentViewportY,
				G_TYPE_INVALID);
			if (erreur)
			{
				cd_warning ("error getting the current output: %s", erreur->message);
				g_error_free (erreur);
			}
		}

		dbus_g_proxy_add_signal (s_pProxy, "output_workspace_changed",
			G_TYPE_UINT,
			G_TYPE_INT,
			G_TYPE_INT,
			G_TYPE_INVALID);
		
		dbus_g_proxy_connect_signal (s_pProxy, "output_workspace_changed",
			G_CALLBACK (_workspace_changed), NULL, NULL);
		
		_register_wayfire_backend ();
	}
	else if (s_pProxy != NULL)
	{
		dbus_g_proxy_disconnect_signal (s_pProxy, "output_workspace_changed",
			G_CALLBACK (_workspace_changed), NULL);

		g_object_unref (s_pProxy);
		s_pProxy = NULL;
		_unregister_wayfire_backend ();
	}
}
static void _on_detect_wayfire (gboolean bPresent, G_GNUC_UNUSED gpointer data)
{
	cd_debug ("Wayfire is present: %d", bPresent);
	if (bPresent)
	{
		_on_wayfire_owner_changed (CD_WF_BUS, TRUE, NULL);
	}
	cairo_dock_watch_dbus_name_owner (CD_WF_BUS,
		(CairoDockDbusNameOwnerChangedFunc) _on_wayfire_owner_changed,
		NULL);
}
void cd_init_wayfire_backend (void)
{
	cairo_dock_dbus_detect_application_async (CD_WF_BUS,
		(CairoDockOnAppliPresentOnDbus) _on_detect_wayfire,
		NULL);
}


