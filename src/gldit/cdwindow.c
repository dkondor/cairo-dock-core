/* cdwindow.c generated by valac 0.56.16, the Vala compiler
 * generated from cdwindow.vala, do not modify */

/*
 * cdwindow.vala
 * A simple GtkWindow subclass that emits a signal during unmap _before_ 
 * the underlying surfaces are destroyed.
 * 
 * Copyright 2024 Daniel Kondor <kondor.dani@gmail.com>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * 
 * 
 */

#include "cdwindow.h"
#include <gtk/gtk.h>
#include <glib-object.h>

#if !defined(VALA_STRICT_C)
#if !defined(__clang__) && defined(__GNUC__) && (__GNUC__ >= 14)
#pragma GCC diagnostic warning "-Wincompatible-pointer-types"
#elif defined(__clang__) && (__clang_major__ >= 16)
#pragma clang diagnostic ignored "-Wincompatible-function-pointer-types"
#pragma clang diagnostic ignored "-Wincompatible-pointer-types"
#endif
#endif

enum  {
	CD_WINDOW_0_PROPERTY,
	CD_WINDOW_NUM_PROPERTIES
};
static GParamSpec* cd_window_properties[CD_WINDOW_NUM_PROPERTIES];
enum  {
	CD_WINDOW_PENDING_UNMAP_SIGNAL,
	CD_WINDOW_NUM_SIGNALS
};
static guint cd_window_signals[CD_WINDOW_NUM_SIGNALS] = {0};

static gpointer cd_window_parent_class = NULL;

static void cd_window_real_unmap (GtkWidget* base);
static GType cd_window_get_type_once (void);

CDWindow*
cd_window_construct (GType object_type,
                     GtkWindowType type)
{
	CDWindow * self = NULL;
	self = (CDWindow*) g_object_new (object_type, "type", type, NULL);
	return self;
}

CDWindow*
cd_window_new (GtkWindowType type)
{
	return cd_window_construct (TYPE_CD_WINDOW, type);
}

static void
cd_window_real_unmap (GtkWidget* base)
{
	CDWindow * self;
	self = (CDWindow*) base;
	g_signal_emit (self, cd_window_signals[CD_WINDOW_PENDING_UNMAP_SIGNAL], 0);
	GTK_WIDGET_CLASS (cd_window_parent_class)->unmap ((GtkWidget*) G_TYPE_CHECK_INSTANCE_CAST (self, gtk_window_get_type (), GtkWindow));
}

static void
cd_window_class_init (CDWindowClass * klass,
                      gpointer klass_data)
{
	cd_window_parent_class = g_type_class_peek_parent (klass);
	((GtkWidgetClass *) klass)->unmap = (void (*) (GtkWidget*)) cd_window_real_unmap;
	cd_window_signals[CD_WINDOW_PENDING_UNMAP_SIGNAL] = g_signal_new ("pending-unmap", TYPE_CD_WINDOW, G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}

static void
cd_window_instance_init (CDWindow * self,
                         gpointer klass)
{
}

static GType
cd_window_get_type_once (void)
{
	static const GTypeInfo g_define_type_info = { sizeof (CDWindowClass), (GBaseInitFunc) NULL, (GBaseFinalizeFunc) NULL, (GClassInitFunc) cd_window_class_init, (GClassFinalizeFunc) NULL, NULL, sizeof (CDWindow), 0, (GInstanceInitFunc) cd_window_instance_init, NULL };
	GType cd_window_type_id;
	cd_window_type_id = g_type_register_static (gtk_window_get_type (), "CDWindow", &g_define_type_info, 0);
	return cd_window_type_id;
}

GType
cd_window_get_type (void)
{
	static volatile gsize cd_window_type_id__once = 0;
	if (g_once_init_enter (&cd_window_type_id__once)) {
		GType cd_window_type_id;
		cd_window_type_id = cd_window_get_type_once ();
		g_once_init_leave (&cd_window_type_id__once, cd_window_type_id);
	}
	return cd_window_type_id__once;
}

