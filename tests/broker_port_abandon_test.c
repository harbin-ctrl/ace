#define _POSIX_C_SOURCE 200809L

/*
 * What happens to a sender whose message can never be answered.
 *
 * The sender is in WaitPort(), which on AmigaOS is Wait(1 << mp_SigBit) with
 * no timeout and no SIGBREAKF_CTRL_C in the mask -- not even a break gets out
 * of it. ACE keeps that, so anything the broker fails to end explicitly waits
 * for the life of the process.
 *
 * The two halves of this test are equally important:
 *
 *   - A receiver that is alive and simply does not reply must produce
 *     NOTHING. That hangs the sender on a real Amiga too, and inventing a
 *     timeout here would be inventing behaviour AmigaOS never had.
 *   - A receiver that dies, or deletes the port, must produce an abandonment,
 *     because Linux can do that and AmigaOS could not.
 */

#include "broker_client.h"
#include "broker_protocol.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *what)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static volatile int abandoned;
static volatile uint64_t abandoned_id;
static volatile int replied;

static void sender_handler(uint32_t operation, uint64_t message_id,
                           uint64_t port_id, const char *payload,
                           size_t payload_length, int stdin_fd,
                           int stdout_fd, void *context)
{
    (void)stdin_fd; (void)stdout_fd;
    (void)port_id; (void)payload; (void)payload_length; (void)context;
    (void)stdin_fd; (void)stdout_fd;
    if (operation == AMIGA_BROKER_PORT_ABANDONED) {
        abandoned_id = message_id;
        abandoned = 1;
    } else if (operation == AMIGA_BROKER_PORT_REPLY) {
        replied = 1;
    }
}

static volatile int delivered;
static volatile uint64_t delivered_id;

static void receiver_handler(uint32_t operation, uint64_t message_id,
                             uint64_t port_id, const char *payload,
                             size_t payload_length, int stdin_fd,
                             int stdout_fd, void *context)
{
    (void)port_id; (void)payload; (void)payload_length; (void)context;
    (void)stdin_fd; (void)stdout_fd;
    if (operation == AMIGA_BROKER_PORT_PUT) {
        delivered_id = message_id;
        delivered = 1;
    }
}

static int wait_flag(volatile int *flag)
{
    struct timespec delay = {0, 10000000L};

    for (int tries = 0; tries < 500; tries++) {
        if (*flag)
            return 0;
        nanosleep(&delay, NULL);
    }
    return -1;
}

static void settle(void)
{
    struct timespec delay = {0, 300000000L};

    nanosleep(&delay, NULL);
}

/*
 * A receiver that takes the message and then does what it is told: either
 * nothing at all, or delete its port. Never replies either way.
 */
static void receiver(const char *port_name, int ready_fd, int go_fd,
                     int delete_port)
{
    uint64_t channel = 0;
    uint64_t port_id = 0;
    char byte;

    native_broker_reset_after_fork();
    if (native_broker_ensure() != 0 ||
        native_broker_port_attach(receiver_handler, NULL, &channel) != 0 ||
        native_broker_port_add(port_name, &port_id) != 0)
        _exit(2);
    if (write(ready_fd, "R", 1) != 1)
        _exit(3);
    if (wait_flag(&delivered) != 0)
        _exit(4);
    if (write(ready_fd, "D", 1) != 1)
        _exit(5);
    if (delete_port) {
        if (native_broker_port_remove(port_id) != 0)
            _exit(6);
        /* Stay alive, so that what releases the sender is the port going
           away and not this process going away. */
        if (read(go_fd, &byte, 1) != 1)
            _exit(7);
        _exit(0);
    }
    /* Hold the message and never answer it. */
    (void)read(go_fd, &byte, 1);
    _exit(0);
}

int main(void)
{
    uint64_t channel = 0;
    uint64_t message_id = 0;
    int ready[2];
    int go[2];
    pid_t child;
    char byte;
    int status;

    if (native_broker_ensure() != 0) {
        fprintf(stderr, "FAIL: no broker\n");
        return 1;
    }
    check(native_broker_port_attach(sender_handler, NULL, &channel) == 0,
          "the sender attaches a channel");

    /* --- a live receiver that never replies --- */
    if (pipe(ready) != 0 || pipe(go) != 0)
        return 1;
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        close(ready[0]);
        close(go[1]);
        receiver("broker-port-abandon-test", ready[1], go[0], 0);
    }
    close(ready[1]);
    close(go[0]);
    if (read(ready[0], &byte, 1) != 1)
        return 1;

    abandoned = 0;
    replied = 0;
    check(native_broker_port_put("broker-port-abandon-test", "x", 1, -1, -1,
                                 &message_id) == 0 && message_id != 0,
          "the message reaches the receiver");
    if (read(ready[0], &byte, 1) != 1 || byte != 'D') {
        fprintf(stderr, "FAIL: the receiver never saw the message\n");
        return 1;
    }

    /* The heart of it: the receiver is alive and silent, so nothing at all
       should happen. A timeout here would be an invention. */
    settle();
    check(!abandoned, "a live receiver that stays silent is not abandoned");
    check(!replied, "and nothing pretends to be a reply");

    kill(child, SIGKILL);
    (void)waitpid(child, &status, 0);
    check(wait_flag(&abandoned) == 0,
          "killing the receiver releases the sender");
    check(abandoned_id == message_id,
          "the abandonment names the message that was waiting");
    check(!replied, "abandonment does not arrive dressed as a reply");
    close(ready[0]);
    close(go[1]);

    /* --- a live receiver that deletes the port --- */
    if (pipe(ready) != 0 || pipe(go) != 0)
        return 1;
    delivered = 0;
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        close(ready[0]);
        close(go[1]);
        receiver("broker-port-abandon-test-2", ready[1], go[0], 1);
    }
    close(ready[1]);
    close(go[0]);
    if (read(ready[0], &byte, 1) != 1)
        return 1;

    abandoned = 0;
    abandoned_id = 0;
    message_id = 0;
    check(native_broker_port_put("broker-port-abandon-test-2", "y", 1, -1, -1,
                                 &message_id) == 0 && message_id != 0,
          "a second message reaches its receiver");
    if (read(ready[0], &byte, 1) != 1 || byte != 'D')
        return 1;
    check(wait_flag(&abandoned) == 0,
          "deleting the port releases the sender too");
    check(abandoned_id == message_id,
          "and names the right message");

    (void)write(go[1], "G", 1);
    close(go[1]);
    (void)waitpid(child, &status, 0);
    close(ready[0]);

    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("broker port abandon: ok\n");
    return failures ? 1 : 0;
}
