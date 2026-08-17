#define _POSIX_C_SOURCE 200809L

#include "aros_exec_runtime.h"
#include "clipboard_device.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <devices/timer.h>
#include <exec/devices.h>
#include <exec/memory.h>

struct ace_port_state {
    struct MsgPort *port;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    struct ace_port_state *next;
};

struct ace_host_unit {
    struct Device device;
    struct Unit unit;
    pthread_mutex_t lock;
    pthread_cond_t input_condition;
    pthread_cond_t output_condition;
    unsigned char *input;
    size_t input_length;
    size_t input_offset;
    size_t input_capacity;
    unsigned char *output;
    size_t output_length;
    size_t output_offset;
    size_t output_capacity;
    int timer;
    int closing;
    struct ace_host_unit *next;
};

struct ace_io_state {
    struct IORequest *request;
    struct ace_host_unit *unit;
    int clipboard;
    pthread_t worker;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int finished;
    int aborted;
    struct ace_io_state *next;
};

static pthread_mutex_t ports_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t units_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t signal_condition = PTHREAD_COND_INITIALIZER;
static struct ace_port_state *ports;
static struct ace_io_state *io_states;
static struct ace_host_unit *units;
static ULONG pending_signals;
static UBYTE next_signal_bit;
static struct ace_host_unit *last_console;

static struct ace_port_state *find_port_locked(struct MsgPort *port)
{
    struct ace_port_state *state;

    for (state = ports; state; state = state->next)
        if (state->port == port)
            return state;
    return NULL;
}

static struct ace_port_state *ensure_port(struct MsgPort *port)
{
    struct ace_port_state *state;

    if (!port)
        return NULL;
    pthread_mutex_lock(&ports_lock);
    state = find_port_locked(port);
    if (!state) {
        state = calloc(1, sizeof(*state));
        if (state) {
            state->port = port;
            pthread_mutex_init(&state->lock, NULL);
            pthread_cond_init(&state->condition, NULL);
            state->next = ports;
            ports = state;
        }
    }
    pthread_mutex_unlock(&ports_lock);
    return state;
}

static void remove_port(struct MsgPort *port, int free_port)
{
    struct ace_port_state **cursor;
    struct ace_port_state *state = NULL;

    pthread_mutex_lock(&ports_lock);
    cursor = &ports;
    while (*cursor) {
        if ((*cursor)->port == port) {
            state = *cursor;
            *cursor = state->next;
            break;
        }
        cursor = &(*cursor)->next;
    }
    pthread_mutex_unlock(&ports_lock);
    if (!state)
        return;
    pthread_cond_destroy(&state->condition);
    pthread_mutex_destroy(&state->lock);
    free(state);
    if (free_port)
        free(port);
}

static struct Node *pop_message(struct List *list)
{
    struct Node *node = list->lh_Head;

    if (!node || !node->ln_Succ)
        return NULL;
    REMOVE(node);
    return node;
}

struct MsgPort *CreateMsgPort(void)
{
    struct MsgPort *port = calloc(1, sizeof(*port));

    if (!port)
        return NULL;
    port->mp_Flags = PA_SIGNAL;
    port->mp_SigBit = next_signal_bit++ & 31;
    NEWLIST(&port->mp_MsgList);
    if (!ensure_port(port)) {
        free(port);
        return NULL;
    }
    return port;
}

void DeleteMsgPort(struct MsgPort *port)
{
    remove_port(port, 1);
}

void PutMsg(struct MsgPort *port, struct Message *message)
{
    struct ace_port_state *state = ensure_port(port);

    if (!state || !message)
        return;
    pthread_mutex_lock(&state->lock);
    ADDTAIL(&port->mp_MsgList, &message->mn_Node);
    pthread_cond_broadcast(&state->condition);
    pthread_mutex_unlock(&state->lock);
    pthread_mutex_lock(&ports_lock);
    pending_signals |= 1UL << port->mp_SigBit;
    pthread_cond_broadcast(&signal_condition);
    pthread_mutex_unlock(&ports_lock);
}

struct Message *GetMsg(struct MsgPort *port)
{
    struct ace_port_state *state = ensure_port(port);
    struct Node *node;

