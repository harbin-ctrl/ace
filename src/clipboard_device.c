#define _POSIX_C_SOURCE 200809L

#include "clipboard_bridge.h"
#include "clipboard_device.h"

#include <errno.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <devices/clipboard.h>
#include <exec/devices.h>
#include <utility/hooks.h>

#define ACE_CLIPBOARD_HOOK_COUNT 32
#define ACE_CLIPBOARD_NS_QUERY 0x4000
#define ACE_CLIPBOARD_TYPE 9

#define ACE_CLIPBOARD_IOERR_ABORTED (-2)
#define ACE_CLIPBOARD_IOERR_NOCMD (-3)
#define ACE_CLIPBOARD_IOERR_BADLENGTH (-4)
#define ACE_CLIPBOARD_IOERR_BADADDRESS (-5)

struct ace_ns_device_query {
    ULONG dev_query_format;
    ULONG size_available;
    UWORD device_type;
    UWORD device_sub_type;
    UWORD *supported_commands;
};

struct ace_clipboard_unit {
    struct ClipboardUnitPartial public_unit;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    unsigned char *committed;
    size_t committed_size;
    int committed_valid;
    unsigned char *pending;
    size_t pending_size;
    size_t pending_capacity;
    LONG read_id;
    LONG write_id;
    LONG post_id;
    int write_active;
};

struct ace_clipboard_device {
    pthread_once_t initialized;
    pthread_mutex_t hook_lock;
    struct Hook *hooks[ACE_CLIPBOARD_HOOK_COUNT];
    struct ace_clipboard_unit units[ACE_CLIPBOARD_UNIT_COUNT];
};

static struct ace_clipboard_device device = {
    .initialized = PTHREAD_ONCE_INIT,
    .hook_lock = PTHREAD_MUTEX_INITIALIZER,
};

static UWORD supported_commands[] = {
    CMD_READ,
    CMD_WRITE,
    CMD_UPDATE,
    CBD_CHANGEHOOK,
    CBD_POST,
    CBD_CURRENTREADID,
    CBD_CURRENTWRITEID,
    ACE_CLIPBOARD_NS_QUERY,
    0,
};

static void initialize_device(void)
{
    size_t index;

    for (index = 0; index < ACE_CLIPBOARD_UNIT_COUNT; index++) {
        struct ace_clipboard_unit *unit = &device.units[index];

        unit->public_unit.cu_UnitNum = (ULONG)index;
        pthread_mutex_init(&unit->lock, NULL);
        pthread_cond_init(&unit->condition, NULL);
    }
}

static struct ace_clipboard_unit *unit_from_request(
    const struct IOClipReq *request)
{
    size_t index;

    if (!request || !request->io_Unit)
        return NULL;
    for (index = 0; index < ACE_CLIPBOARD_UNIT_COUNT; index++) {
        if (request->io_Unit == &device.units[index].public_unit)
            return &device.units[index];
    }
    return NULL;
}

static int grow_buffer(unsigned char **buffer, size_t *capacity,
                       size_t needed)
{
    size_t new_capacity = *capacity ? *capacity : 256;
    unsigned char *grown;

    if (needed <= *capacity)
        return 0;
    while (new_capacity < needed) {
        if (new_capacity > (size_t)-1 / 2)
            return -1;
        new_capacity *= 2;
    }
    grown = realloc(*buffer, new_capacity);
    if (!grown)
        return -1;
    *buffer = grown;
    *capacity = new_capacity;
    return 0;
}

static LONG next_id(LONG id)
{
    return id == 0 || id == INT32_MAX ? 1 : id + 1;
}

static void notify_hooks(struct ace_clipboard_unit *unit, LONG command,
                         LONG clip_id);

/* Commands are separate host processes in ACE. The Amiga device's handler
 * serialized those processes behind one device task; the bridge's per-unit
 * lock and atomic rename provide the same transaction boundary here. Refresh
 * a unit before a new read so a clip written by another command process is
 * visible without relying on this process's old cache. */
