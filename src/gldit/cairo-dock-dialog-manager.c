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
#include "cairo-dock-icon-factory.h"
#include "cairo-dock-icon-facility.h"
#include "cairo-dock-container-priv.h"
#include "cairo-dock-image-buffer.h"
#include "cairo-dock-draw.h"
#include "cairo-dock-draw-opengl.h"
#include "cairo-dock-config.h"
#include "cairo-dock-log.h"
#include "cairo-dock-keyfile-utilities.h"  // cairo_dock_open_key_file
#include "cairo-dock-desklet-factory.h"
#include "cairo-dock-desklet-manager.h"
#include "cairo-dock-dock-manager.h"  // myDockObjectMgr
#include "cairo-dock-dock-facility.h"  // cairo_dock_is_hidden
#include "cairo-dock-backends-manager.h"
#include "cairo-dock-surface-factory.h"
#include "cairo-dock-animations.h"  // for cairo_dock_is_hidden
#include "cairo-dock-desktop-manager.h"
#include "cairo-dock-dialog-factory.h"
#include "cairo-dock-style-manager.h"
#define _MANAGER_DEF_
#include "cairo-dock-dialog-priv.h"

// public (manager, config, data)
CairoDialogsParam myDialogsParam;
GldiManager myDialogsMgr;
GldiObjectManager myDialogObjectMgr;

// dependencies
extern CairoDock *g_pMainDock;
extern gboolean g_bUseOpenGL;
extern CairoDockHidingEffect *g_pHidingBackend;  // cairo_dock_is_hidden
extern gchar *g_cCurrentThemePath;

// private
static GSList *s_pDialogList = NULL;
static cairo_surface_t *s_pButtonOkSurface = NULL;
static cairo_surface_t *s_pButtonCancelSurface = NULL;
static guint s_iSidReplaceDialogs = 0;

static void _set_dialog_orientation (CairoDialog *pDialog, GldiContainer *pContainer);
static void _place_dialog (CairoDialog *pDialog, GldiContainer *pContainer);
static gboolean on_style_changed (G_GNUC_UNUSED gpointer data);


static inline cairo_surface_t *_cairo_dock_load_button_icon (const gchar *cButtonImage, const gchar *cDefaultButtonImage)
{
	cairo_surface_t *pButtonSurface = NULL;
	if (cButtonImage != NULL)
	{
		pButtonSurface = cairo_dock_create_surface_from_image_simple (cButtonImage,
			myDialogsParam.iDialogButtonWidth,
			myDialogsParam.iDialogButtonHeight);
	}
	if (pButtonSurface == NULL)
	{
		pButtonSurface = cairo_dock_create_surface_from_image_simple (cDefaultButtonImage,
			myDialogsParam.iDialogButtonWidth,
			myDialogsParam.iDialogButtonHeight);
	}
	return pButtonSurface;
}

static void _load_dialog_buttons (gchar *cButtonOkImage, gchar *cButtonCancelImage)
{
	if (s_pButtonOkSurface != NULL)
		cairo_surface_destroy (s_pButtonOkSurface);
	s_pButtonOkSurface = _cairo_dock_load_button_icon (cButtonOkImage, GLDI_SHARE_DATA_DIR"/icons/cairo-dock-ok.svg");

	if (s_pButtonCancelSurface != NULL)
		cairo_surface_destroy (s_pButtonCancelSurface);
	s_pButtonCancelSurface = _cairo_dock_load_button_icon (cButtonCancelImage, GLDI_SHARE_DATA_DIR"/icons/cairo-dock-cancel.svg");
}

static void _unload_dialog_buttons (void)
{
	if (s_pButtonOkSurface != NULL)
	{
		cairo_surface_destroy (s_pButtonOkSurface);
		s_pButtonOkSurface = NULL;
	}
	if (s_pButtonCancelSurface != NULL)
	{
		cairo_surface_destroy (s_pButtonCancelSurface);
		s_pButtonCancelSurface = NULL;
	}
}

static void _on_enter_dialog (G_GNUC_UNUSED GtkEventControllerMotion *pCtrl,
	G_GNUC_UNUSED gdouble x,
	G_GNUC_UNUSED gdouble y,
	CairoDialog *pDialog)
{
	pDialog->container.bInside = TRUE;
}

static void _on_leave_dialog (G_GNUC_UNUSED GtkEventControllerMotion *pCtrl, CairoDialog *pDialog)
{
	pDialog->container.bInside = FALSE;
}

static int _cairo_dock_find_clicked_button_in_dialog (gdouble x, gdouble y, CairoDialog *pDialog)
{
	int iButtonX, iButtonY;
	int i, n = pDialog->iNbButtons;
	iButtonY = (pDialog->container.bDirectionUp ?
		pDialog->iTopMargin + pDialog->iMessageHeight + pDialog->iInteractiveHeight + CAIRO_DIALOG_VGAP :
		pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iButtonsHeight));
	int iMinButtonX = .5 * ((pDialog->container.iWidth - pDialog->iLeftMargin - pDialog->iRightMargin) - (n - 1) * CAIRO_DIALOG_BUTTON_GAP - n * myDialogsParam.iDialogButtonWidth) + pDialog->iLeftMargin;
	for (i = 0; i < pDialog->iNbButtons; i++)
	{
		iButtonX = iMinButtonX + i * (CAIRO_DIALOG_BUTTON_GAP + myDialogsParam.iDialogButtonWidth);
		if (x >= iButtonX && x <= iButtonX + myDialogsParam.iDialogButtonWidth && y >= iButtonY && y <= iButtonY + myDialogsParam.iDialogButtonHeight)
		{
			return i;
		}
	}
	return -1;
}

static inline void _answer (CairoDialog *pDialog, int iButton)
{
	pDialog->bInAnswer = TRUE;
	pDialog->action_on_answer (iButton, pDialog->pInteractiveWidget, pDialog->pUserData, pDialog);
	pDialog->bInAnswer = FALSE;
}

static gboolean _is_button_press_in_widget (gdouble x, gdouble y, CairoDialog *pDialog)
{
	graphene_rect_t allocation;
	if (gtk_widget_compute_bounds (pDialog->pInteractiveWidget, pDialog->container.pWidget, &allocation))
		return (x >= allocation.origin.x && x <= allocation.origin.x + allocation.size.width
			&& y >= allocation.origin.y && y <= allocation.origin.y + allocation.size.height);  // the click is inside the widget.
	return FALSE;
}

static void _on_button_press_dialog (G_GNUC_UNUSED GtkGestureClick *pCtrl, G_GNUC_UNUSED gint n_press, gdouble x, gdouble y, CairoDialog *pDialog)
{
	// note: we already know it is the left button
	//!! TODO: we should consider that the widget inside the dialog might pass on the click to us??
	//!! if (pButton->time == pDialog->iButtonPressTime) return;
	
	// the interactive widget may have holes (for instance, a gtk-calendar); ignore them, otherwise it's really easy to close the dialog unexpectedly.
	if (pDialog->pInteractiveWidget)
		if (_is_button_press_in_widget (x, y, pDialog)) return;
	
	if (pDialog->pButtons == NULL)  // not a dialog that can be closed by a button => we close it here
	{
		if (pDialog->bHideOnClick) gldi_dialog_hide (pDialog);
		else pDialog->bPendingClose = TRUE; // wait until the release event before closing
	}
	else // left click on a button.
	{
		int iButton = _cairo_dock_find_clicked_button_in_dialog (x, y, pDialog);
		if (iButton >= 0 && iButton < pDialog->iNbButtons)
		{
			pDialog->pButtons[iButton].iOffset = CAIRO_DIALOG_BUTTON_OFFSET;
			gtk_widget_queue_draw (pDialog->container.pWidget);
		}
	}
}

