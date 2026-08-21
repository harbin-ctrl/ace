/*
 * The ACE fmm: the only part of ACE that is root.
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
 * classes are refused with ACE_PRIVILEGE_REFUSED, which is deliberate -- the
 * channel that root will speak over should be tested before anything
 * dangerous rides on it.
 */

#define _GNU_SOURCE

#include "ace_privilege_protocol.h"
#include "ace_crm.h"
#include "ace_fmm_volume.h"

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

struct fmm_state {
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
 * Which user this fmm serves.
 *
 * pkexec publishes the invoking uid, and that is the authority: it is the
 * identity polkit actually authorised, not something the broker told us.  The
 * fallbacks matter only for a fmm started some other way -- a test, or a
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
 * closes the case of a fmm being pointed at a channel it was not started
 * for.
 */
static int nonce_from_path(const char *path, uint8_t *nonce)
{
    const char *name = strrchr(path, '/');
    const char *prefix = "fmm-";
    size_t index;

    name = name ? name + 1 : path;
    if (strncmp(name, prefix, strlen(prefix)) != 0)
        return -1;
    name += strlen(prefix);
    for (index = 0; index < ACE_PRIVILEGE_NONCE_LENGTH; index++) {
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

static int reply(struct fmm_state *state, uint64_t request_id,
                 int32_t status, int host_errno, const void *payload,
                 uint32_t payload_length)
{
    struct ace_privilege_response response;

    memset(&response, 0, sizeof(response));
    response.magic = ACE_PRIVILEGE_MAGIC;
    response.request_id = request_id;
    response.status = status;
    response.host_errno = host_errno;
    response.payload_length = payload_length;
    return send_record(state->fd, &response, sizeof(response), payload,
                       payload_length);
}

/*
 * Reply carrying one descriptor.
 *
 * SCM_RIGHTS has to ride along with real data, so the response header is the
 * data it accompanies.  The flag says a descriptor is present rather than
 * leaving the receiver to discover it, because "no descriptor" and "a
 * descriptor I failed to notice" must not look alike on a privileged channel.
 */
static int reply_with_fd(struct fmm_state *state, uint64_t request_id,
                         int passed_fd, int32_t worker_pid)
{
    struct ace_privilege_response response;
    struct iovec io[2];
    struct msghdr message;
    union {
        char bytes[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } control;
    struct cmsghdr *entry;
    ssize_t sent;

    memset(&response, 0, sizeof(response));
    response.magic = ACE_PRIVILEGE_MAGIC;
    response.request_id = request_id;
    response.status = ACE_PRIVILEGE_OK;
    response.flags = ACE_PRIVILEGE_FLAG_HAS_FD;
    response.payload_length = sizeof(worker_pid);

    memset(&message, 0, sizeof(message));
    io[0].iov_base = &response;
    io[0].iov_len = sizeof(response);
    io[1].iov_base = &worker_pid;
    io[1].iov_len = sizeof(worker_pid);
    message.msg_iov = io;
    message.msg_iovlen = 2;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    entry = CMSG_FIRSTHDR(&message);
    entry->cmsg_level = SOL_SOCKET;
    entry->cmsg_type = SCM_RIGHTS;
    entry->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(entry), &passed_fd, sizeof(passed_fd));
    do {
        sent = sendmsg(state->fd, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent < 0 ? -1 : 0;
}

/*
 * Fork the CRM into this process's mount namespace.
 *
 * Order matters and is the whole point.  The namespace has to exist first, so
 * that the child inherits it and can see the device view; the child then
 * closes the supervisor's own channel, so that it holds no way to reach the
 * volume side.  What comes back to the broker is one end of a fresh
 * socketpair -- a second, independent conversation with a process that can
 * open files and cannot mount them.
 */
static int spawn_crm(struct fmm_state *state,
                               uint64_t request_id)
{
    int pair[2];
    pid_t child;

    /*
     * A namespace is not required.
     *
     * It was, while the device view was the only thing an CRM could
     * reach: a worker outside the namespace would have been unable to see the
     * one tree it existed for.  Now that protected host paths are the other
     * half of its job, a worker without a namespace is a perfectly good
     * worker -- it simply has no device view to offer, and says so by
     * refusing view-domain requests for want of a root to resolve them under.
     *
     * This matters for the ordinary case: a session with --root and no device
     * view wants exactly this worker, and refusing to make one would mean
     * escalation only worked for people who had also asked for a device view.
     */
    if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, pair) != 0) {
        reply(state, request_id, ACE_PRIVILEGE_HOST_ERROR, errno, NULL, 0);
        return 0;
    }
    child = fork();
    if (child < 0) {
        int failure = errno;

        close(pair[0]);
        close(pair[1]);
        reply(state, request_id, ACE_PRIVILEGE_HOST_ERROR, failure, NULL, 0);
        return 0;
    }
    if (child == 0) {
        close(pair[0]);
        /* The supervisor's channel is not this process's business.  Closing
           it is what makes the isolation structural rather than declared. */
        close(state->fd);
        ace_crm_serve(pair[1], state->served_uid,
                                  ace_fmm_volume_view_root());
        _exit(0);
    }
    close(pair[1]);
    /*
     * The child's pid travels in the payload rather than being read from the
     * socket's peer credentials, because a socketpair reports the credentials
     * of whoever created it -- this process -- and not of the child that ends
     * up using it.  Asking the kernel there would return the supervisor and
     * look like a correct answer.
     */
    if (reply_with_fd(state, request_id, pair[0], (int32_t)child) != 0) {
        close(pair[0]);
        return 1;
    }
    /* The broker owns it now. */
    close(pair[0]);
    return 0;
}

/*
 * The handshake, from the privileged side.
 *
 * Every check here is one this process makes for itself.  A root process that
 * believed what it was told about who it was talking to would be a root
 * process with a user-supplied idea of its own purpose.
 */
static int perform_handshake(struct fmm_state *state,
                             const uint8_t *expected_nonce)
{
    struct ace_privilege_hello hello;
    struct ace_privilege_hello_reply answer;
    struct ucred peer;
    socklen_t peer_size = sizeof(peer);
    ssize_t got;
    int32_t status = ACE_PRIVILEGE_OK;

    do {
        got = recv(state->fd, &hello, sizeof(hello), 0);
    } while (got < 0 && errno == EINTR);
    if (got != (ssize_t)sizeof(hello))
        return -1;
    if (hello.magic != ACE_PRIVILEGE_MAGIC ||
        hello.version != ACE_PRIVILEGE_PROTOCOL_VERSION)
        status = ACE_PRIVILEGE_PROTOCOL_ERROR;
    else if (memcmp(hello.nonce, expected_nonce,
                    ACE_PRIVILEGE_NONCE_LENGTH) != 0)
        status = ACE_PRIVILEGE_UNAUTHORISED;
    else if (getsockopt(state->fd, SOL_SOCKET, SO_PEERCRED, &peer,
                        &peer_size) != 0 || peer_size != sizeof(peer))
        status = ACE_PRIVILEGE_PROTOCOL_ERROR;
    /* The peer must be the user this fmm was authorised for.  Not "some
       user", and not whoever the message claims to be from. */
    else if (peer.uid != state->served_uid)
        status = ACE_PRIVILEGE_UNAUTHORISED;
    /* And it must be the process it says it is.  A broker_pid that disagrees
       with the kernel's view of the connection means the message was composed
       somewhere other than the process that sent it. */
    else if (hello.broker_pid != (int32_t)peer.pid)
        status = ACE_PRIVILEGE_UNAUTHORISED;

    memset(&answer, 0, sizeof(answer));
    answer.magic = ACE_PRIVILEGE_MAGIC;
    answer.version = ACE_PRIVILEGE_PROTOCOL_VERSION;
    answer.status = status;
    if (status == ACE_PRIVILEGE_OK) {
        /* Grant no more than was asked for, and no more than exists.  A
           capability the broker did not request is one it cannot have
           reasoned about. */
        state->capabilities = hello.requested_capabilities &
                              (ACE_PRIVILEGE_CAP_VOLUME | ACE_PRIVILEGE_CAP_ACCESS);
        state->broker_pid = peer.pid;
        answer.granted_capabilities = state->capabilities;
        answer.authorisation_seconds = state->authorisation_seconds;
        if (state->authorisation_seconds)
            state->deadline_ms = now_ms() +
                                 (long long)state->authorisation_seconds * 1000;
    }
    if (send_record(state->fd, &answer, sizeof(answer), NULL, 0) != 0)
        return -1;
    return status == ACE_PRIVILEGE_OK ? 0 : -1;
}

/*
 * One request.  Returns 0 to keep serving, 1 to stop.
 *
 * The order of the checks is the point.  Class membership is decided before
 * the payload is looked at -- before, specifically, any path inside it is
 * examined -- so that a request from the wrong class is refused without this
 * process ever having interpreted a byte of what it carried.
 */
static int dispatch(struct fmm_state *state,
                    const struct ace_privilege_request *request,
                    const void *payload)
{
    uint32_t class_of = ACE_PRIVILEGE_CLASS_OF(request->operation);

    if (request->magic != ACE_PRIVILEGE_MAGIC) {
        reply(state, request->request_id, ACE_PRIVILEGE_PROTOCOL_ERROR, 0,
              NULL, 0);
        return 1;
    }
    if (request->payload_length > ACE_PRIVILEGE_MAX_PAYLOAD) {
        reply(state, request->request_id, ACE_PRIVILEGE_PROTOCOL_ERROR, 0,
              NULL, 0);
        return 1;
    }
    if (class_of == ACE_PRIVILEGE_CLASS_VOLUME &&
        !(state->capabilities & ACE_PRIVILEGE_CAP_VOLUME)) {
        reply(state, request->request_id, ACE_PRIVILEGE_REFUSED, 0, NULL, 0);
        return 0;
    }
    if (class_of == ACE_PRIVILEGE_CLASS_ACCESS &&
        !(state->capabilities & ACE_PRIVILEGE_CAP_ACCESS)) {
        reply(state, request->request_id, ACE_PRIVILEGE_REFUSED, 0, NULL, 0);
        return 0;
    }

    switch (request->operation) {
    case ACE_PRIVILEGE_PING:
        reply(state, request->request_id, ACE_PRIVILEGE_OK, 0, NULL, 0);
        return 0;
    case ACE_PRIVILEGE_CAPS: {
        uint32_t granted = state->capabilities;

        reply(state, request->request_id, ACE_PRIVILEGE_OK, 0, &granted,
              sizeof(granted));
        return 0;
    }
    case ACE_PRIVILEGE_CANCEL:
        /* Nothing is ever in flight yet: this build serves one request at a
           time and answers before reading the next.  Answering OK is honest
           -- the named request is not running -- and keeps the broker's
           break path exercised from the beginning rather than bolted on
           beside the first long operation. */
        reply(state, request->request_id, ACE_PRIVILEGE_OK, 0, NULL, 0);
        return 0;
    case ACE_PRIVILEGE_DROP_PRIVILEGE:
        /* The user asked for their privilege back.  Answer first, so they
           are told it happened, then go. */
        reply(state, request->request_id, ACE_PRIVILEGE_OK, 0, NULL, 0);
        return 1;
    case ACE_PRIVILEGE_SPAWN_ACCESS:
        return spawn_crm(state, request->request_id);
    case ACE_PRIVILEGE_SHUTDOWN:
        reply(state, request->request_id, ACE_PRIVILEGE_OK, 0, NULL, 0);
        return 1;
    default:
        break;
    }

    if (class_of == ACE_PRIVILEGE_CLASS_VOLUME) {
        char answer[ACE_PRIVILEGE_MAX_PAYLOAD];
        size_t answer_length = 0;
        int host_errno = 0;
        int status = ace_fmm_volume_dispatch(request, payload,
                                                  state->served_uid, answer,
                                                  sizeof(answer),
                                                  &answer_length, &host_errno);

        reply(state, request->request_id, status, host_errno, answer,
              (uint32_t)answer_length);
        return 0;
    }
    /* Access operations are contracted but not yet implemented.  Refused
       rather than silently accepted, so a broker built ahead of its fmm
       finds out by being told. */
    if (class_of == ACE_PRIVILEGE_CLASS_ACCESS) {
        reply(state, request->request_id, ACE_PRIVILEGE_UNSUPPORTED, ENOSYS,
              NULL, 0);
        return 0;
    }
    reply(state, request->request_id, ACE_PRIVILEGE_REFUSED, 0, NULL, 0);
    return 0;
}

static void serve(struct fmm_state *state)
{
    for (;;) {
        struct ace_privilege_request request;
        unsigned char payload[ACE_PRIVILEGE_MAX_PAYLOAD];
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
    struct fmm_state state;
    struct sockaddr_un address;
    uint8_t expected_nonce[ACE_PRIVILEGE_NONCE_LENGTH];
    const char *timeout_text;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <channel-socket>\n", argv[0]);
        return 2;
    }
    if (strlen(argv[1]) >= sizeof(address.sun_path)) {
        fprintf(stderr, "ace-fmm: channel path too long\n");
        return 2;
    }

    memset(&state, 0, sizeof(state));
    state.fd = -1;
    state.served_uid = resolve_served_uid();

    if (nonce_from_path(argv[1], expected_nonce) != 0) {
        fprintf(stderr, "ace-fmm: channel name is not a fmm "
                        "rendezvous\n");
        return 2;
    }

    /*
     * An optional lapse.  Off unless asked for: the decision on record is
     * that --root plus one authentication lasts the session, because being
     * asked again and again to use your own computer is what ACE is trying
     * not to feel like.
     */
    timeout_text = getenv("ACE_PRIVILEGE_TIMEOUT");
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
        perror("ace-fmm: socket");
        return 1;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strcpy(address.sun_path, argv[1]);
    if (connect(state.fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        perror("ace-fmm: connect");
        close(state.fd);
        return 1;
    }
    if (perform_handshake(&state, expected_nonce) != 0) {
        /* Deliberately terse.  A handshake that failed did so for one of a
           few reasons, none of which a stranger should be helped to tell
           apart. */
        fprintf(stderr, "ace-fmm: refused\n");
        close(state.fd);
        return 1;
    }

    serve(&state);
    /* Whichever way we leave -- SHUTDOWN, dropped privilege, a lapsed
       authorisation, or a broker that simply stopped existing -- the mounts
       this process made are its own to take down. */
    ace_fmm_volume_shutdown();
    close(state.fd);
    return 0;
}
