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

#include <stdlib.h>
#include <math.h>

#include <cairo.h>
#include <gtk/gtk.h>
#include <GL/gl.h>

#include "cairo-dock-icon-facility.h" // cairo_dock_compute_icon_area
#include "cairo-dock-icon-manager.h"  // myIconsParam
#include "cairo-dock-dock-facility.h" // cairo_dock_is_hidden
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-dock-priv.h"
#include "cairo-dock-style-manager.h" // gldi_style_colors_freeze
#include "cairo-dock-log.h"
#include "cairo-dock-config.h"
#include "cairo-dock-utils.h"  // cairo_dock_string_is_address
#include "cairo-dock-opengl-priv.h"
#include "cairo-dock-animations.h"  // cairo_dock_animation_will_be_visible
#include "cairo-dock-desktop-manager.h"  // gldi_desktop_get_width
// #include "cairo-dock-menu.h"  // gldi_menu_new
#include "cdwindow.h"
#define _MANAGER_DEF_
#include "cairo-dock-container-priv.h"

// public (manager, config, data)
GldiContainersParam myContainersParam;
GldiManager myContainersMgr;
GldiObjectManager myContainerObjectMgr;
GldiContainer *g_pPrimaryContainer = NULL;

// dependencies
extern CairoDockGLConfig g_openglConfig;
extern gboolean g_bUseOpenGL;
extern CairoDockHidingEffect *g_pHidingBackend;  // cairo_dock_is_hidden
extern CairoDock *g_pMainDock;  // for the default dock visibility when composite goes off->on

// private
static gboolean s_bSticky = TRUE;
static GldiContainerManagerBackend s_backend = {0};

void cairo_dock_set_containers_non_sticky (void)
{
	if (g_pPrimaryContainer != NULL)
	{
		cd_warning ("this function has to be called before any container is created.");
		return;
	}
	s_bSticky = FALSE;
}

inline void gldi_container_update_mouse_position (GldiContainer *pContainer)
{
	if (s_backend.update_mouse_position)
		s_backend.update_mouse_position (pContainer);
}

void gldi_container_handle_scroll (GtkEventControllerScroll *pCtrl, gdouble dx, gdouble dy, GldiContainer *pContainer, Icon *pIcon)
{
	GdkScrollUnit unit = gtk_event_controller_scroll_get_unit (pCtrl);
	switch (unit)
	{
		case GDK_SCROLL_UNIT_WHEEL: // discrete scroll events
			// filter out if the same event is delivered both as a "smooth" and "regular" event
			// see e.g. https://bugzilla.gnome.org/show_bug.cgi?id=726878 however it might not be relevant anymore
			if (gtk_event_controller_get_current_event_time (GTK_EVENT_CONTROLLER (pCtrl)) != pContainer->iLastScrollTime)
			{
				if (dy > 0.0) for( ; dy > 0.0; --dy)
					gldi_object_notify (pContainer, NOTIFICATION_SCROLL_ICON, pIcon, pContainer, GDK_SCROLL_DOWN, FALSE);
				else for( ; dy < 0.0; ++dy)
					gldi_object_notify (pContainer, NOTIFICATION_SCROLL_ICON, pIcon, pContainer, GDK_SCROLL_UP, FALSE);
			}
			break;
		case GDK_SCROLL_UNIT_SURFACE:
		{
			pContainer->iLastScrollTime = gtk_event_controller_get_current_event_time (GTK_EVENT_CONTROLLER (pCtrl));
			dx /= 120.0; //!! TODO: figure out a better conversion factor
			dy /= 120.0; // (or just report the real deltas)
			gldi_object_notify (pContainer, NOTIFICATION_SMOOTH_SCROLL_ICON, pIcon, pContainer, dx, dy);
			pContainer->fSmoothScrollAccum += dy;
			for (; pContainer->fSmoothScrollAccum > 1.0; pContainer->fSmoothScrollAccum -= 1.0)
				gldi_object_notify (pContainer, NOTIFICATION_SCROLL_ICON, pIcon, pContainer, GDK_SCROLL_DOWN, TRUE);
			for (; pContainer->fSmoothScrollAccum < -1.0; pContainer->fSmoothScrollAccum += 1.0)
				gldi_object_notify (pContainer, NOTIFICATION_SCROLL_ICON, pIcon, pContainer, GDK_SCROLL_UP, TRUE);
			break;
		}
		default:
			// GDK_SCROLL_LEFT and GDK_SCROLL_RIGHT are ignored
			break;
	}
}

