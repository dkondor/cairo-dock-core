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
#include "cairo-dock-wayfire-integration.h"
#include "cairo-dock-log.h"

#ifdef HAVE_WAYLAND_PROTOCOLS

#include "cairo-dock-wayland-wm.h"
#include "cairo-dock-windows-manager-priv.h"
#include "cairo-dock-wayland-manager-priv.h"
#include "cairo-dock-foreign-toplevel.h"
#include "wayland-wlr-foreign-toplevel-management-client-protocol.h" // zwlr_foreign_toplevel_handle_v1_destroy

#ifdef HAVE_JSON

#include "cairo-dock-dock-priv.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-dock-visibility.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <json.h>
#ifdef HAVE_EVDEV
#include <libevdev/libevdev.h>
#endif

#include "cairo-dock-desktop-manager-priv.h"
#include "cairo-dock-icon-factory.h"  // pAppli
#include "cairo-dock-container-priv.h"  // gldi_container_get_gdk_window
#include "cairo-dock-class-manager-priv.h"
#include "cairo-dock-keybinder.h"


/***********************************************************************
 * private data, reading / writing and some housekeeping
 ***********************************************************************/

static const char default_socket[] = "/tmp/wayfire-wayland-1.socket";

static GIOChannel *s_pIOChannel = NULL; // GIOChannel encapsulating the above socket
static unsigned int s_sidIO = 0; // source ID for the above added to the main loop

struct cb_data {
	gpointer cb; // callback function (either CairoDockDesktopManagerActionResult or CairoDockGrabKeyResult)
	gpointer user_data;
	GldiShortkey *pBinding; // keybinding that is being added (if this callback belongs to a keybinding call)
};
GQueue s_cb_queue; // queue of callbacks for submitted actions -- contains cb_data structs or NULLs if an action does not have a callback

struct wf_keybinding
{
	guint keycode;
	guint modifiers;
	uint64_t id;
};
GList *s_pBindings = NULL; // list of registered keybindings (wf_keybinding structs)

static GldiObjectManager myWayfireWindowObjectMgr;

struct _GldiWfWindowActor {
	GldiWaylandWindowActor wactor;
	
	uint32_t view_id; // IPC ID of this view -- note: a valid view ID is > 0 (Wayfire IPC IDs start from 0, but it will always be assigned to wf::compositor_core_t)
	uint32_t output_id; // IPC ID of the output associated with this view
	
	gboolean bAbove; // whether this view is always on top
	gboolean bPropsChecked; // whether we already queried the menu DBus properties for this view
};
typedef struct _GldiWfWindowActor GldiWfWindowActor;

typedef enum {
	NB_NOTIFICATIONS_WAYFIRE_WINDOW_MANAGER = NB_NOTIFICATIONS_WAYLAND_WM_MANAGER
} CairoWfWMNotifications;


// Helper function to signal failure and free one element of the queue
void _cb_fail (gpointer data)
{
	if (data)
	{
		struct cb_data *cb = (struct cb_data*)data;
		if (cb->pBinding)
		{
			cb->pBinding->bSuccess = FALSE;
			if (cb->cb) ((CairoDockGrabKeyResult) cb->cb) (cb->pBinding, cb->user_data);
			gldi_object_unref (GLDI_OBJECT (cb->pBinding)); // ref was taken in _bind_key ()
		}
		else if (cb->cb)
			((CairoDockDesktopManagerActionResult) cb->cb) (FALSE, cb->user_data);
		g_free (cb);
	}
}

void _handle_broken_socket (void)
{
	if (s_sidIO) g_source_remove (s_sidIO); // will close the socket and free s_pIOChannel as well
	s_sidIO = 0;
	s_pIOChannel = NULL;
	g_queue_clear_full (&s_cb_queue, _cb_fail);
	g_list_free_full (s_pBindings, g_free);
	s_pBindings = NULL;
}

/*
 * Read a message from socket and return its contents or NULL if there
 * is no open socket. The caller should free() the returned string when done.
 * The length of the message is stored in msg_len (if not NULL).
 * Note: this will block until there is a message to read.
 */
static char* _read_msg_full (uint32_t* out_len, GIOChannel *pIOChannel)
{
	const size_t header_size = 4;
	char header[header_size];
	if (g_io_channel_read_chars (pIOChannel, header, header_size, NULL, NULL) != G_IO_STATUS_NORMAL)
	{
		//!! TODO: should this be a failure or is it possible to recover?
		cd_warning ("Error reading from Wayfire's socket");
		return NULL;
	}
	
	uint32_t msg_len;
	memcpy(&msg_len, header, header_size);
	gsize n_read = 0;
	char* msg = (char*)g_malloc (msg_len + 1);
	if (msg_len)
	{
		if (g_io_channel_read_chars (pIOChannel, msg, msg_len, &n_read, NULL) != G_IO_STATUS_NORMAL
			|| n_read != msg_len)
		{
			//!! TODO: should we care if the whole message cannot be read at once?
			cd_warning ("Error reading from Wayfire's socket");
			g_free (msg);
			return NULL;
		}
	}
	msg[msg_len] = 0;
	if (out_len) *out_len = msg_len;
	return msg;
}
/* same as above, but use our static variable */
static char* _read_msg (uint32_t* out_len)
{
	if (! s_pIOChannel) return NULL;
	return _read_msg_full (out_len, s_pIOChannel);
}

/* Send a message to the socket. Return 0 on success, -1 on failure */
static int _send_msg_full (const char* msg, uint32_t len, GIOChannel *pIOChannel)
{
	const size_t header_size = 4;
	gsize n_written = 0;
	char header[header_size];
	memcpy(header, &len, header_size);
	if ((g_io_channel_write_chars (pIOChannel, header, header_size, &n_written, NULL) != G_IO_STATUS_NORMAL) ||
		(g_io_channel_write_chars (pIOChannel, msg, len, &n_written, NULL) != G_IO_STATUS_NORMAL) ||
		n_written != len)
	{
		cd_warning ("Error writing to Wayfire's socket");
		return -1;
	}
	return 0;
}

/* Call a Wayfire IPC method and try to check if it was successful. */
static gboolean _call_ipc_internal (struct json_object *data, gpointer cb, gpointer user_data)
{
	if (! s_pIOChannel)
		// socket was already closed; we should not call _handle_broken_socket () again
		return FALSE;
	
	size_t len;
	const char *tmp = json_object_to_json_string_length (data, JSON_C_TO_STRING_SPACED, &len);
	if (!(tmp && len))
		return FALSE;
	
	if (_send_msg_full (tmp, len, s_pIOChannel) < 0)
	{
		_handle_broken_socket ();
		return FALSE;
	}
	
	// save the callback if provided
	if (cb)
	{
		struct cb_data *data = g_new0 (struct cb_data, 1);
		data->cb = cb;
		data->user_data = user_data;
		g_queue_push_tail (&s_cb_queue, data);
	}
	else g_queue_push_tail (&s_cb_queue, NULL); // dummy element
	
	return TRUE;
}

static void _call_ipc (struct json_object* data, CairoDockDesktopManagerActionResult cb, gpointer user_data) {
	if (!_call_ipc_internal (data, cb, user_data))
		if (cb) cb (FALSE, user_data);
}

static void _call_ipc_method_no_data (const char *method)
{
	struct json_object *obj = json_object_new_object ();
	json_object_object_add (obj, "method", json_object_new_string (method));
	json_object_object_add (obj, "data", json_object_new_object ());
	_call_ipc (obj, NULL, NULL);
	json_object_put (obj);
}