static void _on_button_release_dialog (G_GNUC_UNUSED GtkGestureClick *pCtrl, G_GNUC_UNUSED gint n_press, gdouble x, gdouble y, CairoDialog *pDialog)
{
	// note: we already know it is the left button
	
	// the interactive widget may have holes (for instance, a gtk-calendar); ignore them, otherwise it's really easy to close the dialog unexpectedly.
	if (pDialog->pInteractiveWidget)
		if (_is_button_press_in_widget (x, y, pDialog)) return;
	
	if (pDialog->bPendingClose)
	{
		gldi_object_unref (GLDI_OBJECT(pDialog));
		return;
	}
	if (pDialog->pButtons != NULL)  // release left click with buttons present
	{
		int iButton = _cairo_dock_find_clicked_button_in_dialog (x, y, pDialog);
		cd_debug ("clic on button %d", iButton);
		if (iButton >= 0 && iButton < pDialog->iNbButtons && pDialog->pButtons[iButton].iOffset != 0)
		{
			pDialog->pButtons[iButton].iOffset = 0;
			_answer (pDialog, iButton);
			gtk_widget_queue_draw (pDialog->container.pWidget);  // in case the unref below wouldn't destroy it
			gldi_object_unref (GLDI_OBJECT(pDialog));  // and then destroy the dialog (it might not be destroyed if the ation callback took a ref on it).
		}
		else
		{
			int i;
			for (i = 0; i < pDialog->iNbButtons; i++)
				pDialog->pButtons[i].iOffset = 0;
			gtk_widget_queue_draw (pDialog->container.pWidget);
		}
	}
}

static gboolean _on_key_press_dialog (G_GNUC_UNUSED GtkEventControllerKey *pCtrl, guint keyval,
	G_GNUC_UNUSED guint keycode, GdkModifierType state, CairoDialog *pDialog)
{
	cd_debug ("key pressed on dialog: %d / %d", state, GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK);
	
	if (((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SHIFT_MASK)) == 0) && pDialog->action_on_answer != NULL)
	{
		switch (keyval)
		{
			case GDK_KEY_Return :
			case GDK_KEY_KP_Enter :
				_answer (pDialog, CAIRO_DIALOG_ENTER_KEY);
				gldi_object_unref (GLDI_OBJECT(pDialog));
			break ;
			case GDK_KEY_Escape :
				_answer (pDialog, CAIRO_DIALOG_ESCAPE_KEY);
				gldi_object_unref (GLDI_OBJECT(pDialog));
			break ;
		}
	}
	return FALSE;
}

static void _cairo_dock_dialog_delete (CairoDialog *pDialog)
{
	if (pDialog != NULL)
	{
		if (pDialog->action_on_answer != NULL)
			pDialog->action_on_answer (CAIRO_DIALOG_ESCAPE_KEY, pDialog->pInteractiveWidget, pDialog->pUserData, pDialog);
		gldi_object_unref (GLDI_OBJECT(pDialog));  // on pourrait eventuellement faire un fondu avant.
	}
}

static gboolean _cairo_dock_dialog_auto_delete (CairoDialog *pDialog)
{
	if (pDialog != NULL)
	{
		pDialog->iSidTimer = 0;
		_cairo_dock_dialog_delete (pDialog);  // on pourrait eventuellement faire un fondu avant.
	}
	return FALSE;
}
/*
static void _cairo_dock_draw_inside_dialog_opengl (CairoDialog *pDialog, double fAlpha)
{
	_cairo_dock_enable_texture ();
	_cairo_dock_set_blend_alpha ();
	_cairo_dock_set_alpha (fAlpha);
	
	double x, y;
	if (pDialog->iIconTexture != 0)
	{
		x = pDialog->iLeftMargin;  /// TODO: use iconoffset here, and add a padding for placement only...
		y = (pDialog->container.bDirectionUp ? pDialog->iTopMargin : pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight));
		
		glBindTexture (GL_TEXTURE_2D, pDialog->iIconTexture);
		_cairo_dock_apply_current_texture_portion_at_size_with_offset (0, 0.,
			1., 1.,
			pDialog->iIconSize, pDialog->iIconSize,
			x + pDialog->iIconSize/2, pDialog->container.iHeight - y - pDialog->iIconSize/2);
	}
	
	if (pDialog->iTextTexture != 0)
	{
		x = pDialog->iLeftMargin + pDialog->iIconSize + CAIRO_DIALOG_TEXT_MARGIN;
		y = (pDialog->container.bDirectionUp ? pDialog->iTopMargin : pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight));
		if (pDialog->iTextHeight < pDialog->iMessageHeight)  // on centre le texte.
			y += (pDialog->iMessageHeight - pDialog->iTextHeight) / 2;
		
		glBindTexture (GL_TEXTURE_2D, pDialog->iTextTexture);
		_cairo_dock_apply_current_texture_at_size_with_offset (pDialog->iTextWidth, pDialog->iTextHeight,
			x + pDialog->iTextWidth/2, pDialog->container.iHeight - y - pDialog->iTextHeight/2);
	}
	
	if (pDialog->pButtons != NULL)
	{
		int iButtonX, iButtonY;
		int i, n = pDialog->iNbButtons;
		iButtonY = (pDialog->container.bDirectionUp ? pDialog->iTopMargin + pDialog->iMessageHeight + pDialog->iInteractiveHeight + CAIRO_DIALOG_VGAP : pDialog->container.iHeight - pDialog->iTopMargin - pDialog->iButtonsHeight - CAIRO_DIALOG_VGAP);
		int iMinButtonX = .5 * ((pDialog->container.iWidth - pDialog->iLeftMargin - pDialog->iRightMargin) - (n - 1) * CAIRO_DIALOG_BUTTON_GAP - n * myDialogsParam.iDialogButtonWidth) + pDialog->iLeftMargin;
		for (i = 0; i < pDialog->iNbButtons; i++)
		{
			iButtonX = iMinButtonX + i * (CAIRO_DIALOG_BUTTON_GAP + myDialogsParam.iDialogButtonWidth);
			glBindTexture (GL_TEXTURE_2D, pDialog->pButtons[i].iTexture);
			_cairo_dock_apply_current_texture_at_size_with_offset (myDialogsParam.iDialogButtonWidth,
				myDialogsParam.iDialogButtonWidth,
				iButtonX + pDialog->pButtons[i].iOffset + myDialogsParam.iDialogButtonWidth/2,
				pDialog->container.iHeight - (iButtonY + pDialog->pButtons[i].iOffset + myDialogsParam.iDialogButtonWidth/2));			}
	}
	
	if (pDialog->pRenderer != NULL && pDialog->pRenderer->render_opengl)
		pDialog->pRenderer->render_opengl (pDialog, fAlpha);
}
*/
#define _paint_inside_dialog(pCairoContext, fAlpha) do { \
	if (fAlpha != 0) \
		cairo_paint_with_alpha (pCairoContext, fAlpha); \
	else \
		cairo_paint (pCairoContext); } while (0)
