#ifndef ACE_MEDIATOR_CLIENT_H
#define ACE_MEDIATOR_CLIENT_H

#include "ace_mediator_protocol.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * The broker's side of the mediator channel.
 *
 * Only the broker links this.  The shell, the console, and the commands do
 * not get a privileged socket of their own: one semantic authority and one
 * privilege ingress, so that "may this happen" has exactly one place to be
 * answered and exactly one place to be got wrong.
 */

struct ace_mediator;

/*
 * Launch the mediator and complete the handshake.
 *
 * Lazy by design.  Starting ACE with --root is permission to ask, not an
 * instruction to authenticate now, so the first call is expected to happen
 * when an operation has already been refused to the user -- not at startup,
 * where it would present an authentication prompt to somebody who has not yet
 * done anything that needs one.
 *
 * Returns NULL on any failure, with errno set.  A failure here is reportable
 * and not fatal: an ACE session whose mediator will not start is an ordinary
 * unprivileged session, which is exactly what it would have been without
 * --root.
 */
struct ace_mediator *ace_mediator_start(uint32_t wanted_capabilities);

/*
 * The same, with the two things a test needs to vary.
 *
 * expected_uid is the uid the connecting peer must have.  Production passes
 * 0, and passing anything else is only meaningful for a test that runs the
 * mediator unelevated as itself.  It is a parameter rather than an
 * environment variable on purpose: an env-var backdoor that relaxes a
 * privilege check is a backdoor whether or not it was meant as one, and it
 * would ship.  A parameter can only be reached by code that was linked
 * against it.
 *
 * program overrides which executable is launched, or NULL for the mediator
 * installed beside this one.  elevate selects pkexec; a test that is already
 * root, or that is only exercising the protocol, passes 0.
 */
struct ace_mediator *ace_mediator_start_as(uint32_t wanted_capabilities,
                                           uid_t expected_uid,
                                           const char *program, int elevate);

/* What the mediator actually granted, which may be less than was asked. */
uint32_t ace_mediator_capabilities(const struct ace_mediator *mediator);

/* Seconds until authorisation lapses, or 0 for "until the session ends". */
uint32_t ace_mediator_authorisation_seconds(const struct ace_mediator *mediator);

/* The mediator's pid.  Clients setns() to its mount namespace once the volume
   worker owns one, which is why this is exposed at all. */
pid_t ace_mediator_pid(const struct ace_mediator *mediator);

/* Round-trip liveness check.  Returns 0 if the mediator answered. */
int ace_mediator_ping(struct ace_mediator *mediator);

/*
 * Ask the mediator to give up privilege and exit, at the user's request,
 * without ending the ACE session.  The handle is spent afterwards and must
 * still be freed with ace_mediator_close().
 */
int ace_mediator_drop_privilege(struct ace_mediator *mediator);

/*
 * Cancel an outstanding request by id.
 *
 * Never kill().  A user process holding a signal lever on a privileged one is
 * the arrangement this whole design exists to avoid, so a break is a message
 * like any other.
 */
int ace_mediator_cancel(struct ace_mediator *mediator, uint64_t request_id);

/*
 * One typed request and its reply.
 *
 * request->magic and request->request_id are filled in here; a caller that
 * chose its own ids would eventually reuse one that was still outstanding,
 * and CANCEL would then name two things.
 *
 * reply receives up to reply_size bytes of payload.  received_fd, when not
 * NULL, receives a descriptor if the reply carried one, or -1.  The caller
 * owns it and must close it.
 *
 * Returns 0 when the exchange completed, which is not the same as the
 * operation succeeding: check response->status for that.  Returns -1 when the
 * channel itself failed.
 */
int ace_mediator_request(struct ace_mediator *mediator,
                         struct ace_mediator_request *request,
                         const void *payload,
                         struct ace_mediator_response *response,
                         void *reply, size_t reply_size, int *received_fd);

/*
 * Obtain the access worker's channel.
 *
 * A second, independent conversation with a second process: one that lives
 * inside the volume mediator's mount namespace and can therefore open the
 * device view, and that holds no route back to the volume side.  Ask for it
 * after the namespace exists, because a worker forked before that would be in
 * the ordinary view and would not see the thing it exists to reach.
 *
 * The returned handle is closed with ace_mediator_close() like any other, and
 * closing it does not disturb the volume channel it came from.
 */
struct ace_mediator *ace_mediator_access_worker(struct ace_mediator *volume);

/*
 * Create the namespace and the view root, and learn where it is.
 *
 * The path is reported rather than agreed in advance: the side that created
 * it is the side that knows, and a second copy of that rule in the broker
 * would be a second thing to keep in step.
 */
int ace_mediator_prepare_view(struct ace_mediator *mediator, char *root,
                              size_t root_size);

/*
 * Mount one discovered device, and learn where the mediator put it.
 *
 * The broker says which kernel device it found and what it believes the
 * filesystem to be.  It does not say where the mount should go, and there is
 * no parameter through which it could: the mediator derives /dev/<name>,
 * checks the device, checks the type against its own list, and chooses the
 * mountpoint itself.
 */
int ace_mediator_mount(struct ace_mediator *mediator, const char *kernel_name,
                       const char *filesystem_type, char *view_path,
                       size_t view_path_size);

/*
 * One typed operation on one exact object, addressed to the access worker.
 *
 * path is relative and never begins with a slash: which domain it is in comes
 * from flags (ACE_MEDIATOR_FLAG_HOST_PATH for an absolute host object, clear
 * for one inside the device view), so that a caller states what it means
 * rather than a leading character deciding for it.
 *
 * second is the destination of a rename and NULL for everything else; the two
 * halves travel together because a rename whose halves were authorised
 * separately is two operations with a window between them.
 *
 * received_fd, when not NULL, takes the descriptor an opening operation
 * produced.  The caller owns it and must close it.  Returns the ACE status,
 * or -1 if the channel itself failed.
 */
int ace_mediator_access(struct ace_mediator *worker, uint32_t operation,
                        const char *path, const char *second, uint32_t flags,
                        uint32_t mode, int *received_fd);

/* Orderly shutdown where possible, then release the handle.  Safe on NULL. */
void ace_mediator_close(struct ace_mediator *mediator);

#endif
