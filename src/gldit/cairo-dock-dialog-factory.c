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
#include <string.h>
#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h> // GDK_WINDOW_XID

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>

#include "cairo-dock-icon-factory.h"
#include "cairo-dock-icon-facility.h"
#include "cairo-dock-container.h"
#include "cairo-dock-image-buffer.h"
#include "cairo-dock-draw.h"
#include "cairo-dock-draw-opengl.h"
#include "cairo-dock-log.h"
#include "cairo-dock-desklet-factory.h"
#include "cairo-dock-dock-manager.h"
#include "cairo-dock-dock-facility.h"
#include "cairo-dock-backends-manager.h"
#include "cairo-dock-surface-factory.h"
#include "cairo-dock-animations.h"
#include "cairo-dock-notifications.h"
#include "cairo-dock-callbacks.h"
#include "cairo-dock-launcher-manager.h"
#include "cairo-dock-gui-manager.h"
#include "cairo-dock-applications-manager.h"
#include "cairo-dock-X-manager.h"
#include "cairo-dock-X-utilities.h"  // cairo_dock_set_xwindow_type_hint
#include "cairo-dock-dialog-manager.h"
#include "cairo-dock-dialog-factory.h"

extern gboolean g_bUseOpenGL;
extern CairoDockHidingEffect *g_pHidingBackend;  // cairo_dock_is_hidden

#define _drawn_text_width(pDialog) (pDialog)->iTextWidth

static void _cairo_dock_compute_dialog_sizes (CairoDialog *pDialog)
{
	pDialog->iMessageWidth = pDialog->iIconSize + _drawn_text_width (pDialog) + (pDialog->iTextWidth != 0 ? 2 : 0) * CAIRO_DIALOG_TEXT_MARGIN - pDialog->iIconOffsetX;  // icone + marge + texte + marge.
	pDialog->iMessageHeight = MAX (pDialog->iIconSize - pDialog->iIconOffsetY, pDialog->iTextHeight) + (pDialog->pInteractiveWidget != NULL ? CAIRO_DIALOG_VGAP : 0);  // (icone/texte + marge) + widget + (marge + boutons) + pointe.
	
	if (pDialog->pButtons != NULL)
	{
		pDialog->iButtonsWidth = pDialog->iNbButtons * myDialogsParam.iDialogButtonWidth + (pDialog->iNbButtons - 1) * CAIRO_DIALOG_BUTTON_GAP + 2 * CAIRO_DIALOG_TEXT_MARGIN;  // marge + bouton1 + ecart + bouton2 + marge.
		pDialog->iButtonsHeight = CAIRO_DIALOG_VGAP + myDialogsParam.iDialogButtonHeight;  // il y'a toujours quelque chose au-dessus (texte et/ou widget)
	}
	
	pDialog->iBubbleWidth = MAX (pDialog->iInteractiveWidth, MAX (pDialog->iButtonsWidth, MAX (pDialog->iMessageWidth, pDialog->iMinFrameWidth)));
	pDialog->iBubbleHeight = pDialog->iMessageHeight + pDialog->iInteractiveHeight + pDialog->iButtonsHeight;
	if (pDialog->iBubbleWidth == 0)  // precaution.
		pDialog->iBubbleWidth = 20;
	if (pDialog->iBubbleHeight == 0)
		pDialog->iBubbleHeight = 10;
	
	pDialog->iComputedWidth = pDialog->iLeftMargin + pDialog->iBubbleWidth + pDialog->iRightMargin;
	pDialog->iComputedHeight = pDialog->iTopMargin + pDialog->iBubbleHeight + pDialog->iBottomMargin + pDialog->iMinBottomGap;  // all included.
	
	pDialog->container.iWidth = pDialog->iComputedWidth;
	pDialog->container.iHeight = pDialog->iComputedHeight;
}

static gboolean on_expose_dialog (G_GNUC_UNUSED GtkWidget *pWidget,
#if (GTK_MAJOR_VERSION < 3)
	GdkEventExpose *pExpose,
#else
	cairo_t *ctx,
#endif
	CairoDialog *pDialog)
{
	//g_print ("%s (%dx%d ; %d;%d)\n", __func__, pDialog->container.iWidth, pDialog->container.iHeight, pExpose->area.x, pExpose->area.y);
	/* int x, y;
	// OpenGL renderers are not ready for dialogs.
	if (g_bUseOpenGL && (pDialog->pDecorator == NULL || pDialog->pDecorator->render_opengl != NULL) && (pDialog->pRenderer == NULL || pDialog->pRenderer->render_opengl != NULL))
	{
		if (! gldi_glx_begin_draw_container (CAIRO_CONTAINER (pDialog)))
			return FALSE;
		
		if (pDialog->pDecorator != NULL && pDialog->pDecorator->render_opengl != NULL)
		{
			glPushMatrix ();
			pDialog->pDecorator->render_opengl (pDialog);
			glPopMatrix ();
		}
		
		cairo_dock_notify_on_object (pDialog, NOTIFICATION_RENDER, pDialog, NULL);
		
		gldi_glx_end_draw_container (CAIRO_CONTAINER (pDialog));
	}
	else
	{*/
		cairo_t *pCairoContext;
		
		GdkRectangle area;
		#if (GTK_MAJOR_VERSION < 3)
		memcpy (&area, &pExpose->area, sizeof (GdkRectangle));
		#else
		double x1, x2, y1, y2;
		cairo_clip_extents (ctx, &x1, &y1, &x2, &y2);
		area.x = x1;
		area.y = y1;
		area.width = x2 - x1;
		area.height = y2 - y1;  /// or the opposite ?...
		#endif
		
		if (area.x != 0 || area.y != 0)
		{
			pCairoContext = cairo_dock_create_drawing_context_on_area (CAIRO_CONTAINER (pDialog), &area, myDialogsParam.fDialogColor);
		}
		else
		{
			pCairoContext = cairo_dock_create_drawing_context_on_container (CAIRO_CONTAINER (pDialog));
		}
		
		if (pDialog->pDecorator != NULL)
		{
			cairo_save (pCairoContext);
			pDialog->pDecorator->render (pCairoContext, pDialog);
			cairo_restore (pCairoContext);
		}
		
		cairo_dock_notify_on_object (pDialog, NOTIFICATION_RENDER, pDialog, pCairoContext);
		
		if (pDialog->fAppearanceCounter < 1.)
		{
			double fAlpha = pDialog->fAppearanceCounter * pDialog->fAppearanceCounter;
			cairo_rectangle (pCairoContext,
				0,
				0,
				pDialog->container.iWidth,
				pDialog->container.iHeight);
			cairo_set_line_width (pCairoContext, 0);
			cairo_set_operator (pCairoContext, CAIRO_OPERATOR_DEST_OUT);
			cairo_set_source_rgba (pCairoContext, 0.0, 0.0, 0.0, 1. - fAlpha);
			cairo_fill (pCairoContext);
		}
		
		cairo_destroy (pCairoContext);
	//}
	return FALSE;
}

