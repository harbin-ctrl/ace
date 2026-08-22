/*
 * Try as the user; ask only when that fails.
 *
 * This file is small on purpose.  It is the only place in ACE that decides an
 * operation needs privilege, and a decision made in one place can be read in
 * one sitting and got right once.  Scattering it through the commands is what
 * the migration exists to avoid: there are dozens of them, they are mostly
 * unmodified AROS source, and each one that grew its own idea of when to
 * escalate would be a separate thing to audit.
 */

#define _GNU_SOURCE

#include "ace_crm_retry.h"

#include "ace_privilege_protocol.h"
#include "ace_modes.h"
#include "broker_client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

static int last_was_privileged;

/*
 * Whether the privileged retry is even possible.
 *
 * The only question asked before an operation is attempted, and it is about
 * the session rather than about the object: without --root a refusal simply
 * stands, which is what it would have been without the crm.
 *
 * Nothing here looks at the path.  It used to: a prefix comparison against
 * the device-view root sent matching names straight to the crm without trying
 * them locally at all, which meant ACE decided an operation needed root by
 * inspecting a string.  That was a workaround for this file escalating only
 * EACCES and EPERM -- a device-view path fails locally with ENOENT, because
 * the mount that would satisfy it is in a namespace this process is not in --
 * and with every failure retried the workaround has nothing left to do.  The
 * kernel's answer to the attempt is the whole of the decision.
 */
static int may_escalate(void)
{
    return ace_mode_is_root();
}

/*
 * Which of two failures to report, when the privileged retry failed too.
 *
 * The retry is a second chance, not a second opinion.  When it came back with
 * something the crm learned about the object -- EPERM on a hard link to a
 * directory, ENOENT on a name that really is not there -- that is the better
 * answer, because it was reached with more power than the caller had and is
 * therefore about the object rather than about the caller.
 *
 * When it came back with a fact about the exchange instead, the caller's own
 * refusal stands.  A broken channel or a garbled reply says nothing about the
 * file, and letting it through would report EIO for a mistyped name -- which
 * matters far more now that every failure is retried, because most failures
 * are ordinary and were answered correctly the first time.
 */
static int report_failure(int outcome, int local_failure)
{
    int privileged = errno;

    if (outcome < 0 || privileged == EIO || privileged == EPROTO)
        errno = local_failure;
    else
        errno = privileged;
    return -1;
}

static uint32_t open_flags_to_crm(int flags)
{
    uint32_t result = 0;

    if (flags & O_CREAT)
        result |= ACE_PRIVILEGE_FLAG_CREATE;
    if (flags & O_TRUNC)
        result |= ACE_PRIVILEGE_FLAG_TRUNCATE;
    if (flags & O_APPEND)
        result |= ACE_PRIVILEGE_FLAG_APPEND;
    if (flags & O_EXCL)
        result |= ACE_PRIVILEGE_FLAG_EXCLUSIVE;
    return result;
}

static int crm_operation_for_open(int flags)
{
    int access_mode = flags & O_ACCMODE;

    if (flags & O_DIRECTORY)
        return ACE_PRIVILEGE_ACCESS_OPEN_DIR;
    if (access_mode == O_RDONLY)
        return ACE_PRIVILEGE_ACCESS_OPEN_READ;
    return ACE_PRIVILEGE_ACCESS_OPEN_WRITE;
}

static uint32_t update_flag(int flags)
{
    return (flags & O_ACCMODE) == O_RDWR ? ACE_PRIVILEGE_FLAG_UPDATE : 0;
}

int ace_crm_retry_open(const char *path, int flags, mode_t mode)
{
    int result;
    int failure;
    int outcome;
    int received = -1;

    last_was_privileged = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    result = open(path, flags, mode);
    if (result >= 0)
        return result;
    failure = errno;
    if (!may_escalate()) {
        errno = failure;
        return -1;
    }
    /*
     * Read-write is asked for as write.  The crm's opens are typed and
     * there is no read-write among them, deliberately: an operation that both
     * reads and writes a protected object is two capabilities, and ACE's uses
     * -- copy in, copy out, examine -- each want one.
     */
    if ((outcome = native_broker_privop((uint32_t)crm_operation_for_open(flags), path,
                             NULL,
                             open_flags_to_crm(flags) |
                                 update_flag(flags),
                             (uint32_t)(mode & 07777), 0, &received)) != 0)
        return report_failure(outcome, failure);
    last_was_privileged = 1;
    return received;
}

/*
 * The stdio mode string, turned into open() flags.
 *
 * "+" is the one that matters here: it means the caller intends to read and
 * write the same handle, and it has to survive the trip or the stream it gets
 * back will refuse half of what it was asked for.
 */
