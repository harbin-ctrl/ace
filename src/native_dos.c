#define _GNU_SOURCE

#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <dirent.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <dos/stdio.h>
#include <dos/var.h>
#include <exec/lists.h>
#include <exec/execbase.h>
#include <utility/utility.h>
#include "broker_client.h"
#include "native_host.h"
#include "broker_protocol.h"
#include "aros_dos_path.h"
#include "aros_console_editor.h"

static struct Process native_process;
/* IoErr() is the process's pr_Result2, not a variable beside it. The AROS
   DOS sources ACE compiles set it that way -- rom/dos/readargs.c ends with
   "me->pr_Result2 = error;" and never calls SetIoErr() -- so a separate
   store would leave every ReadArgs() failure reporting no reason at all:
   the command's PrintFault(IoErr(), name) printed nothing and the shell saw
   a bare failure. Keeping one storage location is what makes ACE's stubs and
   upstream's own code agree about the error. */
#define native_ioerr (native_process.pr_Result2)

static int native_name_needs_mapping(const char *name)
{
    size_t length = strlen(name);

    if (length > 107)
        return 1;
    for (size_t index = 0; index < length; index++) {
        unsigned char character = (unsigned char)name[index];

        if (!((character >= 'a' && character <= 'z') ||
              (character >= 'A' && character <= 'Z') ||
              (character >= '0' && character <= '9') ||
              character == '.' || character == '_' || character == '-'))
            return 1;
    }
    return 0;
}

static struct ExecBase native_exec_base;
static struct UtilityBase native_utility_base;
/* rn_Flags left at 0: '*' is a literal character, not also a wildcard for
   '#?', matching modern AmigaDOS's default (RNF_WILDSTAR off). Read by the
   real pattern-matching engine (rom/dos/patternmatching.c) this seam
   compiles unmodified. */
static struct RootNode native_root_node;
static struct DosLibrary native_dos_base = { &native_root_node };
/* Weak, because an AROS command may define these itself. A command that
   sets SH_GLOBAL_SYSBASE/SH_GLOBAL_DOSBASE before including
   <aros/shcommands.h> -- Dir.c does both -- gets a file-scope definition
   from the macro expansion, which on a real AROS build is the whole
   program's only copy. Here ACE's runtime is linked in beside it, so
   without weak linkage the two collide. Yielding keeps the command's own
   copy authoritative, exactly as AROS intends; ace_shcommand_start() hands
   the entry point the real base to put in it, rather than reading it back
   out of a symbol the command may not have filled in yet. */
struct ExecBase *SysBase __attribute__((weak)) = &native_exec_base;
struct DosLibrary *DOSBase __attribute__((weak)) = &native_dos_base;

struct ExecBase *native_exec_base_pointer(void)
{
    return &native_exec_base;
}
static struct CommandLineInterface native_cli;
static char native_cli_prompt[PATH_MAX];
static char native_cli_set_name[PATH_MAX];
static int native_cli_loaded;
static int native_endcli_requested;

#define NATIVE_LOCAL_VAR_LIMIT 128
#define NATIVE_LOCAL_VAR_NAME 64

static struct LocalVar native_local_vars[NATIVE_LOCAL_VAR_LIMIT];
static char native_local_var_names[NATIVE_LOCAL_VAR_LIMIT][NATIVE_LOCAL_VAR_NAME];
static char native_local_var_values[NATIVE_LOCAL_VAR_LIMIT][AMIGA_BROKER_MAX_PAYLOAD];
static FILE *native_input = NULL;
static FILE *native_output = NULL;
static char native_input_prefix[4096];
static size_t native_input_prefix_length;
static size_t native_input_prefix_position;
static int native_input_prefix_loaded;
static struct ace_aros_console_editor *native_console_editor;
static int native_console_editor_attempted;
static int native_input_raw;
/*
 * A shell started to run one script and then stop is not interactive, even
 * though its standard input is the same kind of handle an interactive one
 * has. Shell.c recomputes cli_Background from IsInteractive() when a script
 * ends, and without this it would decide the nested shell should now start
 * prompting -- on a standard input that belongs to somebody else.
 */
static int native_interactive = 1;
static unsigned char native_editor_line[8194];
static size_t native_editor_line_length;
static size_t native_editor_line_position;
static BPTR native_program_dir;
static char native_program_name[PATH_MAX];

struct native_stdio_handle {
    struct FileHandle amiga;
    FILE *stream;
};

static struct native_stdio_handle native_stdin_handle;
static struct native_stdio_handle native_stdout_handle;
static struct native_stdio_handle native_stderr_handle;
static int native_stdio_initialized;

static void set_native_broker_error(void);
static FILE *selected_output(void);

static void native_init_stdio_handles(void)
{
    if (native_stdio_initialized)
        return;
    /* RunCommand() injects a command's arguments ahead of the existing
       Input() stream.  Keep the host stream unbuffered so the shell cannot
       read ahead into the next command before a child handles ReadArgs('?').
       The real AROS input handle has the same packet-level property. */
    (void)setvbuf(stdin, NULL, _IONBF, 0);
    native_stdin_handle.amiga.fh_Pos = 0;
    native_stdin_handle.amiga.fh_End = INT_MAX / 2;
    native_stdin_handle.stream = stdin;
    native_stdout_handle.amiga.fh_Pos = 0;
    native_stdout_handle.amiga.fh_End = INT_MAX / 2;
    native_stdout_handle.stream = stdout;
    native_stderr_handle.amiga.fh_Pos = 0;
    native_stderr_handle.amiga.fh_End = INT_MAX / 2;
    native_stderr_handle.stream = stderr;
    native_stdio_initialized = 1;
}

static void native_load_input_prefix(void)
{
    const char *arguments;

    if (native_input_prefix_loaded)
        return;
    native_input_prefix_loaded = 1;
    arguments = getenv("ACE_COMMAND_ARGUMENTS");
    if (!arguments)
        return;
    /* Set but empty is a command invoked with no arguments at all, and it is
       not the same as unset. AmigaDOS leaves the argument line in the input
       stream for ReadArgs() to read, so a command with no arguments finds an
       empty line there and reports a missing /A argument. Returning here
       instead would leave ReadArgs() reading whatever the process's real
       standard input holds -- in a shell, the next command. */
    native_input_prefix_length = strlen(arguments);
    if (native_input_prefix_length >= sizeof(native_input_prefix) - 1)
        native_input_prefix_length = sizeof(native_input_prefix) - 1;
    memcpy(native_input_prefix, arguments, native_input_prefix_length);
    if (native_input_prefix_length == 0 ||
        native_input_prefix[native_input_prefix_length - 1] != '\n')
        native_input_prefix[native_input_prefix_length++] = '\n';
}

static int native_console_session_enabled(void)
{
    const char *enabled = getenv("ACE_CONSOLE_INTERACTIVE");

    return enabled && strcmp(enabled, "1") == 0;
}

static void native_console_editor_output(void)
{
    unsigned char output[4096];
    size_t length;
    FILE *file = selected_output();

    if (!native_console_editor)
        return;
    do {
        length = ace_aros_console_editor_take_output(
            native_console_editor, output, sizeof(output));
        if (length != 0) {
            if (fwrite(output, 1, length, file) != length) {
                native_ioerr = errno;
                return;
            }
            /* Keystroke echo has to reach the GUI before the next byte is
               read. The normal shell output path may remain buffered. */
            fflush(file);
        }
    } while (length != 0);
}

static int native_console_editor_start(void)
{
    if (native_console_editor_attempted)
        return native_console_editor != NULL;
    native_console_editor_attempted = 1;
    if (!native_console_session_enabled())
        return 0;
    native_console_editor = ace_aros_console_editor_open();
    return native_console_editor != NULL;
}