static void _cairo_dock_draw_inside_dialog (cairo_t *pCairoContext, CairoDialog *pDialog, double fAlpha)
{
	double x, y;
	if (pDialog->pIconBuffer != NULL)
	{
		x = pDialog->iLeftMargin;
		x = MAX (0, x - pDialog->iIconOffsetX);
		y = (pDialog->container.bDirectionUp ? pDialog->iTopMargin : pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight)) - pDialog->iIconOffsetX;
		y = MAX (0, y - pDialog->iIconOffsetY);
		cairo_set_source_surface (pCairoContext,
			pDialog->pIconBuffer,
			x,
			y);
		_paint_inside_dialog(pCairoContext, fAlpha);
	}
	
	if (pDialog->pTextBuffer != NULL)
	{
		x = pDialog->iLeftMargin + pDialog->iIconSize + CAIRO_DIALOG_TEXT_MARGIN - (pDialog->iIconSize != 0 ? pDialog->iIconOffsetX : 0);
		y = (pDialog->container.bDirectionUp ? pDialog->iTopMargin : pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight));
		if (pDialog->iTextHeight < pDialog->iMessageHeight)  // on centre le texte.
			y += (pDialog->iMessageHeight - pDialog->iTextHeight) / 2;
		cairo_set_source_surface (pCairoContext,
			pDialog->pTextBuffer,
			x,
			y);
		_paint_inside_dialog(pCairoContext, fAlpha);
	}
	
	if (pDialog->pButtons != NULL)
	{
		int iButtonX, iButtonY;
		int i, n = pDialog->iNbButtons;
		iButtonY = (pDialog->container.bDirectionUp ? pDialog->iTopMargin + pDialog->iMessageHeight + pDialog->iInteractiveHeight + CAIRO_DIALOG_VGAP : pDialog->container.iHeight - pDialog->iTopMargin - pDialog->iButtonsHeight + CAIRO_DIALOG_VGAP);
		int iMinButtonX = .5 * ((pDialog->container.iWidth - pDialog->iLeftMargin - pDialog->iRightMargin) - (n - 1) * CAIRO_DIALOG_BUTTON_GAP - n * myDialogsParam.iDialogButtonWidth) + pDialog->iLeftMargin;
		cairo_surface_t *pButtonSurface;
		for (i = 0; i < pDialog->iNbButtons; i++)
		{
			iButtonX = iMinButtonX + i * (CAIRO_DIALOG_BUTTON_GAP + myDialogsParam.iDialogButtonWidth);
			if (pDialog->pButtons[i].pSurface != NULL)
				pButtonSurface = pDialog->pButtons[i].pSurface;
			else if (pDialog->pButtons[i].iDefaultType == 1)
				pButtonSurface = s_pButtonOkSurface;
			else
				pButtonSurface = s_pButtonCancelSurface;
			cairo_set_source_surface (pCairoContext,
				pButtonSurface,
				iButtonX + pDialog->pButtons[i].iOffset,
				iButtonY + pDialog->pButtons[i].iOffset);
			_paint_inside_dialog(pCairoContext, fAlpha);
		}
	}
	
	if (pDialog->pRenderer != NULL)
		pDialog->pRenderer->render (pCairoContext, pDialog, fAlpha);
}

static gboolean _cairo_dock_render_dialog_notification (G_GNUC_UNUSED gpointer data, CairoDialog *pDialog, cairo_t *pCairoContext)
{
/*	if (pCairoContext == NULL)
	{
		_cairo_dock_draw_inside_dialog_opengl (pDialog, 0.);
		if (pDialog->container.bUseReflect)
		{
			glTranslatef (0.,
				pDialog->container.iHeight - 2* (pDialog->iTopMargin + pDialog->iBubbleHeight),
				0.);
			glScalef (1., -1., 1.);
			_cairo_dock_draw_inside_dialog_opengl (pDialog, pDialog->container.fRatio);
		}
	}
	else*/
	{
		_cairo_dock_draw_inside_dialog (pCairoContext, pDialog, 0.);
		
		if (pDialog->container.bUseReflect)
		{
			cairo_save (pCairoContext);
			cairo_rectangle (pCairoContext,
				0.,
				pDialog->iTopMargin + pDialog->iBubbleHeight,
				pDialog->iBubbleWidth,
				pDialog->iBottomMargin);
			//g_print( "pDialog->iBottomMargin:%d\n", pDialog->iBottomMargin);
			cairo_clip (pCairoContext);
	
			cairo_translate (pCairoContext,
				0.,
				2* (pDialog->iTopMargin + pDialog->iBubbleHeight));
			cairo_scale (pCairoContext, 1., -1.);
			_cairo_dock_draw_inside_dialog (pCairoContext, pDialog, pDialog->container.fRatio);
		}
	}
	return GLDI_NOTIFICATION_LET_PASS;
}


static gboolean _remove_dialog_on_icon (CairoDialog *pDialog, Icon *icon)
{
	if (pDialog->pIcon == icon && ! pDialog->bInAnswer)  // if inside the answer, don't unref, since the dialog will be destroyed after the answer (for instance, can happen with the confirmation dialog of the destruction of an icon)
		gldi_object_unref (GLDI_OBJECT(pDialog));
	return FALSE;
}
void gldi_dialogs_remove_on_icon (Icon *icon)  // gldi_icon_remove_dialog ?...
{
	g_return_if_fail (icon != NULL);
	gldi_dialogs_foreach ((GCompareFunc)_remove_dialog_on_icon, icon);
}

static void _set_dialog_orientation (CairoDialog *pDialog, GldiContainer *pContainer)
{
	if (pContainer != NULL)
	{
		if (pContainer->bIsHorizontal == CAIRO_DOCK_HORIZONTAL)
			pDialog->container.bDirectionUp = pContainer->bDirectionUp;
		else
		{
			pDialog->container.bDirectionUp = TRUE;
			if (pDialog->pIcon && CAIRO_DOCK_IS_DOCK (pContainer))
			{
				CairoDock *pDock = CAIRO_DOCK (pContainer);
				Icon *pIcon = pDialog->pIcon;
				while (pDock->iRefCount > 0 && ! gldi_container_is_visible (pContainer))  // sous-dock invisible.
				{
					pIcon = cairo_dock_search_icon_pointing_on_dock (pDock, &pDock);
					pContainer = CAIRO_CONTAINER (pDock);
				}
				if (pIcon->fXAtRest < pDock->fFlatDockWidth / 2) pDialog->container.bDirectionUp = FALSE;
				pDialog->bRight = !pDock->container.bDirectionUp;
			}
		}
	}
	else pDialog->container.bDirectionUp = TRUE;
}

static void _place_dialog (CairoDialog *pDialog, GldiContainer *pContainer)
{
	//g_print ("%s (%p;%p, %s)\n", __func__, pDialog->pIcon, pContainer, pDialog->pIcon?pDialog->pIcon->cParentDockName:"none");
	if (pDialog->container.bInside && ! (pDialog->pInteractiveWidget || pDialog->action_on_answer))  // in the case of a modal dialog, the dialog takes the dock's events, including the "enter-event" one. So we are inside the dialog as soon as we enter the dock, and consequently, the dialog is not replaced when the dock unhides itself.
		return;
	
	_set_dialog_orientation (pDialog, pContainer);
	
	pDialog->bPositionForced = FALSE;
	Icon* pPointedIcon = pDialog->pIcon;
	if (pContainer)
	{
		GdkRectangle rect = {0, 0, 1, 1};
		GtkPositionType pos;
		gldi_container_calculate_rect (pContainer, pPointedIcon, &rect, &pos, FALSE);
		
		gtk_popover_set_position (GTK_POPOVER (pDialog->container.pWidget), pos);
		gtk_popover_set_pointing_to (GTK_POPOVER (pDialog->container.pWidget), &rect);
		
		//!! TODO: gtk_popover_set_offset() to set an offset based on pDialog->fAlign ! (now it will be in the middle)
	}
}


