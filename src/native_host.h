#ifndef ACE_NATIVE_HOST_H
#define ACE_NATIVE_HOST_H

#include <stddef.h>
#include <stdio.h>

#include <exec/types.h>

#define NATIVE_ENDCLI_STATUS 201

/* The descriptor a shell running a script hands to the commands it starts,
   so a command can find cli_CurrentInput -- the stream AmigaDOS would have
   let it read straight out of the shared CLI. Named in the environment for
   the same reason ACE_COMMAND_ARGUMENTS is: it is the only channel a fork
   and an exec leave open between an ACE shell and an ACE command. */
#define ACE_SCRIPT_INPUT_VARIABLE "ACE_SCRIPT_INPUT"

/* The script a shell was started to run instead of the usual startup set,
   and the marker that says it was started to run only that. */
#define ACE_STARTUP_SCRIPT_VARIABLE "ACE_STARTUP_SCRIPT"

void native_cli_set_script_input(FILE *file);
FILE *native_cli_script_input(void);
void native_set_interactive(int interactive);
int native_execute_script(const char *name);
int native_quit_script(void);

BPTR native_console_open(const char *specification);
int native_console_is_handle(BPTR handle);
const char *native_console_specification(BPTR handle);
void native_console_close(BPTR handle);
BPTR native_lock_host_path(const char *path);
int native_command_path(const char *name, char *result, size_t result_size);
int native_run_background(const char *command);
void native_request_endcli(void);

#endif
