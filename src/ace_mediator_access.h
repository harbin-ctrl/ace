#ifndef ACE_MEDIATOR_ACCESS_H
#define ACE_MEDIATOR_ACCESS_H

#include "ace_mediator_protocol.h"

#include <sys/types.h>

/*
 * Serve one broker's access channel until it closes.
 *
 * Called only in a freshly forked child that has already closed every other
 * descriptor it inherited.  It does not return until the channel ends, and
 * the caller is expected to _exit() immediately afterwards -- this process
 * exists for one channel and has nothing to do once it is gone.
 */
void ace_mediator_access_serve(int fd, uid_t served_uid, const char *view_root);

#endif
