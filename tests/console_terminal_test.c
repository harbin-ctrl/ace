#include <assert.h>
#include <string.h>

#include "console_terminal.h"

int main(void)
{
    struct terminal terminal;
    const unsigned char output[] = "A\n\033[1;31mB\033[0m";

    ace_console_terminal_reset(&terminal);
    ace_console_terminal_feed(&terminal, output, sizeof(output) - 1);
    assert(terminal.cells[0][0].character == 'A');
    assert(terminal.cells[1][0].character == 'B');
    assert(terminal.cells[1][0].bold != 0);
    assert(terminal.cells[1][0].foreground == 1);
    assert(terminal.bold == 0);
    assert(terminal.foreground == 0);
    return 0;
}
