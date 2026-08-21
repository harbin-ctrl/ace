#ifndef ACE_MEDIATOR_PROTOCOL_H
#define ACE_MEDIATOR_PROTOCOL_H

#include <stdint.h>

/*
 * What the ACE mediator is, and what it is deliberately not.
 *
 * ACE's shell, console, and broker are the user's own processes.  They use
 * the user's session bus, the user's Wayland connection, the user's HOME and
 * configuration.  That is not a preference: a Unix session bus peer and the
 * ownership of a configuration directory follow the process's identity, and
 * no amount of copying environment variables into a root process makes it
 * the same peer.  A root-owned GUI is a different user's GUI wearing the
 * name of this one.
 *
 * So privilege does not live in those programs.  It lives here, behind a
 * typed request/reply protocol, in a process with no HOME, no current
 * directory, no configuration, no GUI, and no way to be asked to run a
 * command.  The mediator performs named operations on named objects and
 * answers with a status.  That is its entire vocabulary.
 *
 * There is no opcode that executes anything, and adding one would defeat the
 * whole design -- see the note above the access class for why that matters
 * more here than it would in a system that re-authorised every operation.
 *
 * LNX is outside all of this.  A Linux program started through LNX is a
 * Linux user process and stays one; a user who wants a root shell runs
 * "LNX sudo bash" and gets exactly what they asked for, visibly, through the
 * escape hatch that exists to be visible.
 */

/*
 * The protocol version is a hash of this file, computed by the Makefile, for
 * the same reason the broker's is: a version number that a human has to
 * remember to bump is worse than none at all, because it reports
 * "compatible" while the layout underneath has moved.  Any edit here
 * produces a new version, which errs in the safe direction.
 *
 * Zero means an ad-hoc compile rather than a make.  Such a build still talks
 * to itself and reports itself as unversioned.
 */
#ifndef ACE_MEDIATOR_PROTOCOL_VERSION
#define ACE_MEDIATOR_PROTOCOL_VERSION 0u
#endif

#define ACE_MEDIATOR_MAGIC_BASE 0x41434D44u /* ACMD */
#define ACE_MEDIATOR_MAGIC \
    (ACE_MEDIATOR_MAGIC_BASE ^ (uint32_t)ACE_MEDIATOR_PROTOCOL_VERSION)

static inline uint32_t ace_mediator_version_from_magic(uint32_t magic)
{
    return magic ^ ACE_MEDIATOR_MAGIC_BASE;
}

/*
 * Authorisation is per session, by explicit product decision.
 *
 * Starting ACE with --root is the permission.  Authenticating once turns it
 * into a running mediator, and that mediator serves the session until the
 * session ends, the user gives the privilege up, or an optional timeout
 * expires.
 *
 * The rejected alternative was to authorise each protected object, which
 * sounds safer and is not.  A Copy of two hundred files would raise two
 * hundred prompts, and a person shown the same dialog two hundred times is
 * not making two hundred decisions -- they are making one and then reflexing
 * one hundred and ninety-nine times.  Prompt fatigue converts a security
 * control into a formality, and a formality that has trained the user to
 * dismiss it unread is worse than no control, because it also launders the
 * result as consent.
 *
 * It is also simply wrong for the machine ACE is imitating.  An Amiga has no
 * privilege model because the person at the keyboard owns the computer, and
 * an interface that keeps asking that person for permission to use their own
 * disk does not feel like an Amiga.  It feels like a guest account.
 *
 * The consequence is that this interface is the whole boundary, not one
 * layer of several.  Nothing downstream asks again.  Every opcode below is
 * therefore narrow on purpose: exact objects, no path strings resolved twice,
 * no operation whose meaning depends on what a string turns out to name, and
 * nothing that can be talked into running a program.  A generic operation
 * added here would not be a weakness in depth -- in a --root session it would
 * be a root shell reachable from any ARexx script.
 */

