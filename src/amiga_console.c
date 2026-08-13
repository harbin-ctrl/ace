#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <errno.h>
#include <limits.h>
#include <pango/pango.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "aros_console_editor.h"
#include "aros_graphics_runtime.h"
#include "ace_appmenu_wayland.h"
#include "console_device_bridge.h"

#define INPUT_MAX 4096

/*
 * The console window's pixel size. Real AROS console classes read
 * win->Width/Height at ConUnit construction (consoleclass.c's
 * console_new()) and again through M_Console_NewWindowSize when the window
 * changes. The GTK drawing area is the emulated AROS Window here.
 */
#define CONSOLE_WIDTH 900
#define CONSOLE_HEIGHT 576

/* Candidates are tried in order so the window still opens on a host without
 * the first choice installed. The active typeface can be changed from the
 * ACE Shell menu. */
static const char *const default_font_candidates[] = {
    "Liberation Mono", "DejaVu Sans Mono", "monospace", NULL
};
#define DEFAULT_FONT_SIZE 16
#define CONFIG_GROUP "ACE Shell"
#define ACE_ICON_NAME "ace"
#define CONFIG_FONT_FAMILY "font-family"
#define CONFIG_FONT_SIZE "font-size"
#define CONFIG_PALETTE_PREFIX "palette-"

static const uint32_t default_palette[ACE_CONSOLE_PEN_COUNT] = {
    0x000000u, 0xffffffu, 0xff5555u, 0x55ff55u,
    0x5555ffu, 0xffff55u, 0xff55ffu, 0x55ffffu,
};

/*
 * Named palettes, so eight pens can be chosen rather than hand-mixed. The pen
 * order follows default_palette above: pen 0 is the background and pen 1 the
 * text -- the two stdconclass.c's penmap[] redirects to BACKGROUNDPEN and
 * TEXTPEN -- and pens 2-7 carry the six accents CSI 32m-37m reach.
 *
 * Pen 7 is worth choosing deliberately: it doubles as the cursor colour. The
 * console draws its cursor with a COMPLEMENT RectFill, and over the
 * background that inverts pen 0 to pen 0 ^ ACE_GFX_PEN_MASK, which is pen 7
 * on the three bitplanes ACE simulates.
 *
 * "Retro" is Commodore's own eight Workbench 3.1 defaults, kept in their own
 * order rather than remapped into the accent order the imported schemes use;
 * that ordering is the historical artifact, so bending it would be a lie.
 * The rest are transcribed from each project's published palette.
 */
struct palette_preset {
    const char *name;
    uint32_t colors[ACE_CONSOLE_PEN_COUNT];
};

static const struct palette_preset palette_presets[] = {
    /* AROS workbench/prefs/palette/prefs.c defaultcolor[], which carries
       Commodore's 12-bit registers ($AAA, $68B, $E44 ...) intact. */
    { "Retro",
      { 0xaaaaaau, 0x000000u, 0xffffffu, 0x6688bbu,
        0xee4444u, 0x55dd55u, 0x0044ddu, 0xee9900u } },
    { "ACE Default",
      { 0x000000u, 0xffffffu, 0xff5555u, 0x55ff55u,
        0x5555ffu, 0xffff55u, 0xff55ffu, 0x55ffffu } },
    { "Catppuccin Latte",
      { 0xeff1f5u, 0x4c4f69u, 0xd20f39u, 0x40a02bu,
        0x1e66f5u, 0xdf8e1du, 0x8839efu, 0x179299u } },
    { "Catppuccin Mocha",
      { 0x1e1e2eu, 0xcdd6f4u, 0xf38ba8u, 0xa6e3a1u,
        0x89b4fau, 0xf9e2afu, 0xcba6f7u, 0x94e2d5u } },
    /* Dracula names no plain blue; its purple stands in, as it does in the
       scheme's own terminal mappings. */
    { "Dracula",
      { 0x282a36u, 0xf8f8f2u, 0xff5555u, 0x50fa7bu,
        0xbd93f9u, 0xf1fa8cu, 0xff79c6u, 0x8be9fdu } },
    { "Gruvbox Dark",
      { 0x282828u, 0xebdbb2u, 0xfb4934u, 0xb8bb26u,
        0x83a598u, 0xfabd2fu, 0xd3869bu, 0x8ec07cu } },
    { "Nord",
      { 0x2e3440u, 0xd8dee9u, 0xbf616au, 0xa3be8cu,
        0x81a1c1u, 0xebcb8bu, 0xb48eadu, 0x88c0d0u } },
    { "Solarized Dark",
      { 0x002b36u, 0x839496u, 0xdc322fu, 0x859900u,
        0x268bd2u, 0xb58900u, 0xd33682u, 0x2aa198u } },
    { "Solarized Light",
      { 0xfdf6e3u, 0x657b83u, 0xdc322fu, 0x859900u,
        0x268bd2u, 0xb58900u, 0xd33682u, 0x2aa198u } },
    /* Official Tango defines no cyan; 0x06989a is the value GNOME Terminal
       added to complete the scheme, and is what "Tango" means for a
       terminal. */
    { "Tango",
      { 0x2e3436u, 0xd3d7cfu, 0xcc0000u, 0x4e9a06u,
        0x3465a4u, 0xc4a000u, 0x75507bu, 0x06989au } },
};

