#define _GNU_SOURCE

#include "ace_appmenu_wayland.h"

#include <gdk/gdkwayland.h>
#include <string.h>
#include <wayland-client.h>

/* This is the small org_kde_kwin_appmenu protocol used by labwc. Keeping the
 * generated-equivalent declarations here avoids making ACE depend on the
 * compositor checkout at build time. */
struct org_kde_kwin_appmenu_manager;
struct org_kde_kwin_appmenu;

static const struct wl_interface appmenu_interface;
static const struct wl_interface *appmenu_types[] = {
    NULL, NULL, &appmenu_interface, &wl_surface_interface,
};

static const struct wl_message manager_requests[] = {
    { "create", "no", appmenu_types + 2 },
    { "release", "2", appmenu_types + 0 },
};

static const struct wl_interface manager_interface = {
    "org_kde_kwin_appmenu_manager", 2,
    2, manager_requests,
    0, NULL,
};

static const struct wl_message appmenu_requests[] = {
    { "set_address", "ss", appmenu_types + 0 },
    { "release", "", appmenu_types + 0 },
};

static const struct wl_interface appmenu_interface = {
    "org_kde_kwin_appmenu", 2,
    2, appmenu_requests,
    0, NULL,
};

/*
 * ACE's Wayland objects live on their own event queue rather than the
 * display's default one. The default queue belongs to GDK: dispatching it --
 * which is what a plain wl_display_roundtrip() does -- runs GDK's own event
 * handlers from wherever ACE happens to be, and this code is called from a
 * GTK timeout, so that is a re-entrant dispatch into GTK from inside a GTK
 * callback. A private queue keeps ACE's round trip to ACE's own objects;
 * GDK's events are still delivered, but GDK dispatches them itself.
 */
struct appmenu_state {
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct wl_registry *registry;
    struct org_kde_kwin_appmenu_manager *manager;
    uint32_t manager_name;
    struct org_kde_kwin_appmenu *appmenu;
    struct wl_surface *surface;
    /*
     * The address is re-sent for a short while after the surface appears,
     * because a compositor may not have built its view for the surface at
     * the moment ACE first offers one, and then left alone. Re-sending it
     * several times a second for the life of the window is protocol traffic
     * that buys nothing: a compositor restart replaces GDK's whole display
     * and is picked up by ensure_manager(), and a manager that comes or goes
     * is reported on the registry listener below.
     */
    int settle_ticks;
};

#define APPMENU_SETTLE_TICKS 8

static struct appmenu_state state;

static struct org_kde_kwin_appmenu *manager_create(
    struct org_kde_kwin_appmenu_manager *manager, struct wl_surface *surface)
{
    return (struct org_kde_kwin_appmenu *)wl_proxy_marshal_flags(
        (struct wl_proxy *)manager, 0, &appmenu_interface,
        wl_proxy_get_version((struct wl_proxy *)manager), 0, NULL, surface);
}

static void manager_destroy(struct org_kde_kwin_appmenu_manager *manager)
{
    /* labwc currently advertises manager version 1; its protocol-level
     * release request was added in version 2, so destroy the client proxy
     * without sending that newer request. */
    wl_proxy_destroy((struct wl_proxy *)manager);
}

static void appmenu_set_address(struct org_kde_kwin_appmenu *appmenu,
                                const char *service_name,
                                const char *object_path)
{
    wl_proxy_marshal_flags((struct wl_proxy *)appmenu, 0, NULL,
                           wl_proxy_get_version((struct wl_proxy *)appmenu),
                           0, service_name, object_path);
}

static void appmenu_destroy(struct org_kde_kwin_appmenu *appmenu)
{
    wl_proxy_marshal_flags((struct wl_proxy *)appmenu, 1, NULL,
                           wl_proxy_get_version((struct wl_proxy *)appmenu),
                           WL_MARSHAL_FLAG_DESTROY);
}

