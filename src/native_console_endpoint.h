#ifndef ACE_NATIVE_CONSOLE_ENDPOINT_H
#define ACE_NATIVE_CONSOLE_ENDPOINT_H

#include <stddef.h>

struct ace_console_channel;
struct native_console_endpoint;

struct native_console_endpoint *native_console_endpoint_open(
    struct ace_console_channel *channel);
void native_console_endpoint_close(struct native_console_endpoint *endpoint);
int native_console_endpoint_read(struct native_console_endpoint *endpoint,
                                 void *data, size_t length, size_t *actual);
int native_console_endpoint_write(struct native_console_endpoint *endpoint,
                                  const void *data, size_t length,
                                  size_t *actual);
int native_console_endpoint_set_raw(struct native_console_endpoint *endpoint,
                                    int raw);

#endif