#define PALETTE_PRESET_COUNT \
    ((int)(sizeof(palette_presets) / sizeof(palette_presets[0])))

struct console_window {
    GtkWidget *window;
    GtkWidget *menu_bar;
    GtkWidget *drawing_area;
    int stream_fd;
    pid_t child_pid;
    struct ace_aros_console_editor *editor;
    char *font_family;
    int font_size;
    uint32_t palette[ACE_CONSOLE_PEN_COUNT];
    guint menu_probe_source;
    guint resize_source;
    int pending_width;
    int pending_height;
    gboolean menu_type_hint_restored;
    gboolean menu_wayland_live;
    GDBusConnection *menu_bus;
    GMenu *menu_model;
    GSimpleActionGroup *menu_actions;
    guint menu_model_export_id;
    guint menu_action_export_id;
    char *menu_object_path;

    /*
     * Real AROS console.device rendering state, behind an opaque bridge --
     * see console_device_bridge.h for why amiga_console.c cannot include
     * AROS's own headers directly (struct timeval and MAX/MIN collide with
     * glib's).
     */
    struct ace_console_device *device;
};

static char *config_path(void)
{
    const char *home = g_get_home_dir();

    if (!home || !*home)
        return NULL;
    return g_build_filename(home, ".config", "ace.conf", NULL);
}

static gboolean parse_rgb(const char *text, uint32_t *rgb)
{
    char *end;
    guint64 value;

    if (!text || strlen(text) != 6)
        return FALSE;
    value = g_ascii_strtoull(text, &end, 16);
    if (end != text + 6 || value > 0xffffffu)
        return FALSE;
    *rgb = (uint32_t)value;
    return TRUE;
}

static void load_config(struct console_window *console)
{
    GKeyFile *key_file;
    GError *error = NULL;
    char *path;
    char *family;
    int size;
    int i;

    path = config_path();
    if (!path)
        return;
    key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &error)) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            fprintf(stderr, "ace-console: cannot read %s: %s\n", path,
                    error->message);
        g_clear_error(&error);
        g_key_file_unref(key_file);
        g_free(path);
        return;
    }

    family = g_key_file_get_string(key_file, CONFIG_GROUP,
                                    CONFIG_FONT_FAMILY, &error);
    if (family && *family && strlen(family) < 128 &&
        ace_gfx_font_family_complete(family)) {
        g_free(console->font_family);
        console->font_family = family;
        family = NULL;
    }
    g_free(family);
    g_clear_error(&error);

    size = g_key_file_get_integer(key_file, CONFIG_GROUP, CONFIG_FONT_SIZE,
                                  &error);
    if (!error && size > 0 && size <= 512)
        console->font_size = size;
    g_clear_error(&error);

    for (i = 0; i < ACE_CONSOLE_PEN_COUNT; i++) {
        char key[32];
        char *text;
        uint32_t rgb;

        snprintf(key, sizeof(key), "%s%d", CONFIG_PALETTE_PREFIX, i);
        text = g_key_file_get_string(key_file, CONFIG_GROUP, key, &error);
        if (text && parse_rgb(text, &rgb))
            console->palette[i] = rgb;
        g_free(text);
        g_clear_error(&error);
    }
    g_key_file_unref(key_file);
    g_free(path);
}

