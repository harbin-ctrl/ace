/*
 * The ACE mediator: the only part of ACE that is root.
 *
 * It has no HOME, no current directory of consequence, no configuration, no
 * GUI, no D-Bus connection, and no way to be asked to run a program.  Those
 * absences are the design, not an oversight.  Everything a person interacts
 * with -- the shell, the console, the commands, the broker -- stays the
 * user's own process, because a Unix session bus peer and the ownership of a
 * configuration directory follow a process's identity and cannot be borrowed.
 *
 * What arrives here is a typed request naming an operation and an object.
 * What goes back is a status.  This file's whole job is to keep that true.
 *
 * This is chunk B of the migration: the channel, the handshake, and the
 * lifetime, with no privileged operation served yet.  The volume and access
 * classes are refused with ACE_MEDIATOR_REFUSED, which is deliberate -- the
 * channel that root will speak over should be tested before anything
 * dangerous rides on it.
 */

#define _GNU_SOURCE

#include "ace_mediator_protocol.h"
#include "ace_mediator_volume.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <sys/un.h>
#include <unistd.h>

struct mediator_state {
    int fd;
    uid_t served_uid;
    pid_t broker_pid;
    uint32_t capabilities;
    uint32_t authorisation_seconds;
    /* Wall-clock point at which authorisation lapses, or zero for "until the
       session ends", which is the default and the normal case. */
    long long deadline_ms;
};

static long long now_ms(void)
{
    struct timespec instant;

    clock_gettime(CLOCK_MONOTONIC, &instant);
    return (long long)instant.tv_sec * 1000 + instant.tv_nsec / 1000000;
}

/*
 * Which user this mediator serves.
 *
 * pkexec publishes the invoking uid, and that is the authority: it is the
 * identity polkit actually authorised, not something the broker told us.  The
 * fallbacks matter only for a mediator started some other way -- a test, or a
 * development run -- and in that case serving the uid we are already running
 * as is the honest answer.
 */
static uid_t resolve_served_uid(void)
{
    const char *value = getenv("PKEXEC_UID");

    if (!value || !*value)
        value = getenv("SUDO_UID");
    if (value && *value) {
        char *end = NULL;
        unsigned long parsed = strtoul(value, &end, 10);

        if (end && *end == '\0')
            return (uid_t)parsed;
    }
    return getuid();
}

/*
 * Recover the nonce from the socket path we were launched with.
 *
 * The broker will prove it knows this same value.  What that establishes is
 * narrow and worth stating exactly: the peer is a process that could read a
 * name inside a directory only the served user may enter.  It is not, on its
 * own, proof of identity -- SO_PEERCRED below is what supplies that -- but it
 * closes the case of a mediator being pointed at a channel it was not started
 * for.
 */
static int nonce_from_path(const char *path, uint8_t *nonce)
{
    const char *name = strrchr(path, '/');
    const char *prefix = "mediator-";
    size_t index;

    name = name ? name + 1 : path;
    if (strncmp(name, prefix, strlen(prefix)) != 0)
        return -1;
    name += strlen(prefix);
    for (index = 0; index < ACE_MEDIATOR_NONCE_LENGTH; index++) {
        unsigned value;

        if (!isxdigit((unsigned char)name[index * 2]) ||
            !isxdigit((unsigned char)name[index * 2 + 1]))
            return -1;
        if (sscanf(name + index * 2, "%2x", &value) != 1)
            return -1;
        nonce[index] = (uint8_t)value;
    }
    return 0;
}