    if (!state)
        return NULL;
    pthread_mutex_lock(&state->lock);
    node = pop_message(&port->mp_MsgList);
    pthread_mutex_unlock(&state->lock);
    if (!node) {
        pthread_mutex_lock(&ports_lock);
        pending_signals &= ~(1UL << port->mp_SigBit);
        pthread_mutex_unlock(&ports_lock);
    }
    return (struct Message *)node;
}

void WaitPort(struct MsgPort *port)
{
    struct ace_port_state *state = ensure_port(port);

    if (!state)
        return;
    pthread_mutex_lock(&state->lock);
    while (!port->mp_MsgList.lh_Head ||
           !port->mp_MsgList.lh_Head->ln_Succ)
        pthread_cond_wait(&state->condition, &state->lock);
    pthread_mutex_unlock(&state->lock);
}

ULONG Wait(ULONG signals)
{
    ULONG result;

    pthread_mutex_lock(&ports_lock);
    while (!(pending_signals & signals))
        pthread_cond_wait(&signal_condition, &ports_lock);
    result = pending_signals & signals;
    pending_signals &= ~result;
    pthread_mutex_unlock(&ports_lock);
    return result;
}

void ace_aros_runtime_signal(ULONG signals)
{
    if (!signals)
        return;
    pthread_mutex_lock(&ports_lock);
    pending_signals |= signals;
    pthread_cond_broadcast(&signal_condition);
    pthread_mutex_unlock(&ports_lock);
}

ULONG ace_aros_runtime_set_signal(ULONG set_mask, ULONG clear_mask)
{
    ULONG old;

    pthread_mutex_lock(&ports_lock);
    old = pending_signals;
    pending_signals |= set_mask;
    pending_signals &= ~clear_mask;
    if (set_mask)
        pthread_cond_broadcast(&signal_condition);
    pthread_mutex_unlock(&ports_lock);
    return old;
}

ULONG ace_aros_runtime_check_signal(ULONG mask)
{
    ULONG result;

    pthread_mutex_lock(&ports_lock);
    result = pending_signals & mask;
    pthread_mutex_unlock(&ports_lock);
    return result;
}

static struct ace_host_unit *unit_from_request(struct IORequest *request)
{
    struct ace_host_unit *unit;

    pthread_mutex_lock(&units_lock);
    for (unit = units; unit; unit = unit->next)
        if ((struct Unit *)request->io_Unit == &unit->unit)
            break;
    pthread_mutex_unlock(&units_lock);
    return unit;
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

static int host_write(struct ace_host_unit *unit, const void *data,
                      size_t length, size_t *actual)
{
    pthread_mutex_lock(&unit->lock);
    if (grow_buffer(&unit->output, &unit->output_capacity,
                    unit->output_length + length) != 0) {
        pthread_mutex_unlock(&unit->lock);
        return -1;
    }
    memcpy(unit->output + unit->output_length, data, length);
    unit->output_length += length;
    pthread_cond_broadcast(&unit->output_condition);
    pthread_mutex_unlock(&unit->lock);
    *actual = length;
    return 0;
}

static int host_read(struct ace_io_state *state, void *data, size_t length,
                     size_t *actual)
{
    struct ace_host_unit *unit = state->unit;
    size_t count;

    pthread_mutex_lock(&unit->lock);
    while (unit->input_length == unit->input_offset && !unit->closing) {
        pthread_mutex_lock(&state->lock);
        if (state->aborted) {
            pthread_mutex_unlock(&state->lock);
            pthread_mutex_unlock(&unit->lock);
            return -2;
        }
        pthread_mutex_unlock(&state->lock);
        pthread_cond_wait(&unit->input_condition, &unit->lock);
    }
    if (unit->closing) {
        pthread_mutex_unlock(&unit->lock);
        return -2;
    }
    count = unit->input_length - unit->input_offset;
    if (count > length)
        count = length;
    memcpy(data, unit->input + unit->input_offset, count);
    unit->input_offset += count;
    if (unit->input_offset == unit->input_length) {
        unit->input_offset = 0;
        unit->input_length = 0;
    }
    pthread_mutex_unlock(&unit->lock);
    *actual = count;
    return 0;
}

static int io_state_cancelled(void *context)
{
    struct ace_io_state *state = context;
    int aborted;

    pthread_mutex_lock(&state->lock);
    aborted = state->aborted;
    pthread_mutex_unlock(&state->lock);
    return aborted;
}

static void *io_worker(void *context)
{
    struct ace_io_state *state = context;
    struct IOStdReq *request = (struct IOStdReq *)state->request;
    size_t actual = 0;
    LONG error = 0;

    if (state->clipboard) {
        error = ace_clipboard_device_io(
            state->request, io_state_cancelled, state);
        actual = ((struct IOStdReq *)request)->io_Actual;
    } else if (state->unit->timer && request->io_Command == TR_ADDREQUEST) {
        struct timerequest *timer = (struct timerequest *)request;
        struct timespec delay = {
            .tv_sec = timer->tr_time.tv_secs,
            .tv_nsec = (long)timer->tr_time.tv_micro * 1000L,
        };
        nanosleep(&delay, NULL);
    } else if (request->io_Command == CMD_READ) {
        error = host_read(state, request->io_Data, request->io_Length,
                          &actual);
    } else if (request->io_Command == CMD_WRITE) {
        error = host_write(state->unit, request->io_Data, request->io_Length,
                           &actual);
    } else {
        error = -3;
    }
    pthread_mutex_lock(&state->lock);
    if (state->aborted)
        error = -2;
    request->io_Error = (BYTE)error;
    request->io_Actual = (ULONG)actual;
    state->finished = 1;
    pthread_cond_broadcast(&state->condition);
    pthread_mutex_unlock(&state->lock);
    if (request->io_Message.mn_ReplyPort)
        PutMsg(request->io_Message.mn_ReplyPort, &request->io_Message);
    return NULL;
}

APTR CreateIORequest(struct MsgPort *reply_port, ULONG size)
{
    struct IORequest *request;

    request = calloc(1, (size_t)size);
    if (!request)
        return NULL;
    request->io_Message.mn_ReplyPort = reply_port;
    request->io_Message.mn_Length = (UWORD)size;
    return request;
}

void DeleteIORequest(struct IORequest *request)
{
    free(request);
}

static struct ace_host_unit *new_unit(int timer)
{
    struct ace_host_unit *unit = calloc(1, sizeof(*unit));

