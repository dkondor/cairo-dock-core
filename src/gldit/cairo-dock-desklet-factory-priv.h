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

#ifndef __CAIRO_DESKLET_FACTORY_PRIV_H__
#define  __CAIRO_DESKLET_FACTORY_PRIV_H__

#include "cairo-dock-container-priv.h"
#include "cairo-dock-desklet-factory.h"

G_BEGIN_DECLS


/// Configuration attributes of a Desklet.
struct _CairoDeskletAttr {
	// parent attributes
	GldiContainerAttr cattr;
	gboolean bDeskletUseSize;
	gint iDeskletWidth;
	gint iDeskletHeight;
	gint iDeskletPositionX;
	gint iDeskletPositionY;
	gboolean bPositionLocked;
	gint iRotation;
	gint iDepthRotationY;
	gint iDepthRotationX;
	gchar *cDecorationTheme;
	CairoDeskletDecoration *pUserDecoration;
	CairoDeskletVisibility iVisibility;
	gboolean bOnAllDesktops;
	gint iNumDesktop;
	gboolean bNoInput;
	Icon *pIcon;
} ;

G_END_DECLS

#endif

