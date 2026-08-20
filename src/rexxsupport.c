/*
 * The host implementation of the acceptance-critical part of
 * rexxsupport.library.
 *
 * On AmigaOS this is an AROS library whose query vector is called with a
 * RexxMsg.  ACE has no executable Amiga vector table, so rexxcall.h routes
 * that one vector through ace_rexx_call_query_lib_func(), which lands here.
 * The public results intentionally keep the ARexx ABI: strings are
 * rexxsyslib argstrings and pointer results are the raw bytes of a host
 * pointer.  Those pointers are useful only inside this Regina process.
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>

#include <exec/libraries.h>
#include <exec/memory.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <rexx/errors.h>
#include <rexx/rexxcall.h>
#include <rexx/storage.h>
#include <clib/rexxsyslib_protos.h>

struct ace_rexxsupport_base {
    struct Library library;
};

static struct ace_rexxsupport_base ace_rexxsupport_base;

struct ace_rexx_allocation {
    void *address;
    size_t size;
    struct ace_rexx_allocation *next;
};

static struct ace_rexx_allocation *ace_rexx_allocations;
static pthread_mutex_t ace_rexx_allocations_lock = PTHREAD_MUTEX_INITIALIZER;

/* The AROS implementation keeps the permit count in a Rexx variable.  A
 * Regina thread is the owner of its call sequence, so a TLS count gives the
 * same observable nesting without making a process-wide or broker-wide lock.
 */
static _Thread_local int ace_rexx_permit_nesting = -1;

static int ace_rexx_arg_count(const struct RexxMsg *message)
{
    return (int)(message->rm_Action & RXARGMASK);
}

static LONG ace_rexx_text(UBYTE **result, const char *text)
{
    *result = CreateArgstring((UBYTE *)text, (ULONG)strlen(text));
    return *result ? RC_OK : ERR10_003;
}

static LONG ace_rexx_bytes(UBYTE **result, const void *bytes, size_t length)
{
    *result = CreateArgstring((UBYTE *)bytes, (ULONG)length);
    return *result ? RC_OK : ERR10_003;
}

static LONG ace_rexx_pointer_result(UBYTE **result, void *address)
{
    return ace_rexx_bytes(result, &address, sizeof(address));
}

static int ace_rexx_parse_ulong(UBYTE *argument, ULONG *value)
{
    char *end;
    unsigned long long parsed;

    if (!argument || *argument == '-')
        return -1;
    errno = 0;
    parsed = strtoull((char *)argument, &end, 10);
    if (end == (char *)argument || errno == ERANGE || parsed > UINT32_MAX)
        return -1;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return -1;
    *value = (ULONG)parsed;
    return 0;
}

static int ace_rexx_parse_long(UBYTE *argument, LONG *value)
{
    char *end;
    long long parsed;

    if (!argument)
        return -1;
    errno = 0;
    parsed = strtoll((char *)argument, &end, 10);
    if (end == (char *)argument || errno == ERANGE ||
        parsed < INT32_MIN || parsed > INT32_MAX)
        return -1;
    while (*end && isspace((unsigned char)*end))
        end++;
    if (*end != '\0')
        return -1;
    *value = (LONG)parsed;
    return 0;
}

static int ace_rexx_read_pointer(UBYTE *argument, void **address)
{
    if (!argument || LengthArgstring(argument) != sizeof(*address))
        return -1;
    memcpy(address, argument, sizeof(*address));
    return 0;
}

static int ace_rexx_allocation_contains_locked(const void *address,
                                               size_t length)
{
    uintptr_t needle = (uintptr_t)address;
    struct ace_rexx_allocation *allocation;

    if (!address)
        return 0;
    for (allocation = ace_rexx_allocations; allocation;
         allocation = allocation->next) {
        uintptr_t start = (uintptr_t)allocation->address;
        uintptr_t offset;

        if (needle < start)
            continue;
        offset = needle - start;
        if (offset <= allocation->size && length <= allocation->size - offset)
            return 1;
    }
    return 0;
}

/* Return 1 for a tracked range, -1 for a tracked but out-of-bounds range,
 * and 0 when the address did not come from ALLOCMEM.  The distinction lets
 * Regina keep its existing behaviour for its own GETSPACE allocations while
 * making pointers returned by this library safe to validate. */
