/*
 * Drives the mediator channel without privilege.
 *
 * The channel, the handshake, the class refusals, and the lifetime are all
 * testable without anybody becoming root, and they should be: a test that
 * needs pkexec is a test that does not run, and an untested privileged
 * channel is worse than an unprivileged one.
 *
 * So the probe starts the mediator as itself and tells the client to expect a
 * peer with its own uid.  That is exactly what ace_mediator_start_as() exists
 * for, and why the expected uid is a parameter rather than an environment
 * variable: the relaxation lives in this file, which is not installed, rather
 * than in the shipped client where it would be reachable by anyone.
 */

#define _GNU_SOURCE

#include "ace_mediator_client.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static struct ace_mediator *start(const char *program, uint32_t caps)
{
    return ace_mediator_start_as(caps, getuid(), program, 0);
}

static int volume_probe(struct ace_mediator *mediator)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;

    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_VOLUME_MOUNT;
    if (ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                             NULL) != 0)
        return -1;
    return response.status;
}

int main(int argc, char **argv)
{
    const char *mode;
    const char *program;
    struct ace_mediator *mediator;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <mode> <ace-mediator-path>\n", argv[0]);
        return 2;
    }
    mode = argv[1];
    program = argv[2];

    if (strcmp(mode, "handshake") == 0) {
        mediator = start(program, ACE_MEDIATOR_CAP_VOLUME |
                                  ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        printf("granted=0x%x\n", ace_mediator_capabilities(mediator));
        printf("authorisation=%u\n",
               ace_mediator_authorisation_seconds(mediator));
        printf("ping=%s\n", ace_mediator_ping(mediator) == 0 ? "ok" : "failed");
        printf("pid=%s\n", ace_mediator_pid(mediator) > 0 ? "known" : "unknown");
        ace_mediator_close(mediator);
        printf("closed\n");
        return 0;
    }

    if (strcmp(mode, "partial") == 0) {
        /* Asked for access only.  A volume request must then be refused on
           the class check, before the mediator looks at anything it carried. */
        mediator = start(program, ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        printf("granted=0x%x\n", ace_mediator_capabilities(mediator));
        printf("volume=%d\n", volume_probe(mediator));
        ace_mediator_close(mediator);
        return 0;
    }

    if (strcmp(mode, "unsupported") == 0) {
        /* Holding the capability, the same request gets a different answer:
           contracted, not yet implemented. */
        mediator = start(program, ACE_MEDIATOR_CAP_VOLUME);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        printf("volume=%d\n", volume_probe(mediator));
        ace_mediator_close(mediator);
        return 0;
    }

    if (strcmp(mode, "drop") == 0) {
        mediator = start(program, ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        printf("drop=%s\n",
               ace_mediator_drop_privilege(mediator) == 0 ? "ok" : "failed");
        /* Spent.  A caller that keeps going gets a clean refusal rather than
           a second attempt on a channel whose far end has gone. */
        printf("ping-after=%s\n",
               ace_mediator_ping(mediator) == 0 ? "ok" : "failed");
        ace_mediator_close(mediator);
        return 0;
    }

    if (strcmp(mode, "death") == 0) {
        pid_t peer;

        mediator = start(program, ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        peer = ace_mediator_pid(mediator);
        /* Only a test may do this, and only because the mediator here is not
           actually privileged.  The broker never signals a mediator. */
        kill(peer, SIGKILL);
        while (waitpid(peer, NULL, 0) < 0 && errno == EINTR)
            ;
        printf("ping-after-death=%s\n",
               ace_mediator_ping(mediator) == 0 ? "ok" : "failed");
        ace_mediator_close(mediator);
        printf("survived\n");
        return 0;
    }

    if (strcmp(mode, "abandon") == 0) {
        /*
         * The case a crashed broker produces: nobody sends SHUTDOWN, nobody
         * closes anything politely, the process simply stops existing.  The
         * mediator has to notice by EOF on its channel and go, because a root
         * process outliving the session that authorised it is the exact thing
         * a session-scoped authorisation is supposed to prevent.
         *
         * _exit() rather than return, so no atexit handler tidies up on the
         * way out and makes this a polite close after all.
         */
        mediator = start(program, ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        printf("abandoned=%ld\n", (long)ace_mediator_pid(mediator));
        fflush(stdout);
        _exit(0);
    }

    fprintf(stderr, "%s: unknown mode %s\n", argv[0], mode);
    return 2;
}