static void save_config(struct console_window *console)
{
    GKeyFile *key_file;
    GError *error = NULL;
    char *path;
    char *directory;
    char *data;
    gsize length;
    int i;

    path = config_path();
    if (!path)
        return;
    directory = g_path_get_dirname(path);
    if (g_mkdir_with_parents(directory, 0700) != 0) {
        fprintf(stderr, "ace-console: cannot create %s: %s\n", directory,
                g_strerror(errno));
        g_free(directory);
        g_free(path);
        return;
    }
    key_file = g_key_file_new();
    g_key_file_set_string(key_file, CONFIG_GROUP, CONFIG_FONT_FAMILY,
                          console->font_family);
    g_key_file_set_integer(key_file, CONFIG_GROUP, CONFIG_FONT_SIZE,
                           console->font_size);
    for (i = 0; i < ACE_CONSOLE_PEN_COUNT; i++) {
        char key[32];
        char value[7];

        snprintf(key, sizeof(key), "%s%d", CONFIG_PALETTE_PREFIX, i);
        snprintf(value, sizeof(value), "%06x", console->palette[i]);
        g_key_file_set_string(key_file, CONFIG_GROUP, key, value);
    }
    data = g_key_file_to_data(key_file, &length, &error);
    if (!data || !g_file_set_contents(path, data, (gssize)length, &error)) {
        fprintf(stderr, "ace-console: cannot write %s: %s\n", path,
                error ? error->message : "unknown error");
    }
    g_clear_error(&error);
    g_free(data);
    g_key_file_unref(key_file);
    g_free(directory);
    g_free(path);
}

static gboolean font_is_monospace(const PangoFontFamily *family,
                                  const PangoFontFace *face, gpointer data)
{
    (void)face;
    (void)data;
    return pango_font_family_is_monospace((PangoFontFamily *)family);
}

static void show_error(struct console_window *console, const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(console->window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static size_t build_font_candidates(const char *preferred,
                                    const char *const *defaults,
                                    const char **out, size_t capacity)
{
    size_t count = 0;
    int i;

    if (preferred && *preferred && count + 1 < capacity)
        out[count++] = preferred;
    for (i = 0; defaults[i] && count + 1 < capacity; i++) {
        if (preferred && strcmp(preferred, defaults[i]) == 0)
            continue;
        out[count++] = defaults[i];
    }
    out[count] = NULL;
    return count;
}

static void choose_font(GtkWidget *widget, gpointer data)
{
    struct console_window *console = data;
    GtkWidget *dialog;
    GtkFontChooser *chooser;
    PangoFontFamily *selected_family;
    const char *family;
    char *initial_font;
    int pixel_size;

    (void)widget;
    dialog = gtk_font_chooser_dialog_new("Choose Typeface",
                                         GTK_WINDOW(console->window));
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_UTILITY);
    chooser = GTK_FONT_CHOOSER(dialog);
    gtk_font_chooser_set_filter_func(chooser, font_is_monospace, NULL, NULL);
    initial_font = g_strdup_printf("%s %d", console->font_family,
                                   console->font_size);
    gtk_font_chooser_set_font(chooser, initial_font);
    g_free(initial_font);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
        gtk_widget_destroy(dialog);
        return;
    }
    selected_family = gtk_font_chooser_get_font_family(chooser);
    family = selected_family ? pango_font_family_get_name(selected_family) : NULL;
    pixel_size = gtk_font_chooser_get_font_size(chooser) / PANGO_SCALE;
    if (!family || pixel_size <= 0 ||
        ace_console_device_set_font(console->device, family, pixel_size) != 0) {
        gtk_widget_destroy(dialog);
        show_error(console, "That typeface could not be loaded by ACE Shell.");
        return;
    }
    g_free(console->font_family);
    console->font_family = g_strdup(family);
    console->font_size = pixel_size;
    save_config(console);
    gtk_widget_queue_draw(console->drawing_area);
    gtk_widget_destroy(dialog);
}

static GdkRGBA palette_rgba(uint32_t rgb)
{
    GdkRGBA color = {
        ((rgb >> 16) & 0xff) / 255.0,
        ((rgb >> 8) & 0xff) / 255.0,
        (rgb & 0xff) / 255.0,
        1.0,
    };
    return color;
}

static uint32_t palette_rgb(const GdkRGBA *color)
{
    guint red = (guint)(CLAMP(color->red, 0.0, 1.0) * 255.0 + 0.5);
    guint green = (guint)(CLAMP(color->green, 0.0, 1.0) * 255.0 + 0.5);
    guint blue = (guint)(CLAMP(color->blue, 0.0, 1.0) * 255.0 + 0.5);

    return (red << 16) | (green << 8) | blue;
}

/*
 * The palette dialog is a fixed list of named schemes over an editable set of
 * eight pens. Picking a scheme greys the pens out -- they then only report
 * what was picked -- and the trailing "Custom" row hands them back.
 */
struct palette_dialog {
    GtkWidget *buttons[ACE_CONSOLE_PEN_COUNT];
    GtkWidget *custom_frame;
    GtkWidget *custom_swatch;
    /* The user's own eight pens, held across excursions into the presets so
       that browsing the list never costs them their colours. */
    uint32_t custom[ACE_CONSOLE_PEN_COUNT];
    int selected; /* index into palette_presets, or -1 for Custom */
};

