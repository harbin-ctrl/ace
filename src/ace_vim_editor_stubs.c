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

/* The console endpoint is the other half of the same story. native_dos.c's
   Read() prefers it, and falls back to reading the descriptor through the
   console channel when a handle has none -- which is the path its own
   comment keeps for unchanged Amiga programs such as Vim, and the one Vim
   gets by opening this endpoint to nothing. Linking the real endpoint would
   drag in the AROS console handler behind it, and that handler defines
   add_to_history() and do_write(), which are also Vim's own: the link fails
   on the collision rather than producing a Vim that reads keys any better.

   The cost is CON: as an object to Open(): a Vim started with its input
   redirected cannot open a console of its own to read from. It still reads
   an interactive stdin, which is how it is started from the ACE Shell. */
struct native_console_endpoint;
struct ace_console_channel;

struct native_console_endpoint *native_console_endpoint_open(
    struct ace_console_channel *channel)
{
    (void)channel;
    return NULL;
}

void native_console_endpoint_close(struct native_console_endpoint *endpoint)
{
    (void)endpoint;
}

int native_console_endpoint_read(struct native_console_endpoint *endpoint,
                                 void *data, size_t length, size_t *actual)
{
    (void)endpoint;
    (void)data;
    (void)length;
    if (actual)
        *actual = 0;
    return -1;
}

int native_console_endpoint_write(struct native_console_endpoint *endpoint,
                                  const void *data, size_t length,
                                  size_t *actual)
{
    (void)endpoint;
    (void)data;
    (void)length;
    if (actual)
        *actual = 0;
    return -1;
}

int native_console_endpoint_set_raw(struct native_console_endpoint *endpoint,
                                    int raw)
{
    (void)endpoint;
    (void)raw;
    return -1;
}