static gboolean _prevent_delete (G_GNUC_UNUSED GtkWindow *pWidget, G_GNUC_UNUSED gpointer data)
{
	cd_debug ("No alt+f4");
	return TRUE;  // on empeche les ALT+F4 malheureux.
}

static gboolean _cairo_default_container_animation_loop (GldiContainer *pContainer)
{
	gboolean bContinue = FALSE;
	
	gboolean bUpdateSlowAnimation = FALSE;
	pContainer->iAnimationStep ++;
	if (pContainer->iAnimationStep * pContainer->iAnimationDeltaT >= CAIRO_DOCK_MIN_SLOW_DELTA_T)
	{
		bUpdateSlowAnimation = TRUE;
		pContainer->iAnimationStep = 0;
		pContainer->bKeepSlowAnimation = FALSE;
	}
	
	if (bUpdateSlowAnimation)
	{
		gldi_object_notify (pContainer, NOTIFICATION_UPDATE_SLOW, pContainer, &pContainer->bKeepSlowAnimation);
	}
	
	gldi_object_notify (pContainer, NOTIFICATION_UPDATE, pContainer, &bContinue);
	
	if (! bContinue && ! pContainer->bKeepSlowAnimation)
	{
		pContainer->iSidGLAnimation = 0;
		return FALSE;
	}
	else
		return TRUE;
}

void cairo_dock_redraw_container (GldiContainer *pContainer)
{
	g_return_if_fail (pContainer != NULL);
	GdkRectangle rect = {0, 0, pContainer->iWidth, pContainer->iHeight};
	if (! pContainer->bIsHorizontal)
	{
		rect.width = pContainer->iHeight;
		rect.height = pContainer->iWidth;
	}
	cairo_dock_redraw_container_area (pContainer, &rect);
}

static inline void _redraw_container_area (GldiContainer *pContainer, GdkRectangle *pArea)
{
	g_return_if_fail (pContainer != NULL);
	if (! gldi_container_is_visible (pContainer))
		return ;
	
	if (pArea->y < 0)
		pArea->y = 0;
	if (pContainer->bIsHorizontal && pArea->y + pArea->height > pContainer->iHeight)
		pArea->height = pContainer->iHeight - pArea->y;
	else if (! pContainer->bIsHorizontal && pArea->x + pArea->width > pContainer->iHeight)
		pArea->width = pContainer->iHeight - pArea->x;
	
	if (pArea->width > 0 && pArea->height > 0)
		gtk_widget_queue_draw (pContainer->pWidget);
}

void cairo_dock_redraw_container_area (GldiContainer *pContainer, GdkRectangle *pArea)
{
	if (CAIRO_DOCK_IS_DOCK (pContainer) && ! cairo_dock_animation_will_be_visible (CAIRO_DOCK (pContainer)))  // inutile de redessiner.
		return ;
	_redraw_container_area (pContainer, pArea);
}

void cairo_dock_redraw_icon (Icon *icon)
{
	g_return_if_fail (icon != NULL);
	GldiContainer *pContainer = cairo_dock_get_icon_container (icon);
	g_return_if_fail (pContainer != NULL);
	GdkRectangle rect;
	cairo_dock_compute_icon_area (icon, pContainer, &rect);
	
	if (CAIRO_DOCK_IS_DOCK (pContainer) &&
		( (cairo_dock_is_hidden (CAIRO_DOCK (pContainer)) && ! icon->bIsDemandingAttention && ! icon->bAlwaysVisible)
		|| (CAIRO_DOCK (pContainer)->iRefCount != 0 && ! gldi_container_is_visible (pContainer)) ) )  // inutile de redessiner.
		return ;
	_redraw_container_area (pContainer, &rect);
}


