#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <limits.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "console_device.h"
#include "aros_console_editor.h"
#include "console_device_bridge.h"

#define INPUT_MAX 4096

/*
 * The console window's pixel size. Real AROS console classes read
 * win->Width/Height once at ConUnit construction (consoleclass.c's
 * console_new()) to lay out the character-cell grid; NewWindowSize()-driven
 * resize is not implemented yet, so this is fixed for the window's lifetime.
 */
#define CONSOLE_WIDTH 900
#define CONSOLE_HEIGHT 576

/*
 * Font is hardcoded here as a placeholder. The intended design is a
 * host-side GTK preferences UI persisted to $HOME/.config, validated with
 * ace_gfx_font_family_complete() the same way the candidates below are --
 * see HANDOFF.md. Candidates are tried in order so the window still opens
 * on a host without the first choice installed.
 */
static const char *const default_font_candidates[] = {
    "Liberation Mono", "DejaVu Sans Mono", "monospace", NULL
};
#define DEFAULT_FONT_SIZE 16

struct console_window {
    GtkWidget *window;
    GtkWidget *drawing_area;
    int stream_fd;
    pid_t child_pid;
    struct ace_aros_console_editor *editor;

    /*
     * Real AROS console.device rendering state, behind an opaque bridge --
     * see console_device_bridge.h for why amiga_console.c cannot include
     * AROS's own headers directly (struct timeval and MAX/MIN collide with
     * glib's).
     */
    struct ace_console_device *device;
};

struct pending_output {
    struct console_window *console;
    size_t length;
    unsigned char data[];
};

static gboolean apply_output(gpointer data)
{
    struct pending_output *output = data;
    struct console_window *console = output->console;

    /*
     * The real entry point console.c's beginio()/CMD_WRITE would call.
     * ACE's rendering path never goes through DoIO()/BeginIO() -- see
     * HANDOFF.md -- so this calls the real ANSI/CSI parser directly with
     * the same arguments beginio() would have passed it.
     */
    ace_console_device_write(console->device, output->data, output->length);
    gtk_widget_queue_draw(console->drawing_area);
    free(output);
    return G_SOURCE_REMOVE;
}

static int render_output(void *context, const void *data, size_t length,
                         size_t *actual)
{
    struct pending_output *output;

    if (length > (size_t)-1 - sizeof(*output))
        return AMIGA_IOERR_UNITBUSY;
    output = malloc(sizeof(*output) + length);
    if (!output)
        return AMIGA_IOERR_UNITBUSY;
    output->console = context;
    output->length = length;
    memcpy(output->data, data, length);
    g_idle_add(apply_output, output);
    if (actual)
        *actual = length;
    return AMIGA_IOERR_OK;
}

static void drain_editor_output(struct console_window *console)
{
    unsigned char output[4096];
    size_t length;

    do {
        length = ace_aros_console_editor_take_output(console->editor,
                                                     output, sizeof(output));
        if (length != 0)
            ace_console_device_write(console->device, output, length);
    } while (length != 0);
    gtk_widget_queue_draw(console->drawing_area);
}

static gboolean draw_console(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct console_window *console = data;
    cairo_surface_t *surface = ace_console_device_surface(console->device);

    (void)widget;
    if (!surface)
        return FALSE;
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    return FALSE;
}

static int send_input(struct console_window *console, const void *data,
                      size_t length)
{
    char line[INPUT_MAX + 2];
    size_t line_length;
    size_t offset = 0;

    if (ace_aros_console_editor_feed(console->editor, data, length) != 0)
        return -1;
    drain_editor_output(console);
    line_length = ace_aros_console_editor_take_line(console->editor,
                                                    line, sizeof(line));
    while (offset < line_length) {
        ssize_t written = write(console->stream_fd, line + offset,
                                line_length - offset);

        if (written <= 0)
            return -1;
        offset += (size_t)written;
    }
    return 0;
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    struct console_window *console = data;
    guint key = event->keyval;
    guint modifiers = event->state & gtk_accelerator_get_default_mod_mask();
    char utf8[8];
    gunichar unicode;

    (void)widget;
    if (key == GDK_KEY_Return || key == GDK_KEY_KP_Enter) {
        (void)send_input(console, "\n", 1);
        return TRUE;
    }
    if (key == GDK_KEY_BackSpace) {
        (void)send_input(console, "\b", 1);
        return TRUE;
    }
    if (key == GDK_KEY_Delete) {
        (void)send_input(console, "\177", 1);
        return TRUE;
    }
    if (key == GDK_KEY_Left) {
        (void)send_input(console, "\233D", 2);
        return TRUE;
    }
    if (key == GDK_KEY_Right) {
        (void)send_input(console, "\233C", 2);
        return TRUE;
    }
    if (key == GDK_KEY_Home) {
        (void)send_input(console, "\23344~", 4);
        return TRUE;
    }
    if (key == GDK_KEY_End) {
        (void)send_input(console, "\23345~", 4);
        return TRUE;
    }
    if (key == GDK_KEY_Up || key == GDK_KEY_Down || key == GDK_KEY_Tab) {
        const char *sequence = key == GDK_KEY_Up ? "\233A" :
                               key == GDK_KEY_Down ? "\233B" : "\t";
        (void)send_input(console, sequence, strlen(sequence));
        return TRUE;
    }
    if (modifiers & GDK_CONTROL_MASK) {
        if (key == GDK_KEY_c)
            (void)send_input(console, "\003", 1);
        else if (key == GDK_KEY_d)
            (void)send_input(console, "\004", 1);
        return TRUE;
    }

    unicode = gdk_keyval_to_unicode(key);
    if (unicode >= 0x20 && unicode != 0x7f &&
        g_unichar_validate(unicode) &&
        g_unichar_to_utf8(unicode, utf8) < (gint)sizeof(utf8)) {
        int length = g_unichar_to_utf8(unicode, utf8);
        (void)send_input(console, utf8, (size_t)length);
        return TRUE;
    }
    return FALSE;
}