static int native_editor_next_char(FILE *file)
{
    unsigned char input[32];
    size_t input_length;
    int character;

    if (native_editor_line_position < native_editor_line_length)
        return native_editor_line[native_editor_line_position++];
    native_editor_line_position = 0;
    native_editor_line_length = 0;
    if (!native_console_editor_start())
        return fgetc(file);

    for (;;) {
        character = fgetc(file);
        if (character == EOF)
            return EOF;
        input[0] = (unsigned char)character;
        input_length = 1;
        /* support.c's CSI matcher receives a buffer, not a stream: give it
           the complete key sequence so an arrow is interpreted as history
           navigation rather than as an unknown CSI followed by a literal
           'A'. The AROS console sequences all terminate in a final letter or
           '~'; the bound keeps malformed input from consuming indefinitely. */
        if (input[0] == 0x9b) {
            while (input_length < sizeof(input)) {
                character = fgetc(file);
                if (character == EOF)
                    break;
                input[input_length++] = (unsigned char)character;
                if ((character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z') ||
                    character == '~' || character == '@')
                    break;
            }
        }
        if (ace_aros_console_editor_feed(native_console_editor, input,
                                          input_length) != 0)
            return input[0];
        native_console_editor_output();
        native_editor_line_length = ace_aros_console_editor_take_line(
            native_console_editor, native_editor_line,
            sizeof(native_editor_line));
        if (native_editor_line_length != 0)
            return native_editor_line[native_editor_line_position++];
    }
}

static int native_input_getc(FILE *file)
{
    native_load_input_prefix();
    /* ACE_COMMAND_ARGUMENTS supplies the command line that AmigaDOS
       ReadArgs() expects to find on a child CLI's cooked Input() stream.
       A raw terminal program such as unchanged Vim must see the real
       console bytes immediately; otherwise the synthetic empty argument
       line's newline is consumed as Vim's first terminal response byte. */
    if (file == stdin && !native_input_raw && native_input_prefix_position <
        native_input_prefix_length)
        return (unsigned char)native_input_prefix[
            native_input_prefix_position++];
    if (file == stdin && !native_input_raw)
        return native_editor_next_char(file);
    return fgetc(file);
}

static void native_refresh_local_vars(void)
{
    char listing[AMIGA_BROKER_MAX_PAYLOAD];
    char *line;
    size_t count = 0;

    NEWLIST(&native_process.pr_LocalVars);
    memset(native_local_vars, 0, sizeof(native_local_vars));
    memset(native_local_var_names, 0, sizeof(native_local_var_names));
    memset(native_local_var_values, 0, sizeof(native_local_var_values));
    if (native_broker_listvars(AMIGA_BROKER_VAR_LOCAL |
                               AMIGA_BROKER_VAR_ANY,
                               listing, sizeof(listing)) != 0)
        return;

    line = listing;
    while (*line && count < NATIVE_LOCAL_VAR_LIMIT) {
        char *tab = strchr(line, '\t');
        char *end = strchr(line, '\n');
        size_t name_length;
        struct LocalVar *variable;

        if (!tab || !end || tab > end)
            break;
        name_length = (size_t)(end - tab - 1);
        if (name_length == 0 || name_length >= NATIVE_LOCAL_VAR_NAME) {
            line = end + 1;
            continue;
        }
        memcpy(native_local_var_names[count], tab + 1, name_length);
        native_local_var_names[count][name_length] = '\0';
        variable = &native_local_vars[count++];
        variable->lv_Node.ln_Type = (line[0] == '1') ? LV_ALIAS : LV_VAR;
        variable->lv_Node.ln_Name = native_local_var_names[count - 1];
        if (native_broker_getvar(variable->lv_Node.ln_Name,
                                 AMIGA_BROKER_VAR_LOCAL |
                                 (variable->lv_Node.ln_Type == LV_ALIAS ?
                                  AMIGA_BROKER_VAR_ALIAS :
                                  AMIGA_BROKER_VAR_VARIABLE),
                                 native_local_var_values[count - 1],
                                 sizeof(native_local_var_values[count - 1])) == 0) {
            variable->lv_Value = (UBYTE *)native_local_var_values[count - 1];
            variable->lv_Len = strlen((char *)variable->lv_Value);
        }
        variable->lv_Node.ln_Succ = (struct Node *)&native_process.pr_LocalVars.lh_Tail;
        variable->lv_Node.ln_Pred = native_process.pr_LocalVars.lh_TailPred;
        native_process.pr_LocalVars.lh_TailPred->ln_Succ = &variable->lv_Node;
        native_process.pr_LocalVars.lh_TailPred = &variable->lv_Node;
        line = end + 1;
    }
}

struct native_lock {
    /* Keep the public prefix compatible with AROS's FileLock.  The imported
       GetDeviceProc() code reads fl_Task when it advances a multi-assign;
       the host bridge uses the lock pointer itself as its handler marker. */
    BPTR fl_Link;
    IPTR fl_Key;
    LONG fl_Access;
    struct MsgPort *fl_Task;
    BPTR fl_Volume;
    char path[PATH_MAX];
    /* Opened by Examine() when the lock is a directory; consumed by
       ExNext(). Real AmigaDOS ties this scan position to the filesystem
       handler process behind the lock (fl_Task) -- ACE's Lock() has no
       handler process at all (see the OpenDevice("console.device",...)-
       style comment on Examine() below), so the scan lives directly on the
       lock instead. */
    DIR *scan;
    /* ExAll() uses the FileInfoBlock disk key as a progress marker between
       calls.  The host scan itself stays open across those calls, so this
       only needs to be a non-zero, monotonically increasing token. */
    IPTR scan_key;
};

/* CurrentDir() in AmigaDOS only swaps the process's lock pointer; it does not
   manufacture a new lock for every query.  Keep the initial process lock
   separately from caller-owned Lock()/DupLock() results.  This matters to
   the real AROS MatchNext() implementation, which saves and restores the
   current directory on every iteration without freeing either returned
   value. */
static struct native_lock native_initial_dir;
static BPTR native_current_dir;

static void native_init_lock(struct native_lock *lock, const char *path,
                              LONG access)
{
    memset(lock, 0, sizeof(*lock));
    snprintf(lock->path, sizeof(lock->path), "%s", path);
    lock->fl_Access = access;
    lock->fl_Task = (struct MsgPort *)lock;
}

/* Commands run as separate host processes, but ACE deliberately gives them
   one broker session so an Amiga command such as CD can update the shell's
   directory.  The shell process therefore has to refresh its cached native
   lock before AROS code uses it.  Without this, Shell.c's command loader
   restores the shell's pre-CD lock after every command and silently undoes
   the child's successful CurrentDir(). */
static int native_sync_current_dir(void)
{
    char current[PATH_MAX];
    struct native_lock *current_lock = native_current_dir;

    if (native_broker_getcwd(current, sizeof(current)) != 0)
        return -1;
    if (!native_current_dir) {
        native_init_lock(&native_initial_dir, current, SHARED_LOCK);
        native_current_dir = &native_initial_dir;
        return 0;
    }
    if (strcmp(current_lock->path, current) != 0) {
        if (current_lock->scan) {
            closedir(current_lock->scan);
            current_lock->scan = NULL;
        }
        current_lock->scan_key = 0;
        snprintf(current_lock->path, sizeof(current_lock->path),
                 "%s", current);
    }
    return 0;
}

/* Unix epoch (1970-01-01) to Amiga epoch (1978-01-01): 2922 days, including
   the two leap years (1972, 1976) in between. Amiga DateStamp cannot
   represent an earlier date, so those clamp to the epoch itself. */
#define NATIVE_AMIGA_EPOCH_DAYS 2922

static void native_datestamp_from_unix(time_t when, struct DateStamp *stamp)
{
    long long days = (long long)when / 86400 - NATIVE_AMIGA_EPOCH_DAYS;
    long long seconds_of_day = (long long)when % 86400;

    if (seconds_of_day < 0) {
        seconds_of_day += 86400;
        days--;
    }
    if (days < 0) {
        days = 0;
        seconds_of_day = 0;
    }
    stamp->ds_Days = (LONG)days;
    stamp->ds_Minute = (LONG)(seconds_of_day / 60);
    stamp->ds_Tick = (LONG)((seconds_of_day % 60) * 50);
}

/* FIBF_{READ,WRITE,EXECUTE,DELETE} are historically inverted: the bit is
   SET to mean the permission is DENIED. A Unix file that is owner-writable
   and owner-executable therefore maps to protection 0 (everything
   permitted); denied permissions set their bit. */
static LONG native_protection_from_stat(const struct stat *information)
{
    LONG protection = 0;

    if (!(information->st_mode & S_IRUSR))
        protection |= FIBF_READ;
    if (!(information->st_mode & S_IWUSR))
        protection |= FIBF_WRITE | FIBF_DELETE;
    if (!(information->st_mode & S_IXUSR))
        protection |= FIBF_EXECUTE;
    return protection;
}

/* The inverse, for SetProtection(). AmigaDOS's delete bit has no separate
   Unix permission -- on Unix it is the containing directory that governs
   removal -- so it shares the owner write bit with FIBF_WRITE, which is the
   pairing the read direction above already fixed. Either bit therefore
   withdraws write permission, and Delete FORCE (which clears the whole
   protection mask before unlinking) restores it. Bits ACE does not model,
   including the archive/pure/script trio, are left alone rather than
   guessed at. */
static mode_t native_mode_from_protection(mode_t mode, LONG protection)
{
    mode = (protection & FIBF_READ) ? (mode & ~(mode_t)S_IRUSR) :
                                      (mode | S_IRUSR);
    mode = (protection & (FIBF_WRITE | FIBF_DELETE)) ?
           (mode & ~(mode_t)S_IWUSR) : (mode | S_IWUSR);
    mode = (protection & FIBF_EXECUTE) ? (mode & ~(mode_t)S_IXUSR) :
                                         (mode | S_IXUSR);
    return mode;
}

/* An AmigaDOS file comment has no Unix permission or timestamp to live in,
   so it is kept in an extended attribute on the file itself. That puts it on
   the inode, which is what makes it survive a rename or a move within a
   filesystem without ACE doing anything.

   The name is deliberately generic, and neither half of that is an
   oversight. It is not ACE's, because a comment on a file is not an ACE
   concept -- ACE is only what happens to be writing this one, and anything
   else that wants to read or write one should not have to know that. It
   does not carry a vendor's name either, because unlike a source filename
   an extended attribute propagates: onto the user's own files, and through
   every backup and copy that preserves xattrs. Everything ACE puts on disk
   is named for ACE or for nothing -- ace.conf, ace-broker.sock -- and this
   is the "for nothing" case.

   The cost of a shared name is that a foreign writer is not bound by
   AmigaDOS's 79-character limit, which is why the read path truncates
   instead of trusting what it finds. */
#define NATIVE_COMMENT_ATTRIBUTE "user.comment"

/* Reads a file comment into a fib, which was zeroed by the caller: no
   attribute, an empty one, or a filesystem with no extended attributes at
   all all leave the fib's empty string in place, since none of those is an
   error in an object that simply has no comment. */
static void native_fill_fib_comment(const char *path,
                                    struct FileInfoBlock *fib)
{
    char stored[1024];
    ssize_t size = getxattr(path, NATIVE_COMMENT_ATTRIBUTE, stored,
                            sizeof(stored));

    if (size <= 0)
        return;
    if ((size_t)size >= sizeof(fib->fib_Comment))
        size = (ssize_t)sizeof(fib->fib_Comment) - 1;
    memcpy(fib->fib_Comment, stored, (size_t)size);
    fib->fib_Comment[size] = '\0';
}

/* Fills fib for a resolved host path already known to exist, shared by
   Examine() (the locked object itself) and ExNext() (its next directory
   child). name overrides the basename ACE would otherwise take from path,
   since a directory child's fib_FileName is the entry's own name, not the
   parent directory's. */
static int native_fill_fib(const char *path, const char *name,
                           struct FileInfoBlock *fib)
{
    struct stat information;
    const char *fib_name = name;
    char mapped_path[PATH_MAX];

    if (stat(path, &information) != 0) {
        native_ioerr = errno;
        return -1;
    }
    if (native_name_needs_mapping(name)) {
        const char *last;

        if (native_broker_name_from_host(path, mapped_path,
                                         sizeof(mapped_path)) != 0) {
            native_ioerr = errno;
            return -1;
        }
        last = strrchr(mapped_path, '/');
        fib_name = last ? last + 1 : mapped_path;
    }
    memset(fib, 0, sizeof(*fib));
    fib->fib_DirEntryType = S_ISDIR(information.st_mode) ? ST_USERDIR : ST_FILE;
    fib->fib_EntryType = fib->fib_DirEntryType;
    if (strlen(fib_name) >= sizeof(fib->fib_FileName)) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return -1;
    }
    strcpy((char *)fib->fib_FileName, fib_name);
    fib->fib_Protection = native_protection_from_stat(&information);
    fib->fib_Size = (LONG)information.st_size;
    fib->fib_NumBlocks = (LONG)information.st_blocks;
    native_datestamp_from_unix(information.st_mtime, &fib->fib_Date);
    native_fill_fib_comment(path, fib);
    /* A successful DOS lookup replaces any error left by an earlier
       operation.  Shell.c uses IoErr() after its failed command lookup when
       it falls back to treating the command text as a directory name. */
    native_ioerr = 0;
    return 0;
}

