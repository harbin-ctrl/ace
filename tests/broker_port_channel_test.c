#define _POSIX_C_SOURCE 200809L

/*
 * The message-delivery channel, on its own.
 *
 * Nothing pushes down this channel yet -- PORT_PUT and PORT_REPLY are the
 * next step -- so what is testable here is the channel's own lifetime: that a
 * process gets exactly one, that the calls which race to open it agree, that
 * two processes get two, and that the broker lets go of one when its process
 * does.  The last of those is what makes an indefinite WaitPort() safe later,
 * so it is worth pinning down before anything depends on it.
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

static void record_sink(uint32_t operation, uint64_t message_id,
                        uint64_t port_id, const char *payload,
                        size_t payload_length, void *context)
{
    (void)operation; (void)message_id; (void)port_id;
    (void)payload; (void)payload_length; (void)context;
}

static void other_sink(uint32_t operation, uint64_t message_id,
                       uint64_t port_id, const char *payload,
                       size_t payload_length, void *context)
{
    (void)operation; (void)message_id; (void)port_id;
    (void)payload; (void)payload_length; (void)context;
}

/* Reads one \t-separated count out of a STATUS report. */
static long status_count(const char *field)
{
    char report[AMIGA_BROKER_MAX_PAYLOAD];
    char needle[64];
    const char *found;

    if (native_broker_status(report, sizeof(report)) != 0)
        return -1;
    snprintf(needle, sizeof(needle), "\n%s\t", field);
    found = strstr(report, needle);
    if (!found)
        return -1;
    return strtol(found + strlen(needle), NULL, 10);
}

/*
 * The broker notices a closed connection when it next polls, not at the
 * instant the process exits, so a count that is about to drop needs a moment
 * rather than an assertion on the first read.
 */
static int wait_for_count(const char *field, long expected)
{
    struct timespec delay = {0, 10000000L};

    for (int tries = 0; tries < 200; tries++) {
        if (status_count(field) == expected)
            return 0;
        nanosleep(&delay, NULL);
    }
    return -1;
}

int main(void)
{
    uint64_t channel = 0;
    uint64_t again = 0;
    uint64_t task_id = 0;
    long baseline;
    int ready[2];
    int done[2];
    pid_t child;
    char byte;
    int status;
    unsigned long long child_channel = 0;

    if (native_broker_ensure() != 0) {
        fprintf(stderr, "FAIL: no broker\n");
        return 1;
    }

    check(native_broker_port_attach(NULL, NULL, &channel) != 0 &&
          errno == EINVAL, "a channel without a handler is refused");

    baseline = status_count("port-channels");
    check(baseline >= 0, "the broker reports a port-channel count");

    check(native_broker_port_attach(record_sink, NULL, &channel) == 0,
          "attaching a delivery channel succeeds");
    check(channel != 0, "the channel has an id");
    check(wait_for_count("port-channels", baseline + 1) == 0,
          "the broker is holding one more channel");

    /* Idempotent: whichever port call attaches first, the others follow. */
    check(native_broker_port_attach(record_sink, NULL, &again) == 0,
          "re-attaching with the same handler succeeds");
    check(again == channel, "re-attaching returns the same channel");
    check(wait_for_count("port-channels", baseline + 1) == 0,
          "re-attaching did not open a second channel");

    /* Two handlers in one process would mean two owners of one stream. */
    again = 0;
    check(native_broker_port_attach(other_sink, NULL, &again) != 0 &&
          errno == EBUSY, "a second, different handler is refused");
    check(native_broker_port_attach(record_sink, NULL, &again) == 0 &&
          again == channel, "the refusal left the channel intact");

    /*
     * Registering a name, finding it, and giving it up. Here because this is
     * the only test that stands a private broker up for the port subsystem,
     * and because remove reporting success is not something to take on trust:
     * it returned ENAMETOOLONG on every successful call until the reply path
     * stopped treating "caller wants no payload" as "caller's buffer is too
     * small".
     */
    {
        uint64_t port_id = 0;
        uint64_t found = 0;

        check(native_broker_port_add("broker-port-channel-test-port",
                                     &port_id) == 0 && port_id != 0,
              "a port name can be registered");
        check(native_broker_port_find("broker-port-channel-test-port",
                                      &found) == 0 && found == port_id,
              "the registered name resolves to the same id");
        check(native_broker_port_remove(port_id) == 0,
              "removing a port reports success");
        found = 0;
        check(native_broker_port_find("broker-port-channel-test-port",
                                      &found) != 0,
              "the name is gone after removal");
    }

    /* The task channel is a separate connection and must not collide. */
    check(native_broker_task_attach("broker-port-channel-test",
                                    NULL, NULL, &task_id) != 0,
          "task attach still validates its own arguments");

    /* A second process gets a channel of its own, and gives it back. */
    if (pipe(ready) != 0 || pipe(done) != 0)
        return 1;
    child = fork();
    if (child < 0)
        return 1;
    if (child == 0) {
        uint64_t mine = 0;
        char message[32];

        close(ready[0]);
        close(done[1]);
        /* The inherited connections belong to the parent. */
        native_broker_reset_after_fork();
        if (native_broker_ensure() != 0 ||
            native_broker_port_attach(record_sink, NULL, &mine) != 0)
            _exit(2);
        snprintf(message, sizeof(message), "%llu\n",
                 (unsigned long long)mine);
        if (write(ready[1], message, strlen(message)) < 0)
            _exit(3);
        /* Hold the channel open until the parent has looked at it. */
        (void)read(done[0], &byte, 1);
        _exit(0);
    }
    close(ready[1]);
    close(done[0]);
    {
        char message[32] = {0};
        ssize_t got = read(ready[0], message, sizeof(message) - 1);

        check(got > 0, "the child reported its channel");
        child_channel = strtoull(message, NULL, 10);
    }
    check(child_channel != 0, "the child got a channel");
    check(child_channel != channel, "the child's channel is its own");
    check(wait_for_count("port-channels", baseline + 2) == 0,
          "the broker is holding both channels");

    (void)write(done[1], "G", 1);
    close(done[1]);
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        fprintf(stderr, "FAIL: child exited badly (status %d)\n", status);
        failures++;
    }
    check(wait_for_count("port-channels", baseline + 1) == 0,
          "the broker released the channel when its process exited");

    close(ready[0]);
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("broker port channel: ok\n");
    return failures ? 1 : 0;
}
