#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <string.h>
#include <unistd.h>

#include <dos/dos.h>

#include "native_host.h"

static void write_all(int file, const void *data, size_t length)
{
    const unsigned char *bytes = data;

    while (length != 0) {
        ssize_t written = write(file, bytes, length);

        assert(written > 0);
        bytes += written;
        length -= (size_t)written;
    }
}

static void read_all(int file, void *data, size_t length)
{
    unsigned char *bytes = data;

    while (length != 0) {
        ssize_t count = read(file, bytes, length);

        assert(count > 0);
        bytes += count;
        length -= (size_t)count;
    }
}

int main(void)
{
    int input[2];
    int output[2];
    int saved_stdin;
    int saved_stdout;
    BPTR console;
    BPTR alias;
    BPTR star;
    char input_byte = 0;
    char output_bytes[3];
    int rows;
    int cols;

    assert(pipe(input) == 0);
    assert(pipe(output) == 0);
    saved_stdin = dup(STDIN_FILENO);
    saved_stdout = dup(STDOUT_FILENO);
    assert(saved_stdin >= 0);
    assert(saved_stdout >= 0);
    assert(dup2(input[0], STDIN_FILENO) == STDIN_FILENO);
    assert(dup2(output[1], STDOUT_FILENO) == STDOUT_FILENO);
    close(input[0]);
    close(output[1]);

    console = Open("CONSOLE:", MODE_READWRITE);
    assert(console != BNULL);
    assert(native_console_is_handle(console));
    assert(strcmp(native_console_specification(console), "CONSOLE:") == 0);
    assert(IsInteractive(console) == DOSTRUE);

    alias = Open("CON:", MODE_READWRITE);
    star = Open("*", MODE_READWRITE);
    assert(alias != BNULL);
    assert(star != BNULL);
    assert(native_console_is_handle(alias));
    assert(native_console_is_handle(star));
    assert(native_console_is_raw_mode(console) == 0);
    assert(native_console_is_raw_mode(alias) == 0);
    assert(native_console_is_raw_mode(star) == 0);
    assert(native_console_is_raw_mode((BPTR)stdin) == 0);

    assert(SetMode(alias, 1) == DOSTRUE);
    assert(native_console_is_raw_mode(console) == 1);
    assert(native_console_is_raw_mode(star) == 1);
    assert(native_console_is_raw_mode((BPTR)stdin) == 1);
    write_all(input[1], "x", 1);
    assert(WaitForChar(console, 1000000) != DOSFALSE);
    assert(Read(console, &input_byte, 1) == 1);
    assert(input_byte == 'x');
    assert(SetMode(star, 0) == DOSTRUE);
    assert(native_console_is_raw_mode(console) == 0);
    assert(native_console_is_raw_mode(alias) == 0);

    assert(native_console_geometry(console, &rows, &cols) == 0);
    assert(rows == 0);
    assert(cols == 0);
    assert(native_console_resize_generation(console) == 0);
    assert(native_console_take_resize(alias) == 0);
    native_console_notify_resize(24, 80);
    assert(native_console_geometry(alias, &rows, &cols) == 0);
    assert(rows == 24);
    assert(cols == 80);
    assert(native_console_geometry(star, &rows, &cols) == 0);
    assert(rows == 24);
    assert(cols == 80);
    assert(native_console_resize_generation(console) == 1);
    assert(native_console_resize_generation(alias) == 1);
    assert(native_console_take_resize(star) == 1);
    assert(native_console_take_resize(console) == 0);
    native_console_notify_resize(24, 80);
    assert(native_console_resize_generation(console) == 1);
    native_console_notify_resize(25, 80);
    assert(native_console_resize_generation(alias) == 2);
    assert(native_console_take_resize(console) == 1);

    assert(FPuts(console, "out") == 0);
    assert(Flush(console) == DOSTRUE);
    read_all(output[0], output_bytes, sizeof(output_bytes));
    assert(memcmp(output_bytes, "out", sizeof(output_bytes)) == 0);

    assert(Close(alias) == DOSTRUE);
    assert(Close(star) == DOSTRUE);
    assert(Close(console) == DOSTRUE);

    assert(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    assert(dup2(saved_stdout, STDOUT_FILENO) == STDOUT_FILENO);
    close(saved_stdin);
    close(saved_stdout);
    close(input[1]);
    close(output[0]);
    return 0;
}
