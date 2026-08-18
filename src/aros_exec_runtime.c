#define _POSIX_C_SOURCE 200809L

#include "aros_exec_runtime.h"
#include "clipboard_device.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <devices/timer.h>
#include <exec/devices.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <dos/dosextens.h>

struct ace_port_state {
    struct MsgPort *port;
    struct Task *owner;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    struct ace_port_state *next;
};

struct ace_task_state {
    struct Task *task;
    ULONG allocated_signals;
    ULONG pending_signals;
    struct ace_task_state *next;
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
    int worker_started;
    struct ace_io_state *next;
};

static pthread_mutex_t ports_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t io_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t units_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t signal_condition = PTHREAD_COND_INITIALIZER;
static struct ace_port_state *ports;
static struct ace_task_state *tasks;
static struct ace_io_state *io_states;
static struct ace_host_unit *units;
/* A signal handler cannot take ports_lock.  It leaves its bits here for the
   normal runtime path to merge while holding that lock. */
static volatile sig_atomic_t host_pending_signals;
/* One ACE host process may bootstrap a Process task and later enter upstream
   code through an implicit Exec task identity.  Broker control connections
   address the host process, so retain their bits until the process consumes
   them, independently of that internal identity. */
static ULONG broker_pending_signals;
static UBYTE next_signal_bit;
static struct ace_host_unit *last_console;
static _Thread_local struct Task *current_task;
static _Thread_local unsigned char implicit_task_identity;
static int host_signal_pipe[2] = {-1, -1};
static pthread_t host_signal_thread;
static struct Task *host_signal_target;

static struct ace_task_state *task_state_locked(struct Task *task, int create);

static struct Task *runtime_current_task(void)
{
    if (!current_task)
        current_task = (struct Task *)&implicit_task_identity;
    return current_task;
}

void ace_aros_runtime_set_current_task(struct Task *task)
{
    current_task = task;
    if (task) {
        pthread_mutex_lock(&ports_lock);
        (void)task_state_locked(task, 1);
        host_signal_target = task;
        pthread_mutex_unlock(&ports_lock);
    }
}

static struct ace_task_state *task_state_locked(struct Task *task, int create)
{
    struct ace_task_state *state;

    if (!task)
        task = runtime_current_task();
    for (state = tasks; state; state = state->next) {
        if (state->task == task)
            return state;
    }
    if (!create)
        return NULL;
    state = calloc(1, sizeof(*state));
    if (!state)
        return NULL;
    state->task = task;
    state->next = tasks;
    tasks = state;
    return state;
}

int ace_aros_runtime_register_task(struct Task *task)
{
    int result;

    if (!task)
        return -1;
    pthread_mutex_lock(&ports_lock);
    result = task_state_locked(task, 1) ? 0 : -1;
    pthread_mutex_unlock(&ports_lock);
    return result;
}

void ace_aros_runtime_unregister_task(struct Task *task)
{
    struct ace_task_state **cursor;

    if (!task)
        return;
    pthread_mutex_lock(&ports_lock);
    cursor = &tasks;
    while (*cursor && (*cursor)->task != task)
        cursor = &(*cursor)->next;
    if (*cursor) {
        struct ace_task_state *state = *cursor;

        *cursor = state->next;
        free(state);
    }
    if (host_signal_target == task)
        host_signal_target = NULL;
    for (struct ace_port_state *port = ports; port; port = port->next) {
        if (port->owner == task)
            port->owner = NULL;
    }
    pthread_mutex_unlock(&ports_lock);
    if (current_task == task)
        current_task = NULL;
}

struct Task *ace_aros_runtime_find_task(CONST_STRPTR name)
{
    struct ace_task_state *state;
    struct Task *result = NULL;

    if (!name)
        return runtime_current_task();
    pthread_mutex_lock(&ports_lock);
    for (state = tasks; state; state = state->next) {
        const char *task_name = state->task->tc_Node.ln_Name;

        if (task_name && strcmp(task_name, name) == 0) {
            result = state->task;
            break;
        }
    }
    pthread_mutex_unlock(&ports_lock);
    return result;
}

