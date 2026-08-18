#define _POSIX_C_SOURCE 200809L

#include "broker_client.h"

#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static volatile uint32_t received;

static void receive_signal(uint32_t signals, void *context)
{
    (void)context;
    received |= signals;
}

static int wait_for(volatile uint32_t *value, uint32_t expected)
{
    struct timespec delay = {0, 10000000L};

    for (int tries = 0; tries < 200; tries++) {
        if ((*value & expected) == expected)
            return 0;
        nanosleep(&delay, NULL);
    }
    return -1;
}

int main(void)
{
    int ready[2];
    pid_t child;
    uint64_t task_id = 0;
    char byte;
    int status;

    if (pipe(ready) != 0)
        return 1;
    (void)fcntl(ready[0], F_SETFD, fcntl(ready[0], F_GETFD) | FD_CLOEXEC);
    (void)fcntl(ready[1], F_SETFD, fcntl(ready[1], F_GETFD) | FD_CLOEXEC);
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        uint64_t ignored;

        close(ready[0]);
        if (native_broker_ensure() != 0 ||
            native_broker_task_attach("broker-task-test-target", receive_signal,
                                      NULL, &ignored) != 0)
            _exit(2);
        (void)write(ready[1], "R", 1);
        _exit(wait_for(&received, 0x80000001u) == 0 ? 0 : 3);
    }
    close(ready[1]);
    if (read(ready[0], &byte, 1) != 1 ||
        native_broker_ensure() != 0)
        return 1;
    for (int tries = 0; tries < 50; tries++) {
        if (native_broker_task_find("broker-task-test-target", &task_id) == 0)
            break;
        { struct timespec delay = {0, 10000000L}; nanosleep(&delay, NULL); }
    }
    if (!task_id || native_broker_task_signal(task_id, 0x80000001u) != 0)
        return 1;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return 1;
    puts("broker task signal test: ok");
    return 0;
}