static void _cairo_dock_set_dialog_input_shape (CairoDialog *pDialog)
{
	if (pDialog->pShapeBitmap != NULL)
		gldi_shape_destroy (pDialog->pShapeBitmap);
	
	pDialog->pShapeBitmap = gldi_container_create_input_shape (CAIRO_CONTAINER (pDialog),
		0,
		0,
		1,
		1);  // workaround a bug in X with fully transparent window => let 1 pixel ON.

	gldi_container_set_input_shape (CAIRO_CONTAINER (pDialog), pDialog->pShapeBitmap);
}

static gboolean on_configure_dialog (G_GNUC_UNUSED GtkWidget* pWidget,
	GdkEventConfigure* pEvent,
	CairoDialog *pDialog)
{
	//g_print ("%s (%dx%d, %d;%d) [%d]\n", __func__, pEvent->width, pEvent->height, pEvent->x, pEvent->y, pDialog->bPositionForced);
	if (pEvent->width <= CAIRO_DIALOG_MIN_SIZE && pEvent->height <= CAIRO_DIALOG_MIN_SIZE && ! pDialog->bNoInput)
	{
		pDialog->container.bInside = FALSE;
		return FALSE;
	}
	
	//\____________ get dialog size and position.
	int iPrevWidth = pDialog->container.iWidth, iPrevHeight = pDialog->container.iHeight;
	pDialog->container.iWidth = pEvent->width;
	pDialog->container.iHeight = pEvent->height;
	pDialog->container.iWindowPositionX = pEvent->x;
	pDialog->container.iWindowPositionY = pEvent->y;
	
	//\____________ if an interactive widget is present, internal sizes may have changed.
	if (pDialog->pInteractiveWidget != NULL)
	{
		int w = pDialog->iInteractiveWidth, h = pDialog->iInteractiveHeight;
		GtkRequisition requisition;
		#if (GTK_MAJOR_VERSION < 3)
		gtk_widget_size_request (pDialog->pInteractiveWidget, &requisition);
		#else
		gtk_widget_get_preferred_size (pDialog->pInteractiveWidget, &requisition, NULL);
		#endif
		pDialog->iInteractiveWidth = requisition.width;
		pDialog->iInteractiveHeight = requisition.height;
		//g_print ("  pInteractiveWidget : %dx%d\n", pDialog->iInteractiveWidth, pDialog->iInteractiveHeight);
		_cairo_dock_compute_dialog_sizes (pDialog);
		
		if (w != pDialog->iInteractiveWidth || h != pDialog->iInteractiveHeight)
		{
			cairo_dock_replace_all_dialogs ();
			/*Icon *pIcon = pDialog->pIcon;
			if (pIcon != NULL)
			{
				CairoContainer *pContainer = cairo_dock_search_container_from_icon (pIcon);
				cairo_dock_place_dialog (pDialog, pContainer);
			}*/
		}
	}
	//g_print ("dialog size: %dx%d / %dx%d\n", pEvent->width, pEvent->height, pDialog->iComputedWidth, pDialog->iComputedHeight);
	
	//\____________ set input shape if size has changed or if no shape yet.
	if (pDialog->bNoInput && (iPrevWidth != pEvent->width || iPrevHeight != pEvent->height || ! pDialog->pShapeBitmap))
	{
		_cairo_dock_set_dialog_input_shape (pDialog);
		pDialog->container.bInside = FALSE;
	}
	
	//\____________ force position for buggy WM (Compiz).
	if (pDialog->iComputedWidth == pEvent->width && pDialog->iComputedHeight == pEvent->height && (pEvent->y != pDialog->iComputedPositionY || pEvent->x != pDialog->iComputedPositionX) && pDialog->bPositionForced == 3)
	{
		pDialog->container.bInside = FALSE;
		cd_debug ("force to %d;%d", pDialog->iComputedPositionX, pDialog->iComputedPositionY);
		/*gtk_window_move (GTK_WINDOW (pDialog->container.pWidget),
			pDialog->iComputedPositionX,
			pDialog->iComputedPositionY);
		*/pDialog->bPositionForced ++;
	}
	
	gtk_widget_queue_draw (pDialog->container.pWidget);  // les widgets internes peuvent avoir changer de taille sans que le dialogue n'en ait change, il faut donc redessiner tout le temps.

	return FALSE;
}