static gboolean read_console(GIOChannel *channel, GIOCondition condition,
                             gpointer data)
{
    struct console_window *console = data;
    size_t actual;
    char buffer[4096];
    ssize_t length;

    (void)channel;
    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        if (console->window)
            gtk_widget_destroy(console->window);
        return G_SOURCE_REMOVE;
    }
    length = read(console->stream_fd, buffer, sizeof(buffer));
    if (length <= 0) {
        if (console->window)
            gtk_widget_destroy(console->window);
        return G_SOURCE_REMOVE;
    }
    if (render_output(console, buffer, (size_t)length, &actual) !=
            AMIGA_IOERR_OK || actual != (size_t)length)
        return G_SOURCE_REMOVE;
    return G_SOURCE_CONTINUE;
}

static void console_destroy(GtkWidget *widget, gpointer data)
{
    struct console_window *console = data;
    int status;

    (void)widget;
    if (console->child_pid > 0) {
        kill(console->child_pid, SIGHUP);
        (void)waitpid(console->child_pid, &status, 0);
    }
    if (console->stream_fd >= 0)
        close(console->stream_fd);
    gtk_main_quit();
}

static int executable_directory(const char *argv0, char *directory,
                                size_t directory_size)
{
    char path[PATH_MAX];
    char *slash;

    if (!realpath(argv0, path))
        return -1;
    slash = strrchr(path, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (strlen(path) >= directory_size)
        return -1;
    strcpy(directory, path);
    return 0;
}

int main(int argc, char **argv)
{
    struct console_window console;
    GtkWidget *window;
    char directory[PATH_MAX];
    char shell_path[PATH_MAX];
    const char *session;
    int sockets[2];

    memset(&console, 0, sizeof(console));
    console.stream_fd = -1;
    console.child_pid = -1;
    console.device = ace_console_device_open(CONSOLE_WIDTH, CONSOLE_HEIGHT,
                                             default_font_candidates,
                                             DEFAULT_FONT_SIZE);
    if (!console.device) {
        fprintf(stderr, "ace-console: failed to set up console.device\n");
        return 20;
    }
    console.editor = ace_aros_console_editor_open();
    if (!console.editor)
        return 20;
    if (argc != 3 || strcmp(argv[1], "--session") != 0) {
        fprintf(stderr, "usage: %s --session SESSION\n", argv[0]);
        return 20;
    }
    session = argv[2];
    if (executable_directory(argv[0], directory, sizeof(directory)) != 0)
        return 20;
    if (snprintf(shell_path, sizeof(shell_path), "%s/ace-user-shell", directory) >=
        (int)sizeof(shell_path))
        return 20;

    /*
     * ACE runs as a native Wayland console, even when DISPLAY is also set.
     * It has no GTK menus, so avoid loading the desktop appmenu module; on
     * this session that optional module emits a GDK critical during realize.
    */
    setenv("GDK_BACKEND", "wayland", 1);
    setenv("GTK_MODULES", "", 1);
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
        return 20;
    console.child_pid = fork();
    if (console.child_pid < 0)
        return 20;
    if (console.child_pid == 0) {
        close(sockets[0]);
        if (dup2(sockets[1], STDIN_FILENO) < 0 ||
            dup2(sockets[1], STDOUT_FILENO) < 0 ||
            dup2(sockets[1], STDERR_FILENO) < 0)
            _exit(20);
        if (sockets[1] > STDERR_FILENO)
            close(sockets[1]);
        setenv("ACE_SESSION", session, 1);
        execl(shell_path, shell_path, (char *)NULL);
        _exit(20);
    }
    close(sockets[1]);
    console.stream_fd = sockets[0];

    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    console.window = window;
    gtk_window_set_title(GTK_WINDOW(window), "ACE Shell");
    gtk_window_set_default_size(GTK_WINDOW(window), CONSOLE_WIDTH, CONSOLE_HEIGHT);
    gtk_widget_set_size_request(window, CONSOLE_WIDTH, CONSOLE_HEIGHT);
    g_signal_connect(window, "destroy", G_CALLBACK(console_destroy), &console);
    console.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(console.drawing_area, TRUE);
    gtk_widget_add_events(console.drawing_area, GDK_KEY_PRESS_MASK);
    g_signal_connect(console.drawing_area, "draw", G_CALLBACK(draw_console), &console);
    g_signal_connect(console.drawing_area, "key-press-event", G_CALLBACK(key_press), &console);
    gtk_container_add(GTK_CONTAINER(window), console.drawing_area);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(console.drawing_area);

    g_io_add_watch(g_io_channel_unix_new(console.stream_fd),
                   G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                   read_console, &console);
    gtk_main();
    ace_aros_console_editor_close(console.editor);
    ace_console_device_close(console.device);
    return 0;
}
