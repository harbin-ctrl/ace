#ifndef AMIGA_SHELL_BROKER_PROTOCOL_H
#define AMIGA_SHELL_BROKER_PROTOCOL_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * One broker per user, and this is how everything finds it.
 *
 * The default used to be a single machine-wide /tmp/ace-broker.sock, which
 * is wrong in both directions: two people logged into the same machine
 * collide on one path (and, since the socket is created 0600, the second one
 * simply cannot use it), while anything that set ACE_BROKER_SOCKET to a
 * private path got a whole additional broker process. Test runs left drifts
 * of them behind.
 *
 * XDG_RUNTIME_DIR is the per-user directory made for exactly this: it is
 * private to the user, and the system clears it when their last session
 * ends, so a broker's socket cannot outlive the login it belongs to. Where
 * there is no such directory the uid goes in the filename instead, which
 * separates users even though nothing will clean up after them.
 *
 * ACE_BROKER_SOCKET still overrides, for a deliberately isolated broker.
 * That is a thing worth being able to do; it just should not be what happens
 * by accident.
 */
static inline const char *amiga_broker_socket_path(void)
{
    /* Kept well inside sockaddr_un's 108-byte sun_path. */
    static char resolved[104];
    const char *configured = getenv("ACE_BROKER_SOCKET");
    const char *runtime_dir;
    int written = -1;

    if (configured && *configured)
        return configured;
    if (resolved[0])
        return resolved;

    runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && *runtime_dir)
        written = snprintf(resolved, sizeof(resolved), "%s/ace-broker.sock",
                           runtime_dir);
    if (written < 0 || written >= (int)sizeof(resolved))
        written = snprintf(resolved, sizeof(resolved), "/tmp/ace-broker-%lu.sock",
                           (unsigned long)getuid());
    if (written < 0 || written >= (int)sizeof(resolved))
        resolved[0] = '\0'; /* unusable; callers will fail to connect */
    return resolved;
}

#define AMIGA_BROKER_MAGIC 0x414D4742u /* AMGB */

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
    AMIGA_BROKER_ATTACH = 16
};

#define AMIGA_BROKER_VAR_LOCAL  0x0001u
#define AMIGA_BROKER_VAR_GLOBAL 0x0002u
#define AMIGA_BROKER_VAR_SAVE   0x0004u
#define AMIGA_BROKER_VAR_ALIAS  0x0008u
#define AMIGA_BROKER_VAR_ANY    0x0010u
#define AMIGA_BROKER_VAR_VARIABLE 0x0020u

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

#endif