void cairo_dock_allow_widget_to_receive_data (GtkWidget *pWidget, GCallback pCallBack, gpointer data)
{
	// /*GtkTargetEntry pTargetEntry[6] = {0};
	// pTargetEntry[0].target = (gchar*)"text/*";
	/* pTargetEntry[0].flags = (GtkTargetFlags) 0;
	pTargetEntry[0].info = 0;
	pTargetEntry[1].target = (gchar*)"text/uri-list";
	pTargetEntry[2].target = (gchar*)"text/plain";
	pTargetEntry[3].target = (gchar*)"text/plain;charset=UTF-8";
	pTargetEntry[4].target = (gchar*)"text/directory";
	pTargetEntry[5].target = (gchar*)"text/html";
	gtk_drag_dest_set (pWidget,
		GTK_DEST_DEFAULT_DROP | GTK_DEST_DEFAULT_MOTION,  // GTK_DEST_DEFAULT_HIGHLIGHT ne rend pas joli je trouve.
		pTargetEntry,
		6,
		GDK_ACTION_COPY | GDK_ACTION_MOVE);  // le 'GDK_ACTION_MOVE' c'est pour KDE.*/
/*	gtk_drag_dest_set (pWidget,
		GTK_DEST_DEFAULT_DROP | GTK_DEST_DEFAULT_MOTION,  // GTK_DEST_DEFAULT_HIGHLIGHT ne rend pas joli je trouve.
		NULL,
		0,
		GDK_ACTION_COPY | GDK_ACTION_MOVE);  // le 'GDK_ACTION_MOVE' c'est pour KDE.
	
	GtkTargetList *targets = gtk_target_list_new (NULL, 0);
	gtk_target_list_add (targets, gldi_container_icon_dnd_atom (), GTK_TARGET_SAME_APP, 0);
	gtk_target_list_add_text_targets (targets, 0);
	gtk_target_list_add_uri_targets (targets, 0);
	gtk_drag_dest_set_target_list (pWidget, targets);
	gtk_target_list_unref (targets); // above function should take ref
	
	g_signal_connect (G_OBJECT (pWidget),
		"drag_data_received",
		pCallBack,
		data);*/
}

void gldi_container_disable_drop (GldiContainer *pContainer)
{
	// gtk_drag_dest_set_target_list (pContainer->pWidget, NULL);
}

/* TODO: define our own GdkContentFormats ??
GdkAtom gldi_container_icon_dnd_atom (void)
{
	if (s_dnd_atom == GDK_NONE)
		s_dnd_atom = gdk_atom_intern_static_string ("cairo-dock/icon");
	return s_dnd_atom;
}
*/

void gldi_container_notify_drop_data (GldiContainer *pContainer, gchar *cReceivedData, Icon *pPointedIcon, double fOrder)
{
	g_return_if_fail (cReceivedData != NULL);
	gchar *cData = NULL;
	
	gchar **cStringList = g_strsplit (cReceivedData, "\n", -1);
	GString *sArg = g_string_new ("");
	int i=0, j;
	while (cStringList[i] != NULL)
	{
		g_string_assign (sArg, cStringList[i]);
		
		if (! cairo_dock_string_is_address (cStringList[i]))
		{
			j = i + 1;
			while (cStringList[j] != NULL)
			{
				if (cairo_dock_string_is_address (cStringList[j]))
					break ;
				g_string_append_printf (sArg, "\n%s", cStringList[j]);
				j ++;
			}
			i = j;
		}
		else
		{
			cd_debug (" + adresse");
			if (sArg->str[sArg->len-1] == '\r')
			{
				cd_debug ("retour charriot");
				sArg->str[sArg->len-1] = '\0';
			}
			i ++;
		}
		
		cData = sArg->str;
		cd_debug (" notification de drop '%s'", cData);
		gldi_object_notify (pContainer, NOTIFICATION_DROP_DATA, cData, pPointedIcon, fOrder, pContainer);
	}
	
	g_strfreev (cStringList);
	g_string_free (sArg, TRUE);
}


void gldi_container_reserve_space (GldiContainer *pContainer, int left, int right, int top, int bottom, int left_start_y, int left_end_y, int right_start_y, int right_end_y, int top_start_x, int top_end_x, int bottom_start_x, int bottom_end_x)
{
	if (s_backend.reserve_space)
		s_backend.reserve_space (pContainer, left, right, top, bottom, left_start_y, left_end_y, right_start_y, right_end_y, top_start_x, top_end_x, bottom_start_x, bottom_end_x);
}

int gldi_container_get_current_desktop_index (GldiContainer *pContainer)
{
	if (s_backend.get_current_desktop_index)
		return s_backend.get_current_desktop_index (pContainer);
	return 0;
}

