#ifndef ACE_AROS_CONSOLE_EDITOR_H
#define ACE_AROS_CONSOLE_EDITOR_H

#include <stddef.h>

struct ace_aros_console_editor;

struct ace_aros_console_editor *ace_aros_console_editor_open(void);
void ace_aros_console_editor_close(struct ace_aros_console_editor *editor);
int ace_aros_console_editor_feed(struct ace_aros_console_editor *editor,
                                 const void *data, size_t length);
size_t ace_aros_console_editor_take_line(
    struct ace_aros_console_editor *editor, void *data, size_t length);
size_t ace_aros_console_editor_take_output(
    struct ace_aros_console_editor *editor, void *data, size_t length);

#endif
