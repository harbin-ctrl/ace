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
#include "con_handler.h"

#define TERM_ROWS 32
#define TERM_COLS 100
#define TERM_MAX_PARAMS 16
#define INPUT_MAX 4096

struct term_cell {
    unsigned char character;
    unsigned char foreground;
    unsigned char background;
    unsigned char bold;
    unsigned char italic;
    unsigned char underline;
    unsigned char reverse;
};

struct terminal {
    struct term_cell cells[TERM_ROWS][TERM_COLS];
    int row;
    int column;
    int saved_row;
    int saved_column;
    unsigned char foreground;
    unsigned char background;
    unsigned char bold;
    unsigned char italic;
    unsigned char underline;
    unsigned char reverse;
    unsigned char cursor_visible;
    unsigned char parser_state;
    int params[TERM_MAX_PARAMS];
    size_t parameter_count;
    int parameter_value;
    int parameter_has_value;
};

struct console_window {
    struct terminal terminal;
    GtkWidget *window;
    GtkWidget *drawing_area;
    int stream_fd;
    pid_t child_pid;
    char input[INPUT_MAX];
    size_t input_length;
    size_t input_cursor;
    int input_row;
    int input_column;
    gboolean input_active;
    struct amiga_console_device device;
    struct amiga_console_unit *console_unit;
    struct amiga_con_handler handler;
};

struct pending_output {
    struct console_window *console;
    size_t length;
    unsigned char data[];
};

static const double palette[16][3] = {
    {0.00, 0.00, 0.00}, {0.67, 0.00, 0.00}, {0.00, 0.50, 0.00},
    {0.67, 0.33, 0.00}, {0.00, 0.00, 0.67}, {0.67, 0.00, 0.67},
    {0.00, 0.50, 0.50}, {0.00, 0.00, 0.00}, {0.50, 0.50, 0.50},
    {1.00, 0.00, 0.00}, {0.00, 0.75, 0.00}, {1.00, 0.67, 0.00},
    {0.00, 0.33, 1.00}, {1.00, 0.00, 1.00}, {0.00, 0.75, 0.75},
    {1.00, 1.00, 1.00}
};

static struct term_cell current_cell(struct terminal *terminal)
{
    struct term_cell cell;

    cell.character = ' ';
    cell.foreground = terminal->foreground;
    cell.background = terminal->background;
    cell.bold = terminal->bold;
    cell.italic = terminal->italic;
    cell.underline = terminal->underline;
    cell.reverse = terminal->reverse;
    return cell;
}

static void terminal_reset(struct terminal *terminal)
{
    memset(terminal, 0, sizeof(*terminal));
    terminal->foreground = 0;
    terminal->background = 8;
    terminal->cursor_visible = 1;
    for (int row = 0; row < TERM_ROWS; row++)
        for (int column = 0; column < TERM_COLS; column++)
            terminal->cells[row][column] = current_cell(terminal);
}

static void terminal_scroll(struct terminal *terminal)
{
    memmove(terminal->cells[0], terminal->cells[1],
            sizeof(terminal->cells[0]) * (TERM_ROWS - 1));
    for (int column = 0; column < TERM_COLS; column++)
        terminal->cells[TERM_ROWS - 1][column] = current_cell(terminal);
    terminal->row = TERM_ROWS - 1;
}

static void terminal_linefeed(struct terminal *terminal)
{
    terminal->row++;
    if (terminal->row >= TERM_ROWS)
        terminal_scroll(terminal);
}

static void terminal_put_character(struct terminal *terminal, unsigned char value)
{
    if (value == '\n') {
        terminal_linefeed(terminal);
        return;
    }
    if (value == '\r') {
        terminal->column = 0;
        return;
    }
    if (value == '\b') {
        if (terminal->column > 0)
            terminal->column--;
        return;
    }
    if (value == '\t') {
        terminal->column = (terminal->column + 8) & ~7;
        if (terminal->column >= TERM_COLS)
            terminal->column = TERM_COLS - 1;
        return;
    }
    if (value < 0x20 || value == 0x7f)
        return;

    terminal->cells[terminal->row][terminal->column] = current_cell(terminal);
    terminal->cells[terminal->row][terminal->column].character = value;
    terminal->column++;
    if (terminal->column >= TERM_COLS) {
        terminal->column = 0;
        terminal_linefeed(terminal);
    }
}

