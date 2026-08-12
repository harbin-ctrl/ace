#include "con_handler.h"

#include <string.h>

static size_t cooked_available(const struct amiga_con_handler *handler)
{
    for (size_t index = 0; index < handler->input_length; index++)
        if (handler->input[index] == '\n' || handler->input[index] == '\r')
            return index + 1;
    return 0;
}

static int append_input(struct amiga_con_handler *handler,
                        const unsigned char *data, size_t length)
{
    if (length > sizeof(handler->input) - handler->input_length)
        return AMIGA_IOERR_UNITBUSY;
    memcpy(handler->input + handler->input_length, data, length);
    handler->input_length += length;
    return AMIGA_IOERR_OK;
}

int amiga_con_handler_Open(struct amiga_console_unit *unit,
                           struct amiga_con_handler *handler)
{
    if (!handler)
        return AMIGA_IOERR_BADADDRESS;
    memset(handler, 0, sizeof(*handler));
    return amiga_con_Open(unit, &handler->file);
}

void amiga_con_handler_Close(struct amiga_con_handler *handler)
{
    if (!handler)
        return;
    amiga_con_Close(&handler->file);
    handler->input_length = 0;
}

int amiga_con_handler_Read(struct amiga_con_handler *handler, void *data,
                           size_t length, size_t *actual)
{
    struct amiga_console_io_request request = {
        .command = AMIGA_CMD_READ,
        .data = handler ? handler->device_input : NULL,
        .length = sizeof(handler->device_input),
    };
    unsigned char *destination = data;
    size_t available;
    size_t count;
    int error;

    if (actual)
        *actual = 0;
    if (!handler || (!data && length != 0))
        return AMIGA_IOERR_BADADDRESS;
    if (length == 0)
        return AMIGA_IOERR_OK;
    for (;;) {
        available = handler->raw ? handler->input_length :
                    cooked_available(handler);
        if (available != 0)
            break;
        amiga_console_InitIO(&request);
        error = amiga_console_SendIO(handler->file.unit, &request);
        if (error != AMIGA_IOERR_OK)
            return error;
        error = amiga_console_WaitIO(&request);
        if (error != AMIGA_IOERR_OK)
            return error;
        if (request.actual == 0)
            continue;
        error = append_input(handler, handler->device_input, request.actual);
        if (error != AMIGA_IOERR_OK)
            return error;
    }
    count = available < length ? available : length;
    memcpy(destination, handler->input, count);
    memmove(handler->input, handler->input + count,
            handler->input_length - count);
    handler->input_length -= count;
    if (actual)
        *actual = count;
    return AMIGA_IOERR_OK;
}

int amiga_con_handler_Write(struct amiga_con_handler *handler,
                            const void *data, size_t length, size_t *actual)
{
    if (!handler)
        return AMIGA_IOERR_BADADDRESS;
    return amiga_con_Write(&handler->file, data, length, actual);
}

int amiga_con_handler_SetRaw(struct amiga_con_handler *handler, int raw)
{
    if (!handler)
        return AMIGA_IOERR_BADADDRESS;
    handler->raw = raw != 0;
    return AMIGA_IOERR_OK;
}

int amiga_con_handler_FeedInput(struct amiga_con_handler *handler,
                                const void *data, size_t length)
{
    if (!handler)
        return AMIGA_IOERR_BADADDRESS;
    return amiga_console_FeedInput(handler->file.unit, data, length);
}
