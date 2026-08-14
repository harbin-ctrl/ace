#include <stddef.h>

struct ace_aros_console_editor;

/* A Vim process enters raw mode before it reads console input. Keep the
   native DOS object linkable without importing the shell's cooked AROS
   console handler, whose helper names collide with Vim's own functions. */
struct ace_aros_console_editor *ace_aros_console_editor_open(void)
{
    return NULL;
}

void ace_aros_console_editor_close(struct ace_aros_console_editor *editor)
{
    (void)editor;
}

int ace_aros_console_editor_feed(struct ace_aros_console_editor *editor,
                                 const void *data, size_t length)
{
    (void)editor;
    (void)data;
    (void)length;
    return -1;
}

size_t ace_aros_console_editor_take_line(
    struct ace_aros_console_editor *editor, void *data, size_t length)
{
    (void)editor;
    (void)data;
    (void)length;
    return 0;
}

size_t ace_aros_console_editor_take_output(
    struct ace_aros_console_editor *editor, void *data, size_t length)
{
    (void)editor;
    (void)data;
    (void)length;
    return 0;
}
