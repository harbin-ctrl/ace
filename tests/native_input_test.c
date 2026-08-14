#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dos/dos.h>

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

int main(void)
{
    int descriptors[2];
    char line[32] = {0};
    char raw[8] = {0};

    assert(pipe(descriptors) == 0);
    assert(dup2(descriptors[0], STDIN_FILENO) == STDIN_FILENO);
    close(descriptors[0]);
    setenv("ACE_CONSOLE_INTERACTIVE", "1", 1);

    /* Cooked input is edited in the process that owns Input(), and the
       completed line is what FGetC exposes to the shell. */
    write_all(descriptors[1], "first\n", 6);
    assert(FGetC(Input()) == 'f');
    assert(FGetC(Input()) == 'i');
    assert(FGetC(Input()) == 'r');
    assert(FGetC(Input()) == 's');
    assert(FGetC(Input()) == 't');
    assert(FGetC(Input()) == '\n');

    /* CSI has to be presented to AROS's parser as one input item. */
    write_all(descriptors[1], "\233A\n", 3);
    assert(FGets(Input(), line, sizeof(line)) != NULL);
    assert(strcmp(line, "first\n") == 0);

    /* Raw mode is the contract Vim uses: no cooked editing or echo, and
       WaitForChar observes the bytes before Read() consumes them. */
    assert(SetMode(Input(), 1) == DOSTRUE);
    write_all(descriptors[1], "\033[A", 3);
    assert(WaitForChar(Input(), 1000000) != 0);
    assert(Read(Input(), raw, sizeof(raw)) == 3);
    assert(memcmp(raw, "\033[A", 3) == 0);
    assert(SetMode(Input(), 0) == DOSTRUE);

    close(descriptors[1]);
    return 0;
}
