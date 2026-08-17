#define _GNU_SOURCE

#include "console_channel.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static FILE *open_trace(const char *variable)
{
    const char *path = getenv(variable);
    FILE *trace;

    if (!path || !*path)
        return NULL;
    trace = fopen(path, "wb");
    if (!trace) {
        fprintf(stderr, "ace-console: cannot open %s=%s: %s\n", variable,
                path, strerror(errno));
        return NULL;
    }
    setvbuf(trace, NULL, _IONBF, 0);
    return trace;
}

static void trace_bytes(FILE *trace, const void *data, size_t length)
{
    const unsigned char *bytes = data;

    while (trace && length != 0) {
        size_t written = fwrite(bytes, 1, length, trace);

        if (written == 0)
            break;
        bytes += written;
        length -= written;
    }
}

void ace_console_channel_init(struct ace_console_channel *channel, int fd)
{
    ace_console_channel_attach(channel, fd, fd);
    channel->output_trace = open_trace("ACE_DBGCON");
    channel->input_trace = open_trace("ACE_DBGCON_INPUT");
}

void ace_console_channel_attach(struct ace_console_channel *channel,
                                int input_fd, int output_fd)
{
    memset(channel, 0, sizeof(*channel));
    channel->fd = input_fd == output_fd ? input_fd : -1;
    channel->input_fd = input_fd;
    channel->output_fd = output_fd;
}

void ace_console_channel_close(struct ace_console_channel *channel)
{
    if (!channel)
        return;
    if (channel->output_trace)
        fclose(channel->output_trace);
    if (channel->input_trace)
        fclose(channel->input_trace);
    channel->output_trace = NULL;
    channel->input_trace = NULL;
    channel->fd = -1;
    channel->input_fd = -1;
    channel->output_fd = -1;
}

void ace_console_channel_set_fd(struct ace_console_channel *channel, int fd)
{
    ace_console_channel_set_fds(channel, fd, fd);
}

void ace_console_channel_set_fds(struct ace_console_channel *channel,
                                 int input_fd, int output_fd)
{
    if (!channel)
        return;
    channel->fd = input_fd == output_fd ? input_fd : -1;
    channel->input_fd = input_fd;
    channel->output_fd = output_fd;
}

int ace_console_channel_send(struct ace_console_channel *channel,
                             const void *data, size_t length)
{
    const unsigned char *bytes = data;

    if (!channel || channel->output_fd < 0) {
        errno = EBADF;
        return -1;
    }
    while (length != 0) {
        ssize_t written = write(channel->output_fd, bytes, length);

        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (written == 0) {
            errno = EPIPE;
            return -1;
        }
        trace_bytes(channel->input_trace, bytes, (size_t)written);
        bytes += written;
        length -= (size_t)written;
    }
    return 0;
}

ssize_t ace_console_channel_read(struct ace_console_channel *channel,
                                 void *data, size_t length)
{
    ssize_t received;

    if (!channel || channel->input_fd < 0) {
        errno = EBADF;
        return -1;
    }
    do {
        received = read(channel->input_fd, data, length);
    } while (received < 0 && errno == EINTR);
    if (received > 0)
        trace_bytes(channel->output_trace, data, (size_t)received);
    return received;
}

ssize_t ace_console_channel_receive(struct ace_console_channel *channel,
                                    void *data, size_t length)
{
    ssize_t received;

    if (!channel || channel->input_fd < 0) {
        errno = EBADF;
        return -1;
    }
    received = recv(channel->input_fd, data, length, MSG_DONTWAIT);
    if (received < 0 && (errno == ENOTSOCK || errno == EOPNOTSUPP))
        received = read(channel->input_fd, data, length);
    if (received > 0)
        trace_bytes(channel->output_trace, data, (size_t)received);
    return received;
}

int ace_console_channel_wait(struct ace_console_channel *channel,
                             long timeout_microseconds)
{
    fd_set readable;
    struct timeval timeout;
    struct timeval *timeout_pointer = NULL;
    int result;

    if (!channel || channel->input_fd < 0) {
        errno = EBADF;
        return -1;
    }
    if (timeout_microseconds >= 0) {
        timeout.tv_sec = timeout_microseconds / 1000000L;
        timeout.tv_usec = timeout_microseconds % 1000000L;
        timeout_pointer = &timeout;
    }
    do {
        FD_ZERO(&readable);
        FD_SET(channel->input_fd, &readable);
        result = select(channel->input_fd + 1, &readable, NULL, NULL,
                        timeout_pointer);
    } while (result < 0 && errno == EINTR);
    return result > 0 ? 1 : result;
}

void ace_console_channel_set_geometry(struct ace_console_channel *channel,
                                      int rows, int cols)
{
    if (!channel)
        return;
    channel->rows = rows > 0 ? rows : 0;
    channel->cols = cols > 0 ? cols : 0;
}

void ace_console_channel_notify_resize(struct ace_console_channel *channel,
                                       int rows, int cols)
{
    if (!channel)
        return;
    if (channel->rows == (rows > 0 ? rows : 0) &&
        channel->cols == (cols > 0 ? cols : 0))
        return;
    ace_console_channel_set_geometry(channel, rows, cols);
    channel->resize_generation++;
    channel->resize_pending = true;
}

int ace_console_channel_rows(const struct ace_console_channel *channel)
{
    return channel ? channel->rows : 0;
}

int ace_console_channel_cols(const struct ace_console_channel *channel)
{
    return channel ? channel->cols : 0;
}

unsigned long ace_console_channel_resize_generation(
    const struct ace_console_channel *channel)
{
    return channel ? channel->resize_generation : 0;
}

bool ace_console_channel_take_resize(struct ace_console_channel *channel)
{
    bool pending;

    if (!channel)
        return false;
    pending = channel->resize_pending;
    channel->resize_pending = false;
    return pending;
}

void ace_console_channel_set_raw(struct ace_console_channel *channel, bool raw)
{
    if (channel)
        channel->raw = raw;
}

bool ace_console_channel_is_raw(const struct ace_console_channel *channel)
{
    return channel && channel->raw;
}