struct native_console_handle {
    uint64_t magic;
    char specification[PATH_MAX];
};

#define NATIVE_CONSOLE_MAGIC UINT64_C(0x414345434f4e3031)

BPTR native_console_open(const char *specification)
{
    struct native_console_handle *handle;

    handle = calloc(1, sizeof(*handle));
    if (!handle) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return BNULL;
    }
    handle->magic = NATIVE_CONSOLE_MAGIC;
    snprintf(handle->specification, sizeof(handle->specification), "%s",
             specification ? specification : "CON:");
    return handle;
}

int native_console_is_handle(BPTR handle)
{
    struct native_console_handle *candidate = handle;

    return candidate && candidate->magic == NATIVE_CONSOLE_MAGIC;
}

const char *native_console_specification(BPTR handle)
{
    struct native_console_handle *console = handle;

    return native_console_is_handle(handle) ? console->specification : NULL;
}

void native_console_close(BPTR handle)
{
    if (native_console_is_handle(handle))
        free(handle);
}

static FILE *as_file(BPTR handle)
{
    native_init_stdio_handles();
    if (handle == (BPTR)&native_stdin_handle.amiga)
        return native_stdin_handle.stream;
    if (handle == (BPTR)&native_stdout_handle.amiga)
        return native_stdout_handle.stream;
    if (handle == (BPTR)&native_stderr_handle.amiga)
        return native_stderr_handle.stream;
    return (FILE *)handle;
}

struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    (void)version;
    if (name && strcasecmp(name, "dos.library") == 0) {
        native_dos_base.dl_Root = &native_root_node;
        DOSBase = &native_dos_base;
        return (struct Library *)&native_dos_base;
    }
    if (name && strcasecmp(name, "utility.library") == 0)
        return (struct Library *)&native_utility_base;
    return NULL;
}

APTR FindTask(CONST_STRPTR name)
{
    (void)name;
    SysBase = &native_exec_base;
    native_refresh_local_vars();
    return &native_process;
}

struct LocalVar *FindVar(CONST_STRPTR name, LONG type)
{
    struct Node *node;

    if (!name)
        return NULL;
    native_refresh_local_vars();
    ForeachNode(&native_process.pr_LocalVars, node) {
        struct LocalVar *variable = (struct LocalVar *)node;
        if (strcasecmp(node->ln_Name, name) == 0 &&
            (type == 0 || variable->lv_Node.ln_Type == type))
            return variable;
    }
    return NULL;
}

void CloseLibrary(struct Library *library)
{
    (void)library;
}

/*
 * AROS's own findarg.c and strtolong.c are already compiled into the DOS
 * runtime, under renamed symbols so that readargs.c reaches them without
 * ACE having to publish them. If and Else are the first callers from
 * outside, and they call them by their real names, which is what these are.
 */
LONG ace_aros_FindArg(CONST_STRPTR keywords, CONST_STRPTR argument);
LONG ace_aros_StrToLong(CONST_STRPTR string, LONG *value);

LONG FindArg(CONST_STRPTR keywords, CONST_STRPTR argument)
{
    return ace_aros_FindArg(keywords, argument);
}

LONG StrToLong(CONST_STRPTR string, LONG *value)
{
    return ace_aros_StrToLong(string, value);
}

LONG Stricmp(CONST_STRPTR left, CONST_STRPTR right)
{
    return strcasecmp(left, right);
}

LONG Strnicmp(CONST_STRPTR left, CONST_STRPTR right, LONG length)
{
    return strncasecmp(left, right, (size_t)length);
}

LONG SplitName(CONST_STRPTR path, LONG separator, STRPTR buffer,
               LONG buffer_position, LONG buffer_size)
{
    const char *end;
    size_t length;

    if (!path || !buffer || buffer_position < 0 || buffer_size <= 0)
        return 0;
    end = strchr(path, (int)separator);
    if (!end)
        return 0;
    length = (size_t)(end - path);
    if (length > (size_t)buffer_size - 1)
        length = (size_t)buffer_size - 1;
    memcpy(buffer + buffer_position, path, length);
    buffer[buffer_position + length] = '\0';
    return (LONG)(length + 1);
}

BOOL ErrorReport(LONG error, ULONG type, IPTR object, APTR requester)
{
    (void)type;
    (void)object;
    (void)requester;
    SetIoErr(error);
    /*
     * The return value is which button the user chose, not whether anything
     * was reported: DOSFALSE is "Retry", DOSTRUE is "Cancel". AmigaDOS also
     * specifies DOSTRUE for an error that could not be reported at all,
     * which is permanently ACE's case -- there is no requester at this
     * layer.
     *
     * Every caller of this in rom/dos is a retry loop that only exits on
     * DOSTRUE, so answering "Retry" to an error nothing will ever fix spins
     * forever. getdeviceproc.c's `do { ... } while (dl == NULL)` is the one
     * a bad device name reaches -- "CD A:" burned a core there -- and
     * internalseek.c, startnotify.c and getdeviceproc.c's volume path hang
     * the same way.
     */
    return DOSTRUE;
}

UBYTE ToUpper(ULONG character)
{
    return (UBYTE)toupper((int)character);
}

APTR AllocVec(ULONG size, ULONG flags)
{
    (void)flags;
    return calloc(1, size);
}

APTR AllocMem(ULONG size, ULONG flags)
{
    return (flags & 1u) ? calloc(1, size) : malloc(size);
}

void FreeMem(APTR memory, ULONG size)
{
    (void)size;
    free(memory);
}

ULONG AvailMem(ULONG flags)
{
    (void)flags;
    return (ULONG)-1;
}

void FreeVec(APTR memory)
{
    free(memory);
}

void CopyMemQuick(CONST_APTR source, APTR destination, ULONG length)
{
    memmove(destination, source, length);
}

void CopyMem(CONST_APTR source, APTR destination, ULONG length)
{
    memmove(destination, source, length);
}

STRPTR FilePart(CONST_STRPTR path)
{
    const char *cursor;

    if (!path)
        return NULL;
    if (!*path)
        return (STRPTR)path;

    cursor = path + strlen(path) - 1;
    while (*cursor != ':' && *cursor != '/' && cursor != path)
        cursor--;
    if (*cursor == ':' || *cursor == '/')
        cursor++;
    return (STRPTR)cursor;
}

STRPTR PathPart(CONST_STRPTR path)
{
    const char *cursor;

    if (!path)
        return NULL;
    while (*path == '/')
        path++;
    cursor = path;
    while (*cursor) {
        if (*cursor == '/')
            path = cursor;
        else if (*cursor == ':')
            path = cursor + 1;
        cursor++;
    }
    return (STRPTR)path;
}

BOOL AddPart(STRPTR dirname, CONST_STRPTR filename, ULONG size)
{
    char *position;
    size_t dirname_length;
    size_t filename_length;
    int add_slash = 0;

    if (!dirname || !filename || size == 0) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }

    position = strchr(filename, ':');
    if (position) {
        if (position == filename) {
            position = strchr(dirname, ':');
            if (!position)
                position = dirname;
        } else {
            position = dirname;
        }
    } else {
        dirname_length = strlen(dirname);
        position = dirname + dirname_length;
        if (dirname_length > 0 && position[-1] != ':' && position[-1] != '/')
            add_slash = 1;
    }

    dirname_length = (size_t)(position - dirname);
    filename_length = strlen(filename);
    if (dirname_length + filename_length + 1 + (size_t)add_slash > size) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }
    if (add_slash)
        *position++ = '/';
    strcpy(position, filename);
    return DOSTRUE;
}

STRPTR stccpy(STRPTR destination, CONST_STRPTR source, LONG length)
{
    size_t source_length;
    size_t copied;

    if (!destination || !source || length <= 0)
        return destination;
    source_length = strlen(source);
    copied = source_length < (size_t)length - 1 ? source_length :
             (size_t)length - 1;
    memcpy(destination, source, copied);
    destination[copied] = '\0';
    return destination;
}

BPTR AllocDosObject(LONG type, APTR tags)
{
    (void)tags;
    if (type == DOS_FIB)
        return calloc(1, sizeof(struct FileInfoBlock));
    if (type == DOS_RDARGS)
        return calloc(1, sizeof(struct RDArgs));
    if (type == DOS_EXALLCONTROL)
        return calloc(1, sizeof(struct InternalExAllControl));
    return NULL;
}

static void native_free_rdargs_values(struct RDArgs *arguments)
{
    if (!arguments)
        return;
    for (ULONG i = 0; i < arguments->RDA_ArgumentCount && i < 32; i++) {
        if (arguments->RDA_Multiple[i] && arguments->RDA_Values[i]) {
            char **values = arguments->RDA_Values[i];
            for (size_t j = 0; values[j]; j++)
                free(values[j]);
        }
        free(arguments->RDA_Values[i]);
        arguments->RDA_Values[i] = NULL;
        arguments->RDA_Multiple[i] = 0;
    }
    if (arguments->RDA_Arguments) {
        for (ULONG i = 0; i < arguments->RDA_ArgumentCount; i++)
            arguments->RDA_Arguments[i] = 0;
    }
    arguments->RDA_ArgumentCount = 0;
}

void FreeDosObject(LONG type, APTR object)
{
    struct RDArgs *arguments = object;

    if (type == DOS_RDARGS)
        native_free_rdargs_values(arguments);
    if (type == DOS_EXALLCONTROL && object) {
        /* exall.c's real ExAll() emulation allocates this internally (see
           compat/include/dos/exall.h) the first time it is called on a
           lock, and documents it as freed by FreeDosObject(). */
        struct InternalExAllControl *control = object;

        free(control->fib);
    }
    free(object);
}

