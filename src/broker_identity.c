/*
 * Which broker is this, and where does it listen.
 *
 * Both questions are answered here, once, because every client has to reach
 * the same answer the broker did.  Two copies of this rule that drifted apart
 * would produce a client and a broker that can never meet -- a failure with
 * no symptom other than a broker that mysteriously will not answer.
 *
 * It lives in its own file rather than as inline code in broker_protocol.h
 * because resolving SYS: needs stat() and readlink(), and this project
 * declares the feature macros it wants per source file.  A header included
 * from forty translation units cannot rely on any of them having done so.
 */

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include "broker_protocol.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef ACE_SYS_DIR
#define ACE_SYS_DIR ""
#endif

/*
 * SYS: is what makes an ACE system that system: on a real Amiga "which
 * machine" is answered by which volume you booted, and the same question has
 * the same answer here.  So it is what the broker's identity follows.
 *
 * The fallback to the executable's own directory is what gives an
 * uninstalled build tree a SYS: of its own, and therefore a broker of its
 * own, distinct from an installed copy -- but only while nothing has been
 * installed yet.  Once ACE_SYS_DIR exists, a build tree and the install
 * genuinely are the same system and sharing one broker is correct.  What
 * must not be shared is a broker of a different *build*, and the protocol
 * version in the socket name below is what handles that.
 */
const char *amiga_broker_system_root(void)
{
    static char resolved[PATH_MAX];
    const char *override = getenv("ACE_SYS_DIR");
    char executable[PATH_MAX];
    struct stat info;
    ssize_t length;
    char *slash;

    if (resolved[0])
        return resolved;
    if (override && *override && strlen(override) < sizeof(resolved)) {
        strcpy(resolved, override);
        return resolved;
    }
    if (ACE_SYS_DIR[0] && stat(ACE_SYS_DIR, &info) == 0 &&
        S_ISDIR(info.st_mode) && strlen(ACE_SYS_DIR) < sizeof(resolved)) {
        strcpy(resolved, ACE_SYS_DIR);
        return resolved;
    }
    length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length > 0 && (size_t)length < sizeof(executable) - 1) {
        executable[length] = '\0';
        slash = strrchr(executable, '/');
        if (slash && slash != executable) {
            *slash = '\0';
            if (strlen(executable) < sizeof(resolved)) {
                strcpy(resolved, executable);
                return resolved;
            }
        }
    }
    strcpy(resolved, "/");
    return resolved;
}

/* FNV-1a.  This only has to separate one SYS: root from another inside a
   directory the user already owns, so a short non-cryptographic digest is the
   right size of hammer: it keeps the path well inside sun_path. */
static uint64_t broker_hash(const char *text)
{
    uint64_t hash = 1469598103934665603ull;

    for (; *text; text++) {
        hash ^= (unsigned char)*text;
        hash *= 1099511628211ull;
    }
    return hash;
}

/*
 * The name carries both halves of the broker's identity: which system it
 * serves, and which wire protocol it speaks.
 *
 * Including the protocol version is what makes a stale broker harmless
 * rather than merely detectable.  Two builds whose protocol differs do not
 * listen on the same path at all, so a freshly built command cannot reach
 * the broker left behind by the previous build -- it starts its own instead
 * of failing.  The version handshake still exists for the cases this cannot
 * cover: an ACE_BROKER_SOCKET set by hand, and ad-hoc builds that carry no
 * version at all.
 *
 * The socket lives in XDG_RUNTIME_DIR, which is per-user, private, and
 * cleared when the user's last session ends, so a socket cannot outlive the
 * login it belongs to.  Where there is no such directory the uid goes in the
 * name instead, which still separates users even though nothing will clean
 * up after them.
 */
const char *amiga_broker_socket_path(void)
{
    /* Kept well inside sockaddr_un's 108-byte sun_path. */
    static char resolved[104];
    const char *configured = getenv("ACE_BROKER_SOCKET");
    const char *runtime_dir;
    unsigned long long key;
    unsigned version = (unsigned)AMIGA_BROKER_PROTOCOL_VERSION;
    int written = -1;

    if (configured && *configured)
        return configured;
    if (resolved[0])
        return resolved;

    key = (unsigned long long)broker_hash(amiga_broker_system_root());
    runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && *runtime_dir)
        written = snprintf(resolved, sizeof(resolved),
                           "%s/ace-broker-%016llx-%08x.sock",
                           runtime_dir, key, version);
    if (written < 0 || written >= (int)sizeof(resolved))
        written = snprintf(resolved, sizeof(resolved),
                           "/tmp/ace-broker-%lu-%016llx-%08x.sock",
                           (unsigned long)getuid(), key, version);
    if (written < 0 || written >= (int)sizeof(resolved))
        resolved[0] = '\0'; /* unusable; callers will fail to connect */
    return resolved;
}
