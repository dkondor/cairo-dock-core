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
#include <math.h>  // fabs

#include <cairo.h>
#include <gtk/gtk.h>

#include "cairo-dock-container-priv.h"
#include "cairo-dock-icon-factory.h"
#include "cairo-dock-icon-facility.h"  // cairo_dock_get_icon_container
#include "cairo-dock-desktop-manager.h"  // gldi_desktop_get_height
#include "cairo-dock-log.h"
#include "cairo-dock-draw.h"
#include "cairo-dock-backends-manager.h"  // cairo_dock_get_dialog_decorator
#include "cairo-dock-dialog-manager.h"  // myDialogsParam
#include "cairo-dock-dialog-factory.h"  // myDialogsParam
#include "cairo-dock-style-manager.h"
#include "cairo-dock-menu.h"
#include "cairo-dock-wayland-manager.h"
#include "cdwindow.h"

extern gchar *g_cCurrentThemePath;
extern GldiContainer *g_pPrimaryContainer;

// static gboolean _draw_menu_item (GtkWidget *widget, cairo_t *cr, G_GNUC_UNUSED gpointer data);

  ////////////
 /// MENU ///
/////////////

static void _draw_menu (GtkWidget *pWidget,
	cairo_t *pCairoContext,
	G_GNUC_UNUSED GtkWidget *menu)
{
	// reset the clip set by GTK, to allow us draw in the margin of the widget
	// cairo_reset_clip(pCairoContext); -- not needed
	
	// erase the default background
	cairo_dock_erase_cairo_context (pCairoContext); // -- not needed, we get a new context each time
	
	// draw the background/outline and set the clip
	CairoDialogDecorator *pDecorator = cairo_dock_get_dialog_decorator (myDialogsParam.cDecoratorName);
	if (pDecorator)
		pDecorator->render_menu (pWidget, pCairoContext);
	else
	{
		if (myDialogsParam.bUseDefaultColors)
			gldi_style_colors_set_bg_color_full (pCairoContext, FALSE);
		else
			gldi_color_set_cairo_rgb (pCairoContext, &myDialogsParam.fBgColor);
		cairo_paint (pCairoContext);
	}
	
	// draw the items
	// cairo_set_source_rgba (pCairoContext, 0.0, 0.0, 0.0, 1.0); -- not needed? (snapshot() will use different surfaces)
	/* -- done by our snapshot() implementation
	GtkWidgetClass *parent_class = g_type_class_peek (g_type_parent (G_TYPE_FROM_INSTANCE (pWidget)));
	parent_class = g_type_class_peek_parent (parent_class);  // skip the direct parent (GtkBin, which does anyway nothing usually), because dbusmenu-gtk draws it
	parent_class->draw (pWidget, pCairoContext);
	*/
}

static void _set_margin_position (GtkWidget *pMenu, GldiMenuParams *pParams)
{
	if (pParams == NULL)
		pParams = g_object_get_data (G_OBJECT (pMenu), "gldi-params");
	g_return_if_fail (pParams);
	Icon *pIcon = pParams->pIcon;
	g_return_if_fail (pIcon);
	GldiContainer *pContainer = cairo_dock_get_icon_container (pIcon);
	g_return_if_fail (pContainer);
	
	// define where the menu will point
	int iMarginPosition;  // b, t, r, l
	
	if (pContainer->bIsHorizontal)
	{
		if (pContainer->bDirectionUp) iMarginPosition = 0;
		else iMarginPosition = 1;
	}
	else
	{
		if (pContainer->bDirectionUp) iMarginPosition = 2;
		else iMarginPosition = 3;
	}
	
	// store the result, and allocate some space to draw the arrow
	if (iMarginPosition != pParams->iMarginPosition)  // margin position is now defined or has changed -> update it on the menu
	{
		// store the value
		pParams->iMarginPosition = iMarginPosition;
		
		// get/add a css -- note: this does not work in GTK4, but see the original note:
		// actually gtk_widget_set_margin_xxx works, but then GTK adds a translation to the cairo_context, forcing each renderer to offset its drawing by gtk_widget_get_margin_xxx()
		// also, gtk_widget_get_allocation() doesn't take into account the margin, forcing each renderer to add it
		// so in the end it's better not to use it
		// -> cairo_context is not a problem, it is created by us, but the allocation can be?
		
		
		int ah = pParams->iArrowHeight;
		int b=0, t=0, r=0, l=0;
		switch (iMarginPosition)
		{
			case 0: b = ah; break;
			case 1: t = ah; break;
			case 2: r = ah; break;
			case 3: l = ah; break;
			default: break;
		}
		
		gtk_widget_set_margin_start (pMenu, l);
		gtk_widget_set_margin_top (pMenu, t);
		gtk_widget_set_margin_end (pMenu, r);
		gtk_widget_set_margin_bottom (pMenu, b);
	}
}