static void host_break_handler(int signal_number)
{
    unsigned char notification = (unsigned char)signal_number;

    if (signal_number == SIGUSR1)
        host_pending_signals |= (sig_atomic_t)(1UL << 12); /* Ctrl-C */
    else if (signal_number == SIGUSR2)
        host_pending_signals |= (sig_atomic_t)(1UL << 13); /* Ctrl-D */
    else if (signal_number == SIGRTMIN)
        host_pending_signals |= (sig_atomic_t)(1UL << 14); /* Ctrl-E */
    else if (signal_number == SIGRTMIN + 1)
        host_pending_signals |= (sig_atomic_t)(1UL << 15); /* Ctrl-F */
    if (host_signal_pipe[1] >= 0)
        (void)write(host_signal_pipe[1], &notification, sizeof(notification));
}

static void *host_signal_dispatch(void *unused)
{
    unsigned char notification;

    (void)unused;
    while (read(host_signal_pipe[0], &notification, sizeof(notification)) > 0) {
        ULONG signals = notification == SIGUSR1 ? 1UL << 12 :
                        notification == SIGUSR2 ? 1UL << 13 :
                        notification == SIGRTMIN ? 1UL << 14 :
                        notification == SIGRTMIN + 1 ? 1UL << 15 : 0;
        struct Task *target;

        if (!signals)
            continue;
        pthread_mutex_lock(&ports_lock);
        target = host_signal_target;
        pthread_mutex_unlock(&ports_lock);
        if (target)
            ace_aros_runtime_signal_task(target, signals);
    }
    return NULL;
}

static void install_host_break_handler(void) __attribute__((constructor));

static void install_host_break_handler(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = host_break_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    if (pipe(host_signal_pipe) == 0) {
        int flags;

        flags = fcntl(host_signal_pipe[1], F_GETFL);
        if (flags >= 0)
            (void)fcntl(host_signal_pipe[1], F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(host_signal_pipe[0], F_GETFD);
        if (flags >= 0)
            (void)fcntl(host_signal_pipe[0], F_SETFD, flags | FD_CLOEXEC);
        flags = fcntl(host_signal_pipe[1], F_GETFD);
        if (flags >= 0)
            (void)fcntl(host_signal_pipe[1], F_SETFD, flags | FD_CLOEXEC);
        if (pthread_create(&host_signal_thread, NULL, host_signal_dispatch,
                           NULL) == 0)
            (void)pthread_detach(host_signal_thread);
        else {
            close(host_signal_pipe[0]);
            close(host_signal_pipe[1]);
            host_signal_pipe[0] = host_signal_pipe[1] = -1;
        }
    }
    (void)sigaction(SIGUSR1, &action, NULL);
    (void)sigaction(SIGUSR2, &action, NULL);
    (void)sigaction(SIGRTMIN, &action, NULL);
    (void)sigaction(SIGRTMIN + 1, &action, NULL);
}

void ace_aros_runtime_raise_from_host(ULONG signals)
{
    host_pending_signals |= (sig_atomic_t)signals;
}

static void merge_host_signals_locked(void)
{
    sig_atomic_t signals = host_pending_signals;
    struct ace_task_state *state;

    if (signals) {
        host_pending_signals = 0;
        state = task_state_locked(runtime_current_task(), 1);
        if (state)
            state->pending_signals |= (ULONG)signals;
    }
}

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
            state->owner = runtime_current_task();
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
    {
        struct ace_task_state *owner = task_state_locked(state->owner, 1);

        if (owner)
            owner->pending_signals |= 1UL << port->mp_SigBit;
    }
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
        {
            struct ace_task_state *owner = task_state_locked(state->owner, 0);

            if (owner)
                owner->pending_signals &= ~(1UL << port->mp_SigBit);
        }
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
    struct ace_task_state *state;

    pthread_mutex_lock(&ports_lock);
    merge_host_signals_locked();
    state = task_state_locked(runtime_current_task(), 1);
    if (!state) {
        pthread_mutex_unlock(&ports_lock);
        return 0;
    }
    while (!((state->pending_signals | broker_pending_signals) & signals)) {
        /* The host-signal dispatcher broadcasts this condition after its
           async-signal-safe pipe handoff, just like Exec wakes Wait(). */
        (void)pthread_cond_wait(&signal_condition, &ports_lock);
        merge_host_signals_locked();
    }
    result = (state->pending_signals | broker_pending_signals) & signals;
    state->pending_signals &= ~result;
    broker_pending_signals &= ~result;
    pthread_mutex_unlock(&ports_lock);
    return result;
}

