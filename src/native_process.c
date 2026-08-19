#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/lists.h>
#include <utility/tagitem.h>

#include "aros_exec_runtime.h"
#include "broker_client.h"
#include "native_host.h"

struct Process *native_this_process(void);
void native_set_this_process(struct Process *process);

static int executable_directory(char *directory, size_t directory_size)
{
    char executable[PATH_MAX];
    char *slash;
    ssize_t length;

    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length < 0 || (size_t)length >= sizeof(executable) - 1)
        return -1;
    executable[length] = '\0';
    slash = strrchr(executable, '/');
    if (!slash)
        return -1;
    *slash = '\0';
    if (strlen(executable) >= directory_size)
        return -1;
    strcpy(directory, executable);
    return 0;
}

static int child_session_name(char *session, size_t session_size)
{
    const char *parent = getenv("ACE_SESSION");
    struct timespec now;

    if (!parent || !*parent)
        parent = "default";
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    if (snprintf(session, session_size, "%s-child-%ld-%ld", parent,
                 (long)getpid(), (long)now.tv_nsec) >= (int)session_size)
        return -1;
    return 0;
}

static int launch_console(BPTR input)
{
    char directory[PATH_MAX];
    char console_path[PATH_MAX];
    char session[128];
    pid_t child;

    if (!native_console_is_handle(input) ||
        executable_directory(directory, sizeof(directory)) != 0 ||
        snprintf(console_path, sizeof(console_path), "%s/ace-console",
                 directory) >= (int)sizeof(console_path) ||
        child_session_name(session, sizeof(session)) != 0 ||
        native_broker_ensure() != 0 ||
        native_broker_clone_session(session) != 0) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return -1;
    }

    child = fork();
    if (child < 0) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return -1;
    }
    if (child == 0) {
        (void)setsid();
        execl(console_path, console_path, "--session", session,
              (char *)NULL);
        _exit(RETURN_FAIL);
    }
    return 0;
}

static int duplicate_handle(BPTR handle, int output)
{
    int descriptor;

    if (!handle)
        return -1;
    if (native_console_is_handle(handle))
        return output ? native_console_dup_output(handle) :
                        native_console_dup_input(handle);
    descriptor = fileno((FILE *)handle);
    return descriptor >= 0 ? dup(descriptor) : -1;
}

static int duplicate_script_handle(BPTR handle)
{
    int descriptor;

    if (!handle || native_console_is_handle(handle))
        return -1;
    descriptor = fileno((FILE *)handle);
    return descriptor >= 0 ? dup(descriptor) : -1;
}