GtkWidget *gldi_menu_new (Icon *pIcon)
{
	GtkWidget *pMenu = GTK_WIDGET(cd_menu_new ());
	
	gldi_menu_init (pMenu, pIcon);
	
	return pMenu;
}

static gboolean _on_icon_destroyed (GtkWidget *pMenu, G_GNUC_UNUSED Icon *pIcon)
{
	GldiMenuParams *pParams = g_object_get_data (G_OBJECT (pMenu), "gldi-params");
	if (pParams)
		pParams->pIcon = NULL;
	// no sense in keeping the menu (and it might hold more stale references to pIcon)
	gtk_widget_unparent (pMenu);
	return GLDI_NOTIFICATION_LET_PASS;
}

static void _on_menu_destroyed (GtkWidget *pMenu, G_GNUC_UNUSED gpointer data)
{
	/* Steal data: with GTK 3.14, we receive two 'popup' signals and for the
	 * second one, a 'destroy' signal has already been sent! Then, 'pParams'
	 * will not be correct... https://bugzilla.gnome.org/738537
	 */
	GldiMenuParams *pParams = g_object_steal_data (G_OBJECT (pMenu), "gldi-params");
	if (!pParams)
		return;

	Icon *pIcon = pParams->pIcon;
	if (pIcon)
		gldi_object_remove_notification (pIcon,
			NOTIFICATION_DESTROY,
			(GldiNotificationFunc) _on_icon_destroyed,
			pMenu);
	g_free (pParams);
}

static void _on_menu_deactivated (GtkWidget *pMenu, G_GNUC_UNUSED gpointer data)
{
	GldiMenuParams *pParams = g_object_get_data (G_OBJECT (pMenu), "gldi-params");
	if (!pParams)
		return;
	Icon *pIcon = pParams->pIcon;
	gtk_widget_unparent (pMenu); // will destroy pMenu, since we should not have any other reference to it
	if (!pIcon) return;
	GldiContainer *pContainer = cairo_dock_get_icon_container (pIcon);
	if (pIcon->iHideLabel > 0)
	{
		pIcon->iHideLabel --;
		if (pIcon->iHideLabel == 0 && pContainer)
			gtk_widget_queue_draw (pContainer->pWidget);
	}
	// no need to test if we're running on Wayland, if not, this is a no-op
	if (pContainer) gldi_wayland_release_keyboard (pContainer, GLDI_KEYBOARD_RELEASE_MENU_CLOSED);
}

static void _menu_realized_cb (GtkWidget *widget, gpointer user_data);

