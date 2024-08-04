/* cdwindow.h generated by valac 0.56.16, the Vala compiler, do not modify */

#ifndef __CDWINDOW_H__
#define __CDWINDOW_H__

#include <gtk/gtk.h>
#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

#if !defined(VALA_EXTERN)
#if defined(_MSC_VER)
#define VALA_EXTERN __declspec(dllexport) extern
#elif __GNUC__ >= 4
#define VALA_EXTERN __attribute__((visibility("default"))) extern
#else
#define VALA_EXTERN extern
#endif
#endif

#define TYPE_CD_WINDOW (cd_window_get_type ())
#define CD_WINDOW(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_CD_WINDOW, CDWindow))
#define CD_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_CD_WINDOW, CDWindowClass))
#define IS_CD_WINDOW(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_CD_WINDOW))
#define IS_CD_WINDOW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_CD_WINDOW))
#define CD_WINDOW_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_CD_WINDOW, CDWindowClass))

typedef struct _CDWindow CDWindow;
typedef struct _CDWindowClass CDWindowClass;
typedef struct _CDWindowPrivate CDWindowPrivate;

struct _CDWindow {
	GtkWindow parent_instance;
	CDWindowPrivate * priv;
};

struct _CDWindowClass {
	GtkWindowClass parent_class;
};

VALA_EXTERN GType cd_window_get_type (void) G_GNUC_CONST ;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (CDWindow, g_object_unref)
VALA_EXTERN CDWindow* cd_window_new (GtkWindowType type);
VALA_EXTERN CDWindow* cd_window_construct (GType object_type,
                               GtkWindowType type);

G_END_DECLS

#endif
