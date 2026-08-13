#include "aros_console_editor.h"

#include <exec/io.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <devices/conunit.h>
#include <intuition/intuition.h>

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "aros_exec_runtime.h"
#include "con_handler_intern.h"
#include "support.h"

struct ace_aros_console_editor {
    struct filehandle filehandle;
    struct MsgPort *reply_port;
    void *console;
    unsigned char completed[INPUTBUFFER_SIZE + 2];
    size_t completed_length;
};

LONG strnicmp(CONST_STRPTR left, CONST_STRPTR right, LONG length)
{
    return strncasecmp(left, right, (size_t)length);
}

void CloseWindow(struct Window *window)
{
    (void)window;
}

/* Host implementations of the Exec/DOS calls used by real support.c. */
APTR AllocVec(ULONG size, ULONG flags)
{
    (void)flags;
    return calloc(1, size);
}

APTR AllocMem(ULONG size, ULONG flags)
{
    (void)flags;
    return calloc(1, size);
}

void FreeVec(APTR memory)
{
    free(memory);
}

void FreeMem(APTR memory, ULONG size)
{
    (void)size;
    free(memory);
}

void CopyMem(CONST_APTR source, APTR destination, ULONG length)
{
    memmove(destination, source, length);
}

void SetMem(APTR destination, ULONG length, UBYTE value)
{
    memset(destination, value, length);
}

void Delay(ULONG ticks)
{
    struct timespec request = {
        .tv_sec = ticks / 50,
        .tv_nsec = (long)(ticks % 50) * 20000000L,
    };

    nanosleep(&request, NULL);
}

UWORD PeekQualifier(void)
{
    return 0;
}

APTR FindTask(CONST_STRPTR name)
{
    (void)name;
    return NULL;
}

struct MsgPort *FindPort(CONST_STRPTR name)
{
    (void)name;
    return NULL;
}

ULONG SetSignal(ULONG set_mask, ULONG clear_mask)
{
    (void)set_mask;
    (void)clear_mask;
    return 0;
}

void Signal(struct Task *task, ULONG signal_set)
{
    (void)task;
    (void)signal_set;
}

void AddTail(struct List *list, struct Node *node)
{
    ADDTAIL(list, node);
}

struct Node *RemHead(struct List *list)
{
    return REMHEAD(list);
}

void Remove(struct Node *node)
{
    if (node && node->ln_Pred && node->ln_Succ)
        REMOVE(node);
}

static void reset_input(struct filehandle *filehandle)
{
    filehandle->inputstart = 0;
    filehandle->inputpos = 0;
    filehandle->inputsize = 0;
}

static void capture_completed_line(struct ace_aros_console_editor *editor)
{
    size_t length = editor->filehandle.inputstart;

    if (length == 0 || length > sizeof(editor->completed))
        return;
    memcpy(editor->completed, editor->filehandle.inputbuffer, length);
    editor->completed_length = length;
    reset_input(&editor->filehandle);
}

struct ace_aros_console_editor *ace_aros_console_editor_open(void)
{
    struct ace_aros_console_editor *editor = calloc(1, sizeof(*editor));

    if (!editor)
        return NULL;
    editor->reply_port = CreateMsgPort();
    if (!editor->reply_port)
        goto fail;
    editor->filehandle.conwriteio.io_Message.mn_ReplyPort = editor->reply_port;
    if (OpenDevice("console.device", CONU_STANDARD,
                   (struct IORequest *)&editor->filehandle.conwriteio, 0) != 0)
        goto fail;
    editor->console = ace_aros_console_last();
    return editor;

fail:
    if (editor->reply_port)
        DeleteMsgPort(editor->reply_port);
    free(editor);
    return NULL;
}

void ace_aros_console_editor_close(struct ace_aros_console_editor *editor)
{
    if (!editor)
        return;
    CloseDevice((struct IORequest *)&editor->filehandle.conwriteio);
    DeleteMsgPort(editor->reply_port);
    free(editor);
}

int ace_aros_console_editor_feed(struct ace_aros_console_editor *editor,
                                 const void *data, size_t length)
{
    if (!editor || (!data && length != 0) || length > CONSOLEBUFFER_SIZE)
        return -1;
    editor->filehandle.conbufferpos = 0;
    editor->filehandle.conbuffersize = (WORD)length;
    memcpy(editor->filehandle.consolebuffer, data, length);
    process_input(&editor->filehandle);
    if (editor->filehandle.inputstart > 0 &&
        editor->filehandle.inputbuffer[editor->filehandle.inputstart - 1] == '\n')
        capture_completed_line(editor);
    return 0;
}

size_t ace_aros_console_editor_take_line(
    struct ace_aros_console_editor *editor, void *data, size_t length)
{
    size_t count;

    if (!editor || !data || editor->completed_length == 0)
        return 0;
    count = editor->completed_length < length ? editor->completed_length : length;
    memcpy(data, editor->completed, count);
    if (count == editor->completed_length)
        editor->completed_length = 0;
    return count;
}

size_t ace_aros_console_editor_take_output(
    struct ace_aros_console_editor *editor, void *data, size_t length)
{
    if (!editor)
        return 0;
    return ace_aros_console_take_output(editor->console, data, length);
}
