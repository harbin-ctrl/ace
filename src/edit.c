/*
 * Edit -- the AmigaDOS line editor.
 *
 * AmigaDOS shipped two editors: ED, the full-screen one, and EDIT, the line
 * editor described in the AmigaDOS manual chapter this file implements.  This
 * is EDIT: it processes a file line by line, passing each line from the
 * source file through a queue of previous lines to the destination file, so a
 * file far larger than memory can be edited by a script of one-line commands.
 *
 * There is no AROS source for it, so this is a clone written to the manual
 * rather than a port -- and then corrected against the original itself,
 * running under emulation, which settled a dozen things the manual either
 * garbles or never says. It is written to dos.library and exec.library alone
 * -- no stdio, no malloc, no host calls -- so the one source builds for
 * AmigaOS, for AROS, and for ACE, where it links against ACE's broker-backed
 * DOS the same way every other command here does.
 *
 * What the original does that the manual does not say, all of it observed
 * rather than guessed, and all of it covered by the transcript cases in
 * tests/edit_test.sh:
 *
 *   - A line verifies as two lines: the number, then the text. The number is
 *     +++ for a line that has none of its own, and the terminator is a period
 *     except on the extra line past the end of the file, which shows the
 *     number it would have and an asterisk.
 *
 *   - Verification is deferred to the end of a command line and happens only
 *     if nothing has shown the line already. That one rule is why M2;M3 shows
 *     one line, 3(N) shows only the line it arrives at, and 2(N;?) shows two
 *     lines rather than four.
 *
 *   - n(...) is a repeat group, with semicolons between the commands inside.
 *     The manual only hints at this, in a sentence about ending a command
 *     with a closing parenthesis.
 *
 *   - An error prints a line of spaces and a > under the character of the
 *     command line the editor had reached, then the message.
 *
 *   - A string left unclosed ends at a semicolon as well as at the end of the
 *     line, so PB/CAT;? is a pointer command followed by a verify.
 *
 *   - The editor announces itself as "Editor" and prompts with ":" before a
 *     command only when the command line before it did not end by verifying a
 *     line: a verification is itself the invitation to type the next command.
 *     There is no prompt inside an insertion.
 *
 *   - T,L has a one-line format of its own -- the number right-aligned in
 *     five columns, two spaces, the text -- while T and T,P and T,N type the
 *     text alone. Only ? and ! and the end-of-line verification use the
 *     two-line form, and only those satisfy a pending verification: T ending
 *     on the current line still leaves it to be verified afterwards.
 *
 *   - ! heads its two rows with the line number and marks capitals with
 *     underscores, not the minus signs the manual describes.
 *
 *   - The line window's mark sits under the character before the window, not
 *     under the first character in it: PR shows no mark, one > puts it in
 *     column 0, two put it in column 1.
 *
 *   - The window deletions cut to the first match, not the last: D,T,A on a
 *     line holding "cat" twice leaves the text after the first one.
 *
 *   - D takes a count or a range. The manual's "D .*" for deleting to the end
 *     of the file is not a thing: D runs on its own and the period is left
 *     over as "Unknown command - .", which is how an unreadable character is
 *     reported where an unknown name is only "Unknown command".
 *
 *   - M takes a number or an asterisk. A period is "Number expected after M",
 *     and the offending character is consumed with the command rather than
 *     left to be read as another one. A number the file has gone past is
 *     "Line number <n> too small"; a number beyond the end walks to the end
 *     and is "Input exhausted", which is also what a failed F reports.
 *
 *   - Work files are T:E<nn>-WK<n> and the backup is T:EDIT-BACKUP.
 *
 * Two places where the manual contradicts itself, resolved the same way:
 *
 *   - The manual's summary table writes the exchange command as
 *     "E <string2> <string1>" while its own worked example, GE /DF0:/DF2:/,
 *     changes DF0: into DF2:. The example wins, and the original agrees:
 *     chaining E, A and B turns "one cat" into "one YdogX". The first string
 *     is always the one searched for.
 *
 *   - Global changes are described as applying "to any occurrence" of the
 *     search string, where the single-line A, B and E commands act on the
 *     first occurrence only. The original does change every occurrence in
 *     each line a global sees.
 *
 * The string qualifiers the manual mentions for the split and global commands
 * are not implemented, because the excerpt this was written from never
 * defines them; a qualifier is reported as an unknown command rather than
 * quietly ignored.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include <string.h>

#define EDIT_TEMPLATE "FROM/A,TO,WITH/K,VER/K,OPT/K,WIDTH/K/N,PREVIOUS/K/N"

enum {
    ARG_FROM,
    ARG_TO,
    ARG_WITH,
    ARG_VER,
    ARG_OPT,
    ARG_WIDTH,
    ARG_PREVIOUS,
    ARG_COUNT
};

#define DEFAULT_WIDTH     1200
#define DEFAULT_PREVIOUS  100
#define MINIMUM_WIDTH     16
#define MAXIMUM_WIDTH     65535
#define MINIMUM_PREVIOUS  1
#define MAXIMUM_PREVIOUS  65535

#define NAME_SIZE         256
#define COMMAND_SIZE      512
#define STRING_SIZE       256
#define READ_BUFFER       512
#define VERIFY_BUFFER     256
#define COMMAND_DEPTH     8

/* One line of the file.  Non-original lines -- inserted, or made by
   splitting -- have number 0 and verify as ++++ the way the manual's T,L
   command describes.  `newline' records whether the line ended with a line
   feed in the file, so that a file whose last line has none, or a line longer
   than WIDTH that had to be carried in two, is written back byte for byte. */
struct Line
{
    struct Line *next;
    LONG serial;
    LONG number;
    LONG length;
    BOOL newline;
    BOOL phantom;
    BOOL processed;
    UBYTE *text;
};

/* A buffered reader over a file handle.  Both the source files and the
   command files use it; dos.library's own buffering is per handle, and this
   keeps line assembly identical for both. */
struct Reader
{
    BPTR handle;
    UBYTE buffer[READ_BUFFER];
    LONG fill;
    LONG position;
    BOOL eof;
};

/* An input file.  The manual's FROM command switches between several without
   closing any of them, and each keeps its own position and its own line
   numbering, so each needs its own record. */
struct Source
{
    struct Source *next;
    struct Reader reader;
    UBYTE name[NAME_SIZE];
    LONG next_number;
    BOOL ended;
    BOOL temporary;
};

/* An output file: the destination, plus whatever the TO command has opened
   alongside it. */
struct Sink
{
    struct Sink *next;
    BPTR handle;
    UBYTE name[NAME_SIZE];
    BOOL temporary;
};

/* A global change: GA, GB or GE. */
struct Global
{
    struct Global *next;
    LONG id;
    UBYTE type;
    BOOL enabled;
    LONG count;
    UBYTE find[STRING_SIZE];
    LONG find_length;
    UBYTE with[STRING_SIZE];
    LONG with_length;
};

/* A command file, or the keyboard at the bottom of the stack. */
struct CommandFile
{
    struct Reader reader;
    BOOL close;
};

/* A command line being executed, and the arguments read out of it.  A repeat
   group runs its body through a parser of its own, and `offset' is where that
   body starts in the line the user typed, so an error inside a group still
   points at the right column. */
struct Parser
{
    UBYTE *text;
    LONG length;
    LONG position;
    LONG offset;
};

struct Edit
{
    LONG width;
    LONG previous;

    struct Line **queue;
    LONG queue_head;
    LONG queue_count;
    struct Line *ahead;
    struct Line *current;
    struct Line *spare;
    LONG next_serial;

    LONG pointer;
    LONG renumber;

    BPTR ver;
    BOOL ver_close;
    UBYTE ver_buffer[VERIFY_BUFFER];
    LONG ver_fill;

    BOOL verify;
    /* What a command line did, which decides both whether the line it left
       behind is shown at the end of it and whether the next command gets a
       prompt. */
    LONG entry_serial;
    LONG shown_serial;
    LONG numbered_serial;
    BOOL shown_any;
    BOOL modified;
    BOOL verified;
    BOOL trailing;

    UBYTE terminator[STRING_SIZE];
    LONG terminator_length;
    UBYTE search[STRING_SIZE];
    LONG search_length;
    UBYTE last_command[COMMAND_SIZE];
    LONG last_command_length;

    struct Source *sources;
    struct Source *source;
    struct Sink *sinks;
    struct Sink *sink;
    /* The file the editing started on and the file it has to end up in.  They
       are the same file when no TO argument was given, and REWIND moves the
       editing onto temporary files in between, so neither can be recovered
       from the open file lists at the end. */
    struct Source *primary_source;
    struct Sink *primary_sink;
    UBYTE final_name[NAME_SIZE];
    UBYTE source_name[NAME_SIZE];
    BOOL backup_source;
    LONG temp_serial;

    struct CommandFile commands[COMMAND_DEPTH];
    LONG depth;

    struct Global *globals;
    LONG next_global;
    struct Parser *active;

    BOOL finished;
    BOOL saving;
    LONG result;
};

enum Command
{
    CMD_NONE = 0,
    CMD_M, CMD_N, CMD_P, CMD_F, CMD_BF,
    CMD_A, CMD_B, CMD_E, CMD_AP, CMD_BP, CMD_EP,
    CMD_I, CMD_D, CMD_DF, CMD_R, CMD_Z, CMD_SHD, CMD_TR,
    CMD_PR, CMD_PA, CMD_PB,
    CMD_DTA, CMD_DTB, CMD_DFA, CMD_DFB,
    CMD_SB, CMD_SA, CMD_CL,
    CMD_REWIND, CMD_V, CMD_T, CMD_TP, CMD_TN, CMD_TL,
    CMD_GA, CMD_GB, CMD_GE, CMD_CG, CMD_SG, CMD_EG, CMD_SHG,
    CMD_C, CMD_FROM, CMD_CF, CMD_TO, CMD_Q, CMD_W, CMD_STOP
};

struct CommandName
{
    const char *name;
    enum Command command;
};

/* Longest name first: the parser takes the longest match, so that PA is the
   pointer command rather than P followed by a stray A, and TO selects an
   output file rather than typing to the end of the source. */
