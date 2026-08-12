#ifndef AMIGA_SHELL_CONSOLE_DEVICE_H
#define AMIGA_SHELL_CONSOLE_DEVICE_H

#include <stddef.h>
#include <stdint.h>

/*
 * These are the two stream commands which CON: relies on.  They deliberately
 * have the values used by AmigaDOS/AROS console.device.
 */
#define AMIGA_CMD_READ 2
#define AMIGA_CMD_WRITE 3

#define AMIGA_IOERR_OK 0
#define AMIGA_IOERR_ABORTED (-2)
#define AMIGA_IOERR_NOCMD (-3)
#define AMIGA_IOERR_BADADDRESS (-4)
#define AMIGA_IOERR_UNITBUSY (-5)

struct amiga_console_unit;

struct amiga_console_io_request {
    uint16_t command;
    void *data;
    size_t length;
    size_t actual;
    int error;
    void *private_state;
};

typedef int (*amiga_console_read_callback)(void *context, void *data,
                                           size_t length, size_t *actual);
typedef int (*amiga_console_write_callback)(void *context, const void *data,
                                            size_t length, size_t *actual);

/*
 * A deliberately small console.device-shaped object.  The callbacks are
 * serviced by the unit's worker task, which gives SendIO/WaitIO the same
 * request lifetime shape as Exec I/O without requiring an AROS scheduler.
 */
struct amiga_console_device {
    void *context;
    amiga_console_read_callback read;
    amiga_console_write_callback write;
};

/* A CON: file handle is an adapter over a console unit. */
struct amiga_con_file {
    struct amiga_console_unit *unit;
};

int amiga_console_OpenDevice(struct amiga_console_device *device,
                             struct amiga_console_unit **unit_out);
void amiga_console_CloseDevice(struct amiga_console_unit *unit);
void amiga_console_InitIO(struct amiga_console_io_request *request);
int amiga_console_SendIO(struct amiga_console_unit *unit,
                         struct amiga_console_io_request *request);
int amiga_console_WaitIO(struct amiga_console_io_request *request);
int amiga_console_AbortIO(struct amiga_console_unit *unit,
                          struct amiga_console_io_request *request);
int amiga_console_DoIO(struct amiga_console_unit *unit,
                       struct amiga_console_io_request *request);
int amiga_console_FeedInput(struct amiga_console_unit *unit, const void *data,
                            size_t length);

int amiga_con_Open(struct amiga_console_unit *unit, struct amiga_con_file *file);
void amiga_con_Close(struct amiga_con_file *file);
int amiga_con_Read(struct amiga_con_file *file, void *data, size_t length,
                   size_t *actual);
int amiga_con_Write(struct amiga_con_file *file, const void *data, size_t length,
                    size_t *actual);

#endif