static gboolean s_bInRefreshDialogs = FALSE;
static void _refresh_all_dialogs (gboolean bReplace)
{
	//g_print ("%s ()\n", __func__);
	GSList *ic;
	CairoDialog *pDialog;
	GldiContainer *pContainer;
	Icon *pIcon;

	if (s_pDialogList == NULL)
		return ;

	// delete any dialog that does not have a parent anymore
	GSList *next, *to_delete = NULL;
	GSList dummy;
	dummy.next = s_pDialogList;
	ic = &dummy;
	
	while (ic && ic->next)
	{
		// in this case, the dialog in ic has a visible parent (or is the dummy), we need to check ic->next
		next = ic->next;
		pDialog = next->data;
		pIcon = pDialog->pIcon;
		pContainer = pIcon ? cairo_dock_get_icon_container (pIcon) : NULL;
		gboolean bDelete = FALSE;
		if (pContainer && !gldi_container_is_visible (pContainer))
		{
			if (pDialog->bHideOnClick)
			{
				if (gldi_container_is_visible (CAIRO_CONTAINER (pDialog)))
					gtk_widget_set_visible (pDialog->container.pWidget, FALSE);
			}
			else
			{
				// remove next from the list
				ic->next = next->next;
				next->next = to_delete;
				to_delete = next;
				bDelete = TRUE;
			}
		}
		if (!bDelete) ic = next;
	}
	s_pDialogList = dummy.next; // in case we removed the first element in the list
	
	if (to_delete)
	{
		s_bInRefreshDialogs = TRUE;
		g_slist_free_full (to_delete, (GDestroyNotify)_cairo_dock_dialog_delete);
		s_bInRefreshDialogs = FALSE;
	}

	for (ic = s_pDialogList; ic != NULL; ic = ic->next)
	{
		pDialog = ic->data;
		
		pIcon = pDialog->pIcon;
		if (pIcon != NULL && gldi_container_is_visible (CAIRO_CONTAINER (pDialog)))  // on ne replace pas les dialogues en cours de destruction ou caches.
		{
			pContainer = cairo_dock_get_icon_container (pIcon);
			if (pContainer)
			{
				int iAimedX = pDialog->iAimedX;
				int iAimedY = pDialog->iAimedY;
				if (bReplace)
					_place_dialog (pDialog, pContainer);
				else
					_set_dialog_orientation (pDialog, pContainer);
				
				//!! TODO: iAimedX will likely not change
				if (iAimedX != pDialog->iAimedX || iAimedY != pDialog->iAimedY)
					gtk_widget_queue_draw (pDialog->container.pWidget);  // on redessine si la pointe change de position.
			}
		}
	}
}

void gldi_dialogs_refresh_all (void)
{
	_refresh_all_dialogs (FALSE);
}

void gldi_dialogs_replace_all (void)
{
	_refresh_all_dialogs (TRUE);
}


static gboolean _replace_all_dialogs_idle (G_GNUC_UNUSED gpointer data)
{
	gldi_dialogs_replace_all ();
	s_iSidReplaceDialogs = 0;
	return FALSE;
}
static void _trigger_replace_all_dialogs (void)
{
	if (s_iSidReplaceDialogs == 0)
	{
		s_iSidReplaceDialogs = g_idle_add ((GSourceFunc)_replace_all_dialogs_idle, NULL);
	}
}

void gldi_dialog_leave (CairoDialog *pDialog)
{
	Icon *pIcon = pDialog->pIcon;
	if (pIcon != NULL)
	{
		GldiContainer *pContainer = cairo_dock_get_icon_container (pIcon);
		//g_print ("leave from container %x\n", pContainer);
		if (pContainer)
		{
			if (CAIRO_DOCK_IS_DOCK (pContainer))
			{
				CAIRO_DOCK (pContainer)->bHasModalWindow = FALSE;
				gldi_dock_leave_synthetic (CAIRO_DOCK (pContainer));
			}
		}
		if (pIcon->iHideLabel > 0)
		{
			pIcon->iHideLabel --;
			if (pIcon->iHideLabel == 0 && pContainer)
				gtk_widget_queue_draw (pContainer->pWidget);
		}
	}
}

void gldi_dialog_hide (CairoDialog *pDialog)
{
	cd_debug ("%s ()", __func__);
	if (gldi_container_is_visible (CAIRO_CONTAINER (pDialog)))
	{
		pDialog->bAllowMinimize = TRUE;
		gtk_widget_set_visible (pDialog->container.pWidget, FALSE);
		pDialog->container.bInside = FALSE;
		
		_trigger_replace_all_dialogs ();
		
		gldi_dialog_leave (pDialog);
	}
}

void gldi_dialog_unhide (CairoDialog *pDialog)
{
	cd_debug ("%s ()", __func__);
	if (! gldi_container_is_visible (CAIRO_CONTAINER (pDialog)))
	{
		if (pDialog->pInteractiveWidget != NULL)
			gtk_widget_grab_focus (pDialog->pInteractiveWidget);
		Icon *pIcon = pDialog->pIcon;
		if (pIcon != NULL)
		{
			GldiContainer *pContainer = cairo_dock_get_icon_container (pIcon);
			_place_dialog (pDialog, pContainer);
			
			if (CAIRO_DOCK_IS_DOCK (pContainer) && cairo_dock_get_icon_max_scale (pIcon) < 1.01)  // same remark
			{
				if (pIcon->iHideLabel == 0 && pContainer)
					gtk_widget_queue_draw (pContainer->pWidget);
				pIcon->iHideLabel ++;
			}
			if (CAIRO_DOCK_IS_DOCK (pContainer))
			{
				CAIRO_DOCK (pContainer)->bHasModalWindow = TRUE;
			}
		}
	}
	pDialog->bPositionForced = FALSE;
	gtk_popover_popup (GTK_POPOVER (pDialog->container.pWidget));
}

void gldi_dialog_toggle_visibility (CairoDialog *pDialog)
{
	if (gldi_container_is_visible (CAIRO_CONTAINER (pDialog)))
		gldi_dialog_hide (pDialog);
	else
		gldi_dialog_unhide (pDialog);
}

static gboolean on_icon_removed (G_GNUC_UNUSED gpointer pUserData, Icon *pIcon, CairoDock *pDock)
{
	// if an icon is detached from the dock, and is destroyed (for instance, when removing an icon), the icon is detached and then freed;
	// its dialogs (for instance the confirmation dialog) are then destroyed, but since the icon is already detached, the dialog can't unset the 'bHasModalWindow' flag on its previous container.
	// therefore we do it here (plus it's logical to do that whenever an icon is detached. Note: we could handle the case of the icon being rattached to a container while having a modal dialog, to set the 'bHasModalWindow' flag, but I can't imagine a way it would happen).
	if (pIcon && pDock)
	{
		if (pDock->bHasModalWindow)  // check that the icon that is being detached was not carrying a modal dialog.
		{
			GSList *d;
			CairoDialog *pDialog;
			for (d = s_pDialogList; d != NULL; d = d->next)
			{
				pDialog = d->data;
				if (pDialog->pIcon == pIcon)
				{
					pDock->bHasModalWindow = FALSE;
					gldi_dock_leave_synthetic (pDock);
					break;  // there can only be 1 modal window at a time.
				}
			}
		}
	}
	return GLDI_NOTIFICATION_LET_PASS;
}