static int send_record(int fd, const void *header, size_t header_size,
                       const void *payload, size_t payload_length)
{
    struct iovec io[2];
    struct msghdr message;
    ssize_t sent;

    memset(&message, 0, sizeof(message));
    io[0].iov_base = (void *)header;
    io[0].iov_len = header_size;
    io[1].iov_base = (void *)payload;
    io[1].iov_len = payload_length;
    message.msg_iov = io;
    message.msg_iovlen = payload && payload_length ? 2 : 1;
    do {
        sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent < 0 ? -1 : 0;
}

static int reply(struct mediator_state *state, uint64_t request_id,
                 int32_t status, int host_errno, const void *payload,
                 uint32_t payload_length)
{
    struct ace_mediator_response response;

    memset(&response, 0, sizeof(response));
    response.magic = ACE_MEDIATOR_MAGIC;
    response.request_id = request_id;
    response.status = status;
    response.host_errno = host_errno;
    response.payload_length = payload_length;
    return send_record(state->fd, &response, sizeof(response), payload,
                       payload_length);
}

/*
 * The handshake, from the privileged side.
 *
 * Every check here is one this process makes for itself.  A root process that
 * believed what it was told about who it was talking to would be a root
 * process with a user-supplied idea of its own purpose.
 */
static int perform_handshake(struct mediator_state *state,
                             const uint8_t *expected_nonce)
{
    struct ace_mediator_hello hello;
    struct ace_mediator_hello_reply answer;
    struct ucred peer;
    socklen_t peer_size = sizeof(peer);
    ssize_t got;
    int32_t status = ACE_MEDIATOR_OK;

    do {
        got = recv(state->fd, &hello, sizeof(hello), 0);
    } while (got < 0 && errno == EINTR);
    if (got != (ssize_t)sizeof(hello))
        return -1;
    if (hello.magic != ACE_MEDIATOR_MAGIC ||
        hello.version != ACE_MEDIATOR_PROTOCOL_VERSION)
        status = ACE_MEDIATOR_PROTOCOL_ERROR;
    else if (memcmp(hello.nonce, expected_nonce,
                    ACE_MEDIATOR_NONCE_LENGTH) != 0)
        status = ACE_MEDIATOR_UNAUTHORISED;
    else if (getsockopt(state->fd, SOL_SOCKET, SO_PEERCRED, &peer,
                        &peer_size) != 0 || peer_size != sizeof(peer))
        status = ACE_MEDIATOR_PROTOCOL_ERROR;
    /* The peer must be the user this mediator was authorised for.  Not "some
       user", and not whoever the message claims to be from. */
    else if (peer.uid != state->served_uid)
        status = ACE_MEDIATOR_UNAUTHORISED;
    /* And it must be the process it says it is.  A broker_pid that disagrees
       with the kernel's view of the connection means the message was composed
       somewhere other than the process that sent it. */
    else if (hello.broker_pid != (int32_t)peer.pid)
        status = ACE_MEDIATOR_UNAUTHORISED;

    memset(&answer, 0, sizeof(answer));
    answer.magic = ACE_MEDIATOR_MAGIC;
    answer.version = ACE_MEDIATOR_PROTOCOL_VERSION;
    answer.status = status;
    if (status == ACE_MEDIATOR_OK) {
        /* Grant no more than was asked for, and no more than exists.  A
           capability the broker did not request is one it cannot have
           reasoned about. */
        state->capabilities = hello.requested_capabilities &
                              (ACE_MEDIATOR_CAP_VOLUME | ACE_MEDIATOR_CAP_ACCESS);
        state->broker_pid = peer.pid;
        answer.granted_capabilities = state->capabilities;
        answer.authorisation_seconds = state->authorisation_seconds;
        if (state->authorisation_seconds)
            state->deadline_ms = now_ms() +
                                 (long long)state->authorisation_seconds * 1000;
    }
    if (send_record(state->fd, &answer, sizeof(answer), NULL, 0) != 0)
        return -1;
    return status == ACE_MEDIATOR_OK ? 0 : -1;
}

/*
 * One request.  Returns 0 to keep serving, 1 to stop.
 *
 * The order of the checks is the point.  Class membership is decided before
 * the payload is looked at -- before, specifically, any path inside it is
 * examined -- so that a request from the wrong class is refused without this
 * process ever having interpreted a byte of what it carried.
 */
static int dispatch(struct mediator_state *state,
                    const struct ace_mediator_request *request,
                    const void *payload)
{
    uint32_t class_of = ACE_MEDIATOR_CLASS_OF(request->operation);

    if (request->magic != ACE_MEDIATOR_MAGIC) {
        reply(state, request->request_id, ACE_MEDIATOR_PROTOCOL_ERROR, 0,
              NULL, 0);
        return 1;
    }
    if (request->payload_length > ACE_MEDIATOR_MAX_PAYLOAD) {
        reply(state, request->request_id, ACE_MEDIATOR_PROTOCOL_ERROR, 0,
              NULL, 0);
        return 1;
    }
    if (class_of == ACE_MEDIATOR_CLASS_VOLUME &&
        !(state->capabilities & ACE_MEDIATOR_CAP_VOLUME)) {
        reply(state, request->request_id, ACE_MEDIATOR_REFUSED, 0, NULL, 0);
        return 0;
    }
    if (class_of == ACE_MEDIATOR_CLASS_ACCESS &&
        !(state->capabilities & ACE_MEDIATOR_CAP_ACCESS)) {
        reply(state, request->request_id, ACE_MEDIATOR_REFUSED, 0, NULL, 0);
        return 0;
    }

    switch (request->operation) {
    case ACE_MEDIATOR_PING:
        reply(state, request->request_id, ACE_MEDIATOR_OK, 0, NULL, 0);
        return 0;
    case ACE_MEDIATOR_CAPS: {
        uint32_t granted = state->capabilities;

        reply(state, request->request_id, ACE_MEDIATOR_OK, 0, &granted,
              sizeof(granted));
        return 0;
    }
    case ACE_MEDIATOR_CANCEL:
        /* Nothing is ever in flight yet: this build serves one request at a
           time and answers before reading the next.  Answering OK is honest
           -- the named request is not running -- and keeps the broker's
           break path exercised from the beginning rather than bolted on
           beside the first long operation. */
        reply(state, request->request_id, ACE_MEDIATOR_OK, 0, NULL, 0);
        return 0;
    case ACE_MEDIATOR_DROP_PRIVILEGE:
        /* The user asked for their privilege back.  Answer first, so they
           are told it happened, then go. */
        reply(state, request->request_id, ACE_MEDIATOR_OK, 0, NULL, 0);
        return 1;
    case ACE_MEDIATOR_SHUTDOWN:
        reply(state, request->request_id, ACE_MEDIATOR_OK, 0, NULL, 0);
        return 1;
    default:
        break;
    }

    if (class_of == ACE_MEDIATOR_CLASS_VOLUME) {
        char answer[ACE_MEDIATOR_MAX_PAYLOAD];
        size_t answer_length = 0;
        int host_errno = 0;
        int status = ace_mediator_volume_dispatch(request, payload,
                                                  state->served_uid, answer,
                                                  sizeof(answer),
                                                  &answer_length, &host_errno);

        reply(state, request->request_id, status, host_errno, answer,
              (uint32_t)answer_length);
        return 0;
    }
    /* Access operations are contracted but not yet implemented.  Refused
       rather than silently accepted, so a broker built ahead of its mediator
       finds out by being told. */
    if (class_of == ACE_MEDIATOR_CLASS_ACCESS) {
        reply(state, request->request_id, ACE_MEDIATOR_UNSUPPORTED, ENOSYS,
              NULL, 0);
        return 0;
    }
    reply(state, request->request_id, ACE_MEDIATOR_REFUSED, 0, NULL, 0);
    return 0;
}

static void serve(struct mediator_state *state)
{
    for (;;) {
        struct ace_mediator_request request;
        unsigned char payload[ACE_MEDIATOR_MAX_PAYLOAD];
        struct iovec io[2];
        struct msghdr message;
        struct pollfd waiting;
        ssize_t got;
        int timeout = -1;
        int ready;

        if (state->deadline_ms) {
            long long remaining = state->deadline_ms - now_ms();

            if (remaining <= 0)
                return; /* Authorisation lapsed.  Leave quietly. */
            timeout = (int)remaining;
        }
        waiting.fd = state->fd;
        waiting.events = POLLIN;
        waiting.revents = 0;
        do {
            ready = poll(&waiting, 1, timeout);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0)
            return;

        memset(&message, 0, sizeof(message));
        io[0].iov_base = &request;
        io[0].iov_len = sizeof(request);
        io[1].iov_base = payload;
        io[1].iov_len = sizeof(payload);
        message.msg_iov = io;
        message.msg_iovlen = 2;
        do {
            got = recvmsg(state->fd, &message, 0);
        } while (got < 0 && errno == EINTR);
        /*
         * Zero means the broker is gone.  That is the ordinary way this
         * process ends: a broker that crashed cannot send SHUTDOWN, so EOF
         * has to mean the same thing, and it is checked before anything else
         * because there is nothing else it could mean.
         */
        if (got <= 0)
            return;
        if ((size_t)got < sizeof(request))
            return;
        if (dispatch(state, &request, payload))
            return;
    }
}

int main(int argc, char **argv)
{
    struct mediator_state state;
    struct sockaddr_un address;
    uint8_t expected_nonce[ACE_MEDIATOR_NONCE_LENGTH];
    const char *timeout_text;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <channel-socket>\n", argv[0]);
        return 2;
    }
    if (strlen(argv[1]) >= sizeof(address.sun_path)) {
        fprintf(stderr, "ace-mediator: channel path too long\n");
        return 2;
    }

    memset(&state, 0, sizeof(state));
    state.fd = -1;
    state.served_uid = resolve_served_uid();

    if (nonce_from_path(argv[1], expected_nonce) != 0) {
        fprintf(stderr, "ace-mediator: channel name is not a mediator "
                        "rendezvous\n");
        return 2;
    }

    /*
     * An optional lapse.  Off unless asked for: the decision on record is
     * that --root plus one authentication lasts the session, because being
     * asked again and again to use your own computer is what ACE is trying
     * not to feel like.
     */
    timeout_text = getenv("ACE_MEDIATOR_TIMEOUT");
    if (timeout_text && *timeout_text) {
        char *end = NULL;
        unsigned long parsed = strtoul(timeout_text, &end, 10);

        if (end && *end == '\0' && parsed <= 86400u)
            state.authorisation_seconds = (uint32_t)parsed;
    }

    /* No terminal, no controlling process group, no inherited working
       directory holding a mount busy.  A root process should be reachable
       only through its channel. */
    signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    if (chdir("/") != 0)
        return 1;
    umask(022);

    state.fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (state.fd < 0) {
        perror("ace-mediator: socket");
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, argv[1]);
    if (connect(state.fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("ace-mediator: connect");
        close(state.fd);
        return 1;
    }
    if (perform_handshake(&state, expected_nonce) != 0) {
        /* Deliberately terse.  A handshake that failed did so for one of a
           few reasons, none of which a stranger should be helped to tell
           apart. */
        fprintf(stderr, "ace-mediator: refused\n");
        close(state.fd);
        return 1;
    }

    serve(&state);
    /* Whichever way we leave -- SHUTDOWN, dropped privilege, a lapsed
       authorisation, or a broker that simply stopped existing -- the mounts
       this process made are its own to take down. */
    ace_mediator_volume_shutdown();
    close(state.fd);
    return 0;
}