void gldi_menu_init (GtkWidget *pMenu, Icon *pIcon)
{
	g_return_if_fail (g_object_get_data (G_OBJECT (pMenu), "gldi-params") == NULL);
	
	g_signal_connect (G_OBJECT (pMenu),
		"draw",
		G_CALLBACK (_draw_menu),
		pMenu);

	gtk_widget_add_css_class (pMenu, "gldimenu");

	// set params
	GldiMenuParams *pParams = g_new0 (GldiMenuParams, 1);
	g_object_set_data (G_OBJECT (pMenu), "gldi-params", pParams);
	g_signal_connect (G_OBJECT (pMenu), // TODO: do we need this? / do we have this?
		"destroy",
		G_CALLBACK (_on_menu_destroyed),
		NULL);
	
	// Handle any adjustments necessary when the menu is first shown.
	// This is a bit hacky: if we have an icon (so we are the first-level menu), we connect to the "realized" signal,
	// which will allow us to resize the menu before it is positioned, leading to better results if it needs to be moved out
	// of the way from the dock it's pointing at. For submenus, we connect to the "map" signal, to better handle it
	// opening and closing multiple times.
	if (pIcon) g_signal_connect (G_OBJECT (pMenu), "realize", G_CALLBACK (_menu_realized_cb), pParams);
	else g_signal_connect (G_OBJECT (pMenu), "map", G_CALLBACK (_menu_realized_cb), pParams);
	
	// init a main menu
	if (pIcon != NULL)  // the menu points on an icon
	{
		// link it to the icon
		g_object_set_data (G_OBJECT (pMenu), "gldi-icon", pIcon);
		pParams->pIcon = pIcon;
		gldi_object_register_notification (pIcon,
			NOTIFICATION_DESTROY,
			(GldiNotificationFunc) _on_icon_destroyed,
			GLDI_RUN_AFTER, pMenu);  // when the icon is destroyed, unlink the menu from it; when the menu is destroyed, the above notification will be unregistered on the icon in the "destroy" callback
		
		GldiContainer *pContainer = cairo_dock_get_icon_container (pIcon);
		if (pContainer != NULL)
		{
			// init the rendering --> align, margin-height
			CairoDialogDecorator *pDecorator = cairo_dock_get_dialog_decorator (myDialogsParam.cDecoratorName);
			if (pDecorator)
				pDecorator->setup_menu (pMenu);
			
			// define where the menu will point, and allocate some space to draw the arrow
			pParams->iMarginPosition = -1;
			_set_margin_position (pMenu, pParams);
			
			// show the icon's label back when the menu is hidden and destroy the menu
			g_signal_connect (G_OBJECT (pMenu),
				"closed",
				G_CALLBACK (_on_menu_deactivated),
				NULL);
			
			// set transient for (parent relationship; needed for positioning)
			// note: it is an error to try to map (and position) a popup
			// relative to a window that is not mapped; we need to take care of this
			GtkWidget *tmp = pContainer->pWidget;
			while (tmp && !gtk_widget_get_mapped (tmp))
				tmp = gtk_widget_get_parent (tmp);
			gtk_widget_set_parent (pMenu, tmp);
		}
	}
}

void gldi_menu_reinit (GtkWidget *pMenu, Icon *pIcon)
{
	GldiMenuParams *pParams = g_object_get_data (G_OBJECT (pMenu), "gldi-params");
	if (! pParams)
	{
		gldi_menu_init (pMenu, pIcon);
		return;
	}
	
	Icon *pOldIcon = pParams->pIcon;
	if (pOldIcon)
		gldi_object_remove_notification (pOldIcon,
			NOTIFICATION_DESTROY,
			(GldiNotificationFunc) _on_icon_destroyed,
			pMenu);
	
	//!! TODO: do we need to unparent pMenu??
	
	// link the menu to the new icon
	g_object_set_data (G_OBJECT (pMenu), "gldi-icon", pIcon);
	pParams->pIcon = pIcon;
	if (pIcon)
	{
		gldi_object_register_notification (pIcon,
			NOTIFICATION_DESTROY,
			(GldiNotificationFunc) _on_icon_destroyed,
			GLDI_RUN_AFTER, pMenu);  // when the icon is destroyed, unlink the menu from it; when the menu is destroyed, the above notification will be unregistered on the icon in the "destroy" callback
		
		// set new parent
		// note: it is an error to try to map (and position) a popup
		// relative to a window that is not mapped; we need to take care of this
		GldiContainer *pContainer = cairo_dock_get_icon_container (pIcon);
		if (pContainer)
		{
			GtkWidget *tmp = pContainer->pWidget;
			while (tmp && !gtk_widget_get_mapped (tmp))
				tmp = gtk_widget_get_parent (tmp);
			gtk_widget_set_parent (pMenu, tmp);
		}
	}
}