static int launch_command(CONST_STRPTR command, BPTR input, BPTR output,
                          BPTR error, BPTR script_input, BOOL asynch)
{
    char directory[PATH_MAX];
    char shell_path[PATH_MAX];
    char session[128];
    const char *existing_session;
    char amiga_script[64] = "";
    char script_descriptor[32] = "";
    int input_fd = -1;
    int output_fd = -1;
    int error_fd = -1;
    int script_fd = -1;
    pid_t child;

    if (executable_directory(directory, sizeof(directory)) != 0 ||
        snprintf(shell_path, sizeof(shell_path), "%s/ace-user-shell", directory) >= (int)sizeof(shell_path) ||
        native_broker_ensure() != 0) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return -1;
    }

    existing_session = native_console_session(input);
    if (existing_session) {
        if (snprintf(session, sizeof(session), "%s", existing_session) >=
            (int)sizeof(session)) {
            SetIoErr(ERROR_LINE_TOO_LONG);
            return -1;
        }
    } else if (child_session_name(session, sizeof(session)) != 0 ||
               native_broker_clone_session(session) != 0) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return -1;
    }

    if (command && *command) {
        static int cmd_counter = 1;
        if (snprintf(amiga_script, sizeof(amiga_script), "T:System-%d-%d",
                     getpid(), cmd_counter++) >= (int)sizeof(amiga_script)) {
            SetIoErr(ERROR_LINE_TOO_LONG);
            return -1;
        }
        BPTR script = Open(amiga_script, MODE_NEWFILE);
        if (script) {
            FILE *f = (FILE *)script;
            fprintf(f, "%s\n", (const char *)command);
            Close(script);
        }
    }

    input_fd = duplicate_handle(input, 0);
    output_fd = duplicate_handle(output, 1);
    error_fd = duplicate_handle(error, 1);
    if (input_fd >= 0 && output_fd < 0)
        output_fd = duplicate_handle(input, 1);
    if (input_fd >= 0 && error_fd < 0)
        error_fd = duplicate_handle(input, 1);
    if (script_input)
        script_fd = duplicate_script_handle(script_input);
    if ((input && input_fd < 0) || (output && output_fd < 0) ||
        (error && error_fd < 0) || (script_input && script_fd < 0)) {
        if (input_fd >= 0)
            close(input_fd);
        if (output_fd >= 0)
            close(output_fd);
        if (error_fd >= 0)
            close(error_fd);
        if (script_fd >= 0)
            close(script_fd);
        SetIoErr(ERROR_OBJECT_WRONG_TYPE);
        return -1;
    }

    child = fork();
    if (child < 0) {
        if (input_fd >= 0)
            close(input_fd);
        if (output_fd >= 0)
            close(output_fd);
        if (error_fd >= 0)
            close(error_fd);
        if (script_fd >= 0)
            close(script_fd);
        SetIoErr(ERROR_NO_FREE_STORE);
        return -1;
    }
    if (child == 0) {
        if (input_fd >= 0)
            dup2(input_fd, STDIN_FILENO);
        if (output_fd >= 0)
            dup2(output_fd, STDOUT_FILENO);
        if (error_fd >= 0)
            dup2(error_fd, STDERR_FILENO);
        if (script_fd >= 0) {
            snprintf(script_descriptor, sizeof(script_descriptor), "%d",
                     script_fd);
            setenv(ACE_SCRIPT_INPUT_VARIABLE, script_descriptor, 1);
        }
        if (amiga_script[0] != '\0')
            setenv(ACE_STARTUP_SCRIPT_VARIABLE, amiga_script, 1);
        if (input_fd > STDERR_FILENO)
            close(input_fd);
        if (output_fd > STDERR_FILENO && output_fd != input_fd)
            close(output_fd);
        if (error_fd > STDERR_FILENO && error_fd != input_fd &&
            error_fd != output_fd)
            close(error_fd);
        /* ACE_SCRIPT_INPUT names this descriptor in the exec'd native DOS
           layer; it must remain open across exec. */
        (void)setsid();
        setenv("ACE_SESSION", session, 1);
        setenv("TERM", "amiga", 1);
        execl(shell_path, shell_path, (char *)NULL);
        _exit(RETURN_FAIL);
    }
    if (input_fd >= 0)
        close(input_fd);
    if (output_fd >= 0)
        close(output_fd);
    if (error_fd >= 0)
        close(error_fd);
    if (script_fd >= 0)
        close(script_fd);
    if (!asynch) {
        int status;
        waitpid(child, &status, 0);
    }

    return 0;
}

LONG SystemTagList(CONST_STRPTR command, const struct TagItem *tags)
{
    const struct TagItem *tag;
    BPTR input = BNULL;
    BPTR output = BNULL;
    BPTR error = BNULL;
    BPTR script_input = BNULL;
    BOOL asynch = FALSE;

    for (tag = tags; tag && tag->ti_Tag != TAG_DONE; tag++) {
        if (tag->ti_Tag == SYS_Input)
            input = (BPTR)(uintptr_t)tag->ti_Data;
        else if (tag->ti_Tag == SYS_Output)
            output = (BPTR)(uintptr_t)tag->ti_Data;
        else if (tag->ti_Tag == SYS_Error)
            error = (BPTR)(uintptr_t)tag->ti_Data;
        else if (tag->ti_Tag == SYS_ScriptInput)
            script_input = (BPTR)(uintptr_t)tag->ti_Data;
        else if (tag->ti_Tag == SYS_Asynch)
            asynch = tag->ti_Data ? TRUE : FALSE;
    }

    if (command) {
        if (launch_command(command, input, output, error, script_input,
                           asynch) != 0)
            return -1;
    } else {
        if (launch_console(input) != 0)
            return -1;
        native_console_close(input);
    }
    return 0;
}

