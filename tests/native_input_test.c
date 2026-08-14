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
    /* The shell exports an argument line for every command it runs, empty
       one included -- AmigaDOS leaves that line in the child's cooked
       Input() for ReadArgs() to find. */
    setenv("ACE_COMMAND_ARGUMENTS", "", 1);

    /* A raw reader is not a ReadArgs() caller and never consumes that line,
       so the line must not be reported as input waiting either. A zero
       timeout asks whether a character is available at this instant, which
       is how a full-screen program decides it can stop and redraw; claiming
       one is there leaves the program's next Read() blocking on a keypress
       and its screen unpainted until one arrives. */
    assert(SetMode(Input(), 1) == DOSTRUE);
    assert(WaitForChar(Input(), 0) == DOSFALSE);
    write_all(descriptors[1], "x", 1);
    assert(WaitForChar(Input(), 1000000) != 0);
    assert(Read(Input(), raw, sizeof(raw)) == 1);
    assert(raw[0] == 'x');
    assert(SetMode(Input(), 0) == DOSTRUE);

    /* Cooked input still finds it: a command with no arguments reads the
       empty argument line, which is what makes ReadArgs() report a missing
       /A argument rather than eating the next command. */
    assert(FGetC(Input()) == '\n');

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