static void _menu_realized_cb (GtkWidget *widget, gpointer user_data)
{
	GldiMenuParams *pParams = (GldiMenuParams*)user_data;
	g_return_if_fail (pParams != NULL);
	
	int w, h;  // taille menu
	GtkRequisition requisition;
	gtk_widget_get_preferred_size (widget, NULL, &requisition);  // retrieve the natural size; Note: before gtk3.10 we used the minimum size but it's now incorrect; the natural size works for prior versions too.
	w = requisition.width;
	h = requisition.height;
	
	/** TODO: is this still needed?
	// constrain the menu's size to fit within the screen
	// see https://github.com/wmww/gtk-layer-shell/issues/148
	// (note: menus created here are always ultimately a child of a layer-shell window)
	// solution used here is originally by LBCrion (under GPL3):
	// https://github.com/LBCrion/sfwbar/commit/760e68ef50c540a55c13791876f853d358b4dcaa
	if (gldi_wayland_manager_have_layer_shell ())
	{
		GdkRectangle area;
		GdkWindow *window = gtk_widget_get_window (gtk_widget_get_toplevel (widget));
		if (window)
		{
			GdkDisplay *dsp = gdk_window_get_display (window);
			GdkMonitor *mon = gdk_display_get_monitor_at_window (dsp, window);
			gdk_monitor_get_workarea (mon, &area);
			gboolean bResize = FALSE;
			if (w > area.width)
			{
				w = area.width;
				bResize = TRUE;
			}
			if (h > area.height)
			{
				h = area.height;
				bResize = TRUE;
			}
			if (bResize) gdk_window_resize (window, w, h);
		}
		else cd_warning ("menu has no associated GdkWindow!");
	}
	*/
	
	if (pParams->pIcon)
		gldi_container_calculate_aimed_point (pParams->pIcon, widget, w, h, pParams->iMarginPosition,
			pParams->fAlign, &(pParams->iAimedX), &(pParams->iAimedY));
}
/*
static void _init_menu_item (GtkWidget *pMenuItem);
static void _init_menu_item2 (GtkWidget *menu, G_GNUC_UNUSED gpointer dummy)
{
	_init_menu_item (menu);
}
*/
static void _init_menu_item (GtkWidget *pMenuItem)
{
	(void)*pMenuItem;
	return; // not needed
	
	//!! TODO: submenus not supported yet
	// GtkWidget *pSubMenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (pMenuItem));
	
	// add our class on the menu-item; the style of this class is (will be) defined in a css, which will override the default gtkmenuitem style.
	gboolean bStyleIsSet = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (pMenuItem), "gldi-style-set"));
	if (! bStyleIsSet)  // not done yet -> do it once
	{
		// draw the menu items; actually, we only want to draw the separators, which are not well drawn by GTK
		/* g_signal_connect (G_OBJECT (pMenuItem),
			"draw",
			G_CALLBACK (_draw_menu_item),
			NULL); */
		
		//!! TODO: not needed? (we now have cdmenuitem as the css name)
		// gtk_style_context_add_class (gtk_widget_get_style_context (pMenuItem), "gldimenuitem");
		
		// if (pSubMenu != NULL)  // if this item has a sub-menu, init it as well
		// 	gldi_menu_init (pSubMenu, NULL);
		
		g_object_set_data (G_OBJECT (pMenuItem), "gldi-style-set", GINT_TO_POINTER(1));
		
	}
	
	// iterate on sub-menu's items
	// if (pSubMenu != NULL)
	// 	gtk_container_forall (GTK_CONTAINER (pSubMenu), (GtkCallback) _init_menu_item2, NULL);
}

static void _adjust_anchor (GdkGravity *anchor, gboolean bLeft)
{
	switch (*anchor)
	{
		case GDK_GRAVITY_SOUTH:
			*anchor = bLeft ? GDK_GRAVITY_SOUTH_WEST : GDK_GRAVITY_SOUTH_EAST;
			break;
		case GDK_GRAVITY_NORTH:
			*anchor = bLeft ? GDK_GRAVITY_NORTH_WEST : GDK_GRAVITY_NORTH_EAST;
			break;
		case GDK_GRAVITY_EAST:
			*anchor = bLeft ? GDK_GRAVITY_NORTH_EAST : GDK_GRAVITY_SOUTH_EAST;
			break;
		case GDK_GRAVITY_WEST:
			*anchor = bLeft ? GDK_GRAVITY_NORTH_WEST : GDK_GRAVITY_SOUTH_WEST;
			break;
		default:
			break;
	}
}