/* Draws a row's eight pens as one strip, so the list can be read by colour
   rather than by name alone. */
static gboolean draw_palette_swatch(GtkWidget *widget, cairo_t *cr,
                                    gpointer data)
{
    const uint32_t *colors = data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    int i;

    for (i = 0; i < ACE_CONSOLE_PEN_COUNT; i++) {
        int x0 = width * i / ACE_CONSOLE_PEN_COUNT;
        int x1 = width * (i + 1) / ACE_CONSOLE_PEN_COUNT;
        GdkRGBA color = palette_rgba(colors[i]);

        gdk_cairo_set_source_rgba(cr, &color);
        cairo_rectangle(cr, x0, 0, x1 - x0, height);
        cairo_fill(cr);
    }
    return TRUE;
}

static GtkWidget *palette_list_row(const char *name, const uint32_t *colors,
                                   int index)
{
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *label = gtk_label_new(name);
    GtkWidget *swatch = gtk_drawing_area_new();

    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 5);
    gtk_widget_set_margin_bottom(box, 5);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_size_request(swatch, 136, 16);
    gtk_widget_set_valign(swatch, GTK_ALIGN_CENTER);
    g_signal_connect(swatch, "draw", G_CALLBACK(draw_palette_swatch),
                     (gpointer)colors);
    gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), swatch, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    g_object_set_data(G_OBJECT(row), "preset-index", GINT_TO_POINTER(index));
    g_object_set_data(G_OBJECT(row), "swatch", swatch);
    return row;
}

/* GtkColorButton emits color-set only for user edits, never for the
   programmatic set_rgba() below, so the user's colours survive a tour of the
   presets without needing to be snapshotted on the way out. */
static void palette_custom_edited(GtkColorButton *button, gpointer data)
{
    struct palette_dialog *dialog = data;
    int i;

    for (i = 0; i < ACE_CONSOLE_PEN_COUNT; i++) {
        GdkRGBA color;

        gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(dialog->buttons[i]),
                                   &color);
        dialog->custom[i] = palette_rgb(&color);
    }
    (void)button;
    gtk_widget_queue_draw(dialog->custom_swatch);
}

static void palette_row_selected(GtkListBox *list, GtkListBoxRow *row,
                                 gpointer data)
{
    struct palette_dialog *dialog = data;
    const uint32_t *colors;
    int index;
    int i;

    (void)list;
    if (!row)
        return;
    index = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "preset-index"));
    dialog->selected = index;
    colors = index >= 0 ? palette_presets[index].colors : dialog->custom;

    for (i = 0; i < ACE_CONSOLE_PEN_COUNT; i++) {
        GdkRGBA color = palette_rgba(colors[i]);

        gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(dialog->buttons[i]),
                                   &color);
    }
    gtk_widget_set_sensitive(dialog->custom_frame, index < 0);
}