/***********************************************************************
 * basic desktop manager functionality (scale, expo)
 ***********************************************************************/

/* Start scale on the current workspace */
static void _present_windows() {
	_call_ipc_method_no_data ("scale/toggle");
}

/* Start scale including all views of the given class */
static void _present_class(const gchar *cClass, CairoDockDesktopManagerActionResult cb, gpointer user_data) {
	const gchar *cWmClass = cairo_dock_get_class_wm_class (cClass);
	if (cWmClass) 
	{
		struct json_object *obj = json_object_new_object ();
		json_object_object_add (obj, "method", json_object_new_string ("scale_ipc_filter/activate_appid"));
		struct json_object *data = json_object_new_object ();
		json_object_object_add (data, "all_workspaces", json_object_new_boolean (1));
		json_object_object_add (data, "app_id", json_object_new_string (cWmClass));
		json_object_object_add (obj, "data", data);
		_call_ipc (obj, cb, user_data);
		json_object_put (obj);
	}
	else if (cb) cb (FALSE, user_data);
}


/* Start expo on the current output */
static void _present_desktops() {
	_call_ipc_method_no_data ("expo/toggle");
}

/* Toggle show desktop functionality (i.e. minimize / unminimize all views).
 * Note: bShow argument is ignored, we don't know if the desktop is shown / hidden */
static void _show_hide_desktop(G_GNUC_UNUSED gboolean bShow) {
	_call_ipc_method_no_data ("wm-actions/toggle_showdesktop");
}


/***********************************************************************
 * keybindings
 ***********************************************************************/

static void _binding_registered (gboolean bSuccess, GldiShortkey *pBinding, CairoDockGrabKeyResult cb, gpointer user_data, uint64_t id)
{
	pBinding->bSuccess = bSuccess;
	
	if (bSuccess)
	{
		struct wf_keybinding *binding = g_new (struct wf_keybinding, 1);
		binding->keycode = pBinding->keycode;
		binding->modifiers = pBinding->modifiers;
		binding->id = id;
	
		s_pBindings = g_list_prepend (s_pBindings, binding);
	}
	
	if (cb) cb (pBinding, user_data);
	gldi_object_unref (GLDI_OBJECT (pBinding)); // ref was taken in _bind_key () -- note that this might immediately unbind if was unrefed outside
}

#ifdef HAVE_EVDEV
static void _bind_key (GldiShortkey *pBinding, gboolean grab, CairoDockGrabKeyResult cb, gpointer user_data)
{
	if (! s_pIOChannel)
	{
		// socket was already closed; we should not call _handle_broken_socket () again
		// (in this case, our binding list should be empty as well)
		pBinding->bSuccess = FALSE;
		if (cb) cb (pBinding, user_data);
		return;
	}
	
	guint keycode = pBinding->keycode;
	guint modifiers = pBinding->modifiers;
	
	if (grab)
	{
		const char *name = libevdev_event_code_get_name (EV_KEY, keycode - 8); // -8: difference between X11 and Linux hardware keycodes
		if (!name)
		{
			cd_warning ("Unknown keycode: %u", keycode);
			pBinding->bSuccess = FALSE;
			if (cb) cb (pBinding, user_data);
			return;
		}
		
		GString *str = g_string_sized_new (40); // all modifiers + some long key name
		
		// first process modifiers (note: GDK_SUPER_MASK and GDK_META_MASK have been removed by the caller)
		if (modifiers & GDK_CONTROL_MASK) g_string_append (str, "<ctrl>");
		if (modifiers & GDK_MOD1_MASK) g_string_append (str, "<alt>");
		if (modifiers & GDK_SHIFT_MASK) g_string_append (str, "<shift>");
		if (modifiers & GDK_MOD4_MASK) g_string_append (str, "<super>");
		
		g_string_append (str, name);
		
		cd_debug ("Trying to register binding: %u, %u -- %s", keycode, modifiers, str->str);
		
		struct json_object *obj = json_object_new_object ();
		json_object_object_add (obj, "method", json_object_new_string ("command/register-binding"));
		struct json_object *data = json_object_new_object ();
		json_object_object_add (data, "binding", json_object_new_string_len (str->str, str->len));
		json_object_object_add (data, "exec-always", json_object_new_boolean (1)); // unsure, maybe FALSE would be better?
		json_object_object_add (obj, "data", data);
		
		size_t len;
		const char *tmp = json_object_to_json_string_length (obj, JSON_C_TO_STRING_SPACED, &len);
		if (!(tmp && len))
		{
			json_object_put (obj);
			g_string_free (str, TRUE);
			pBinding->bSuccess = FALSE;
			if (cb) cb (pBinding, user_data);
			return;
		}
		
		int ret = _send_msg_full (tmp, len, s_pIOChannel);
		json_object_put (obj);
		g_string_free (str, TRUE);
		
		if (ret < 0)
		{
			pBinding->bSuccess = FALSE;
			if (cb) cb (pBinding, user_data);
			_handle_broken_socket ();
			return;
		}
		
		// add a callback
		struct cb_data *cb1 = g_new0 (struct cb_data, 1);
		cb1->pBinding = pBinding;
		cb1->cb = cb;
		cb1->user_data = user_data;
		gldi_object_ref (GLDI_OBJECT (pBinding)); // need to keep a reference in case the caller destroys it before our callback
		g_queue_push_tail (&s_cb_queue, cb1);
	}
	else if (pBinding->bSuccess) // only try to remove if it was successfully registered
	{
		GList *it = s_pBindings;
		while (it)
		{
			struct wf_keybinding *binding = (struct wf_keybinding*)it->data;
			
			if (binding->keycode == keycode && binding->modifiers == modifiers)
			{
				struct json_object *obj = json_object_new_object ();
				json_object_object_add (obj, "method", json_object_new_string ("command/unregister-binding"));
				struct json_object *data = json_object_new_object ();
				json_object_object_add (data, "binding-id", json_object_new_uint64 (binding->id));
				json_object_object_add (obj, "data", data);
				_call_ipc (obj, NULL, NULL); // we don't care about the result in this case
				json_object_put (obj);
				
				s_pBindings = g_list_delete_link (s_pBindings, it);
				g_free (binding);
				break;
			}
			
			it = it->next;
		}
		
		if (!it) cd_warning ("Cannot find binding to unregister");
	}
}
#endif


/***********************************************************************
 * watch extra view state
 ***********************************************************************/

static void _start_watch_events (void);

// whether we are watching sticky events
static gboolean s_bHaveAbove = FALSE; // whether Wayfire provides the "view-above" event
static gboolean s_bCanSticky = FALSE;
static gboolean s_bCanAbove = FALSE;
static gboolean s_bHaveMenuProps = FALSE; // if gtk-shell and kde-appmenu properties are available
static gboolean s_bCoordsFloat = FALSE; // coordinates are reported as floats (newer Wayfire versions)

static gboolean s_bWatchWMEvents = FALSE;
// hash table mapping view IPC IDs to window actors
static GHashTable *s_pActorMap = NULL; // key: IPC ID, value: GldiWfWindowActor*
// hash table IDs seen in the IPC interface but not with a matching GldiWfWindowActor
typedef struct _WfMissingActorData {
	gboolean bSticky;
	gboolean bAbove;
	uint32_t output_id;
	GtkAllocation windowGeometry;
} WfMissingActorData;