void gldi_menu_popup (GtkWidget *menu)
{
	GldiMenuParams *pParams = g_object_get_data (G_OBJECT(menu), "gldi-params");
	g_return_if_fail (pParams != NULL);

	Icon *pIcon = pParams->pIcon;
	GldiContainer *pContainer = (pIcon ? cairo_dock_get_icon_container (pIcon) : NULL);

	// setup the menu for the container
	if (pContainer && pContainer->iface.setup_menu)
		pContainer->iface.setup_menu (pContainer, pIcon, menu);

	// Not needed -- we can only add our own menuitems
	// init each items (and sub-menus), in case it contains some foreign GtkMenuItems (for instance in case of an indicator menu or the gtk recent files sub-menu, which can have new items at any time)
	// gtk_container_forall (GTK_CONTAINER (menu), (GtkCallback) _init_menu_item2, NULL);  // init each menu-item style
	
	GtkPopover *pPopover = GTK_POPOVER (menu);
	
	if (pIcon && pContainer)
	{
		// hide the icon's label, since menus are placed right above the icon (and therefore, the arrow overlaps the label, which makes it hard to see if both colors are similar).
		if (pIcon->iHideLabel == 0 && pContainer)
			gtk_widget_queue_draw (pContainer->pWidget);
		pIcon->iHideLabel ++;
		
		// ensure margin position is still correct
		_set_margin_position (menu, pParams);

		GdkRectangle rect = {0, 0, 1, 1};
		GtkPositionType pos;
		gldi_container_calculate_rect (pContainer, pIcon, &rect, &pos, FALSE);
		
		gtk_popover_set_position (pPopover, pos);
		gtk_popover_set_pointing_to (pPopover, &rect);
		
		/** TODO: offsets !
		if (pParams->fAlign == 0.0 || pParams->fAlign == 1.0)
		{
			// adjust anchors
			_adjust_anchor (&rect_anchor, (pParams->fAlign == 0.0));
			_adjust_anchor (&menu_anchor, (pParams->fAlign == 0.0));
		}
		else
		{*/
			/* add an offset -- unfortunately, we can only add an absolute offset, but we
			 * do not know our size, and by the time we get it in _menu_realized_cb (),
			 * setting an offset does not have an effect
			 * see e.g. (links to the latest GTK+3 release when writing this):
https://gitlab.gnome.org/GNOME/gtk/-/blob/e1d664da630ee32c4068c8ead4101bce94e7e24a/gtk/gtkmenu.c#L5218
https://gitlab.gnome.org/GNOME/gtk/-/blob/e1d664da630ee32c4068c8ead4101bce94e7e24a/gtk/gtkmenu.c#L5303
https://gitlab.gnome.org/GNOME/gtk/-/blob/e1d664da630ee32c4068c8ead4101bce94e7e24a/gtk/gtkmenu.c#L5325
			 * so we just use a dummy size that works in most cases (note: this is only
			 * used by the "modern" renderer, all others have fAlign == 0.0, 0.5 or 1.0
			 * which is handled correctly by setting anchors)
			 *//*
			const double dummy_width = 240.0;
			const double dummy_height = 120.0;
			if (pContainer->bIsHorizontal)
			{
				int dx = (int) (dummy_width * (0.5 - pParams->fAlign));
				g_object_set (G_OBJECT (menu), "rect-anchor-dx", dx, NULL);
			}
			else
			{
				int dy = (int) (dummy_height * (0.5 - pParams->fAlign));
				g_object_set (G_OBJECT (menu), "rect-anchor-dy", dy, NULL);
			}
		}*/
	}
	
	gtk_popover_popup (pPopover);
}
/*
static gboolean _popup_menu_delayed (GtkWidget *menu)
{
	_popup_menu (menu, NULL);
	return FALSE;
}
void gldi_menu_popup_full (GtkWidget *menu, const GdkEvent *event)
{
	if (menu == NULL)
		return;
	
	GdkEvent *currentEvent = NULL;
	if (!event) event = currentEvent = gtk_get_current_event ();
	
	if (event)
	{
		_popup_menu (menu, event);
		if (currentEvent) gdk_event_free (currentEvent);
	}
	else  // 'gtk_menu_popup' is buggy and doesn't work if not triggered directly by an X event :-/ so in this case, we run it with a delay (200ms is the minimal value that always works).
	{
		g_timeout_add (250, (GSourceFunc)_popup_menu_delayed, menu);
	}
}*/


  /////////////////
 /// MENU ITEM ///