static int parameter(struct terminal *terminal, size_t index, int fallback)
{
    if (index >= terminal->parameter_count || !terminal->params[index])
        return fallback;
    return terminal->params[index];
}

static void erase_cells(struct terminal *terminal, int first_row, int first_column,
                        int last_row, int last_column)
{
    for (int row = first_row; row <= last_row; row++) {
        int begin = row == first_row ? first_column : 0;
        int end = row == last_row ? last_column : TERM_COLS - 1;
        for (int column = begin; column <= end; column++)
            terminal->cells[row][column] = current_cell(terminal);
    }
}

static void terminal_sgr(struct terminal *terminal)
{
    if (terminal->parameter_count == 0) {
        terminal->foreground = 0;
        terminal->background = 8;
        terminal->bold = terminal->italic = terminal->underline = 0;
        terminal->reverse = 0;
        return;
    }
    for (size_t index = 0; index < terminal->parameter_count; index++) {
        int value = terminal->params[index];
        if (value == 0) {
            terminal->foreground = 0;
            terminal->background = 8;
            terminal->bold = terminal->italic = terminal->underline = 0;
            terminal->reverse = 0;
        } else if (value == 1) {
            terminal->bold = 1;
        } else if (value == 3) {
            terminal->italic = 1;
        } else if (value == 4) {
            terminal->underline = 1;
        } else if (value == 7) {
            terminal->reverse = 1;
        } else if (value == 22) {
            terminal->bold = 0;
        } else if (value == 23) {
            terminal->italic = 0;
        } else if (value == 24) {
            terminal->underline = 0;
        } else if (value == 27) {
            terminal->reverse = 0;
        } else if (value >= 30 && value <= 37) {
            terminal->foreground = (unsigned char)(value - 30);
        } else if (value == 39) {
            terminal->foreground = 0;
        } else if (value >= 40 && value <= 47) {
            terminal->background = (unsigned char)(value - 40);
        } else if (value == 49) {
            terminal->background = 8;
        }
    }
}

static void terminal_finish_csi(struct terminal *terminal, unsigned char final)
{
    int count = (int)terminal->parameter_count;
    int amount;

    if (terminal->parameter_has_value || count == 0) {
        if (count < TERM_MAX_PARAMS)
            terminal->params[terminal->parameter_count++] =
                terminal->parameter_has_value ? terminal->parameter_value : 0;
    }
    amount = parameter(terminal, 0, 1);
    switch (final) {
    case 'A':
        terminal->row -= amount;
        break;
    case 'B':
        terminal->row += amount;
        break;
    case 'C':
    case 'a':
        terminal->column += amount;
        break;
    case 'D':
        terminal->column -= amount;
        break;
    case 'G':
    case '`':
        terminal->column = amount - 1;
        break;
    case 'd':
        terminal->row = amount - 1;
        break;
    case 'H':
    case 'f':
        terminal->row = parameter(terminal, 0, 1) - 1;
        terminal->column = parameter(terminal, 1, 1) - 1;
        break;
    case 'J':
        if (parameter(terminal, 0, 0) == 2)
            erase_cells(terminal, 0, 0, TERM_ROWS - 1, TERM_COLS - 1);
        else if (parameter(terminal, 0, 0) == 1)
            erase_cells(terminal, 0, 0, terminal->row, terminal->column);
        else
            erase_cells(terminal, terminal->row, terminal->column,
                        TERM_ROWS - 1, TERM_COLS - 1);
        break;
    case 'K':
        if (parameter(terminal, 0, 0) == 2)
            erase_cells(terminal, terminal->row, 0, terminal->row, TERM_COLS - 1);
        else if (parameter(terminal, 0, 0) == 1)
            erase_cells(terminal, terminal->row, 0, terminal->row, terminal->column);
        else
            erase_cells(terminal, terminal->row, terminal->column,
                        terminal->row, TERM_COLS - 1);
        break;
    case 'm':
        terminal_sgr(terminal);
        break;
    case 's':
        terminal->saved_row = terminal->row;
        terminal->saved_column = terminal->column;
        break;
    case 'u':
        terminal->row = terminal->saved_row;
        terminal->column = terminal->saved_column;
        break;
    case 'h':
        if (parameter(terminal, 0, 0) == 25)
            terminal->cursor_visible = 1;
        break;
    case 'l':
        if (parameter(terminal, 0, 0) == 25)
            terminal->cursor_visible = 0;
        break;
    default:
        break;
    }
    if (terminal->row < 0)
        terminal->row = 0;
    if (terminal->row >= TERM_ROWS)
        terminal->row = TERM_ROWS - 1;
    if (terminal->column < 0)
        terminal->column = 0;
    if (terminal->column >= TERM_COLS)
        terminal->column = TERM_COLS - 1;
    terminal->parameter_count = 0;
    terminal->parameter_value = 0;
    terminal->parameter_has_value = 0;
    terminal->parser_state = 0;
}