/*typedef enum {
	MISSING_ACTOR_STATE_STICKY = 1,
	MISSING_ACTOR_STATE_ABOVE = 2
} WfMissingActorState; */
static GHashTable *s_pMissingActors = NULL; // key: IPC ID, value: WfMissingActorData

typedef enum {
	PROP_GTK_BUS_NAME,
	PROP_GTK_MENUBAR_PATH,
	PROP_GTK_APPMENU_PATH,
	PROP_GTK_WINDOW_PATH,
	PROP_GTK_APP_PATH,
	PROP_KDE_SERVICE_NAME,
	PROP_KDE_OBJECT_PATH
} WfViewMenuProps;

typedef struct _wf_view_prop_cb_data {
	WfViewMenuProps prop;
	uint32_t id;
} wf_view_prop_cb_data;

static void _on_got_view_prop (wf_view_prop_cb_data *data, struct json_object *res, gboolean bResult)
{
	GldiWaylandWindowActor *wactor = g_hash_table_lookup (s_pActorMap, GUINT_TO_POINTER (data->id));
	if (wactor) // note: not an error if missing, could have been destroyed
	{
		struct json_object *tmp = bResult ? json_object_object_get (res, "value") : NULL;
		if (tmp || !bResult)
		{
			const char *value = tmp ? json_object_get_string (tmp) : NULL;
			if (value || !bResult)
			{
				GldiWindowActor *actor = (GldiWindowActor*)wactor;
				if (!actor->pDBusProps) actor->pDBusProps = g_new0 (GldiWindowDBusProperties, 1);
				
				switch (data->prop)
				{
					case PROP_GTK_BUS_NAME:
						g_free (actor->pDBusProps->cGTKBusName);
						actor->pDBusProps->cGTKBusName = (value && *value) ? g_strdup (value) : NULL;
						break;
					case PROP_GTK_MENUBAR_PATH:
						g_free (actor->pDBusProps->cGTKMenuBarPath);
						actor->pDBusProps->cGTKMenuBarPath = (value && *value) ? g_strdup (value) : NULL;
						break;
					case PROP_GTK_APPMENU_PATH:
						g_free (actor->pDBusProps->cGTKAppMenuPath);
						actor->pDBusProps->cGTKAppMenuPath = (value && *value) ? g_strdup (value) : NULL;
						break;
					case PROP_GTK_WINDOW_PATH:
						g_free (actor->pDBusProps->cGTKWindowPath);
						actor->pDBusProps->cGTKWindowPath = (value && *value) ? g_strdup (value) : NULL;
						break;
					case PROP_GTK_APP_PATH:
						g_free (actor->pDBusProps->cGTKAppPath);
						actor->pDBusProps->cGTKAppPath = (value && *value) ? g_strdup (value) : NULL;
						break;
					case PROP_KDE_SERVICE_NAME:
						g_free (actor->pDBusProps->cKDEServiceName);
						actor->pDBusProps->cKDEServiceName = (value && *value) ? g_strdup (value) : NULL;
						break;
					case PROP_KDE_OBJECT_PATH:
						g_free (actor->pDBusProps->cKDEObjectPath);
						actor->pDBusProps->cKDEObjectPath = (value && *value) ? g_strdup (value) : NULL;
						break;
				}
			}
			else cd_warning ("No 'value' member in reply to get-view-property request");
		}
		else cd_warning ("No 'value' member in reply to get-view-property request");
	}
	
	g_free (data);
}

static gboolean _check_one_prop (uint32_t id, const char *prop_name, WfViewMenuProps prop)
{
	struct json_object *obj = json_object_new_object ();
	json_object_object_add (obj, "method", json_object_new_string ("window-rules/get-view-property"));
	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "id", json_object_new_uint64 (id));
	json_object_object_add (data, "property", json_object_new_string (prop_name));
	json_object_object_add (obj, "data", data);
	
	wf_view_prop_cb_data *cb_data1 = g_new (wf_view_prop_cb_data, 1);
	cb_data1->id = id;
	cb_data1->prop = prop;
	
	gboolean ret = _call_ipc_internal (obj, _on_got_view_prop, cb_data1);
	if (!ret) g_free (cb_data1);
	json_object_put (obj);
	return ret;
}

static void _check_menu_props (uint32_t id)
{
	// note: failure means error writing to Wayfire's socket, do not continue with the checks in this case
	if (_check_one_prop (id, "kde-appmenu-service-name", PROP_KDE_SERVICE_NAME))
	if (_check_one_prop (id, "kde-appmenu-object-path", PROP_KDE_OBJECT_PATH))
	if (_check_one_prop (id, "gtk-shell-app-menu-path", PROP_GTK_APPMENU_PATH))
	if (_check_one_prop (id, "gtk-shell-application-object-path", PROP_GTK_APP_PATH))
	if (_check_one_prop (id, "gtk-shell-menubar-path", PROP_GTK_MENUBAR_PATH))
	if (_check_one_prop (id, "gtk-shell-unique-bus-name", PROP_GTK_BUS_NAME))
		_check_one_prop (id, "gtk-shell-window-object-path", PROP_GTK_WINDOW_PATH);
}

static gboolean _wf_on_window_created (G_GNUC_UNUSED gpointer data, GldiWindowActor *actor)
{
	if (!(s_bCanAbove || s_bCanSticky || s_bHaveMenuProps)) return GLDI_NOTIFICATION_LET_PASS;
	if (!gldi_object_is_manager_child (GLDI_OBJECT(actor), &myWayfireWindowObjectMgr)) return GLDI_NOTIFICATION_LET_PASS;
	
	GldiWfWindowActor *wfactor = (GldiWfWindowActor*)actor;
	if (!wfactor->wactor.cClassExtra) return GLDI_NOTIFICATION_LET_PASS;
	if (wfactor->view_id)
	{
		cd_warning ("View already seen!");
		return GLDI_NOTIFICATION_LET_PASS;
	}
	
	const char *tmp = strstr (wfactor->wactor.cClassExtra, "wf-ipc-");
	if (tmp)
	{
		tmp += 7; // strlen ("wf-ipc-")
		if (*tmp)
		{
			const char *end = NULL;
			errno = 0;
			uint32_t id = (uint32_t)strtoul (tmp, (char**)&end, 10);
			if (errno == 0 && end != tmp)
			{
				// valid ID
				wfactor->view_id = id;
				g_hash_table_insert (s_pActorMap, GUINT_TO_POINTER (id), wfactor); // note: will invalidate any previous value
				
				// check if this view was already seen
				gpointer tmp;
				if (g_hash_table_lookup_extended (s_pMissingActors, GUINT_TO_POINTER (id), NULL, &tmp))
				{
					GldiWaylandWindowActor *wactor = (GldiWaylandWindowActor*)actor;
					WfMissingActorData *pData = (WfMissingActorData*) (tmp);
					gldi_wayland_wm_sticky_changed (wactor, pData->bSticky, TRUE);
					wfactor->bAbove = pData->bAbove;
					wfactor->output_id = pData->output_id;
					gldi_wayland_wm_geometry_changed (wactor, &pData->windowGeometry, TRUE);
					g_hash_table_remove (s_pMissingActors, GUINT_TO_POINTER (id));
				}
				
				if (s_bHaveMenuProps)
				{
					_check_menu_props (id);
					wfactor->bPropsChecked = TRUE;
				}
			}
			else cd_warning ("Error parsing IPC ID: %s", tmp - 7);
		}
	}
	
	return GLDI_NOTIFICATION_LET_PASS;
}