void gldi_container_move (GldiContainer *pContainer, int iNumDesktop, int iAbsolutePositionX, int iAbsolutePositionY)
{
	if (s_backend.move)
		s_backend.move (pContainer, iNumDesktop, iAbsolutePositionX, iAbsolutePositionY);
}

void gldi_container_set_screen (GldiContainer* pContainer, int iNumScreen)
{
	if (s_backend.set_monitor)
		s_backend.set_monitor (pContainer, iNumScreen);
}

void gldi_container_calculate_rect (const GldiContainer* pContainer, const Icon* pPointedIcon,
	GdkRectangle *rect, GtkPositionType *pos, gboolean bSkipLabel)
{
	if (!pContainer) return;

	if (pContainer->bIsHorizontal)
	{
		if (pPointedIcon)
		{
			rect->x = pPointedIcon->fDrawX;
			rect->y = pPointedIcon->fDrawY;
			rect->width = pPointedIcon->fWidth * pPointedIcon->fScale;
			rect->height = pPointedIcon->fHeight * pPointedIcon->fScale;
		}
		else
		{
			rect->x = pContainer->iWidth / 2;
			rect->y = 0;
			rect->width = 1;
			rect->height = pContainer->iHeight;
		}
		if (pContainer->bDirectionUp)
		{
			*pos = GTK_POS_BOTTOM;
			if (pPointedIcon && bSkipLabel) rect->y -= myIconsParam.iLabelSize;
		}
		else
		{
			*pos = GTK_POS_TOP;
			if (pPointedIcon && bSkipLabel) rect->y += myIconsParam.iLabelSize;
		}
	}
	else
	{
		if (pPointedIcon)
		{
			rect->x = pPointedIcon->fDrawY;
			rect->y = pPointedIcon->fDrawX;
			rect->width = pPointedIcon->fHeight * pPointedIcon->fScale;
			rect->height = pPointedIcon->fWidth * pPointedIcon->fScale;
		}
		else
		{
			rect->x = 0;
			rect->y = pContainer->iWidth / 2;
			rect->width = pContainer->iHeight;
			rect->height = 1;
		}
		if (pContainer->bDirectionUp)
			*pos = GTK_POS_RIGHT;
		else *pos = GTK_POS_LEFT;
	}
}

void gldi_container_calculate_aimed_point_base (int w, int h, int iMarginPosition,
	gdouble fAlign, int *iAimedX, int *iAimedY)
{
	switch (iMarginPosition)
	{
		case 0:
			// bottom
			*iAimedX = w * fAlign;
			*iAimedY = h;
			break;
		case 1:
			// top
			*iAimedX = w * fAlign;
			*iAimedY = 0;
			break;
		case 2:
			// right
			*iAimedX = w;
			*iAimedY = h * fAlign;
			break;
		case 3:
			// left
			*iAimedX = 0;
			*iAimedY = h * fAlign;
			break;
	}
}

void gldi_container_calculate_aimed_point (const Icon *pIcon, GtkWidget *pWidget, int w, int h,
	int iMarginPosition, gdouble fAlign, int *iAimedX, int *iAimedY)
{
	GldiContainer *pContainer = (pIcon ? cairo_dock_get_icon_container (pIcon) : NULL);
	if (pIcon && pContainer && CAIRO_DOCK_IS_DOCK (pContainer))
	{
		// if we have a dock, the position is relative to it
		int x0 = pIcon->fDrawX + pIcon->fWidth * pIcon->fScale/2;
		
		CairoDock *pDock = CAIRO_DOCK (pContainer);
		int y0, dy;
		if (pDock->iInputState == CAIRO_DOCK_INPUT_ACTIVE)
			dy = pContainer->iHeight - pDock->iActiveHeight;
		else if (cairo_dock_is_hidden (pDock))
			dy = pContainer->iHeight-1;  // on laisse 1 pixels pour pouvoir sortir du dialogue avant de toucher le bord de l'ecran, et ainsi le faire se replacer, lorsqu'on fait apparaitre un dock en auto-hide.
		else
			dy = pContainer->iHeight - pDock->iMinDockHeight;
		if (pContainer->bDirectionUp)
			y0 = dy;
		else y0 = pContainer->iHeight - dy;
		
		if (iMarginPosition == 0 || iMarginPosition == 1)
		{
			*iAimedX = x0;
			*iAimedY = y0;
		}
		else
		{
			*iAimedX = y0;
			*iAimedY = x0;
		}
	}
	else {
		// default: aimed point is in the middle of the selected edge,
		// it is calculated relative to or position
		gldi_container_calculate_aimed_point_base (w, h, iMarginPosition, fAlign, iAimedX, iAimedY);
	}
	
	if (s_backend.adjust_aimed_point)
		s_backend.adjust_aimed_point (pIcon, pWidget, w, h, iMarginPosition, fAlign, iAimedX, iAimedY);
	
	// g_print ("aimed point: %d, %d\n", *iAimedX, *iAimedY);
}