static gboolean on_icon_destroyed (G_GNUC_UNUSED gpointer pUserData, Icon *pIcon)
{
	gldi_dialogs_remove_on_icon (pIcon);
	return GLDI_NOTIFICATION_LET_PASS;
}

CairoDialog *gldi_dialogs_foreach (GCompareFunc callback, gpointer data)
{
	CairoDialog *pDialog;
	GSList *d, *next_d;
	for (d = s_pDialogList; d != NULL; d = next_d)
	{
		next_d = d->next;  // in case the dialog is destroyed in the callback
		pDialog = d->data;
		if (callback (pDialog, data))
			return pDialog;
	}
	return NULL;
}

  //////////////////
 /// GET CONFIG ///
//////////////////

static gboolean get_config (GKeyFile *pKeyFile, CairoDialogsParam *pDialogs)
{
	gboolean bFlushConfFileNeeded = FALSE;
	
	double fScale = 1.0;
	if (gldi_container_get_scale_setting (pKeyFile, &fScale, &bFlushConfFileNeeded))
	{
		pDialogs->fUIScale = fScale;
		gboolean bDontScaleMenus = cairo_dock_get_boolean_key_value (pKeyFile, "System", "ui scale exclude menus", &bFlushConfFileNeeded, FALSE, NULL, NULL);
		if (bDontScaleMenus) pDialogs->fMenuFontScale = 1.0;
		else pDialogs->fMenuFontScale = fScale;
	}
	else
	{
		pDialogs->fMenuFontScale = 1.0;
		pDialogs->fUIScale = 1.0;
	}
	
	pDialogs->cButtonOkImage = cairo_dock_get_string_key_value (pKeyFile, "Dialogs", "button_ok image", &bFlushConfFileNeeded, NULL, NULL, NULL);
	pDialogs->cButtonCancelImage = cairo_dock_get_string_key_value (pKeyFile, "Dialogs", "button_cancel image", &bFlushConfFileNeeded, NULL, NULL, NULL);
	
	cairo_dock_get_size_key_value_helper (pKeyFile, "Dialogs", "button ", bFlushConfFileNeeded, pDialogs->iDialogButtonWidth, pDialogs->iDialogButtonHeight);
	pDialogs->iDialogButtonWidth *= pDialogs->fUIScale;
	pDialogs->iDialogButtonHeight *= pDialogs->fUIScale;
	
	GldiColor couleur_bulle = {{1.0, 1.0, 1.0, 0.7}};
	cairo_dock_get_color_key_value (pKeyFile, "Dialogs", "bg color", &bFlushConfFileNeeded, &pDialogs->fBgColor, &couleur_bulle, NULL, "background color");
	pDialogs->iDialogIconSize = MAX (16, cairo_dock_get_integer_key_value (pKeyFile, "Dialogs", "icon size", &bFlushConfFileNeeded, 48, NULL, NULL));
	pDialogs->iDialogIconSize *= pDialogs->fUIScale;
	
	pDialogs->cDecoratorName = cairo_dock_get_string_key_value (pKeyFile, "Dialogs", "decorator", &bFlushConfFileNeeded, "comics", NULL, NULL);
	
	if (! g_key_file_has_key (pKeyFile, "Dialogs", "line color", NULL))  // old params (< 3.4)
	{
		// get the old params from the Dialog module's config
		gchar *cRenderingConfFile = g_strdup_printf ("%s/plug-ins/dialog-rendering/dialog-rendering.conf", g_cCurrentThemePath);
		GKeyFile *keyfile = cairo_dock_open_key_file (cRenderingConfFile);
		g_free (cRenderingConfFile);
		
		gchar *cRenderer = g_strdup (pDialogs->cDecoratorName);
		if (cRenderer)
		{
			cRenderer[0] = g_ascii_toupper (cRenderer[0]);
			
			cairo_dock_get_color_key_value (keyfile, cRenderer, "line color", &bFlushConfFileNeeded, &pDialogs->fLineColor, NULL, NULL, NULL);
			g_key_file_set_double_list (pKeyFile, "Dialogs", "line color", (double*)&pDialogs->fLineColor.rgba, 4);
			
			pDialogs->iLineWidth = g_key_file_get_integer (keyfile, cRenderer, "border", NULL);
			g_key_file_set_integer (pKeyFile, "Dialogs", "linewidth", pDialogs->iLineWidth);
			
			pDialogs->iCornerRadius = g_key_file_get_integer (keyfile, cRenderer, "corner", NULL);
			g_key_file_set_integer (pKeyFile, "Dialogs", "corner", pDialogs->iCornerRadius);
			
			g_free (cRenderer);
		}
		g_key_file_free (keyfile);
		
		bFlushConfFileNeeded = TRUE;
	}
	else
	{
		pDialogs->iCornerRadius = g_key_file_get_integer (pKeyFile, "Dialogs", "corner", NULL);
		pDialogs->iLineWidth = g_key_file_get_integer (pKeyFile, "Dialogs", "linewidth", NULL);
		cairo_dock_get_color_key_value (pKeyFile, "Dialogs", "line color", &bFlushConfFileNeeded, &pDialogs->fLineColor, NULL, NULL, NULL);
	}
	
	pDialogs->bUseDefaultColors = (cairo_dock_get_integer_key_value (pKeyFile, "Dialogs", "style", &bFlushConfFileNeeded, 0, NULL, NULL) == 0);
	
	gboolean bCustomFont = cairo_dock_get_boolean_key_value (pKeyFile, "Dialogs", "custom", &bFlushConfFileNeeded, TRUE, NULL, NULL);
	gchar *cFont = (bCustomFont ? cairo_dock_get_string_key_value (pKeyFile, "Dialogs", "message police", &bFlushConfFileNeeded, NULL, "Icons", NULL) : NULL);
	gldi_text_description_set_font (&pDialogs->dialogTextDescription, cFont);
	pDialogs->dialogTextDescription.iSize *= pDialogs->fUIScale;
	
	pDialogs->dialogTextDescription.fMaxRelativeWidth = .5;  // limit to half of the screen (the dialog is not placed on a given screen, it can overlap 2 screens, so it's half of the mean screen width)
	
	pDialogs->dialogTextDescription.bOutlined = FALSE;
	pDialogs->dialogTextDescription.iMargin = 0;
	pDialogs->dialogTextDescription.bNoDecorations = TRUE;
	
	GldiColor couleur_dtext = {{0., 0., 0., 1.}};
	cairo_dock_get_color_key_value (pKeyFile, "Dialogs", "text color", &bFlushConfFileNeeded, &pDialogs->dialogTextDescription.fColorStart, &couleur_dtext, NULL, NULL);
	
	pDialogs->dialogTextDescription.bUseDefaultColors = pDialogs->bUseDefaultColors;
	
	return bFlushConfFileNeeded;
}

/** Init style for menus, i.e. the gldimenuitem css class. This would conceptually
 * belong to cairo-dock-menu.c, but is here as the config for menus and dialogs is
 * handled together.
 */
