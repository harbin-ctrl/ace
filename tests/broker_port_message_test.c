#define _POSIX_C_SOURCE 200809L

/*
 * A message across two processes, and the reply back.
 *
 * This is the first thing to actually travel down the delivery channel, so it
 * is also the first real exercise of the reader thread from PORT_ATTACH.
 *
 * The payload deliberately contains NUL bytes and high bytes. An ARexx
 * argstring is counted bytes rather than a string, and until the broker's
 * request path stopped measuring payloads with strlen() this could not have
 * survived the trip -- so a round trip that only carried printable ASCII
 * would prove nothing about the thing most likely to be wrong.
 */

#include "broker_client.h"
#include "broker_protocol.h"

#include <errno.h>
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

/*
 * NUL in the middle, NUL at the end as part of the message rather than as a
 * terminator, and bytes above 0x7f. Written as an array rather than a string
 * literal so that every byte is exactly what it says: adjacent hex escapes in
 * a literal run together, which is its own trap.
 */
static const unsigned char sent_message[] = {
    'p', 'u', 't', 0x00, 0xff, 0xfe, 0x01, 'm', 'i', 'd', 0x00
};
#define SENT_LENGTH (sizeof(sent_message))
static const unsigned char sent_reply[] = {
    'r', 'e', 'p', 'l', 'y', 0x00, 0x80, 0x7f, 0xff, 'e', 'n', 'd', 0x00
};
#define REPLY_LENGTH (sizeof(sent_reply))

static volatile int delivered;
static char delivered_bytes[256];
static size_t delivered_length;
static volatile uint64_t delivered_id;

static volatile int replied;
static char replied_bytes[256];
static size_t replied_length;
static volatile uint64_t replied_id;

static void receiver_handler(uint32_t operation, uint64_t message_id,
                             uint64_t port_id, const char *payload,
                             size_t payload_length, int stdin_fd,
                             int stdout_fd, void *context)
{
    (void)stdin_fd; (void)stdout_fd;
    (void)port_id; (void)context;
    (void)stdin_fd; (void)stdout_fd;
    if (operation != AMIGA_BROKER_PORT_PUT ||
        payload_length > sizeof(delivered_bytes))
        return;
    memcpy(delivered_bytes, payload, payload_length);
    delivered_length = payload_length;
    delivered_id = message_id;
    delivered = 1;
}

static void sender_handler(uint32_t operation, uint64_t message_id,
                           uint64_t port_id, const char *payload,
                           size_t payload_length, int stdin_fd,
                           int stdout_fd, void *context)
{
    (void)port_id; (void)context;
    (void)stdin_fd; (void)stdout_fd;
    if (operation != AMIGA_BROKER_PORT_REPLY ||
        payload_length > sizeof(replied_bytes))
        return;
    memcpy(replied_bytes, payload, payload_length);
    replied_length = payload_length;
    replied_id = message_id;
    replied = 1;
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

#define PORT_NAME "broker-port-message-test"

int main(void)
{
    int ready[2];
    int go[2];
    pid_t child;
    uint64_t channel = 0;
    uint64_t message_id = 0;
    char byte;
    int status;

    if (native_broker_ensure() != 0) {
        fprintf(stderr, "FAIL: no broker\n");
        return 1;
    }

    /* Sending before this process can be replied to is refused rather than
       accepted into a wait that could never end. */
    check(native_broker_port_put(PORT_NAME, sent_message, SENT_LENGTH, -1, -1,
                                 &message_id) != 0 && errno == ENOTCONN,
          "sending without a delivery channel is refused");

    check(native_broker_port_attach(sender_handler, NULL, &channel) == 0,
          "the sender attaches a channel");

    /* No owner yet: this is the "is RexxMast running" case, and it has to be
       ESRCH so a caller can tell it from a real failure. */
    check(native_broker_port_put(PORT_NAME, sent_message, SENT_LENGTH, -1, -1,
                                 &message_id) != 0 && errno == ESRCH,
          "sending to an unregistered name reports ESRCH");

    if (pipe(ready) != 0 || pipe(go) != 0)
        return 1;
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        uint64_t mine = 0;
        uint64_t port_id = 0;
        char signal_byte;

        close(ready[0]);
        close(go[1]);
        native_broker_reset_after_fork();
        if (native_broker_ensure() != 0 ||
            native_broker_port_attach(receiver_handler, NULL, &mine) != 0 ||
            native_broker_port_add(PORT_NAME, &port_id) != 0)
            _exit(2);
        if (write(ready[1], "R", 1) != 1)
            _exit(3);
        if (wait_flag(&delivered) != 0)
            _exit(4);
        if (delivered_length != SENT_LENGTH ||
            memcmp(delivered_bytes, sent_message, SENT_LENGTH) != 0)
            _exit(5);
        /* Hold the reply until the sender has tried to answer its own
           message, so that check is not racing this one. */
        if (read(go[0], &signal_byte, 1) != 1)
            _exit(6);
        if (native_broker_port_reply(delivered_id, sent_reply,
                                     REPLY_LENGTH) != 0)
            _exit(7);
        _exit(0);
    }
    close(ready[1]);
    close(go[0]);
    if (read(ready[0], &byte, 1) != 1) {
        fprintf(stderr, "FAIL: the receiver never came up\n");
        return 1;
    }

    message_id = 0;
    check(native_broker_port_put(PORT_NAME, sent_message, SENT_LENGTH, -1, -1,
                                 &message_id) == 0 && message_id != 0,
          "the message is accepted and given an id");

    /* The sender is not the owner, so it must not be able to answer its own
       message and satisfy its own wait. */
    check(native_broker_port_reply(message_id, sent_reply, REPLY_LENGTH) != 0
          && errno == EPERM,
          "only the process the message reached may reply to it");

    check(native_broker_port_reply(message_id + 100000, sent_reply,
                                   REPLY_LENGTH) != 0 && errno == ESRCH,
          "replying to an unknown id reports ESRCH");

    (void)write(go[1], "G", 1);
    close(go[1]);

    check(wait_flag(&replied) == 0, "the reply comes back");
    check(replied_id == message_id,
          "the reply carries the id the message was sent with");
    check(replied_length == REPLY_LENGTH,
          "the reply keeps its length, NULs included");
    check(replied_length == REPLY_LENGTH &&
          memcmp(replied_bytes, sent_reply, REPLY_LENGTH) == 0,
          "the reply arrives byte for byte");

    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        fprintf(stderr, "FAIL: receiver exited %d\n",
                WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        failures++;
    }

    close(ready[0]);
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("broker port message: ok\n");
    return failures ? 1 : 0;
}