/*
 * The two personalities, encoded in the opcode itself.
 *
 * Mount management and protected file access are both privileged, but they
 * are not the same power and must not imply one another.  A file operation
 * that has been talked into misbehaving must not be able to mount a
 * filesystem, and the volume worker must never be reachable by an access
 * request that got creative.
 *
 * Putting the class in the high byte of the opcode makes that check a mask
 * rather than a lookup table.  A table is a thing that can fall out of date
 * with the enum beside it; a mask cannot.  A worker holds a bitmask of the
 * classes it may serve and refuses everything else before any payload is
 * examined -- before, specifically, any path in that payload is looked at.
 */
#define ACE_MEDIATOR_CLASS_MASK    0xFF00u
#define ACE_MEDIATOR_CLASS_CONTROL 0x0000u
#define ACE_MEDIATOR_CLASS_VOLUME  0x0100u
#define ACE_MEDIATOR_CLASS_ACCESS  0x0200u

#define ACE_MEDIATOR_CLASS_OF(opcode) ((uint32_t)(opcode) & ACE_MEDIATOR_CLASS_MASK)

/* Capability bits, one per class, carried in the handshake and held by each
   worker for the life of the process.  A worker cannot grant itself more. */
#define ACE_MEDIATOR_CAP_VOLUME 0x0001u
#define ACE_MEDIATOR_CAP_ACCESS 0x0002u

enum ace_mediator_operation {
    /*
     * Control.  Available to any authenticated peer, because none of these
     * touch an object: they establish who is talking, end the conversation,
     * or interrupt something already in flight.
     */

    /*
     * First message on the channel, sent by the broker once it has accepted
     * the mediator's connection.
     *
     * The mediator connects outward rather than inheriting a socketpair,
     * because pkexec will not carry a descriptor across the exec.  So the
     * broker listens, and the direction of every check follows from that:
     * the broker knows its peer is root from SO_PEERCRED, and the mediator
     * knows its peer is the broker it was launched for because this message
     * carries the nonce the mediator was started with.  Neither side takes
     * the other's word for anything, and neither trusts the socket's path --
     * a path is a rendezvous, not an authentication.
     */
    ACE_MEDIATOR_HELLO = 0x0001,
    /* Which classes this mediator will actually serve, negotiated rather
       than assumed, so a newer broker talking to an older mediator finds out
       by asking instead of by receiving an error mid-operation. */
    ACE_MEDIATOR_CAPS = 0x0002,
    /* Liveness, for the broker to notice a mediator that is present but
       wedged.  A mediator that has died is reported by EOF on the channel
       and needs no polling. */
    ACE_MEDIATOR_PING = 0x0003,
    /*
     * Interrupt an operation by request id.
     *
     * Ctrl-C must not be delivered to the mediator as a signal.  The root
     * workers are deliberately kept out of the terminal's foreground process
     * group, and a broker that controlled a root process with kill() would be
     * a user process holding a lever on a privileged one.  So a break becomes
     * a message: terminal SIGINT reaches the shell, the shell tells the
     * broker, the broker sends CANCEL naming the request.
     *
     * What cancellation means is per operation and is not negotiable here.
     * Rename and unlink either happened or did not; there is no partial one
     * to abandon.  A read in progress stops.  Copy is not one operation at
     * this layer at all -- it is many, driven by the command -- so the
     * partial destination is the command's problem to define, and it does.
     */
    ACE_MEDIATOR_CANCEL = 0x0004,
    /*
     * Give the privilege up before the session ends.
     *
     * The user asked for a way to reduce privilege deliberately rather than
     * only by quitting.  This is it: the mediator releases what it holds and
     * exits, and the session continues as an ordinary unprivileged one.  A
     * later protected operation fails the way it would have without --root.
     *
     * Distinct from SHUTDOWN, which is the broker ending the session.  This
     * one is the user's choice and is expected to be reported back to them.
     */
    ACE_MEDIATOR_DROP_PRIVILEGE = 0x0005,
    /* Orderly end of session: unmount what we mounted, release handles,
       exit.  Never the only path -- the mediator also exits on channel EOF,
       because a broker that crashed cannot send anything at all. */
    ACE_MEDIATOR_SHUTDOWN = 0x0006,
    /*
     * Ask the supervisor for an access worker, and get its channel back as a
     * descriptor.
     *
     * This is what makes the two personalities two processes rather than two
     * branches in one.  The access worker is forked after the mount namespace
     * exists, so it is inside it and can open the device view; it closes the
     * supervisor's own channel on the way in, so it holds no route to the
     * volume side at all.  "The access worker cannot issue volume operations"
     * therefore stops being a check that could be got wrong and becomes a
     * connection that does not exist.
     *
     * It also solves the problem that made a private namespace look
     * impossible once the broker stopped being root.  setns() into a
     * root-owned mount namespace needs CAP_SYS_ADMIN in the user namespace
     * that owns it, so an unprivileged broker can never enter one -- but it
     * never needs to.  The worker opens the object inside the namespace and
     * passes back a descriptor, and a descriptor does not belong to a
     * namespace.  Nobody unprivileged enters; everybody unprivileged reads.
     *
     * One authentication covers both processes: the user authorised ACE, not
     * a particular number of helpers, and asking twice for one decision is
     * the prompt fatigue this design already refused once.
     */
    ACE_MEDIATOR_SPAWN_ACCESS = 0x0007,