static void terminal_feed(struct terminal *terminal, const unsigned char *data,
                          size_t length)
{
    for (size_t index = 0; index < length; index++) {
        unsigned char value = data[index];

        if (terminal->parser_state == 1) {
            if (value == '[' || value == 0x9b) {
                terminal->parser_state = 2;
                terminal->parameter_count = 0;
                terminal->parameter_value = 0;
                terminal->parameter_has_value = 0;
            } else {
                terminal->parser_state = 0;
            }
            continue;
        }
        if (terminal->parser_state == 2) {
            if (value >= '0' && value <= '9') {
                terminal->parameter_value = terminal->parameter_value * 10 + value - '0';
                terminal->parameter_has_value = 1;
            } else if (value == ';') {
                if (terminal->parameter_count < TERM_MAX_PARAMS)
                    terminal->params[terminal->parameter_count++] =
                        terminal->parameter_has_value ? terminal->parameter_value : 0;
                terminal->parameter_value = 0;
                terminal->parameter_has_value = 0;
            } else if (value >= 0x40 && value <= 0x7e) {
                terminal_finish_csi(terminal, value);
            }
            continue;
        }
        if (value == 0x1b) {
            terminal->parser_state = 1;
        } else if (value == 0x9b) {
            terminal->parser_state = 2;
            terminal->parameter_count = 0;
            terminal->parameter_value = 0;
            terminal->parameter_has_value = 0;
        } else {
            terminal_put_character(terminal, value);
        }
    }
}