static void registry_global(void *data, struct wl_registry *registry,
                            uint32_t name, const char *interface,
                            uint32_t version)
{
    struct appmenu_state *appmenu = data;

    if (strcmp(interface, manager_interface.name) == 0 && !appmenu->manager) {
        appmenu->manager_name = name;
        appmenu->manager = (struct org_kde_kwin_appmenu_manager *)
            wl_registry_bind(registry, name, &manager_interface,
                             version < 2 ? version : 2);
        /* A manager that only just appeared has never been told ACE's
         * address, so start the settle window over. */
        appmenu->settle_ticks = 0;
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                   uint32_t name)
{
    struct appmenu_state *appmenu = data;

    (void)registry;
    if (appmenu->manager && name == appmenu->manager_name) {
        wl_proxy_destroy((struct wl_proxy *)appmenu->manager);
        appmenu->manager = NULL;
        appmenu->manager_name = 0;
        if (appmenu->appmenu) {
            wl_proxy_destroy((struct wl_proxy *)appmenu->appmenu);
            appmenu->appmenu = NULL;
            appmenu->surface = NULL;
        }
    }
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    registry_global_remove,
};

static gboolean ensure_manager(GdkDisplay *display)
{
    struct wl_display *wl = gdk_wayland_display_get_wl_display(
        GDK_WAYLAND_DISPLAY(display));

    if (state.display != wl) {
        /* A new display means a new compositor connection -- a restart, or
         * the first call. Everything bound to the old one is gone. */
        ace_appmenu_wayland_forget();
        state.display = wl;
        state.queue = wl_display_create_queue(wl);
        if (!state.queue) {
            state.display = NULL;
            return FALSE;
        }
        state.registry = wl_display_get_registry(wl);
        if (!state.registry) {
            ace_appmenu_wayland_forget();
            return FALSE;
        }
        /* Objects the registry creates inherit its queue, so this is the
         * only proxy that has to be reassigned by hand. */
        wl_proxy_set_queue((struct wl_proxy *)state.registry, state.queue);
        wl_registry_add_listener(state.registry, &registry_listener, &state);
        /* One round trip, on ACE's own queue, to learn what the compositor
         * offers. */
        (void)wl_display_roundtrip_queue(state.display, state.queue);
    } else {
        /* Pick up a manager that appeared or went away since the last check.
         * This only dispatches events already delivered to ACE's queue, so
         * it neither blocks nor touches GDK's. */
        (void)wl_display_dispatch_queue_pending(state.display, state.queue);
    }
    return state.manager != NULL;
}

gboolean ace_appmenu_wayland_advertise(GtkWindow *window,
                                       GDBusConnection *bus,
                                       const char *object_path)
{
    GdkDisplay *display;
    GdkWindow *gdk_window;
    struct wl_surface *surface;
    const char *service_name;

    if (!window || !bus || !object_path)
        return FALSE;
    display = gtk_widget_get_display(GTK_WIDGET(window));
    if (!GDK_IS_WAYLAND_DISPLAY(display))
        return FALSE;
    /* labwc associates the protocol object with its view when create() is
     * received. Before the GTK toplevel is mapped there is no view yet and
     * set_address is intentionally ignored, so do not create a one-shot
     * object that cannot be repaired by later address updates. */
    if (!gtk_widget_get_mapped(GTK_WIDGET(window)))
        return FALSE;
    gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
    if (!gdk_window || !GDK_IS_WAYLAND_WINDOW(gdk_window))
        return FALSE;
    if (!ensure_manager(display))
        return FALSE;
    surface = gdk_wayland_window_get_wl_surface(gdk_window);
    if (!surface)
        return FALSE;
    if (state.appmenu && state.surface != surface) {
        appmenu_destroy(state.appmenu);
        state.appmenu = NULL;
    }
    if (!state.appmenu) {
        state.appmenu = manager_create(state.manager, surface);
        state.surface = surface;
        state.settle_ticks = 0;
    }
    if (state.settle_ticks >= APPMENU_SETTLE_TICKS)
        return TRUE;

    service_name = g_dbus_connection_get_unique_name(bus);
    appmenu_set_address(state.appmenu, service_name, object_path);
    state.settle_ticks++;
    if (wl_display_flush(state.display) < 0)
        return FALSE;
    return TRUE;
}

void ace_appmenu_wayland_forget(void)
{
    if (state.appmenu) {
        appmenu_destroy(state.appmenu);
        state.appmenu = NULL;
    }
    state.surface = NULL;
    if (state.manager) {
        manager_destroy(state.manager);
        state.manager = NULL;
    }
    if (state.registry) {
        wl_registry_destroy(state.registry);
        state.registry = NULL;
    }
    /* The queue outlives every proxy assigned to it, so it goes last. */
    if (state.queue) {
        wl_event_queue_destroy(state.queue);
        state.queue = NULL;
    }
    state.display = NULL;
    state.manager_name = 0;
    state.settle_ticks = 0;
}