static const struct CommandName command_names[] =
{
    { "REWIND", CMD_REWIND },
    { "REWI",   CMD_REWIND },
    { "STOP",   CMD_STOP },
    { "FROM",   CMD_FROM },
    { "SHD",    CMD_SHD },
    { "SHG",    CMD_SHG },
    { "DTA",    CMD_DTA },
    { "DTB",    CMD_DTB },
    { "DFA",    CMD_DFA },
    { "DFB",    CMD_DFB },
    { "BF",     CMD_BF },
    { "BP",     CMD_BP },
    { "AP",     CMD_AP },
    { "EP",     CMD_EP },
    { "PR",     CMD_PR },
    { "PA",     CMD_PA },
    { "PB",     CMD_PB },
    { "DF",     CMD_DF },
    { "TR",     CMD_TR },
    { "TP",     CMD_TP },
    { "TN",     CMD_TN },
    { "TL",     CMD_TL },
    { "SB",     CMD_SB },
    { "SA",     CMD_SA },
    { "SG",     CMD_SG },
    { "CL",     CMD_CL },
    { "CG",     CMD_CG },
    { "CF",     CMD_CF },
    { "GA",     CMD_GA },
    { "GB",     CMD_GB },
    { "GE",     CMD_GE },
    { "EG",     CMD_EG },
    { "TO",     CMD_TO },
    { "A",      CMD_A },
    { "B",      CMD_B },
    { "C",      CMD_C },
    { "D",      CMD_D },
    { "E",      CMD_E },
    { "F",      CMD_F },
    { "I",      CMD_I },
    { "M",      CMD_M },
    { "N",      CMD_N },
    { "P",      CMD_P },
    { "Q",      CMD_Q },
    { "R",      CMD_R },
    { "T",      CMD_T },
    { "V",      CMD_V },
    { "W",      CMD_W },
    { "Z",      CMD_Z },
    { NULL,     CMD_NONE }
};

static void execute_line(struct Edit *edit, UBYTE *text, LONG length);
static void execute_commands(struct Edit *edit, struct Parser *parser);
static void report_unknown(struct Edit *edit, struct Parser *parser);

/* ------------------------------------------------------------------ */
/* Small character and string helpers, written out rather than taken from
   utility.library so that the file needs nothing but dos and exec. */

static UBYTE upper_case(UBYTE character)
{
    return (character >= 'a' && character <= 'z') ?
           (UBYTE)(character - 'a' + 'A') : character;
}

static UBYTE lower_case(UBYTE character)
{
    return (character >= 'A' && character <= 'Z') ?
           (UBYTE)(character - 'A' + 'a') : character;
}

static BOOL is_letter(UBYTE character)
{
    character = upper_case(character);
    return character >= 'A' && character <= 'Z';
}

static BOOL is_digit(UBYTE character)
{
    return character >= '0' && character <= '9';
}

static BOOL is_graphic(UBYTE character)
{
    return character >= 0x20 && character < 0x7f;
}

static BOOL is_blank(UBYTE character)
{
    return character == ' ' || character == '\t';
}

static void append_number(UBYTE *buffer, LONG *at, LONG value, LONG digits)
{
    UBYTE text[12];
    LONG count = 0;

    do {
        text[count++] = (UBYTE)('0' + (value % 10));
        value /= 10;
    } while (value > 0);
    while (count < digits)
        text[count++] = '0';
    while (count > 0)
        buffer[(*at)++] = text[--count];
}

static void copy_name(UBYTE *destination, const UBYTE *source, LONG length)
{
    if (length >= NAME_SIZE)
        length = NAME_SIZE - 1;
    if (length > 0)
        memcpy(destination, source, (size_t)length);
    destination[length] = '\0';
}

static BOOL same_name(const UBYTE *left, const UBYTE *right)
{
    while (*left && *right) {
        if (upper_case(*left) != upper_case(*right))
            return FALSE;
        left++;
        right++;
    }
    return *left == *right;
}

/* ------------------------------------------------------------------ */
/* Verification output.  Everything the editor says goes to the VER file,
   which is the screen unless the VER argument named a file, so nothing here
   may use Printf() -- it would go to the wrong place. */

static void ver_flush(struct Edit *edit)
{
    if (edit->ver_fill > 0) {
        Write(edit->ver, edit->ver_buffer, edit->ver_fill);
        edit->ver_fill = 0;
    }
}

static void ver_bytes(struct Edit *edit, const UBYTE *text, LONG length)
{
    LONG index;

    for (index = 0; index < length; index++) {
        if (edit->ver_fill == VERIFY_BUFFER)
            ver_flush(edit);
        edit->ver_buffer[edit->ver_fill++] = text[index];
    }
}

static void ver_char(struct Edit *edit, UBYTE character)
{
    ver_bytes(edit, &character, 1);
}

static void ver_text(struct Edit *edit, const char *text)
{
    ver_bytes(edit, (const UBYTE *)text, (LONG)strlen(text));
}