static gboolean apply_output(gpointer data)
{
    struct pending_output *output = data;
    struct console_window *console = output->console;

    if (console->input_active) {
        console->input_active = FALSE;
        console->input_length = 0;
        console->input_cursor = 0;
    }
    terminal_feed(&console->terminal, output->data, output->length);
    if (!console->input_active) {
        console->input_row = console->terminal.row;
        console->input_column = console->terminal.column;
        console->input_active = TRUE;
    }
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

static void draw_cell(cairo_t *cr, int row, int column, struct term_cell cell)
{
    double cell_width = 9.0;
    double cell_height = 18.0;
    unsigned char foreground = cell.foreground;
    unsigned char background = cell.background;
    char text[2] = {(char)(cell.character ? cell.character : ' '), '\0'};

    if (cell.reverse) {
        unsigned char swap = foreground;
        foreground = background;
        background = swap;
    }
    cairo_set_source_rgb(cr, palette[background][0], palette[background][1],
                         palette[background][2]);
    cairo_rectangle(cr, column * cell_width, row * cell_height,
                    cell_width + 1, cell_height + 1);
    cairo_fill(cr);
    cairo_select_font_face(cr, "Monospace",
        cell.italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL,
        cell.bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14.0);
    cairo_set_source_rgb(cr, palette[foreground][0], palette[foreground][1],
                         palette[foreground][2]);
    cairo_move_to(cr, column * cell_width, row * cell_height + 14.0);
    cairo_show_text(cr, text);
    if (cell.underline) {
        cairo_move_to(cr, column * cell_width, row * cell_height + 16.0);
        cairo_line_to(cr, (column + 1) * cell_width, row * cell_height + 16.0);
        cairo_stroke(cr);
    }
}

static void draw_input(cairo_t *cr, struct console_window *console)
{
    struct term_cell cell;
    int row = console->input_row;
    int column = console->input_column;

    if (!console->input_active)
        return;
    cell = current_cell(&console->terminal);
    for (size_t index = 0; index < console->input_length; index++) {
        if (console->input[index] == '\n') {
            row++;
            column = 0;
        } else {
            cell.character = (unsigned char)console->input[index];
            draw_cell(cr, row, column, cell);
            column++;
            if (column >= TERM_COLS) {
                column = 0;
                row++;
            }
        }
        if (row >= TERM_ROWS)
            break;
    }
}

/*
 * The bootstrap shell still consumes a PTY.  Feed that PTY through the
 * console.device read path so this window exercises the same input direction
 * that the imported CON: handler will use later.
 */
static int send_input(struct console_window *console, const void *data,
                      size_t length, int raw)
{
    unsigned char translated[INPUT_MAX + 8];
    size_t actual;
    size_t offset = 0;
    int error;

    if (length > sizeof(translated))
        return -1;
    error = amiga_con_handler_SetRaw(&console->handler, raw);
    if (error == AMIGA_IOERR_OK)
        error = amiga_con_handler_FeedInput(&console->handler, data, length);
    if (error == AMIGA_IOERR_OK)
        error = amiga_con_handler_Read(&console->handler, translated, length,
                                       &actual);
    if (error != AMIGA_IOERR_OK || actual != length) {
        (void)amiga_con_handler_SetRaw(&console->handler, 0);
        return -1;
    }
    while (offset < actual) {
        ssize_t written = write(console->stream_fd, translated + offset,
                                actual - offset);

        if (written <= 0)
            return -1;
        offset += (size_t)written;
    }
    (void)amiga_con_handler_SetRaw(&console->handler, 0);
    return 0;
}

static gboolean draw_console(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    struct console_window *console = data;
    GtkAllocation allocation;

    gtk_widget_get_allocation(widget, &allocation);
    cairo_set_source_rgb(cr, 0.50, 0.50, 0.50);
    cairo_paint(cr);
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int column = 0; column < TERM_COLS; column++) {
            struct term_cell cell = console->terminal.cells[row][column];
            draw_cell(cr, row, column, cell);
        }
    }
    draw_input(cr, console);
    if (console->terminal.cursor_visible) {
        int cursor_row = console->input_active ? console->input_row : console->terminal.row;
        int cursor_column = console->input_active ? console->input_column : console->terminal.column;
        for (size_t index = 0; console->input_active && index < console->input_cursor; index++) {
            if (console->input[index] == '\n') {
                cursor_row++;
                cursor_column = 0;
            } else if (++cursor_column >= TERM_COLS) {
                cursor_column = 0;
                cursor_row++;
            }
        }
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.65);
        cairo_rectangle(cr, cursor_column * 9.0, cursor_row * 18.0, 9.0, 18.0);
        cairo_fill(cr);
    }
    (void)allocation;
    return FALSE;
}

