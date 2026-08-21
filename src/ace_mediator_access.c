/*
 * The access worker: one protected object operation at a time.
 *
 * A separate process from the volume worker, and separate on purpose.  It is
 * forked after the mount namespace exists, so it is inside it and can see the
 * device view; it closes the supervisor's channel on the way in, so it has no
 * route back to the volume side.  There is no capability bit here saying "may
 * not mount" -- there is simply nothing to mount with and nobody to ask.
 *
 * Its reason for existing is that an unprivileged broker cannot enter a
 * root-owned mount namespace.  setns() with CLONE_NEWNS requires CAP_SYS_ADMIN
 * in the user namespace owning the target, which a user process does not have
 * and cannot get.  So the user's side never enters: this process opens the
 * object and passes the descriptor out, and a descriptor carries no namespace
 * with it.  The bytes are then read and written by the ordinary user process
 * that asked, which is also what keeps a large Copy from becoming a large
 * number of round trips.
 *
 * This is chunk C: the process, its isolation, and its refusals.  The typed
 * file operations themselves are chunk D, and until then they answer
 * UNSUPPORTED rather than pretending.
 */

#define _GNU_SOURCE

#include "ace_mediator_access.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/openat2.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SYS_openat2
#define SYS_openat2 437
#endif

/* The directory every path in this process is resolved beneath.  Opened once,
   at start, and never re-derived from a string: a subtree that is looked up
   again each time is a subtree that can change between the check and the
   use. */
static int view_root_fd = -1;

/*
 * Resolve one relative path beneath the view root, and nothing else.
 *
 * RESOLVE_BENEATH is what makes "..", an absolute path, and a symlink
 * pointing out of the tree into refusals rather than into a walk that happens
 * to end up somewhere else.  RESOLVE_NO_MAGICLINKS stops the magic links
 * under procfs, the descriptor ones especially, from being a way back out.  Symlinks *within* the tree
 * still work, because a Linux symlink keeps its Linux meaning inside an ACE
 * volume and following one is what the user asked for.
 *
 * openat2() rather than a check followed by an open: the kernel applies the
 * constraint during resolution, so there is no window between deciding a path
 * is acceptable and using it.
 */
static int resolve_beneath(const char *relative, uint64_t flags,
                           int32_t *status)
{
    struct open_how how;
    int fd;

    memset(&how, 0, sizeof(how));
    how.flags = flags;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS;
    fd = (int)syscall(SYS_openat2, view_root_fd, relative, &how, sizeof(how));
    if (fd >= 0)
        return fd;
    /* EXDEV and ELOOP are what RESOLVE_BENEATH reports when a path tried to
       leave.  They are not ordinary failures and should not be reported as
       "denied" -- "it tried to get out" is a different sentence. */
    if (errno == EXDEV || errno == ELOOP)
        *status = ACE_MEDIATOR_ESCAPED;
    else
        *status = ACE_MEDIATOR_HOST_ERROR;
    return -1;
}

/* A path this worker will consider: relative, terminated, and not empty.  An
   absolute path is refused outright rather than trimmed, because a caller
   that sent one meant something this interface does not do. */
static const char *checked_relative(const void *payload, uint32_t length)
{
    const char *bytes = payload;

    if (!bytes || length < 2 || bytes[length - 1] != '\0')
        return NULL;
    if (bytes[0] == '/' || bytes[0] == '\0')
        return NULL;
    if (strnlen(bytes, length) != length - 1)
        return NULL;
    return bytes;
}

/* Reply carrying the descriptor the operation produced.  The worker never
   serialises metadata: it hands back an open handle and lets the ordinary
   user process do the interpreting, which is both fewer bytes of protocol to
   get wrong and the thing that keeps bulk I/O out of this process. */