static gboolean on_unmap_dialog (GtkWidget* pWidget,
	G_GNUC_UNUSED GdkEvent *pEvent,
	CairoDialog *pDialog)
{
	//g_print ("unmap dialog (bAllowMinimize:%d, visible:%d)\n", pDialog->bAllowMinimize, GTK_WIDGET_VISIBLE (pWidget));
	if (! pDialog->bAllowMinimize)
	{
		if (pDialog->pUnmapTimer)
		{
			double fElapsedTime = g_timer_elapsed (pDialog->pUnmapTimer, NULL);
			//g_print ("fElapsedTime : %fms\n", fElapsedTime);
			g_timer_destroy (pDialog->pUnmapTimer);
			pDialog->pUnmapTimer = NULL;
			if (fElapsedTime < .2)
				return TRUE;
		}
		gtk_window_present (GTK_WINDOW (pWidget));
	}
	else
	{
		pDialog->bAllowMinimize = FALSE;
		if (pDialog->pUnmapTimer == NULL)
			pDialog->pUnmapTimer = g_timer_new ();  // starts the timer.
	}
	return TRUE;  // stops other handlers from being invoked for the event.
}

static GtkWidget *_cairo_dock_add_dialog_internal_box (CairoDialog *pDialog, int iWidth, int iHeight, gboolean bCanResize)
{
	GtkWidget *pBox = _gtk_hbox_new (0);
	if (iWidth != 0 && iHeight != 0)
		g_object_set (pBox, "height-request", iHeight, "width-request", iWidth, NULL);
	else if (iWidth != 0)
			g_object_set (pBox, "width-request", iWidth, NULL);
	else if (iHeight != 0)
			g_object_set (pBox, "height-request", iHeight, NULL);
	gtk_box_pack_start (GTK_BOX (pDialog->pWidgetLayout),
		pBox,
		bCanResize,
		bCanResize,
		0);
	return pBox;
}

static gboolean _cairo_dialog_animation_loop (CairoContainer *pContainer)
{
	CairoDialog *pDialog = CAIRO_DIALOG (pContainer);
	gboolean bContinue = FALSE;
	gboolean bUpdateSlowAnimation = FALSE;
	pContainer->iAnimationStep ++;
	if (pContainer->iAnimationStep * pContainer->iAnimationDeltaT >= CAIRO_DOCK_MIN_SLOW_DELTA_T)
	{
		bUpdateSlowAnimation = TRUE;
		pContainer->iAnimationStep = 0;
		pContainer->bKeepSlowAnimation = FALSE;
	}
	
	if (pDialog->fAppearanceCounter < 1)
	{
		pDialog->fAppearanceCounter += .08;
		if (pDialog->fAppearanceCounter > .99)
		{
			pDialog->fAppearanceCounter = 1.;
		}
		else
		{
			bContinue = TRUE;
		}
	}
	
	if (bUpdateSlowAnimation)
	{
		cairo_dock_notify_on_object (pDialog, NOTIFICATION_UPDATE_SLOW, pDialog, &pContainer->bKeepSlowAnimation);
	}
	
	cairo_dock_notify_on_object (pDialog, NOTIFICATION_UPDATE, pDialog, &bContinue);
	
	cairo_dock_redraw_container (CAIRO_CONTAINER (pDialog));
	if (! bContinue && ! pContainer->bKeepSlowAnimation)
	{
		pContainer->iSidGLAnimation = 0;
		return FALSE;
	}
	else
		return TRUE;
}