void ace_aros_runtime_signal(ULONG signals)
{
    ace_aros_runtime_signal_task(runtime_current_task(), signals);
}

void ace_aros_runtime_signal_task(struct Task *task, ULONG signals)
{
    struct ace_task_state *state;

    if (!signals)
        return;
    pthread_mutex_lock(&ports_lock);
    merge_host_signals_locked();
    state = task_state_locked(task, 0);
    if (state)
        state->pending_signals |= signals;
    pthread_cond_broadcast(&signal_condition);
    pthread_mutex_unlock(&ports_lock);
}

void ace_aros_runtime_signal_local_tasks(ULONG signals)
{
    struct ace_task_state *state;

    if (!signals)
        return;
    pthread_mutex_lock(&ports_lock);
    broker_pending_signals |= signals;
    for (state = tasks; state; state = state->next)
        state->pending_signals |= signals;
    pthread_cond_broadcast(&signal_condition);
    pthread_mutex_unlock(&ports_lock);
}

ULONG ace_aros_runtime_set_signal(ULONG set_mask, ULONG clear_mask)
{
    ULONG old;
    struct ace_task_state *state;

    pthread_mutex_lock(&ports_lock);
    merge_host_signals_locked();
    state = task_state_locked(runtime_current_task(), 1);
    old = state ? state->pending_signals : 0;
    if (state) {
        state->pending_signals |= set_mask;
        state->pending_signals &= ~clear_mask;
    }
    if (set_mask)
        pthread_cond_broadcast(&signal_condition);
    pthread_mutex_unlock(&ports_lock);
    return old;
}

ULONG ace_aros_runtime_check_signal(ULONG mask)
{
    ULONG result;
    struct ace_task_state *state;

    pthread_mutex_lock(&ports_lock);
    merge_host_signals_locked();
    state = task_state_locked(runtime_current_task(), 1);
    result = state ? state->pending_signals & mask : 0;
    pthread_mutex_unlock(&ports_lock);
    return result;
}

LONG ace_aros_runtime_alloc_signal(LONG signal_number)
{
    struct ace_task_state *state;
    LONG result = -1;

    if (signal_number < -1 || signal_number >= 32)
        return -1;
    pthread_mutex_lock(&ports_lock);
    state = task_state_locked(runtime_current_task(), 1);
    if (state && signal_number >= 0) {
        ULONG bit = 1UL << signal_number;

        if (!(state->allocated_signals & bit)) {
            state->allocated_signals |= bit;
            result = signal_number;
        }
    } else if (state) {
        for (LONG bit = 0; bit < 32; bit++) {
            ULONG mask = 1UL << bit;

            if (!(state->allocated_signals & mask)) {
                state->allocated_signals |= mask;
                result = bit;
                break;
            }
        }
    }
    pthread_mutex_unlock(&ports_lock);
    return result;
}

