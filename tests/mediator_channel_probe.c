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
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static struct ace_mediator *start(const char *program, uint32_t caps)
{
    return ace_mediator_start_as(caps, getuid(), program, 0);
}

/* One typed access request naming a path relative to the view root.  Any
   descriptor that comes back belongs to the caller. */
static int access_open(struct ace_mediator *worker, uint32_t operation,
                       const char *path, int *received)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;

    *received = -1;
    memset(&request, 0, sizeof(request));
    request.operation = operation;
    request.payload_length = (uint32_t)strlen(path) + 1;
    if (ace_mediator_request(worker, &request, path, &response, NULL, 0,
                             received) != 0)
        return -1;
    return response.status;
}

/* One typed mount request: the device named, the type proposed. */
static int volume_mount(struct ace_mediator *mediator, const char *name,
                        const char *type)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;
    char payload[128];
    size_t name_length = strlen(name);
    size_t type_length = strlen(type);

    if (name_length + type_length + 2 > sizeof(payload))
        return -1;
    memcpy(payload, name, name_length + 1);
    memcpy(payload + name_length + 1, type, type_length + 1);
    memset(&request, 0, sizeof(request));
    request.operation = ACE_MEDIATOR_VOLUME_MOUNT;
    request.payload_length = (uint32_t)(name_length + type_length + 2);
    if (ace_mediator_request(mediator, &request, payload, &response, NULL, 0,
                             NULL) != 0)
        return -1;
    return response.status;
}

