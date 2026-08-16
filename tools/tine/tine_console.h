#ifndef TINE_CONSOLE_H
#define TINE_CONSOLE_H

#include <stdbool.h>
#include <stdarg.h>
#include <wchar.h>

/* TINE uses only a very small part of curses.  Keep this interface local to
 * TINE: the editor sees a screen and logical windows, while this layer emits
 * Amiga/ANSI console control sequences. */
typedef struct tine_window WINDOW;

struct tine_window {
    int top;
    int rows;
    int cols;
    int y;
    int x;
    int attr;
    int base_attr;
};

enum {
    TINE_OK = 0,
    TINE_ERR = -1,
    TINE_KEY_CODE = 1,
    TINE_KEY_BACKSPACE = 263,
    TINE_KEY_DC = 330,
};

/* Keep special key values outside the Unicode range. */
#undef TINE_KEY_DOWN
enum {
    TINE_KEY_UP = 1001,
    TINE_KEY_DOWN,
    TINE_KEY_LEFT,
    TINE_KEY_RIGHT,
    TINE_KEY_SLEFT,
    TINE_KEY_SRIGHT,
    TINE_KEY_RESIZE,
    TINE_KEY_ENTER,
    TINE_KEY_BTAB,
    TINE_KEY_IC,
    TINE_KEY_HOME,
    TINE_KEY_END,
    TINE_KEY_PPAGE,
    TINE_KEY_NPAGE,
    TINE_KEY_F1,
};

#define TINE_KEY_F(n) (TINE_KEY_F1 + ((n) - 1))

#define TINE_A_NORMAL    0
#define TINE_A_BOLD      0x01
#define TINE_A_UNDERLINE 0x02
#define TINE_A_REVERSE   0x04

extern WINDOW *tine_stdscr;

WINDOW *tine_initscr(void);
void tine_set_dimensions(int rows, int cols);
int tine_init(bool reversed, WINDOW **command_window);
void tine_endwin(void);
void tine_refresh(void);
void tine_raw(void);
void tine_noecho(void);
void tine_nonl(void);
void tine_intrflush(WINDOW *window, bool enabled);
void tine_keypad(WINDOW *window, bool enabled);
void tine_ripoffline(int line, int (*callback)(WINDOW *, int));

void tine_wbkgdset(WINDOW *window, int attr);
void tine_werase(WINDOW *window);
void tine_wmove(WINDOW *window, int y, int x);
void tine_getyx(WINDOW *window, int *y, int *x);
void tine_getmaxyx(WINDOW *window, int *y, int *x);
void tine_delwin(WINDOW *window);
void tine_wrefresh(WINDOW *window);
void tine_wattrset(WINDOW *window, int attr);
void tine_wattron(WINDOW *window, int attr);
void tine_wattroff(WINDOW *window, int attr);
void tine_waddch(WINDOW *window, wchar_t character);
void tine_waddwstr(WINDOW *window, const wchar_t *string);
void tine_mvwaddstr(WINDOW *window, int y, int x, const char *string);
void tine_mvwprintw(WINDOW *window, int y, int x, const char *format, ...);
void tine_mvwhline(WINDOW *window, int y, int x, wchar_t character,
                   int count);
void tine_redrawwin(WINDOW *window);
int tine_curs_set(int visible);
void tine_nodelay(WINDOW *window, bool enabled);
int tine_wget_wch(WINDOW *window, wint_t *character);
void tine_napms(unsigned milliseconds);

/* TINE's source remains readable while the old curses surface is removed. */
#define stdscr       tine_stdscr
#define TRUE         1
#define FALSE        0
#define initscr      tine_initscr
#define OK           TINE_OK
#define ERR          TINE_ERR
#define KEY_CODE_YES TINE_KEY_CODE
#define KEY_BACKSPACE TINE_KEY_BACKSPACE
#define KEY_DC       TINE_KEY_DC
#define KEY_UP       TINE_KEY_UP
#define KEY_DOWN     TINE_KEY_DOWN
#define KEY_LEFT     TINE_KEY_LEFT
#define KEY_RIGHT    TINE_KEY_RIGHT
#define KEY_SLEFT    TINE_KEY_SLEFT
#define KEY_SRIGHT   TINE_KEY_SRIGHT
#define KEY_RESIZE   TINE_KEY_RESIZE
#define KEY_ENTER    TINE_KEY_ENTER
#define KEY_BTAB     TINE_KEY_BTAB
#define KEY_IC       TINE_KEY_IC
#define KEY_HOME     TINE_KEY_HOME
#define KEY_END      TINE_KEY_END
#define KEY_PPAGE    TINE_KEY_PPAGE
#define KEY_NPAGE    TINE_KEY_NPAGE
#define KEY_F(n)     TINE_KEY_F(n)
#define A_NORMAL     TINE_A_NORMAL
#define A_BOLD       TINE_A_BOLD
#define A_UNDERLINE  TINE_A_UNDERLINE
#define A_REVERSE    TINE_A_REVERSE
#define endwin       tine_endwin
#define refresh      tine_refresh
#define raw          tine_raw
#define noecho       tine_noecho
#define nonl         tine_nonl
#define intrflush    tine_intrflush
#define keypad       tine_keypad
#define ripoffline   tine_ripoffline
#define wbkgdset     tine_wbkgdset
#define werase       tine_werase
#define wmove        tine_wmove
#define getyx(w,y,x) do { \
    int tine_y__, tine_x__; \
    tine_getyx((w), &tine_y__, &tine_x__); \
    (y) = tine_y__; (x) = tine_x__; \
} while (0)
#define getmaxyx(w,y,x) do { \
    int tine_y__, tine_x__; \
    tine_getmaxyx((w), &tine_y__, &tine_x__); \
    (y) = tine_y__; (x) = tine_x__; \
} while (0)
#define delwin       tine_delwin
#define wrefresh     tine_wrefresh
#define wattrset      tine_wattrset
#define wattron       tine_wattron
#define wattroff      tine_wattroff
#define waddch        tine_waddch
#define waddwstr      tine_waddwstr
#define mvwaddstr     tine_mvwaddstr
#define mvwprintw     tine_mvwprintw
#define mvwhline      tine_mvwhline
#define redrawwin     tine_redrawwin
#define curs_set      tine_curs_set
#define nodelay       tine_nodelay
#define wget_wch      tine_wget_wch
#define napms         tine_napms

#endif