void ace_aros_runtime_free_signal(LONG signal_number)
{
    struct ace_task_state *state;

    if (signal_number < 0 || signal_number >= 32)
        return;
    pthread_mutex_lock(&ports_lock);
    state = task_state_locked(runtime_current_task(), 0);
    if (state) {
        ULONG bit = 1UL << signal_number;

        state->allocated_signals &= ~bit;
        state->pending_signals &= ~bit;
    }
    pthread_mutex_unlock(&ports_lock);
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

/* timer.device requests are asynchronous.  In particular, AbortIO() must
   cause their reply immediately: callers such as the AROS Wait command abort
   a timer after receiving Ctrl-C, then WaitIO() for that reply before they
   can return to the shell.  nanosleep() cannot be woken by AbortIO(), so use
   the request's condition variable as an interruptible timer instead. */
static int wait_for_timer_or_abort(struct ace_io_state *state,
                                   const struct timerequest *timer)
{
    struct timespec deadline;
    int outcome = 0;

    if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0)
        return -1;
    deadline.tv_sec += timer->tr_time.tv_secs;
    deadline.tv_nsec += (long)timer->tr_time.tv_micro * 1000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += deadline.tv_nsec / 1000000000L;
        deadline.tv_nsec %= 1000000000L;
    }
    pthread_mutex_lock(&state->lock);
    while (!state->aborted) {
        outcome = pthread_cond_timedwait(&state->condition, &state->lock,
                                         &deadline);
        if (outcome == ETIMEDOUT)
            break;
        if (outcome != 0)
            break;
    }
    if (state->aborted)
        outcome = -2;
    else if (outcome == ETIMEDOUT)
        outcome = 0;
    pthread_mutex_unlock(&state->lock);
    return outcome;
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
        error = wait_for_timer_or_abort(state, timer);
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
    struct ace_host_unit **cursor;

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
    /* CloseDevice is only valid after outstanding I/O has completed. */
    pthread_mutex_lock(&units_lock);
    cursor = &units;
    while (*cursor && *cursor != unit)
        cursor = &(*cursor)->next;
    if (*cursor)
        *cursor = unit->next;
    if (last_console == unit)
        last_console = NULL;
    pthread_mutex_unlock(&units_lock);
    pthread_cond_destroy(&unit->input_condition);
    pthread_cond_destroy(&unit->output_condition);
    pthread_mutex_destroy(&unit->lock);
    free(unit);
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
    {
        pthread_condattr_t attributes;

        if (pthread_condattr_init(&attributes) != 0) {
            pthread_mutex_destroy(&state->lock);
            free(state);
            return NULL;
        }
        if (pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) != 0 ||
            pthread_cond_init(&state->condition, &attributes) != 0) {
            pthread_condattr_destroy(&attributes);
            pthread_mutex_destroy(&state->lock);
            free(state);
            return NULL;
        }
        pthread_condattr_destroy(&attributes);
    }
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
    } else
        state->worker_started = 1;
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

/* WaitIO may share a reply port with other requests.  Keep replies that do
   not belong to this request and put them back at the front, in their original
   order, before returning. */
static void restore_reply_messages(struct MsgPort *port,
                                   struct Message *last)
{
    struct ace_port_state *state = ensure_port(port);

    if (!state)
        return;
    pthread_mutex_lock(&state->lock);
    while (last) {
        struct Message *previous = (struct Message *)last->mn_Node.ln_Pred;

        {
            struct Node *node = &last->mn_Node;
            struct Node *head = port->mp_MsgList.lh_Head;

            node->ln_Succ = head;
            node->ln_Pred = (struct Node *)&port->mp_MsgList;
            head->ln_Pred = node;
            port->mp_MsgList.lh_Head = node;
        }
        last = previous;
    }
    pthread_cond_broadcast(&state->condition);
    pthread_mutex_unlock(&state->lock);
}

LONG WaitIO(struct IORequest *request)
{
    struct ace_io_state *state = find_io_state(request);
    struct Message *message;
    struct Message *deferred_last = NULL;

    if (!state)
        return -1;
    if (request->io_Message.mn_ReplyPort) {
        do {
            WaitPort(request->io_Message.mn_ReplyPort);
            message = GetMsg(request->io_Message.mn_ReplyPort);
            if (message != &request->io_Message) {
                message->mn_Node.ln_Pred = (struct Node *)deferred_last;
                if (deferred_last)
                    deferred_last->mn_Node.ln_Succ = &message->mn_Node;
                deferred_last = message;
            }
        } while (message != &request->io_Message);
        restore_reply_messages(request->io_Message.mn_ReplyPort,
                               deferred_last);
    } else {
        pthread_mutex_lock(&state->lock);
        while (!state->finished)
            pthread_cond_wait(&state->condition, &state->lock);
        pthread_mutex_unlock(&state->lock);
    }
    if (state->worker_started)
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

struct IORequest *CheckIO(struct IORequest *request)
{
    struct ace_io_state *state = find_io_state(request);
    int finished;

    if (!state)
        return request;
    pthread_mutex_lock(&state->lock);
    finished = state->finished;
    pthread_mutex_unlock(&state->lock);
    return finished ? request : NULL;
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
    pthread_cond_broadcast(&state->condition);
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