static void submit_input(struct console_window *console)
{
    char line[INPUT_MAX + 2];
    size_t length = console->input_length;

    memcpy(line, console->input, length);
    line[length++] = '\n';
    line[length] = '\0';
    (void)send_input(console, line, length, 0);
    for (size_t index = 0; index < console->input_length; index++)
        terminal_put_character(&console->terminal,
                               (unsigned char)console->input[index]);
    console->input_active = FALSE;
    console->input_length = 0;
    console->input_cursor = 0;
    terminal_put_character(&console->terminal, '\n');
    gtk_widget_queue_draw(console->drawing_area);
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
        submit_input(console);
        return TRUE;
    }
    if (key == GDK_KEY_BackSpace) {
        if (console->input_cursor > 0) {
            memmove(console->input + console->input_cursor - 1,
                    console->input + console->input_cursor,
                    console->input_length - console->input_cursor + 1);
            console->input_cursor--;
            console->input_length--;
            gtk_widget_queue_draw(console->drawing_area);
        }
        return TRUE;
    }
    if (key == GDK_KEY_Delete) {
        if (console->input_cursor < console->input_length) {
            memmove(console->input + console->input_cursor,
                    console->input + console->input_cursor + 1,
                    console->input_length - console->input_cursor);
            console->input_length--;
            gtk_widget_queue_draw(console->drawing_area);
        }
        return TRUE;
    }
    if (key == GDK_KEY_Left) {
        if (console->input_cursor > 0)
            console->input_cursor--;
        gtk_widget_queue_draw(console->drawing_area);
        return TRUE;
    }
    if (key == GDK_KEY_Right) {
        if (console->input_cursor < console->input_length)
            console->input_cursor++;
        gtk_widget_queue_draw(console->drawing_area);
        return TRUE;
    }
    if (key == GDK_KEY_Home) {
        console->input_cursor = 0;
        gtk_widget_queue_draw(console->drawing_area);
        return TRUE;
    }
    if (key == GDK_KEY_End) {
        console->input_cursor = console->input_length;
        gtk_widget_queue_draw(console->drawing_area);
        return TRUE;
    }
    if (key == GDK_KEY_Up || key == GDK_KEY_Down || key == GDK_KEY_Tab) {
        const char *sequence = key == GDK_KEY_Up ? "\033[A" :
                               key == GDK_KEY_Down ? "\033[B" : "\t";
        (void)send_input(console, sequence, strlen(sequence), 1);
        return TRUE;
    }
    if (modifiers & GDK_CONTROL_MASK) {
        if (key == GDK_KEY_c)
            (void)send_input(console, "\003", 1, 1);
        else if (key == GDK_KEY_d)
            (void)send_input(console, "\004", 1, 1);
        return TRUE;
    }

    unicode = gdk_keyval_to_unicode(key);
    if (unicode >= 0x20 && unicode != 0x7f &&
        g_unichar_validate(unicode) &&
        g_unichar_to_utf8(unicode, utf8) < (gint)sizeof(utf8)) {
        int length = g_unichar_to_utf8(unicode, utf8);
        if (console->input_length + (size_t)length < sizeof(console->input)) {
            memmove(console->input + console->input_cursor + length,
                    console->input + console->input_cursor,
                    console->input_length - console->input_cursor + 1);
            memcpy(console->input + console->input_cursor, utf8, (size_t)length);
            console->input_length += (size_t)length;
            console->input_cursor += (size_t)length;
            gtk_widget_queue_draw(console->drawing_area);
        }
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
    if (amiga_con_handler_Write(&console->handler, buffer, (size_t)length,
                                &actual) !=
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
    terminal_reset(&console.terminal);
    console.device.context = &console;
    console.device.write = render_output;
    if (amiga_console_OpenDevice(&console.device, &console.console_unit) !=
        AMIGA_IOERR_OK)
        return 20;
    if (amiga_con_handler_Open(console.console_unit, &console.handler) !=
        AMIGA_IOERR_OK)
        return 20;
    if (argc != 3 || strcmp(argv[1], "--session") != 0) {
        fprintf(stderr, "usage: %s --session SESSION\n", argv[0]);
        return 20;
    }
    session = argv[2];
    if (executable_directory(argv[0], directory, sizeof(directory)) != 0)
        return 20;
    if (snprintf(shell_path, sizeof(shell_path), "%s/ace-shell", directory) >=
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
    gtk_window_set_default_size(GTK_WINDOW(window), 900, 576);
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
    amiga_con_handler_Close(&console.handler);
    amiga_console_CloseDevice(console.console_unit);
    return 0;
}