static int ace_rexx_allocation_status_locked(const void *address,
                                             size_t length)
{
    uintptr_t needle = (uintptr_t)address;
    struct ace_rexx_allocation *allocation;

    if (!address)
        return -1;
    for (allocation = ace_rexx_allocations; allocation;
         allocation = allocation->next) {
        uintptr_t start = (uintptr_t)allocation->address;
        uintptr_t offset;

        if (needle < start)
            continue;
        offset = needle - start;
        if (offset <= allocation->size)
            return length <= allocation->size - offset ? 1 : -1;
    }
    return 0;
}

static int ace_rexx_allocation_contains(const void *address, size_t length)
{
    int found;

    pthread_mutex_lock(&ace_rexx_allocations_lock);
    found = ace_rexx_allocation_contains_locked(address, length);
    pthread_mutex_unlock(&ace_rexx_allocations_lock);
    return found;
}

int ace_rexxsupport_memory_status(const void *address, size_t length)
{
    int status;

    pthread_mutex_lock(&ace_rexx_allocations_lock);
    status = ace_rexx_allocation_status_locked(address, length);
    pthread_mutex_unlock(&ace_rexx_allocations_lock);
    return status;
}

int ace_rexxsupport_memory_cstring_length(const void *address, size_t *length)
{
    uintptr_t needle = (uintptr_t)address;
    struct ace_rexx_allocation *allocation;
    size_t index;

    if (!address || !length)
        return -1;
    pthread_mutex_lock(&ace_rexx_allocations_lock);
    for (allocation = ace_rexx_allocations; allocation;
         allocation = allocation->next) {
        uintptr_t start = (uintptr_t)allocation->address;
        uintptr_t offset;
        size_t available;

        if (needle < start)
            continue;
        offset = needle - start;
        if (offset > allocation->size)
            continue;
        available = allocation->size - offset;
        for (index = 0; index < available; index++)
            if (((const unsigned char *)address)[index] == 0) {
                *length = index;
                pthread_mutex_unlock(&ace_rexx_allocations_lock);
                return 1;
            }
        pthread_mutex_unlock(&ace_rexx_allocations_lock);
        return -1;
    }
    pthread_mutex_unlock(&ace_rexx_allocations_lock);
    return 0;
}

static int ace_rexx_record_allocation(void *address, size_t size)
{
    struct ace_rexx_allocation *allocation = malloc(sizeof(*allocation));

    if (!allocation)
        return -1;
    allocation->address = address;
    allocation->size = size;
    pthread_mutex_lock(&ace_rexx_allocations_lock);
    allocation->next = ace_rexx_allocations;
    ace_rexx_allocations = allocation;
    pthread_mutex_unlock(&ace_rexx_allocations_lock);
    return 0;
}

static int ace_rexx_take_allocation(void *address, ULONG requested_size)
{
    struct ace_rexx_allocation **link;
    struct ace_rexx_allocation *allocation;

    pthread_mutex_lock(&ace_rexx_allocations_lock);
    for (link = &ace_rexx_allocations; *link; link = &(*link)->next) {
        allocation = *link;
        if (allocation->address != address)
            continue;
        if ((uint64_t)requested_size > allocation->size) {
            pthread_mutex_unlock(&ace_rexx_allocations_lock);
            return -1;
        }
        *link = allocation->next;
        free(allocation);
        pthread_mutex_unlock(&ace_rexx_allocations_lock);
        return 0;
    }
    pthread_mutex_unlock(&ace_rexx_allocations_lock);
    return -1;
}

static LONG ace_rexx_allocmem(struct Library *base, struct RexxMsg *message,
                              UBYTE **result)
{
    ULONG size;
    ULONG attributes = MEMF_PUBLIC;
    void *address;

    if (ace_rexx_parse_ulong(ARG1(message), &size) != 0)
        return ERR10_018;
    if (ace_rexx_arg_count(message) == 2) {
        UBYTE *flags = ARG2(message);

        if (!flags || LengthArgstring(flags) != 4)
            return ERR10_018;
        attributes = ((ULONG)flags[0] << 24) |
                     ((ULONG)flags[1] << 16) |
                     ((ULONG)flags[2] << 8) | (ULONG)flags[3];
    }
    address = AllocMem(size, attributes);
    if (!address)
        return ERR10_003;
    if (ace_rexx_record_allocation(address, size) != 0) {
        FreeMem(address, size);
        return ERR10_003;
    }
    return ace_rexx_pointer_result(result, address);
}