static void choose_palette(GtkWidget *widget, gpointer data)
{
    struct console_window *console = data;
    struct palette_dialog state;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *scroller;
    GtkWidget *list;
    GtkWidget *grid;
    GtkWidget *custom_row;
    uint32_t palette[ACE_CONSOLE_PEN_COUNT];
    int i;

    (void)widget;
    memcpy(state.custom, console->palette, sizeof(state.custom));
    state.selected = -1;
    for (i = 0; i < PALETTE_PRESET_COUNT; i++) {
        if (memcmp(palette_presets[i].colors, console->palette,
                   sizeof(state.custom)) == 0) {
            state.selected = i;
            break;
        }
    }

    dialog = gtk_dialog_new_with_buttons(
        "Choose Palette", GTK_WINDOW(console->window), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Apply", GTK_RESPONSE_OK, NULL);
    gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 520);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    gtk_box_set_spacing(GTK_BOX(content), 12);

    /* The list is sized to stand on its own rather than to fit its contents:
       it has to be visible without hunting, and scroll rather than grow as
       schemes are added. */
    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller),
                                               220);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller),
                                        GTK_SHADOW_IN);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_BROWSE);
    gtk_container_add(GTK_CONTAINER(scroller), list);
    gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 0);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 10);
    state.custom_frame = gtk_frame_new("Custom colors");
    gtk_container_add(GTK_CONTAINER(state.custom_frame), grid);
    gtk_box_pack_start(GTK_BOX(content), state.custom_frame, FALSE, FALSE, 0);

    /* Two rows of four pens; each pen is a label over its button, so the two
       pen rows occupy four grid rows. */
    for (i = 0; i < ACE_CONSOLE_PEN_COUNT; i++) {
        char label_text[32];
        GtkWidget *label;
        GdkRGBA color = palette_rgba(state.custom[i]);
        int column = i % 4;
        int base = (i / 4) * 2;

        snprintf(label_text, sizeof(label_text), "Pen %d%s", i,
                 i == 0 ? " (bg)" : i == 1 ? " (text)" :
                 i == 7 ? " (cursor)" : "");
        label = gtk_label_new(label_text);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        state.buttons[i] = gtk_color_button_new_with_rgba(&color);
        gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(state.buttons[i]),
                                        FALSE);
        gtk_widget_set_hexpand(state.buttons[i], TRUE);
        g_signal_connect(state.buttons[i], "color-set",
                         G_CALLBACK(palette_custom_edited), &state);
        gtk_grid_attach(GTK_GRID(grid), label, column, base, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), state.buttons[i], column, base + 1,
                        1, 1);
    }

    for (i = 0; i < PALETTE_PRESET_COUNT; i++)
        gtk_container_add(GTK_CONTAINER(list),
                          palette_list_row(palette_presets[i].name,
                                           palette_presets[i].colors, i));
    custom_row = palette_list_row("Custom", state.custom, -1);
    state.custom_swatch = g_object_get_data(G_OBJECT(custom_row), "swatch");
    gtk_container_add(GTK_CONTAINER(list), custom_row);

    g_signal_connect(list, "row-selected", G_CALLBACK(palette_row_selected),
                     &state);
    gtk_list_box_select_row(
        GTK_LIST_BOX(list),
        state.selected >= 0
            ? gtk_list_box_get_row_at_index(GTK_LIST_BOX(list), state.selected)
            : GTK_LIST_BOX_ROW(custom_row));
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        memcpy(palette,
               state.selected >= 0 ? palette_presets[state.selected].colors
                                   : state.custom,
               sizeof(palette));
        if (ace_console_device_set_palette(console->device, palette) != 0) {
            gtk_widget_destroy(dialog);
            show_error(console, "The palette could not be applied to ACE Shell.");
            return;
        }
        memcpy(console->palette, palette, sizeof(console->palette));
        save_config(console);
        gtk_widget_queue_draw(console->drawing_area);
    }
    gtk_widget_destroy(dialog);
}

static void activate_dbus_menu_action(GSimpleAction *action,
                                      GVariant *parameter, gpointer data)
{
    struct console_window *console = data;
    const char *name = g_action_get_name(G_ACTION(action));

    (void)parameter;
    if (strcmp(name, "typeface") == 0)
        choose_font(NULL, console);
    else if (strcmp(name, "palette") == 0)
        choose_palette(NULL, console);
    else if (strcmp(name, "quit") == 0)
        gtk_widget_destroy(console->window);
}

static void add_dbus_menu_action(struct console_window *console,
                                 const char *name)
{
    GSimpleAction *action = g_simple_action_new(name, NULL);

    g_signal_connect(action, "activate", G_CALLBACK(activate_dbus_menu_action),
                     console);
    g_action_map_add_action(G_ACTION_MAP(console->menu_actions),
                            G_ACTION(action));
    g_object_unref(action);
}