static CairoDialog *_cairo_dock_create_empty_dialog (gboolean bInteractive)
{
	//\________________ create a dialog.
	CairoDialog *pDialog = gldi_container_new_full (CairoDialog, &myDialogsMgr, CAIRO_DOCK_TYPE_DIALOG, FALSE);  // FALSE <=> no opengl
	
	//\__________________ initialize its parameters
	pDialog->container.fRatio = 1.;
	pDialog->container.bIsHorizontal = TRUE;
	pDialog->container.iface.animation_loop = _cairo_dialog_animation_loop;
	pDialog->iRefCount = 1;
	
	//\________________ set up the window.
	GtkWidget *pWindow = pDialog->container.pWidget;
	gtk_window_set_title (GTK_WINDOW (pWindow), "cairo-dock-dialog");
	if (! bInteractive)
		gtk_window_set_type_hint (GTK_WINDOW (pDialog->container.pWidget), GDK_WINDOW_TYPE_HINT_SPLASHSCREEN);  // pour ne pas prendre le focus.
	
	gtk_widget_add_events (pWindow, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
	gtk_window_resize (GTK_WINDOW (pWindow), CAIRO_DIALOG_MIN_SIZE, CAIRO_DIALOG_MIN_SIZE);
	gtk_window_set_keep_above (GTK_WINDOW (pWindow), TRUE);
	
	return pDialog;
}

static cairo_surface_t *_cairo_dock_create_dialog_text_surface (const gchar *cText, gboolean bUseMarkup, int *iTextWidth, int *iTextHeight)
{
	if (cText == NULL)
		return NULL;
	
	myDialogsParam.dialogTextDescription.bUseMarkup = bUseMarkup;  // slight optimization, rather than duplicating the TextDescription each time.
	cairo_surface_t *pSurface = cairo_dock_create_surface_from_text (cText,
		&myDialogsParam.dialogTextDescription,
		iTextWidth,
		iTextHeight);
	myDialogsParam.dialogTextDescription.bUseMarkup = FALSE;  // by default
	return pSurface;
}

static cairo_surface_t *_cairo_dock_create_dialog_icon_surface (const gchar *cImageFilePath, int iNbFrames, Icon *pIcon, CairoContainer *pContainer, int iDesiredSize, int *iIconSize)
{
	if (cImageFilePath == NULL)
		return NULL;
	if (iDesiredSize == 0)
		iDesiredSize = myDialogsParam.iDialogIconSize;
	cairo_surface_t *pIconBuffer = NULL;
	if (strcmp (cImageFilePath, "same icon") == 0)
	{
		if (pIcon && pIcon->image.pSurface)
		{
			if (pContainer == NULL)
				pContainer = cairo_dock_search_container_from_icon (pIcon);
			int iWidth, iHeight;
			cairo_dock_get_icon_extent (pIcon, &iWidth, &iHeight);
			pIconBuffer = cairo_dock_duplicate_surface (pIcon->image.pSurface,
				iWidth, iHeight,
				iDesiredSize, iDesiredSize);
		}
		else if (pIcon && pIcon->cFileName)
		{
			pIconBuffer = cairo_dock_create_surface_from_image_simple (pIcon->cFileName,
				iDesiredSize,
				iDesiredSize);
		}
	}
	else
	{
		double fImageWidth = iNbFrames * iDesiredSize, fImageHeight = iDesiredSize;
		pIconBuffer = cairo_dock_create_surface_from_image_simple (cImageFilePath,
			fImageWidth,
			fImageHeight);
	}
	if (pIconBuffer != NULL)
		*iIconSize = iDesiredSize;
	return pIconBuffer;
}

static gboolean _cairo_dock_animate_dialog_icon (CairoDialog *pDialog)
{
	pDialog->iCurrentFrame ++;
	if (pDialog->iCurrentFrame == pDialog->iNbFrames)
		pDialog->iCurrentFrame = 0;
	cairo_dock_damage_icon_dialog (pDialog);
	return TRUE;
}
static gboolean on_button_press_widget (G_GNUC_UNUSED GtkWidget *widget,
	GdkEventButton *pButton,
	CairoDialog *pDialog)
{
	cd_debug ("press button on widget");
	// memorize the time when the user clicked on the widget.
	pDialog->iButtonPressTime = pButton->time;
	return FALSE;
}
static void _force_above (G_GNUC_UNUSED GtkWidget *pWidget, CairoDialog *pDialog)
{
	gtk_window_set_keep_above (GTK_WINDOW (pDialog->container.pWidget), TRUE);
	Window Xid = gldi_container_get_Xid (CAIRO_CONTAINER (pDialog));
	cairo_dock_set_xwindow_type_hint (Xid, "_NET_WM_WINDOW_TYPE_DOCK");  // pour passer devant les fenetres plein ecran; depend du WM.
}
CairoDialog *cairo_dock_new_dialog (CairoDialogAttribute *pAttribute, Icon *pIcon, CairoContainer *pContainer)
{
	//\________________ create an empty dialog.
	CairoDialog *pDialog = _cairo_dock_create_empty_dialog (pAttribute->pInteractiveWidget || pAttribute->pActionFunc);
	pDialog->pIcon = pIcon;
	if (pAttribute->bForceAbove)
	{
		g_signal_connect (G_OBJECT (pDialog->container.pWidget),
			"realize",
			G_CALLBACK (_force_above),
			pDialog);  // gtk_widget_get_window() returns NULL until the window is realized.
	}
	
	//\________________ On cree la surface du message.
	if (pAttribute->cText != NULL)
	{
		pDialog->pTextBuffer = _cairo_dock_create_dialog_text_surface (pAttribute->cText,
			pAttribute->bUseMarkup,
			&pDialog->iTextWidth, &pDialog->iTextHeight);
		///pDialog->iTextTexture = cairo_dock_create_texture_from_surface (pDialog->pTextBuffer);
	}
	pDialog->bUseMarkup = pAttribute->bUseMarkup;  // remember this attribute, in case another text is set (with cairo_dock_set_dialog_message).

	//\________________ On cree la surface de l'icone a afficher sur le cote.
	if (pAttribute->cImageFilePath != NULL)
	{
		pDialog->iNbFrames = (pAttribute->iNbFrames > 0 ? pAttribute->iNbFrames : 1);
		pDialog->pIconBuffer = _cairo_dock_create_dialog_icon_surface (pAttribute->cImageFilePath, pDialog->iNbFrames, pIcon, pContainer, pAttribute->iIconSize, &pDialog->iIconSize);
		///pDialog->iIconTexture = cairo_dock_create_texture_from_surface (pDialog->pIconBuffer);
		if (pDialog->pIconBuffer != NULL && pDialog->iNbFrames > 1)
		{
			pDialog->iSidAnimateIcon = g_timeout_add (100, (GSourceFunc) _cairo_dock_animate_dialog_icon, (gpointer) pDialog);
		}
	}

	//\________________ On prend en compte le widget interactif.
	if (pAttribute->pInteractiveWidget != NULL)
	{
		pDialog->pInteractiveWidget = pAttribute->pInteractiveWidget;
		
		GtkRequisition requisition;
		#if (GTK_MAJOR_VERSION < 3)
		gtk_widget_size_request (pAttribute->pInteractiveWidget, &requisition);
		#else
		gtk_widget_get_preferred_size (pAttribute->pInteractiveWidget, &requisition, NULL);
		#endif
		pDialog->iInteractiveWidth = requisition.width;
		pDialog->iInteractiveHeight = requisition.height;
	}
	
	//\________________ On prend en compte les boutons.
	pDialog->pUserData = pAttribute->pUserData;
	pDialog->pFreeUserDataFunc = pAttribute->pFreeDataFunc;
	if (pAttribute->cButtonsImage != NULL && pAttribute->pActionFunc != NULL)
	{
		int i;
		for (i = 0; pAttribute->cButtonsImage[i] != NULL; i++);
		
		pDialog->iNbButtons = i;
		pDialog->action_on_answer = pAttribute->pActionFunc;
		pDialog->pButtons = g_new0 (CairoDialogButton, pDialog->iNbButtons);
		const gchar *cButtonImage;
		for (i = 0; i < pDialog->iNbButtons; i++)
		{
			cButtonImage = pAttribute->cButtonsImage[i];
			if (strcmp (cButtonImage, "ok") == 0)
			{
				pDialog->pButtons[i].iDefaultType = 1;
			}
			else if (strcmp (cButtonImage, "cancel") == 0)
			{
				pDialog->pButtons[i].iDefaultType = 0;
			}
			else
			{
				gchar *cButtonPath;
				if (*cButtonImage != '/')
					cButtonPath = cairo_dock_search_icon_s_path (cButtonImage,
						MAX (myDialogsParam.iDialogButtonWidth, myDialogsParam.iDialogButtonHeight));
				else
					cButtonPath = (gchar*)cButtonImage;
				pDialog->pButtons[i].pSurface = cairo_dock_create_surface_from_image_simple (cButtonPath,
					myDialogsParam.iDialogButtonWidth,
					myDialogsParam.iDialogButtonHeight);
				if (cButtonPath != cButtonImage)
					g_free (cButtonPath);
				///pDialog->pButtons[i].iTexture = cairo_dock_create_texture_from_surface (pDialog->pButtons[i].pSurface);
			}
		}
	}
	else
	{
		pDialog->bNoInput = pAttribute->bNoInput;
	}
	
	//\________________ Interactive dialogs are set modal, to be fixed.
	if ((pDialog->pInteractiveWidget || pDialog->pButtons || pAttribute->iTimeLength == 0) && ! pDialog->bNoInput)
	{
		gtk_window_set_modal (GTK_WINDOW (pDialog->container.pWidget), TRUE);  // Note: there is a bug in Ubuntu version of GTK: gtkscrolledwindow in dialog breaks his modality (http://www.gtkforums.com/viewtopic.php?f=3&t=55727, LP: https://bugs.launchpad.net/ubuntu/+source/overlay-scrollbar/+bug/903302).
		if (CAIRO_DOCK_IS_DOCK (pContainer))
		{
			CAIRO_DOCK (pContainer)->bHasModalWindow = TRUE;
			cairo_dock_emit_enter_signal (pContainer);  // to prevent the dock from hiding. We want to see it until the dialog is visible (a leave event will be emited when it disappears).
		}
	}
	pDialog->bHideOnClick = pAttribute->bHideOnClick;
	
	//\________________ On lui affecte un decorateur.
	cairo_dock_set_dialog_decorator_by_name (pDialog, (pAttribute->cDecoratorName ? pAttribute->cDecoratorName : myDialogsParam.cDecoratorName));
	if (pDialog->pDecorator != NULL)
		pDialog->pDecorator->set_size (pDialog);
	
	//\________________ Maintenant qu'on connait tout, on calcule les tailles des divers elements.
	_cairo_dock_compute_dialog_sizes (pDialog);
	pDialog->container.iWidth = pDialog->iBubbleWidth + pDialog->iLeftMargin + pDialog->iRightMargin;
	pDialog->container.iHeight = pDialog->iBubbleHeight + pDialog->iTopMargin + pDialog->iBottomMargin + pDialog->iMinBottomGap;
	
	//\________________ On definit son orientation.
	cairo_dock_set_dialog_orientation (pDialog, pContainer);  // renseigne aussi bDirectionUp, bIsHorizontal, et iHeight.
	
	//\________________ On reserve l'espace pour les decorations.
	GtkWidget *pMainHBox = _gtk_hbox_new (0);
	gtk_container_add (GTK_CONTAINER (pDialog->container.pWidget), pMainHBox);
	pDialog->pLeftPaddingBox = _gtk_vbox_new (0);
	g_object_set (pDialog->pLeftPaddingBox, "width-request", pDialog->iLeftMargin, NULL);
	gtk_box_pack_start (GTK_BOX (pMainHBox),
		pDialog->pLeftPaddingBox,
		FALSE,
		FALSE,
		0);

	pDialog->pWidgetLayout = _gtk_vbox_new (0);
	gtk_box_pack_start (GTK_BOX (pMainHBox),
		pDialog->pWidgetLayout,
		FALSE,
		FALSE,
		0);

	pDialog->pRightPaddingBox = _gtk_vbox_new (0);
	g_object_set (pDialog->pRightPaddingBox, "width-request", pDialog->iRightMargin, NULL);
	gtk_box_pack_start (GTK_BOX (pMainHBox),
		pDialog->pRightPaddingBox,
		FALSE,
		FALSE,
		0);
	
	//\________________ On reserve l'espace pour les elements.
	if (pDialog->container.bDirectionUp)
		pDialog->pTopWidget = _cairo_dock_add_dialog_internal_box (pDialog, 0, pDialog->iTopMargin, FALSE);
	else
		pDialog->pTipWidget = _cairo_dock_add_dialog_internal_box (pDialog, 0, pDialog->iMinBottomGap + pDialog->iBottomMargin, TRUE);
	if (pDialog->iMessageWidth != 0 && pDialog->iMessageHeight != 0)
	{
		pDialog->pMessageWidget = _cairo_dock_add_dialog_internal_box (pDialog, pDialog->iMessageWidth, pDialog->iMessageHeight, FALSE);
	}
	if (pDialog->pInteractiveWidget != NULL)
	{
		gtk_box_pack_start (GTK_BOX (pDialog->pWidgetLayout),
			pDialog->pInteractiveWidget,
			FALSE,
			FALSE,
			0);
		cd_debug ("grab focus");
		gtk_window_present (GTK_WINDOW (pDialog->container.pWidget));
		gtk_widget_grab_focus (pDialog->pInteractiveWidget);
	}
	if (pDialog->pButtons != NULL)
	{
		pDialog->pButtonsWidget = _cairo_dock_add_dialog_internal_box (pDialog, pDialog->iButtonsWidth, pDialog->iButtonsHeight, FALSE);
	}
	if (pDialog->container.bDirectionUp)
		pDialog->pTipWidget = _cairo_dock_add_dialog_internal_box (pDialog, 0, pDialog->iMinBottomGap + pDialog->iBottomMargin, TRUE);
	else
		pDialog->pTopWidget = _cairo_dock_add_dialog_internal_box (pDialog, 0, pDialog->iTopMargin, TRUE);
	
	gtk_widget_show_all (pDialog->container.pWidget);
	
	if (pDialog->bNoInput)
	{
		_cairo_dock_set_dialog_input_shape (pDialog);
	}
	//\________________ On connecte les signaux utiles.
	g_signal_connect (G_OBJECT (pDialog->container.pWidget),
		#if (GTK_MAJOR_VERSION < 3)
		"expose-event",
		#else
		"draw",
		#endif
		G_CALLBACK (on_expose_dialog),
		pDialog);
	g_signal_connect (G_OBJECT (pDialog->container.pWidget),
		"configure-event",
		G_CALLBACK (on_configure_dialog),
		pDialog);
	g_signal_connect (G_OBJECT (pDialog->container.pWidget),
		"unmap-event",
		G_CALLBACK (on_unmap_dialog),
		pDialog);
	if (pDialog->pInteractiveWidget != NULL && pDialog->pButtons == NULL)  // the dialog has no button to be closed, so it can be closed by clicking on it. But some widget (like the GTK calendar) let pass the click to their parent (= the dialog), which then close it. To prevent this, we memorize the last click on the widget.
		g_signal_connect (G_OBJECT (pDialog->pInteractiveWidget),
			"button-press-event",
			G_CALLBACK (on_button_press_widget),
			pDialog);
		
	cairo_dock_launch_animation (CAIRO_CONTAINER (pDialog));
	
	return pDialog;
}

void cairo_dock_free_dialog (CairoDialog *pDialog)
{
	if (pDialog == NULL)
		return ;
	
	if (pDialog->iSidTimer > 0)
	{
		g_source_remove (pDialog->iSidTimer);
	}
	if (pDialog->iSidAnimateIcon > 0)
	{
		g_source_remove (pDialog->iSidAnimateIcon);
	}
	
	cd_debug ("");

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
	
	cairo_dock_finish_container (CAIRO_CONTAINER (pDialog));
	
	if (pDialog->pUnmapTimer != NULL)
		g_timer_destroy (pDialog->pUnmapTimer);
	
	if (pDialog->pShapeBitmap != NULL)
		gldi_shape_destroy (pDialog->pShapeBitmap);
	
	if (pDialog->pUserData != NULL && pDialog->pFreeUserDataFunc != NULL)
		pDialog->pFreeUserDataFunc (pDialog->pUserData);
	
	g_free (pDialog);
}


static void _cairo_dock_dialog_calculate_aimed_point (Icon *pIcon, CairoContainer *pContainer, int *iX, int *iY, gboolean *bRight, gboolean *bIsHorizontal, gboolean *bDirectionUp, double fAlign)
{
	g_return_if_fail (/*pIcon != NULL && */pContainer != NULL);
	//g_print ("%s (%.2f, %.2f)\n", __func__, pIcon?pIcon->fXAtRest:0, pIcon?pIcon->fDrawX:0);
	if (CAIRO_DOCK_IS_DOCK (pContainer))
	{
		CairoDock *pDock = CAIRO_DOCK (pContainer);
		if (pDock->iRefCount > 0 && ! gldi_container_is_visible (pContainer))  // sous-dock invisible.
		{
			CairoDock *pParentDock = NULL;
			Icon *pPointingIcon = cairo_dock_search_icon_pointing_on_dock (pDock, &pParentDock);
			_cairo_dock_dialog_calculate_aimed_point (pPointingIcon, CAIRO_CONTAINER (pParentDock), iX, iY, bRight, bIsHorizontal, bDirectionUp, fAlign);
		}
		else  // dock principal ou sous-dock visible.
		{
			*bIsHorizontal = (pContainer->bIsHorizontal == CAIRO_DOCK_HORIZONTAL);
			if (! *bIsHorizontal)
			{
				int *tmp = iX;
				iX = iY;
				iY = tmp;
			}
			int dy;
			if (pDock->iInputState == CAIRO_DOCK_INPUT_ACTIVE)
				dy = pContainer->iHeight - pDock->iActiveHeight;
			else if (cairo_dock_is_hidden (pDock))
				dy = pContainer->iHeight-1;  // on laisse 1 pixels pour pouvoir sortir du dialogue avant de toucher le bord de l'ecran, et ainsi le faire se replacer, lorsqu'on fait apparaitre un dock en auto-hide.
			else
				dy = pContainer->iHeight - pDock->iMinDockHeight;
			if (pContainer->bIsHorizontal)
			{
				*bRight = (pIcon ? pIcon->fXAtRest < pDock->fFlatDockWidth / 2 : TRUE);
				*bDirectionUp = pContainer->bDirectionUp;
				
				if (*bDirectionUp)
					*iY = pContainer->iWindowPositionY + dy;
				else
					*iY = pContainer->iWindowPositionY + pContainer->iHeight - dy;
			}
			else
			{
				*bRight = (pContainer->iWindowPositionY > gldi_get_desktop_width() / 2);  // we don't know if the container is set on a given screen or not, so take the X screen.
				*bDirectionUp = (pIcon ? pIcon->fXAtRest > pDock->fFlatDockWidth / 2 : TRUE);
				*iY = (pContainer->bDirectionUp ?
					pContainer->iWindowPositionY + dy :
					pContainer->iWindowPositionY + pContainer->iHeight - dy);
			}
			
			if (cairo_dock_is_hidden (pDock))
			{
				*iX = pContainer->iWindowPositionX +
					pDock->iMaxDockWidth/2
					- pDock->fFlatDockWidth/2
					+ pIcon->fXAtRest + pIcon->fWidth/2;
					///(pIcon ? (pIcon->fXAtRest + pIcon->fWidth/2) / pDock->fFlatDockWidth * pDock->iMaxDockWidth : 0);
			}
			else
			{
				*iX = pContainer->iWindowPositionX +
					(pIcon ? pIcon->fDrawX + pIcon->fWidth * pIcon->fScale/2 : 0);
			}
		}
	}
	else if (CAIRO_DOCK_IS_DESKLET (pContainer))
	{
		*bDirectionUp = (pContainer->iWindowPositionY > gldi_get_desktop_height() / 2);
		///*bIsHorizontal = (pContainer->iWindowPositionX > 50 && pContainer->iWindowPositionX + pContainer->iHeight < gldi_get_desktop_width() - 50);
		*bIsHorizontal = TRUE;
		
		if (*bIsHorizontal)
		{
			*bRight = (pContainer->iWindowPositionX + pContainer->iWidth/2 < gldi_get_desktop_width() / 2);
			*iX = pContainer->iWindowPositionX + pContainer->iWidth * (*bRight ? .7 : .3);
			*iY = (*bDirectionUp ? pContainer->iWindowPositionY : pContainer->iWindowPositionY + pContainer->iHeight);
		}
		else
		{
			*bRight = (pContainer->iWindowPositionX < gldi_get_desktop_width() / 2);
			*iY = pContainer->iWindowPositionX + pContainer->iWidth * (*bRight ? 1 : 0);
			*iX =pContainer->iWindowPositionY + pContainer->iHeight / 2;
		}
	}
}


void cairo_dock_set_dialog_orientation (CairoDialog *pDialog, CairoContainer *pContainer)
{
	if (pContainer != NULL/* && pDialog->pIcon != NULL*/)
	{
		_cairo_dock_dialog_calculate_aimed_point (pDialog->pIcon, pContainer, &pDialog->iAimedX, &pDialog->iAimedY, &pDialog->bRight, &pDialog->bTopBottomDialog, &pDialog->container.bDirectionUp, pDialog->fAlign);
		//g_print ("%s (%d,%d %d %d %d)\n", __func__, pDialog->iAimedX, pDialog->iAimedY, pDialog->bRight, pDialog->bTopBottomDialog, pDialog->container.bDirectionUp);
	}
	else
	{
		pDialog->container.bDirectionUp = TRUE;
	}
}


GtkWidget *cairo_dock_steal_widget_from_its_container (GtkWidget *pWidget)
{
	g_return_val_if_fail (pWidget != NULL, NULL);
	GtkWidget *pContainer = gtk_widget_get_parent (pWidget);
	if (pContainer != NULL)
	{
		g_object_ref (G_OBJECT (pWidget));
		gtk_container_remove (GTK_CONTAINER (pContainer), pWidget);
		
		// if we were monitoring the click events on the widget, stop it.
		g_signal_handlers_disconnect_matched (pWidget,
			G_SIGNAL_MATCH_FUNC,
			0,
			0,
			NULL,
			on_button_press_widget,
			NULL);
	}
	return pWidget;
}

GtkWidget *cairo_dock_steal_interactive_widget_from_dialog (CairoDialog *pDialog)
{
	if (pDialog == NULL)
		return NULL;
	
	GtkWidget *pInteractiveWidget = pDialog->pInteractiveWidget;
	if (pInteractiveWidget != NULL)
	{
		pInteractiveWidget = cairo_dock_steal_widget_from_its_container (pInteractiveWidget);
		pDialog->pInteractiveWidget = NULL;
	}
	return pInteractiveWidget;
}

void cairo_dock_set_dialog_widget_text_color (GtkWidget *pWidget)
{
	#if (GTK_MAJOR_VERSION < 3)
	static GdkColor color;
	color.red = myDialogsParam.dialogTextDescription.fColorStart[0] * 65535;
	color.green = myDialogsParam.dialogTextDescription.fColorStart[1] * 65535;
	color.blue = myDialogsParam.dialogTextDescription.fColorStart[2] * 65535;
	gtk_widget_modify_fg (pWidget, GTK_STATE_NORMAL, &color);
	#else
	static GdkRGBA color;
	color.red = myDialogsParam.dialogTextDescription.fColorStart[0];
	color.green = myDialogsParam.dialogTextDescription.fColorStart[1];
	color.blue = myDialogsParam.dialogTextDescription.fColorStart[2];
	color.alpha = 1.;
	gtk_widget_override_color (pWidget, GTK_STATE_NORMAL, &color);
	#endif
}

void cairo_dock_set_dialog_widget_bg_color (GtkWidget *pWidget)
{
	#if (GTK_MAJOR_VERSION < 3)
	static GdkColor color;
	color.red = myDialogsParam.fDialogColor[0] * 65535;
	color.green = myDialogsParam.fDialogColor[1] * 65535;
	color.blue = myDialogsParam.fDialogColor[2] * 65535;
	gtk_widget_modify_bg (pWidget, GTK_STATE_NORMAL, &color);
	#else
	static GdkRGBA color;
	color.red = myDialogsParam.fDialogColor[0];
	color.green = myDialogsParam.fDialogColor[1];
	color.blue = myDialogsParam.fDialogColor[2];
	color.alpha = myDialogsParam.fDialogColor[3];
	gtk_widget_override_color (pWidget, GTK_STATE_NORMAL, &color);
	#endif
}

void cairo_dock_set_new_dialog_text_surface (CairoDialog *pDialog, cairo_surface_t *pNewTextSurface, int iNewTextWidth, int iNewTextHeight)
{
	int iPrevMessageWidth = pDialog->iMessageWidth;
	int iPrevMessageHeight = pDialog->iMessageHeight;

	cairo_surface_destroy (pDialog->pTextBuffer);
	pDialog->pTextBuffer = pNewTextSurface;
	if (pDialog->iTextTexture != 0)
		_cairo_dock_delete_texture (pDialog->iTextTexture);
	///pDialog->iTextTexture = cairo_dock_create_texture_from_surface (pNewTextSurface);
	
	pDialog->iTextWidth = iNewTextWidth;
	pDialog->iTextHeight = iNewTextHeight;
	_cairo_dock_compute_dialog_sizes (pDialog);

	if (pDialog->iMessageWidth != iPrevMessageWidth || pDialog->iMessageHeight != iPrevMessageHeight)
	{
		g_object_set (pDialog->pMessageWidget, "width-request", pDialog->iMessageWidth, "height-request", pDialog->iMessageHeight, NULL);  // inutile de replacer le dialogue puisque sa gravite fera le boulot.
		
		gtk_widget_queue_draw (pDialog->container.pWidget);
		
		gboolean bInside = pDialog->container.bInside;
		pDialog->container.bInside = FALSE;  // unfortunately the gravity is really badly handled by many WMs, so we have to replace he dialog ourselves :-/
		cairo_dock_replace_all_dialogs ();
		pDialog->container.bInside = bInside;
	}
	else
	{
		cairo_dock_damage_text_dialog (pDialog);
	}
}

void cairo_dock_set_new_dialog_icon_surface (CairoDialog *pDialog, cairo_surface_t *pNewIconSurface, int iNewIconSize)
{
	int iPrevMessageWidth = pDialog->iMessageWidth;
	int iPrevMessageHeight = pDialog->iMessageHeight;

	cairo_surface_destroy (pDialog->pIconBuffer);
	
	pDialog->pIconBuffer = cairo_dock_duplicate_surface (pNewIconSurface, iNewIconSize, iNewIconSize, iNewIconSize, iNewIconSize);
	if (pDialog->iIconTexture != 0)
		_cairo_dock_delete_texture (pDialog->iIconTexture);
	///	pDialog->iIconTexture = cairo_dock_create_texture_from_surface (pDialog->pIconBuffer);
	
	pDialog->iIconSize = iNewIconSize;
	_cairo_dock_compute_dialog_sizes (pDialog);

	if (pDialog->iMessageWidth != iPrevMessageWidth || pDialog->iMessageHeight != iPrevMessageHeight)
	{
		g_object_set (pDialog->pMessageWidget, "width-request", pDialog->iMessageWidth, "height-request", pDialog->iMessageHeight, NULL);  // inutile de replacer le dialogue puisque sa gravite fera le boulot.
		
		gtk_widget_queue_draw (pDialog->container.pWidget);
	}
	else
	{
		cairo_dock_damage_icon_dialog (pDialog);
	}
}


void cairo_dock_set_dialog_message (CairoDialog *pDialog, const gchar *cMessage)
{
	int iNewTextWidth=0, iNewTextHeight=0;
	cairo_surface_t *pNewTextSurface = _cairo_dock_create_dialog_text_surface (cMessage, pDialog->bUseMarkup, &iNewTextWidth, &iNewTextHeight);
	
	cairo_dock_set_new_dialog_text_surface (pDialog, pNewTextSurface, iNewTextWidth, iNewTextHeight);
}
void cairo_dock_set_dialog_message_printf (CairoDialog *pDialog, const gchar *cMessageFormat, ...)
{
	g_return_if_fail (cMessageFormat != NULL);
	va_list args;
	va_start (args, cMessageFormat);
	gchar *cMessage = g_strdup_vprintf (cMessageFormat, args);
	cairo_dock_set_dialog_message (pDialog, cMessage);
	g_free (cMessage);
	va_end (args);
}

void cairo_dock_set_dialog_icon (CairoDialog *pDialog, const gchar *cImageFilePath)
{
	cairo_surface_t *pNewIconSurface = cairo_dock_create_surface_for_square_icon (cImageFilePath, myDialogsParam.iDialogIconSize);
	int iNewIconSize = (pNewIconSurface != NULL ? myDialogsParam.iDialogIconSize : 0);
	
	cairo_dock_set_new_dialog_icon_surface (pDialog, pNewIconSurface, iNewIconSize);
}


void cairo_dock_damage_icon_dialog (CairoDialog *pDialog)
{
	if (!pDialog->container.bUseReflect)
		gtk_widget_queue_draw_area (pDialog->container.pWidget,
			pDialog->iLeftMargin,
			(pDialog->container.bDirectionUp ? 
				pDialog->iTopMargin :
				pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight)),
			pDialog->iIconSize,
			pDialog->iIconSize);
	else
		gtk_widget_queue_draw (pDialog->container.pWidget);
}

void cairo_dock_damage_text_dialog (CairoDialog *pDialog)
{
	if (!pDialog->container.bUseReflect)
		gtk_widget_queue_draw_area (pDialog->container.pWidget,
			pDialog->iLeftMargin + pDialog->iIconSize + CAIRO_DIALOG_TEXT_MARGIN,
			(pDialog->container.bDirectionUp ? 
				pDialog->iTopMargin :
				pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight)),
			_drawn_text_width (pDialog),
			pDialog->iTextHeight);
	else
		gtk_widget_queue_draw (pDialog->container.pWidget);
}

void cairo_dock_damage_interactive_widget_dialog (CairoDialog *pDialog)
{
	if (!pDialog->container.bUseReflect)
		gtk_widget_queue_draw_area (pDialog->container.pWidget,
			pDialog->iLeftMargin,
			(pDialog->container.bDirectionUp ? 
				pDialog->iTopMargin + pDialog->iMessageHeight :
				pDialog->container.iHeight - (pDialog->iTopMargin + pDialog->iBubbleHeight) + pDialog->iMessageHeight),
			pDialog->iInteractiveWidth,
			pDialog->iInteractiveHeight);
	else
		gtk_widget_queue_draw (pDialog->container.pWidget);
}
