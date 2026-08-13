#include "console_terminal.h"

#include <string.h>

static struct term_cell current_cell(const struct terminal *terminal)
{
    struct term_cell cell = {
        ' ', terminal->foreground, terminal->background, terminal->bold,
        terminal->italic, terminal->underline, terminal->reverse
    };
    return cell;
}

struct term_cell ace_console_terminal_current_cell(
    const struct terminal *terminal)
{
    return current_cell(terminal);
}

void ace_console_terminal_reset(struct terminal *terminal)
{
    memset(terminal, 0, sizeof(*terminal));
    terminal->background = 8;
    terminal->cursor_visible = 1;
    for (int row = 0; row < TERM_ROWS; row++)
        for (int column = 0; column < TERM_COLS; column++)
            terminal->cells[row][column] = current_cell(terminal);
}

static void scroll(struct terminal *terminal)
{
    memmove(terminal->cells[0], terminal->cells[1],
            sizeof(terminal->cells[0]) * (TERM_ROWS - 1));
    for (int column = 0; column < TERM_COLS; column++)
        terminal->cells[TERM_ROWS - 1][column] = current_cell(terminal);
    terminal->row = TERM_ROWS - 1;
}

static void linefeed(struct terminal *terminal)
{
    /* CON: treats a linefeed as a complete Amiga console line ending. */
    terminal->column = 0;
    terminal->row++;
    if (terminal->row >= TERM_ROWS)
        scroll(terminal);
}

void ace_console_terminal_put(struct terminal *terminal, unsigned char value)
{
    if (value == '\n') {
        linefeed(terminal);
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
    if (++terminal->column >= TERM_COLS)
        linefeed(terminal);
}

static int parameter(const struct terminal *terminal, size_t index,
                     int fallback)
{
    if (index >= terminal->parameter_count || !terminal->params[index])
        return fallback;
    return terminal->params[index];
}

static void erase_cells(struct terminal *terminal, int first_row,
                        int first_column, int last_row, int last_column)
{
    for (int row = first_row; row <= last_row; row++) {
        int begin = row == first_row ? first_column : 0;
        int end = row == last_row ? last_column : TERM_COLS - 1;
        for (int column = begin; column <= end; column++)
            terminal->cells[row][column] = current_cell(terminal);
    }
}

static void sgr(struct terminal *terminal)
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

static void finish_csi(struct terminal *terminal, unsigned char final)
{
    int amount;

    if (terminal->parameter_has_value || terminal->parameter_count == 0) {
        if (terminal->parameter_count < TERM_MAX_PARAMS)
            terminal->params[terminal->parameter_count++] =
                terminal->parameter_has_value ? terminal->parameter_value : 0;
    }
    amount = parameter(terminal, 0, 1);
    switch (final) {
    case 'A': terminal->row -= amount; break;
    case 'B': terminal->row += amount; break;
    case 'C':
    case 'a': terminal->column += amount; break;
    case 'D': terminal->column -= amount; break;
    case 'G':
    case '`': terminal->column = amount - 1; break;
    case 'd': terminal->row = amount - 1; break;
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
            erase_cells(terminal, terminal->row, 0, terminal->row,
                        TERM_COLS - 1);
        else if (parameter(terminal, 0, 0) == 1)
            erase_cells(terminal, terminal->row, 0, terminal->row,
                        terminal->column);
        else
            erase_cells(terminal, terminal->row, terminal->column,
                        terminal->row, TERM_COLS - 1);
        break;
    case 'm': sgr(terminal); break;
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
    if (terminal->row < 0) terminal->row = 0;
    if (terminal->row >= TERM_ROWS) terminal->row = TERM_ROWS - 1;
    if (terminal->column < 0) terminal->column = 0;
    if (terminal->column >= TERM_COLS) terminal->column = TERM_COLS - 1;
    terminal->parameter_count = 0;
    terminal->parameter_value = 0;
    terminal->parameter_has_value = 0;
    terminal->parser_state = 0;
}

void ace_console_terminal_feed(struct terminal *terminal,
                               const unsigned char *data, size_t length)
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
                terminal->parameter_value =
                    terminal->parameter_value * 10 + value - '0';
                terminal->parameter_has_value = 1;
            } else if (value == ';') {
                if (terminal->parameter_count < TERM_MAX_PARAMS)
                    terminal->params[terminal->parameter_count++] =
                        terminal->parameter_has_value ?
                        terminal->parameter_value : 0;
                terminal->parameter_value = 0;
                terminal->parameter_has_value = 0;
            } else if (value >= 0x40 && value <= 0x7e) {
                finish_csi(terminal, value);
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
            ace_console_terminal_put(terminal, value);
        }
    }
}
