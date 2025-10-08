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

#ifndef __CAIRO_DIALOG_MANAGER_PRIV__
#define  __CAIRO_DIALOG_MANAGER_PRIV__

#include "cairo-dock-container-priv.h"
#include "cairo-dock-dialog-manager.h"
G_BEGIN_DECLS


struct _CairoDialogAttr {
	// parent attributes
	GldiContainerAttr cattr;
	/// path to an image to display in the left margin, or NULL.
	const gchar *cImageFilePath;
	/// size of the icon in the left margin, or 0 to use the default one.
	gint iIconSize;
	/// text of the message, or NULL.
	const gchar *cText;
	/// whether to use Pango markups or not (markups are html-like marks, like <b>...</b>; using markups force you to escape some characters like "&" -> "&amp;")
	gboolean bUseMarkup;
	/// a widget to interact with the user, or NULL.
	GtkWidget *pInteractiveWidget;
	/// a NULL-terminated list of images for buttons, or NULL. "ok" and "cancel" are key word to load the default "ok" and "cancel" buttons.
	const gchar **cButtonsImage;
	/// function that will be called when the user click on a button, or NULL.
	CairoDockActionOnAnswerFunc pActionFunc;
	/// data passed as a parameter of the callback, or NULL.
	gpointer pUserData;
	/// a function to free the data when the dialog is destroyed, or NULL.
	GFreeFunc pFreeDataFunc;
	/// life time of the dialog (in ms), or 0 for an unlimited dialog.
	gint iTimeLength;
	/// name of a decorator, or NULL to use the default one.
	const gchar *cDecoratorName;
	/// whether the dialog should be transparent to mouse input.
	gboolean bNoInput;
	/// whether to pop-up the dialog in front of al other windows, including fullscreen windows.
	gboolean bForceAbove;
	/// for a dialog with no buttons, clicking on it will close it, or hide if this boolean is TRUE.
	gboolean bHideOnClick;
	/// icon it will be point to
	Icon *pIcon;
	/// container it will point to
	GldiContainer *pContainer;
};

G_END_DECLS
#endif

