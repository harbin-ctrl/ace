#define _GNU_SOURCE

#include "native_console_endpoint.h"

#include "con_handler.h"
#include "console_channel.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdlib.h>

struct native_console_endpoint {
    struct amiga_console_device device;
    struct amiga_console_unit *unit;
    struct amiga_con_handler handler;
    struct ace_console_channel *channel;
    atomic_bool closing;
};

static int endpoint_read(void *context, void *data, size_t length,
                         size_t *actual)
{
    struct native_console_endpoint *endpoint = context;

    if (!endpoint || !endpoint->channel || !actual) {
        if (actual)
            *actual = 0;
        return AMIGA_IOERR_BADADDRESS;
    }
    while (!atomic_load(&endpoint->closing)) {
        ssize_t received;
        int ready = ace_console_channel_wait(endpoint->channel, 10000);

        if (ready < 0) {
            *actual = 0;
            return errno == EINTR ? AMIGA_IOERR_ABORTED : errno;
        }
        if (ready == 0)
            continue;
        received = ace_console_channel_receive(endpoint->channel, data,
                                               length);
        if (received > 0) {
            *actual = (size_t)received;
            return AMIGA_IOERR_OK;
        }
        if (received == 0) {
            *actual = 0;
            return AMIGA_IOERR_ABORTED;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            continue;
        *actual = 0;
        return errno;
    }
    *actual = 0;
    return AMIGA_IOERR_ABORTED;
}

static int endpoint_write(void *context, const void *data, size_t length,
                          size_t *actual)
{
    struct native_console_endpoint *endpoint = context;

    if (!endpoint || !endpoint->channel || !actual)
        return AMIGA_IOERR_BADADDRESS;
    if (atomic_load(&endpoint->closing)) {
        *actual = 0;
        return AMIGA_IOERR_ABORTED;
    }
    if (ace_console_channel_send(endpoint->channel, data, length) != 0) {
        *actual = 0;
        return errno;
    }
    *actual = length;
    return AMIGA_IOERR_OK;
}

struct native_console_endpoint *native_console_endpoint_open(
    struct ace_console_channel *channel)
{
    struct native_console_endpoint *endpoint;

    if (!channel)
        return NULL;
    endpoint = calloc(1, sizeof(*endpoint));
    if (!endpoint)
        return NULL;
    endpoint->channel = channel;
    atomic_init(&endpoint->closing, false);
    endpoint->device.context = endpoint;
    endpoint->device.read = endpoint_read;
    endpoint->device.write = endpoint_write;
    if (amiga_console_OpenDevice(&endpoint->device, &endpoint->unit) !=
        AMIGA_IOERR_OK ||
        amiga_con_handler_Open(endpoint->unit, &endpoint->handler) !=
        AMIGA_IOERR_OK) {
        if (endpoint->unit)
            amiga_console_CloseDevice(endpoint->unit);
        free(endpoint);
        return NULL;
    }
    return endpoint;
}

void native_console_endpoint_close(struct native_console_endpoint *endpoint)
{
    if (!endpoint)
        return;
    atomic_store(&endpoint->closing, true);
    amiga_con_handler_Close(&endpoint->handler);
    amiga_console_CloseDevice(endpoint->unit);
    free(endpoint);
}

int native_console_endpoint_read(struct native_console_endpoint *endpoint,
                                 void *data, size_t length, size_t *actual)
{
    if (!endpoint)
        return AMIGA_IOERR_BADADDRESS;
    return amiga_con_handler_Read(&endpoint->handler, data, length, actual);
}

int native_console_endpoint_write(struct native_console_endpoint *endpoint,
                                  const void *data, size_t length,
                                  size_t *actual)
{
    if (!endpoint)
        return AMIGA_IOERR_BADADDRESS;
    return amiga_con_handler_Write(&endpoint->handler, data, length, actual);
}

int native_console_endpoint_set_raw(struct native_console_endpoint *endpoint,
                                    int raw)
{
    if (!endpoint)
        return AMIGA_IOERR_BADADDRESS;
    return amiga_con_handler_SetRaw(&endpoint->handler, raw);
}
