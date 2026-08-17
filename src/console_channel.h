#ifndef ACE_CONSOLE_CHANNEL_H
#define ACE_CONSOLE_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/*
 * The current-console channel is the byte-oriented seam between an ACE
 * process and the console window that owns its standard handles.  It is
 * deliberately smaller than a CON: handler: the handler/device split and
 * packet protocol are later stages.  Keeping the state here gives those
 * stages one place to grow without changing the stream that ET already
 * speaks.
 */
struct ace_console_channel {
    int fd;
    int rows;
    int cols;
    bool raw;
    void *output_trace;
    void *input_trace;
};

/* Opens optional raw traces named by ACE_DBGCON and ACE_DBGCON_INPUT. */
void ace_console_channel_init(struct ace_console_channel *channel, int fd);
void ace_console_channel_close(struct ace_console_channel *channel);

void ace_console_channel_set_fd(struct ace_console_channel *channel, int fd);
int ace_console_channel_send(struct ace_console_channel *channel,
                             const void *data, size_t length);
ssize_t ace_console_channel_receive(struct ace_console_channel *channel,
                                    void *data, size_t length);

void ace_console_channel_set_geometry(struct ace_console_channel *channel,
                                      int rows, int cols);
int ace_console_channel_rows(const struct ace_console_channel *channel);
int ace_console_channel_cols(const struct ace_console_channel *channel);

void ace_console_channel_set_raw(struct ace_console_channel *channel,
                                 bool raw);
bool ace_console_channel_is_raw(const struct ace_console_channel *channel);

#endif