static int export_dbus_menu(struct console_window *console)
{
    GMenu *submenu;
    GError *error = NULL;

    console->menu_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!console->menu_bus) {
        fprintf(stderr, "ace-console: cannot connect to session D-Bus: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return -1;
    }
    console->menu_model = g_menu_new();
    submenu = g_menu_new();
    g_menu_append(submenu, "Typeface…", "app.typeface");
    g_menu_append(submenu, "Palette…", "app.palette");
    g_menu_append(submenu, "Quit", "app.quit");
    g_menu_append_submenu(console->menu_model, "ACE Shell",
                          G_MENU_MODEL(submenu));
    g_object_unref(submenu);

    console->menu_actions = g_simple_action_group_new();
    add_dbus_menu_action(console, "typeface");
    add_dbus_menu_action(console, "palette");
    add_dbus_menu_action(console, "quit");
    console->menu_object_path = g_strdup(
        "/org/appmenu/gtk/window/menus/menubar/ace_shell");
    console->menu_model_export_id = g_dbus_connection_export_menu_model(
        console->menu_bus, console->menu_object_path,
        G_MENU_MODEL(console->menu_model), &error);
    if (!console->menu_model_export_id) {
        fprintf(stderr, "ace-console: cannot export menu on D-Bus: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        return -1;
    }
    console->menu_action_export_id = g_dbus_connection_export_action_group(
        console->menu_bus, console->menu_object_path,
        G_ACTION_GROUP(console->menu_actions), &error);
    if (!console->menu_action_export_id) {
        fprintf(stderr, "ace-console: cannot export menu actions on D-Bus: %s\n",
                error ? error->message : "unknown error");
        g_clear_error(&error);
        g_dbus_connection_unexport_menu_model(console->menu_bus,
                                              console->menu_model_export_id);
        console->menu_model_export_id = 0;
        return -1;
    }
    return 0;
}

static void unexport_dbus_menu(struct console_window *console)
{
    if (console->menu_bus) {
        if (console->menu_action_export_id)
            g_dbus_connection_unexport_action_group(
                console->menu_bus, console->menu_action_export_id);
        if (console->menu_model_export_id)
            g_dbus_connection_unexport_menu_model(
                console->menu_bus, console->menu_model_export_id);
    }
    console->menu_action_export_id = 0;
    console->menu_model_export_id = 0;
    g_clear_object(&console->menu_actions);
    g_clear_object(&console->menu_model);
    g_clear_object(&console->menu_bus);
    g_clear_pointer(&console->menu_object_path, g_free);
}

static GtkWidget *build_menu(struct console_window *console)
{
    GtkWidget *bar = gtk_menu_bar_new();
    GtkWidget *root_item = gtk_menu_item_new_with_mnemonic("_ACE Shell");
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *font_item = gtk_menu_item_new_with_mnemonic("_Typeface…");
    GtkWidget *palette_item = gtk_menu_item_new_with_mnemonic("_Palette…");
    GtkWidget *quit_item = gtk_menu_item_new_with_mnemonic("_Quit");

    g_signal_connect(font_item, "activate", G_CALLBACK(choose_font), console);
    g_signal_connect(palette_item, "activate", G_CALLBACK(choose_palette), console);
    g_signal_connect_swapped(quit_item, "activate",
                             G_CALLBACK(gtk_widget_destroy), console->window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), font_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), palette_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(root_item), menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(bar), root_item);
    return bar;
}

static void prepare_appmenu_environment(void)
{
    const char *modules = getenv("GTK_MODULES");
    GString *filtered = g_string_new(NULL);

    if (modules && *modules) {
        char **names = g_strsplit(modules, ":", -1);
        int i;

        for (i = 0; names[i]; i++) {
            if (!names[i][0] || strcmp(names[i], "appmenu-gtk-module") == 0)
                continue;
            if (filtered->len)
                g_string_append_c(filtered, ':');
            g_string_append(filtered, names[i]);
        }
        g_strfreev(names);
    }

    /* ACE exports its own GMenu/GAction pair and advertises that one address
     * over org_kde_kwin_appmenu. Loading appmenu-gtk-module as well creates a
     * second model/action group for the same surface. The compositor stores a
     * single address per view, so the two advertisements race and can leave
     * the displayed menu backed by the module's stale GTK action state. Keep
     * unrelated GTK modules, but make ACE's menu the only provider. */
    setenv("GTK_MODULES", filtered->str, 1);
    g_string_free(filtered, TRUE);
}

static gboolean update_menu_visibility(gpointer data)
{
    struct console_window *console = data;
    GtkSettings *settings;
    gboolean shell_shows_menubar = FALSE;
    GParamSpec *property;

    settings = gtk_widget_get_settings(console->window);
    property = g_object_class_find_property(G_OBJECT_GET_CLASS(settings),
                                             "gtk-shell-shows-menubar");
    if (property)
        g_object_get(settings, "gtk-shell-shows-menubar",
                     &shell_shows_menubar, NULL);

    /* Keep this live. The compositor can appear after the window, and a
     * compositor restart invalidates the previous Wayland advertisement. */
    console->menu_wayland_live = ace_appmenu_wayland_advertise(
        GTK_WINDOW(console->window), console->menu_bus,
        console->menu_object_path);
    if (console->menu_wayland_live || shell_shows_menubar)
        gtk_widget_hide(console->menu_bar);
    else
        gtk_widget_show(console->menu_bar);

    if (gtk_widget_get_mapped(console->window) &&
        !console->menu_type_hint_restored) {
        /* The installed appmenu module only inspects NORMAL/DIALOG windows
         * during its pre-realize hook. UTILITY avoids its premature private
         * GDK call; by this timer the real window exists, so restore the
         * normal ACE Shell type hint. */
        gtk_window_set_type_hint(GTK_WINDOW(console->window),
                                 GDK_WINDOW_TYPE_HINT_NORMAL);
        console->menu_type_hint_restored = TRUE;
    }
    return G_SOURCE_CONTINUE;
}