static int native_named_device_path(CONST_STRPTR name)
{
    const char *colon;

    if (!name || strncasecmp(name, "PROGDIR:", 8) == 0 ||
        strncasecmp(name, "NIL:", 4) == 0)
        return 0;
    colon = strchr(name, ':');
    return colon && colon != name;
}

static int native_device_base(struct DevProc *device, char *result,
                              size_t result_size)
{
    struct native_lock *lock = device ? device->dvp_Lock : NULL;

    if (!lock && device && device->dvp_DevNode)
        lock = device->dvp_DevNode->dol_Lock;
    if (!lock)
        return -1;
    if (snprintf(result, result_size, "%s", lock->path) >=
        (int)result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int native_device_relative(CONST_STRPTR name, char *result,
                                  size_t result_size)
{
    const char *colon = strchr(name, ':');
    const char *cursor = colon + 1;

    /* Keep AmigaDOS's leading slashes intact. ACE once rewrote each slash as
       "../" before asking the broker to resolve it, which required the
       resolver to invent Linux-style dot syntax for every caller. */
    if (strlen(cursor) >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(result, cursor);
    return 0;
}

static int native_union_candidate(CONST_STRPTR name, struct DevProc *device,
                                  char *result, size_t result_size)
{
    char base[PATH_MAX];
    char relative[PATH_MAX * 2];

    if (native_device_base(device, base, sizeof(base)) != 0 ||
        native_device_relative(name, relative, sizeof(relative)) != 0)
        return -1;
    {
        return native_broker_resolve_beneath(base, relative, result,
                                             result_size);
    }
}

static int native_union_existing(CONST_STRPTR name, char *result,
                                 size_t result_size)
{
    struct DevProc *device = ace_aros_GetDeviceProc(name, NULL);
    /*
     * A NULL on the very first call means no handler claims this name at
     * all -- an unmounted device, or an assign that was never made -- and
     * GetDeviceProc() has already recorded the specific reason, normally
     * ERROR_DEVICE_NOT_MOUNTED. Keep it. The generic not-found below is
     * only right once a handler does exist and it is the object within it
     * that is missing, which is the distinction "CD A:" and "CD DH0:junk"
     * turn on.
     */
    int saved_error = device ? ERROR_OBJECT_NOT_FOUND : (int)IoErr();

    while (device) {
        struct stat information;

        if (native_union_candidate(name, device, result, result_size) == 0) {
            if (stat(result, &information) == 0) {
                ace_aros_FreeDeviceProc(device);
                return 0;
            }
            saved_error = errno;
            if (saved_error != ENOENT && saved_error != ENOTDIR) {
                ace_aros_FreeDeviceProc(device);
                errno = saved_error;
                return -1;
            }
        } else {
            saved_error = errno;
        }
        device = ace_aros_GetDeviceProc(name, device);
    }
    errno = saved_error;
    return -1;
}

BPTR Lock(CONST_STRPTR name, LONG mode)
{
    char resolved[PATH_MAX];
    struct stat information;
    struct native_lock *lock;

    if ((native_named_device_path(name) ?
         native_union_existing(name, resolved, sizeof(resolved)) :
         native_broker_resolve_path(name, resolved, sizeof(resolved))) != 0 ||
        stat(resolved, &information) != 0) {
        if (native_named_device_path(name) &&
            (errno == ENOENT || errno == ENOTDIR))
            native_ioerr = ERROR_OBJECT_NOT_FOUND;
        else
            set_native_broker_error();
        return NULL;
    }
    lock = calloc(1, sizeof(*lock));
    if (!lock) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    native_init_lock(lock, resolved, mode);
    /* Match AmigaDOS Lock(): a successful locate has no pending error. */
    native_ioerr = 0;
    return lock;
}

/* The broker stores assignment targets as canonical host paths.  DOS Lock()
   deliberately interprets leading '/' with Amiga parent-directory meaning,
   so the DosList compatibility layer needs this explicit host-path seam when
   it materializes a broker assignment for Assign LIST/EXISTS. */
BPTR native_lock_host_path(const char *path)
{
    struct stat information;
    struct native_lock *lock;

    if (!path || stat(path, &information) != 0) {
        native_ioerr = errno;
        return NULL;
    }
    lock = calloc(1, sizeof(*lock));
    if (!lock) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    native_init_lock(lock, path, SHARED_LOCK);
    native_ioerr = 0;
    return lock;
}

LONG UnLock(BPTR handle)
{
    struct native_lock *lock = handle;

    /* The initial current-directory lock is process state, not a heap lock
       returned by Lock().  Commands such as AROS CD quite reasonably call
       UnLock() on the old value returned by CurrentDir(). */
    if (lock == &native_initial_dir)
        return DOSTRUE;
    if (lock && lock->scan)
        closedir(lock->scan);
    free(handle);
    return DOSTRUE;
}

BPTR CreateDir(CONST_STRPTR name)
{
    char resolved[PATH_MAX];
    struct native_lock *lock;

    if (!name || native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0) {
        native_ioerr = ERROR_OBJECT_NOT_FOUND;
        return BNULL;
    }
    if (mkdir(resolved, 0777) != 0) {
        if (errno == EEXIST)
            native_ioerr = ERROR_OBJECT_EXISTS;
        else
            set_native_broker_error();
        return BNULL;
    }
    lock = calloc(1, sizeof(*lock));
    if (!lock) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return BNULL;
    }
    native_init_lock(lock, resolved, EXCLUSIVE_LOCK);
    return lock;
}

LONG ChangeMode(LONG type, BPTR object, LONG mode)
{
    (void)type;
    (void)object;
    (void)mode;
    return DOSTRUE;
}

LONG Examine(BPTR handle, struct FileInfoBlock *fib)
{
    struct native_lock *lock = handle;
    const char *name;

    if (!lock || !fib) {
        native_ioerr = ERROR_INVALID_COMPONENT_NAME;
        return DOSFALSE;
    }
    name = strrchr(lock->path, '/');
    name = name ? name + 1 : lock->path;
    if (native_fill_fib(lock->path, name, fib) != 0)
        return DOSFALSE;

    /* A real Examine() on a directory lock (re)starts the scan ExNext()
       continues; AROS's own ExAll() relies on exactly this to restart
       enumeration by setting eac_LastKey back to 0 and calling Examine()
       again. */
    if (lock->scan)
        closedir(lock->scan);
    lock->scan = fib->fib_DirEntryType > 0 ? opendir(lock->path) : NULL;
    lock->scan_key = 0;
    return DOSTRUE;
}

LONG ExNext(BPTR handle, struct FileInfoBlock *fib)
{
    struct native_lock *lock = handle;
    struct dirent *entry;
    char child[PATH_MAX];

    if (!lock || !fib || !lock->scan) {
        native_ioerr = ERROR_NO_MORE_ENTRIES;
        return DOSFALSE;
    }
    for (;;) {
        entry = readdir(lock->scan);
        if (!entry) {
            closedir(lock->scan);
            lock->scan = NULL;
            native_ioerr = ERROR_NO_MORE_ENTRIES;
            return DOSFALSE;
        }
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            break;
    }
    if (snprintf(child, sizeof(child), "%s/%s", lock->path, entry->d_name) >=
        (int)sizeof(child)) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }
    if (native_fill_fib(child, entry->d_name, fib) != 0)
        return DOSFALSE;
    fib->fib_DiskKey = ++lock->scan_key;
    return DOSTRUE;
}

BPTR DupLock(BPTR handle)
{
    struct native_lock *lock = handle;
    struct native_lock *copy;

    if (!lock)
        return BNULL;
    copy = malloc(sizeof(*copy));
    if (!copy) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return BNULL;
    }
    memcpy(copy, lock, sizeof(*copy));
    copy->fl_Task = (struct MsgPort *)copy;
    copy->fl_Link = NULL;
    copy->fl_Volume = lock->fl_Volume;
    copy->scan = NULL;
    copy->scan_key = 0;
    return copy;
}

BPTR CurrentDir(BPTR handle)
{
    BPTR old;

    if (native_sync_current_dir() != 0) {
        native_ioerr = errno;
        return NULL;
    }

    old = native_current_dir;
    if (handle) {
        struct native_lock *new_dir = handle;
        if (native_broker_setcwd(new_dir->path) != 0) {
            native_ioerr = errno;
            return NULL;
        }
        native_current_dir = handle;
    }
    native_ioerr = 0;
    return old;
}

LONG NameFromLock(BPTR handle, STRPTR buffer, LONG length)
{
    struct native_lock *lock = handle;
    char amiga_name[PATH_MAX];

    if (!lock || !buffer || length <= 0) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }
    if (native_broker_name_from_host(lock->path, amiga_name,
                                     sizeof(amiga_name)) != 0) {
        native_ioerr = errno;
        return DOSFALSE;
    }
    if (strlen(amiga_name) >= (size_t)length) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }
    strcpy(buffer, amiga_name);
    return DOSTRUE;
}

void SetCurrentDirName(CONST_STRPTR name)
{
    if (!name)
        name = "";
    snprintf(native_cli_set_name, sizeof(native_cli_set_name), "%s", name);
    native_cli.cli_SetName = native_cli_set_name;
}

static FILE *selected_input(void)
{
    native_init_stdio_handles();
    return native_input ? native_input : stdin;
}

static FILE *selected_output(void)
{
    native_init_stdio_handles();
    return native_output ? native_output : stdout;
}

static BPTR handle_for_file(FILE *file)
{
    native_init_stdio_handles();
    if (file == stdin)
        return (BPTR)&native_stdin_handle.amiga;
    if (file == stdout)
        return (BPTR)&native_stdout_handle.amiga;
    if (file == stderr)
        return (BPTR)&native_stderr_handle.amiga;
    return (BPTR)file;
}

BPTR Output(void)
{
    return handle_for_file(selected_output());
}

BPTR Input(void)
{
    return handle_for_file(selected_input());
}

