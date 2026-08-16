#include "tine_console.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

/* Amiga console.device calls CSI 0x9b.  ACE accepts both this native form
 * and the equivalent ESC-[ form, but emitting native CSI keeps this backend
 * usable on an actual Amiga console as well.  Its cursor visibility commands
 * are CSI SP p (visible) and CSI 0 SP p (invisible), not VT ?25h/?25l. */
#define CSI "\233"

WINDOW *tine_stdscr;

static WINDOW command_window;
static struct termios saved_termios;
static bool have_termios;
static bool active;
static bool cursor_visible;
static bool input_delay = true;
static bool ace_console;
static int output_attr = -1;
static int pending_byte = -1;
static volatile sig_atomic_t resized;
static int (*offline_callback)(WINDOW *, int);
static int requested_rows;
static int requested_cols;

static int read_byte(unsigned char *byte, int timeout_ms);

static void
write_all(const char *data, size_t length)
{
    while (length) {
        ssize_t written = write(STDOUT_FILENO, data, length);
        if (written > 0) {
            data += written;
            length -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }
}

static void
emit(const char *data)
{
    write_all(data, strlen(data));
}

static void
emit_csi(const char *format, ...)
{
    char buffer[128];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length > 0)
        write_all(buffer, (size_t)length);
}

static void
apply_attr(int attr)
{
    if (output_attr == attr)
        return;
    emit(CSI "0m");
    if (attr & TINE_A_BOLD)
        emit(CSI "1m");
    if (attr & TINE_A_UNDERLINE)
        emit(CSI "4m");
    if (attr & TINE_A_REVERSE)
        emit(CSI "7m");
    output_attr = attr;
}

static void
move_absolute(int y, int x)
{
    emit_csi("\233%d;%dH", y + 1, x + 1);
}

/* Amiga console.device implements erase-in-display as CSI J without a
 * parameter.  CSI 2J is the VT form, but its parameter makes the Amiga
 * parser reject the sequence and leaves the literal "2J" on the screen.
 * Home first so the Amiga form still clears the entire display. */
static void
clear_screen(void)
{
    move_absolute(0, 0);
    emit(CSI "J");
    move_absolute(0, 0);
}

static void
update_size(void)
{
    struct winsize size;
    int rows = 24;
    int cols = 80;

    if (ace_console) {
        char reply[64];
        size_t length = 0;
        unsigned char byte;
        int reply_rows;
        int reply_cols;

        /* ACE's console.device answers the Amiga DSR size query with
         * CSI 1;1;<rows>;<columns> r.  stdout and stdin are the same
         * session socket here, so this is the authoritative size; there is
         * no host tty for TIOCGWINSZ to inspect. */
        emit(CSI "0 q");
        while (length + 1 < sizeof(reply)) {
            if (read_byte(&byte, -1) != 1)
                break;
            if (length == 0 && byte == 0x1b) {
                if (read_byte(&byte, -1) != 1 || byte != '[')
                    break;
                reply[length++] = (char)0x9b;
                continue;
            }
            if (length == 0 && byte != 0x9b)
                break;
            if (length != 0 || byte == 0x9b)
                reply[length++] = (char)byte;
            if (byte == 'r')
                break;
        }
        reply[length] = '\0';
        if (sscanf(reply, "\2331;1;%d;%d r", &reply_rows,
                   &reply_cols) == 2 && reply_rows > 1 && reply_cols > 0) {
            rows = reply_rows;
            cols = reply_cols;
        }
    } else if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_row)
            rows = size.ws_row;
        if (size.ws_col)
            cols = size.ws_col;
    }
    if (rows < 2)
        rows = 2;
    if (requested_rows > 1 && requested_rows < rows)
        rows = requested_rows;
    if (requested_cols > 0 && requested_cols < cols)
        cols = requested_cols;
    tine_stdscr->top = 0;
    tine_stdscr->rows = rows;
    tine_stdscr->cols = cols;
    tine_stdscr->y = tine_stdscr->x = 0;
    command_window.top = rows - 1;
    command_window.rows = 1;
    command_window.cols = cols;
    command_window.y = command_window.x = 0;
}