/*
 * Repaint only the rectangle the console actually drew into. A console spends
 * most of its time changing one line or one cell, and asking GTK to redraw
 * the whole window for that means handing the compositor a full window's
 * worth of pixels on every frame.
 */
static void queue_console_damage(struct console_window *console)
{
    int x;
    int y;
    int width;
    int height;

    if (ace_console_device_take_damage(console->device, &x, &y, &width,
                                       &height))
        gtk_widget_queue_draw_area(console->drawing_area, x, y, width, height);
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
    queue_console_damage(console);
}

static gboolean draw_console(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct console_window *console = data;
    cairo_surface_t *surface = ace_console_device_surface(console->device);
    GdkRGBA background = palette_rgba(console->palette[0]);
    GtkAllocation allocation;
    int origin_y = ace_console_device_origin_y(console->device);
    int width;
    int height;

    if (!surface)
        return FALSE;
    ace_console_device_size(console->device, &width, &height);
    gtk_widget_get_allocation(widget, &allocation);

    /* A resize enlarges the widget before the console has caught up with it.
     * Paint the shortfall in the background pen so the gap reads as empty
     * console rather than as whatever the compositor last had there. */
    if (width < allocation.width || height < allocation.height) {
        cairo_set_source_rgb(cr, background.red, background.green,
                             background.blue);
        if (width < allocation.width)
            cairo_rectangle(cr, width, 0, allocation.width - width,
                            allocation.height);
        if (height < allocation.height)
            cairo_rectangle(cr, 0, height, width, allocation.height - height);
        cairo_fill(cr);
    }

    /*
     * The surface carries growth slack and scroll headroom around the
     * console, so the console's own rows start at origin_y and the blit is
     * clipped to the console's extent.
     */
    cairo_save(cr);
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_clip(cr);
    cairo_set_source_surface(cr, surface, 0, -origin_y);
    cairo_paint(cr);
    cairo_restore(cr);
    return FALSE;
}

static gboolean apply_pending_resize(gpointer data)
{
    struct console_window *console = data;
    int width = console->pending_width;
    int height = console->pending_height;

    console->resize_source = 0;
    if (width <= 0 || height <= 0)
        return G_SOURCE_REMOVE;
    if (ace_console_device_resize(console->device, width, height) != 0) {
        fprintf(stderr, "ace-console: failed to resize console.device to %dx%d\n",
                width, height);
        return G_SOURCE_REMOVE;
    }
    gtk_widget_queue_draw(console->drawing_area);
    return G_SOURCE_REMOVE;
}

