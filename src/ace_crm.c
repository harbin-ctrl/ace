/*
 * The CRM: one protected object operation at a time.
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

#include "ace_crm.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <linux/openat2.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef SYS_openat2
#define SYS_openat2 437
#endif

/*
 * The two starting points, opened once and never re-derived from a string.
 *
 * A subtree looked up again on each request is a subtree that can change
 * between the check and the use; holding the descriptor removes that window
 * entirely, because every later resolution starts from the directory this
 * process already had open.
 */
static int view_root_fd = -1;
static int host_root_fd = -1;

/*
 * Resolve one path, in the domain the request named.
 *
 * A device-view path must stay inside its volume: RESOLVE_BENEATH makes "..",
 * an absolute path, and a symlink leading out of the tree into refusals
 * rather than into a walk that happens to end up elsewhere.
 *
 * A host path is ordinary Linux naming, where an absolute symlink is
 * meaningful and expected -- /etc/foo pointing at /var/lib/foo is not an
 * attack, it is Tuesday.  RESOLVE_IN_ROOT scopes those to the root this
 * process opened, which is the real one, so they resolve exactly as they
 * would for anybody else while ".." above / still goes nowhere.
 *
 * Both refuse magic links.  Neither domain has business being redirected
 * through somebody's descriptor table.
 *
 * openat2() rather than a check followed by an open: the kernel applies the
 * constraint during resolution, so there is no moment when a path has been
 * judged acceptable and not yet used.
 */
static int resolve_in_domain(const char *relative, uint32_t request_flags,
                             uint64_t flags, mode_t mode, int32_t *status)
{
    int host = (request_flags & ACE_PRIVILEGE_FLAG_HOST_PATH) != 0;
    int start = host ? host_root_fd : view_root_fd;
    struct open_how how;
    int fd;

    if (start < 0) {
        *status = ACE_PRIVILEGE_REFUSED;
        return -1;
    }
    memset(&how, 0, sizeof(how));
    how.flags = flags;
    how.mode = (flags & O_CREAT) ? mode : 0;
    how.resolve = (host ? RESOLVE_IN_ROOT : RESOLVE_BENEATH) |
                  RESOLVE_NO_MAGICLINKS;
    fd = (int)syscall(SYS_openat2, start, relative, &how, sizeof(how));
    if (fd >= 0)
        return fd;
    /* EXDEV and ELOOP are what the resolve flags report when a path tried to
       leave.  "It tried to get out" is a different sentence from "you may
       not", and the caller should be able to tell them apart. */
    if (errno == EXDEV || errno == ELOOP)
        *status = ACE_PRIVILEGE_ESCAPED;
    else
        *status = ACE_PRIVILEGE_HOST_ERROR;
    return -1;
}

/* A path this worker will consider: relative, terminated, and not empty.  A
   leading slash is refused rather than trimmed -- the domain is stated in the
   request flags, and a path that looks like it disagrees with the flag is a
   path whose author was confused about which one they were in. */
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

/*
 * The last component of a path, for the operations that act on a name rather
 * than on an open object.
 *
 * unlink, rename, mkdir and a protection change cannot be expressed as a
 * descriptor, so they are done as (parent directory, name) with the parent
 * resolved under the same constraints as everything else.  The name is then
 * checked for the three things it must not be: empty, a directory reference,
 * or a path in its own right.  Only the parent walk is resolution; the final
 * component never is.
 */
static int split_last(const char *path, char *directory, size_t size,
                      const char **name)
{
    const char *slash = strrchr(path, '/');

    if (!slash) {
        if (size < 2)
            return -1;
        strcpy(directory, ".");
        *name = path;
    } else {
        size_t length = (size_t)(slash - path);

        if (length == 0 || length + 1 > size)
            return -1;
        memcpy(directory, path, length);
        directory[length] = '\0';
        *name = slash + 1;
    }
    if (!**name || strcmp(*name, ".") == 0 || strcmp(*name, "..") == 0 ||
        strchr(*name, '/'))
        return -1;
    return 0;
}