/*
 * CreateNewProc() -- an AmigaDOS process, which is a thread here.
 *
 * SystemTagList() above starts a *command*: it has a name to look up, so it
 * forks and execs ace-user-shell, and the child shares nothing but its
 * streams.  NP_Entry is the other kind of process creation and cannot work
 * that way.  The entry point is a function pointer in this executable, and
 * the code on the far side of it reaches back into memory the caller is still
 * using -- Regina's StartCommand() reads a ChildInfo the parent filled in
 * just before the call, and writes the return code the parent reads after.
 * A forked child would get a copy of all of it and the parent would see none
 * of the writes.
 *
 * A thread is also what the Amiga actually does.  Exec has one address space
 * and CreateNewProc() adds a thread of execution to it; two processes sharing
 * memory is the normal case there, not a compromise made here.
 *
 * What a created process needs before it may run:
 *   - its own struct Process, so FindTask(NULL) tells it apart from its
 *     parent -- see native_this_process();
 *   - its own entry in the task registry, so Signal()/Wait() between parent
 *     and child address two different tasks;
 *   - the three standard streams it was given, and the NP_Close* ownership
 *     that says which of them it must close on the way out.
 *
 * NP_Seglist is rejected rather than ignored.  It names code to load and run,
 * which is SystemTagList()'s job, and quietly starting a process that runs
 * nothing would be much harder to diagnose than a refusal.
 */

struct native_new_process {
    struct Process process;
    char name[64];
    void (*entry)(void);
    BPTR input;
    BPTR output;
    BPTR error;
    int close_input;
    int close_output;
    int close_error;
};

static void native_new_process_free(struct native_new_process *created)
{
    /* Ownership is per stream and defaults to TRUE, which is AmigaDOS's rule:
       a process closes what it was handed unless the caller kept it. */
    if (created->close_input && created->input)
        Close(created->input);
    if (created->close_output && created->output)
        Close(created->output);
    if (created->close_error && created->error)
        Close(created->error);
    ace_aros_runtime_unregister_task(&created->process.pr_Task);
    free(created);
}

static void *native_new_process_thread(void *argument)
{
    struct native_new_process *created = argument;

    /* Identity first, and before anything that might call FindTask(): from
       here on this thread is a different AmigaDOS process from the one that
       created it. */
    native_set_this_process(&created->process);
    ace_aros_runtime_set_current_task(&created->process.pr_Task);
    created->entry();
    native_set_this_process(NULL);
    native_new_process_free(created);
    return NULL;
}

struct Process *CreateNewProc(const struct TagItem *tags)
{
    const struct TagItem *tag;
    struct native_new_process *created;
    const char *name = "New Process";
    pthread_t thread;
    static int process_counter;

    created = calloc(1, sizeof(*created));
    if (!created) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    /* AmigaDOS closes each supplied stream on exit unless told otherwise. */
    created->close_input = 1;
    created->close_output = 1;
    created->close_error = 1;

    for (tag = tags; tag && tag->ti_Tag != TAG_DONE; tag++) {
        switch (tag->ti_Tag) {
        case NP_Entry:
            created->entry = (void (*)(void))(uintptr_t)tag->ti_Data;
            break;
        case NP_Input:
            created->input = (BPTR)(uintptr_t)tag->ti_Data;
            break;
        case NP_Output:
            created->output = (BPTR)(uintptr_t)tag->ti_Data;
            break;
        case NP_Error:
            created->error = (BPTR)(uintptr_t)tag->ti_Data;
            break;
        case NP_CloseInput:
            created->close_input = tag->ti_Data ? 1 : 0;
            break;
        case NP_CloseOutput:
            created->close_output = tag->ti_Data ? 1 : 0;
            break;
        case NP_CloseError:
            created->close_error = tag->ti_Data ? 1 : 0;
            break;
        case NP_Name:
            name = (const char *)(uintptr_t)tag->ti_Data;
            break;
        case NP_Seglist:
            /* See the note above: this is SystemTagList()'s job. */
            free(created);
            SetIoErr(ERROR_OBJECT_WRONG_TYPE);
            return NULL;
        default:
            /* NP_Cli, NP_StackSize, NP_Priority and the rest are accepted and
               not acted on.  A created process here already has the CLI its
               Linux process has, gets a host thread stack rather than one it
               was told the size of, and is scheduled by the host.  Refusing
               them would stop callers that pass them for good reason. */
            break;
        }
    }