static void drawing_area_size_allocate(GtkWidget *widget,
                                       GtkAllocation *allocation,
                                       gpointer data)
{
    struct console_window *console = data;

    if (allocation->width <= 0 || allocation->height <= 0)
        return;
    console->pending_width = allocation->width;
    console->pending_height = allocation->height;
    if (console->resize_source != 0)
        g_source_remove(console->resize_source);
    /* A live resize delivers a stream of allocations, and each one puts the
     * console's whole character grid through AROS's geometry path. Coalesce
     * them to about one frame so the console tracks the drag closely without
     * running that path several times per frame; draw_console() paints the
     * background into whatever the console has not caught up with yet. */
    console->resize_source = g_timeout_add(16, apply_pending_resize, console);
    (void)widget;
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

/*
 * How much shell output one main-loop turn will absorb. Draining the socket
 * in one pass is what keeps a burst of output from costing a console write
 * and a repaint per 4KB the socket happened to deliver, but an unbounded
 * drain would let a program that prints without pause hold the main loop --
 * and with it the keyboard and the window -- for as long as it kept
 * printing. At this size the loop comes back for air several times a second
 * even under a flood.
 */
#define OUTPUT_DRAIN_MAX (16 * 1024)

static gboolean read_console(GIOChannel *channel, GIOCondition condition,
                             gpointer data)
{
    struct console_window *console = data;
    char buffer[8192];
    size_t drained = 0;
    int closed = 0;

    (void)channel;
    if (condition & (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) {
        if (console->window)
            gtk_widget_destroy(console->window);
        return G_SOURCE_REMOVE;
    }

    while (drained < OUTPUT_DRAIN_MAX) {
        ssize_t length = recv(console->stream_fd, buffer, sizeof(buffer),
                              MSG_DONTWAIT);

        if (length > 0) {
            /*
             * The real entry point console.c's beginio()/CMD_WRITE would
             * call. ACE's rendering path never goes through DoIO()/BeginIO()
             * -- see HANDOFF.md -- so this calls the real ANSI/CSI parser
             * directly with the same arguments beginio() would have passed
             * it.
             */
            ace_console_device_write(console->device, buffer, (size_t)length);
            drained += (size_t)length;
            continue;
        }
        if (length == 0) {
            closed = 1;
            break;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        closed = 1;
        break;
    }

    if (drained != 0)
        queue_console_damage(console);
    if (closed) {
        if (console->window)
            gtk_widget_destroy(console->window);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void console_destroy(GtkWidget *widget, gpointer data)
{
    struct console_window *console = data;
    int status;

    (void)widget;
    if (console->menu_probe_source != 0) {
        g_source_remove(console->menu_probe_source);
        console->menu_probe_source = 0;
    }
    if (console->resize_source != 0) {
        g_source_remove(console->resize_source);
        console->resize_source = 0;
    }
    ace_appmenu_wayland_forget();
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
    GtkWidget *box;
    GtkWidget *menu;
    char directory[PATH_MAX];
    char shell_path[PATH_MAX];
    const char *session;
    const char *font_candidates[5];
    int sockets[2];
    int i;

    memset(&console, 0, sizeof(console));
    console.stream_fd = -1;
    console.child_pid = -1;
    console.font_size = DEFAULT_FONT_SIZE;
    memcpy(console.palette, default_palette, sizeof(console.palette));
    console.font_family = g_strdup(default_font_candidates[0]);
    for (i = 0; default_font_candidates[i]; i++) {
        if (ace_gfx_font_family_complete(default_font_candidates[i])) {
            g_free(console.font_family);
            console.font_family = g_strdup(default_font_candidates[i]);
            break;
        }
    }
    load_config(&console);
    build_font_candidates(console.font_family, default_font_candidates,
                          font_candidates, G_N_ELEMENTS(font_candidates));
    console.device = ace_console_device_open(CONSOLE_WIDTH, CONSOLE_HEIGHT,
                                             font_candidates, console.font_size);
    if (!console.device) {
        fprintf(stderr, "ace-console: failed to set up console.device\n");
        return 20;
    }
    if (ace_console_device_set_palette(console.device, console.palette) != 0) {
        fprintf(stderr, "ace-console: failed to apply saved palette\n");
        ace_console_device_close(console.device);
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

    /* ACE runs as a native Wayland console, even when DISPLAY is also set.
     * ACE owns the exported menu/action pair; GtkMenuBar remains the local
     * fallback when no appmenu compositor is available. */
    setenv("GDK_BACKEND", "wayland", 1);
    prepare_appmenu_environment();
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

    /* Keep the Wayland app_id, X11 class, launcher desktop file, and
     * icon-theme lookup under the same identity so the panel groups the
     * running console with ACE Shell and displays its application icon. */
    g_set_prgname(ACE_ICON_NAME);
    g_set_application_name("ACE Shell");
    gdk_set_program_class(ACE_ICON_NAME);
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    console.window = window;
    gtk_window_set_title(GTK_WINDOW(window), "ACE Shell");
    gtk_window_set_icon_name(GTK_WINDOW(window), ACE_ICON_NAME);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_default_size(GTK_WINDOW(window), CONSOLE_WIDTH, CONSOLE_HEIGHT);
    g_signal_connect(window, "destroy", G_CALLBACK(console_destroy), &console);
    console.drawing_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(console.drawing_area, TRUE);
    gtk_widget_add_events(console.drawing_area, GDK_KEY_PRESS_MASK);
    g_signal_connect(console.drawing_area, "draw", G_CALLBACK(draw_console), &console);
    g_signal_connect(console.drawing_area, "key-press-event", G_CALLBACK(key_press), &console);
    g_signal_connect(console.drawing_area, "size-allocate",
                     G_CALLBACK(drawing_area_size_allocate), &console);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    menu = build_menu(&console);
    console.menu_bar = menu;
    gtk_box_pack_start(GTK_BOX(box), menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), console.drawing_area, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(window), box);
    (void)export_dbus_menu(&console);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(console.drawing_area);
    console.menu_probe_source = g_timeout_add(250, update_menu_visibility,
                                              &console);

    g_io_add_watch(g_io_channel_unix_new(console.stream_fd),
                   G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                   read_console, &console);
    gtk_main();
    ace_appmenu_wayland_forget();
    unexport_dbus_menu(&console);
    ace_aros_console_editor_close(console.editor);
    ace_console_device_close(console.device);
    g_free(console.font_family);
    return 0;
}
