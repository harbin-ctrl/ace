#ifndef ACE_REQUESTOR_PROTOCOL_H
#define ACE_REQUESTOR_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>

/* Private, host-side protocol carried over the broker's public port layer. */
#define ACE_REQUESTOR_MAGIC 0x41525131u       /* ARQ1 */
#define ACE_REQUESTOR_REPLY_MAGIC 0x41525231u /* ARR1 */
#define ACE_REQUESTOR_PORT_PREFIX "ACE.REQUESTOR."

struct ace_requestor_wire {
    uint32_t magic;
    uint32_t title_length;
    uint32_t text_length;
    uint32_t gadgets_length;
};

struct ace_requestor_reply {
    uint32_t magic;
    int32_t status;
    int32_t result;
};

static inline int ace_requestor_port_name(const char *session, char *result,
                                          size_t result_size)
{
    int written;

    if (!session || !*session)
        session = "default";
    written = snprintf(result, result_size, "%s%s",
                       ACE_REQUESTOR_PORT_PREFIX, session);
    if (written < 0 || (size_t)written >= result_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

#endif