    if (!unit)
        return NULL;
    pthread_mutex_init(&unit->lock, NULL);
    pthread_cond_init(&unit->input_condition, NULL);
    pthread_cond_init(&unit->output_condition, NULL);
    unit->timer = timer;
    pthread_mutex_lock(&units_lock);
    unit->next = units;
    units = unit;
    if (!timer)
        last_console = unit;
    pthread_mutex_unlock(&units_lock);
    return unit;
}

LONG OpenDevice(CONST_STRPTR name, ULONG unit_number,
                struct IORequest *request, ULONG flags)
{
    struct ace_host_unit *unit;

    (void)unit_number;
    (void)flags;
    if (!name || !request)
        return -1;
    if (strcmp(name, "clipboard.device") == 0)
        return ace_clipboard_device_open(unit_number, request);
    if (strcmp(name, "console.device") == 0)
        unit = new_unit(0);
    else if (strcmp(name, TIMERNAME) == 0)
        unit = new_unit(1);
    else
        return -1;
    if (!unit)
        return -1;
    request->io_Device = &unit->device;
    request->io_Unit = &unit->unit;
    return 0;
}

void CloseDevice(struct IORequest *request)
{
    struct ace_host_unit *unit;

    if (!request)
        return;
    if (ace_clipboard_device_owns_request(request)) {
        ace_clipboard_device_close(request);
        return;
    }
    unit = unit_from_request(request);
    if (!unit)
        return;
    pthread_mutex_lock(&unit->lock);
    unit->closing = 1;
    pthread_cond_broadcast(&unit->input_condition);
    pthread_cond_broadcast(&unit->output_condition);
    pthread_mutex_unlock(&unit->lock);
}

static struct ace_io_state *new_io_state(struct IORequest *request)
{
    struct ace_io_state *state = calloc(1, sizeof(*state));