static gboolean _wf_on_window_destroyed (G_GNUC_UNUSED gpointer data, GldiWindowActor *actor)
{
	if (!(s_bCanAbove || s_bCanSticky || s_bHaveMenuProps)) return GLDI_NOTIFICATION_LET_PASS;
	if (!gldi_object_is_manager_child (GLDI_OBJECT(actor), &myWayfireWindowObjectMgr)) return GLDI_NOTIFICATION_LET_PASS;
	
	GldiWfWindowActor *wfactor = (GldiWfWindowActor*)actor;
	if (!wfactor->view_id) return GLDI_NOTIFICATION_LET_PASS;
	
	g_hash_table_remove (s_pActorMap, GUINT_TO_POINTER (wfactor->view_id));
	
	wfactor->view_id = 0; // need to reset as we can call function from _wf_on_window_class_changed() below
	wfactor->bAbove = FALSE;
	wfactor->bPropsChecked = FALSE;
	
	return GLDI_NOTIFICATION_LET_PASS;
}

static gboolean _wf_on_window_class_changed (G_GNUC_UNUSED gpointer ptr, GldiWindowActor *actor,
	G_GNUC_UNUSED const gchar *cOldClass, G_GNUC_UNUSED const gchar *cOldWmClass)
{
	if (!(s_bCanAbove || s_bCanSticky || s_bHaveMenuProps)) return GLDI_NOTIFICATION_LET_PASS;
	if (!gldi_object_is_manager_child (GLDI_OBJECT(actor), &myWayfireWindowObjectMgr)) return GLDI_NOTIFICATION_LET_PASS;
	
	GldiWfWindowActor *wfactor = (GldiWfWindowActor*)actor;
	uint32_t old_id = wfactor->view_id;
	gboolean bAbove, bPropsChecked;
	if (old_id)
	{
		bAbove = wfactor->bAbove;
		bPropsChecked = wfactor->bPropsChecked;
	}
	
	_wf_on_window_destroyed (NULL, actor);
	_wf_on_window_created (NULL, actor);
	
	if (old_id && (wfactor->view_id != 0))
	{
		if (old_id == wfactor->view_id)
		{
			wfactor->bAbove = bAbove;
			wfactor->bPropsChecked = bPropsChecked;
		}
		else cd_warning ("View ID changed unexpectedly!");
	}
	
	return GLDI_NOTIFICATION_LET_PASS;
}


static void _add_existing_actor (gpointer ptr, G_GNUC_UNUSED gpointer data)
{
	GldiWindowActor *actor = (GldiWindowActor*)ptr;
	// note: if bDisplayed == FALSE, then this window is not in the
	// taskbar yet and we will get a separate notification about it later
	if (actor->bDisplayed) _wf_on_window_created (NULL, actor);
}

static void _init_window_maping (void)
{
	if (! (s_bCanAbove || s_bCanSticky || s_bHaveMenuProps)) return;
	if (s_bWatchWMEvents) return; // already started
	
	s_pActorMap = g_hash_table_new (NULL, NULL);
	s_pMissingActors = g_hash_table_new_full (NULL, NULL, NULL, g_free);
	
	gldi_object_register_notification (&myWindowObjectMgr,
		NOTIFICATION_WINDOW_CREATED,
		(GldiNotificationFunc) _wf_on_window_created,
		GLDI_RUN_FIRST, NULL);
	gldi_object_register_notification (&myWindowObjectMgr,
		NOTIFICATION_WINDOW_DESTROYED,
		(GldiNotificationFunc) _wf_on_window_destroyed,
		GLDI_RUN_FIRST, NULL);
	gldi_object_register_notification (&myWindowObjectMgr,
		NOTIFICATION_WINDOW_CLASS_CHANGED,
		(GldiNotificationFunc) _wf_on_window_class_changed,
		GLDI_RUN_FIRST, NULL);
	
	// Add all existing views
	gldi_windows_foreach_unordered (_add_existing_actor, NULL);
	
	// Start listening to events to track the actual state
	s_bWatchWMEvents = TRUE;
	_start_watch_events ();
}


static void _get_supported_actions (gboolean *bCanFullscreen, gboolean *bCanSticky, gboolean *bCanBelow, gboolean *bCanAbove, gboolean *bCanKill)
{
	// note: there is no capabilities event, we just guess based on the requests that are available
	if (bCanFullscreen) *bCanFullscreen = TRUE;
	if (bCanSticky) *bCanSticky = FALSE;
	if (bCanBelow) *bCanBelow = FALSE;
	if (bCanAbove) *bCanAbove = FALSE;
	if (bCanKill) *bCanKill = FALSE;
	if (bCanSticky) *bCanSticky = s_bCanSticky && s_bWatchWMEvents;
	if (bCanAbove) *bCanAbove = s_bCanAbove && s_bWatchWMEvents;
}

void gldi_wf_set_sticky (GldiWindowActor *actor, gboolean bSticky)
{
	if (! (s_bCanSticky && s_bWatchWMEvents)) return;
	GldiWfWindowActor *wactor = (GldiWfWindowActor*)actor;
	uint32_t id = wactor->view_id;
	if (!id) return;
	
	struct json_object *obj = json_object_new_object ();
	json_object_object_add (obj, "method", json_object_new_string ("wm-actions/set-sticky"));
	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "state", json_object_new_boolean (!!bSticky));
	json_object_object_add (data, "view-id", json_object_new_uint64 (id));
	json_object_object_add (obj, "data", data);
	_call_ipc (obj, NULL, NULL); // no callback -- we will be notified of a state change
	json_object_put (obj);
}

typedef struct _WfAboveCbData {
	uint32_t id;
	gboolean bAbove;
} WfAboveCbData;

static void _set_above_cb (gboolean bSuccess, gpointer ptr)
{
	if (bSuccess)
	{
		// let's try to set the bAbove for this view
		WfAboveCbData *pData = (WfAboveCbData*)ptr;
		GldiWfWindowActor *wfactor = g_hash_table_lookup (s_pActorMap, GUINT_TO_POINTER (pData->id));
		if (wfactor)
		{
			wfactor->bAbove = pData->bAbove;
		}
	}
	g_free (ptr);
}

void gldi_wf_set_above (GldiWindowActor *actor, gboolean bAbove)
{
	if (! (s_bCanAbove && s_bWatchWMEvents)) return;
	GldiWfWindowActor *wactor = (GldiWfWindowActor*)actor;
	uint32_t id = wactor->view_id;
	if (!id) return;
	
	struct json_object *obj = json_object_new_object ();
	json_object_object_add (obj, "method", json_object_new_string ("wm-actions/set-always-on-top"));
	struct json_object *data = json_object_new_object ();
	json_object_object_add (data, "state", json_object_new_boolean (!!bAbove));
	json_object_object_add (data, "view-id", json_object_new_uint64 (id));
	json_object_object_add (obj, "data", data);
	
	WfAboveCbData *cbdata = NULL;
	if (!s_bHaveAbove)
	{
		cbdata = g_new0 (WfAboveCbData, 1);
		cbdata->id = id;
		cbdata->bAbove = bAbove;
	}
	_call_ipc (obj, cbdata ? _set_above_cb : NULL, cbdata); // we give the ID as the parameter in case wactor is destroyed before the callback
	json_object_put (obj);
}

void gldi_wf_is_above_or_below (GldiWindowActor *actor, gboolean *bIsAbove, gboolean *bIsBelow)
{
	if (bIsBelow) *bIsBelow = FALSE;
	if (bIsAbove)
	{
		GldiWfWindowActor *wfactor = (GldiWfWindowActor*)actor;
		*bIsAbove = wfactor->bAbove;
	}
}