static int class_probe(struct ace_mediator *mediator, uint32_t operation)
{
    struct ace_mediator_request request;
    struct ace_mediator_response response;

    memset(&request, 0, sizeof(request));
    request.operation = operation;
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
        printf("volume=%d\n", class_probe(mediator, ACE_MEDIATOR_VOLUME_MOUNT));
        ace_mediator_close(mediator);
        return 0;
    }

    if (strcmp(mode, "unsupported") == 0) {
        /* Holding the capability, an operation that is contracted but not
           yet built answers differently from a class that was never granted.
           The opens are implemented now, so the operations still to come --
           mkdir among them -- are what carry this distinction. */
        mediator = start(program, ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        printf("access=%d\n", class_probe(mediator, ACE_MEDIATOR_ACCESS_MKDIR));
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

    if (strcmp(mode, "volume") == 0) {
        /*
         * The volume class, exercised for what it refuses.
         *
         * Most of this needs no privilege at all, because the checks that
         * matter happen before anything is asked of the kernel: a name is
         * validated before it is used to build a path, and a filesystem type
         * is checked against the mediator's own list rather than the one in
         * the request.  Those refusals are the security property, so they are
         * the ones worth pinning down.
         */
        struct ace_mediator_request request;
        struct ace_mediator_response response;
        char answer[PATH_MAX];

        mediator = start(program, ACE_MEDIATOR_CAP_VOLUME);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }

        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_INIT_NAMESPACE;
        if (ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                                 NULL) != 0)
            return 1;
        printf("namespace=%d\n", response.status);

        /* A name that is trying to be a path, not a device. */
        printf("badname=%d\n", volume_mount(mediator, "../../etc", "ext4"));
        /* A real name, a filesystem this build will not hand to the kernel. */
        printf("badtype=%d\n", volume_mount(mediator, "sda1", "btrfs"));
        /* A name with a dot in it is a name trying to mean something else. */
        printf("dotted=%d\n", volume_mount(mediator, "sda1.", "ext4"));
        /* Well formed, supported, and not a device that exists. */
        printf("absent=%d\n", volume_mount(mediator, "zzznotadevice", "ext4"));

        /* A payload with no terminator must fail as protocol, not as a path. */
        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_MOUNT;
        request.payload_length = 4;
        if (ace_mediator_request(mediator, &request, "sda1", &response, NULL, 0,
                                 NULL) != 0)
            return 1;
        printf("unterminated=%d\n", response.status);

        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_UNMOUNT;
        request.payload_length = (uint32_t)strlen("zzznotadevice") + 1;
        if (ace_mediator_request(mediator, &request, "zzznotadevice", &response,
                                 NULL, 0, NULL) != 0)
            return 1;
        printf("unmount-unknown=%d\n", response.status);

        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_LIST;
        if (ace_mediator_request(mediator, &request, NULL, &response, answer,
                                 sizeof(answer), NULL) != 0)
            return 1;
        printf("list=%d entries=%u\n", response.status,
               response.payload_length);

        ace_mediator_close(mediator);
        return 0;
    }

    if (strcmp(mode, "access") == 0) {
        /*
         * The two personalities as two processes.
         *
         * What is being demonstrated is not that the access worker declines
         * volume work -- it does, and says so -- but that it is a different
         * process, inside the same mount namespace, holding no channel to the
         * volume side.  The refusal is the stated answer; the separate pid
         * and the shared namespace are the structural one.
         */
        struct ace_mediator *worker;
        struct ace_mediator_request request;
        struct ace_mediator_response response;
        char volume_ns[64];
        char access_ns[64];
        char link[128];
        ssize_t length;

        mediator = start(program, ACE_MEDIATOR_CAP_VOLUME |
                                  ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_INIT_NAMESPACE;
        if (ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                                 NULL) != 0)
            return 1;
        printf("namespace=%d\n", response.status);

        worker = ace_mediator_access_worker(mediator);
        if (!worker) {
            printf("spawn=failed\n");
            ace_mediator_close(mediator);
            return 0;
        }
        printf("spawn=ok\n");
        printf("separate=%s\n",
               ace_mediator_pid(worker) != ace_mediator_pid(mediator) &&
               ace_mediator_pid(worker) > 0 ? "yes" : "no");
        printf("worker-ping=%s\n",
               ace_mediator_ping(worker) == 0 ? "ok" : "failed");
        /* The volume class, asked of a process that has no volume side. */
        printf("worker-volume=%d\n",
               class_probe(worker, ACE_MEDIATOR_VOLUME_MOUNT));
        /* No view has been prepared, so the worker holds no subtree to
           resolve inside and declines rather than guessing at one. */
        printf("worker-rootless=%d\n",
               class_probe(worker, ACE_MEDIATOR_ACCESS_STAT));

        snprintf(link, sizeof(link), "/proc/%ld/ns/mnt",
                 (long)ace_mediator_pid(mediator));
        length = readlink(link, volume_ns, sizeof(volume_ns) - 1);
        if (length > 0)
            volume_ns[length] = '\0';
        snprintf(link, sizeof(link), "/proc/%ld/ns/mnt",
                 (long)ace_mediator_pid(worker));
        length = readlink(link, access_ns, sizeof(access_ns) - 1);
        if (length > 0)
            access_ns[length] = '\0';
        printf("same-namespace=%s\n",
               strcmp(volume_ns, access_ns) == 0 ? "yes" : "no");
        /* And that it is genuinely a private one: this process -- standing in
           for the unprivileged broker -- is outside it.  Without this the
           check above would pass just as well if nobody had unshared
           anything. */
        {
            char own_ns[64];

            length = readlink("/proc/self/ns/mnt", own_ns, sizeof(own_ns) - 1);
            if (length > 0)
                own_ns[length] = '\0';
            printf("private=%s\n",
                   strcmp(own_ns, access_ns) == 0 ? "no" : "yes");
        }

        ace_mediator_close(worker);
        /* Closing the worker must not disturb the channel it came from. */
        printf("volume-alive=%s\n",
               ace_mediator_ping(mediator) == 0 ? "ok" : "failed");
        ace_mediator_close(mediator);
        return 0;
    }

    if (strcmp(mode, "openat") == 0) {
        /*
         * What the access worker will and will not open.
         *
         * The interesting cases are the escapes, and the symlink one most of
         * all: a check that inspected the string would pass "out/passwd"
         * happily and only discover where it led after following it.
         * openat2() applies the constraint during resolution, so there is no
         * such moment.
         */
        struct ace_mediator *worker;
        struct ace_mediator_request request;
        struct ace_mediator_response response;
        char root[PATH_MAX / 2];
        char scratch[PATH_MAX];
        int fd = -1;

        mediator = start(program, ACE_MEDIATOR_CAP_VOLUME |
                                  ACE_MEDIATOR_CAP_ACCESS);
        if (!mediator) {
            printf("start failed: %s\n", strerror(errno));
            return 1;
        }
        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_INIT_NAMESPACE;
        if (ace_mediator_request(mediator, &request, NULL, &response, NULL, 0,
                                 NULL) != 0)
            return 1;
        if (response.status != ACE_MEDIATOR_OK) {
            printf("namespace=%d\n", response.status);
            return 0;
        }
        memset(&request, 0, sizeof(request));
        request.operation = ACE_MEDIATOR_VOLUME_PREPARE_VIEW;
        if (ace_mediator_request(mediator, &request, NULL, &response, root,
                                 sizeof(root), NULL) != 0)
            return 1;
        if (response.status != ACE_MEDIATOR_OK) {
            printf("prepare=%d\n", response.status);
            return 0;
        }
        printf("prepare=0\n");

        /* Something to find, something to look inside, and a way out. */
        snprintf(scratch, sizeof(scratch), "%s/hello", root);
        {
            int made = open(scratch, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (made < 0 || write(made, "amiga\n", 6) != 6) {
                printf("setup failed: %s\n", strerror(errno));
                return 1;
            }
            close(made);
        }
        snprintf(scratch, sizeof(scratch), "%s/sub", root);
        if (mkdir(scratch, 0755) != 0 && errno != EEXIST) {
            printf("setup failed: %s\n", strerror(errno));
            return 1;
        }
        snprintf(scratch, sizeof(scratch), "%s/out", root);
        unlink(scratch);
        if (symlink("/etc", scratch) != 0) {
            printf("setup failed: %s\n", strerror(errno));
            return 1;
        }

        worker = ace_mediator_access_worker(mediator);
        if (!worker) {
            printf("spawn=failed\n");
            return 1;
        }

        /* An ordinary file, opened and then read here rather than there. */
        printf("read=%d\n", access_open(worker, ACE_MEDIATOR_ACCESS_OPEN_READ,
                                         "hello", &fd));
        if (fd >= 0) {
            char got[16] = {0};
            ssize_t length = read(fd, got, sizeof(got) - 1);

            printf("content=%s\n",
                   length == 6 && strcmp(got, "amiga\n") == 0 ? "ok" : "wrong");
            close(fd);
            fd = -1;
        } else {
            printf("content=none\n");
        }

        printf("dotdot=%d\n", access_open(worker,
                                           ACE_MEDIATOR_ACCESS_OPEN_READ,
                                           "../hello", &fd));
        printf("absolute=%d\n", access_open(worker,
                                             ACE_MEDIATOR_ACCESS_OPEN_READ,
                                             "/etc/passwd", &fd));
        /* The one a string check would wave through. */
        printf("symlink=%d\n", access_open(worker,
                                            ACE_MEDIATOR_ACCESS_OPEN_READ,
                                            "out/passwd", &fd));
        printf("dir=%d\n", access_open(worker, ACE_MEDIATOR_ACCESS_OPEN_DIR,
                                        "sub", &fd));
        if (fd >= 0) { close(fd); fd = -1; }
        printf("notdir=%d\n", access_open(worker, ACE_MEDIATOR_ACCESS_OPEN_DIR,
                                           "hello", &fd));
        printf("stat=%d\n", access_open(worker, ACE_MEDIATOR_ACCESS_STAT,
                                         "hello", &fd));
        if (fd >= 0) {
            struct stat information;

            printf("size=%s\n",
                   fstat(fd, &information) == 0 && information.st_size == 6
                       ? "ok" : "wrong");
            close(fd);
            fd = -1;
        } else {
            printf("size=none\n");
        }

        ace_mediator_close(worker);
        ace_mediator_close(mediator);
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