void
tine_set_dimensions(int rows, int cols)
{
    requested_rows = rows;
    requested_cols = cols;
}

static void
resize_handler(int signal_number)
{
    (void)signal_number;
    resized = 1;
}

static void
enter_raw(void)
{
    struct termios raw_mode;

    if (!have_termios || active)
        return;
    raw_mode = saved_termios;
    raw_mode.c_iflag &= (tcflag_t)~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw_mode.c_oflag &= (tcflag_t)~OPOST;
    raw_mode.c_cflag |= CS8;
    raw_mode.c_lflag &= (tcflag_t)~(ECHO | ICANON | IEXTEN | ISIG);
    raw_mode.c_cc[VMIN] = 1;
    raw_mode.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_mode);
    active = true;
}

int
tine_init(bool reversed, WINDOW **command_window_out)
{
    struct sigaction action = {0};
    const char *term;

    tine_stdscr = calloc(1, sizeof(*tine_stdscr));
    if (!tine_stdscr)
        return TINE_ERR;
    term = getenv("TERM");
    ace_console = getenv("ACE_SESSION") != NULL && term != NULL &&
                  strcmp(term, "amiga") == 0;
    if (tcgetattr(STDIN_FILENO, &saved_termios) == 0)
        have_termios = true;
    update_size();
    command_window.base_attr = reversed ? TINE_A_REVERSE : TINE_A_NORMAL;
    tine_stdscr->base_attr = reversed ? TINE_A_REVERSE : TINE_A_NORMAL;
    tine_stdscr->attr = tine_stdscr->base_attr;
    command_window.attr = command_window.base_attr;
    if (!ace_console) {
        action.sa_handler = resize_handler;
        sigemptyset(&action.sa_mask);
        sigaction(SIGWINCH, &action, NULL);
    }
    enter_raw();
    active = true;
    cursor_visible = true;
    emit(CSI " p");
    clear_screen();
    if (ace_console)
        emit(CSI "12{");
    apply_attr(tine_stdscr->base_attr);
    *command_window_out = &command_window;
    if (offline_callback)
        offline_callback(&command_window, command_window.top ? -1 : 1);
    return TINE_OK;
}

WINDOW *
tine_initscr(void)
{
    WINDOW *ignored;
    return tine_init(false, &ignored) == TINE_OK ? tine_stdscr : NULL;
}

void
tine_endwin(void)
{
    if (!active)
        return;
    emit(CSI "0m" CSI " p" "\n");
    if (have_termios)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
    active = false;
    output_attr = -1;
}

void tine_raw(void) { enter_raw(); }
void tine_noecho(void) { }
void tine_nonl(void) { }
void tine_intrflush(WINDOW *window, bool enabled) { (void)window; (void)enabled; }
void tine_keypad(WINDOW *window, bool enabled) { (void)window; (void)enabled; }
void tine_ripoffline(int line, int (*callback)(WINDOW *, int))
{
    (void)line;
    offline_callback = callback;
}

void
tine_refresh(void)
{
    enter_raw();
    if (tine_stdscr)
        tine_wrefresh(tine_stdscr);
}

void
tine_wbkgdset(WINDOW *window, int attr)
{
    window->base_attr = attr;
    tine_wattrset(window, attr);
}

void
tine_werase(WINDOW *window)
{
    apply_attr(window->base_attr);
    if (window == tine_stdscr) {
        clear_screen();
    } else {
        move_absolute(window->top, 0);
        emit(CSI "K");
        move_absolute(window->top, 0);
    }
    window->y = window->x = 0;
    window->attr = window->base_attr;
}