static void _reset_object (GldiObject *obj)
{
	GldiWfWindowActor *wfactor = (GldiWfWindowActor*)obj;
	zwlr_foreign_toplevel_handle_v1_destroy ((struct zwlr_foreign_toplevel_handle_v1*) wfactor->wactor.handle);
}

/***********************************************************************
 * watch views' state
 ***********************************************************************/

// whether we registered a watch (we can only do this once)
static gboolean s_bWatchRegistered = FALSE;
// whether we received Wayfire's configuration
static gboolean s_bGotConfig = FALSE;
// whether we received the initial list of views already
static gboolean s_bListViewsDone = FALSE;

static void _add_vis_watch_cb (gboolean bSuccess, G_GNUC_UNUSED gpointer user_data);
static void _list_views_cb (const struct json_object *arr);
static void _process_view (const gchar *event, const struct json_object *view);
static void _got_configuration (struct json_object *conf);

static void _start_watch_events (void)
{
	// start listening to Wayfire's events
	if (s_bWatchRegistered) return;
	s_bWatchRegistered = TRUE; // we can only try once
	
	// check first whether the view-above event is available
	const gchar *req = "{\"method\": \"wayfire/configuration\", \"data\": { } }";
	int ret = _send_msg_full (req, strlen (req), s_pIOChannel);
	if (ret < 0)
	{
		// socket error, abort
		_handle_broken_socket ();
		return;
	}
	
	// add a callback to check if we succeeded
	struct cb_data *cb1 = g_new0 (struct cb_data, 1);
	cb1->cb = _got_configuration;
	g_queue_push_tail (&s_cb_queue, cb1);
}
	
static void _got_configuration (struct json_object *conf)
{	
	struct json_object *version = json_object_object_get (conf, "api-version");
	if (version)
	{
		uint64_t ver = json_object_get_uint64 (version);
		s_bHaveAbove = (ver >= 20251226);
		s_bHaveMenuProps = (ver >= 20260122);
		s_bCoordsFloat = (ver >= 20260617);
	}
	else cd_warning ("Cannot parse Wayfire version!");
	
	s_bGotConfig = TRUE;
	
	const gchar *watch_req =
	s_bHaveAbove ?
"{\"method\": \"window-rules/events/watch\", \"data\": {"
"\"events\": [\"view-geometry-changed\","
"\"view-mapped\", \"view-unmapped\", \"view-sticky\", \"view-always-on-top\"] } }" :
"{\"method\": \"window-rules/events/watch\", \"data\": {"
"\"events\": [\"view-geometry-changed\","
"\"view-mapped\", \"view-unmapped\", \"view-sticky\"] } }";

	int ret = _send_msg_full (watch_req, strlen (watch_req), s_pIOChannel);
	if (ret < 0)
	{
		// socket error, abort
		_handle_broken_socket ();
		return;
	}
	
	// add a callback to check if we succeeded
	struct cb_data *cb1 = g_new0 (struct cb_data, 1);
	cb1->cb = _add_vis_watch_cb;
	g_queue_push_tail (&s_cb_queue, cb1);
}

static void _add_vis_watch_cb (gboolean bSuccess, G_GNUC_UNUSED gpointer user_data)
{
	if (!bSuccess)
	{
		cd_warning ("Cannot add Wayfire event watch");
		return;
	}
	
	if (!s_bWatchWMEvents) return; // if cancelled in the meantime
	
	// now we are watching events, but we need to call list-views, since
	// we may not get events about all of them
	const gchar *list_req = "{\"method\": \"window-rules/list-views\", \"data\": {} }";
	int ret = _send_msg_full (list_req, strlen (list_req), s_pIOChannel);
	if (ret < 0)
	{
		// socket error, abort
		_handle_broken_socket ();
		return;
	}
	
	// add a special callback -- need to be careful as this call will return a json array instead of the usual dict
	struct cb_data *cb1 = g_new0 (struct cb_data, 1);
	cb1->cb = _list_views_cb;
	g_queue_push_tail (&s_cb_queue, cb1);
}

static void _list_views_cb (const struct json_object *arr)
{
	if (!s_bWatchWMEvents) return; // if cancelled before getting results
	
	size_t i;
	size_t len = json_object_array_length (arr);
	for (i = 0; i < len; i++)
	{
		struct json_object *tmp = json_object_array_get_idx (arr, i);
		if (tmp) _process_view (NULL, tmp);
	}
	
	gldi_docks_visibility_start (); // will refresh dock visibility if needed
	
	s_bListViewsDone = TRUE;
}


static gboolean _get_int_value2 (const struct json_object *tmp, int32_t *res)
{
	if (!json_object_is_type (tmp, json_type_int)) return FALSE;
	errno = 0;
	int64_t res2 = json_object_get_int64 (tmp);
	if (errno || res2 > G_MAXINT32 || res2 < G_MININT32) return FALSE;
	*res = (int32_t) res2;
	return TRUE;
}

static gboolean _get_int_value (const struct json_object *obj, const gchar *key, int32_t *res)
{
	const struct json_object *tmp = json_object_object_get (obj, key);
	if (!tmp) return FALSE;
	return _get_int_value2 (tmp, res);
}

static gboolean _get_float_value (const struct json_object *obj, const gchar *key, int32_t *res)
{
	const struct json_object *tmp = json_object_object_get (obj, key);
	if (!tmp) return FALSE;
	if (!json_object_is_type (tmp, json_type_double)) return _get_int_value2 (tmp, res); // try parse as integer instead
	errno = 0;
	double res2 = json_object_get_double (tmp);
	if (errno || res2 > G_MAXINT32 || res2 < G_MININT32) return FALSE;
	*res = (int32_t) round (res2); // note: cannot do much if it is not a whole number
	return TRUE;
}

static gboolean _get_view_geometry (const struct json_object *view, GtkAllocation *rect, uint32_t *output_id)
{
	const struct json_object *tmp = json_object_object_get (view, "output-id");
	if (!tmp || !json_object_is_type (tmp, json_type_int)) return FALSE;
	
	errno = 0;
	*output_id = (uint32_t)json_object_get_uint64 (tmp); // note: Wayfire IPC IDs are 32-bit
	if (errno) return FALSE;
	
	tmp = json_object_object_get (view, "geometry");
	if (!tmp || ! json_object_is_type (tmp, json_type_object))
		return FALSE;
	
	return
		s_bCoordsFloat ?
		(_get_float_value (tmp, "x", &rect->x) && _get_float_value (tmp, "y", &rect->y) &&
			_get_float_value (tmp, "width", &rect->width) && _get_float_value (tmp, "height", &rect->height))
		:
		(_get_int_value (tmp, "x", &rect->x) && _get_int_value (tmp, "y", &rect->y) &&
			_get_int_value (tmp, "width", &rect->width) && _get_int_value (tmp, "height", &rect->height));
}