static void _init_menu_style (void)
{
	static GtkCssProvider *cssProvider = NULL;
	/**static int s_stamp = 0;
	if (s_stamp == gldi_style_colors_get_stamp())  // if the style has not changed since we last called this function, there is nothing to do
		return;
	s_stamp = gldi_style_colors_get_stamp();
	cd_debug ("%s (%d)", __func__, s_stamp);*/
	cd_debug ("%s (%d)", __func__, myDialogsParam.bUseDefaultColors);
	
	if (myDialogsParam.bUseDefaultColors && myStyleParam.bUseSystemColors)
	{
		if (cssProvider != NULL)
		{
			gldi_style_colors_freeze ();
			gtk_style_context_remove_provider_for_display (gdk_display_get_default(), GTK_STYLE_PROVIDER(cssProvider));
			gldi_style_colors_freeze ();
			g_object_unref (cssProvider);
			cssProvider = NULL;
			//!! TODO: might still need a minimal provider to adjust font size !!
		}
	}
	else
	{
		// make a css provider
		if (cssProvider == NULL)
		{
			cssProvider = gtk_css_provider_new ();
			gldi_style_colors_freeze ();
			gtk_style_context_add_provider_for_display (gdk_display_get_default(), GTK_STYLE_PROVIDER(cssProvider), GTK_STYLE_PROVIDER_PRIORITY_USER);
			gldi_style_colors_freeze ();
		}
		
		// css header: define colors from the global style
		GldiColor bg_color;
		if (myDialogsParam.bUseDefaultColors)
			gldi_style_color_get (GLDI_COLOR_BG, &bg_color);
		else
			bg_color = myDialogsParam.fBgColor;
		GldiColor text_color;
		if (myDialogsParam.bUseDefaultColors)
			gldi_style_color_get (GLDI_COLOR_TEXT, &text_color);
		else
			text_color = myDialogsParam.dialogTextDescription.fColorStart;
		GldiColor rgb;  // menuitem bg color: a little darker/lighter than the menu's bg color; also separator color (with no alpha)
		gldi_style_color_shade (&bg_color, GLDI_COLOR_SHADE_MEDIUM, &rgb);
		GldiColor rgbb;  // menuitem border color and menuitem's child bg color (for instance, calendar, scale, etc): a little darker/lighter than the menuitem bg color
		gldi_style_color_shade (&bg_color, GLDI_COLOR_SHADE_STRONG, &rgbb);
		GldiColor arrow_color; // submenu arrow color -- a bit darker/lighter than the text color
		gldi_style_color_shade (&text_color, GLDI_COLOR_SHADE_MEDIUM, &arrow_color);
		
		gchar *cssheader = g_strdup_printf ("@define-color menuitem_bg_color rgba(%d, %d, %d, %f); \n\
		@define-color menuitem_text_color rgb(%d, %d, %d); \n\
		@define-color menuitem_insensitive_text_color rgba(%d, %d, %d, .5); \n\
		@define-color menuitem_separator_color rgb(%d, %d, %d); \n\
		@define-color menuitem_child_bg_color rgba(%d, %d, %d, %f); \n\
		@define-color menu_bg_color rgba(%d, %d, %d, %f);\n\
		@define-color submenu_arrow_color rgba(%d, %d, %d, %f);\n",
			(int)(rgb.rgba.red*255), (int)(rgb.rgba.green*255), (int)(rgb.rgba.blue*255), rgb.rgba.alpha,
			(int)(text_color.rgba.red*255), (int)(text_color.rgba.green*255), (int)(text_color.rgba.blue*255),
			(int)(text_color.rgba.red*255), (int)(text_color.rgba.green*255), (int)(text_color.rgba.blue*255),
			(int)(rgb.rgba.red*255), (int)(rgb.rgba.green*255), (int)(rgb.rgba.blue*255),
			(int)(rgbb.rgba.red*255), (int)(rgbb.rgba.green*255), (int)(rgbb.rgba.blue*255), rgbb.rgba.alpha,
			(int)(bg_color.rgba.red*255), (int)(bg_color.rgba.green*255), (int)(bg_color.rgba.blue*255), bg_color.rgba.alpha,
			(int)(arrow_color.rgba.red*255), (int)(arrow_color.rgba.green*255), (int)(arrow_color.rgba.blue*255), arrow_color.rgba.alpha);
		
		// css body: load a custom file if it exists
		gchar *cCustomCss = NULL;
		gchar *cCustomCssFile = g_strdup_printf ("%s/menu.css", g_cCurrentThemePath);  // this is mainly for advanced customizing and to be able to work around some gtk themes that could pose problems; avoid using it in public themes, since it's not available to normal user from the config window
		if (g_file_test (cCustomCssFile, G_FILE_TEST_EXISTS))
		{
			gsize length = 0;
			g_file_get_contents (cCustomCssFile,
				&cCustomCss,
				&length,
				NULL);
		}
		g_free (cCustomCssFile);
		
		gchar *css;
		if (cCustomCss != NULL)
		{
			css = g_strconcat (cssheader, cCustomCss, NULL);
			g_free (cCustomCss);
		}
		else
		{
			gchar *cFontSize = (myDialogsParam.fMenuFontScale != 1.0) ?
				g_strdup_printf ("cdmenuitem label { font-size: %f%%; }\n", myDialogsParam.fMenuFontScale * 100.0)
				: NULL;
			
			css = g_strconcat (cssheader,
			"\
			cdmenu.background {\
			background-color: rgba(0, 0, 0, 0.0);\
			}\
			cdmenu contents {\
				box-shadow: 0 0 0 0;\
				background-color: rgba(0, 0, 0, 0.0);\
				border: none;\
				border-radius: 0 0 0 0;\
				padding: 0 0 0 0;\
			}\
			cdmenuitem { \
				text-shadow: none; \
				border-image: none; \
				box-shadow: none; \
				background: transparent; \
				color: @menuitem_text_color; \
				border-color: transparent; \
			} \
			cdarrow {\
				color: @submenu_arrow_color; \
			} \
			cdmenuitem GtkImage, \
			cdmenuitem .image { \
				background: transparent; \
			} \
			cdmenuseparator { \
				color: @menuitem_separator_color; \
			} \
			cdmenuitem:hover { \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				text-shadow: none; \
				border-image: none; \
				box-shadow: none; \
				color: @menuitem_text_color; \
				border-radius: 5px; \
				border-style: solid; \
				border-color: @menuitem_child_bg_color; \
			} \
			cdmenuitem *:disabled { \
				text-shadow: none; \
				color: @menuitem_insensitive_text_color; \
				background: transparent; \
			} \
			cdmenuitem .entry, \
			cdmenuitem.entry { \
				background: @menuitem_bg_color; \
				border-width: 1px; \
				border-style: solid; \
				border-image: none; \
				border-color: @menuitem_child_bg_color; \
				color: @menuitem_text_color; \
			} \
			cdmenuitem .button, \
			cdmenuitem.button { \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				box-shadow: none; \
				border-image: none; \
				border-color: @menuitem_child_bg_color; \
				border-width: 1px; \
				border-style: solid;padding: 2px; \
			} \
			.gldimenuitem .scale, \
			.gldimenuitem.scale { \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				color: @menuitem_text_color; \
				border-width: 1px; \
				border-style: solid; \
				border-image: none; \
			} \
			.gldimenuitem .scale.left, \
			.gldimenuitem.scale.left { \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				border-image: none; \
			} \
			.gldimenuitem .scale.slider, \
			.gldimenuitem.scale.slider { \
				background-color: @menuitem_text_color; \
				background-image: none; \
				border-image: none; \
			} \
			.gldimenuitem GtkCalendar, \
			.gldimenuitem GtkCalendar.button, \
			.gldimenuitem GtkCalendar.header, \
			.gldimenuitem GtkCalendar.view { \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				color: @menuitem_text_color; \
			} \
			.gldimenuitem GtkCalendar { \
				background-color: @menuitem_child_bg_color; \
				background-image: none; \
			} \
			.gldimenuitem GtkCalendar:indeterminate { \
				color: shade(@menuitem_child_bg_color, 0.6); \
			} \
			.gldimenuitem .toolbar .button, \
			.gldimenuitem column-header .button  { \
				color: @menuitem_text_color; \
				text-shadow: none; \
			} \
			.gldimenuitem row { \
				color: @menuitem_text_color; \
				text-shadow: none; \
				background-color: @menu_bg_color; \
				background-image: none; \
			} \
			.gldimenuitem row:selected { \
				color: @menuitem_text_color; \
				text-shadow: none; \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				border-color: @menuitem_child_bg_color; \
			} \
			cdmenuitem check, \
			cdmenuitem radio { \
				color: @menuitem_text_color; \
				background-color: @menuitem_bg_color; \
				background-image: none; \
				border-width: 1px; \
				border-style: solid; \
				border-image: none; \
				border-color: @menuitem_child_bg_color; \
			} \
			.gldimenu { \
				background-color: @menu_bg_color; \
				background-image: none; \
				color: @menuitem_text_color; \
			} \
			.window-frame { \
				box-shadow: none; \
			}\n",
			cFontSize,
			NULL);  // we also define ".menu", so that custom widgets (like in the SoundMenu) can get our colors. Note that we don't redefine Gtk's menuitem, because we want to keep normal menus for GUI
			// for "entry", using "background-color" will not affect entries inside another widget (like a box), we actually have to use "background" ... (TBC with gtk > 3.6)
			// for ".window-frame": remove shadow added by some WMs (Marco/Metacity) to the menu (LP #1407880)
			
			g_free (cFontSize);
		}
		
		gldi_style_colors_freeze ();
		gtk_css_provider_load_from_string (cssProvider, css);  // (should) clear any previously loaded information
		gldi_style_colors_freeze ();
		g_free (cssheader);
		g_free (css);
	}
}

  ////////////////////
 /// RESET CONFIG ///