void
tine_wmove(WINDOW *window, int y, int x)
{
    if (y < 0)
        y = 0;
    if (x < 0)
        x = 0;
    window->y = y < window->rows ? y : window->rows - 1;
    window->x = x < window->cols ? x : window->cols - 1;
    move_absolute(window->top + window->y, window->x);
}

void
tine_getyx(WINDOW *window, int *y, int *x)
{
    *y = window->y;
    *x = window->x;
}

void
tine_getmaxyx(WINDOW *window, int *y, int *x)
{
    *y = window->rows;
    *x = window->cols;
}

void tine_delwin(WINDOW *window) { (void)window; }

void tine_wrefresh(WINDOW *window)
{
    move_absolute(window->top + window->y, window->x);
    fflush(stdout);
}

void tine_wattrset(WINDOW *window, int attr)
{
    window->attr = attr;
    apply_attr(attr);
}

void tine_wattron(WINDOW *window, int attr)
{
    tine_wattrset(window, window->attr | attr);
}

void tine_wattroff(WINDOW *window, int attr)
{
    tine_wattrset(window, window->attr & ~attr);
}

void
tine_waddch(WINDOW *window, wchar_t character)
{
    char buffer[MB_LEN_MAX];
    mbstate_t state = {0};
    int length;
    int width;

    if (character == L'\0')
        character = L' ';
    length = wcrtomb(buffer, character, &state);
    if (length < 0) {
        buffer[0] = '?';
        length = 1;
    }
    write_all(buffer, (size_t)length);
    width = wcwidth(character);
    if (width > 0)
        window->x += width;
    if (window->x >= window->cols)
        window->x = window->cols - 1;
}

void
tine_waddwstr(WINDOW *window, const wchar_t *string)
{
    while (*string)
        tine_waddch(window, *string++);
}

void
tine_mvwaddstr(WINDOW *window, int y, int x, const char *string)
{
    tine_wmove(window, y, x);
    write_all(string, strlen(string));
    window->x += (int)strlen(string);
    if (window->x >= window->cols)
        window->x = window->cols - 1;
}

void
tine_mvwprintw(WINDOW *window, int y, int x, const char *format, ...)
{
    char buffer[4096];
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length < 0)
        return;
    buffer[sizeof(buffer) - 1] = '\0';
    tine_mvwaddstr(window, y, x, buffer);
}

void
tine_mvwhline(WINDOW *window, int y, int x, wchar_t character, int count)
{
    tine_wmove(window, y, x);
    while (count-- > 0)
        tine_waddch(window, character);
}

void tine_redrawwin(WINDOW *window) { tine_wrefresh(window); }

int
tine_curs_set(int visible)
{
    int old = cursor_visible ? 1 : 0;
    cursor_visible = visible != 0;
    emit(cursor_visible ? CSI " p" : CSI "0 p");
    return old;
}

void tine_nodelay(WINDOW *window, bool enabled)
{
    (void)window;
    input_delay = !enabled;
}

void
tine_napms(unsigned milliseconds)
{
    struct timeval delay = {
        .tv_sec = (long)(milliseconds / 1000),
        .tv_usec = (long)(milliseconds % 1000) * 1000,
    };
    (void)select(0, NULL, NULL, NULL, &delay);
}

static int
read_byte(unsigned char *byte, int timeout_ms)
{
    fd_set set;
    struct timeval timeout;
    int result;
    ssize_t length;

    if (pending_byte >= 0) {
        *byte = (unsigned char)pending_byte;
        pending_byte = -1;
        return 1;
    }
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    result = select(STDIN_FILENO + 1, &set, NULL,
                    NULL, timeout_ms < 0 ? NULL : &timeout);
    if (result <= 0)
        return result;
    do {
        length = read(STDIN_FILENO, byte, 1);
    } while (length < 0 && errno == EINTR);
    return length == 1 ? 1 : 0;
}