// Process a view, either because of an event, or from the initial reply to
// list-views (in this case, event == NULL).
// Note: view should not be NULL, but could be for some events (but we should not be listening to those)
static void _process_view (const gchar *event, const struct json_object *view)
{
	if (!s_bWatchWMEvents) return; // if we are not listening (cannot unregister watch, so we will keep getting events)
	
	{
		struct json_object *tmp = view ? json_object_object_get (view, "app-id") : NULL;
		cd_debug ("event: %s, app-id: %s", event, tmp ? json_object_get_string (tmp) : "(none)");
	}
	
	// check if we actually have a view
	if (!view)
	{
		cd_warning ("Unexpected event with no view data: %s", event);
		return;
	}
	
	// first, get the view's ID -- Wayfire uses 32-bit integers for now
	uint32_t id;
	errno = 0;
	struct json_object *tmp = json_object_object_get (view, "id");
	if (tmp) id = (uint32_t) json_object_get_uint64 (tmp);
	else errno = 1;
	if (errno)
	{
		cd_warning ("Cannot parse view ID");
		return; // TODO: should we cancel?
	}
	
	gboolean bDelete = event && ! strcmp (event, "view-unmapped");
	if (bDelete) g_hash_table_remove (s_pMissingActors, GUINT_TO_POINTER (id));
	
	// add or update this view -- only if it is interesting to us
	// first, check if it is mapped (we don't care about unmapped views)
	tmp = json_object_object_get (view, "mapped");
	if (!tmp || ! json_object_is_type (tmp, json_type_boolean))
	{
		cd_warning ("No 'mapped' property for view %u", id);
		return;
	}
	if (! json_object_get_boolean (tmp)) return; // nothing else to do
	
	tmp = json_object_object_get (view, "type");
	const char *str = tmp ? json_object_get_string (tmp) : NULL;
	if (!str)
	{
		cd_warning ("Cannot get type for view %u", id);
		return;
	}
	if (!strcmp (str, "toplevel"))
	{
		// normal toplevel view (including XWayland views as well)
		// note: s_pActorMap != NULL if s_bWatchWMEvents == TRUE
		GldiWfWindowActor *wfactor = g_hash_table_lookup (s_pActorMap, GUINT_TO_POINTER (id));
		
		// get and update the view's geometry
		GtkAllocation rect;
		uint32_t output_id;
		if (! _get_view_geometry (view, &rect, &output_id))
		{
			cd_warning ("Cannot parse geometry for view %u", id);
			return;
		}
		
		if (wfactor)
		{
			if (wfactor->output_id != output_id)
			{
				wfactor->output_id = output_id;
				//!! TODO: do this from idle??
				gldi_object_notify (&myWindowObjectMgr, NOTIFICATION_WINDOW_SIZE_POSITION_CHANGED, wfactor);
			}
			
			const GtkAllocation *tmp = &wfactor->wactor.pending_window_geometry;
			
			if ( (tmp->x != rect.x) || (tmp->y != rect.y) ||
				(tmp->width != rect.width) || (tmp->height != rect.height) )
			{
				gldi_wayland_wm_geometry_changed ((GldiWaylandWindowActor*)wfactor, &rect, TRUE);
			}
		}
		else
		{
			WfMissingActorData *pData = NULL;
			gpointer ptr = g_hash_table_lookup (s_pMissingActors, GUINT_TO_POINTER (id));
			if (ptr) pData = (WfMissingActorData*)ptr;
			else pData = g_new0 (WfMissingActorData, 1);
			
			pData->output_id = output_id;
			pData->windowGeometry = rect;
			
			if (!ptr) g_hash_table_insert (s_pMissingActors, GUINT_TO_POINTER (id), pData);
		}
		
		gboolean bInitialState = !event || !strcmp (event, "view-mapped");
		gboolean bStickyChanged = !bInitialState && event && !strcmp (event, "view-sticky");
		gboolean bAboveChanged  = !bInitialState && event && !strcmp (event, "view-always-on-top");
		
		if (bInitialState || bStickyChanged || bAboveChanged)
		{
			if (wfactor)
			{
				if (bInitialState || bStickyChanged)
				{
					tmp = json_object_object_get (view, "sticky");
					if (!tmp || ! json_object_is_type (tmp, json_type_boolean))
						cd_warning ("No 'sticky' property for view %u", id);
					else gldi_wayland_wm_sticky_changed ((GldiWaylandWindowActor*)wfactor, !!json_object_get_boolean (tmp), TRUE);
				}
				if ((bInitialState || bAboveChanged) && s_bHaveAbove)
				{
					tmp = json_object_object_get (view, "always-on-top");
					if (!tmp || ! json_object_is_type (tmp, json_type_boolean))
						cd_warning ("No 'always-on-top' property for view %u", id);
					else wfactor->bAbove = !!json_object_get_boolean (tmp);
				}
				if (!wfactor->bPropsChecked)
				{
					_check_menu_props (id);
					wfactor->bPropsChecked = TRUE;
				}
			}
			else
			{
				WfMissingActorData *pData = NULL;
				gpointer ptr = g_hash_table_lookup (s_pMissingActors, GUINT_TO_POINTER (id));
				if (ptr) pData = (WfMissingActorData*)ptr;
				else
				{
					pData = g_new0 (WfMissingActorData, 1);
					g_hash_table_insert (s_pMissingActors, GUINT_TO_POINTER (id), pData);
				}
				
				if (bInitialState || bStickyChanged)
				{
					tmp = json_object_object_get (view, "sticky");
					if (!tmp || ! json_object_is_type (tmp, json_type_boolean))
						cd_warning ("No 'sticky' property for view %u", id);
					else pData->bSticky = !!json_object_get_boolean (tmp);
				}
				if ((bInitialState || bAboveChanged) && s_bHaveAbove)
				{
					tmp = json_object_object_get (view, "always-on-top");
					if (!tmp || ! json_object_is_type (tmp, json_type_boolean))
						cd_warning ("No 'always-on-top' property for view %u", id);
					else pData->bAbove = !!json_object_get_boolean (tmp);
				}
			}
		}
	}
	else if (! (strcmp (str, "background") && strcmp (str, "panel") && strcmp (str, "overlay")))
	{
		// a layer-shell view
		tmp = json_object_object_get (view, "app-id");
		if (!tmp  || ! json_object_is_type (tmp, json_type_string))
		{
			cd_warning ("No app-id for view %u", id);
			return;
		}
		str = json_object_get_string (tmp);
		if (! strncmp (str, "cairo-dock-", 11))
		{
			// this is one of our docks
			CairoDock *pDock = gldi_dock_get (str + 11);
			// not an error if it does not exist, we could have just
			// removed it before getting this message
			// (or there could be another Cairo-Dock process running)
			if (pDock && pDock->iRefCount == 0)
			{
				GtkAllocation rect;
				uint32_t output_id;
				if (! _get_view_geometry (view, &rect, &output_id))
				{
					cd_warning ("Cannot parse geometry for view %u", id);
					return;
				}
				
				if (pDock->container.bIsHorizontal)
				{
					pDock->container.iWindowPositionX = rect.x;
					pDock->container.iWindowPositionY = rect.y;
					pDock->container.iWidth = rect.width;
					pDock->container.iHeight = rect.height;
				}
				else
				{
					pDock->container.iWindowPositionX = rect.y;
					pDock->container.iWindowPositionY = rect.x;
					pDock->container.iWidth = rect.height;
					pDock->container.iHeight = rect.width;
				}
				pDock->pVisibilityData = GUINT_TO_POINTER (output_id);
				
				gldi_dock_visibility_refresh (pDock);
			}
		}
	}
}

gboolean _dock_is_same_output (const GldiWindowActor *actor, const CairoDock *pDock)
{
	if (!gldi_object_is_manager_child (GLDI_OBJECT(actor), &myWayfireWindowObjectMgr)) return FALSE;
	
	GldiWfWindowActor *wfactor = (GldiWfWindowActor*)actor;
	uint32_t dock_output = GPOINTER_TO_UINT (pDock->pVisibilityData);
	return (wfactor->output_id == dock_output);
}

