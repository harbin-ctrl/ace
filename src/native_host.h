#ifndef ACE_NATIVE_HOST_H
#define ACE_NATIVE_HOST_H

#include <stddef.h>

#include <exec/types.h>

#define NATIVE_ENDCLI_STATUS 201

BPTR native_console_open(const char *specification);
int native_console_is_handle(BPTR handle);
const char *native_console_specification(BPTR handle);
void native_console_close(BPTR handle);
BPTR native_lock_host_path(const char *path);
int native_command_path(const char *name, char *result, size_t result_size);
int native_run_background(const char *command);
void native_request_endcli(void);

#endif