gboolean gldi_container_is_active (GldiContainer *pContainer)
{
	if (s_backend.is_active)
		return s_backend.is_active (pContainer);
	return FALSE;
}

void gldi_container_present (GldiContainer *pContainer)
{
	if (s_backend.present)
		s_backend.present (pContainer);
}

void gldi_container_init_layer (GldiContainer *pContainer, const gchar *cNamespace)
{
	if (s_backend.init_layer)
		s_backend.init_layer (pContainer, cNamespace);
}

void gldi_container_move_resize_dock (CairoDock *pDock)
{
	if (s_backend.move_resize_dock)
		s_backend.move_resize_dock (pDock);
}

gboolean gldi_container_is_wayland_backend ()
{
	if (s_backend.is_wayland)
		return s_backend.is_wayland ();
	return FALSE;
}

void gldi_container_set_keep_below (GldiContainer *pContainer, gboolean bKeepBelow)
{
	if (s_backend.set_keep_below)
		s_backend.set_keep_below (pContainer, bKeepBelow);
}

void gldi_container_set_input_shape(GldiContainer *pContainer, cairo_region_t *pShape)
{
	GdkSurface *pSurface = gldi_container_get_gdk_window (pContainer);
	if (pSurface) gdk_surface_set_input_region (pSurface, pShape);
	if (s_backend.set_input_shape)
		s_backend.set_input_shape (pContainer, pShape);
}

void gldi_container_update_polling_screen_edge (void)
{
	if (s_backend.update_polling_screen_edge)
		s_backend.update_polling_screen_edge ();
}

gboolean gldi_container_can_poll_screen_edge (void)
{
	return (s_backend.update_polling_screen_edge != NULL);
}

gboolean gldi_container_can_reserve_space (int iNumScreen, gboolean bDirectionUp, gboolean bIsHorizontal)
{
	if (s_backend.can_reserve_space)
		return s_backend.can_reserve_space (iNumScreen, bDirectionUp, bIsHorizontal);
	return TRUE;
}

gboolean gldi_container_dock_handle_leave (CairoDock *pDock, gboolean bRealEvent)
{
	gboolean ret = TRUE; // default return value is true -- it means there is no need for further checks
	if (s_backend.dock_handle_leave)
		ret = s_backend.dock_handle_leave (pDock, bRealEvent);
	if (ret) pDock->container.fSmoothScrollAccum = 0.0; // reset scroll events
	return ret;
}

void gldi_container_dock_check_if_mouse_inside_linear (CairoDock *pDock)
{
	if (s_backend.dock_check_if_mouse_inside_linear)
		s_backend.dock_check_if_mouse_inside_linear (pDock);
}

void gldi_container_manager_register_backend (GldiContainerManagerBackend *pBackend)
{
	gpointer *ptr = (gpointer*)&s_backend;
	gpointer *src = (gpointer*)pBackend;
	gpointer *src_end = (gpointer*)(pBackend + 1);
	while (src != src_end)
	{
		if (*src != NULL)
			*ptr = *src;
		src ++;
		ptr ++;
	}
}