/***********************************************************************
 * parse incoming messages, init
 ***********************************************************************/

static gboolean _socket_cb (G_GNUC_UNUSED GIOChannel *pSource, GIOCondition cond, G_GNUC_UNUSED gpointer data)
{
	gboolean bContinue = FALSE;
	
	if ((cond & G_IO_HUP) || (cond & G_IO_ERR))
		cd_warning ("Wayfire socket disconnected");	
	else if (cond & G_IO_IN)
	{
		// we should be able to read at least one complete message
		uint32_t len2 = 0;
		char* tmp2 = _read_msg (&len2);
		if (tmp2)
		{
			struct json_object *res = json_tokener_parse (tmp2);
			if (res)
			{
				bContinue = TRUE; // we could read a valid JSON, the connection is OK
				gboolean bResult = FALSE;
				
				struct cb_data *peek_cb = g_queue_peek_head (&s_cb_queue); // OK to call with an empty queue
				gboolean is_array = json_object_is_type (res, json_type_array);
				// events can arrive while we are waiting for a reply, they are not the reply
				gboolean is_event = !is_array && (json_object_object_get (res, "event") != NULL);
				gboolean is_list_cb = !is_event && peek_cb && (peek_cb->cb == _list_views_cb);
				gboolean is_conf_cb = !is_event && peek_cb && (peek_cb->cb == _got_configuration);
				if (is_list_cb)
				{
					// this is the reply to a list-views request, process it
					g_free (g_queue_pop_head (&s_cb_queue)); // we don't need the callback struct anymore
					if (is_array) _list_views_cb (res);
					else cd_warning ("Unexpected reply format to list-views request"); //!! TODO: cancel watching events !!
					
					json_object_put (res);
					g_free (tmp2);
					return TRUE;
				}
				else if (is_conf_cb)
				{
					// this is the reply to a wayfire/configuration request, process it
					g_free (g_queue_pop_head (&s_cb_queue)); // we don't need the callback struct anymore
					_got_configuration (res);
					json_object_put (res);
					g_free (tmp2);
					return TRUE;
				}
				
				struct json_object *event = json_object_object_get (res, "event");
				if (event)
				{
					// try to process an event that we are subscribed to
					const char *event_str = json_object_get_string (event);
					if (!event_str) cd_warning ("Cannot parse event name!");
					else
					{
						if (! strcmp (event_str, "command-binding"))
						{
							struct json_object *id_obj = json_object_object_get (res, "binding-id");
							if (id_obj)
							{
								errno = 0;
								uint64_t id = json_object_get_uint64 (id_obj);
								if (errno) cd_warning ("Cannot parse binding ID");
								else
								{
									GList *it = s_pBindings;
									while (it)
									{
										struct wf_keybinding *binding = (struct wf_keybinding*)it->data;
										if (binding->id == id)
										{
											// found
											gldi_object_notify (&myDesktopMgr, NOTIFICATION_SHORTKEY_PRESSED, binding->keycode, binding->modifiers);
											break;
										}
										it = it->next;
									}
									
									if (!it) cd_warning ("Binding event with unknown ID");
								}
							}
							else cd_warning ("Binding event without ID");
						}
						else
						{
							// likely a view related event
							_process_view (event_str, json_object_object_get (res, "view"));
						}
					}
				}
				else
				{
					// in this case, this is a reply to a request we sent before
					struct json_object *result = json_object_object_get (res, "result");
					if (result)
					{
						const char *value = json_object_get_string (result);
						if (value && !strcmp(value, "ok")) bResult = TRUE;
					}
					
					// consume the callback that belongs to this call
					if (g_queue_is_empty (&s_cb_queue))
						g_critical ("Missing callback in queue");
					else
					{
						struct cb_data *data = g_queue_pop_head (&s_cb_queue);
						if (data)
						{
							if (data->pBinding)
							{
								// result of registering a keybinding
								uint64_t id = 0;
								if (bResult)
								{
									struct json_object *id_obj = json_object_object_get (res, "binding-id");
									if (id_obj)
									{
										errno = 0;
										id = json_object_get_uint64 (id_obj);
										if (errno) bResult = FALSE;
									}
									else bResult = FALSE;
								}
								
								_binding_registered (bResult, data->pBinding, (CairoDockGrabKeyResult) data->cb, data->user_data, id);
							}
							else if (data->cb)
							{
								if (data->cb == _on_got_view_prop) _on_got_view_prop ((wf_view_prop_cb_data*) data->user_data, res, bResult);
								else ((CairoDockDesktopManagerActionResult) data->cb) (bResult, data->user_data);
							}
							g_free (data);
						}
					}
				}
				
				json_object_put (res);
			}
			else cd_warning ("Invalid JSON returned by Wayfire");
			free(tmp2);
		}
		// if we have no message, warning already shown, we will stop the connection
		// as bContinue == FALSE
	}
	
	if (!bContinue)
	{
		s_sidIO = 0;
		s_pIOChannel = NULL;
		g_queue_clear_full (&s_cb_queue, _cb_fail);
		g_list_free_full (s_pBindings, g_free);
		s_pBindings = NULL;
		return FALSE; // will also free our GIOChannel and close the socket
	}
	
	return TRUE;
}


static void _init_appid_tracking (void);

