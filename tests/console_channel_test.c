#define _GNU_SOURCE

#include "console_channel.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static void read_trace(const char *path, const char *expected)
{
    char buffer[64] = {0};
    FILE *trace = fopen(path, "rb");

    assert(trace != NULL);
    assert(fread(buffer, 1, sizeof(buffer) - 1, trace) == strlen(expected));
    assert(memcmp(buffer, expected, strlen(expected)) == 0);
    assert(fgetc(trace) == EOF);
    fclose(trace);
}

int main(void)
{
    char output_path[] = "/tmp/ace-console-channel-output-XXXXXX";
    char input_path[] = "/tmp/ace-console-channel-input-XXXXXX";
    int output_file = mkstemp(output_path);
    int input_file = mkstemp(input_path);
    int sockets[2];
    struct ace_console_channel channel;
    char buffer[16];

    assert(output_file >= 0);
    assert(input_file >= 0);
    close(output_file);
    close(input_file);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    assert(setenv("ACE_DBGCON", output_path, 1) == 0);
    assert(setenv("ACE_DBGCON_INPUT", input_path, 1) == 0);

    ace_console_channel_init(&channel, sockets[0]);
    ace_console_channel_set_geometry(&channel, 24, 80);
    assert(ace_console_channel_rows(&channel) == 24);
    assert(ace_console_channel_cols(&channel) == 80);
    assert(!ace_console_channel_is_raw(&channel));
    ace_console_channel_set_raw(&channel, true);
    assert(ace_console_channel_is_raw(&channel));

    assert(ace_console_channel_send(&channel, "key", 3) == 0);
    assert(read(sockets[1], buffer, sizeof(buffer)) == 3);
    assert(memcmp(buffer, "key", 3) == 0);
    assert(write(sockets[1], "screen", 6) == 6);
    assert(ace_console_channel_receive(&channel, buffer, sizeof(buffer)) == 6);
    assert(memcmp(buffer, "screen", 6) == 0);
    ace_console_channel_close(&channel);
    close(sockets[0]);
    close(sockets[1]);

    read_trace(output_path, "screen");
    read_trace(input_path, "key");
    unlink(output_path);
    unlink(input_path);
    return 0;
}
