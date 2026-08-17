#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
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
#include <dos/dostags.h>
#include <utility/tagitem.h>

#include "broker_client.h"
#include "native_host.h"

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
