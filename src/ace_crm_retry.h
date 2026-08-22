#ifndef ACE_PRIVOPS_H
#define ACE_PRIVOPS_H

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

/*
 * The shared seam between an ACE file operation and the host.
 *
 * Every one of these does the ordinary thing first, as the ordinary user, and
 * only asks for help when the host refuses on permission grounds.  That order
 * is the whole design and not an optimisation:
 *
 *   - it is what keeps a session-scoped authorisation from making everything
 *     the user touches root-owned, because an operation the user could do is
 *     never sent anywhere;
 *   - it is what makes the boundary emergent rather than configured.  ACE
 *     holds no list of protected paths and must never grow one: the set of
 *     places that need privilege is whatever the kernel refuses, which is
 *     always right and never drifts.
 *
 * Only EACCES and EPERM escalate.  ENOENT, ENOTDIR, EROFS, ENOSPC, EISDIR and
 * every other failure is an answer, not a reason to become root -- a
 * misspelled filename must not raise an authentication prompt.
 *
 * There is one exception, and it is as narrow as it can be made.  A few
 * objects exist only inside the crm's mount namespace -- a device the host
 * has not mounted, or a directory covered by something mounted over it -- and
 * for those a local attempt does not fail on permission grounds, it fails
 * with ENOENT, because from here they genuinely are not there.  There is no
 * user-mode try to base the decision on, so those are routed by position,
 * using a view root learned once from the broker.
 *
 * What keeps that from swallowing the rule: the broker hands out such a path
 * only when the host's own mount tree cannot name the object.  Everything the
 * user could open themselves arrives here as an ordinary path and is tried as
 * one, in an authorised session exactly as in any other.  If that ever stops
 * being true the symptom is unmistakable and worth naming here, because it is
 * what this design exists to prevent: files the user owns quietly being read
 * and written by root, and new ones coming out root-owned.
 *
 * These take host paths.  AmigaDOS-to-host translation has already happened
 * by the time anything reaches here.
 */

/* Open a file, escalating a permission refusal.  Returns a descriptor. */
int ace_crm_retry_open(const char *path, int flags, mode_t mode);

/*
 * The stdio form, for the callers that want a stream.
 *
 * Goes through the descriptor form rather than calling fopen() on a path, so
 * that a protected file opens the same way here as anywhere else.  A plain
 * fopen() would reach the host directly and miss both the escalation and the
 * device view.
 */
FILE *ace_crm_retry_fopen(const char *path, const char *mode);

/* Open a directory for enumeration.  Returns a DIR the caller closes. */
DIR *ace_crm_retry_opendir(const char *path);

/* stat or lstat, escalating a permission refusal. */
int ace_crm_retry_stat(const char *path, struct stat *information, int follow);

int ace_crm_retry_unlink(const char *path);
int ace_crm_retry_rename(const char *from, const char *to);
int ace_crm_retry_mkdir(const char *path, mode_t mode);
int ace_crm_retry_chmod(const char *path, mode_t mode);

/* Set an object's date, escalating a permission refusal.  NULL times means
   now, as utime() does. */
int ace_crm_retry_utime(const char *path, const struct utimbuf *times);

/* Read a symlink's target, escalating a permission refusal.  Returns what
   readlink() returns: the length, with no terminator written. */
ssize_t ace_crm_retry_readlink(const char *path, char *buffer, size_t size);

/* Give an object a second name, escalating a permission refusal -- except a
   hard link to a directory, which the kernel refuses to root as well and so
   is reported rather than retried. */
int ace_crm_retry_link(const char *from, const char *to);

/* Create a symlink, escalating a permission refusal.  Argument order is
   symlink()'s: what it points at, then what to call it.  The target is stored
   exactly as given and is never resolved by anything on the way. */
int ace_crm_retry_symlink(const char *target, const char *path);

/*
 * Whether the last successful ace_crm_retry call needed the crm.
 *
 * ACE commands are talky and report what happened, and a file that has just
 * been created root-owned is exactly the kind of thing a user should be told
 * about at the time rather than discover later when they cannot edit it.
 * Cleared at the start of every call, so it always describes the most recent
 * one.
 */
int ace_crm_retry_last_was_privileged(void);

#endif
