#ifndef ACE_PRIVILEGE_VOLUME_H
#define ACE_PRIVILEGE_VOLUME_H

#include "ace_privilege_protocol.h"

#include <stddef.h>
#include <sys/types.h>

/*
 * The volume worker: the fmm's mount personality.
 *
 * It owns kernel state -- a private mount namespace, the mounts inside it,
 * and the directories they hang on -- and that is correct.  What it must not
 * own is anything Amiga-shaped.  No current directories, no assigns, no
 * variables, no aliases: those are session semantics and they belong to the
 * broker, which is the user's own process.  This side knows only that a block
 * device exists and where it put it.
 *
 * The interface deliberately contains no way to mount an arbitrary host path.
 * The broker names a kernel device by name; this worker derives /dev/<name>
 * itself, checks that it really is a block device, checks the filesystem type
 * against its own list, and chooses the mountpoint.  A target path is never
 * accepted from the far side, so a malformed request cannot ask for a mount
 * anywhere in particular.
 */

/* Reply to a request in the volume class.  Returns the ACE status, and fills
   reply/reply_length with any payload the operation produces. */
int ace_fmm_volume_dispatch(const struct ace_privilege_request *request,
                                 const void *payload, uid_t served_uid,
                                 char *reply, size_t reply_size,
                                 size_t *reply_length, int *host_errno);

/* Whether the private mount namespace has been created.  The supervisor asks
   before forking an CRM: a worker outside the namespace would not
   see the device view it exists to reach. */
int namespace_is_ready(void);

/* Where the device roots hang, or "" before the view has been prepared.  The
   supervisor hands this to the CRM: it is the subtree that worker
   is allowed to resolve inside, and it is the only one it is given. */
const char *ace_fmm_volume_view_root(void);

/* Unmount everything this worker mounted, in reverse order.  Called on the
   way out, including the way out that a dead broker causes. */
void ace_fmm_volume_shutdown(void);

#endif