static int refresh_unit_from_store(struct ace_clipboard_unit *unit)
{
    unsigned char *data = NULL;
    size_t size = 0;
    int valid;
    int changed = 0;
    LONG clip_id = 0;

    if (ace_clipboard_store_load(unit->public_unit.cu_UnitNum, &data,
                                 &size) == 0) {
        valid = 1;
    } else if (errno == ENOENT) {
        valid = 0;
    } else {
        return -1;
    }
    pthread_mutex_lock(&unit->lock);
    if (valid != unit->committed_valid ||
        (valid && (size != unit->committed_size ||
                   memcmp(data, unit->committed, size) != 0))) {
        unsigned char *old_data = unit->committed;

        unit->committed = data;
        unit->committed_size = size;
        unit->committed_valid = valid;
        unit->write_id = next_id(unit->write_id);
        unit->post_id = 0;
        clip_id = unit->write_id;
        pthread_cond_broadcast(&unit->condition);
        data = old_data;
        changed = 1;
    }
    pthread_mutex_unlock(&unit->lock);
    free(data);
    if (changed)
        notify_hooks(unit, CBD_POST, clip_id);
    return 0;
}

static void notify_hooks(struct ace_clipboard_unit *unit, LONG command,
                         LONG clip_id)
{
    struct ClipHookMsg message = {
        .chm_Type = 0,
        .chm_ChangeCmd = command,
        .chm_ClipID = clip_id,
    };
    struct Hook *local_hooks[ACE_CLIPBOARD_HOOK_COUNT];
    size_t count = 0;
    size_t index;

    pthread_mutex_lock(&device.hook_lock);
    for (index = 0; index < ACE_CLIPBOARD_HOOK_COUNT; index++) {
        if (device.hooks[index])
            local_hooks[count++] = device.hooks[index];
    }
    pthread_mutex_unlock(&device.hook_lock);

    for (index = 0; index < count; index++) {
        typedef ULONG (*hook_function)(struct Hook *, APTR, APTR);
        hook_function function = (hook_function)local_hooks[index]->h_Entry;

        if (function)
            (void)function(local_hooks[index], &unit->public_unit, &message);
    }
}

static LONG change_hook(struct IOClipReq *request)
{
    struct Hook *hook = (struct Hook *)request->io_Data;
    size_t index;

    if (!hook || (request->io_Length != 0 && request->io_Length != 1))
        return ACE_CLIPBOARD_IOERR_BADADDRESS;
    pthread_mutex_lock(&device.hook_lock);
    if (request->io_Length == 1) {
        for (index = 0; index < ACE_CLIPBOARD_HOOK_COUNT; index++) {
            if (!device.hooks[index]) {
                device.hooks[index] = hook;
                break;
            }
        }
        if (index == ACE_CLIPBOARD_HOOK_COUNT) {
            pthread_mutex_unlock(&device.hook_lock);
            return ACE_CLIPBOARD_IOERR_BADLENGTH;
        }
    } else {
        for (index = 0; index < ACE_CLIPBOARD_HOOK_COUNT; index++) {
            if (device.hooks[index] == hook)
                device.hooks[index] = NULL;
        }
    }
    pthread_mutex_unlock(&device.hook_lock);
    return 0;
}

static LONG read_clip(struct ace_clipboard_unit *unit,
                      struct IOClipReq *request,
                      ace_clipboard_cancelled cancelled,
                      void *cancel_context)
{
    size_t remaining;
    size_t actual;

    if (request->io_ClipID == 0) {
        if (unit->public_unit.cu_UnitNum == PRIMARY_CLIP)
            (void)ace_clipboard_host_refresh();
        if (refresh_unit_from_store(unit) != 0)
            return ACE_CLIPBOARD_IOERR_BADLENGTH;
    }

    pthread_mutex_lock(&unit->lock);
    if (request->io_ClipID == 0) {
        while (unit->post_id != 0 && unit->post_id == unit->write_id) {
            if (cancelled && cancelled(cancel_context)) {
                pthread_mutex_unlock(&unit->lock);
                request->io_ClipID = -1;
                return ACE_CLIPBOARD_IOERR_ABORTED;
            }
            pthread_cond_wait(&unit->condition, &unit->lock);
        }
        if (!unit->committed_valid) {
            request->io_Actual = 0;
            request->io_ClipID = -1;
            pthread_mutex_unlock(&unit->lock);
            return 0;
        }
        unit->read_id = next_id(unit->read_id);
        request->io_ClipID = unit->read_id;
    } else if (request->io_ClipID != unit->read_id) {
        pthread_mutex_unlock(&unit->lock);
        return ACE_CLIPBOARD_IOERR_ABORTED;
    }

    if (request->io_Offset >= unit->committed_size) {
        request->io_Actual = 0;
        request->io_ClipID = -1;
        pthread_mutex_unlock(&unit->lock);
        return 0;
    }

    remaining = unit->committed_size - request->io_Offset;
    actual = request->io_Length;
    if (actual > remaining)
        actual = remaining;
    if (request->io_Data)
        memcpy(request->io_Data, unit->committed + request->io_Offset, actual);
    request->io_Actual = (ULONG)actual;
    request->io_Offset += (ULONG)actual;
    pthread_mutex_unlock(&unit->lock);
    return 0;
}