gboolean cd_init_wayfire_backend (struct wl_registry *registry) {
	g_queue_init (&s_cb_queue);
	
	const char* wf_socket = getenv("WAYFIRE_SOCKET");
	if (!wf_socket) wf_socket = default_socket;
	
	int wayfire_socket = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (wayfire_socket < 0) return FALSE;
	
	struct sockaddr_un sa;
	memset (&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy (sa.sun_path, wf_socket, sizeof (sa.sun_path) - 1);
	
	
	if (connect (wayfire_socket, (const struct sockaddr*)&sa, sizeof (sa))) {
		close (wayfire_socket);
		return FALSE;
	}
	
	GIOChannel *pIOChannel = g_io_channel_unix_new (wayfire_socket);
	if (!pIOChannel)
	{
		cd_warning ("Cannot create GIOChannel!");
		close (wayfire_socket);
		return FALSE;
	}
	
	g_io_channel_set_close_on_unref (pIOChannel, TRUE); // take ownership of wayfire_socket
	g_io_channel_set_encoding (pIOChannel, NULL, NULL); // otherwise GLib would try to validate received data as UTF-8
	g_io_channel_set_buffered (pIOChannel, FALSE); // we don't want GLib to block trying to read too much data
	
	// Check supported capabilities -- do this here so that we can register only
	// the supported functions.
	// Note: we need to do this synchronously, since we should register the desktop
	// manager backend before other components are initialized (e.g. keybindings).
	const char *msg = "{\"method\": \"list-methods\", \"data\": {}}";
	if (_send_msg_full (msg, strlen (msg), pIOChannel) < 0)
	{
		// warning already shown
		g_io_channel_unref (pIOChannel);
		return FALSE;
	}
	uint32_t len = 0;
	char *ret = _read_msg_full (&len, pIOChannel);
	if (!ret)
	{
		// warning already shown
		g_io_channel_unref (pIOChannel);
		return FALSE;
	}
	
	struct json_object *res = json_tokener_parse (ret);
	struct json_object *methods = json_object_object_get (res, "methods"); // should be an array
	if (! methods || ! json_object_is_type (methods, json_type_array))
	{
		cd_warning ("Error getting the list of available IPC methods!");
		g_io_channel_unref (pIOChannel);
		json_object_put (res);
		g_free (ret);
		return FALSE;
	}
	
	// we have a list of methods, need to check the expected ones
	GldiDesktopManagerBackend p;
	memset(&p, 0, sizeof (GldiDesktopManagerBackend));
	
	size_t n_methods = json_object_array_length (methods);
	size_t i;
	gboolean any_found = FALSE;
	gboolean watch_found = FALSE;
	gboolean list_found = FALSE;
	for (i = 0; i < n_methods; i++)
	{
		struct json_object *m = json_object_array_get_idx (methods, i);
		const char *value = json_object_get_string (m);
		if (! value) cd_warning ("Invalid json value among IPC methods!");
		else
		{
			if (! p.present_class && ! strcmp (value, "scale_ipc_filter/activate_appid"))
				{ p.present_class = _present_class; any_found = TRUE; }
			else if (! p.present_windows && ! strcmp (value, "scale/toggle"))
				{ p.present_windows = _present_windows; any_found = TRUE; }
			else if (! p.present_desktops && ! strcmp (value, "expo/toggle"))
				{ p.present_desktops = _present_desktops; any_found = TRUE; }
			else if (! p.show_hide_desktop && ! strcmp (value, "wm-actions/toggle_showdesktop"))
				{ p.show_hide_desktop = _show_hide_desktop; any_found = TRUE; }
			else if (! watch_found && ! strcmp (value, "window-rules/events/watch"))
				watch_found = TRUE;
			else if (! list_found && ! strcmp (value, "window-rules/list-views"))
				list_found = TRUE;
			else if (! s_bCanSticky && ! strcmp (value, "wm-actions/set-sticky"))
				s_bCanSticky = TRUE;
			else if (! s_bCanAbove && ! strcmp (value, "wm-actions/set-always-on-top"))
				s_bCanAbove = TRUE;
#ifdef HAVE_EVDEV
			else if (! p.grab_shortkey && ! strcmp (value, "command/register-binding"))
				{ p.grab_shortkey = _bind_key; any_found = TRUE; }
#endif
		}
	}
	
	json_object_put (res);
	g_free (ret);
	
	if (! (watch_found && list_found))
	{
		// need to be able to list and watch views to set these
		s_bCanSticky = FALSE;
		s_bCanAbove = FALSE;
	}
	
	gboolean bSuccess = FALSE;
	if (any_found || (watch_found && list_found))
	{
		s_sidIO = g_io_add_watch (pIOChannel, G_IO_IN | G_IO_HUP | G_IO_ERR, _socket_cb, NULL);
		if (s_sidIO)
		{
			s_pIOChannel = pIOChannel;
			if (watch_found && list_found)
			{
				GldiWindowManagerBackend wmb;
				memset (&wmb, 0, sizeof (GldiWindowManagerBackend));
				
				if (!gldi_wlr_foreign_toplevel_try_init_custom (registry, &wmb))
				{
					cd_warning ("Found Wayfire IPC socket, but no wlr-foreign-toplevel interface. Are you running in a nested session or have a stale WAYFIRE_SOCKET environment variable set?");
					g_source_remove (s_sidIO);
					g_io_channel_unref (pIOChannel);
					s_sidIO = 0;
					s_pIOChannel = NULL;
					return FALSE;
				}
				
				if (s_bCanAbove) wmb.set_above = gldi_wf_set_above;
				if (s_bCanSticky) wmb.set_sticky = gldi_wf_set_sticky;
				if (s_bCanAbove || s_bCanSticky)
				{
					wmb.is_above_or_below = gldi_wf_is_above_or_below;
					_init_window_maping ();
				}
				_init_appid_tracking ();
				wmb.get_supported_actions = _get_supported_actions;
				wmb.is_same_output = _dock_is_same_output;
				wmb.name = "Wayfire / wlr";
				
				gldi_windows_manager_register_backend (&wmb);
				gldi_wayland_wm_init (NULL, &myWayfireWindowObjectMgr);
				
				// extend the generic Wayland toplevel object manager
				memset (&myWayfireWindowObjectMgr, 0, sizeof (GldiObjectManager));
				myWayfireWindowObjectMgr.cName        = "wayfire-window-manager";
				myWayfireWindowObjectMgr.iObjectSize  = sizeof (GldiWfWindowActor);
				// interface
				myWayfireWindowObjectMgr.init_object  = NULL;
				myWayfireWindowObjectMgr.reset_object = _reset_object;
				// signals
				gldi_object_install_notifications (&myWayfireWindowObjectMgr, NB_NOTIFICATIONS_WAYFIRE_WINDOW_MANAGER);
				// parent object
				gldi_object_set_manager (GLDI_OBJECT (&myWayfireWindowObjectMgr), &myWaylandWMObjectMgr);
			}
			
			if (any_found) gldi_desktop_manager_register_backend (&p, "Wayfire");
			bSuccess = TRUE;
		}
		else cd_warning ("Cannot add socket IO event source!");
	}
	else cd_message ("Connected to Wayfire socket, but no methods available, not registering");
	
	g_io_channel_unref (pIOChannel); // note: ref taken by g_io_add_watch () if succesful
	return bSuccess;
}

#else // HAVE_JSON

void cd_init_wayfire_backend (void) {
	cd_message("Cairo-Dock was not built with Wayfire IPC support");
}

#endif // HAVE_JSON

static gboolean _wf_appid_changed (G_GNUC_UNUSED gpointer ptr, GldiWaylandWindowActor *wactor)
{
	if (!wactor) return GLDI_NOTIFICATION_LET_PASS;
	
	GldiWindowActor *actor = (GldiWindowActor*)wactor;
	
	// App ID format:
	// xdg-shell-app-id gtk-shell-app-id wf-ipc-id
	// cClassExtra already skipped the first part
	const char *cGTKAppID = wactor->cClassExtra;
	
	if (!cGTKAppID) return GLDI_NOTIFICATION_LET_PASS;
	while (*cGTKAppID == ' ') ++cGTKAppID;
	if (!*cGTKAppID) return GLDI_NOTIFICATION_LET_PASS;
	if (!strncmp (cGTKAppID, "wf-ipc-", 7)) return GLDI_NOTIFICATION_LET_PASS; // only IPC ID given
	
	// we have a valid string in cGTKAppID, potentially followed by a space and the IPC ID
	const char *tmp2 = strchr (cGTKAppID, ' ');
	size_t len = tmp2 ? (size_t)(tmp2 - cGTKAppID) : strlen(cGTKAppID); // will be > 0
	
	if (!actor->pDBusProps) actor->pDBusProps = g_new0 (GldiWindowDBusProperties, 1);
	else g_free (actor->pDBusProps->cGTKAppID);
	actor->pDBusProps->cGTKAppID = g_strndup (cGTKAppID, len);
	
	return GLDI_NOTIFICATION_LET_PASS;
}

static void _init_appid_tracking (void)
{
	gldi_object_register_notification (&myWaylandWMObjectMgr,
		NOTIFICATION_WAYLAND_APP_ID,
		(GldiNotificationFunc) _wf_appid_changed,
		GLDI_RUN_FIRST, NULL);
}

#else // HAVE_WAYLAND_PROTOCOLS

void cd_init_wayfire_backend (void) {
	cd_message("Cairo-Dock was not built with Wayfire IPC support");
}

#endif