static int flags_for_stdio_mode(const char *mode)
{
    int update = strchr(mode, '+') != NULL;

    switch (mode[0]) {
    case 'w':
        return (update ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    case 'a':
        return (update ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    case 'r':
    default:
        return update ? O_RDWR : O_RDONLY;
    }
}

FILE *ace_crm_retry_fopen(const char *path, const char *mode)
{
    int fd;
    FILE *stream;

    if (!path || !mode) {
        errno = EINVAL;
        return NULL;
    }
    fd = ace_crm_retry_open(path, flags_for_stdio_mode(mode), 0666);
    if (fd < 0)
        return NULL;
    stream = fdopen(fd, mode);
    if (!stream) {
        int failure = errno;

        close(fd);
        errno = failure;
    }
    return stream;
}

DIR *ace_crm_retry_opendir(const char *path)
{
    int fd = ace_crm_retry_open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    DIR *directory;

    if (fd < 0)
        return NULL;
    directory = fdopendir(fd);
    if (!directory) {
        int failure = errno;

        close(fd);
        errno = failure;
    }
    return directory;
}

int ace_crm_retry_stat(const char *path, struct stat *information, int follow)
{
    int failure;
    int outcome;
    int received = -1;

    last_was_privileged = 0;
    if (!path || !information) {
        errno = EINVAL;
        return -1;
    }
    if ((follow ? stat(path, information) : lstat(path, information)) == 0)
        return 0;
    failure = errno;
    if (!may_escalate()) {
        errno = failure;
        return -1;
    }
    /*
     * An O_PATH descriptor comes back and the fstat happens here.  The worker
     * never serialises a struct stat across the boundary: its layout is the
     * kernel's business and a copy of it in a protocol is a copy that can
     * disagree with the one the caller compiled against.
     */
    if ((outcome = native_broker_privop(ACE_PRIVILEGE_ACCESS_STAT, path, NULL,
                                        follow ? 0 : ACE_PRIVILEGE_FLAG_NOFOLLOW,
                                        0, 0, &received)) != 0)
        return report_failure(outcome, failure);
    if (received < 0) {
        errno = EPROTO;
        return -1;
    }
    /* AT_EMPTY_PATH is how an O_PATH descriptor is stat'ed.  Which object it
       refers to -- the link or its target -- was settled by the flag above,
       so the caller's question survives the trip rather than being answered
       about whichever object the worker happened to reach. */
    if (fstatat(received, "", information, AT_EMPTY_PATH) != 0) {
        int failure = errno;

        close(received);
        errno = failure;
        return -1;
    }
    close(received);
    last_was_privileged = 1;
    return 0;
}

/*
 * Read one symlink's target, escalating a refusal like everything else.
 *
 * Returns what readlink() returns, unterminated length and all, because every
 * caller of this was written against readlink() and a wrapper that improved
 * on its interface would be a second thing to remember.
 *
 * The privileged half asks for a stat and reads the answer off the
 * descriptor.  A symlink's target is not separable from the link the way a
 * file's contents are separable from the file: the same O_PATH|O_NOFOLLOW
 * handle that describes the link also carries what it points at.  So there is
 * no opcode for this, and no second resolution of the name -- which is the
 * property worth having, since resolving a name twice is how the object
 * examined stops being the object acted on.
 */
ssize_t ace_crm_retry_readlink(const char *path, char *buffer, size_t size)
{
    int failure;
    int outcome;
    int received = -1;
    ssize_t length;

    last_was_privileged = 0;
    if (!path || !buffer || !size) {
        errno = EINVAL;
        return -1;
    }
    length = readlink(path, buffer, size);
    if (length >= 0)
        return length;
    failure = errno;
    if (!may_escalate()) {
        errno = failure;
        return -1;
    }
    if ((outcome = native_broker_privop(ACE_PRIVILEGE_ACCESS_STAT, path, NULL,
                                        ACE_PRIVILEGE_FLAG_NOFOLLOW, 0, 0,
                                        &received)) != 0)
        return (int)report_failure(outcome, failure);
    if (received < 0) {
        errno = EPROTO;
        return -1;
    }
    length = readlinkat(received, "", buffer, size);
    if (length < 0)
        failure = errno;
    close(received);
    if (length < 0) {
        /* EINVAL from here means the object is not a link, which is what
           readlink() says about it too.  Nothing to translate. */
        errno = failure;
        return -1;
    }
    last_was_privileged = 1;
    return length;
}

/*
 * The operations with no descriptor to hand back.  Same shape as the others:
 * try, and let the failure decide.
 *
 * The caller passes the result of its own attempt and has not touched errno
 * since, so the refusal read here is the one that attempt produced.
 *
 * When the privileged retry fails too, report_failure() decides which of the
 * two answers the caller is told about.
 */
static int named_operation(uint32_t privop, const char *path,
                           const char *second, uint32_t mode, int64_t when,
                           int local_result)
{
    int failure;
    int outcome;

    if (local_result == 0)
        return 0;
    failure = errno;
    if (!may_escalate()) {
        errno = failure;
        return -1;
    }
    outcome = native_broker_privop(privop, path, second, 0, mode, when, NULL);
    if (outcome != 0)
        return report_failure(outcome, failure);
    last_was_privileged = 1;
    return 0;
}

/*
 * Remove either kind of object, which is what one AmigaDOS Delete does.
 *
 * Linux splits the two calls and reports the difference rather than acting on
 * it, so the choice is made here -- on the unprivileged path as well as the
 * privileged one, where the worker already does exactly this.  Keeping both
 * sides of the seam agreed on what "remove" means is what lets the caller
 * stop caring: it asks once, and the answer it gets back is about the object,
 * not about which system call was tried on the way.
 */
static int remove_either_kind(const char *path)
{
    int failure;

    if (unlink(path) == 0)
        return 0;
    failure = errno;
    if (failure != EISDIR && failure != EPERM) {
        errno = failure;
        return -1;
    }
    if (rmdir(path) == 0)
        return 0;
    /* ENOTDIR means it was a file after all and rmdir was the wrong question,
       so unlink's answer is the real one.  Anything else is the directory
       speaking -- ENOTEMPTY above all, which is what makes Delete recurse
       before trying again -- and must not be overwritten by it. */
    if (errno != ENOTDIR)
        failure = errno;
    errno = failure;
    return -1;
}

/*
 * Create a symlink, escalating a refusal.
 *
 * The argument order is symlink()'s, target first, for the same reason the
 * readlink wrapper keeps readlink()'s: every caller was written against the
 * system call.
 *
 * The target is passed through untouched.  What it should say was decided by
 * the caller -- see MakeLink() in native_dos.c, which has the volume
 * information needed to choose -- and this layer has no business improving on
 * a string it will never resolve.
 */
int ace_crm_retry_symlink(const char *target, const char *path)
{
    last_was_privileged = 0;
    if (!target || !*target || !path) {
        errno = EINVAL;
        return -1;
    }
    return named_operation(ACE_PRIVILEGE_ACCESS_SYMLINK, path, target, 0, 0,
                           symlink(target, path));
}

int ace_crm_retry_unlink(const char *path)
{
    last_was_privileged = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    return named_operation(ACE_PRIVILEGE_ACCESS_UNLINK, path, NULL, 0, 0,
                           remove_either_kind(path));
}

/*
 * Give an object a second name, escalating a refusal.
 *
 * With one refusal deliberately not escalated.  A hard link to a directory is
 * refused by the kernel itself, for root exactly as for anyone: a directory
 * with two parents is no longer a tree, and everything that walks one --
 * "..", a recursive delete, a filesystem check -- depends on it being one.
 * Asking the worker would spend an authorisation on an operation that cannot
 * succeed, and an authorisation prompt that buys nothing is precisely the
 * thing this design refuses to produce.
 *
 * The distinction is worth making carefully, because EPERM says both things
 * here.  On a file it can mean an ordinary refusal that privilege would fix
 * -- a protected_hardlinks refusal, say -- and that one escalates.
 */
int ace_crm_retry_link(const char *from, const char *to)
{
    struct stat information;
    int failure;

    last_was_privileged = 0;
    if (!from || !to) {
        errno = EINVAL;
        return -1;
    }
    if (link(from, to) == 0)
        return 0;
    failure = errno;
    if (failure == EPERM && lstat(from, &information) == 0 &&
        S_ISDIR(information.st_mode)) {
        errno = failure;
        return -1;
    }
    errno = failure;
    return named_operation(ACE_PRIVILEGE_ACCESS_LINK, from, to, 0, 0, -1);
}

int ace_crm_retry_rename(const char *from, const char *to)
{
    last_was_privileged = 0;
    if (!from || !to) {
        errno = EINVAL;
        return -1;
    }
    return named_operation(ACE_PRIVILEGE_ACCESS_RENAME, from, to, 0, 0,
                           rename(from, to));
}

int ace_crm_retry_mkdir(const char *path, mode_t mode)
{
    last_was_privileged = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    return named_operation(ACE_PRIVILEGE_ACCESS_MKDIR, path, NULL,
                           (uint32_t)(mode & 07777), 0, mkdir(path, mode));
}

int ace_crm_retry_chmod(const char *path, mode_t mode)
{
    last_was_privileged = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    return named_operation(ACE_PRIVILEGE_ACCESS_SET_PROTECTION, path, NULL,
                           (uint32_t)(mode & 07777), 0, chmod(path, mode));
}

/*
 * Stamp one object with one date, escalating a refusal like everything else.
 *
 * utimensat() rather than utime(): the same call the worker makes, so the two
 * sides of the seam are doing the same thing to the same object and only the
 * identity performing it differs.  A caller with no times means "now", which
 * is what Touch asks for.
 */
int ace_crm_retry_utime(const char *path, const struct utimbuf *times)
{
    int64_t when;
    int local;

    last_was_privileged = 0;
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    when = times ? (int64_t)times->modtime : (int64_t)time(NULL);
    local = utime(path, times);
    return named_operation(ACE_PRIVILEGE_ACCESS_SET_DATE, path, NULL, 0, when,
                           local);
}

int ace_crm_retry_last_was_privileged(void)
{
    return last_was_privileged;
}
