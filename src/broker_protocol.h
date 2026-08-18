#ifndef AMIGA_SHELL_BROKER_PROTOCOL_H
#define AMIGA_SHELL_BROKER_PROTOCOL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * The protocol version is a hash of this file, computed by the Makefile and
 * passed in as a define.  It is derived rather than hand-maintained because
 * a version nobody remembers to bump is worse than none: it says "compatible"
 * while the layout underneath has changed.  Any edit to this header produces
 * a new version, which is conservative in the right direction.
 *
 * Zero means the build did not supply one -- an ad-hoc compile rather than a
 * make.  Such a build still talks to itself, and reports "unversioned".
 */
#ifndef AMIGA_BROKER_PROTOCOL_VERSION
#define AMIGA_BROKER_PROTOCOL_VERSION 0u
#endif

/*
 * How everything finds the broker.
 *
 * The socket lives in XDG_RUNTIME_DIR, which is per-user, private, and
 * cleared when the user's last session ends, so a socket cannot outlive the
 * login it belongs to.  Where there is no such directory the uid goes in the
 * filename instead, which still separates users even though nothing will
 * clean up after them.
 *
 * The name also carries a hash of the SYS: root -- see
 * amiga_broker_system_root() for why that, and not the user, is the thing a
 * broker's identity actually follows.
 *
 * ACE_BROKER_SOCKET still overrides, for a deliberately isolated broker.
 * That is a thing worth being able to do; it just should not be what happens
 * by accident.
 */
/* Implemented in src/broker_identity.c -- see there for why the rule lives
   in one place and what the broker's identity actually follows. */
const char *amiga_broker_system_root(void);
const char *amiga_broker_socket_path(void);

/*
 * The magic carries the protocol version, so a mismatch is caught on the
 * very first field either side reads rather than after a payload has been
 * misparsed.  A peer's version can be recovered from the magic it sent
 * (magic ^ BASE), which lets the diagnostic name both builds instead of
 * saying only that something is wrong.
 */
#define AMIGA_BROKER_MAGIC_BASE 0x414D4742u /* AMGB */
#define AMIGA_BROKER_MAGIC \
    (AMIGA_BROKER_MAGIC_BASE ^ (uint32_t)AMIGA_BROKER_PROTOCOL_VERSION)

static inline uint32_t amiga_broker_version_from_magic(uint32_t magic)
{
    return magic ^ AMIGA_BROKER_MAGIC_BASE;
}

enum amiga_broker_operation {
    AMIGA_BROKER_RESOLVE = 1,
    AMIGA_BROKER_GETCWD  = 2,
    AMIGA_BROKER_SETCWD  = 3,
    AMIGA_BROKER_ASSIGN  = 4,
    AMIGA_BROKER_GETVAR  = 5,
    AMIGA_BROKER_SETVAR  = 6,
    AMIGA_BROKER_DELVAR  = 7,
    AMIGA_BROKER_GETRESULT = 8,
    AMIGA_BROKER_SETRESULT = 9,
    AMIGA_BROKER_LISTVARS = 10,
    AMIGA_BROKER_GETCLI = 11,
    AMIGA_BROKER_SETFAILLEVEL = 12,
    AMIGA_BROKER_SETPROMPT = 13,
    AMIGA_BROKER_CLONESESSION = 14,
    AMIGA_BROKER_LISTDOS = 15,
    /*
     * Claim ownership of this connection's session.
     *
     * Every other operation is a transient use: a command process asks a
     * question and exits, and the broker cannot tell whether the shell that
     * owns the session is still alive. ATTACH is the shell saying "this
     * session is mine, and it lives exactly as long as this connection
     * does". The broker frees the session when the last attached connection
     * closes, which the kernel reports whether the shell exits, is killed,
     * or crashes.
     */
    AMIGA_BROKER_ATTACH = 16,
    /* Translate an absolute host path back into an AmigaDOS volume name. */
    AMIGA_BROKER_NAMEFROMHOST = 17,
    /* Return the current session's assignments for the DOS compatibility
     * layer that implements the unmodified AROS Assign command. */
    AMIGA_BROKER_LISTASSIGNS = 18,
    /* Resolve a relative DOS path beneath a specific host-side assignment
     * target. The AROS DOS dispatcher chooses the target; the broker only
     * performs component mapping and containment checks here. */
    AMIGA_BROKER_RESOLVE_BENEATH = 19,
    /* Change the filesystem label behind a DOS volume alias. */
    AMIGA_BROKER_RELABEL = 20,
    /* The shell's PATH list, kept in the broker because commands are
     * separate processes from the shell that owns the CLI. */
    AMIGA_BROKER_LISTPATH = 21,
    AMIGA_BROKER_PATH = 22,
    /* A dedicated, broker-to-task control connection.  It is deliberately
       separate from the request/reply connection: signals may arrive while
       a task is doing DOS I/O. */
    AMIGA_BROKER_TASK_ATTACH = 23,
    AMIGA_BROKER_TASK_FIND = 24,
    AMIGA_BROKER_TASK_SIGNAL = 25,
    AMIGA_BROKER_TASK_SET_FOREGROUND = 26,
    AMIGA_BROKER_TASK_BREAK_FOREGROUND = 27,
    AMIGA_BROKER_TASK_LIST = 28,
    /* Report what this broker is: its build, where it lives, which SYS: it
       serves, and what it is currently holding.  Answering "which broker am
       I talking to" should not require reading ps output. */
    AMIGA_BROKER_STATUS = 29
};

#define AMIGA_BROKER_ASSIGN_REMOVE       0x0001u
#define AMIGA_BROKER_ASSIGN_ADD          0x0002u
#define AMIGA_BROKER_ASSIGN_PREPEND      0x0004u
#define AMIGA_BROKER_ASSIGN_PATH         0x0008u
#define AMIGA_BROKER_ASSIGN_DEFER        0x0010u
#define AMIGA_BROKER_ASSIGN_REMOVE_ITEM  0x0020u

#define AMIGA_BROKER_PATH_ADD       0x0001u
#define AMIGA_BROKER_PATH_PREPEND   0x0002u
#define AMIGA_BROKER_PATH_REMOVE    0x0004u
#define AMIGA_BROKER_PATH_RESET     0x0008u

#define AMIGA_BROKER_VAR_LOCAL  0x0001u
#define AMIGA_BROKER_VAR_GLOBAL 0x0002u
#define AMIGA_BROKER_VAR_SAVE   0x0004u
#define AMIGA_BROKER_VAR_ALIAS  0x0008u
#define AMIGA_BROKER_VAR_ANY    0x0010u
#define AMIGA_BROKER_VAR_VARIABLE 0x0020u
/* Internal callers may already hold an absolute Linux path (for example a
 * native lock passed to CurrentDir). It must not receive Amiga '/' semantics. */
#define AMIGA_BROKER_PATH_HOST  0x80000000u

#define AMIGA_BROKER_MAX_PAYLOAD 16384u

struct amiga_broker_request {
    uint32_t magic;
    uint32_t operation;
    uint32_t session_length;
    uint32_t path_length;
    uint32_t value_length;
    uint32_t flags;
};

struct amiga_broker_response {
    uint32_t magic;
    int32_t status;
    uint32_t payload_length;
};

/* Records sent only from broker to a TASK_ATTACH connection, after its
   ordinary successful attach response. */
struct amiga_broker_task_signal {
    uint32_t magic;
    uint32_t operation;
    uint64_t task_id;
    uint32_t signals;
};

#endif