/////////////////

/*
static gboolean _draw_menu_item (GtkWidget *widget,
	cairo_t *cr,
	G_GNUC_UNUSED gpointer data)
{
	if (! GTK_IS_SEPARATOR_MENU_ITEM(widget))  // not a separator => skip drawing anything and let GTK handle it
		return FALSE;
	
	// get menu's geometry
	guint border_width = gtk_container_get_border_width (GTK_CONTAINER (widget));
	gint width = gtk_widget_get_allocated_width (widget);

	gint x, y, w;
	x = border_width;
	y = border_width;
	w = width - border_width * 2;

	// get the line color of the menu
	GldiColor rgb;
	if (myDialogsParam.bUseDefaultColors)
		gldi_style_color_get (GLDI_COLOR_LINE, &rgb);
	else
		rgb = myDialogsParam.fLineColor;
	
	// make a pattern with the alpha channel: 0 - 0.1 ---- .9 - 1
	int mb = w*.05;  // margin to border
	cairo_pattern_t *pattern;
	pattern = cairo_pattern_create_linear (x+mb, y, x+w-mb, y);
	cairo_pattern_add_color_stop_rgba (pattern, 0., rgb.rgba.red, rgb.rgba.green, rgb.rgba.blue, rgb.rgba.alpha*.1);
	cairo_pattern_add_color_stop_rgba (pattern, .1, rgb.rgba.red, rgb.rgba.green, rgb.rgba.blue, rgb.rgba.alpha);
	cairo_pattern_add_color_stop_rgba (pattern, .9, rgb.rgba.red, rgb.rgba.green, rgb.rgba.blue, rgb.rgba.alpha);
	cairo_pattern_add_color_stop_rgba (pattern, 1., rgb.rgba.red, rgb.rgba.green, rgb.rgba.blue, rgb.rgba.alpha*.1);
	cairo_set_source (cr, pattern);
	
	// draw the separator as a 1px line with a margin from the border
	cairo_move_to(cr, x+mb, y);
	cairo_set_line_width (cr, 1);
	cairo_line_to(cr, x+w-mb, y);
	cairo_stroke(cr);
	cairo_pattern_destroy (pattern);
	
	return TRUE;  // intercept
}
*/

static void _set_menu_item_image (CDMenuItem *pMenuItem, cairo_surface_t *surface)
{
	g_return_if_fail (surface);
	
	// we need to keep it since the GdkTexture we create may use the data directly
	cairo_surface_reference (surface);
	
	cairo_surface_flush (surface);
	size_t h = cairo_image_surface_get_height (surface);
	size_t stride = cairo_image_surface_get_stride (surface);
	GBytes *bytes = g_bytes_new_with_free_func (
		cairo_image_surface_get_data (surface),
		stride * h,
		(GDestroyNotify)cairo_surface_destroy,
		surface); // takes ownership of our ref on surface
	GdkTexture *texture = gdk_memory_texture_new (
		cairo_image_surface_get_width (surface),
		h,
		GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,
		bytes,
		stride);
	g_bytes_unref (bytes);
	
	cd_menu_item_set_image (CD_MENU_ITEM (pMenuItem), GDK_PAINTABLE (texture));
	g_object_unref (texture);
}

void gldi_menu_item_set_image (GtkWidget *pMenuItem, cairo_surface_t *surface)
{
	g_return_if_fail (pMenuItem && IS_CD_MENU_ITEM (pMenuItem));
	_set_menu_item_image (CD_MENU_ITEM (pMenuItem), surface);
}
	

/** TODO: return the GdkPaintable !!
GtkWidget *gldi_menu_item_get_image (GtkWidget *pMenuItem)
{
	return gtk3_image_menu_item_get_image (GTK3_IMAGE_MENU_ITEM (pMenuItem));
}
*/


