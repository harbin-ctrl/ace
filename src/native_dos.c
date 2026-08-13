#define _GNU_SOURCE

#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/stdio.h>
#include <dos/var.h>
#include <exec/lists.h>
#include <exec/execbase.h>
#include <utility/utility.h>
#include "broker_client.h"
#include "native_host.h"
#include "broker_protocol.h"

static LONG native_ioerr;

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
static struct ExecBase native_exec_base;
static struct UtilityBase native_utility_base;
static struct DosLibrary native_dos_base;
static struct CommandLineInterface native_cli;
static char native_cli_prompt[PATH_MAX];
static char native_cli_set_name[PATH_MAX];
static int native_cli_loaded;
static int native_endcli_requested;

#define NATIVE_LOCAL_VAR_LIMIT 128
#define NATIVE_LOCAL_VAR_NAME 64

static struct Process native_process;
static struct LocalVar native_local_vars[NATIVE_LOCAL_VAR_LIMIT];
static char native_local_var_names[NATIVE_LOCAL_VAR_LIMIT][NATIVE_LOCAL_VAR_NAME];
static char native_local_var_values[NATIVE_LOCAL_VAR_LIMIT][AMIGA_BROKER_MAX_PAYLOAD];
static FILE *native_input = NULL;
static FILE *native_output = NULL;
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

static void native_init_stdio_handles(void)
{
    if (native_stdio_initialized)
        return;
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
    char path[PATH_MAX];
};

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

LONG Strnicmp(CONST_STRPTR left, CONST_STRPTR right, LONG length)
{
    return strncasecmp(left, right, (size_t)length);
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

BPTR AllocDosObject(LONG type, APTR tags)
{
    (void)tags;
    if (type == DOS_FIB)
        return calloc(1, sizeof(struct FileInfoBlock));
    if (type == DOS_RDARGS)
        return calloc(1, sizeof(struct RDArgs));
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
    free(object);
}

BPTR Lock(CONST_STRPTR name, LONG mode)
{
    char resolved[PATH_MAX];
    struct stat information;
    struct native_lock *lock;

    (void)mode;
    if (native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0 ||
        stat(resolved, &information) != 0) {
        native_ioerr = errno;
        return NULL;
    }
    lock = malloc(sizeof(*lock));
    if (!lock) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    strcpy(lock->path, resolved);
    return lock;
}

LONG UnLock(BPTR handle)
{
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
    lock = malloc(sizeof(*lock));
    if (!lock) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return BNULL;
    }
    snprintf(lock->path, sizeof(lock->path), "%s", resolved);
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
    struct stat information;
    struct native_lock *lock = handle;
    if (!lock || !fib || stat(lock->path, &information) != 0) {
        native_ioerr = errno;
        return DOSFALSE;
    }
    fib->fib_DirEntryType = S_ISDIR(information.st_mode) ? 1 : -1;
    return DOSTRUE;
}

BPTR CurrentDir(BPTR handle)
{
    char current[PATH_MAX];
    struct native_lock *old;

    if (native_broker_getcwd(current, sizeof(current)) != 0) {
        native_ioerr = errno;
        return NULL;
    }
    old = malloc(sizeof(*old));
    if (!old) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    strcpy(old->path, current);
    if (handle) {
        struct native_lock *new_dir = handle;
        if (native_broker_setcwd(new_dir->path) != 0) {
            native_ioerr = errno;
            free(old);
            return NULL;
        }
    }
    return old;
}

LONG NameFromLock(BPTR handle, STRPTR buffer, LONG length)
{
    struct native_lock *lock = handle;
    if (!lock || !buffer || length <= 0 || strlen(lock->path) >= (size_t)length) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return DOSFALSE;
    }
    strcpy(buffer, lock->path);
    return DOSTRUE;
}