    if (!created->entry) {
        /* Neither NP_Entry nor NP_Seglist: there is nothing to run. */
        free(created);
        SetIoErr(ERROR_REQUIRED_ARG_MISSING);
        return NULL;
    }

    snprintf(created->name, sizeof(created->name), "%s %d", name,
             ++process_counter);
    created->process.pr_Task.tc_Node.ln_Name = created->name;
    created->process.pr_CIS = created->input;
    created->process.pr_COS = created->output;
    created->process.pr_CES = created->error;
    NEWLIST(&created->process.pr_LocalVars);

    /* Registered by the creator rather than by the new thread, so that a
       Signal() sent to the returned process before that thread is scheduled
       still finds a task to deliver to. */
    if (ace_aros_runtime_register_task(&created->process.pr_Task) != 0) {
        free(created);
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    if (pthread_create(&thread, NULL, native_new_process_thread, created) != 0) {
        ace_aros_runtime_unregister_task(&created->process.pr_Task);
        free(created);
        SetIoErr(ERROR_NO_FREE_STORE);
        return NULL;
    }
    /* Detached: AmigaDOS has no join, and the process frees itself on exit. */
    pthread_detach(thread);
    return &created->process;
}

struct Process *CreateNewProcTags(IPTR tag1, ...)
{
    struct TagItem tags[32];
    size_t count = 0;
    va_list arguments;
    IPTR tag = tag1;

    va_start(arguments, tag1);
    while (count < sizeof(tags) / sizeof(tags[0]) - 1 && tag != TAG_DONE) {
        tags[count].ti_Tag = tag;
        tags[count].ti_Data = va_arg(arguments, IPTR);
        count++;
        tag = va_arg(arguments, IPTR);
    }
    va_end(arguments);
    tags[count].ti_Tag = TAG_DONE;
    tags[count].ti_Data = 0;
    return CreateNewProc(tags);
}

/*
 * CreateTask() -- the same thing CreateNewProc() makes, with less of it.
 *
 * Exec distinguishes a Task from a Process: a Process is a Task plus the DOS
 * state that makes DOS calls legal from it. ACE creates a Process either way,
 * because struct Process begins with its struct Task and because the
 * alternative -- a bare Task whose FindTask(NULL) has nowhere to point -- is
 * a trap. Regina's helper is the case in point: amifuncs.c starts
 * ReginaHandleMessages this way and that code waits on signals like any other
 * task, which needs an identity to deliver to.
 *
 * The cost of the difference is a struct Process rather than a struct Task
 * per created task, which is the smaller problem.
 */
struct Task *CreateTask(CONST_STRPTR name, LONG priority, APTR init_pc,
                        ULONG stack_size)
{
    struct Process *created;

    /* Stack size is the host thread's to choose, and priority is the host
       scheduler's; both are accepted so that callers written for Exec still
       compile and run. */
    (void)stack_size;
    created = CreateNewProcTags(NP_Entry, (IPTR)init_pc,
                                NP_Name, (IPTR)(name ? name : "New Task"),
                                NP_Priority, (IPTR)priority,
                                TAG_DONE, (IPTR)0);
    return created ? (struct Task *)created : NULL;
}

/*
 * Returns the previous priority, per rom/exec/settaskpri.c.  ACE does not
 * reorder anything on the strength of it: these are host threads and the host
 * scheduler decides.  Recorded rather than discarded so that a caller reading
 * the value back gets what it set.
 */
BYTE SetTaskPri(struct Task *task, LONG priority)
{
    BYTE previous;

    if (!task)
        return 0;
    previous = task->tc_Node.ln_Pri;
    task->tc_Node.ln_Pri = (BYTE)priority;
    return previous;
}