    /*
     * Volume class.  The mount namespace, the device view, and the mounts
     * inside it.
     *
     * This worker holds real kernel state -- a namespace, mounts, bindroots,
     * open descriptors -- and that is correct and unavoidable.  What it must
     * not hold is anything Amiga-shaped: no current directories, no assigns,
     * no variables.  Those belong to the broker, which is the user's process
     * and is where session semantics live.
     */

    /* Create the private mount namespace and make it private recursively.
       Returns nothing but a status; the namespace is identified afterwards by
       the mediator's own pid, which is what clients setns() to. */
    ACE_MEDIATOR_VOLUME_INIT_NAMESPACE = 0x0101,
    /* Build the full device view: every supported block-backed filesystem
       mounted at its own kernel name, bind-mounting an existing mount where
       one exists so that the directories a nested mount obscures become
       visible. */
    ACE_MEDIATOR_VOLUME_PREPARE_VIEW = 0x0102,
    /* Mount one already-discovered device on demand. */
    ACE_MEDIATOR_VOLUME_MOUNT = 0x0103,
    /* Unmount one, by logical device id rather than by path: a path would
       have to be resolved again here, and resolving an untrusted string twice
       is how a check and its use come apart. */
    ACE_MEDIATOR_VOLUME_UNMOUNT = 0x0104,
    /* What is mounted, for the broker's DOS device list and for diagnostics
       that should not have to read /proc to answer. */
    ACE_MEDIATOR_VOLUME_LIST = 0x0105,

    /*
     * Access class.  Exactly one protected object operation per request.
     *
     * Where the operation can be represented by a descriptor, it is: the
     * mediator opens the object and passes the fd back, and the ACE command
     * copies bytes as the ordinary user it already is.  That keeps the
     * privileged part down to the single open() that needed it, and hands
     * back a capability to one object rather than the power to reach others.
     *
     * Unlink, rename, and mkdir have no descriptor to hand back, so the
     * worker performs that exact operation and reports precisely what
     * happened.  These are the only cases where the mediator acts rather than
     * opens, and the list does not grow casually.
     *
     * Every path here is resolved under openat2() with RESOLVE_BENEATH and
     * RESOLVE_NO_MAGICLINKS against a dirfd the mediator already holds --
     * never by handing a string to the kernel and hoping.  ".." , absolute
     * paths, and symlinks that leave the tree are refused, not normalised.
     */

