#ifndef AMIGA_SHELL_CON_HANDLER_H
#define AMIGA_SHELL_CON_HANDLER_H

#include "console_device.h"

#include <stddef.h>

#define AMIGA_CON_HANDLER_INPUT_SIZE 8192

/*
 * Classic, menu-free CON: handler state.  This is the host-port boundary
 * corresponding to the AROS handler's ACTION_READ/ACTION_WRITE path.  Window
 * policy, Workbench, menus, and clipboard behavior intentionally live outside
 * this object.
 */
struct amiga_con_handler {
    struct amiga_con_file file;
    unsigned char input[AMIGA_CON_HANDLER_INPUT_SIZE];
    unsigned char device_input[256];
    size_t input_length;
    int raw;
};

int amiga_con_handler_Open(struct amiga_console_unit *unit,
                           struct amiga_con_handler *handler);
void amiga_con_handler_Close(struct amiga_con_handler *handler);
int amiga_con_handler_Read(struct amiga_con_handler *handler, void *data,
                           size_t length, size_t *actual);
int amiga_con_handler_Write(struct amiga_con_handler *handler,
                            const void *data, size_t length, size_t *actual);
int amiga_con_handler_SetRaw(struct amiga_con_handler *handler, int raw);
int amiga_con_handler_FeedInput(struct amiga_con_handler *handler,
                                const void *data, size_t length);

#endif
