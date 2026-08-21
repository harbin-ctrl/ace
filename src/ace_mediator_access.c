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
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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

void ace_mediator_access_serve(int fd, uid_t served_uid)
{
    (void)served_uid; /* Used by the typed operations in chunk D. */

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
        send_reply(fd, request.request_id, ACE_MEDIATOR_UNSUPPORTED, ENOSYS);
    }
}