    /* Open one exact existing object for reading; returns an fd. */
    ACE_MEDIATOR_ACCESS_OPEN_READ = 0x0201,
    /* Open or create one exact object for writing; returns an fd.  A file
       created this way is owned by root, as sudo cp would leave it: ACE does
       not invent an ownership rule, and AmigaDOS has no owner model to
       borrow one from. */
    ACE_MEDIATOR_ACCESS_OPEN_WRITE = 0x0202,
    /* Enumerate one exact directory; returns an fd suitable for fdopendir. */
    ACE_MEDIATOR_ACCESS_OPEN_DIR = 0x0203,
    /* Metadata for one exact object, for Examine and friends. */
    ACE_MEDIATOR_ACCESS_STAT = 0x0204,
    ACE_MEDIATOR_ACCESS_UNLINK = 0x0205,
    /* Both paths are resolved beneath the same held dirfd, in one request,
       because a rename whose halves were authorised separately is not a
       rename -- it is two operations with a window between them. */
    ACE_MEDIATOR_ACCESS_RENAME = 0x0206,
    ACE_MEDIATOR_ACCESS_MKDIR = 0x0207,
    /* AmigaDOS protection bits, translated at the seam rather than here. */
    ACE_MEDIATOR_ACCESS_SET_PROTECTION = 0x0208
};

/*
 * Status codes.
 *
 * Stable and ACE's own, because a command's reported failure should not
 * change when a kernel changes which errno it prefers.  The errno the
 * mediator actually saw travels alongside as diagnostic detail -- worth
 * printing, never worth branching on.
 *
 * ACE commands are talky by design and report both success and failure with
 * a specific reason, so these exist to be turned into sentences.
 */
enum ace_mediator_status {
    ACE_MEDIATOR_OK = 0,
    /* The request was refused before the payload was examined: bad magic,
       wrong version, unknown opcode, or a class this worker does not hold. */
    ACE_MEDIATOR_REFUSED = 1,
    /* The path did not resolve beneath the dirfd it was required to stay
       under.  Deliberately distinct from a permission failure: this one means
       the request tried to leave the tree, and that is worth saying plainly
       rather than reporting as "denied". */
    ACE_MEDIATOR_ESCAPED = 2,
    /* Authorisation is not held, or has been given up or timed out. */
    ACE_MEDIATOR_UNAUTHORISED = 3,
    /* The operation ran and the host refused it; errno carries the detail. */
    ACE_MEDIATOR_HOST_ERROR = 4,
    /* Cancelled by a CANCEL naming this request id. */
    ACE_MEDIATOR_CANCELLED = 5,
    /* Well-formed, understood, and not supported by this build -- a btrfs
       subvolume asked to become a device root, for instance. */
    ACE_MEDIATOR_UNSUPPORTED = 6,
    /* Malformed: lengths that do not add up, payload over the bound, a
       descriptor count that does not match the flags. */
    ACE_MEDIATOR_PROTOCOL_ERROR = 7
};

/* Bounded because unbounded is how a length field becomes an allocator.  A
   path fits, a directory name fits, and anything larger is a protocol error
   rather than a large request. */
#define ACE_MEDIATOR_MAX_PAYLOAD 8192u
/* At most one descriptor returns per request today.  Named as a bound rather
   than assumed as a constant, so the receive path has something to check
   against instead of trusting the sender's count. */
#define ACE_MEDIATOR_MAX_FDS 1u