static LONG write_clip(struct ace_clipboard_unit *unit,
                       struct IOClipReq *request)
{
    size_t end;

    if (request->io_Length != 0 && !request->io_Data)
        return ACE_CLIPBOARD_IOERR_BADADDRESS;
    pthread_mutex_lock(&unit->lock);
    if (request->io_ClipID == 0 || request->io_ClipID == unit->post_id) {
        if (request->io_ClipID == 0)
            unit->write_id = next_id(unit->write_id);
        request->io_ClipID = unit->write_id;
        unit->post_id = 0;
        unit->pending_size = 0;
        unit->write_active = 1;
    } else if (!unit->write_active || request->io_ClipID != unit->write_id) {
        pthread_mutex_unlock(&unit->lock);
        return ACE_CLIPBOARD_IOERR_ABORTED;
    }

    if ((uintmax_t)request->io_Offset + (uintmax_t)request->io_Length >
        (uintmax_t)SIZE_MAX) {
        pthread_mutex_unlock(&unit->lock);
        return ACE_CLIPBOARD_IOERR_BADLENGTH;
    }
    end = (size_t)request->io_Offset + request->io_Length;
    if (grow_buffer(&unit->pending, &unit->pending_capacity, end) != 0) {
        pthread_mutex_unlock(&unit->lock);
        return ACE_CLIPBOARD_IOERR_BADLENGTH;
    }
    if ((size_t)request->io_Offset > unit->pending_size)
        memset(unit->pending + unit->pending_size, 0,
               (size_t)request->io_Offset - unit->pending_size);
    if (request->io_Length)
        memcpy(unit->pending + request->io_Offset, request->io_Data,
               request->io_Length);
    if (end > unit->pending_size)
        unit->pending_size = end;
    request->io_Actual = request->io_Length;
    request->io_Offset += request->io_Actual;
    pthread_mutex_unlock(&unit->lock);
    return 0;
}

static LONG update_clip(struct ace_clipboard_unit *unit,
                        struct IOClipReq *request)
{
    unsigned char *old_data;

    pthread_mutex_lock(&unit->lock);
    if (!unit->write_active || request->io_ClipID != unit->write_id) {
        pthread_mutex_unlock(&unit->lock);
        request->io_ClipID = -1;
        return ACE_CLIPBOARD_IOERR_ABORTED;
    }
    if (ace_clipboard_store_commit(unit->public_unit.cu_UnitNum,
                                   unit->pending, unit->pending_size) != 0) {
        pthread_mutex_unlock(&unit->lock);
        return ACE_CLIPBOARD_IOERR_BADLENGTH;
    }
    old_data = unit->committed;
    unit->committed = unit->pending;
    unit->committed_size = unit->pending_size;
    unit->committed_valid = 1;
    unit->pending = NULL;
    unit->pending_size = 0;
    unit->pending_capacity = 0;
    unit->write_active = 0;
    unit->post_id = 0;
    pthread_cond_broadcast(&unit->condition);
    pthread_mutex_unlock(&unit->lock);
    free(old_data);
    notify_hooks(unit, CMD_UPDATE, request->io_ClipID);
    request->io_ClipID = -1;
    return 0;
}