// static GtkWidget *s_pMenu = NULL;  // right-click menu
GtkWidget *gldi_container_build_menu (GldiContainer *pContainer, Icon *icon)
{
	cd_warning ("menus disabled");
	return NULL;
/*	if (s_pMenu != NULL)
	{
		//g_print ("previous menu still alive\n");
		gtk_widget_destroy (GTK_WIDGET (s_pMenu));  // -> 's_pMenu' becomes NULL thanks to the weak pointer.
	}
	g_return_val_if_fail (pContainer != NULL, NULL);
	
	//\_________________________ On construit le menu.
	GtkWidget *menu = gldi_menu_new (icon);
	
	//\_________________________ On passe la main a ceux qui veulent y rajouter des choses.
	gboolean bDiscardMenu = FALSE;
	gldi_object_notify (pContainer, NOTIFICATION_BUILD_CONTAINER_MENU, icon, pContainer, menu, &bDiscardMenu);
	if (bDiscardMenu)
	{
		gtk_widget_destroy (menu);
		return NULL;
	}
	
	gldi_object_notify (pContainer, NOTIFICATION_BUILD_ICON_MENU, icon, pContainer, menu);
	
	s_pMenu = menu;
	g_object_add_weak_pointer (G_OBJECT (menu), (gpointer*)&s_pMenu);  // will nullify 's_pMenu' as soon as the menu is destroyed.
	// TODO: it would make sense to destroy the menu as soon as it is closed, since it will not be reused
	return menu; */
}


cairo_region_t *gldi_container_create_input_shape (GldiContainer *pContainer, int x, int y, int w, int h)
{
	if (pContainer->iWidth == 0 || pContainer->iHeight == 0)  // very unlikely to happen, but anyway avoid this case.
		return NULL;

	cairo_rectangle_int_t rect = {x, y, w, h};
	cairo_region_t *pShapeBitmap = cairo_region_create_rectangle (&rect);  // for a more complex shape, we would need to draw it on a cairo_surface_t, and then make it a region with gdk_cairo_region_from_surface().

	return pShapeBitmap;
}

  //////////////////
 /// GET CONFIG ///
//////////////////

static gboolean get_config (GKeyFile *pKeyFile, GldiContainersParam *pContainersParam)
{
	gboolean bFlushConfFileNeeded = FALSE;
	
	int iRefreshFrequency = cairo_dock_get_integer_key_value (pKeyFile, "System", "opengl anim freq", &bFlushConfFileNeeded, 33, NULL, NULL);
	pContainersParam->iGLAnimationDeltaT = 1000. / iRefreshFrequency;
	
	iRefreshFrequency = cairo_dock_get_integer_key_value (pKeyFile, "System", "cairo anim freq", &bFlushConfFileNeeded, 25, NULL, NULL);
	pContainersParam->iCairoAnimationDeltaT = 1000. / iRefreshFrequency;
	
	return bFlushConfFileNeeded;
}

gboolean gldi_container_get_scale_setting (GKeyFile *pKeyFile, double *fScale, gboolean *bFlushConfFileNeeded)
{
	int iUIScaleBackend = cairo_dock_get_integer_key_value (pKeyFile, "System", "ui scale backend", bFlushConfFileNeeded, 2, NULL, NULL);
	gboolean bScale = FALSE;
	switch (iUIScaleBackend)
	{
		case 2: // both
			bScale = TRUE;
			break;
		case 1: // Wayland
			bScale = gldi_container_is_wayland_backend ();
			break;
		case 0: // X11
			bScale = !gldi_container_is_wayland_backend ();
			break;
	}
	if (bScale)
		*fScale = cairo_dock_get_double_key_value (pKeyFile, "System", "ui scale", bFlushConfFileNeeded, 1., NULL, NULL);
	
	return bScale;
}

  ////////////
 /// INIT ///
////////////

