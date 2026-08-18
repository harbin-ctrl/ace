#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <dos/dos.h>
#include <proto/exec.h>

int main(int argc, char **argv)
{
    struct timespec pause = {0, 10000000L};
    int ticks = argc > 1 && strcmp(argv[1], "short") == 0 ? 50 : 500;
    int tick;

    puts("break-probe: ready");
    fflush(stdout);
    for (tick = 0; tick < ticks; tick++) {
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) {
            puts("break-probe: SIGBREAKF_CTRL_C received");
            return 0;
        }
        nanosleep(&pause, NULL);
    }
    fputs("break-probe: timed out waiting for Ctrl-C\n", stderr);
    return 20;
}
