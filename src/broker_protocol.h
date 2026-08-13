#ifndef AMIGA_SHELL_BROKER_PROTOCOL_H
#define AMIGA_SHELL_BROKER_PROTOCOL_H

#include <stdint.h>

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
    AMIGA_BROKER_LISTDOS = 15
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