/* Request flags. */
#define ACE_MEDIATOR_FLAG_CREATE    0x0001u
#define ACE_MEDIATOR_FLAG_TRUNCATE  0x0002u
#define ACE_MEDIATOR_FLAG_APPEND    0x0004u
#define ACE_MEDIATOR_FLAG_EXCLUSIVE 0x0008u
/* The reply to this request carries a descriptor as ancillary data. */
#define ACE_MEDIATOR_FLAG_HAS_FD    0x0010u
/*
 * Which of the two path domains this request names.
 *
 * Set: an absolute host path, for an object the user was refused -- /etc,
 * /root, and their like.  Clear: a path relative to the device-view root, for
 * an object that exists only inside the mediator's mount namespace and that
 * the user could not have opened however the permissions stood.
 *
 * They are different questions and they get different resolution rules.  A
 * device-view path must stay inside its volume, so it is resolved with
 * RESOLVE_BENEATH and a symlink leading out is an escape.  A host path is
 * ordinary Linux naming and is resolved with RESOLVE_IN_ROOT, where an
 * absolute symlink means what it says.  Both refuse magic links, because
 * neither has any business being redirected through a descriptor table.
 *
 * The distinction is in the protocol rather than inferred from a leading
 * slash, so that a caller states which it means and a malformed path cannot
 * quietly change domain.
 */
#define ACE_MEDIATOR_FLAG_HOST_PATH 0x0020u

/* Length of the per-launch random instance identifier.  Sixteen bytes so
   that guessing it is not a strategy. */
#define ACE_MEDIATOR_NONCE_LENGTH 16u

/*
 * The handshake.
 *
 * pkexec will not carry an inherited socketpair across the exec -- it
 * rewrites the environment and does not preserve arbitrary descriptors --
 * so the mediator cannot simply be handed one end of a pair.  It connects
 * back instead, to a socket whose name contains this nonce, in a directory
 * only this user can read.
 *
 * That makes the nonce and the peer credentials the trust boundary rather
 * than the path, which is the right way round: a predictable path is a
 * rendezvous, not an authentication.  The broker checks SO_PEERCRED to
 * confirm the connecting peer is uid 0, and the mediator checks the nonce to
 * confirm the broker is the one it was launched for.  Both directions are
 * checked because both directions matter -- a mediator that will talk to any
 * local process is as bad as a broker that will accept any local root.
 */
struct ace_mediator_hello {
    uint32_t magic;
    uint32_t version;
    /* The broker this mediator was launched for.  Recorded so a mediator can
       notice it is talking to a different process than the one it was started
       for, which pid reuse alone would not reveal. */
    int32_t broker_pid;
    uint32_t requested_capabilities;
    uint8_t nonce[ACE_MEDIATOR_NONCE_LENGTH];
};

struct ace_mediator_hello_reply {
    uint32_t magic;
    uint32_t version;
    int32_t status;
    /* What was actually granted, which may be less than was requested. */
    uint32_t granted_capabilities;
    /* Seconds until authorisation lapses, or zero for "until the session
       ends", which is the default.  A timeout is offered because the user
       asked for the option, not because ACE imposes one. */
    uint32_t authorisation_seconds;
};

/*
 * One request.  Fixed header, then payload_length bytes.
 *
 * request_id is chosen by the broker and monotonic within a channel.  It is
 * what a reply is matched to and what CANCEL names, so it has to be unique
 * among the requests still outstanding rather than merely unique-ish.
 *
 * device_id names a logical volume the volume worker already knows about,
 * and is how an unmount says which filesystem it means without a path.  It
 * is ignored by the control class.
 */
struct ace_mediator_request {
    uint32_t magic;
    uint32_t operation;
    uint64_t request_id;
    uint64_t device_id;
    uint32_t flags;
    /* AmigaDOS protection bits or a creation mode, per operation. */
    uint32_t mode;
    /* For RENAME, the payload holds two NUL-terminated paths and this is the
       length of the first.  Zero for every operation that takes one path. */
    uint32_t first_path_length;
    uint32_t payload_length;
};

struct ace_mediator_response {
    uint32_t magic;
    uint64_t request_id;
    /* enum ace_mediator_status. */
    int32_t status;
    /* The host errno, supplementary and diagnostic only.  Never the thing a
       caller branches on -- that is what status is for. */
    int32_t host_errno;
    uint32_t flags;
    uint32_t payload_length;
};

#endif