static GtkWidget *_menu_item_new_with_action (const gchar *cLabel, const gchar *cImage,
	void (*pFunction)(GtkWidget*, gpointer), gpointer pData, CDMenu *pMenu)
{
	CDMenuItem *pMenuItem = cd_menu_item_new (cLabel, pMenu);
	if (cImage && *cImage)
	{
		// for icons that are not stock-icons, we choose a bigger size; the reason is that these icons usually don't
		// have a 16x16 version, and don't scale very well to such a small size (most of the time, it's the icon of an
		// application, or the cairo-dock or recent-documents icon (note: for these 2, we could make a small version)).
		// It's a workaround and a better solution may exist ^^
		unsigned int iSize = (*cImage == '/') ? 24 : 16; // GTK_ICON_SIZE_LARGE_TOOLBAR : GTK_ICON_SIZE_MENU
		
		iSize *= myDialogsParam.fUIScale;
		// note: this takes care to load the icon with the correct scale factor
		cairo_surface_t *surface = cairo_dock_create_surface_from_icon (cImage, iSize, iSize);
		_set_menu_item_image (pMenuItem, surface);
		cairo_surface_destroy (surface); // ref was taken above, so it will not actually destroy it
	}
	if (pFunction) g_signal_connect (G_OBJECT (pMenuItem), "clicked", G_CALLBACK (pFunction), pData);
	return GTK_WIDGET (pMenuItem);
}

GtkWidget *gldi_menu_add_item (GtkWidget *pMenu, const gchar *cLabel, const gchar *cImage, GCallback pFunction, gpointer pData)
{
	g_return_val_if_fail (pMenu && IS_CD_MENU (pMenu), NULL);
	
	GtkWidget *pMenuItem = _menu_item_new_with_action (cLabel, cImage, pFunction, pData, CD_MENU (pMenu));
	return pMenuItem;
}

GtkWidget *gldi_menu_add_item_with_tooltip (GtkWidget *pMenu, const gchar *cLabel, const gchar *cImage, const gchar *cToolTip, void (*pFunction)(GtkWidget*, gpointer), gpointer pData)
{
	g_return_val_if_fail (pMenu && IS_CD_MENU (pMenu), NULL);
	
	GtkWidget *pMenuItem = _menu_item_new_with_action (cLabel, cImage, pFunction, pData, CD_MENU (pMenu));
	if (cToolTip) gtk_widget_set_tooltip_text (pMenuItem, cToolTip);
	return pMenuItem;
}

GtkWidget *gldi_menu_add_item_with_checkbox (GtkWidget *pMenu, const gchar *cLabel)
{
	g_return_val_if_fail (pMenu && IS_CD_MENU (pMenu), NULL);
	
	GtkWidget *pMenuItem = cd_menu_item_base_new (pMenu);
	GtkWidget *pCheckBox = gtk_check_button_new_with_label (cLabel);
	gtk_widget_set_margin_top (pCheckBox, 2);
	gtk_widget_set_margin_bottom (pCheckBox, 2);
	// gtk_widget_add_css_class (pMenuItem, "gldimenuitem");
	gtk_box_append (GTK_BOX (pMenuItem), pCheckBox); // takes ref
	return pCheckBox;
}

GtkWidget *gldi_menu_add_sub_menu_full (GtkWidget *pMenu, const gchar *cLabel, const gchar *cImage, GtkWidget **pMenuItemPtr)
{
	g_return_val_if_fail (pMenu && IS_CD_MENU (pMenu), NULL);
	
	GtkWidget *pMenuItem = _menu_item_new_with_action (cLabel, cImage, NULL, NULL, CD_MENU (pMenu));
	if (pMenuItemPtr) *pMenuItemPtr = pMenuItem;
	
	GtkWidget *pSubMenu = gldi_menu_new (NULL);
	cd_menu_item_set_submenu (CD_MENU_ITEM (pMenuItem), CD_MENU (pSubMenu));
	
	return pSubMenu;
}

void gldi_menu_add_separator (GtkWidget *pMenu)
{
	g_return_if_fail (pMenu && IS_CD_MENU (pMenu));
	
	cd_menu_separator_new (pMenu);
}

gboolean GLDI_IS_IMAGE_MENU_ITEM (GtkWidget *pMenuItem)  // defined as a function to not export cdwindow.h
{
	return IS_CD_MENU_ITEM (pMenuItem);
}

