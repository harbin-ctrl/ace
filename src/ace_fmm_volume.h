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

/* Whether the private mount namespace has been created.  Nothing outside this
   worker decides anything by it -- the namespace is the supervisor's to make,
   once, before any worker exists -- but a request that needs one and finds
   none is answered from here. */
int namespace_is_ready(void);

/* Create the private mount namespace, once, in the supervisor and before
   either worker is forked -- so that both live in the same one.  A failure is
   remembered and reported to every later request that needed it, rather than
   letting a worker quietly make a second namespace of its own. */
int ace_fmm_volume_start_namespace(int *host_errno);

/* Unmount everything this worker mounted, in reverse order.  Called on the
   way out, including the way out that a dead broker causes. */
void ace_fmm_volume_shutdown(void);

#endif