static LONG ace_rexx_freemem(struct Library *base, struct RexxMsg *message,
                             UBYTE **result)
{
    ULONG size;
    void *address;
    int take_status;

    if (ace_rexx_read_pointer(ARG1(message), &address) != 0 ||
        ace_rexx_parse_ulong(ARG2(message), &size) != 0)
        return ERR10_018;
    take_status = ace_rexx_take_allocation(address, size);
    if (take_status != 0)
        return ERR10_018;
    FreeMem(address, size);
    /* Keep the result non-NULL so Regina knows the library function was
     * found, but empty so a standalone FREEMEM expression is not submitted
     * to ADDRESS COMMAND as a command line. */
    return ace_rexx_text(result, "");
}

static LONG ace_rexx_null(struct Library *base, struct RexxMsg *message,
                          UBYTE **result)
{
    return ace_rexx_pointer_result(result, NULL);
}

static LONG ace_rexx_offset(struct Library *base, struct RexxMsg *message,
                            UBYTE **result)
{
    LONG offset;
    void *address;
    uintptr_t result_address;

    if (ace_rexx_read_pointer(ARG1(message), &address) != 0 ||
        ace_rexx_parse_long(ARG2(message), &offset) != 0)
        return ERR10_018;

    /* NULL is deliberately accepted: ARexx uses NULL plus a small offset as
     * a BCPL/pointer arithmetic probe. It is returned but never dereferenced.
     */
    if (!address) {
        if (offset < 0 || (uint64_t)offset > UINTPTR_MAX)
            return ERR10_018;
        result_address = (uintptr_t)offset;
    } else {
        uintptr_t start;
        uintptr_t end;
        uintptr_t current = (uintptr_t)address;
        int valid = 0;
        struct ace_rexx_allocation *allocation;

        pthread_mutex_lock(&ace_rexx_allocations_lock);
        for (allocation = ace_rexx_allocations; allocation;
             allocation = allocation->next) {
            start = (uintptr_t)allocation->address;
            end = start + allocation->size;
            if (current >= start && current <= end) {
                if (offset >= 0) {
                    uintptr_t distance = (uintptr_t)offset;
                    valid = distance <= end - current;
                    if (valid)
                        result_address = current + distance;
                } else {
                    uintptr_t distance = (uintptr_t)(-(int64_t)offset);
                    valid = distance <= current - start;
                    if (valid)
                        result_address = current - distance;
                }
                break;
            }
        }
        pthread_mutex_unlock(&ace_rexx_allocations_lock);
        if (!valid)
            return ERR10_018;
    }
    return ace_rexx_pointer_result(result, (void *)result_address);
}

static LONG ace_rexx_baddr(struct Library *base, struct RexxMsg *message,
                           UBYTE **result)
{
    void *address;

    if (ace_rexx_read_pointer(ARG1(message), &address) != 0)
        return ERR10_018;
    return ace_rexx_pointer_result(result, BADDR((BPTR)address));
}

static LONG ace_rexx_typepkt(struct Library *base, struct RexxMsg *message,
                             UBYTE **result)
{
    struct RexxMsg *packet;
    LONG action = 0;

    if (ace_rexx_read_pointer(ARG1(message), (void **)&packet) != 0)
        return ERR10_018;
    if (ace_rexx_allocation_contains(packet,
                                     offsetof(struct RexxMsg, rm_Action) +
                                     sizeof(packet->rm_Action)))
        memcpy(&action, (char *)packet + offsetof(struct RexxMsg, rm_Action),
               sizeof(action));

    if (ace_rexx_arg_count(message) == 1)
        return ace_rexx_bytes(result, &action, sizeof(action));
    switch (toupper((unsigned char)ARG2(message)[0])) {
    case 'A': {
        char value = (char)('0' + (action & RXARGMASK));
        return ace_rexx_bytes(result, &value, 1);
    }
    case 'C': {
        char value = (action & RXCODEMASK) == RXCOMM ? '1' : '0';
        return ace_rexx_bytes(result, &value, 1);
    }
    case 'F': {
        char value = (action & RXCODEMASK) == RXFUNC ? '1' : '0';
        return ace_rexx_bytes(result, &value, 1);
    }
    default:
        return ERR10_018;
    }
}

