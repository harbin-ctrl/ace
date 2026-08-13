#include "aros_console_editor.h"

#include <assert.h>
#include <string.h>

static void drain_output(struct ace_aros_console_editor *editor)
{
    unsigned char output[512];

    while (ace_aros_console_editor_take_output(editor, output,
                                                sizeof(output)) != 0)
        ;
}

int main(void)
{
    struct ace_aros_console_editor *editor;
    char line[64] = {0};

    editor = ace_aros_console_editor_open();
    assert(editor != NULL);
    assert(ace_aros_console_editor_feed(editor, "first\n", 6) == 0);
    assert(ace_aros_console_editor_take_line(editor, line, sizeof(line)) == 6);
    assert(memcmp(line, "first\n", 6) == 0);
    drain_output(editor);

    assert(ace_aros_console_editor_feed(editor, "second\n", 7) == 0);
    assert(ace_aros_console_editor_take_line(editor, line, sizeof(line)) == 7);
    assert(memcmp(line, "second\n", 7) == 0);
    drain_output(editor);

    /* A blank Return must submit only a blank line.  It must not let the
       console handler's stale input bytes become a repeated command. */
    assert(ace_aros_console_editor_feed(editor, "\n", 1) == 0);
    assert(ace_aros_console_editor_take_line(editor, line, sizeof(line)) == 1);
    assert(line[0] == '\n');
    drain_output(editor);

    /* Real AROS support.c handles CSI-Up and retrieves the previous line. */
    assert(ace_aros_console_editor_feed(editor, "\233A", 2) == 0);
    assert(ace_aros_console_editor_feed(editor, "\n", 1) == 0);
    assert(ace_aros_console_editor_take_line(editor, line, sizeof(line)) == 7);
    assert(memcmp(line, "second\n", 7) == 0);

    ace_aros_console_editor_close(editor);
    return 0;
}