    if (!state)
        return NULL;
    state->request = request;
    state->unit = unit_from_request(request);
    state->clipboard = ace_clipboard_device_owns_request(request);
    if (!state->unit && !state->clipboard) {
        free(state);
        return NULL;
    }
    pthread_mutex_init(&state->lock, NULL);
    pthread_cond_init(&state->condition, NULL);
    pthread_mutex_lock(&io_lock);
    state->next = io_states;
    io_states = state;
    pthread_mutex_unlock(&io_lock);
    return state;
}

void SendIO(struct IORequest *request)
{
    struct ace_io_state *state;

    if (!request || !request->io_Unit)
        return;
    state = new_io_state(request);
    if (!state)
        return;
    request->io_Error = 0;
    ((struct IOStdReq *)request)->io_Actual = 0;
    if (pthread_create(&state->worker, NULL, io_worker, state) != 0) {
        pthread_mutex_lock(&state->lock);
        state->finished = 1;
        state->request->io_Error = -1;
        pthread_cond_broadcast(&state->condition);
        pthread_mutex_unlock(&state->lock);
    }
}

static struct ace_io_state *find_io_state(struct IORequest *request)
{
    struct ace_io_state *state;

    pthread_mutex_lock(&io_lock);
    for (state = io_states; state; state = state->next)
        if (state->request == request)
            break;
    pthread_mutex_unlock(&io_lock);
    return state;
}

LONG WaitIO(struct IORequest *request)
{
    struct ace_io_state *state = find_io_state(request);
    struct Message *message;

    if (!state)
        return -1;
    if (request->io_Message.mn_ReplyPort) {
        do {
            WaitPort(request->io_Message.mn_ReplyPort);
            message = GetMsg(request->io_Message.mn_ReplyPort);
        } while (message != &request->io_Message);
    } else {
        pthread_mutex_lock(&state->lock);
        while (!state->finished)
            pthread_cond_wait(&state->condition, &state->lock);
        pthread_mutex_unlock(&state->lock);
    }
    pthread_join(state->worker, NULL);
    pthread_mutex_lock(&io_lock);
    if (io_states == state)
        io_states = state->next;
    else {
        struct ace_io_state *cursor;
        for (cursor = io_states; cursor && cursor->next; cursor = cursor->next)
            if (cursor->next == state) {
                cursor->next = state->next;
                break;
            }
    }
    pthread_mutex_unlock(&io_lock);
    pthread_cond_destroy(&state->condition);
    pthread_mutex_destroy(&state->lock);
    free(state);
    return request->io_Error;
}

LONG DoIO(struct IORequest *request)
{
    SendIO(request);
    return WaitIO(request);
}

void AbortIO(struct IORequest *request)
{
    struct ace_io_state *state = find_io_state(request);
    struct ace_host_unit *unit;

    if (!state)
        return;
    pthread_mutex_lock(&state->lock);
    state->aborted = 1;
    pthread_mutex_unlock(&state->lock);
    if (state->clipboard) {
        ace_clipboard_device_abort(state->request);
        return;
    }
    unit = state->unit;
    if (unit) {
        pthread_mutex_lock(&unit->lock);
        pthread_cond_broadcast(&unit->input_condition);
        pthread_mutex_unlock(&unit->lock);
    }
}

void *ace_aros_console_last(void)
{
    return last_console;
}

int ace_aros_console_feed(void *context, const void *data, size_t length)
{
    struct ace_host_unit *unit = context;

    if (!unit || unit->timer || (!data && length != 0))
        return -1;
    pthread_mutex_lock(&unit->lock);
    if (grow_buffer(&unit->input, &unit->input_capacity,
                    unit->input_length + length) != 0) {
        pthread_mutex_unlock(&unit->lock);
        return -1;
    }
    memcpy(unit->input + unit->input_length, data, length);
    unit->input_length += length;
    pthread_cond_broadcast(&unit->input_condition);
    pthread_mutex_unlock(&unit->lock);
    return 0;
}

size_t ace_aros_console_take_output(void *context, void *data, size_t length)
{
    struct ace_host_unit *unit = context;
    size_t count;

    if (!unit || unit->timer || (!data && length != 0))
        return 0;
    pthread_mutex_lock(&unit->lock);
    count = unit->output_length - unit->output_offset;
    if (count > length)
        count = length;
    memcpy(data, unit->output + unit->output_offset, count);
    unit->output_offset += count;
    if (unit->output_offset == unit->output_length) {
        unit->output_offset = 0;
        unit->output_length = 0;
    }
    pthread_mutex_unlock(&unit->lock);
    return count;
}