void SetCurrentDirName(CONST_STRPTR name)
{
    (void)name;
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

    if (name && strncasecmp(name, "CON:", 4) == 0)
        return native_console_open(name);

    if (native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0) {
        native_ioerr = errno;
        return NULL;
    }
    file = fopen(resolved, access);
    if (!file && mode == MODE_OLDFILE && name && !strchr(name, '/') &&
        !strchr(name, ':')) {
        if (native_command_path(name, resolved, sizeof(resolved)) == 0)
            file = fopen(resolved, access);
    }
    if (!file)
        native_ioerr = errno == ENOENT ? ERROR_OBJECT_NOT_FOUND : errno;
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

    if (!name || native_broker_resolve_path(name, resolved, sizeof(resolved)) != 0 ||
        unlink(resolved) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    return DOSTRUE;
}

BOOL IsInteractive(BPTR handle)
{
    return handle == (BPTR)stdin || handle == (BPTR)stdout ||
           handle == (BPTR)stderr;
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
    if (!fgets(buffer, length, as_file(handle))) {
        native_ioerr = errno;
        return NULL;
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
    if (errno == ENOENT)
        native_ioerr = ERROR_OBJECT_NOT_FOUND;
    else if (errno == ENOMEM)
        native_ioerr = ERROR_NO_FREE_STORE;
    else
        native_ioerr = (LONG)errno;
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
        return NULL;

    if (native_broker_getcli(state, sizeof(state)) != 0)
        return NULL;
    return_code = strtok_r(state, "\n", &save);
    result2 = strtok_r(NULL, "\n", &save);
    fail_level = strtok_r(NULL, "\n", &save);
    prompt = strtok_r(NULL, "\n", &save);
    if (!return_code || !result2 || !fail_level)
        return NULL;

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
    if (native_broker_getcwd(cwd, sizeof(cwd)) == 0)
        snprintf(native_cli_set_name, sizeof(native_cli_set_name), "%s", cwd);
    else
        native_cli_set_name[0] = '\0';
    native_cli.cli_SetName = native_cli_set_name;
    native_cli.cli_StandardInput = Input();
    native_cli.cli_CurrentInput = native_cli.cli_StandardInput;
    native_cli.cli_StandardOutput = Output();
    native_cli.cli_CurrentOutput = native_cli.cli_StandardOutput;
    native_cli.cli_StandardError = handle_for_file(stderr);
    native_cli.cli_DefaultStack = 8192 / CLI_DEFAULTSTACK_UNIT;
    native_process.pr_CES = (BPTR)stderr;
    native_process.pr_CLI = (BPTR)&native_cli;
    native_process.pr_TaskNum = 1;
    native_cli_loaded = 1;
    return &native_cli;
}

void Forbid(void)
{
}

void Permit(void)
{
}

ULONG SetSignal(ULONG set_mask, ULONG clear_mask)
{
    (void)set_mask;
    (void)clear_mask;
    return 0;
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

LONG VPrintf(CONST_STRPTR format, APTR arguments)
{
    (void)arguments;
    return PutStr(format);
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

LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, LONG flags)
{
    char value[PATH_MAX];
    uint32_t broker_flags = 0;
    size_t length;

    if (flags & GVF_LOCAL_ONLY)
        broker_flags |= AMIGA_BROKER_VAR_LOCAL;
    if (flags & GVF_GLOBAL_ONLY)
        broker_flags |= AMIGA_BROKER_VAR_GLOBAL;
    if ((flags & 0xff) == LV_ALIAS)
        broker_flags |= AMIGA_BROKER_VAR_ALIAS;
    if (native_broker_getvar(name, broker_flags, value, sizeof(value)) != 0) {
        set_native_broker_error();
        return -1;
    }
    length = strlen(value);
    if (!buffer || size <= 0 || length >= (size_t)size) {
        native_ioerr = ERROR_LINE_TOO_LONG;
        return -1;
    }
    memcpy(buffer, value, length + 1);
    return (LONG)length;
}

BOOL SetVar(CONST_STRPTR name, CONST_STRPTR value, LONG size, LONG flags)
{
    char truncated[PATH_MAX];
    const char *stored = value;
    uint32_t broker_flags = 0;
    size_t length;

    if (flags & GVF_LOCAL_ONLY)
        broker_flags |= AMIGA_BROKER_VAR_LOCAL;
    if (flags & GVF_GLOBAL_ONLY)
        broker_flags |= AMIGA_BROKER_VAR_GLOBAL;
    if (flags & GVF_SAVE_VAR)
        broker_flags |= AMIGA_BROKER_VAR_SAVE;
    if ((flags & 0xff) == LV_ALIAS)
        broker_flags |= AMIGA_BROKER_VAR_ALIAS;
    if (size >= 0) {
        length = (size_t)size;
        if (length >= sizeof(truncated)) {
            native_ioerr = ERROR_LINE_TOO_LONG;
            return DOSFALSE;
        }
        memcpy(truncated, value, length);
        truncated[length] = '\0';
        stored = truncated;
    }
    if (native_broker_setvar(name, stored, broker_flags) != 0) {
        set_native_broker_error();
        return DOSFALSE;
    }
    return DOSTRUE;
}

BOOL DeleteVar(CONST_STRPTR name, LONG flags)
{
    uint32_t broker_flags = 0;

    if (flags & GVF_LOCAL_ONLY)
        broker_flags |= AMIGA_BROKER_VAR_LOCAL;
    if (flags & GVF_GLOBAL_ONLY)
        broker_flags |= AMIGA_BROKER_VAR_GLOBAL;
    if ((flags & 0xff) == LV_ALIAS)
        broker_flags |= AMIGA_BROKER_VAR_ALIAS;
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
    int character = fgetc(handle ? as_file(handle) : selected_input());

    if (character == EOF) {
        native_ioerr = ERROR_OBJECT_NOT_FOUND;
        return ENDSTREAMCH;
    }
    return (UBYTE)character;
}

LONG Read(BPTR handle, APTR buffer, LONG length)
{
    size_t result;
    FILE *file = handle ? as_file(handle) : selected_input();

    if (!buffer || length <= 0)
        return 0;
    result = fread(buffer, 1, (size_t)length, file);
    if (result == 0 && ferror(file))
        native_ioerr = errno;
    return (LONG)result;
}

LONG FRead(BPTR handle, APTR buffer, LONG block_size, LONG block_count)
{
    size_t result;

    if (!buffer || block_size <= 0 || block_count <= 0)
        return 0;
    result = fread(buffer, (size_t)block_size, (size_t)block_count,
                   handle ? as_file(handle) : selected_input());
    if (result == 0 && ferror(handle ? as_file(handle) : selected_input()))
        native_ioerr = errno;
    return (LONG)result;
}

LONG UnGetC(BPTR handle, LONG character)
{
    int result = ungetc((unsigned char)character,
                        handle ? as_file(handle) : selected_input());
    if (result == EOF) {
        native_ioerr = errno;
        return ENDSTREAMCH;
    }
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

LONG ReadItem(STRPTR buffer, LONG size, struct CSource *source)
{
    LONG start, length = 0;
    BOOL quoted = FALSE;

    if (!buffer || size <= 0 || !source || !source->CS_Buffer) {
        native_ioerr = ERROR_REQUIRED_ARG_MISSING;
        return ITEM_NOTHING;
    }

    while (source->CS_CurChr < source->CS_Length &&
           (source->CS_Buffer[source->CS_CurChr] == ' ' ||
            source->CS_Buffer[source->CS_CurChr] == '\t' ||
            source->CS_Buffer[source->CS_CurChr] == '\r' ||
            source->CS_Buffer[source->CS_CurChr] == '\n'))
        source->CS_CurChr++;

    if (source->CS_CurChr >= source->CS_Length ||
        source->CS_Buffer[source->CS_CurChr] == '\0')
        return ITEM_NOTHING;

    start = source->CS_CurChr;
    if (source->CS_Buffer[source->CS_CurChr] == '"') {
        quoted = TRUE;
        source->CS_CurChr++;
    }

    while (source->CS_CurChr < source->CS_Length) {
        char character = source->CS_Buffer[source->CS_CurChr];

        if (quoted && character == '"') {
            source->CS_CurChr++;
            break;
        }
        if (!quoted && (character == ' ' || character == '\t' ||
                        character == '\r' || character == '\n'))
            break;
        if (!quoted && character == '"')
            break;
        if (character == '*' && source->CS_CurChr + 1 < source->CS_Length) {
            source->CS_CurChr++;
            character = source->CS_Buffer[source->CS_CurChr];
        }
        if (length + 1 < size)
            buffer[length++] = character;
        source->CS_CurChr++;
    }

    buffer[length] = '\0';
    if (length == 0 && !quoted && source->CS_CurChr == start)
        return ITEM_NOTHING;
    if (length + 1 >= size)
        native_ioerr = ERROR_LINE_TOO_LONG;
    return quoted ? ITEM_QUOTED : ITEM_UNQUOTED;
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
    lock = malloc(sizeof(*lock));
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

struct native_arg_spec {
    char name[64];
    unsigned flags;
};

static size_t native_parse_template(CONST_STRPTR template,
                                    struct native_arg_spec *specs,
                                    size_t capacity)
{
    const char *cursor = template;
    size_t count = 0;

    while (cursor && *cursor && count < capacity) {
        const char *end = strchr(cursor, ',');
        const char *slash = strchr(cursor, '/');
        const char *name_end = end ? end : cursor + strlen(cursor);
        struct native_arg_spec *spec = &specs[count];
        size_t length;

        if (slash && slash < name_end)
            name_end = slash;
        while (cursor < name_end && isspace((unsigned char)*cursor))
            cursor++;
        length = (size_t)(name_end - cursor);
        while (length && isspace((unsigned char)cursor[length - 1]))
            length--;
        if (length >= sizeof(spec->name))
            length = sizeof(spec->name) - 1;
        memcpy(spec->name, cursor, length);
        spec->name[length] = '\0';
        if (slash && (!end || slash < end)) {
            for (const char *modifier = slash + 1;
                 modifier < (end ? end : cursor + strlen(cursor)); modifier++) {
                switch (toupper((unsigned char)*modifier)) {
                case 'A': spec->flags |= 0x80; break;
                case 'K': spec->flags |= 0x40; break;
                case 'M': spec->flags |= 0x20; break;
                case 'S': spec->flags |= 0x01; break;
                case 'T': spec->flags |= 0x02; break;
                case 'N': spec->flags |= 0x04; break;
                case 'F': spec->flags |= 0x08; break;
                default: break;
                }
            }
        }
        count++;
        cursor = end ? end + 1 : NULL;
    }
    return count;
}

static size_t native_split_args(char *line, char **tokens, size_t capacity)
{
    char *cursor = line;
    size_t count = 0;

    while (*cursor && count < capacity) {
        BOOL quoted = FALSE;
        char *write;

        while (isspace((unsigned char)*cursor))
            cursor++;
        if (!*cursor)
            break;
        tokens[count++] = write = cursor;
        while (*cursor) {
            if (*cursor == '"') {
                quoted = !quoted;
                cursor++;
            } else if (!quoted && isspace((unsigned char)*cursor)) {
                *write = '\0';
                cursor++;
                break;
            } else {
                *write++ = *cursor++;
            }
        }
        *write = '\0';
    }
    return count;
}

static BOOL native_process_arguments(char *line, size_t line_size)
{
    unsigned char raw[AMIGA_BROKER_MAX_PAYLOAD];
    FILE *source = fopen("/proc/self/cmdline", "rb");
    size_t bytes, cursor = 0, argument = 0, used = 0;

    if (!source)
        return FALSE;
    bytes = fread(raw, 1, sizeof(raw) - 1, source);
    fclose(source);
    while (cursor < bytes) {
        unsigned char *end = memchr(raw + cursor, '\0', bytes - cursor);
        size_t length = end ? (size_t)(end - (raw + cursor)) : bytes - cursor;

        if (argument++ > 0 && length > 0) {
            BOOL quote = FALSE;
            if (used > 0 && used + 1 < line_size)
                line[used++] = ' ';
            for (size_t i = 0; i < length; i++)
                if (isspace(raw[cursor + i]) || raw[cursor + i] == '"')
                    quote = TRUE;
            if (quote && used + 1 < line_size)
                line[used++] = '"';
            for (size_t i = 0; i < length && used + 2 < line_size; i++) {
                if (raw[cursor + i] == '"' || raw[cursor + i] == '\\')
                    line[used++] = '\\';
                line[used++] = (char)raw[cursor + i];
            }
            if (quote && used + 1 < line_size)
                line[used++] = '"';
        }
        cursor += length + 1;
    }
    line[used] = '\0';
    return argument > 1;
}

static BOOL native_token_matches(CONST_STRPTR token,
                                 const struct native_arg_spec *spec,
                                 CONST_STRPTR *value)
{
    const char *equals = strchr(token, '=');
    size_t name_length = equals ? (size_t)(equals - token) : strlen(token);

    if (name_length != strlen(spec->name) ||
        strncasecmp(token, spec->name, name_length) != 0)
        return FALSE;
    if (value)
        *value = equals ? equals + 1 : "1";
    return TRUE;
}

static BOOL native_is_keyword(char *token,
                              const struct native_arg_spec *specs,
                              size_t spec_count)
{
    for (size_t i = 0; i < spec_count; i++) {
        if (specs[i].flags & (0x40 | 0x01 | 0x02)) {
            CONST_STRPTR value;
            if (native_token_matches(token, &specs[i], &value))
                return TRUE;
        }
    }
    return FALSE;
}

static BOOL native_store_arg(struct RDArgs *result, IPTR *arguments,
                             size_t index, CONST_STRPTR value, unsigned flags)
{
    if (flags & (0x01 | 0x02)) {
        arguments[index] = (IPTR)-1;
        return TRUE;
    }
    if (flags & 0x20) {
        char **values = calloc(2, sizeof(*values));
        if (!values)
            return FALSE;
        values[0] = strdup(value);
        if (!values[0]) {
            free(values);
            return FALSE;
        }
        result->RDA_Values[index] = values;
        result->RDA_Multiple[index] = 1;
        arguments[index] = (IPTR)values;
    } else {
        char *copy = strdup(value);
        if (!copy)
            return FALSE;
        result->RDA_Values[index] = copy;
        arguments[index] = (IPTR)copy;
    }
    return TRUE;
}

struct RDArgs *ReadArgs(CONST_STRPTR template, IPTR *arguments,
                        struct RDArgs *rdargs)
{
    struct native_arg_spec specs[32] = {0};
    char line[AMIGA_BROKER_MAX_PAYLOAD];
    char *tokens[64] = {0};
    BOOL used[64] = {0};
    CONST_STRPTR keyed_values[32] = {0};
    size_t spec_count, token_count, positional = 0;
    struct RDArgs *result = rdargs;
    BOOL allocated = FALSE;
    struct CSource source = {0};

    if (!template || !arguments) {
        native_ioerr = ERROR_REQUIRED_ARG_MISSING;
        return NULL;
    }
    if (result) {
        source = result->RDA_Source;
        native_free_rdargs_values(result);
        memset(result, 0, sizeof(*result));
        result->RDA_Source = source;
    } else {
        result = calloc(1, sizeof(*result));
        allocated = TRUE;
    }
    if (!result) {
        native_ioerr = ERROR_NO_FREE_STORE;
        return NULL;
    }
    result->RDA_Owned = allocated ? 1 : 0;

    if (source.CS_Buffer) {
        size_t length = source.CS_Length > source.CS_CurChr ?
                        (size_t)(source.CS_Length - source.CS_CurChr) : 0;
        if (length >= sizeof(line))
            length = sizeof(line) - 1;
        memcpy(line, source.CS_Buffer + source.CS_CurChr, length);
        line[length] = '\0';
    } else if (!native_process_arguments(line, sizeof(line)) &&
               !FGets(Input(), line, sizeof(line))) {
        native_ioerr = ERROR_REQUIRED_ARG_MISSING;
        goto fail;
    }
    spec_count = native_parse_template(template, specs, 32);
    token_count = native_split_args(line, tokens, 64);
    for (size_t i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "?") == 0) {
            PutStr(template);
            PutStr(": ");
            Flush(Output());
            if (!FGets(Input(), line, sizeof(line))) {
                native_ioerr = ERROR_REQUIRED_ARG_MISSING;
                goto fail;
            }
            token_count = native_split_args(line, tokens, 64);
            break;
        }
    }
    result->RDA_Arguments = arguments;
    result->RDA_ArgumentCount = spec_count;

    /* First consume named switches and /K arguments. */
    for (size_t i = 0; i < spec_count; i++) {
        if (!(specs[i].flags & (0x40 | 0x01 | 0x02)))
            continue;
        for (size_t j = 0; j < token_count; j++) {
            CONST_STRPTR value;
            if (!used[j] && native_token_matches(tokens[j], &specs[i], &value)) {
                used[j] = TRUE;
                keyed_values[i] = value;
                break;
            }
        }
        if (keyed_values[i] &&
            !native_store_arg(result, arguments, i, keyed_values[i], specs[i].flags))
            goto fail;
    }

    /* Then assign remaining positional tokens.  /M consumes all of them
       except tokens belonging to named switches. */
    for (size_t i = 0; i < spec_count; i++) {
        if (specs[i].flags & (0x40 | 0x01 | 0x02))
            continue;
        if (specs[i].flags & 0x20) {
            size_t count = 0;
            char **values;
            for (size_t j = 0; j < token_count; j++)
                if (!used[j] && !native_is_keyword(tokens[j], specs, spec_count))
                    count++;
            if (!count)
                continue;
            values = calloc(count + 1, sizeof(*values));
            if (!values)
                goto fail;
            for (size_t j = 0, value_index = 0; j < token_count; j++) {
                if (!used[j] && !native_is_keyword(tokens[j], specs, spec_count)) {
                    values[value_index] = strdup(tokens[j]);
                    if (!values[value_index++]) {
                        for (size_t k = 0; k < value_index; k++)
                            free(values[k]);
                        free(values);
                        goto fail;
                    }
                    used[j] = TRUE;
                }
            }
            result->RDA_Values[i] = values;
            result->RDA_Multiple[i] = 1;
            arguments[i] = (IPTR)values;
        } else {
            while (positional < token_count &&
                   (used[positional] ||
                    native_is_keyword(tokens[positional], specs, spec_count)))
                positional++;
            if (positional < token_count) {
                if (!native_store_arg(result, arguments, i, tokens[positional],
                                      specs[i].flags))
                    goto fail;
                used[positional++] = TRUE;
            }
        }
    }
    for (size_t i = 0; i < spec_count; i++)
        if ((specs[i].flags & 0x80) && !arguments[i]) {
            native_ioerr = ERROR_REQUIRED_ARG_MISSING;
            goto fail;
        }
    for (size_t i = 0; i < token_count; i++)
        if (!used[i] && !native_is_keyword(tokens[i], specs, spec_count)) {
            native_ioerr = ERROR_TOO_MANY_ARGS;
            goto fail;
        }
    return result;

fail:
    native_free_rdargs_values(result);
    if (allocated)
        free(result);
    return NULL;
}

void FreeArgs(struct RDArgs *rdargs)
{
    BOOL owned;

    if (!rdargs)
        return;
    owned = rdargs->RDA_Owned;
    native_free_rdargs_values(rdargs);
    if (owned)
        free(rdargs);
}