BPTR Open(CONST_STRPTR name, LONG mode)
{
    const char *access = mode == MODE_NEWFILE ? "wb" :
                         mode == MODE_READWRITE ? "r+b" : "rb";
    char resolved[PATH_MAX];
    FILE *file;
    /* Why the open failed, kept from the moment it failed. Everything between
       here and the report -- freeing a DevProc, asking the broker for the
       next target in a multi-assign -- is entitled to leave errno holding
       something else, and errno is only meaningful immediately after the call
       that set it. */
    int open_errno = 0;

    if (name && strncasecmp(name, "CON:", 4) == 0)
        return native_console_open(name);

    if (native_named_device_path(name)) {
        struct DevProc *device = ace_aros_GetDeviceProc(name, NULL);

        file = NULL;
        while (device) {
            if (native_union_candidate(name, device, resolved,
                                       sizeof(resolved)) == 0) {
                errno = 0;
                file = fopen(resolved, access);
                open_errno = errno;
            }
            if (file || mode == MODE_NEWFILE ||
                (file == NULL && open_errno != ENOENT &&
                 open_errno != ENOTDIR))
                break;
            device = ace_aros_GetDeviceProc(name, device);
        }
        if (device)
            ace_aros_FreeDeviceProc(device);
        if (file)
            native_ioerr = 0;
        else
            native_ioerr = open_errno == ENOENT ? ERROR_OBJECT_NOT_FOUND :
                           open_errno;
    } else if (native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0) {
        native_ioerr = errno == ENOENT ? ERROR_OBJECT_NOT_FOUND : errno;
        return NULL;
    } else {
        errno = 0;
        file = fopen(resolved, access);
        open_errno = errno;
    }
    /* A bare name that is not in the current directory used to be looked up
       as a command here, which is where ACE kept its stand-in for C: before
       it had one. That belongs in the loader, not in Open(): AROS's
       loadCommand() already searches the current directory, then the path
       list, then the C: multiassign, and doing it here as well meant any
       failed open of a data file quietly found a command of that name
       instead of reporting that the file was missing. */
    /*
     * A directory is not a file to be opened. Linux will open one read-only
     * quite happily, but AmigaDOS answers ERROR_OBJECT_WRONG_TYPE, and
     * something is listening for that: the Shell tries to open a typed word
     * as a command, and when the open fails with wrong-type, not-found or
     * invalid-component it locks the name instead and changes directory to
     * it if it is one. That is how typing "C:" at an Amiga prompt moves you
     * there. With the open succeeding, the Shell got a handle on a
     * directory, believed it had found a command, and tried to run it.
     */
    if (file) {
        struct stat information;
        int descriptor = fileno(file);

        if (descriptor >= 0 && fstat(descriptor, &information) == 0 &&
            S_ISDIR(information.st_mode)) {
            fclose(file);
            native_ioerr = ERROR_OBJECT_WRONG_TYPE;
            return NULL;
        }
    }
    if (!file && !native_named_device_path(name))
        native_ioerr = open_errno == ENOENT ? ERROR_OBJECT_NOT_FOUND :
                       open_errno;
    else if (file)
        native_ioerr = 0;
    return (BPTR)file;
}

LONG Close(BPTR handle)
{
    native_init_stdio_handles();
    if (handle == (BPTR)&native_stdin_handle.amiga ||
        handle == (BPTR)&native_stdout_handle.amiga ||
        handle == (BPTR)&native_stderr_handle.amiga)
        return DOSTRUE;
    if (native_console_is_handle(handle)) {
        native_console_close(handle);
        return DOSTRUE;
    }
    if (!handle || fclose(as_file(handle)) != 0) {
        native_ioerr = errno;
        return DOSFALSE;
    }
    return DOSTRUE;
}

