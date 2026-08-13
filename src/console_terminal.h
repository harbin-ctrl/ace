#ifndef ACE_CONSOLE_TERMINAL_H
#define ACE_CONSOLE_TERMINAL_H

#include <stddef.h>

#define TERM_ROWS 32
#define TERM_COLS 100
#define TERM_MAX_PARAMS 16

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

void ace_console_terminal_reset(struct terminal *terminal);
void ace_console_terminal_feed(struct terminal *terminal,
                               const unsigned char *data, size_t length);
void ace_console_terminal_put(struct terminal *terminal, unsigned char value);
struct term_cell ace_console_terminal_current_cell(
    const struct terminal *terminal);

#endif