static LONG device_query(struct IOClipReq *request)
{
    struct ace_ns_device_query *query;

    if (!request->io_Data)
        return ACE_CLIPBOARD_IOERR_BADADDRESS;
    if (request->io_Length <
        offsetof(struct ace_ns_device_query, supported_commands) +
            sizeof(query->supported_commands))
        return ACE_CLIPBOARD_IOERR_BADLENGTH;
    query = (struct ace_ns_device_query *)request->io_Data;
    query->dev_query_format = 0;
    query->size_available = sizeof(*query);
    query->device_type = ACE_CLIPBOARD_TYPE;
    query->device_sub_type = 0;
    query->supported_commands = supported_commands;
    request->io_Actual = sizeof(*query);
    return 0;
}

int ace_clipboard_device_owns_request(const struct IORequest *request)
{
    pthread_once(&device.initialized, initialize_device);
    return request && request->io_Device == (struct Device *)&device;
}

LONG ace_clipboard_device_open(ULONG unit_number, struct IORequest *request)
{
    struct ace_clipboard_unit *unit;

    pthread_once(&device.initialized, initialize_device);
    if (!request || unit_number >= ACE_CLIPBOARD_UNIT_COUNT)
        return ACE_CLIPBOARD_IOERR_BADADDRESS;
    unit = &device.units[unit_number];
    request->io_Device = (struct Device *)&device;
    request->io_Unit = (struct Unit *)&unit->public_unit;
    return 0;
}

void ace_clipboard_device_close(struct IORequest *request)
{
    (void)request;
}

LONG ace_clipboard_device_io(struct IORequest *request,
                             ace_clipboard_cancelled cancelled,
                             void *cancel_context)
{
    struct IOClipReq *clip_request = (struct IOClipReq *)request;
    struct ace_clipboard_unit *unit;
    LONG error;

    if (!request || !ace_clipboard_device_owns_request(request))
        return ACE_CLIPBOARD_IOERR_BADADDRESS;
    unit = unit_from_request(clip_request);
    if (!unit)
        return ACE_CLIPBOARD_IOERR_BADADDRESS;
    clip_request->io_Actual = 0;
    clip_request->io_Error = 0;
    switch (clip_request->io_Command) {
    case CMD_READ:
        error = read_clip(unit, clip_request, cancelled, cancel_context);
        break;
    case CMD_WRITE:
        error = write_clip(unit, clip_request);
        break;
    case CMD_UPDATE:
        error = update_clip(unit, clip_request);
        break;
    case CBD_POST:
        pthread_mutex_lock(&unit->lock);
        unit->write_id = next_id(unit->write_id);
        unit->post_id = unit->write_id;
        clip_request->io_ClipID = unit->post_id;
        pthread_cond_broadcast(&unit->condition);
        pthread_mutex_unlock(&unit->lock);
        notify_hooks(unit, CBD_POST, clip_request->io_ClipID);
        error = 0;
        break;
    case CBD_CURRENTREADID:
        if (unit->public_unit.cu_UnitNum == PRIMARY_CLIP)
            (void)ace_clipboard_host_refresh();
        (void)refresh_unit_from_store(unit);
        pthread_mutex_lock(&unit->lock);
        clip_request->io_ClipID = unit->read_id;
        pthread_mutex_unlock(&unit->lock);
        error = 0;
        break;
    case CBD_CURRENTWRITEID:
        if (unit->public_unit.cu_UnitNum == PRIMARY_CLIP)
            (void)ace_clipboard_host_refresh();
        (void)refresh_unit_from_store(unit);
        pthread_mutex_lock(&unit->lock);
        clip_request->io_ClipID = unit->write_id;
        pthread_mutex_unlock(&unit->lock);
        error = 0;
        break;
    case CBD_CHANGEHOOK:
        error = change_hook(clip_request);
        break;
    case ACE_CLIPBOARD_NS_QUERY:
        error = device_query(clip_request);
        break;
    default:
        error = ACE_CLIPBOARD_IOERR_NOCMD;
        break;
    }
    clip_request->io_Error = (BYTE)error;
    return error;
}

void ace_clipboard_device_abort(struct IORequest *request)
{
    size_t index;

    if (!ace_clipboard_device_owns_request(request))
        return;
    for (index = 0; index < ACE_CLIPBOARD_UNIT_COUNT; index++) {
        pthread_mutex_lock(&device.units[index].lock);
        pthread_cond_broadcast(&device.units[index].condition);
        pthread_mutex_unlock(&device.units[index].lock);
    }
}
