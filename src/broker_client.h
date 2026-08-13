#ifndef AMIGA_SHELL_BROKER_CLIENT_H
#define AMIGA_SHELL_BROKER_CLIENT_H

#include <stddef.h>
#include <stdint.h>

int native_broker_ensure(void);
int native_broker_resolve_path(const char *path, char *result, size_t result_size);
int native_broker_getcwd(char *result, size_t result_size);
int native_broker_setcwd(const char *path);
int native_broker_assign(const char *name, const char *path);
int native_broker_getvar(const char *name, uint32_t flags,
                         char *result, size_t result_size);
int native_broker_setvar(const char *name, const char *value, uint32_t flags);
int native_broker_deletevar(const char *name, uint32_t flags);
int native_broker_listvars(uint32_t flags, char *result, size_t result_size);
int native_broker_getcli(char *result, size_t result_size);
int native_broker_setfaillevel(int32_t fail_level);
int native_broker_setprompt(const char *prompt);
int native_broker_clone_session(const char *child_session);
int native_broker_getresult(char *result, size_t result_size);
int native_broker_setresult(int32_t return_code, int32_t result2);
int native_broker_listdos(char *result, size_t result_size);

#endif