static int
special_sequence(const char *sequence, size_t length)
{
    const char *final = sequence + length - 1;
    int value = 0;

    if (length == 0)
        return 0;
    if (length == 2 && sequence[0] == ' ' && *final == 'A')
        return TINE_KEY_SLEFT;
    if (length == 2 && sequence[0] == ' ' && *final == '@')
        return TINE_KEY_SRIGHT;
    switch (*final) {
    case 'A': return TINE_KEY_UP;
    case 'B': return TINE_KEY_DOWN;
    case 'C': return TINE_KEY_RIGHT;
    case 'D': return TINE_KEY_LEFT;
    case 'H': return TINE_KEY_HOME;
    case 'F': return TINE_KEY_END;
    case 'Z': return TINE_KEY_BTAB;
    case '~':
        (void)sscanf(sequence, "%d", &value);
        switch (value) {
        case 1: case 7: return TINE_KEY_HOME;
        case 2: return TINE_KEY_IC;
        case 3: return TINE_KEY_DC;
        case 4: case 8: return TINE_KEY_END;
        case 5: return TINE_KEY_PPAGE;
        case 6: return TINE_KEY_NPAGE;
        case 10: return TINE_KEY_F(1);
        case 11: return TINE_KEY_F(2);
        case 12: return TINE_KEY_F(3);
        case 13: return TINE_KEY_F(4);
        case 14: return TINE_KEY_F(5);
        case 15: return TINE_KEY_F(6);
        case 17: return TINE_KEY_F(7);
        case 18: return TINE_KEY_F(8);
        case 19: return TINE_KEY_F(9);
        case 20: return TINE_KEY_F(10);
        default: return 0;
        }
    default: return 0;
    }
}

static int
read_special(wint_t *character, unsigned char first)
{
    char sequence[32];
    size_t length = 0;
    unsigned char byte;

    if (first == '\033') {
        if (read_byte(&byte, 30) != 1) {
            *character = 0x1b;
            return TINE_OK;
        }
        if (byte != '[' && byte != 'O') {
            pending_byte = byte;
            *character = 0x1b;
            return TINE_OK;
        }
    }
    while (length < sizeof(sequence) - 1) {
        if (read_byte(&byte, 100) != 1)
            break;
        sequence[length++] = (char)byte;
        if (byte >= '@' && byte <= '~')
            break;
    }
    sequence[length] = '\0';
    if (length >= 3 && sequence[0] == '1' && sequence[1] == '2' &&
        sequence[2] == ';' && sequence[length - 1] == '|') {
        if (ace_console) {
            update_size();
            *character = TINE_KEY_RESIZE;
            return TINE_KEY_CODE;
        }
    }
    {
        int key = special_sequence(sequence, length);
        if (!key) {
            *character = 0x1b;
            return TINE_OK;
        }
        *character = (wint_t)key;
        return TINE_KEY_CODE;
    }
}

int
tine_wget_wch(WINDOW *window, wint_t *character)
{
    unsigned char first;
    char utf8[MB_LEN_MAX];
    mbstate_t state = {0};
    size_t length = 1;
    int expected = 0;

    (void)window;
    if (resized) {
        resized = 0;
        update_size();
        *character = TINE_KEY_RESIZE;
        return TINE_KEY_CODE;
    }
    if (read_byte(&first, input_delay ? -1 : 0) != 1)
        return TINE_ERR;
    if (first == 0x1b || first == 0x9b)
        return read_special(character, first);
    utf8[0] = (char)first;
    if ((first & 0xe0) == 0xc0) expected = 1;
    else if ((first & 0xf0) == 0xe0) expected = 2;
    else if ((first & 0xf8) == 0xf0) expected = 3;
    while ((int)length <= expected && length < sizeof(utf8)) {
        if (read_byte(&first, -1) != 1)
            return TINE_ERR;
        utf8[length++] = (char)first;
    }
    utf8[length] = '\0';
    if (mbrtowc(character, utf8, length, &state) == (size_t)-1)
        *character = L'?';
    return TINE_OK;
}