static void init (void)
{
	static GtkCssProvider *cssProvider = NULL;
	if (cssProvider) return;
	
	cssProvider = gtk_css_provider_new ();
	gldi_style_colors_freeze ();
	gtk_style_context_add_provider_for_display (gdk_display_get_default(), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
	const gchar *css = "\
		window.cairo-dock {\
			background-color: rgba(0, 0, 0, 0.0);\
		}\
		popover.background {\
			background-color: rgba(0, 0, 0, 0.0);\
		}\
		popover.cairo-dock contents {\
			box-shadow: 0 0 0 0;\
			background-color: rgba(0, 0, 0, 0.0);\
			border: none;\
			border-radius: 0 0 0 0;\
			padding: 0 0 0 0;\
		}\
		";
	gtk_css_provider_load_from_string (cssProvider, css);
	gldi_style_colors_freeze ();
}

  ///////////////
 /// MANAGER ///
///////////////

static void init_object (GldiObject *obj, gpointer attr)
{
	GldiContainer *pContainer = (GldiContainer*)obj;
	GldiContainerAttr *cattr = (GldiContainerAttr*)attr;
	
	pContainer->iface.animation_loop = _cairo_default_container_animation_loop;
	pContainer->fRatio = 1;
	pContainer->bIsHorizontal = TRUE;
	pContainer->bDirectionUp = TRUE;
	
	// create a window
	GtkWidget* pWindow = cattr->bIsPopup ? GTK_WIDGET(cd_popup_new ()) : GTK_WIDGET(cd_window_new ());
	pContainer->pWidget = pWindow;
	// gtk_window_set_default_size (pWindow, 1, 1);  // this should prevent having grey rectangles during the loading, when the window is mapped and rendered by the WM but not yet by us.
	// gtk_window_resize (pWindow, 1, 1);
	// gtk_window_set_skip_pager_hint (pWindow, TRUE); TODO: move to X11 backend: gdk_x11_surface_set_skip_pager_hint
	// gtk_window_set_skip_taskbar_hint (pWindow, TRUE); -> gdk_x11_surface_set_skip_taskbar_hint
	
	//!! TODO: sticky on X11
	
	if (! cattr->bIsPopup)
		g_signal_connect (G_OBJECT (pWindow),
			"close-request",
			G_CALLBACK (_prevent_delete),
			NULL);
	pContainer->iWidth = 1;
	pContainer->iHeight = 1;
	
	if (g_bUseOpenGL && ! cattr->bNoOpengl) //!! TODO: add a GtkGLArea !!
		pContainer->iAnimationDeltaT = myContainersParam.iGLAnimationDeltaT;
	else
		pContainer->iAnimationDeltaT = myContainersParam.iCairoAnimationDeltaT;
	if (pContainer->iAnimationDeltaT == 0)
		pContainer->iAnimationDeltaT = 30;

	// make it the primary container if it's the first
	if (g_pPrimaryContainer == NULL)
		g_pPrimaryContainer = pContainer;
}

static void reset_object (GldiObject *obj)
{
	GldiContainer *pContainer = (GldiContainer*)obj;
	
	// destroy the window (will remove all signals)
	if (GTK_IS_WINDOW (pContainer->pWidget))
		gtk_window_destroy (GTK_WINDOW (pContainer->pWidget));
	else if (GTK_IS_POPOVER (pContainer->pWidget))
		gtk_widget_unparent (pContainer->pWidget); // will destroy it
	pContainer->pWidget = NULL;
	
	// stop the animation loop
	if (pContainer->iSidGLAnimation != 0)
	{
		g_source_remove (pContainer->iSidGLAnimation);
		pContainer->iSidGLAnimation = 0;
	}
	
	if (g_pPrimaryContainer == pContainer)
		g_pPrimaryContainer = NULL;
	
	if (pContainer->pMoveToRect)
		free(pContainer->pMoveToRect);
}

void gldi_register_containers_manager (void)
{
	// Manager
	memset (&myContainersMgr, 0, sizeof (GldiManager));
	gldi_object_init (GLDI_OBJECT(&myContainersMgr), &myManagerObjectMgr, NULL);
	myContainersMgr.cModuleName  = "Containers";
	// interface
	myContainersMgr.init         = init;
	// myContainersMgr.load         = load;
	// myContainersMgr.unload       = unload;
	myContainersMgr.reload       = (GldiManagerReloadFunc)NULL;
	myContainersMgr.get_config   = (GldiManagerGetConfigFunc)get_config;
	myContainersMgr.reset_config = (GldiManagerResetConfigFunc)NULL;
	// Config
	myContainersMgr.pConfig = (GldiManagerConfigPtr)&myContainersParam;
	myContainersMgr.iSizeOfConfig = sizeof (GldiContainersParam);
	// data
	myContainersMgr.pData = (GldiManagerDataPtr)NULL;
	myContainersMgr.iSizeOfData = 0;
	
	// Object Manager
	memset (&myContainerObjectMgr, 0, sizeof (GldiObjectManager));
	myContainerObjectMgr.cName        = "Container";
	myContainerObjectMgr.iObjectSize  = sizeof (GldiContainer);
	// interface
	myContainerObjectMgr.init_object  = init_object;
	myContainerObjectMgr.reset_object = reset_object;
	// signals
	gldi_object_install_notifications (&myContainerObjectMgr, NB_NOTIFICATIONS_CONTAINER);
}