/* Open the parent directory of a path, in the request's domain. */
static int open_parent(const char *path, uint32_t request_flags,
                       const char **name, char *directory, size_t size,
                       int32_t *status)
{
    if (split_last(path, directory, size, name) != 0) {
        *status = ACE_PRIVILEGE_PROTOCOL_ERROR;
        return -1;
    }
    return resolve_in_domain(directory, request_flags,
                             O_PATH | O_DIRECTORY | O_CLOEXEC, 0, status);
}

/* Reply carrying the descriptor the operation produced.  The worker never
   serialises metadata: it hands back an open handle and lets the ordinary
   user process do the interpreting, which is both fewer bytes of protocol to
   get wrong and the thing that keeps bulk I/O out of this process. */
static int send_reply_fd(int fd, uint64_t request_id, int passed_fd)
{
    struct ace_privilege_response response;
    struct iovec io;
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
    struct ace_privilege_response response;
    ssize_t sent;

    memset(&response, 0, sizeof(response));
    response.magic = ACE_PRIVILEGE_MAGIC;
    response.request_id = request_id;
    response.status = status;
    response.host_errno = host_errno;
    do {
        sent = send(fd, &response, sizeof(response), MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent < 0 ? -1 : 0;
}

/*
 * The operations that produce a descriptor.
 *
 * Each opens one object and hands the handle back.  There is nothing here
 * that reads, writes, lists or copies: those happen in the user's own process
 * on the returned descriptor, so the privileged part is the single open()
 * that had to be privileged, and what crosses back is a capability to one
 * object rather than the power to reach others.
 */
static int perform_open(int channel, const struct ace_privilege_request *request,
                        const char *relative)
{
    int32_t status = ACE_PRIVILEGE_HOST_ERROR;
    uint64_t flags;
    mode_t mode;
    int opened;

    switch (request->operation) {
    case ACE_PRIVILEGE_ACCESS_OPEN_READ:
        flags = O_RDONLY | O_CLOEXEC;
        break;
    case ACE_PRIVILEGE_ACCESS_OPEN_DIR:
        flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
        break;
    case ACE_PRIVILEGE_ACCESS_STAT:
        /* O_PATH: enough to fstat through, and not enough to read.  Examine
           does not need the contents and should not be handed them. */
        flags = O_PATH | O_CLOEXEC;
        break;
    case ACE_PRIVILEGE_ACCESS_OPEN_WRITE:
        flags = ((request->flags & ACE_PRIVILEGE_FLAG_UPDATE) ? O_RDWR
                                                             : O_WRONLY) |
                O_CLOEXEC;
        if (request->flags & ACE_PRIVILEGE_FLAG_CREATE)
            flags |= O_CREAT;
        if (request->flags & ACE_PRIVILEGE_FLAG_TRUNCATE)
            flags |= O_TRUNC;
        if (request->flags & ACE_PRIVILEGE_FLAG_APPEND)
            flags |= O_APPEND;
        if (request->flags & ACE_PRIVILEGE_FLAG_EXCLUSIVE)
            flags |= O_EXCL;
        break;
    default:
        return send_reply(channel, request->request_id,
                          ACE_PRIVILEGE_UNSUPPORTED, ENOSYS);
    }

    /*
     * A file created here is owned by root, as sudo cp would leave it.  ACE
     * does not invent an ownership rule and AmigaDOS has none to borrow.
     *
     * This only ever applies where the user could not have written anyway --
     * the seam tries as the user first and arrives here only on a refusal --
     * so the places it can happen are the places where root ownership is the
     * correct outcome and a user-owned file would be the anomaly.  It would
     * also be an escalation: a user-owned file in a system directory outlives
     * the session as permanent write access obtained from a temporary one.
     */
    mode = request->mode ? (mode_t)(request->mode & 07777) : 0644;
    opened = resolve_in_domain(relative, request->flags, flags, mode, &status);
    if (opened < 0)
        return send_reply(channel, request->request_id, status, errno);
    if (send_reply_fd(channel, request->request_id, opened) != 0) {
        close(opened);
        return -1;
    }
    close(opened);
    return 0;
}

/*
 * The operations that act on a name.
 *
 * unlink, rename, mkdir and a protection change have no descriptor to hand
 * back, so this worker performs exactly that operation and reports precisely
 * what happened.  These are the only cases where it acts rather than opens,
 * and the list is not to grow casually.
 */
static int perform_named(int channel,
                         const struct ace_privilege_request *request,
                         const char *relative)
{
    int32_t status = ACE_PRIVILEGE_HOST_ERROR;
    char directory[PATH_MAX];
    const char *name = NULL;
    int parent;
    int outcome;

    if (request->operation == ACE_PRIVILEGE_ACCESS_RENAME) {
        /*
         * Both halves in one request, resolved here, one after the other,
         * with nothing in between.  A rename whose halves were authorised
         * separately is not a rename: it is two operations with a window
         * between them, and the second one acts on whatever the name means by
         * the time it runs.
         */
        char from_directory[PATH_MAX];
        const char *from_name = NULL;
        const char *second;
        int from_parent;

        /* Two strings, each terminated within the length the header
           declared, each relative and non-empty.  Checked against the
           declared bounds rather than by looking for terminators and hoping
           they are there. */
        if (request->first_path_length < 2 ||
            request->first_path_length >= request->payload_length ||
            relative[request->first_path_length - 1] != '\0' ||
            relative[request->payload_length - 1] != '\0' ||
            !*relative || *relative == '/')
            return send_reply(channel, request->request_id,
                              ACE_PRIVILEGE_PROTOCOL_ERROR, 0);
        second = relative + request->first_path_length;
        if (!*second || *second == '/' ||
            strnlen(second, request->payload_length -
                            request->first_path_length) !=
                request->payload_length - request->first_path_length - 1)
            return send_reply(channel, request->request_id,
                              ACE_PRIVILEGE_PROTOCOL_ERROR, 0);

        from_parent = open_parent(relative, request->flags, &from_name,
                                  from_directory, sizeof(from_directory),
                                  &status);
        if (from_parent < 0)
            return send_reply(channel, request->request_id, status, errno);
        parent = open_parent(second, request->flags, &name, directory,
                             sizeof(directory), &status);
        if (parent < 0) {
            int failure = errno;

            close(from_parent);
            return send_reply(channel, request->request_id, status, failure);
        }
        outcome = renameat(from_parent, from_name, parent, name);
        close(from_parent);
        close(parent);
        return send_reply(channel, request->request_id,
                          outcome == 0 ? ACE_PRIVILEGE_OK
                                       : ACE_PRIVILEGE_HOST_ERROR,
                          outcome == 0 ? 0 : errno);
    }

    parent = open_parent(relative, request->flags, &name, directory,
                         sizeof(directory), &status);
    if (parent < 0)
        return send_reply(channel, request->request_id, status, errno);

    switch (request->operation) {
    case ACE_PRIVILEGE_ACCESS_UNLINK:
        outcome = unlinkat(parent, name, 0);
        /* AmigaDOS Delete removes both, and Linux reports the difference as
           EISDIR rather than doing it.  Retrying is what makes the one DOS
           operation one operation here too. */
        if (outcome != 0 && (errno == EISDIR || errno == EPERM))
            outcome = unlinkat(parent, name, AT_REMOVEDIR);
        break;
    case ACE_PRIVILEGE_ACCESS_MKDIR:
        outcome = mkdirat(parent, name,
                          request->mode ? (mode_t)(request->mode & 07777)
                                        : 0755);
        break;
    case ACE_PRIVILEGE_ACCESS_SET_PROTECTION:
        /* The AmigaDOS-to-Linux translation happens at the seam, where the
           protection bits are understood.  What arrives here is already a
           mode, and this worker's job is only to apply it to one exact
           object. */
        outcome = fchmodat(parent, name, (mode_t)(request->mode & 07777), 0);
        break;
    case ACE_PRIVILEGE_ACCESS_SET_DATE: {
        struct timespec when[2];

        /* Both stamps from the one value the request carried.  AmigaDOS keeps
           a single date per object; splitting it into two here would mean
           this worker deciding an access time nobody asked it for. */
        when[0].tv_sec = (time_t)request->modification_time;
        when[0].tv_nsec = 0;
        when[1] = when[0];
        outcome = utimensat(parent, name, when, 0);
        break;
    }
    default:
        close(parent);
        return send_reply(channel, request->request_id,
                          ACE_PRIVILEGE_UNSUPPORTED, ENOSYS);
    }
    close(parent);
    return send_reply(channel, request->request_id,
                      outcome == 0 ? ACE_PRIVILEGE_OK : ACE_PRIVILEGE_HOST_ERROR,
                      outcome == 0 ? 0 : errno);
}

static int perform(int channel, const struct ace_privilege_request *request,
                   const void *payload)
{
    const char *relative;

    /* Rename is the one operation whose payload is two paths, so it is also
       the one that cannot be checked as a single string.  It validates both
       halves itself. */
    if (request->operation == ACE_PRIVILEGE_ACCESS_RENAME)
        return perform_named(channel, request, payload);

    relative = checked_relative(payload, request->payload_length);
    if (!relative)
        return send_reply(channel, request->request_id,
                          ACE_PRIVILEGE_PROTOCOL_ERROR, 0);
    switch (request->operation) {
    case ACE_PRIVILEGE_ACCESS_UNLINK:
    case ACE_PRIVILEGE_ACCESS_MKDIR:
    case ACE_PRIVILEGE_ACCESS_SET_PROTECTION:
    case ACE_PRIVILEGE_ACCESS_SET_DATE:
        return perform_named(channel, request, relative);
    default:
        break;
    }
    return perform_open(channel, request, relative);
}

void ace_crm_serve(int fd, uid_t served_uid, const char *view_root)
{
    (void)served_uid; /* Ownership of created files arrives with OPEN_WRITE. */

    if (view_root && *view_root)
        view_root_fd = open(view_root, O_PATH | O_DIRECTORY | O_CLOEXEC);
    /* The real root, for the protected objects the user was refused.  Opened
       here rather than per request for the same reason as the view root: the
       starting point of every resolution should be a descriptor this process
       already holds, not a name it looks up again. */
    host_root_fd = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);

    for (;;) {
        struct ace_privilege_request request;
        unsigned char payload[ACE_PRIVILEGE_MAX_PAYLOAD];
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
        if (request.magic != ACE_PRIVILEGE_MAGIC ||
            request.payload_length > ACE_PRIVILEGE_MAX_PAYLOAD) {
            send_reply(fd, request.request_id, ACE_PRIVILEGE_PROTOCOL_ERROR, 0);
            return;
        }

        switch (request.operation) {
        case ACE_PRIVILEGE_PING:
            send_reply(fd, request.request_id, ACE_PRIVILEGE_OK, 0);
            continue;
        case ACE_PRIVILEGE_SHUTDOWN:
        case ACE_PRIVILEGE_DROP_PRIVILEGE:
            send_reply(fd, request.request_id, ACE_PRIVILEGE_OK, 0);
            return;
        case ACE_PRIVILEGE_CANCEL:
            send_reply(fd, request.request_id, ACE_PRIVILEGE_OK, 0);
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
        if (ACE_PRIVILEGE_CLASS_OF(request.operation) !=
            ACE_PRIVILEGE_CLASS_ACCESS) {
            send_reply(fd, request.request_id, ACE_PRIVILEGE_REFUSED, 0);
            continue;
        }
        if (perform(fd, &request, payload) != 0)
            return;
    }
}