static void ver_number(struct Edit *edit, LONG value)
{
    UBYTE digits[12];
    LONG count = 0;
    ULONG magnitude;

    if (value < 0) {
        ver_char(edit, '-');
        magnitude = (ULONG)(-(value + 1)) + 1;
    } else
        magnitude = (ULONG)value;
    do {
        digits[count++] = (UBYTE)('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude > 0);
    while (count > 0)
        ver_char(edit, digits[--count]);
}

static void ver_newline(struct Edit *edit)
{
    ver_char(edit, '\n');
    ver_flush(edit);
}

/* An error names its place before it names itself: a line of spaces and a >
   under the character of the command line the editor had reached, then the
   message.  The pointer is under the last character consumed, so a command
   whose argument was read in full points at the end of that argument. */
static void report(struct Edit *edit, const char *message)
{
    if (edit->active) {
        LONG column = edit->active->offset + edit->active->position - 1;
        LONG index;

        if (column < 0)
            column = 0;
        for (index = 0; index < column; index++)
            ver_char(edit, ' ');
        ver_char(edit, '>');
        ver_newline(edit);
    }
    ver_text(edit, message);
    ver_newline(edit);
}

static void report_name(struct Edit *edit, const char *message,
                        const UBYTE *name)
{
    ver_text(edit, message);
    ver_text(edit, " ");
    ver_bytes(edit, name, (LONG)strlen((const char *)name));
    ver_newline(edit);
}

/* ------------------------------------------------------------------ */
/* Lines.  Every line carries a buffer of WIDTH bytes, so PREVIOUS * WIDTH is
   the editor's memory footprint exactly as the manual describes it.  Freed
   lines go on a spare list rather than back to exec, because the queue turns
   them over one per line of input. */

static struct Line *line_alloc(struct Edit *edit)
{
    struct Line *line = edit->spare;

    if (line)
        edit->spare = line->next;
    else {
        line = (struct Line *)AllocVec((ULONG)(sizeof(struct Line) +
                                               (size_t)edit->width + 1),
                                       MEMF_ANY);
        if (!line)
            return NULL;
        line->text = (UBYTE *)(line + 1);
    }
    line->next = NULL;
    line->serial = ++edit->next_serial;
    line->number = 0;
    line->length = 0;
    line->newline = TRUE;
    line->phantom = FALSE;
    line->processed = FALSE;
    line->text[0] = '\0';
    return line;
}

static void line_free(struct Edit *edit, struct Line *line)
{
    if (!line)
        return;
    line->next = edit->spare;
    edit->spare = line;
}

static void lines_release(struct Edit *edit)
{
    while (edit->spare) {
        struct Line *line = edit->spare;

        edit->spare = line->next;
        FreeVec(line);
    }
}

static BOOL line_insert(struct Edit *edit, struct Line *line, LONG at,
                        const UBYTE *text, LONG length)
{
    if (length <= 0)
        return TRUE;
    if (line->length + length > edit->width) {
        report(edit, "Line too long");
        return FALSE;
    }
    memmove(line->text + at + length, line->text + at,
            (size_t)(line->length - at));
    memcpy(line->text + at, text, (size_t)length);
    line->length += length;
    line->text[line->length] = '\0';
    return TRUE;
}

static void line_delete(struct Line *line, LONG at, LONG count)
{
    if (at >= line->length)
        return;
    if (count > line->length - at)
        count = line->length - at;
    if (count <= 0)
        return;
    memmove(line->text + at, line->text + at + count,
            (size_t)(line->length - at - count));
    line->length -= count;
    line->text[line->length] = '\0';
}

static LONG line_find(const struct Line *line, LONG from, const UBYTE *text,
                      LONG length)
{
    LONG index;

    if (length <= 0 || from < 0)
        return -1;
    for (index = from; index + length <= line->length; index++)
        if (memcmp(line->text + index, text, (size_t)length) == 0)
            return index;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Buffered reading. */

static void reader_init(struct Reader *reader, BPTR handle)
{
    reader->handle = handle;
    reader->fill = 0;
    reader->position = 0;
    reader->eof = FALSE;
}

static LONG reader_char(struct Reader *reader)
{
    if (reader->position == reader->fill) {
        LONG got;

        if (reader->eof)
            return -1;
        got = Read(reader->handle, reader->buffer, READ_BUFFER);
        if (got <= 0) {
            reader->eof = TRUE;
            return -1;
        }
        reader->fill = got;
        reader->position = 0;
    }
    return reader->buffer[reader->position++];
}

/* ------------------------------------------------------------------ */
/* The destination side. */

static void sink_write(struct Edit *edit, struct Line *line)
{
    if (!line)
        return;
    /* The extra line the editor makes past the end of the source exists only
       so that text can be inserted there.  An untouched one is not part of
       the file and must not add a blank line to it. */
    if (line->phantom && line->length == 0)
        return;
    if (line->length > 0)
        Write(edit->sink->handle, line->text, line->length);
    if (line->newline)
        Write(edit->sink->handle, (APTR)"\n", 1);
}

/* ------------------------------------------------------------------ */
/* The queue of previous lines, and moving through the source.

   A line leaves the current position in one of two directions.  Forward, it
   joins the queue, and the queue's oldest line is written to the destination
   when the queue is full -- that is the limit the P command runs into.
   Backward, it goes on the `ahead' stack, to be handed back before the source
   file is read from again. */

static void queue_push(struct Edit *edit, struct Line *line)
{
    if (edit->queue_count == edit->previous) {
        struct Line *oldest = edit->queue[edit->queue_head];

        edit->queue[edit->queue_head] = NULL;
        edit->queue_head = (edit->queue_head + 1) % edit->previous;
        edit->queue_count--;
        sink_write(edit, oldest);
        line_free(edit, oldest);
    }
    edit->queue[(edit->queue_head + edit->queue_count) % edit->previous] = line;
    edit->queue_count++;
}

static struct Line *queue_pop(struct Edit *edit)
{
    LONG index;
    struct Line *line;

    if (edit->queue_count == 0)
        return NULL;
    index = (edit->queue_head + edit->queue_count - 1) % edit->previous;
    line = edit->queue[index];
    edit->queue[index] = NULL;
    edit->queue_count--;
    return line;
}

static struct Line *queue_at(struct Edit *edit, LONG offset)
{
    return edit->queue[(edit->queue_head + offset) % edit->previous];
}

static void queue_flush(struct Edit *edit)
{
    while (edit->queue_count > 0) {
        struct Line *line = edit->queue[edit->queue_head];

        edit->queue[edit->queue_head] = NULL;
        edit->queue_head = (edit->queue_head + 1) % edit->previous;
        edit->queue_count--;
        sink_write(edit, line);
        line_free(edit, line);
    }
}

/* Reads one line from a source file.  A line longer than WIDTH is carried in
   two, the first with no line feed of its own, so that writing them back out
   reproduces the file exactly. */
static struct Line *source_line(struct Edit *edit, struct Source *source)
{
    struct Line *line;
    LONG character;

    character = reader_char(&source->reader);
    if (character < 0) {
        if (source->ended)
            return NULL;
        source->ended = TRUE;
        line = line_alloc(edit);
        if (!line)
            return NULL;
        /* The extra line past the end of the file takes the next number and
           is marked as having none of its own, which is what the original
           shows: 8* on a file of seven lines. */
        line->number = source->next_number++;
        line->phantom = TRUE;
        line->newline = FALSE;
        return line;
    }
    line = line_alloc(edit);
    if (!line)
        return NULL;
    line->number = source->next_number++;
    line->newline = FALSE;
    while (character >= 0) {
        if (character == '\n') {
            line->newline = TRUE;
            break;
        }
        line->text[line->length++] = (UBYTE)character;
        if (line->length == edit->width)
            break;
        character = reader_char(&source->reader);
    }
    line->text[line->length] = '\0';
    if (!edit->trailing) {
        while (line->length > 0 && is_blank(line->text[line->length - 1]))
            line->length--;
        line->text[line->length] = '\0';
    }
    return line;
}

/* ------------------------------------------------------------------ */
/* Global changes, applied to every line as it becomes current. */

static void apply_globals(struct Edit *edit, struct Line *line)
{
    struct Global *global;

    for (global = edit->globals; global; global = global->next) {
        LONG at = 0;

        if (!global->enabled)
            continue;
        while ((at = line_find(line, at, global->find,
                               global->find_length)) >= 0) {
            LONG resume;

            switch (global->type) {
            case 'A':
                if (!line_insert(edit, line, at + global->find_length,
                                 global->with, global->with_length))
                    return;
                resume = at + global->find_length + global->with_length;
                break;
            case 'B':
                if (!line_insert(edit, line, at, global->with,
                                 global->with_length))
                    return;
                resume = at + global->with_length + global->find_length;
                break;
            default:
                line_delete(line, at, global->find_length);
                if (!line_insert(edit, line, at, global->with,
                                 global->with_length))
                    return;
                resume = at + global->with_length;
                break;
            }
            global->count++;
            at = resume;
        }
    }
}

/* Everything a line goes through on becoming the current line: the line
   window returns to the start of it, the = command's renumbering reaches it,
   and any global change that has not already seen it is applied. */
static void line_arrive(struct Edit *edit, struct Line *line)
{
    edit->pointer = 0;
    if (!line)
        return;
    if (edit->renumber > 0 && !line->processed)
        line->number = edit->renumber++;
    if (!line->processed) {
        line->processed = TRUE;
        apply_globals(edit, line);
    }
}

/* ------------------------------------------------------------------ */
/* Movement. */

static struct Line *pull_line(struct Edit *edit)
{
    struct Line *line;

    if (edit->ahead) {
        line = edit->ahead;
        edit->ahead = line->next;
        line->next = NULL;
        return line;
    }
    return source_line(edit, edit->source);
}

static BOOL next_line(struct Edit *edit)
{
    struct Line *line = pull_line(edit);

    if (!line) {
        report(edit, "Input exhausted");
        return FALSE;
    }
    if (edit->current)
        queue_push(edit, edit->current);
    edit->current = line;
    line_arrive(edit, line);
    return TRUE;
}

static BOOL previous_line(struct Edit *edit)
{
    struct Line *line = queue_pop(edit);

    if (!line) {
        report(edit, "No more previous lines");
        return FALSE;
    }
    if (edit->current) {
        edit->current->next = edit->ahead;
        edit->ahead = edit->current;
    }
    edit->current = line;
    edit->pointer = 0;
    return TRUE;
}

/* Drops the current line without passing it to the destination, and makes the
   line after it current. */
static BOOL delete_current(struct Edit *edit)
{
    struct Line *line;

    if (!edit->current || edit->current->phantom) {
        report(edit, "Input exhausted");
        return FALSE;
    }
    line = pull_line(edit);
    if (!line) {
        report(edit, "Input exhausted");
        return FALSE;
    }
    line_free(edit, edit->current);
    edit->current = line;
    line_arrive(edit, line);
    return TRUE;
}

static BOOL at_end(struct Edit *edit)
{
    return edit->current && edit->current->phantom;
}

static void move_to_end(struct Edit *edit)
{
    while (!at_end(edit))
        if (!next_line(edit))
            return;
}

/* Moves to an original line by number.  The direction is decided once, before
   the first step: a search that changed its mind on the way would never stop,
   because the extra line past the end of the file and every non-original line
   have no number to compare against. */
static BOOL move_to_number(struct Edit *edit, LONG number)
{
    BOOL backward;

    if (!edit->current) {
        report(edit, "No current line");
        return FALSE;
    }
    if (edit->current->number == number)
        return TRUE;
    /* A line with no number of its own -- one that was inserted or split, or
       the extra line past the end of the file -- says nothing about where the
       wanted line is, so the last numbered line passed does. */
    if (edit->current->number > 0)
        backward = edit->current->number > number;
    else {
        LONG index = edit->queue_count;

        backward = FALSE;
        while (index-- > 0) {
            struct Line *line = queue_at(edit, index);

            if (line->number > 0) {
                backward = line->number >= number;
                break;
            }
        }
    }
    while (edit->current->number != number) {
        if (backward) {
            if (!previous_line(edit))
                return FALSE;
        } else {
            /* Walking forward past the wanted number means it is not in the
               file any more -- deleted, or renumbered away -- and there is no
               going back for it once its place has been passed. */
            if (edit->current->number > number) {
                UBYTE message[48];
                LONG at = 0;

                memcpy(message, "Line number ", 12);
                at = 12;
                append_number(message, &at, number, 1);
                memcpy(message + at, " too small", 11);
                report(edit, (const char *)message);
                return FALSE;
            }
            if (!next_line(edit))
                return FALSE;
        }
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Verification. */

/* A line verifies as two lines: its number, followed by a period when the
   line has that number in the source file and by an asterisk when it does
   not, and then the text on a line of its own.  The line window's position is
   shown by a pointer under the text -- with no number field in the way, since
   the number is on the line above -- and the original leaves the cursor
   sitting after that pointer rather than ending the line, which is why a
   command typed next appears alongside it. */
/* The number, or +++ for a line that has none of its own because it was
   inserted or split.  The terminator is a period, except on the extra line
   past the end of the file, which shows the number it would have and an
   asterisk. */
static void verify_header(struct Edit *edit, struct Line *line)
{
    if (line->number > 0)
        ver_number(edit, line->number);
    else
        ver_text(edit, "+++");
    ver_char(edit, line->phantom ? '*' : '.');
    ver_newline(edit);
}

static void verify_line(struct Edit *edit, struct Line *line, BOOL numbered)
{
    LONG index;

    if (!line)
        return;
    if (numbered) {
        /* The number, or +++ for a line that has none of its own because it
           was inserted or split.  The terminator is a period, except on the
           extra line past the end of the file, which shows the number it
           would have and an asterisk: 8* on a file of seven lines. */
        if (line->number > 0)
            ver_number(edit, line->number);
        else
            ver_text(edit, "+++");
        ver_char(edit, line->phantom ? '*' : '.');
        ver_newline(edit);
    }
    for (index = 0; index < line->length; index++)
        ver_char(edit, is_graphic(line->text[index]) ? line->text[index] : '?');
    ver_newline(edit);
    edit->shown_serial = line->serial;
    edit->shown_any = TRUE;
    if (numbered) {
        edit->numbered_serial = line->serial;
        edit->verified = TRUE;
    }
    /* The mark sits under the character before the window, not under the
       first character in it: PR shows no mark at all, one > puts it in
       column 0, and two put it in column 1. */
    if (edit->pointer > 0 && line == edit->current) {
        for (index = 1; index < edit->pointer; index++)
            ver_char(edit, ' ');
        ver_char(edit, '>');
        ver_flush(edit);
    }
}

/* Called by every command that changes the text of the current line.  Moving
   is not a change: the end-of-line rule below notices movement on its own. */
static void verify_current(struct Edit *edit)
{
    edit->modified = TRUE;
}

/* The end of a command line.  The line it left behind is shown when the
   command line moved somewhere else, changed the line's text, or put some
   other line on the screen -- and not when something has already shown the
   current line.  That one rule accounts for all of it: M2;M3 shows one line,
   3(N) shows only the line it arrives at, 2(N;?) shows two rather than four,
   M1 when line 1 is already current shows nothing at all, M1;I3 that walks
   away and is thrown back where it started shows nothing, and T,P shows the
   current line again after typing the queue over it. */
static void verify_pending(struct Edit *edit)
{
    LONG serial = edit->current ? edit->current->serial : 0;
    BOOL moved = serial != edit->entry_serial;

    if (!edit->verify || !edit->current)
        return;
    /* Nothing happened worth reporting: no move, no change, and nothing put
       on the screen. */
    if (!moved && !edit->modified && !edit->shown_any)
        return;
    /* Only a numbered display satisfies the pending verification.  The typing
       commands show text without numbers, so T ending on the current line
       still leaves it to be verified -- which is what T on the extra line
       past the end does, typing its empty text and then showing 7* after
       it. */
    if (edit->numbered_serial == serial)
        return;
    verify_line(edit, edit->current, TRUE);
}

static UBYTE hex_digit(UBYTE value)
{
    value &= 0x0f;
    return (UBYTE)(value < 10 ? '0' + value : 'A' + value - 10);
}

/* The ! command: the line with every non-graphic character replaced by the
   first digit of its hexadecimal value, and beneath it a minus sign under
   each upper case letter and the second hexadecimal digit under each
   non-graphic character. */
static void verify_hex(struct Edit *edit)
{
    struct Line *line = edit->current;
    LONG index;

    if (!line)
        return;
    verify_header(edit, line);
    for (index = 0; index < line->length; index++) {
        UBYTE character = line->text[index];

        ver_char(edit, is_graphic(character) ? character :
                       hex_digit((UBYTE)(character >> 4)));
    }
    ver_newline(edit);
    {
        LONG last = -1;

        for (index = 0; index < line->length; index++) {
            UBYTE character = line->text[index];

            if (!is_graphic(character) ||
                (character >= 'A' && character <= 'Z'))
                last = index;
        }
        for (index = 0; index <= last; index++) {
            UBYTE character = line->text[index];

            if (!is_graphic(character))
                ver_char(edit, hex_digit(character));
            else if (character >= 'A' && character <= 'Z')
                ver_char(edit, '_');
            else
                ver_char(edit, ' ');
        }
    }
    ver_newline(edit);
    edit->shown_serial = line->serial;
    edit->numbered_serial = line->serial;
    edit->shown_any = TRUE;
    edit->verified = TRUE;
}

/* ------------------------------------------------------------------ */
/* Command line parsing. */

static void parser_init(struct Parser *parser, UBYTE *text, LONG length,
                        LONG offset)
{
    parser->text = text;
    parser->length = length;
    parser->position = 0;
    parser->offset = offset;
}

static void parse_blanks(struct Parser *parser)
{
    while (parser->position < parser->length &&
           is_blank(parser->text[parser->position]))
        parser->position++;
}

static LONG parse_peek(struct Parser *parser)
{
    parse_blanks(parser);
    if (parser->position == parser->length)
        return -1;
    return parser->text[parser->position];
}

static BOOL parse_number(struct Parser *parser, LONG *value)
{
    LONG result = 0;
    BOOL any = FALSE;

    parse_blanks(parser);
    while (parser->position < parser->length &&
           is_digit(parser->text[parser->position])) {
        result = result * 10 + (parser->text[parser->position++] - '0');
        any = TRUE;
    }
    if (any)
        *value = result;
    return any;
}

/* Reads up to the closing delimiter, which it consumes.  A string left open
   is closed by the end of the line or by a semicolon -- PB/CAT;? is the
   pointer command on CAT followed by a verify, not a search for "CAT;?" --
   and the semicolon is left for the command parser to see.  Returns FALSE
   only when the string was left open, which the callers that need a pair of
   strings care about and the rest do not. */
static BOOL read_delimited(struct Parser *parser, UBYTE delimiter,
                           UBYTE *buffer, LONG *length)
{
    LONG count = 0;
    BOOL closed = FALSE;

    while (parser->position < parser->length) {
        UBYTE character = parser->text[parser->position];

        if (character == delimiter) {
            parser->position++;
            closed = TRUE;
            break;
        }
        if (character == ';')
            break;
        if (count < STRING_SIZE - 1)
            buffer[count++] = character;
        parser->position++;
    }
    buffer[count] = '\0';
    *length = count;
    return closed;
}

/* A string argument is bracketed by a delimiter character of the caller's
   choosing -- a slash by convention, a period for file names, because a
   slash is part of a path.  Two strings for one command share the delimiter
   between them. */
static BOOL parse_string(struct Parser *parser, UBYTE *buffer, LONG *length)
{
    UBYTE delimiter;

    parse_blanks(parser);
    if (parser->position == parser->length)
        return FALSE;
    delimiter = parser->text[parser->position];
    if (delimiter == ';' || is_letter(delimiter) || is_digit(delimiter))
        return FALSE;
    parser->position++;
    read_delimited(parser, delimiter, buffer, length);
    return TRUE;
}

static BOOL parse_pair(struct Parser *parser, UBYTE *first, LONG *first_length,
                       UBYTE *second, LONG *second_length)
{
    UBYTE delimiter;

    parse_blanks(parser);
    if (parser->position == parser->length)
        return FALSE;
    delimiter = parser->text[parser->position];
    if (delimiter == ';' || is_letter(delimiter) || is_digit(delimiter))
        return FALSE;
    parser->position++;
    if (!read_delimited(parser, delimiter, first, first_length))
        return FALSE;
    read_delimited(parser, delimiter, second, second_length);
    return TRUE;
}

/* A file name, delimited the way the manual delimits one -- with periods,
   since a path already contains slashes -- or given bare.

   A name can contain the delimiter: .SYS:C/x.txt. is one file name and not
   "SYS:C/x" followed by stray letters.  So the closing delimiter is the one
   that ends the argument -- the last on the line, or the one before the next
   command -- rather than the first one found. */
static BOOL parse_file(struct Parser *parser, UBYTE *buffer)
{
    LONG length = 0;
    LONG character = parse_peek(parser);

    if (character < 0 || character == ';')
        return FALSE;
    if (!is_letter((UBYTE)character) && !is_digit((UBYTE)character)) {
        UBYTE delimiter = (UBYTE)character;
        LONG scan;
        LONG close;

        parser->position++;
        close = -1;
        for (scan = parser->position; scan < parser->length; scan++) {
            if (parser->text[scan] != delimiter)
                continue;
            if (scan + 1 == parser->length || is_blank(parser->text[scan + 1]) ||
                parser->text[scan + 1] == ';')
                close = scan;
        }
        if (close < 0)
            close = parser->length;
        while (parser->position < close) {
            if (length < NAME_SIZE - 1)
                buffer[length++] = parser->text[parser->position];
            parser->position++;
        }
        if (parser->position < parser->length)
            parser->position++;
        buffer[length] = '\0';
        return length > 0;
    }
    while (parser->position < parser->length &&
           !is_blank(parser->text[parser->position]) &&
           parser->text[parser->position] != ';') {
        if (length < NAME_SIZE - 1)
            buffer[length++] = parser->text[parser->position];
        parser->position++;
    }
    buffer[length] = '\0';
    return length > 0;
}

/* + or -, for the V and T,R switches. */
static BOOL parse_switch(struct Parser *parser, BOOL *value)
{
    LONG character = parse_peek(parser);

    if (character == '+' || character == '-') {
        parser->position++;
        *value = (character == '+');
        return TRUE;
    }
    return FALSE;
}

static enum Command parse_command(struct Parser *parser)
{
    UBYTE token[8];
    LONG count = 0;
    LONG scan = parser->position;
    LONG index;

    /* Qualifiers are written with commas -- D,T,A -- and the commas carry no
       meaning of their own, so DTA is the same command. */
    while (scan < parser->length && count < (LONG)sizeof(token)) {
        UBYTE character = parser->text[scan];

        if (character == ',') {
            scan++;
            continue;
        }
        if (!is_letter(character))
            break;
        token[count++] = upper_case(character);
        scan++;
    }
    if (count == 0)
        return CMD_NONE;
    for (index = 0; command_names[index].name; index++) {
        LONG length = (LONG)strlen(command_names[index].name);

        if (length <= count &&
            memcmp(token, command_names[index].name, (size_t)length) == 0) {
            /* Step the parser over exactly the letters that matched, commas
               and all, leaving any further letters for the next command. */
            LONG taken = 0;

            while (taken < length && parser->position < parser->length) {
                if (parser->text[parser->position] == ',') {
                    parser->position++;
                    continue;
                }
                parser->position++;
                taken++;
            }
            return command_names[index].command;
        }
    }
    return CMD_NONE;
}

/* A command that could not be read.  A name made of letters is simply
   unknown; anything else is named in the message, as D.* gets for its period
   once the D has been taken as a command of its own. */
static void report_unknown(struct Edit *edit, struct Parser *parser)
{
    LONG at = parser->position;

    if (at < parser->length && !is_letter(parser->text[at])) {
        UBYTE message[32];

        memcpy(message, "Unknown command - ", 18);
        message[18] = parser->text[at];
        message[19] = '\0';
        parser->position++;
        report(edit, (const char *)message);
        return;
    }
    report(edit, "Unknown command");
}

/* ------------------------------------------------------------------ */
/* Command input: the keyboard, the WITH file, and whatever the C command has
   opened on top of them. */

/* The prompt appears when the command line before it did not end by verifying
   a line -- at startup, after a command that printed nothing, after an error
   that did not move anywhere, and when an insertion ends.  A verification is
   itself the invitation to type the next command, so it is not followed by
   one. */
static void prompt(struct Edit *edit)
{
    struct CommandFile *file;

    if (edit->depth < 0 || edit->verified)
        return;
    file = &edit->commands[edit->depth];
    if (!IsInteractive(file->reader.handle))
        return;
    ver_char(edit, ':');
    ver_flush(edit);
}

static BOOL command_line(struct Edit *edit, UBYTE *buffer, LONG *length)
{
    while (edit->depth >= 0) {
        struct CommandFile *file = &edit->commands[edit->depth];
        LONG count = 0;
        LONG character;

        prompt(edit);
        character = reader_char(&file->reader);
        if (character < 0) {
            if (file->close)
                Close(file->reader.handle);
            edit->depth--;
            continue;
        }
        while (character >= 0 && character != '\n') {
            if (character != '\r' && count < COMMAND_SIZE - 1)
                buffer[count++] = (UBYTE)character;
            character = reader_char(&file->reader);
        }
        buffer[count] = '\0';
        *length = count;
        return TRUE;
    }
    return FALSE;
}

static void push_command_file(struct Edit *edit, const UBYTE *name)
{
    BPTR handle;

    if (edit->depth + 1 >= COMMAND_DEPTH) {
        report(edit, "Command files nested too deeply");
        return;
    }
    handle = Open((CONST_STRPTR)name, MODE_OLDFILE);
    if (!handle) {
        report_name(edit, "can't open", name);
        return;
    }
    edit->depth++;
    reader_init(&edit->commands[edit->depth].reader, handle);
    edit->commands[edit->depth].close = TRUE;
}

/* ------------------------------------------------------------------ */
/* Insertion. */

static void insert_lines(struct Edit *edit)
{
    UBYTE buffer[COMMAND_SIZE];
    LONG length;

    while (command_line(edit, buffer, &length)) {
        struct Line *line;

        /* The terminator is matched without regard to case, the way every
           other command name here is. */
        if (length == edit->terminator_length) {
            LONG index;
            BOOL match = TRUE;

            for (index = 0; index < length; index++)
                if (upper_case(buffer[index]) !=
                    upper_case(edit->terminator[index])) {
                    match = FALSE;
                    break;
                }
            if (match) {
                /* Back in command mode, and an insertion verifies nothing, so
                   the next command is prompted for. */
                edit->verified = FALSE;
                return;
            }
        }
        line = line_alloc(edit);
        if (!line) {
            report(edit, "Out of memory");
            return;
        }
        if (length > edit->width)
            length = edit->width;
        memcpy(line->text, buffer, (size_t)length);
        line->length = length;
        line->text[length] = '\0';
        line->processed = TRUE;
        /* Inserted text goes before the current line, which means it has
           already been passed: straight into the queue. */
        queue_push(edit, line);
    }
    report(edit, "Input exhausted");
}

/* ------------------------------------------------------------------ */
/* Files: FROM, TO, CF. */

static struct Source *find_source(struct Edit *edit, const UBYTE *name)
{
    struct Source *source;

    for (source = edit->sources; source; source = source->next)
        if (same_name(source->name, name))
            return source;
    return NULL;
}

static struct Source *open_source(struct Edit *edit, const UBYTE *name)
{
    struct Source *source = find_source(edit, name);
    BPTR handle;

    if (source)
        return source;
    handle = Open((CONST_STRPTR)name, MODE_OLDFILE);
    if (!handle) {
        report_name(edit, "can't open", name);
        return NULL;
    }
    source = (struct Source *)AllocVec((ULONG)sizeof(*source),
                                       MEMF_ANY | MEMF_CLEAR);
    if (!source) {
        Close(handle);
        report(edit, "Out of memory");
        return NULL;
    }
    reader_init(&source->reader, handle);
    copy_name(source->name, name, (LONG)strlen((const char *)name));
    source->next_number = 1;
    source->next = edit->sources;
    edit->sources = source;
    return source;
}

static struct Sink *find_sink(struct Edit *edit, const UBYTE *name)
{
    struct Sink *sink;

    for (sink = edit->sinks; sink; sink = sink->next)
        if (same_name(sink->name, name))
            return sink;
    return NULL;
}

static struct Sink *open_sink(struct Edit *edit, const UBYTE *name,
                              BOOL temporary)
{
    struct Sink *sink = find_sink(edit, name);
    BPTR handle;

    if (sink)
        return sink;
    handle = Open((CONST_STRPTR)name, MODE_NEWFILE);
    if (!handle) {
        report_name(edit, "can't open", name);
        return NULL;
    }
    sink = (struct Sink *)AllocVec((ULONG)sizeof(*sink),
                                   MEMF_ANY | MEMF_CLEAR);
    if (!sink) {
        Close(handle);
        report(edit, "Out of memory");
        return NULL;
    }
    sink->handle = handle;
    sink->temporary = temporary;
    copy_name(sink->name, name, (LONG)strlen((const char *)name));
    sink->next = edit->sinks;
    edit->sinks = sink;
    return sink;
}

/* The work file, named the way the original names it: T:E<nn>-WK<n>, with the
   editing process's number in it.  A rewind needs a second one while the
   first is still being read, so the WK number alternates between 1 and 2.
   Both are opened with MODE_NEWFILE over whatever is already there.

   The name is not probed for a free one, because the original does not clean
   up after itself -- STOP leaves its work file in T: -- and probing would
   turn that bounded pair of files into an unbounded pile. The process number
   is what keeps two editors apart, so on a system that hands out the same
   process number to everything (ACE does) two editors running at once would
   share these files. That is the original's design and this is a work-alike,
   so it is reproduced rather than fixed. */
static void work_file_name(struct Edit *edit, UBYTE *buffer)
{
    struct Process *process = (struct Process *)FindTask(NULL);
    LONG task = process ? process->pr_TaskNum : 0;
    LONG at = 3;

    memcpy(buffer, "T:E", 3);
    append_number(buffer, &at, task, 2);
    memcpy(buffer + at, "-WK", 3);
    at += 3;
    edit->temp_serial = edit->temp_serial == 1 ? 2 : 1;
    append_number(buffer, &at, edit->temp_serial, 1);
    buffer[at] = '\0';
}

static void close_named_file(struct Edit *edit, const UBYTE *name)
{
    struct Sink **sink_link = &edit->sinks;
    struct Source **source_link = &edit->sources;

    while (*sink_link) {
        struct Sink *sink = *sink_link;

        if (same_name(sink->name, name)) {
            if (sink == edit->sink) {
                report(edit, "Output file in use");
                return;
            }
            *sink_link = sink->next;
            Close(sink->handle);
            FreeVec(sink);
            return;
        }
        sink_link = &sink->next;
    }
    while (*source_link) {
        struct Source *source = *source_link;

        if (same_name(source->name, name)) {
            if (source == edit->source) {
                report(edit, "Input file in use");
                return;
            }
            *source_link = source->next;
            Close(source->reader.handle);
            FreeVec(source);
            return;
        }
        source_link = &source->next;
    }
    report_name(edit, "no such open file:", name);
}

/* ------------------------------------------------------------------ */
/* Finishing: passing what is left of the source to the destination. */

static void drain_source(struct Edit *edit)
{
    struct Line *line;

    if (edit->current) {
        queue_push(edit, edit->current);
        edit->current = NULL;
    }
    while ((line = pull_line(edit)) != NULL) {
        if (line->phantom) {
            line_free(edit, line);
            break;
        }
        line_arrive(edit, line);
        queue_push(edit, line);
    }
    queue_flush(edit);
}

/* REWIND: everything written so far becomes the new source file, and a fresh
   destination takes its place, so that lines inserted during the first pass
   are original lines with numbers of their own from here on.

   The destination just written cannot be reopened for writing as well as for
   reading -- opening it MODE_NEWFILE would truncate the file being read from
   -- so the new destination is a new temporary, and the temporary left behind
   by an earlier rewind is deleted once it has been read through. */
static void rewind_file(struct Edit *edit)
{
    struct Sink *old_sink = edit->primary_sink;
    struct Source *old_source = edit->primary_source;
    UBYTE finished_name[NAME_SIZE];
    UBYTE stale[NAME_SIZE];
    BOOL stale_temp;
    BOOL finished_temp;
    BPTR handle;

    if (edit->sink != old_sink) {
        report(edit, "Rewind needs the main output file");
        return;
    }
    if (edit->source != old_source) {
        report(edit, "Rewind needs the main input file");
        return;
    }
    drain_source(edit);

    copy_name(finished_name, old_sink->name,
              (LONG)strlen((const char *)old_sink->name));
    finished_temp = old_sink->temporary;
    copy_name(stale, old_source->name,
              (LONG)strlen((const char *)old_source->name));
    stale_temp = old_source->temporary;

    /* Every file the FROM command opened has been read through into the
       destination, so the rewound file is the only input from here on. */
    while (edit->sources) {
        struct Source *source = edit->sources;

        edit->sources = source->next;
        if (source->reader.handle)
            Close(source->reader.handle);
        FreeVec(source);
    }
    edit->source = NULL;
    edit->primary_source = NULL;
    while (edit->sinks) {
        struct Sink *sink = edit->sinks;

        edit->sinks = sink->next;
        if (sink->handle)
            Close(sink->handle);
        FreeVec(sink);
    }
    edit->sink = NULL;
    edit->primary_sink = NULL;

    handle = Open((CONST_STRPTR)finished_name, MODE_OLDFILE);
    if (!handle) {
        report_name(edit, "can't reopen", finished_name);
        edit->finished = TRUE;
        edit->result = RETURN_FAIL;
        return;
    }
    edit->source = (struct Source *)AllocVec((ULONG)sizeof(*edit->source),
                                             MEMF_ANY | MEMF_CLEAR);
    if (!edit->source) {
        Close(handle);
        report(edit, "Out of memory");
        edit->finished = TRUE;
        edit->result = RETURN_FAIL;
        return;
    }
    reader_init(&edit->source->reader, handle);
    copy_name(edit->source->name, finished_name,
              (LONG)strlen((const char *)finished_name));
    edit->source->next_number = 1;
    edit->source->temporary = finished_temp;
    edit->sources = edit->source;
    edit->primary_source = edit->source;
    edit->renumber = 0;

    if (stale_temp)
        DeleteFile((CONST_STRPTR)stale);

    work_file_name(edit, stale);
    edit->sink = open_sink(edit, stale, TRUE);
    if (!edit->sink) {
        edit->finished = TRUE;
        edit->result = RETURN_FAIL;
        return;
    }
    edit->primary_sink = edit->sink;
    next_line(edit);
    verify_current(edit);
}

/* ------------------------------------------------------------------ */
/* Command execution. */

static void command_move(struct Edit *edit, struct Parser *parser)
{
    LONG number;
    LONG character = parse_peek(parser);

    if (character == '*') {
        parser->position++;
        move_to_end(edit);
    } else if (parse_number(parser, &number))
        move_to_number(edit, number);
    else {
        /* The offending character is taken with the command, so that the
           error points at it and it is not then read as a command of its
           own. */
        if (character >= 0)
            parser->position++;
        report(edit, "Number expected after M");
    }
}

static void command_delete(struct Edit *edit, struct Parser *parser)
{
    LONG first;
    LONG last;

    if (!parse_number(parser, &first)) {
        delete_current(edit);
        return;
    }
    if (!move_to_number(edit, first))
        return;
    if (parse_number(parser, &last)) {
        while (first <= last && delete_current(edit))
            first++;
    } else
        delete_current(edit);
}

static void command_delete_found(struct Edit *edit, struct Parser *parser)
{
    UBYTE text[STRING_SIZE];
    LONG length;

    if (parse_string(parser, text, &length)) {
        memcpy(edit->search, text, (size_t)length + 1);
        edit->search_length = length;
    } else if (edit->search_length == 0) {
        report(edit, "No search string");
        return;
    }
    while (line_find(edit->current, 0, edit->search, edit->search_length) < 0) {
        if (at_end(edit)) {
            report(edit, "No match");
            return;
        }
        if (!delete_current(edit))
            return;
    }
    verify_current(edit);
}

static void command_find(struct Edit *edit, struct Parser *parser,
                         BOOL backward)
{
    UBYTE text[STRING_SIZE];
    LONG length;

    if (parse_string(parser, text, &length)) {
        memcpy(edit->search, text, (size_t)length + 1);
        edit->search_length = length;
    } else if (edit->search_length == 0) {
        report(edit, "No search string");
        return;
    }
    for (;;) {
        if (backward) {
            if (!previous_line(edit))
                return;
        } else if (!next_line(edit))
            return;
        if (line_find(edit->current, 0, edit->search, edit->search_length) >= 0)
            return;
        /* Searching off the end of the file is reported as running out of
           input, the same as any other move past the last line, and it leaves
           the extra line current. */
        if (!backward && at_end(edit)) {
            report(edit, "Input exhausted");
            return;
        }
    }
}

/* A, B and E, with and without the ,P qualifier that leaves the line window
   pointing after the new text. */
static void command_change(struct Edit *edit, struct Parser *parser,
                           UBYTE type, BOOL move_pointer)
{
    UBYTE first[STRING_SIZE];
    UBYTE second[STRING_SIZE];
    LONG first_length = 0;
    LONG second_length = 0;
    LONG at;

    if (!edit->current) {
        report(edit, "No current line");
        return;
    }
    if (!parse_pair(parser, first, &first_length, second, &second_length)) {
        report(edit, "Two strings needed");
        return;
    }
    at = line_find(edit->current, edit->pointer, first, first_length);
    if (at < 0) {
        report(edit, "No match");
        return;
    }
    switch (type) {
    case 'A':
        if (!line_insert(edit, edit->current, at + first_length, second,
                         second_length))
            return;
        if (move_pointer)
            edit->pointer = at + first_length + second_length;
        break;
    case 'B':
        if (!line_insert(edit, edit->current, at, second, second_length))
            return;
        if (move_pointer)
            edit->pointer = at + second_length;
        break;
    default:
        line_delete(edit->current, at, first_length);
        if (!line_insert(edit, edit->current, at, second, second_length))
            return;
        if (move_pointer)
            edit->pointer = at + second_length;
        break;
    }
    verify_current(edit);
}

static void command_global(struct Edit *edit, struct Parser *parser,
                           UBYTE type)
{
    UBYTE first[STRING_SIZE];
    UBYTE second[STRING_SIZE];
    LONG first_length = 0;
    LONG second_length = 0;
    struct Global *global;
    struct Global **link;

    if (!parse_pair(parser, first, &first_length, second, &second_length)) {
        report(edit, "Two strings needed");
        return;
    }
    if (first_length == 0) {
        report(edit, "No search string");
        return;
    }
    global = (struct Global *)AllocVec((ULONG)sizeof(*global),
                                       MEMF_ANY | MEMF_CLEAR);
    if (!global) {
        report(edit, "Out of memory");
        return;
    }
    global->id = edit->next_global++;
    global->type = type;
    global->enabled = TRUE;
    memcpy(global->find, first, (size_t)first_length + 1);
    global->find_length = first_length;
    memcpy(global->with, second, (size_t)second_length + 1);
    global->with_length = second_length;
    for (link = &edit->globals; *link; link = &(*link)->next)
        ;
    *link = global;
    /* The manual has a new global apply to the line that is current when it
       is given, as well as to every line reached afterwards. */
    if (edit->current) {
        struct Global *saved = edit->globals;

        edit->globals = global;
        apply_globals(edit, edit->current);
        edit->globals = saved;
        verify_current(edit);
    }
    /* The original announces a new global as G followed by its number. */
    ver_char(edit, 'G');
    ver_number(edit, global->id);
    ver_newline(edit);
}

static void command_global_state(struct Edit *edit, struct Parser *parser,
                                 BOOL cancel, BOOL enable)
{
    LONG id;
    BOOL one = parse_number(parser, &id);
    struct Global **link = &edit->globals;

    while (*link) {
        struct Global *global = *link;

        if (one && global->id != id) {
            link = &global->next;
            continue;
        }
        if (cancel) {
            *link = global->next;
            FreeVec(global);
            if (one)
                return;
            continue;
        }
        global->enabled = enable;
        if (one)
            return;
        link = &global->next;
    }
    if (one && cancel)
        report(edit, "No such global");
}

static void command_show_globals(struct Edit *edit)
{
    struct Global *global;

    for (global = edit->globals; global; global = global->next) {
        ver_number(edit, global->id);
        ver_text(edit, ": G");
        ver_char(edit, global->type);
        ver_text(edit, " /");
        ver_bytes(edit, global->find, global->find_length);
        ver_char(edit, '/');
        ver_bytes(edit, global->with, global->with_length);
        ver_text(edit, "/ ");
        ver_number(edit, global->count);
        ver_text(edit, global->enabled ? " matched" : " matched, suspended");
        ver_newline(edit);
    }
    if (!edit->globals)
        report(edit, "No global commands");
}

static void command_show_state(struct Edit *edit)
{
    ver_text(edit, "search string: ");
    ver_bytes(edit, edit->search, edit->search_length);
    ver_newline(edit);
    ver_text(edit, "last command:  ");
    ver_bytes(edit, edit->last_command, edit->last_command_length);
    ver_newline(edit);
    ver_text(edit, "terminator:    ");
    ver_bytes(edit, edit->terminator, edit->terminator_length);
    ver_newline(edit);
    ver_text(edit, "trailing spaces are ");
    ver_text(edit, edit->trailing ? "kept" : "dropped");
    ver_newline(edit);
}

/* The window deletions: D,T,A and D,T,B delete forward from the window's
   start to the string, D,F,A and D,F,B delete from the string to the end of
   the line. */
static void command_delete_window(struct Edit *edit, struct Parser *parser,
                                  BOOL from, BOOL after)
{
    UBYTE text[STRING_SIZE];
    LONG length;
    LONG at;

    if (!edit->current) {
        report(edit, "No current line");
        return;
    }
    if (!parse_string(parser, text, &length)) {
        report(edit, "String needed");
        return;
    }
    at = line_find(edit->current, edit->pointer, text, length);
    if (at < 0) {
        report(edit, "No match");
        return;
    }
    if (from)
        line_delete(edit->current, after ? at + length : at,
                    edit->current->length);
    else
        line_delete(edit->current, edit->pointer,
                    (after ? at + length : at) - edit->pointer);
    verify_current(edit);
}

static void command_split(struct Edit *edit, struct Parser *parser, BOOL after)
{
    UBYTE text[STRING_SIZE];
    LONG length;
    LONG at;
    struct Line *tail;

    if (!edit->current) {
        report(edit, "No current line");
        return;
    }
    if (!parse_string(parser, text, &length)) {
        report(edit, "String needed");
        return;
    }
    at = line_find(edit->current, edit->pointer, text, length);
    if (at < 0) {
        report(edit, "No match");
        return;
    }
    if (after)
        at += length;
    tail = line_alloc(edit);
    if (!tail) {
        report(edit, "Out of memory");
        return;
    }
    tail->length = edit->current->length - at;
    memcpy(tail->text, edit->current->text + at, (size_t)tail->length);
    tail->text[tail->length] = '\0';
    tail->newline = edit->current->newline;
    tail->processed = TRUE;
    edit->current->length = at;
    edit->current->text[at] = '\0';
    edit->current->newline = TRUE;
    /* The first part is finished with: it goes to the output queue, and the
       remainder becomes a new non-original current line. */
    queue_push(edit, edit->current);
    edit->current = tail;
    edit->pointer = 0;
    verify_current(edit);
}

static void command_join(struct Edit *edit, struct Parser *parser)
{
    UBYTE text[STRING_SIZE];
    LONG length = 0;
    struct Line *next;

    if (!edit->current || edit->current->phantom) {
        report(edit, "Input exhausted");
        return;
    }
    if (parse_string(parser, text, &length))
        if (!line_insert(edit, edit->current, edit->current->length, text,
                         length))
            return;
    next = pull_line(edit);
    if (!next) {
        report(edit, "Input exhausted");
        return;
    }
    if (next->phantom) {
        line_free(edit, next);
        edit->current->newline = FALSE;
        verify_current(edit);
        return;
    }
    line_arrive(edit, next);
    if (!line_insert(edit, edit->current, edit->current->length, next->text,
                     next->length)) {
        /* The join did not fit: give the line back rather than lose it. */
        next->next = edit->ahead;
        edit->ahead = next;
        return;
    }
    edit->current->newline = next->newline;
    line_free(edit, next);
    edit->pointer = 0;
    verify_current(edit);
}

static BOOL interrupted(void)
{
#ifdef SIGBREAKF_CTRL_C
    return (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) != 0;
#else
    return FALSE;
#endif
}

static void type_numbered(struct Edit *edit, struct Line *line)
{
    UBYTE field[8];
    LONG at = 0;
    LONG index;

    if (line->number > 0) {
        LONG digits = 1;
        LONG scale = 10;

        while (line->number >= scale && digits < 5) {
            digits++;
            scale *= 10;
        }
        for (index = digits; index < 5; index++)
            field[at++] = ' ';
        append_number(field, &at, line->number, 1);
    } else {
        memcpy(field, "  +++", 5);
        at = 5;
    }
    ver_bytes(edit, field, at);
    ver_text(edit, "  ");
    for (index = 0; index < line->length; index++)
        ver_char(edit, is_graphic(line->text[index]) ? line->text[index] : '?');
    ver_newline(edit);
    edit->shown_serial = line->serial;
    edit->numbered_serial = line->serial;
    edit->shown_any = TRUE;
    edit->verified = TRUE;
}

static void command_type(struct Edit *edit, struct Parser *parser,
                         BOOL numbered)
{
    LONG count;
    BOOL limited = parse_number(parser, &count);

    for (;;) {
        if (interrupted()) {
            report(edit, "***BREAK");
            return;
        }
        if (limited && count-- <= 0)
            return;
        if (!edit->current)
            return;
        if (numbered)
            type_numbered(edit, edit->current);
        else
            verify_line(edit, edit->current, FALSE);
        /* The first line typed is the current line, the extra line past the
           end of the file included, and typing stops there. */
        if (at_end(edit))
            return;
        if (!next_line(edit))
            return;
    }
}

static void command_type_queue(struct Edit *edit)
{
    LONG index;

    for (index = 0; index < edit->queue_count; index++)
        verify_line(edit, queue_at(edit, index), FALSE);
}

/* T,N: type forward until every line now in the output queue has been written
   out and replaced.  A line's serial number says which generation it belongs
   to, so the loop can tell when the last of them has gone. */
static void command_type_new(struct Edit *edit)
{
    LONG threshold = edit->next_serial;

    while (edit->queue_count > 0 &&
           queue_at(edit, 0)->serial <= threshold) {
        if (interrupted()) {
            report(edit, "***BREAK");
            return;
        }
        if (!edit->current || at_end(edit))
            return;
        verify_line(edit, edit->current, FALSE);
        if (!next_line(edit))
            return;
    }
}

static void command_pointer_string(struct Edit *edit, struct Parser *parser,
                                   BOOL after)
{
    UBYTE text[STRING_SIZE];
    LONG length;
    LONG at;

    if (!edit->current) {
        report(edit, "No current line");
        return;
    }
    if (!parse_string(parser, text, &length)) {
        report(edit, "String needed");
        return;
    }
    at = line_find(edit->current, edit->pointer, text, length);
    if (at < 0) {
        report(edit, "No match");
        return;
    }
    edit->pointer = after ? at + length : at;
    verify_current(edit);
}

static void command_from(struct Edit *edit, struct Parser *parser)
{
    UBYTE name[NAME_SIZE];

    if (!parse_file(parser, name)) {
        /* No argument reselects the file EDIT was started on, which is the
           last one on the list. */
        struct Source *source = edit->sources;

        while (source && source->next)
            source = source->next;
        if (source)
            edit->source = source;
        return;
    }
    {
        struct Source *source = open_source(edit, name);

        if (source)
            edit->source = source;
    }
}

static void command_to(struct Edit *edit, struct Parser *parser)
{
    UBYTE name[NAME_SIZE];
    struct Sink *sink;

    if (!parse_file(parser, name)) {
        sink = edit->sinks;
        while (sink && sink->next)
            sink = sink->next;
        if (sink)
            edit->sink = sink;
    } else {
        sink = open_sink(edit, name, FALSE);
        if (!sink)
            return;
        edit->sink = sink;
    }
    /* "The TO command writes the existing queue of output lines to the new TO
       file" -- so the switch happens first, and then the queue is emptied. */
    queue_flush(edit);
}

/* ------------------------------------------------------------------ */
/* Ending. */

static void close_files(struct Edit *edit)
{
    while (edit->sources) {
        struct Source *source = edit->sources;

        edit->sources = source->next;
        if (source->reader.handle)
            Close(source->reader.handle);
        FreeVec(source);
    }
    edit->source = NULL;
    while (edit->sinks) {
        struct Sink *sink = edit->sinks;

        edit->sinks = sink->next;
        if (sink->handle)
            Close(sink->handle);
        FreeVec(sink);
    }
    edit->sink = NULL;
    edit->primary_source = NULL;
    edit->primary_sink = NULL;
    while (edit->depth >= 0) {
        if (edit->commands[edit->depth].close)
            Close(edit->commands[edit->depth].reader.handle);
        edit->depth--;
    }
}

static BOOL copy_file(const UBYTE *from, const UBYTE *to)
{
    UBYTE buffer[READ_BUFFER];
    BPTR in;
    BPTR out;
    LONG got;

    in = Open((CONST_STRPTR)from, MODE_OLDFILE);
    if (!in)
        return FALSE;
    out = Open((CONST_STRPTR)to, MODE_NEWFILE);
    if (!out) {
        Close(in);
        return FALSE;
    }
    while ((got = Read(in, buffer, READ_BUFFER)) > 0)
        if (Write(out, buffer, got) != got) {
            Close(in);
            Close(out);
            return FALSE;
        }
    Close(in);
    Close(out);
    return got >= 0;
}

/* The work file lives in T: and the file being edited generally does not, and
   AmigaDOS cannot rename across devices, so a move that fails is retried as a
   copy.  This is why the editor can afford to keep its work where the manual
   says it does. */
static BOOL move_file(const UBYTE *from, const UBYTE *to)
{
    if (Rename((CONST_STRPTR)from, (CONST_STRPTR)to))
        return TRUE;
    if (!copy_file(from, to))
        return FALSE;
    DeleteFile((CONST_STRPTR)from);
    return TRUE;
}

/* The source file becomes T:EDIT-BACKUP, which lasts only until the next
   edit.  If T: will not take it, the backup goes beside the file instead,
   because losing the original would be worse than keeping it elsewhere. */
static void make_backup(struct Edit *edit)
{
    UBYTE backup[NAME_SIZE];
    LONG length;

    DeleteFile((CONST_STRPTR)"T:EDIT-BACKUP");
    if (move_file(edit->source_name, (const UBYTE *)"T:EDIT-BACKUP"))
        return;
    length = (LONG)strlen((const char *)edit->source_name);
    copy_name(backup, edit->source_name, length);
    if (length < NAME_SIZE - 8)
        memcpy(backup + length, "-backup", 8);
    DeleteFile((CONST_STRPTR)backup);
    if (!move_file(edit->source_name, backup))
        DeleteFile((CONST_STRPTR)edit->source_name);
}

/* W.  Whatever is left of the source is passed through, and the destination
   takes the name the editing has to end up under: its own, when a TO file was
   named and no rewind moved the output elsewhere, and otherwise the name of a
   temporary that is renamed into place here. */
static void finish_saving(struct Edit *edit)
{
    UBYTE temporary[NAME_SIZE];
    BOOL rename_needed;

    drain_source(edit);
    rename_needed = edit->primary_sink && edit->primary_sink->temporary;
    if (rename_needed)
        copy_name(temporary, edit->primary_sink->name,
                  (LONG)strlen((const char *)edit->primary_sink->name));
    close_files(edit);
    /* A work file left over from a rewind stays in T:. The original does not
       remove its work files; the next edit in the same process opens the same
       names over the top of them. */
    if (!rename_needed)
        return;
    if (edit->backup_source)
        make_backup(edit);
    else
        DeleteFile((CONST_STRPTR)edit->final_name);
    if (!move_file(temporary, edit->final_name)) {
        report_name(edit, "Can't write", edit->final_name);
        edit->result = RETURN_FAIL;
    }
}

/* STOP.  Nothing is renamed, so the source file is exactly as it was, but the
   lines already passed are written out before the files are closed, and the
   work file is left in T: holding them.  On the original, a session that had
   walked a file to its end and then stopped left a work file exactly the size
   of the file it was editing.  The current line and everything after it never
   reach the work file, and the source is untouched either way. */
static void finish_stopping(struct Edit *edit)
{
    queue_flush(edit);
    close_files(edit);
}

/* ------------------------------------------------------------------ */

static void execute_one(struct Edit *edit, struct Parser *parser)
{
    LONG character = parse_peek(parser);
    LONG count = 1;
    LONG index;
    enum Command command;
    BOOL flag;

    if (character < 0)
        return;
    if (is_digit((UBYTE)character)) {
        parse_number(parser, &count);
        character = parse_peek(parser);
        if (character < 0) {
            report(edit, "Command needed");
            return;
        }
    }

    /* A repeat group: n(commands), with the commands inside separated by
       semicolons.  The count belongs to the whole group, so 2(N;?) advances
       and verifies twice.  The scan for the closing parenthesis counts nested
       ones but does not look inside string arguments, so a parenthesis used
       as a string delimiter inside a group is not understood. */
    if (character == '(') {
        LONG start = parser->position + 1;
        LONG depth = 1;
        LONG scan = start;

        while (scan < parser->length && depth > 0) {
            if (parser->text[scan] == '(')
                depth++;
            else if (parser->text[scan] == ')')
                depth--;
            scan++;
        }
        if (depth > 0) {
            parser->position = parser->length;
            report(edit, "Unmatched parenthesis");
            return;
        }
        for (index = 0; index < count && !edit->finished; index++) {
            struct Parser body;

            parser_init(&body, parser->text + start, scan - 1 - start,
                        parser->offset + start);
            execute_commands(edit, &body);
        }
        parser->position = scan;
        return;
    }

    /* The single-character commands, which take no name of their own. */
    switch (character) {
    case ')':
        parser->position++;
        report(edit, "Unmatched parenthesis");
        return;
    case ';':
        parser->position++;
        return;
    case '?':
        parser->position++;
        verify_line(edit, edit->current, TRUE);
        return;
    case '!':
        parser->position++;
        verify_hex(edit);
        return;
    case '=':
        parser->position++;
        {
            LONG number;

            if (!parse_number(parser, &number)) {
                report(edit, "Line number needed");
                return;
            }
            if (edit->current)
                edit->current->number = number;
            edit->renumber = number + 1;
        }
        return;
    case '>':
        parser->position++;
        if (edit->current) {
            edit->pointer += count;
            if (edit->pointer > edit->current->length)
                edit->pointer = edit->current->length;
        }
        verify_current(edit);
        return;
    case '<':
        parser->position++;
        edit->pointer -= count;
        if (edit->pointer < 0)
            edit->pointer = 0;
        verify_current(edit);
        return;
    case '$':
    case '%':
    case '_':
        parser->position++;
        if (!edit->current) {
            report(edit, "No current line");
            return;
        }
        for (index = 0; index < count; index++) {
            if (edit->pointer >= edit->current->length)
                break;
            if (character == '$')
                edit->current->text[edit->pointer] =
                    lower_case(edit->current->text[edit->pointer]);
            else if (character == '%')
                edit->current->text[edit->pointer] =
                    upper_case(edit->current->text[edit->pointer]);
            else
                edit->current->text[edit->pointer] = ' ';
            edit->pointer++;
        }
        verify_current(edit);
        return;
    case '#':
        parser->position++;
        if (!edit->current) {
            report(edit, "No current line");
            return;
        }
        line_delete(edit->current, edit->pointer, count);
        verify_current(edit);
        return;
    default:
        break;
    }

    command = parse_command(parser);
    switch (command) {
    case CMD_NONE:
        report_unknown(edit, parser);
        parser->position = parser->length;
        return;

    case CMD_M:
        command_move(edit, parser);
        return;
    case CMD_N: {
        LONG number;

        if (parse_number(parser, &number))
            count = number;
        for (index = 0; index < count; index++)
            if (!next_line(edit))
                break;
        verify_current(edit);
        return;
    }
    case CMD_P: {
        LONG number;

        if (parse_number(parser, &number))
            count = number;
        for (index = 0; index < count; index++)
            if (!previous_line(edit))
                break;
        verify_current(edit);
        return;
    }
    case CMD_F:
        command_find(edit, parser, FALSE);
        return;
    case CMD_BF:
        command_find(edit, parser, TRUE);
        return;

    case CMD_A:
        command_change(edit, parser, 'A', FALSE);
        return;
    case CMD_B:
        command_change(edit, parser, 'B', FALSE);
        return;
    case CMD_E:
        command_change(edit, parser, 'E', FALSE);
        return;
    case CMD_AP:
        command_change(edit, parser, 'A', TRUE);
        return;
    case CMD_BP:
        command_change(edit, parser, 'B', TRUE);
        return;
    case CMD_EP:
        command_change(edit, parser, 'E', TRUE);
        return;

    case CMD_I: {
        LONG number;

        if (parse_peek(parser) == '*') {
            parser->position++;
            move_to_end(edit);
        } else if (parse_number(parser, &number) &&
                   !move_to_number(edit, number))
            return;
        /* Inserting does not change the current line, so it does not arm the
           end-of-line display: M2;I shows line 2 because M2 moved to it, and
           an insert below a line already shown adds no display at all. */
        insert_lines(edit);
        return;
    }
    case CMD_D:
        command_delete(edit, parser);
        return;
    case CMD_DF:
        command_delete_found(edit, parser);
        return;
    case CMD_R: {
        LONG number;

        if (parse_number(parser, &number) && !move_to_number(edit, number))
            return;
        if (!delete_current(edit))
            return;
        insert_lines(edit);
        verify_current(edit);
        return;
    }
    case CMD_Z: {
        UBYTE text[STRING_SIZE];
        LONG length;

        if (parse_string(parser, text, &length)) {
            memcpy(edit->terminator, text, (size_t)length + 1);
            edit->terminator_length = length;
        } else {
            UBYTE name[NAME_SIZE];

            if (parse_file(parser, name)) {
                LONG length2 = (LONG)strlen((const char *)name);

                copy_name(edit->terminator, name, length2);
                edit->terminator_length = length2;
            } else
                report(edit, "String needed");
        }
        return;
    }
    case CMD_SHD:
        command_show_state(edit);
        return;
    case CMD_TR:
        if (parse_switch(parser, &flag))
            edit->trailing = flag;
        else
            report(edit, "+ or - needed");
        return;

    case CMD_PR:
        edit->pointer = 0;
        verify_current(edit);
        return;
    case CMD_PA:
        command_pointer_string(edit, parser, TRUE);
        return;
    case CMD_PB:
        command_pointer_string(edit, parser, FALSE);
        return;

    case CMD_DTA:
        command_delete_window(edit, parser, FALSE, TRUE);
        return;
    case CMD_DTB:
        command_delete_window(edit, parser, FALSE, FALSE);
        return;
    case CMD_DFA:
        command_delete_window(edit, parser, TRUE, TRUE);
        return;
    case CMD_DFB:
        command_delete_window(edit, parser, TRUE, FALSE);
        return;

    case CMD_SB:
        command_split(edit, parser, FALSE);
        return;
    case CMD_SA:
        command_split(edit, parser, TRUE);
        return;
    case CMD_CL:
        command_join(edit, parser);
        return;

    case CMD_REWIND:
        rewind_file(edit);
        return;
    case CMD_V:
        if (parse_switch(parser, &flag))
            edit->verify = flag;
        else
            report(edit, "+ or - needed");
        return;
    case CMD_T:
        command_type(edit, parser, FALSE);
        return;
    case CMD_TL:
        command_type(edit, parser, TRUE);
        return;
    case CMD_TP:
        command_type_queue(edit);
        return;
    case CMD_TN:
        command_type_new(edit);
        return;

    case CMD_GA:
        command_global(edit, parser, 'A');
        return;
    case CMD_GB:
        command_global(edit, parser, 'B');
        return;
    case CMD_GE:
        command_global(edit, parser, 'E');
        return;
    case CMD_CG:
        command_global_state(edit, parser, TRUE, FALSE);
        return;
    case CMD_SG:
        command_global_state(edit, parser, FALSE, FALSE);
        return;
    case CMD_EG:
        command_global_state(edit, parser, FALSE, TRUE);
        return;
    case CMD_SHG:
        command_show_globals(edit);
        return;

    case CMD_C: {
        UBYTE name[NAME_SIZE];

        if (parse_file(parser, name))
            push_command_file(edit, name);
        else
            report(edit, "File name needed");
        return;
    }
    case CMD_FROM:
        command_from(edit, parser);
        return;
    case CMD_TO:
        command_to(edit, parser);
        return;
    case CMD_CF: {
        UBYTE name[NAME_SIZE];

        if (parse_file(parser, name))
            close_named_file(edit, name);
        else
            report(edit, "File name needed");
        return;
    }
    case CMD_Q:
        /* Stops the current command file; at the outermost level there is no
           command file to stop, and Q means W. */
        if (edit->depth > 0) {
            if (edit->commands[edit->depth].close)
                Close(edit->commands[edit->depth].reader.handle);
            edit->depth--;
            return;
        }
        edit->finished = TRUE;
        edit->saving = TRUE;
        return;
    case CMD_W:
        edit->finished = TRUE;
        edit->saving = TRUE;
        return;
    case CMD_STOP:
        edit->finished = TRUE;
        edit->saving = FALSE;
        edit->result = RETURN_WARN;
        return;
    }
}

/* Runs commands until the parser is exhausted.  A repeat group re-enters
   this with a parser over its body, so the count applies to everything
   between the parentheses. */
static void execute_commands(struct Edit *edit, struct Parser *parser)
{
    struct Parser *outer = edit->active;

    edit->active = parser;
    while (!edit->finished) {
        LONG before = parser->position;

        if (parse_peek(parser) < 0)
            break;
        execute_one(edit, parser);
        if (parser->position == before) {
            report_unknown(edit, parser);
            break;
        }
    }
    edit->active = outer;
}

static void execute_line(struct Edit *edit, UBYTE *text, LONG length)
{
    struct Parser parser;

    if (length > 0) {
        LONG keep = length < COMMAND_SIZE - 1 ? length : COMMAND_SIZE - 1;

        memcpy(edit->last_command, text, (size_t)keep);
        edit->last_command[keep] = '\0';
        edit->last_command_length = keep;
    }
    edit->entry_serial = edit->current ? edit->current->serial : 0;
    edit->modified = FALSE;
    edit->shown_serial = 0;
    edit->numbered_serial = 0;
    edit->shown_any = FALSE;
    edit->verified = FALSE;
    parser_init(&parser, text, length, 0);
    execute_commands(edit, &parser);
    /* The line the commands left behind is shown once, here, rather than by
       each command that touched it. */
    if (!edit->finished)
        verify_pending(edit);
}

/* ------------------------------------------------------------------ */
/* Startup. */

/* OPT P<n>W<n>, the older spelling of the PREVIOUS and WIDTH arguments. */
static void parse_options(struct Edit *edit, const UBYTE *text)
{
    while (*text) {
        UBYTE letter = upper_case(*text++);
        LONG value = 0;
        BOOL any = FALSE;

        while (is_digit(*text)) {
            value = value * 10 + (*text++ - '0');
            any = TRUE;
        }
        if (!any)
            continue;
        if (letter == 'P')
            edit->previous = value;
        else if (letter == 'W')
            edit->width = value;
    }
}

static LONG clamp(LONG value, LONG low, LONG high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

int main(void)
{
    struct RDArgs *args_handle;
    IPTR args[ARG_COUNT];
    struct Edit edit;
    UBYTE command[COMMAND_SIZE];
    UBYTE temporary[NAME_SIZE];
    LONG length;
    BPTR handle;

    memset(args, 0, sizeof(args));
    memset(&edit, 0, sizeof(edit));
    edit.width = DEFAULT_WIDTH;
    edit.previous = DEFAULT_PREVIOUS;
    edit.verify = TRUE;
    edit.ver = Output();
    edit.depth = -1;
    edit.next_global = 1;
    edit.terminator[0] = 'Z';
    edit.terminator[1] = '\0';
    edit.terminator_length = 1;
    edit.result = RETURN_OK;

    args_handle = ReadArgs((CONST_STRPTR)EDIT_TEMPLATE, (IPTR *)args, NULL);
    if (!args_handle) {
        PrintFault(IoErr(), (CONST_STRPTR)"Edit");
        return RETURN_FAIL;
    }

    if (args[ARG_OPT])
        parse_options(&edit, (const UBYTE *)args[ARG_OPT]);
    if (args[ARG_WIDTH])
        edit.width = *(LONG *)args[ARG_WIDTH];
    if (args[ARG_PREVIOUS])
        edit.previous = *(LONG *)args[ARG_PREVIOUS];
    edit.width = clamp(edit.width, MINIMUM_WIDTH, MAXIMUM_WIDTH);
    edit.previous = clamp(edit.previous, MINIMUM_PREVIOUS, MAXIMUM_PREVIOUS);

    if (args[ARG_VER]) {
        handle = Open((CONST_STRPTR)args[ARG_VER], MODE_NEWFILE);
        if (!handle) {
            PrintFault(IoErr(), (CONST_STRPTR)args[ARG_VER]);
            FreeArgs(args_handle);
            return RETURN_FAIL;
        }
        edit.ver = handle;
        edit.ver_close = TRUE;
    }

    edit.queue = (struct Line **)AllocVec((ULONG)((size_t)edit.previous *
                                                  sizeof(struct Line *)),
                                          MEMF_ANY | MEMF_CLEAR);
    if (!edit.queue) {
        report(&edit, "out of memory");
        goto fail;
    }

    if (!open_source(&edit, (const UBYTE *)args[ARG_FROM]))
        goto fail;
    edit.source = edit.sources;
    edit.primary_source = edit.source;
    copy_name(edit.source_name, (const UBYTE *)args[ARG_FROM],
              (LONG)strlen((const char *)args[ARG_FROM]));

    /* With no TO file the editing goes to a temporary and the source becomes
       the backup at the end, which is what makes an edit of a file in place
       survive a failure partway through. */
    if (args[ARG_TO] &&
        !same_name((const UBYTE *)args[ARG_TO], edit.source_name)) {
        copy_name(edit.final_name, (const UBYTE *)args[ARG_TO],
                  (LONG)strlen((const char *)args[ARG_TO]));
        if (!open_sink(&edit, edit.final_name, FALSE))
            goto fail;
    } else {
        /* Naming the source as the destination is the in-place edit: opening
           it for output would truncate the file being read. */
        copy_name(edit.final_name, edit.source_name,
                  (LONG)strlen((const char *)edit.source_name));
        edit.backup_source = TRUE;
        work_file_name(&edit, temporary);
        if (!open_sink(&edit, temporary, TRUE))
            goto fail;
    }
    edit.sink = edit.sinks;
    edit.primary_sink = edit.sink;

    /* The keyboard is the bottom of the command stack; a WITH file sits on
       top of it, so that its commands run first and control returns to the
       keyboard when it ends -- which is what makes Q at the outermost level
       the same as W. */
    edit.depth = 0;
    reader_init(&edit.commands[0].reader, Input());
    edit.commands[0].close = FALSE;
    if (args[ARG_WITH])
        push_command_file(&edit, (const UBYTE *)args[ARG_WITH]);

    /* The first line becomes current without being shown: the original
       announces itself and then waits for a command. */
    next_line(&edit);
    report(&edit, "Editor");

    while (!edit.finished && command_line(&edit, command, &length))
        execute_line(&edit, command, length);

    /* Command input running out is the same as W: what has been edited so far
       is written, rather than thrown away. */
    if (!edit.finished)
        edit.saving = TRUE;
    if (edit.saving)
        finish_saving(&edit);
    else
        finish_stopping(&edit);

    ver_flush(&edit);
    if (edit.ver_close)
        Close(edit.ver);
    while (edit.globals) {
        struct Global *global = edit.globals;

        edit.globals = global->next;
        FreeVec(global);
    }
    line_free(&edit, edit.current);
    while (edit.ahead) {
        struct Line *line = edit.ahead;

        edit.ahead = line->next;
        line_free(&edit, line);
    }
    while (edit.queue_count > 0)
        line_free(&edit, queue_pop(&edit));
    lines_release(&edit);
    FreeVec(edit.queue);
    FreeArgs(args_handle);
    return (int)edit.result;

fail:
    ver_flush(&edit);
    finish_stopping(&edit);
    if (edit.ver_close)
        Close(edit.ver);
    lines_release(&edit);
    if (edit.queue)
        FreeVec(edit.queue);
    FreeArgs(args_handle);
    return RETURN_FAIL;
}