////////////////////

static void reset_config (CairoDialogsParam *pDialogs)
{
	g_free (pDialogs->cButtonOkImage);
	g_free (pDialogs->cButtonCancelImage);
	gldi_text_description_reset (&pDialogs->dialogTextDescription);
	g_free (pDialogs->cDecoratorName);
}

  ////////////
 /// LOAD ///
////////////

static void load (void)
{
	_init_menu_style ();
	// additional data are loaded the first time a dialog is created, to avoid create them for nothing.
}

  //////////////
 /// RELOAD ///
//////////////

static void reload (CairoDialogsParam *pPrevDialogs, CairoDialogsParam *pDialogs)
{
	if (g_strcmp0 (pPrevDialogs->cButtonOkImage, pDialogs->cButtonOkImage) != 0
	|| g_strcmp0 (pPrevDialogs->cButtonCancelImage, pDialogs->cButtonCancelImage) != 0
	|| pPrevDialogs->iDialogIconSize != pDialogs->iDialogIconSize)
	{
		_unload_dialog_buttons ();
		_load_dialog_buttons (pDialogs->cButtonOkImage, pDialogs->cButtonCancelImage);
	}
	
	if (pPrevDialogs->bUseDefaultColors != pDialogs->bUseDefaultColors
	|| ! pDialogs->bUseDefaultColors
	|| pPrevDialogs->fMenuFontScale != pDialogs->fMenuFontScale)
		on_style_changed (NULL);
}

  //////////////
 /// UNLOAD ///
//////////////

static void unload (void)
{
	_unload_dialog_buttons ();
}

  ////////////
 /// INIT ///
////////////

static void _reload_dialogs (void)
{
	GSList *d;
	CairoDialog *pDialog;
	for (d = s_pDialogList; d != NULL; d = d->next)
	{
		pDialog = d->data;
/*
		// re-set the GTK style class (global style may have changed between system / custom)
		GtkStyleContext *ctx = gtk_widget_get_style_context (pDialog->pWidgetLayout);

		gtk_style_context_remove_class (ctx, GTK_STYLE_CLASS_MENUITEM);
		gtk_style_context_remove_class (ctx, "gldimenuitem");

		gtk_style_context_add_class (ctx, myDialogsParam.bUseDefaultColors && myStyleParam.bUseSystemColors ? GTK_STYLE_CLASS_MENUITEM : "gldimenuitem");
*/
		// reload the text buffer (color or font may have changed)
		if (pDialog->cText != NULL)
		{
			gchar *cText = pDialog->cText;
			pDialog->cText = NULL;
			gldi_dialog_set_message (pDialog, cText);
			g_free (cText);
		}
	}
	
}
static gboolean on_style_changed (G_GNUC_UNUSED gpointer data)
{
	cd_debug ("Dialogs: , %d", myDialogsParam.bUseDefaultColors);

	// init the menu style (create the "gldimenuitem" gtk style class)
	_init_menu_style ();

	// update existing dialogs
	_reload_dialogs ();

	return GLDI_NOTIFICATION_LET_PASS;
}

static void init (void)
{
	gldi_object_register_notification (&myDialogObjectMgr,
		NOTIFICATION_RENDER,
		(GldiNotificationFunc) _cairo_dock_render_dialog_notification,
		GLDI_RUN_AFTER, NULL);
	gldi_object_register_notification (&myDockObjectMgr,
		NOTIFICATION_REMOVE_ICON,
		(GldiNotificationFunc) on_icon_removed,
		GLDI_RUN_AFTER, NULL);
	gldi_object_register_notification (&myStyleMgr,
		NOTIFICATION_STYLE_CHANGED,
		(GldiNotificationFunc) on_style_changed,
		GLDI_RUN_AFTER, NULL);
}

  ///////////////
 /// MANAGER ///
///////////////

