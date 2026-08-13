#ifndef ACE_APPMENU_WAYLAND_H
#define ACE_APPMENU_WAYLAND_H

#include <gio/gio.h>
#include <gtk/gtk.h>

/* Advertise an already-exported org.gtk.Menus/org.gtk.Actions object for the
 * mapped GTK toplevel. Returns nonzero only while the Wayland appmenu
 * manager is currently reachable. */
gboolean ace_appmenu_wayland_advertise(GtkWindow *window,
                                       GDBusConnection *bus,
                                       const char *object_path);

/* Release the protocol objects before GTK tears down the display. */
void ace_appmenu_wayland_forget(void);

#endif