static int send_reply_fd(int fd, uint64_t request_id, int passed_fd)
{
    struct ace_mediator_response response;
    struct iovec io;
    struct msghdr message;
    union {
        char bytes[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } control;
    struct cmsghdr *entry;
    ssize_t sent;

    memset(&response, 0, sizeof(response));
    response.magic = ACE_MEDIATOR_MAGIC;
    response.request_id = request_id;
    response.status = ACE_MEDIATOR_OK;
    response.flags = ACE_MEDIATOR_FLAG_HAS_FD;

    memset(&message, 0, sizeof(message));
    io.iov_base = &response;
    io.iov_len = sizeof(response);
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control.bytes;
    message.msg_controllen = sizeof(control.bytes);
    entry = CMSG_FIRSTHDR(&message);
    entry->cmsg_level = SOL_SOCKET;
    entry->cmsg_type = SCM_RIGHTS;
    entry->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(entry), &passed_fd, sizeof(passed_fd));
    do {
        sent = sendmsg(fd, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent < 0 ? -1 : 0;
}

static int send_reply(int fd, uint64_t request_id, int32_t status,
                      int host_errno)
{
    struct ace_mediator_response response;
    ssize_t sent;

    memset(&response, 0, sizeof(response));
    response.magic = ACE_MEDIATOR_MAGIC;
    response.request_id = request_id;
    response.status = status;
    response.host_errno = host_errno;
    do {
        sent = send(fd, &response, sizeof(response), MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent < 0 ? -1 : 0;
}

/*
 * One typed operation on one exact object.
 *
 * Every one of these opens something and hands the descriptor back.  There is
 * no operation that reads, writes, lists, or copies: those happen in the
 * user's own process, on the handle this returned.  The privileged part is
 * the single open() that needed to be privileged, and what crosses back is a
 * capability to one object rather than the power to reach others.
 */
static int perform(int channel, const struct ace_mediator_request *request,
                   const void *payload)
{
    const char *relative;
    int32_t status = ACE_MEDIATOR_HOST_ERROR;
    uint64_t flags;
    int opened;

    switch (request->operation) {
    case ACE_MEDIATOR_ACCESS_OPEN_READ:
        flags = O_RDONLY | O_CLOEXEC;
        break;
    case ACE_MEDIATOR_ACCESS_OPEN_DIR:
        flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        break;
    case ACE_MEDIATOR_ACCESS_STAT:
        /* O_PATH: enough to fstat through, and not enough to read.  Examine
           does not need the contents and should not be handed them. */
        flags = O_PATH | O_CLOEXEC;
        break;
    default:
        return send_reply(channel, request->request_id,
                          ACE_MEDIATOR_UNSUPPORTED, ENOSYS);
    }

    if (view_root_fd < 0)
        return send_reply(channel, request->request_id, ACE_MEDIATOR_REFUSED,
                          0);
    relative = checked_relative(payload, request->payload_length);
    if (!relative)
        return send_reply(channel, request->request_id,
                          ACE_MEDIATOR_PROTOCOL_ERROR, 0);

    opened = resolve_beneath(relative, flags, &status);
    if (opened < 0)
        return send_reply(channel, request->request_id, status, errno);
    if (send_reply_fd(channel, request->request_id, opened) != 0) {
        close(opened);
        return -1;
    }
    close(opened);
    return 0;
}

void ace_mediator_access_serve(int fd, uid_t served_uid, const char *view_root)
{
    (void)served_uid; /* Ownership of created files arrives with OPEN_WRITE. */

    if (view_root && *view_root)
        view_root_fd = open(view_root, O_PATH | O_DIRECTORY | O_CLOEXEC);

    for (;;) {
        struct ace_mediator_request request;
        unsigned char payload[ACE_MEDIATOR_MAX_PAYLOAD];
        struct iovec io[2];
        struct msghdr message;
        ssize_t got;

        memset(&message, 0, sizeof(message));
        io[0].iov_base = &request;
        io[0].iov_len = sizeof(request);
        io[1].iov_base = payload;
        io[1].iov_len = sizeof(payload);
        message.msg_iov = io;
        message.msg_iovlen = 2;
        do {
            got = recvmsg(fd, &message, 0);
        } while (got < 0 && errno == EINTR);
        /* EOF means the broker is gone.  Same rule as the supervisor: a
           process that crashed sends nothing, so silence has to mean it. */
        if (got <= 0 || (size_t)got < sizeof(request))
            return;
        if (request.magic != ACE_MEDIATOR_MAGIC ||
            request.payload_length > ACE_MEDIATOR_MAX_PAYLOAD) {
            send_reply(fd, request.request_id, ACE_MEDIATOR_PROTOCOL_ERROR, 0);
            return;
        }

        switch (request.operation) {
        case ACE_MEDIATOR_PING:
            send_reply(fd, request.request_id, ACE_MEDIATOR_OK, 0);
            continue;
        case ACE_MEDIATOR_SHUTDOWN:
        case ACE_MEDIATOR_DROP_PRIVILEGE:
            send_reply(fd, request.request_id, ACE_MEDIATOR_OK, 0);
            return;
        case ACE_MEDIATOR_CANCEL:
            send_reply(fd, request.request_id, ACE_MEDIATOR_OK, 0);
            continue;
        default:
            break;
        }

        /*
         * Anything outside the access class is refused here, even though the
         * absence of a volume channel already makes it impossible.  Two
         * answers to the same question is the intent: one is structural and
         * one is stated, and a reader of either should reach the same
         * conclusion about what this process can do.
         */
        if (ACE_MEDIATOR_CLASS_OF(request.operation) !=
            ACE_MEDIATOR_CLASS_ACCESS) {
            send_reply(fd, request.request_id, ACE_MEDIATOR_REFUSED, 0);
            continue;
        }
        if (perform(fd, &request, payload) != 0)
            return;
    }
}