static void init_object (GldiObject *obj, gpointer attr)
{
	CairoDialog *pDialog = (CairoDialog*)obj;
	CairoDialogAttr *pAttribute = (CairoDialogAttr*)attr;
	
	// set parent -- note: on Wayland, it is an error to try to map (and position) a popup
	// relative to a window that is not mapped; we need to take care of this
	GtkWidget *tmp = pAttribute->pContainer->pWidget;
	while (tmp && !gtk_widget_get_mapped (tmp))
		tmp = gtk_widget_get_parent (tmp);
	gtk_widget_set_parent (pDialog->container.pWidget, tmp);
	
	//\________________ set up its orientation (do it now, as we need bDirectionUp to place the internal widgets)
	pDialog->pIcon = pAttribute->pIcon;
	//!! TODO: called by _place_dialog() below, might not be necessary !!
	_set_dialog_orientation (pDialog, pAttribute->pContainer);  // renseigne aussi bDirectionUp, bIsHorizontal, et iHeight.
	
	gldi_dialog_init_internals (pDialog, pAttribute);
	
	GldiContainer *pContainer = pAttribute->pContainer;
	
	//\________________ Interactive dialogs are set modal, to be fixed.
	if ((pDialog->pInteractiveWidget || pDialog->pButtons || pAttribute->iTimeLength == 0) && ! pDialog->bNoInput)
	{
		gtk_popover_set_autohide (GTK_POPOVER (pDialog->container.pWidget), TRUE);
		if (CAIRO_DOCK_IS_DOCK (pContainer))
		{
			// to prevent the dock from hiding. We want to see it while the dialog is visible (a leave event will be emitted when it disappears).
			gldi_dock_enter_synthetic (CAIRO_DOCK (pContainer));
		}
	}
	if (CAIRO_DOCK_IS_DOCK (pContainer)) CAIRO_DOCK (pContainer)->bHasModalWindow = TRUE;
	pDialog->bHideOnClick = pAttribute->bHideOnClick;
	
	Icon *pIcon = pAttribute->pIcon;
	
	//\________________ register the dialog
	s_pDialogList = g_slist_prepend (s_pDialogList, pDialog);
	
	//\________________ load the button images
	if (pDialog->iNbButtons != 0 && (s_pButtonOkSurface == NULL || s_pButtonCancelSurface == NULL))
		_load_dialog_buttons (myDialogsParam.cButtonOkImage, myDialogsParam.cButtonCancelImage);
	
	//\________________ on le place parmi les autres.
	_place_dialog (pDialog, pContainer);  // renseigne aussi bDirectionUp, bIsHorizontal, et iHeight.
	
	//\________________ On connecte les signaux utiles.
	GtkGesture *pEventClick = gtk_gesture_click_new ();
	gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (pEventClick), 1); // only left button
	g_signal_connect (G_OBJECT (pEventClick), "pressed",
		G_CALLBACK (_on_button_press_dialog), pDialog);
	g_signal_connect (G_OBJECT (pEventClick), "released",
		G_CALLBACK (_on_button_release_dialog), pDialog);
	gtk_widget_add_controller (pDialog->container.pWidget, GTK_EVENT_CONTROLLER (pEventClick));
	
	GtkEventController *pEventKey = gtk_event_controller_key_new ();
	g_signal_connect (G_OBJECT (pEventKey), "key-pressed",
		G_CALLBACK (_on_key_press_dialog), pDialog);
	gtk_widget_add_controller (pDialog->container.pWidget, pEventKey);
	
	if (pIcon != NULL)  // on inhibe le deplacement du dialogue lorsque l'utilisateur est dedans.
	{
		GtkEventController *pEventMotion = gtk_event_controller_motion_new ();
		g_signal_connect (G_OBJECT (pEventMotion), "enter",
			G_CALLBACK (_on_enter_dialog), pDialog);
		g_signal_connect (G_OBJECT (pEventMotion), "leave",
			G_CALLBACK (_on_leave_dialog), pDialog);
		gtk_widget_add_controller (pDialog->container.pWidget, pEventMotion);
		gldi_object_register_notification (pIcon,
			NOTIFICATION_DESTROY,
			(GldiNotificationFunc) on_icon_destroyed,
			GLDI_RUN_AFTER, NULL);
	}
	
	//\ Finally show the dialog
	gtk_popover_popup (GTK_POPOVER (pDialog->container.pWidget));
	if (pDialog->pInteractiveWidget) gtk_widget_grab_focus (pDialog->pInteractiveWidget); //!! TODO: is this necessary?
	
	//\________________ schedule the auto-destruction
	if (pAttribute->iTimeLength != 0)
		pDialog->iSidTimer = g_timeout_add (pAttribute->iTimeLength, (GSourceFunc) _cairo_dock_dialog_auto_delete, (gpointer) pDialog);
}

static void reset_object (GldiObject *obj)
{
	CairoDialog *pDialog = (CairoDialog*)obj;
	
	gldi_dialog_leave (pDialog);
	
	// stop the timer
	if (pDialog->iSidTimer > 0)
	{
		g_source_remove (pDialog->iSidTimer);
	}
	
	gtk_widget_unparent (pDialog->container.pWidget);
	
	// destroy private data
	if (pDialog->pTextBuffer != NULL)
		cairo_surface_destroy (pDialog->pTextBuffer);
	if (pDialog->pIconBuffer != NULL)
		cairo_surface_destroy (pDialog->pIconBuffer);
	if (pDialog->iIconTexture != 0)
		_cairo_dock_delete_texture (pDialog->iIconTexture);
	if (pDialog->iTextTexture != 0)
		_cairo_dock_delete_texture (pDialog->iTextTexture);
	
	if (pDialog->pButtons != NULL)
	{
		cairo_surface_t *pSurface;
		GLuint iTexture;
		int i;
		for (i = 0; i < pDialog->iNbButtons; i++)
		{
			pSurface = pDialog->pButtons[i].pSurface;
			if (pSurface != NULL)
				cairo_surface_destroy (pSurface);
			iTexture = pDialog->pButtons[i].iTexture;
			if (iTexture != 0)
				_cairo_dock_delete_texture (iTexture);
		}
		g_free (pDialog->pButtons);
	}
	
	if (pDialog->pUnmapTimer != NULL)
		g_timer_destroy (pDialog->pUnmapTimer);
	
	if (pDialog->pShapeBitmap != NULL)
		cairo_region_destroy (pDialog->pShapeBitmap);
	
	// destroy user data
	if (pDialog->pUserData != NULL && pDialog->pFreeUserDataFunc != NULL)
		pDialog->pFreeUserDataFunc (pDialog->pUserData);
	
	if (!s_bInRefreshDialogs)
	{
		// unregister the dialog (note: if this function is called from refresh_dialogs, it was already removed from the list)
		s_pDialogList = g_slist_remove (s_pDialogList, pDialog);
		
		_trigger_replace_all_dialogs ();
	}
}

void gldi_register_dialogs_manager (void)
{
	// Manager
	memset (&myDialogsMgr, 0, sizeof (GldiManager));
	gldi_object_init (GLDI_OBJECT(&myDialogsMgr), &myManagerObjectMgr, NULL);
	myDialogsMgr.cModuleName  = "Dialogs";
	// interface
	myDialogsMgr.init         = init;
	myDialogsMgr.load         = load;
	myDialogsMgr.unload       = unload;
	myDialogsMgr.reload       = (GldiManagerReloadFunc)reload;
	myDialogsMgr.get_config   = (GldiManagerGetConfigFunc)get_config;
	myDialogsMgr.reset_config = (GldiManagerResetConfigFunc)reset_config;
	// Config
	memset (&myDialogsParam, 0, sizeof (CairoDialogsParam));
	myDialogsMgr.pConfig = (GldiManagerConfigPtr)&myDialogsParam;
	myDialogsMgr.iSizeOfConfig = sizeof (CairoDialogsParam);
	// data
	myDialogsMgr.iSizeOfData = 0;
	myDialogsMgr.pData = (GldiManagerDataPtr)NULL;
	
	// Object Manager
	memset (&myDialogObjectMgr, 0, sizeof (GldiObjectManager));
	myDialogObjectMgr.cName 	= "Dialog";
	myDialogObjectMgr.iObjectSize    = sizeof (CairoDialog);
	// interface
	myDialogObjectMgr.init_object    = init_object;
	myDialogObjectMgr.reset_object   = reset_object;
	// signals
	gldi_object_install_notifications (&myDialogObjectMgr, NB_NOTIFICATIONS_DIALOG);
	// parent object
	gldi_object_set_manager (GLDI_OBJECT (&myDialogObjectMgr), &myContainerObjectMgr);
}