LONG DeleteFile(CONST_STRPTR name)
{
    char resolved[PATH_MAX];

    if (!name ||
        native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    /* One AmigaDOS call removes either kind of object, where Unix splits
       them. A directory reaches unlink() as EISDIR on Linux (POSIX also
       allows EPERM), and an empty one is then rmdir()'s job; a directory
       that still has children stays refused, which is what makes Delete
       recurse into it before trying again. */
    if (unlink(resolved) == 0)
        return DOSTRUE;
    if ((errno == EISDIR || errno == EPERM) && rmdir(resolved) == 0)
        return DOSTRUE;
    set_native_broker_error();
    /* Refused for want of permission, which for a removal is what AmigaDOS
       calls delete protection -- the state Delete FORCE exists to clear.
       Only this operation can read those two errnos that way. */
    if (errno == EACCES || errno == EPERM)
        native_ioerr = ERROR_DELETE_PROTECTED;
    return DOSFALSE;
}

LONG SetComment(CONST_STRPTR name, CONST_STRPTR comment)
{
    char resolved[PATH_MAX];
    size_t length = comment ? strlen(comment) : 0;

    if (!name ||
        native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    /* Refused rather than truncated, as on AmigaDOS: a FileInfoBlock cannot
       carry more than this back, so a longer comment would be stored and
       then never readable in full by anything that asked for it. */
    if (length >= sizeof(((struct FileInfoBlock *)0)->fib_Comment)) {
        native_ioerr = ERROR_COMMENT_TOO_BIG;
        return DOSFALSE;
    }
    /* An empty comment is how AmigaDOS clears one, so remove the attribute
       rather than leaving an empty one behind. Nothing to remove is the
       state the caller asked for, not a failure. */
    if (length == 0) {
        if (removexattr(resolved, NATIVE_COMMENT_ATTRIBUTE) == 0 ||
            errno == ENODATA)
            return DOSTRUE;
    } else if (setxattr(resolved, NATIVE_COMMENT_ATTRIBUTE, comment, length,
                        0) == 0) {
        return DOSTRUE;
    }
    set_native_broker_error();
    return DOSFALSE;
}

LONG SetProtection(CONST_STRPTR name, ULONG protection)
{
    char resolved[PATH_MAX];
    struct stat information;

    if (!name ||
        native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0 ||
        stat(resolved, &information) != 0 ||
        chmod(resolved, native_mode_from_protection(information.st_mode,
                                                    (LONG)protection)) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    return DOSTRUE;
}

LONG Rename(CONST_STRPTR old_name, CONST_STRPTR new_name)
{
    char old_path[PATH_MAX];
    char new_path[PATH_MAX];

    if (!old_name || !new_name) {
        native_ioerr = ERROR_REQUIRED_ARG_MISSING;
        return DOSFALSE;
    }

    /* AROS's Rename() selects the filesystem for the source through
       GetDeviceProc(), which matters for multi-assigns: the object may live
       in a later AssignList target. The destination is resolved through the
       broker so component mappings remain directory-specific. */
    if ((native_named_device_path(old_name) ?
         native_union_existing(old_name, old_path, sizeof(old_path)) :
         native_broker_resolve_path(old_name, old_path, sizeof(old_path))) != 0 ||
        native_broker_resolve_path(new_name, new_path, sizeof(new_path)) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    if (rename(old_path, new_path) != 0) {
        native_ioerr = errno == EXDEV ? ERROR_RENAME_ACROSS_DEVICES : errno;
        if (errno == ENOENT)
            native_ioerr = ERROR_OBJECT_NOT_FOUND;
        else if (errno == EEXIST)
            native_ioerr = ERROR_OBJECT_EXISTS;
        return DOSFALSE;
    }
    native_ioerr = 0;
    return DOSTRUE;
}

LONG SameLock(BPTR lock1, BPTR lock2)
{
    struct native_lock *first = lock1;
    struct native_lock *second = lock2;
    struct stat first_info;
    struct stat second_info;

    if (!first || !second)
        return LOCK_DIFFERENT;
    if (strcmp(first->path, second->path) == 0)
        return LOCK_SAME;
    if (stat(first->path, &first_info) == 0 &&
        stat(second->path, &second_info) == 0 &&
        first_info.st_dev == second_info.st_dev)
        return LOCK_SAME_VOLUME;
    return LOCK_DIFFERENT;
}

BOOL IsInteractive(BPTR handle)
{
    native_init_stdio_handles();
    if (handle != (BPTR)stdin && handle != (BPTR)stdout &&
        handle != (BPTR)stderr &&
        handle != (BPTR)&native_stdin_handle.amiga &&
        handle != (BPTR)&native_stdout_handle.amiga &&
        handle != (BPTR)&native_stderr_handle.amiga)
        return DOSFALSE;
    return native_interactive ? DOSTRUE : DOSFALSE;
}

void native_set_interactive(int interactive)
{
    native_interactive = interactive;
}

LONG SetMode(BPTR handle, LONG mode)
{
    native_init_stdio_handles();
    if (handle != (BPTR)&native_stdin_handle.amiga && handle != (BPTR)stdin) {
        native_ioerr = ERROR_ACTION_NOT_KNOWN;
        return DOSFALSE;
    }
    native_input_raw = mode != 0;
    return DOSTRUE;
}

LONG WaitForChar(BPTR handle, LONG timeout)
{
    FILE *file;
    fd_set readable;
    struct timeval interval;
    struct timeval *interval_pointer = NULL;
    int descriptor;
    int result;

    native_init_stdio_handles();
    if (!handle || (handle != (BPTR)&native_stdin_handle.amiga &&
                    handle != (BPTR)stdin))
        return DOSFALSE;
    native_load_input_prefix();
    /* The synthetic argument line is part of the cooked Input() stream only.
       A raw-mode reader never consumes it, so counting it here would report
       a character forever and never reach the select() below -- and a
       program that polls with WaitForChar(0) to ask whether input is waiting
       right now, as Vim's char_avail() does before every screen update,
       would be told "yes" and then block in Read() until a key arrives.
       Bytes the cooked line editor has already pulled off the descriptor are
       different: those are real console input, and Read() returns them. */
    if ((!native_input_raw &&
         native_input_prefix_position < native_input_prefix_length) ||
        native_editor_line_position < native_editor_line_length)
        return DOSTRUE;
    file = stdin;
    descriptor = fileno(file);
    if (descriptor < 0)
        return DOSFALSE;
    if (timeout >= 0) {
        interval_pointer = &interval;
    }
    do {
        if (interval_pointer) {
            interval.tv_sec = timeout / 1000000L;
            interval.tv_usec = timeout % 1000000L;
        }
        FD_ZERO(&readable);
        FD_SET(descriptor, &readable);
        result = select(descriptor + 1, &readable, NULL, NULL,
                        interval_pointer);
    } while (result < 0 && errno == EINTR);
    return result > 0 ? DOSTRUE : DOSFALSE;
}

LONG FPutC(BPTR handle, LONG character)
{
    if (fputc((unsigned char)character, as_file(handle)) == EOF) {
        native_ioerr = errno;
        return -1;
    }
    return character;
}

LONG FPuts(BPTR handle, CONST_STRPTR string)
{
    if (fputs(string, as_file(handle)) == EOF) {
        native_ioerr = errno;
        return -1;
    }
    return 0;
}

STRPTR FGets(BPTR handle, STRPTR buffer, LONG length)
{
    FILE *file = as_file(handle);

    if (file != stdin) {
        if (!fgets(buffer, length, file)) {
            native_ioerr = errno;
            return NULL;
        }
        return buffer;
    }
    if (!buffer || length <= 1)
        return NULL;
    for (LONG index = 0; index < length - 1; index++) {
        int character = native_input_getc(file);

        if (character == EOF) {
            if (index == 0) {
                native_ioerr = errno;
                return NULL;
            }
            break;
        }
        buffer[index] = (char)character;
        if (character == '\n') {
            buffer[index + 1] = '\0';
            return buffer;
        }
        buffer[index + 1] = '\0';
    }
    return buffer;
}

LONG Flush(BPTR handle)
{
    if (fflush(as_file(handle)) != 0) {
        native_ioerr = errno;
        return DOSFALSE;
    }
    return DOSTRUE;
}

LONG IoErr(void)
{
    return native_ioerr;
}

void SetIoErr(LONG error)
{
    native_ioerr = error;
}

void native_request_endcli(void)
{
    native_endcli_requested = 1;
    native_cli.cli_Background = DOSTRUE;
    native_cli.cli_Interactive = DOSFALSE;
    /* Shell.c uses this condition to stop a buffered input script. */
    native_cli.cli_FailLevel = 0;
    (void)native_broker_setfaillevel(0);
}

static void set_native_broker_error(void)
{
    /* Anything left unmapped reaches the user as a raw Linux errno through
       an AmigaDOS command's PrintFault(), which prints "Error 39" where
       AmigaDOS would say the directory is not empty. Only errnos with an
       unambiguous AmigaDOS counterpart are translated here; the ones whose
       meaning depends on the operation are mapped by their caller. */
    switch (errno) {
    case ENOENT:      native_ioerr = ERROR_OBJECT_NOT_FOUND; break;
    case ENOMEM:      native_ioerr = ERROR_NO_FREE_STORE; break;
    case ENOTEMPTY:   native_ioerr = ERROR_DIRECTORY_NOT_EMPTY; break;
    case EEXIST:      native_ioerr = ERROR_OBJECT_EXISTS; break;
    case ENOTDIR:     native_ioerr = ERROR_OBJECT_WRONG_TYPE; break;
    case EROFS:       native_ioerr = ERROR_DISK_WRITE_PROTECTED; break;
    case ENOSPC:      native_ioerr = ERROR_DISK_FULL; break;
    case ENAMETOOLONG: native_ioerr = ERROR_INVALID_COMPONENT_NAME; break;
    /* The filesystem does not implement the operation -- a VFAT volume has
       no extended attributes to keep a file comment in, and ACE mounts VFAT.
       ERROR_ACTION_NOT_KNOWN is AmigaDOS's own answer for a handler that
       does not understand a packet, which is the same statement. */
    case ENOTSUP:     native_ioerr = ERROR_ACTION_NOT_KNOWN; break;
    default:          native_ioerr = (LONG)errno; break;
    }
}

/*
 * The script a shell is reading commands from, when it is reading from one.
 * AmigaDOS calls this cli_CurrentInput, and every command that has to know
 * whether it is running inside a script -- If, Else, EndIf -- asks by
 * comparing it against cli_StandardInput.
 *
 * On a real Amiga the shell and its commands share one CLI, so a command can
 * simply read it. An ACE command is its own Linux process, so the shell
 * passes the open descriptor across the fork and names it here. What comes
 * back is a stream over the same file description, which means the same file
 * offset: a command that consumes lines -- If skipping to its EndIf --
 * advances the shell's own position by doing so, exactly as it would on the
 * machine this code was written for.
 *
 * Unbuffered for the same reason. Any read-ahead would consume the shell's
 * next command into a buffer that dies with the command's process.
 */
static FILE *native_script_input;

/* The last character FGetC() handed out, for UnGetC()'s AmigaDOS -1. */
static FILE *native_last_read_file;
static int native_last_read_char = EOF;

static void native_attach_script_input(void)
{
    const char *descriptor = getenv(ACE_SCRIPT_INPUT_VARIABLE);
    char *end;
    long value;

    if (native_script_input || !descriptor || !*descriptor)
        return;
    value = strtol(descriptor, &end, 10);
    if (*end || value < 0 || value > INT_MAX)
        return;
    native_script_input = fdopen((int)value, "r");
    if (native_script_input)
        (void)setvbuf(native_script_input, NULL, _IONBF, 0);
}

void native_cli_set_script_input(FILE *file)
{
    native_script_input = file;
    if (file)
        (void)setvbuf(file, NULL, _IONBF, 0);
    if (native_cli_loaded)
        native_cli.cli_CurrentInput = file ? handle_for_file(file) :
                                      native_cli.cli_StandardInput;
}

FILE *native_cli_script_input(void)
{
    native_attach_script_input();
    return native_script_input;
}


/* Streams and process links, which do not depend on the broker at all. */
static void cli_attach_streams(void)
{
    native_attach_script_input();
    if (native_script_input) {
        /* The two have to stay distinguishable now: If does SelectInput() on
           the script, so Input() is no longer the standard input, and
           Shell.c assigns cli_CurrentInput itself when the script ends.
           Take the script only on the first pass and leave it alone after,
           or the shell could never get back to reading the keyboard. */
        native_cli.cli_StandardInput = handle_for_file(stdin);
        if (!native_cli_loaded)
            native_cli.cli_CurrentInput =
                handle_for_file(native_script_input);
    } else {
        native_cli.cli_StandardInput = Input();
        native_cli.cli_CurrentInput = native_cli.cli_StandardInput;
    }
    native_cli.cli_StandardOutput = Output();
    native_cli.cli_CurrentOutput = native_cli.cli_StandardOutput;
    native_cli.cli_StandardError = handle_for_file(stderr);
    native_cli.cli_DefaultStack = 8192 / CLI_DEFAULTSTACK_UNIT;
    native_process.pr_CES = (BPTR)stderr;
    native_process.pr_CLI = (BPTR)&native_cli;
    native_process.pr_TaskNum = 1;
    native_cli_loaded = 1;
}

/*
 * Real AmigaDOS gives every shell process a CLI, so AROS's own Shell.c and
 * cliPrompt.c dereference Cli() without checking it -- correct for the
 * system they were written against. On ACE the CLI is broker-backed, and the
 * broker is a separate process with a finite session table, so it can fail
 * where a real Amiga's could not. Returning NULL there turned a recoverable
 * broker error into a SIGSEGV inside unmodified AROS source (Shell.c:716,
 * and five more unchecked Cli() calls inside its command loop).
 *
 * So this never returns NULL. It keeps whatever state was last read
 * successfully -- which is what a transient failure wants -- or falls back to
 * the same defaults the broker itself would hand a new session, and says once
 * on stderr what went wrong, so a full session table is a diagnosable
 * degradation rather than a window that vanishes.
 */
static struct CommandLineInterface *cli_without_broker(void)
{
    static int warned;

    if (!warned) {
        warned = 1;
        fprintf(stderr, "ace: broker CLI state unavailable (%s); "
                        "continuing with defaults\n", strerror(errno));
    }
    if (!native_cli_loaded) {
        native_cli.cli_FailLevel = 10; /* the broker's DEFAULT_FAIL_LEVEL */
        native_cli_prompt[0] = '\0';
        native_cli.cli_Prompt = native_cli_prompt;
        native_cli_set_name[0] = '\0';
        native_cli.cli_SetName = native_cli_set_name;
    }
    cli_attach_streams();
    return &native_cli;
}

struct CommandLineInterface *Cli(void)
{
    char state[AMIGA_BROKER_MAX_PAYLOAD];
    char cwd[PATH_MAX];
    char *save = NULL;
    char *return_code;
    char *result2;
    char *fail_level;
    char *prompt;

    if (native_broker_ensure() != 0)
        return cli_without_broker();

    if (native_broker_getcli(state, sizeof(state)) != 0)
        return cli_without_broker();
    return_code = strtok_r(state, "\n", &save);
    result2 = strtok_r(NULL, "\n", &save);
    fail_level = strtok_r(NULL, "\n", &save);
    prompt = strtok_r(NULL, "\n", &save);
    if (!return_code || !result2 || !fail_level)
        return cli_without_broker();

    native_cli.cli_ReturnCode = (LONG)strtol(return_code, NULL, 10);
    native_cli.cli_Result2 = (LONG)strtol(result2, NULL, 10);
    native_cli.cli_FailLevel = (LONG)strtol(fail_level, NULL, 10);
    if (native_endcli_requested) {
        native_cli.cli_Background = DOSTRUE;
        native_cli.cli_Interactive = DOSFALSE;
        native_cli.cli_FailLevel = 0;
    }
    if (prompt) {
        strncpy(native_cli_prompt, prompt, sizeof(native_cli_prompt) - 1);
        native_cli_prompt[sizeof(native_cli_prompt) - 1] = '\0';
    } else {
        native_cli_prompt[0] = '\0';
    }
    native_cli.cli_Prompt = native_cli_prompt;
    if (native_broker_getcwd(cwd, sizeof(cwd)) == 0 &&
        native_broker_name_from_host(cwd, native_cli_set_name,
                                     sizeof(native_cli_set_name)) == 0) {
        /* The broker returns the AROS spelling, not the Linux cwd. */
    } else {
        native_cli_set_name[0] = '\0';
    }
    native_cli.cli_SetName = native_cli_set_name;
    cli_attach_streams();
    return &native_cli;
}

void Forbid(void)
{
}

void Permit(void)
{
}

UBYTE ToLower(ULONG character)
{
    if (character >= 'A' && character <= 'Z')
        character += 'a' - 'A';
    return (UBYTE)character;
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

void SetMem(APTR destination, ULONG length, UBYTE value)
{
    memset(destination, value, length);
}


ULONG CheckSignal(ULONG mask)
{
    (void)mask;
    return 0;
}

LONG AllocSignal(LONG signal_number)
{
    return signal_number;
}

void FreeSignal(LONG signal_number)
{
    (void)signal_number;
}

BOOL SetPrompt(CONST_STRPTR prompt)
{
    if (!prompt || native_broker_setprompt(prompt) != 0) {
        if (prompt)
            set_native_broker_error();
        else
            native_ioerr = ERROR_REQUIRED_ARG_MISSING;
        return DOSFALSE;
    }
    if (native_cli_loaded) {
        strncpy(native_cli_prompt, prompt, sizeof(native_cli_prompt) - 1);
        native_cli_prompt[sizeof(native_cli_prompt) - 1] = '\0';
        native_cli.cli_Prompt = native_cli_prompt;
    }
    return DOSTRUE;
}

/*
 * VPrintf() receives AmigaDOS's RawDoFmt data stream, not a C va_list.
 * The real Dir command deliberately passes an array of IPTR values to it
 * (including formats such as "%-32.s"), so forwarding the format string to
 * PutStr() silently loses every directory name.  Keep this small formatter
 * in the DOS host seam; its grammar mirrors exec.library's RawDoFmt parser
 * for the string and scalar forms used by the ported commands.
 */
static LONG native_vprintf(CONST_STRPTR format, const IPTR *data)
{
    FILE *output = selected_output();
    LONG written = 0;
    size_t argument = 0;

    while (*format) {
        int left = 0;
        int fill = ' ';
        ULONG minimum = 0;
        ULONG maximum = (ULONG)-1;
        char number_buffer[sizeof(IPTR) * 8 + 2];
        const char *text = NULL;
        ULONG length = 0;
        SIPTR signed_value = 0;
        IPTR unsigned_value = 0;
        char conversion;

        if (*format != '%') {
            if (fputc((unsigned char)*format++, output) == EOF)
                return DOSFALSE;
            written++;
            continue;
        }

        format++;
        if (*format == '-') {
            left = 1;
            format++;
        }
        if (*format == '0') {
            fill = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            minimum = minimum * 10u + (ULONG)(*format++ - '0');
        }
        if (*format == '.') {
            format++;
            if (*format >= '0' && *format <= '9') {
                maximum = 0;
                do {
                    maximum = maximum * 10u + (ULONG)(*format++ - '0');
                } while (*format >= '0' && *format <= '9');
            }
        }
        if (*format == 'l' || *format == 'i')
            format++;
        conversion = *format ? *format++ : '\0';

        switch (conversion) {
        case 's':
            text = (const char *)(uintptr_t)data[argument++];
            if (!text)
                text = "";
            length = (ULONG)strlen(text);
            break;

        case 'd':
        case 'D':
            signed_value = (SIPTR)data[argument++];
            if (signed_value < 0) {
                unsigned_value = (IPTR)(-signed_value);
                number_buffer[0] = '-';
                text = number_buffer + 1;
            } else {
                unsigned_value = (IPTR)signed_value;
                text = number_buffer;
            }
            length = (ULONG)(number_buffer + sizeof(number_buffer) - text);
            {
                char digits[sizeof(number_buffer)];
                ULONG digit_count = 0;

                do {
                    digits[digit_count++] =
                        (char)('0' + (unsigned_value % 10));
                    unsigned_value /= 10;
                } while (unsigned_value != 0 &&
                         digit_count < sizeof(digits));
                for (ULONG i = 0; i < digit_count; i++)
                    ((char *)text)[i] = digits[digit_count - i - 1];
                length = digit_count + (text == number_buffer ? 0u : 1u);
            }
            break;

        case 'u':
        case 'U':
        case 'x':
        case 'X':
        case 'p':
        case 'P': {
            static const char digits[] = "0123456789ABCDEF";
            unsigned int base = conversion == 'x' || conversion == 'X' ||
                                conversion == 'p' || conversion == 'P' ? 16 : 10;
            int pointer = conversion == 'p' || conversion == 'P';
            char *end = number_buffer + sizeof(number_buffer);
            char *cursor = end;

            unsigned_value = data[argument++];
            if (pointer) {
                fill = '0';
                minimum = sizeof(APTR) * 2;
            }
            do {
                *--cursor = digits[unsigned_value % base];
                unsigned_value /= base;
            } while (unsigned_value != 0 && cursor > number_buffer);
            text = cursor;
            length = (ULONG)(end - cursor);
            break;
        }

        case 'c':
            number_buffer[0] = (char)data[argument++];
            text = number_buffer;
            length = 1;
            break;

        case 'b': {
            const UBYTE *bstring = (const UBYTE *)(uintptr_t)data[argument++];

            if (!bstring) {
                text = "";
                length = 0;
            } else {
                length = bstring[0];
                text = (const char *)(bstring + 1);
            }
            break;
        }

        case '%':
            number_buffer[0] = '%';
            text = number_buffer;
            length = 1;
            break;

        default:
            /* Match RawDoFmt's useful treatment of an unknown conversion:
               print the conversion character and consume no data value. */
            number_buffer[0] = conversion;
            text = number_buffer;
            length = conversion ? 1u : 0u;
            break;
        }

        if (length > maximum)
            length = maximum;
        if (!left) {
            while (length < minimum) {
                if (fputc(fill, output) == EOF)
                    return DOSFALSE;
                written++;
                minimum--;
            }
        }
        for (ULONG i = 0; i < length; i++) {
            if (fputc((unsigned char)text[i], output) == EOF)
                return DOSFALSE;
            written++;
        }
        if (left) {
            while (length < minimum) {
                if (fputc(fill, output) == EOF)
                    return DOSFALSE;
                written++;
                minimum--;
            }
        }
    }
    return written;
}

LONG VPrintf(CONST_STRPTR format, APTR arguments)
{
    return native_vprintf(format, (const IPTR *)arguments);
}

void native_publish_result(int result_code)
{
    /* A standalone command is still useful without a broker.  In that case
       there is simply no session in which to publish the result. */
    if (native_cli_loaded)
        (void)native_broker_setfaillevel((int32_t)native_cli.cli_FailLevel);
    if (native_cli_loaded && native_cli.cli_Background)
        (void)native_broker_setvar("__ACE_ENDCLI", "1",
                                   AMIGA_BROKER_VAR_LOCAL);
    (void)native_broker_setresult((int32_t)result_code, (int32_t)native_ioerr);
    if (native_cli_loaded && native_cli.cli_Background)
        _exit(NATIVE_ENDCLI_STATUS);
}

/*
 * Global variables are files, one per variable, in ENV: -- and in ENVARC:
 * too when they are meant to outlive the boot. That is not an implementation
 * detail of AmigaDOS, it is the interface: a program reads ENV:SYS/theme.var
 * by opening it, `Type ENV:Editor` prints one, and Delete removes one, all
 * without going near GetVar(). rom/dos/{getvar,setvar,deletevar}.c are the
 * description this follows.
 *
 * Local variables stay in the broker. On AmigaOS they live on the process's
 * own pr_LocalVars list, which every command in that shell shares because
 * every command in that shell is that process. An ACE command is a separate
 * Linux process, so the list has to live somewhere both can reach, and that
 * is what the broker is.
 */
static int global_var_name(const char *volume, CONST_STRPTR name,
                           char *result, size_t result_size)
{
    int written;

    if (!name || !*name)
        return -1;
    /* The volume ends in ':', so this is AddPart() for the only shape that
       reaches here -- and it keeps any subdirectory in the name, which is
       how ENV:SYS/theme.var works. */
    written = snprintf(result, result_size, "%s%s", volume, (const char *)name);
    return written > 0 && (size_t)written < result_size ? 0 : -1;
}

static LONG global_var_read(const char *volume, CONST_STRPTR name,
                            STRPTR buffer, LONG size, LONG flags)
{
    char path[PATH_MAX];
    BPTR file;
    LONG count;

    if (!buffer || size <= 0 ||
        global_var_name(volume, name, path, sizeof(path)) != 0)
        return -1;
    file = Open(path, MODE_OLDFILE);
    if (!file)
        return -1;
    count = Read(file, buffer, size);
    Close(file);
    if (count < 0)
        return -1;
    if (!(flags & GVF_BINARY_VAR)) {
        LONG index = 0;

        /* A variable is the first line of its file unless the caller says
           it wants the bytes. */
        while (index < count && buffer[index] != '\n')
            index++;
        if (index >= size)
            index = size - 1;
        buffer[index] = '\0';
        return index;
    }
    if (!(flags & GVF_DONT_NULL_TERM)) {
        if (count >= size)
            count = size - 1;
        buffer[count] = '\0';
    }
    return count;
}

static BOOL global_var_write(const char *volume, CONST_STRPTR name,
                             CONST_STRPTR value, LONG size)
{
    char path[PATH_MAX];
    BPTR file;
    LONG written;

    if (global_var_name(volume, name, path, sizeof(path)) != 0)
        return DOSFALSE;
    file = Open(path, MODE_NEWFILE);
    if (!file)
        return DOSFALSE;
    written = size > 0 ? Write(file, (APTR)value, size) : 0;
    Close(file);
    return written == size ? DOSTRUE : DOSFALSE;
}

static BOOL global_var_delete(const char *volume, CONST_STRPTR name)
{
    char path[PATH_MAX];

    if (global_var_name(volume, name, path, sizeof(path)) != 0)
        return DOSFALSE;
    return DeleteFile(path);
}

LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, LONG flags)
{
    char value[PATH_MAX];
    uint32_t broker_flags = 0;
    size_t length;

    if ((flags & 0xff) == LV_ALIAS)
        broker_flags |= AMIGA_BROKER_VAR_ALIAS;
    if (!(flags & GVF_GLOBAL_ONLY)) {
        if (native_broker_getvar(name, broker_flags | AMIGA_BROKER_VAR_LOCAL,
                                 value, sizeof(value)) == 0) {
            length = strlen(value);
            if (!buffer || size <= 0 || length >= (size_t)size) {
                native_ioerr = ERROR_LINE_TOO_LONG;
                return -1;
            }
            memcpy(buffer, value, length + 1);
            return (LONG)length;
        }
    }
    /* ENV: first and ENVARC: after: a variable that has been changed this
       boot shadows the saved one, which is the point of having both. */
    if ((flags & 0xff) == LV_VAR && !(flags & GVF_LOCAL_ONLY)) {
        LONG result = global_var_read("ENV:", name, buffer, size, flags);

        if (result >= 0)
            return result;
        result = global_var_read("ENVARC:", name, buffer, size, flags);
        if (result >= 0)
            return result;
    }
    native_ioerr = ERROR_OBJECT_NOT_FOUND;
    return -1;
}

BOOL SetVar(CONST_STRPTR name, CONST_STRPTR value, LONG size, LONG flags)
{
    char truncated[PATH_MAX];
    const char *stored = value;
    uint32_t broker_flags = 0;
    size_t length;

    if (size >= 0) {
        length = (size_t)size;
        if (length >= sizeof(truncated)) {
            native_ioerr = ERROR_LINE_TOO_LONG;
            return DOSFALSE;
        }
        memcpy(truncated, value, length);
        truncated[length] = '\0';
        stored = truncated;
    } else {
        length = strlen(value);
        size = (LONG)length;
    }
    if ((flags & 0xff) == LV_VAR && (flags & GVF_GLOBAL_ONLY)) {
        if (!global_var_write("ENV:", name, stored, size))
            return DOSFALSE;
        /* GVF_SAVE_VAR is what makes a variable survive the next boot: it
           goes to the archive as well as to the live copy. */
        if ((flags & GVF_SAVE_VAR) &&
            !global_var_write("ENVARC:", name, stored, size))
            return DOSFALSE;
        return DOSTRUE;
    }
    if ((flags & 0xff) == LV_ALIAS)
        broker_flags |= AMIGA_BROKER_VAR_ALIAS;
    broker_flags |= AMIGA_BROKER_VAR_LOCAL;
    if (native_broker_setvar(name, stored, broker_flags) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    return DOSTRUE;
}

BOOL DeleteVar(CONST_STRPTR name, LONG flags)
{
    uint32_t broker_flags = 0;

    if ((flags & 0xff) == LV_VAR && (flags & GVF_GLOBAL_ONLY)) {
        BOOL removed = global_var_delete("ENV:", name);

        /* Without GVF_SAVE_VAR the archived copy stays, so the variable
           comes back at the next boot -- which is the documented way to undo
           a change made only for this one. */
        if (flags & GVF_SAVE_VAR)
            removed = global_var_delete("ENVARC:", name) || removed;
        return removed;
    }
    if ((flags & 0xff) == LV_ALIAS)
        broker_flags |= AMIGA_BROKER_VAR_ALIAS;
    broker_flags |= AMIGA_BROKER_VAR_LOCAL;
    if (native_broker_deletevar(name, broker_flags) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    return DOSTRUE;
}

LONG Printf(CONST_STRPTR format, ...)
{
    va_list arguments;
    int result;

    va_start(arguments, format);
    result = vfprintf(selected_output(), format, arguments);
    va_end(arguments);
    return result < 0 ? DOSFALSE : result;
}

LONG PutStr(CONST_STRPTR string)
{
    if (fputs(string, selected_output()) == EOF) {
        native_ioerr = errno;
        return DOSFALSE;
    }
    return DOSTRUE;
}

#include "aros_fault.c"

LONG FGetC(BPTR handle)
{
    FILE *file = handle ? as_file(handle) : selected_input();
    int character = native_input_getc(file);

    if (character == EOF) {
        native_ioerr = ERROR_OBJECT_NOT_FOUND;
        return ENDSTREAMCH;
    }
    native_last_read_file = file;
    native_last_read_char = character;
    return (UBYTE)character;
}

LONG Read(BPTR handle, APTR buffer, LONG length)
{
    size_t result;
    FILE *file = handle ? as_file(handle) : selected_input();

    if (!buffer || length <= 0)
        return 0;
    if (file == stdin) {
        size_t index;

        /* A raw console Read() is a byte-stream operation.  Reading one
           byte through stdio and then polling between bytes can return the
           first byte of an injected console reply by itself when the
           producer and consumer are scheduled apart.  Unchanged Amiga
           programs such as Vim expect the complete currently available
           control reply, so use the underlying descriptor in raw mode. */
        if (native_input_raw) {
            ssize_t bytes;

            /* Anything the cooked line editor already read off the
               descriptor is console input the program has not seen yet --
               keys typed ahead while the command was starting.  Hand those
               back before touching the descriptor, and without waiting for
               more, exactly as a single availability-sized read would. */
            if (native_editor_line_position < native_editor_line_length) {
                size_t available = native_editor_line_length -
                                   native_editor_line_position;

                if (available > (size_t)length)
                    available = (size_t)length;
                memcpy(buffer, native_editor_line +
                       native_editor_line_position, available);
                native_editor_line_position += available;
                return (LONG)available;
            }
            bytes = read(fileno(file), buffer, (size_t)length);
            if (bytes < 0)
                native_ioerr = errno;
            return (LONG)bytes;
        }

        for (index = 0; index < (size_t)length; index++) {
            /* A raw console read is commonly preceded by WaitForChar(),
               and must return the bytes available at that instant rather
               than block trying to fill Vim's larger scratch buffer. */
            if (native_input_raw && index != 0 &&
                !WaitForChar(handle, 0))
                break;
            int character = native_input_getc(file);

            if (character == EOF)
                break;
            ((unsigned char *)buffer)[index] = (unsigned char)character;
        }
        result = index;
    } else {
        result = fread(buffer, 1, (size_t)length, file);
    }
    if (result == 0 && ferror(file))
        native_ioerr = errno;
    return (LONG)result;
}

LONG FRead(BPTR handle, APTR buffer, LONG block_size, LONG block_count)
{
    size_t result;
    FILE *file = handle ? as_file(handle) : selected_input();

    if (!buffer || block_size <= 0 || block_count <= 0)
        return 0;
    if (file == stdin) {
        size_t bytes = (size_t)block_size * (size_t)block_count;
        result = (size_t)Read(handle, buffer, (LONG)bytes) /
                 (size_t)block_size;
    } else {
        result = fread(buffer, (size_t)block_size, (size_t)block_count,
                       file);
    }
    if (result == 0 && ferror(file))
        native_ioerr = errno;
    return (LONG)result;
}

LONG UnGetC(BPTR handle, LONG character)
{
    FILE *file = handle ? as_file(handle) : selected_input();
    int result;

    /* -1 is AmigaDOS for "the character just read", which is how readitem.c
       puts back the delimiter that ended an item -- a newline, most of the
       time. Pushing back the byte 0xff instead, which is what casting -1
       produces, left that newline consumed and a spurious byte in its place:
       enough for If's skip loop, which reads to the end of the line after
       finding its EndIf, to run on and swallow the line after it. */
    if (character == -1) {
        if (file != native_last_read_file || native_last_read_char == EOF) {
            native_ioerr = ERROR_OBJECT_WRONG_TYPE;
            return ENDSTREAMCH;
        }
        character = native_last_read_char;
    }
    result = ungetc((unsigned char)character, file);
    if (result == EOF) {
        native_ioerr = errno;
        return ENDSTREAMCH;
    }
    native_last_read_char = EOF;
    return result;
}

BPTR SelectInput(BPTR handle)
{
    BPTR old = Input();
    native_input = handle ? as_file(handle) : stdin;
    return old;
}

BPTR SelectOutput(BPTR handle)
{
    BPTR old = Output();
    native_output = handle ? as_file(handle) : stdout;
    return old;
}

LONG Seek(BPTR handle, LONG position, LONG mode)
{
    FILE *file = handle ? as_file(handle) : selected_input();
    long old = ftell(file);
    int whence = mode == OFFSET_BEGINNING ? SEEK_SET :
                 mode == OFFSET_END ? SEEK_END : SEEK_CUR;

    if (old < 0 || fseek(file, position, whence) != 0) {
        native_ioerr = errno;
        return -1;
    }
    return (LONG)old;
}

void SetProgramName(CONST_STRPTR name)
{
    if (!name)
        name = "";
    strncpy(native_program_name, name, sizeof(native_program_name) - 1);
    native_program_name[sizeof(native_program_name) - 1] = '\0';
}

/* The name the shell entered this command under, which a command reports
   its own errors against. ACE has no CLI-owned command name to read it back
   out of, so the program name is whatever SetProgramName() was last told --
   for a command started by RunCommand(), argv[0]. */
LONG GetProgramName(STRPTR buffer, LONG length)
{
    size_t used = strlen(native_program_name);

    if (!buffer || length <= 0)
        return DOSFALSE;
    if (used >= (size_t)length) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }
    memcpy(buffer, native_program_name, used + 1);
    return DOSTRUE;
}

BPTR SetProgramDir(BPTR lock)
{
    BPTR old = native_program_dir;
    native_program_dir = lock;
    return old;
}

BPTR ParentOfFH(BPTR file)
{
    char current[PATH_MAX];
    struct native_lock *lock;

    (void)file;
    if (native_broker_getcwd(current, sizeof(current)) != 0)
        return BNULL;
    lock = calloc(1, sizeof(*lock));
    if (!lock)
        return BNULL;
    snprintf(lock->path, sizeof(lock->path), "%s", current);
    return lock;
}

LONG ExamineFH(BPTR file, struct FileInfoBlock *fib)
{
    struct stat information;

    if (!file || !fib || fstat(fileno(as_file(file)), &information) != 0) {
        native_ioerr = errno;
        return DOSFALSE;
    }
    fib->fib_DirEntryType = S_ISDIR(information.st_mode) ? 1 : -1;
    fib->fib_Protection = 0;
    return DOSTRUE;
}

void bug(const char *format, ...)
{
    (void)format;
}

/* ReadArgs() and FreeArgs() are imported from AROS's dos.library.  Keep the
   public symbols at the host seam while the real implementation owns the
   template grammar, /M behavior, aliases, and allocation lifetime. */
extern struct RDArgs *ace_aros_ReadArgs(CONST_STRPTR template,
                                        IPTR *arguments,
                                        struct RDArgs *rdargs);
extern void ace_aros_FreeArgs(struct RDArgs *rdargs);

struct RDArgs *ReadArgs(CONST_STRPTR template, IPTR *arguments,
                        struct RDArgs *rdargs)
{
    return ace_aros_ReadArgs(template, arguments, rdargs);
}

void FreeArgs(struct RDArgs *rdargs)
{
    ace_aros_FreeArgs(rdargs);
}