static LONG ace_rexx_forbid(struct Library *base, struct RexxMsg *message,
                            UBYTE **result)
{
    char value[24];

    ace_rexx_permit_nesting++;
    if (ace_rexx_permit_nesting == 0)
        Forbid();
    snprintf(value, sizeof(value), "%d", ace_rexx_permit_nesting);
    return ace_rexx_text(result, value);
}

static LONG ace_rexx_permit(struct Library *base, struct RexxMsg *message,
                            UBYTE **result)
{
    char value[24];
    int was_forbidden = ace_rexx_permit_nesting >= 0;

    ace_rexx_permit_nesting--;
    if (was_forbidden && ace_rexx_permit_nesting < 0)
        Permit();
    snprintf(value, sizeof(value), "%d", ace_rexx_permit_nesting);
    return ace_rexx_text(result, value);
}

static LONG ace_rexx_makedir(struct Library *base, struct RexxMsg *message,
                             UBYTE **result)
{
    BPTR lock = CreateDir((CONST_STRPTR)ARG1(message));

    if (lock != BNULL)
        UnLock(lock);
    return ace_rexx_text(result, lock != BNULL ? "1" : "0");
}

static LONG ace_rexx_rename(struct Library *base, struct RexxMsg *message,
                            UBYTE **result)
{
    return ace_rexx_text(result,
                         Rename((CONST_STRPTR)ARG1(message),
                                (CONST_STRPTR)ARG2(message)) ? "1" : "0");
}

static LONG ace_rexx_delete(struct Library *base, struct RexxMsg *message,
                            UBYTE **result)
{
    return ace_rexx_text(result,
                         DeleteFile((CONST_STRPTR)ARG1(message)) ? "1" : "0");
}

struct ace_rexx_function {
    const char *name;
    UBYTE minimum_args;
    UBYTE maximum_args;
    LONG (*call)(struct Library *, struct RexxMsg *, UBYTE **);
};

/* Keep this list in the same alphabetical order as AROS's dispatch table.
 * IMPORT is intentionally absent: Regina implements it as a core ARexx
 * builtin, consuming the raw pointer bytes returned by ALLOCMEM/OFFSET. */
static const struct ace_rexx_function ace_rexx_functions[] = {
    { "ALLOCMEM", 1, 2, ace_rexx_allocmem },
    { "BADDR",    1, 1, ace_rexx_baddr },
    { "DELETE",   1, 1, ace_rexx_delete },
    { "FORBID",   0, 0, ace_rexx_forbid },
    { "FREEMEM",  2, 2, ace_rexx_freemem },
    { "MAKEDIR",  1, 1, ace_rexx_makedir },
    { "NULL",     0, 0, ace_rexx_null },
    { "OFFSET",   2, 2, ace_rexx_offset },
    { "PERMIT",   0, 0, ace_rexx_permit },
    { "RENAME",   2, 2, ace_rexx_rename },
    { "TYPEPKT",  1, 2, ace_rexx_typepkt }
};

static const struct ace_rexx_function *ace_rexx_find_function(const char *name)
{
    size_t index;

    for (index = 0; index < sizeof(ace_rexx_functions) /
                            sizeof(ace_rexx_functions[0]); index++)
        if (strcasecmp(name, ace_rexx_functions[index].name) == 0)
            return &ace_rexx_functions[index];
    return NULL;
}

struct Library *ace_rexxsupport_library_base(void)
{
    return &ace_rexxsupport_base.library;
}

ULONG ace_rexx_call_query_lib_func(struct RexxMsg *message,
                                   struct Library *libbase,
                                   SIPTR offset,
                                   UBYTE **result)
{
    const struct ace_rexx_function *function;
    int argument_count;
    LONG status;

    (void)offset;
    if (result)
        *result = NULL;
    if (!message || !result || libbase != &ace_rexxsupport_base.library ||
        !ARG0(message))
        return 1;

    function = ace_rexx_find_function((const char *)ARG0(message));
    if (!function)
        return 1;
    argument_count = ace_rexx_arg_count(message);
    if (argument_count < function->minimum_args ||
        argument_count > function->maximum_args)
        return ERR10_018;
    status = function->call(libbase, message, result);
    return (ULONG)status;
}
