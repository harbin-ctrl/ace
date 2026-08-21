#ifndef ACE_PRIVOPS_H
#define ACE_PRIVOPS_H

#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

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
 * The exception is the device view, whose paths exist only inside the
 * mediator's mount namespace and so fail locally with ENOENT no matter what
 * the permissions say.  Those are routed by where they are rather than by how
 * they failed, using a view root learned once from the broker.  That is one
 * runtime value from the process that created it, not a policy.
 *
 * These take host paths.  AmigaDOS-to-host translation has already happened
 * by the time anything reaches here.
 */

/* Open a file, escalating a permission refusal.  Returns a descriptor. */
int ace_privops_open(const char *path, int flags, mode_t mode);

/*
 * The stdio form, for the callers that want a stream.
 *
 * Goes through the descriptor form rather than calling fopen() on a path, so
 * that a protected file opens the same way here as anywhere else.  A plain
 * fopen() would reach the host directly and miss both the escalation and the
 * device view.
 */
FILE *ace_privops_fopen(const char *path, const char *mode);

/* Open a directory for enumeration.  Returns a DIR the caller closes. */
DIR *ace_privops_opendir(const char *path);

/* stat or lstat, escalating a permission refusal. */
int ace_privops_stat(const char *path, struct stat *information, int follow);

int ace_privops_unlink(const char *path);
int ace_privops_rename(const char *from, const char *to);
int ace_privops_mkdir(const char *path, mode_t mode);
int ace_privops_chmod(const char *path, mode_t mode);

/*
 * Whether the last successful ace_privops call needed the mediator.
 *
 * ACE commands are talky and report what happened, and a file that has just
 * been created root-owned is exactly the kind of thing a user should be told
 * about at the time rather than discover later when they cannot edit it.
 * Cleared at the start of every call, so it always describes the most recent
 * one.
 */
int ace_privops_last_was_privileged(void);

#endif
