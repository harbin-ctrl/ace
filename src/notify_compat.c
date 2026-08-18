#define _POSIX_C_SOURCE 200809L

#include "aros_exec_runtime.h"
#include "clipboard_bridge.h"

#include <dos/dos.h>
#include <dos/notify.h>

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

struct ace_clip_signature {
    int exists;
    ino_t inode;
    off_t size;
    time_t seconds;
    long nanoseconds;
};

struct ace_notify_state {
    struct NotifyRequest *request;
    unsigned unit;
    struct ace_clip_signature previous;
    pthread_t thread;
    pthread_mutex_t lock;
    int stopping;
    struct ace_notify_state *next;
};

static pthread_mutex_t notify_lock = PTHREAD_MUTEX_INITIALIZER;
static struct ace_notify_state *notify_states;

static int notify_unit(CONST_STRPTR name, unsigned *unit)
{
    char *end;
    unsigned long value;

    if (!name || strncasecmp(name, "CLIPS:", 6) != 0 || !name[6])
        return -1;
    errno = 0;
    value = strtoul(name + 6, &end, 10);
    if (errno || *end || value >= ACE_CLIPBOARD_UNIT_COUNT)
        return -1;
    *unit = (unsigned)value;
    return 0;
}

static int clip_signature(unsigned unit, struct ace_clip_signature *signature)
{
    char root[512];
    char path[1024];
    struct stat status;

    if (!signature || ace_clipboard_store_root(root, sizeof(root)) != 0 ||
        snprintf(path, sizeof(path), "%s/clip%u", root, unit) >=
            (int)sizeof(path))
        return -1;
    memset(signature, 0, sizeof(*signature));
    if (stat(path, &status) != 0) {
        if (errno == ENOENT)
            return 0;
        return -1;
    }
    signature->exists = 1;
    signature->inode = status.st_ino;
    signature->size = status.st_size;
    signature->seconds = status.st_mtim.tv_sec;
    signature->nanoseconds = status.st_mtim.tv_nsec;
    return 0;
}

static int signature_equal(const struct ace_clip_signature *first,
                           const struct ace_clip_signature *second)
{
    return first->exists == second->exists &&
           (!first->exists ||
            (first->inode == second->inode && first->size == second->size &&
             first->seconds == second->seconds &&
             first->nanoseconds == second->nanoseconds));
}

static void *notify_thread(void *context)
{
    struct ace_notify_state *state = context;

    for (;;) {
        struct ace_clip_signature current = {0};
        int stopping;

        nanosleep(&(struct timespec){.tv_nsec = 20000000L}, NULL);
        (void)clip_signature(state->unit, &current);
        pthread_mutex_lock(&state->lock);
        stopping = state->stopping;
        pthread_mutex_unlock(&state->lock);
        if (stopping)
            break;
        if (!signature_equal(&state->previous, &current) && current.exists)
            ace_aros_runtime_signal_task(
                state->request->nr_stuff.nr_Signal.nr_Task,
                1UL << state->request->nr_stuff.nr_Signal.nr_SignalNum);
        state->previous = current;
    }
    return NULL;
}

BOOL StartNotify(struct NotifyRequest *request)
{
    struct ace_notify_state *state;
    unsigned unit;
    int mutex_ready = 0;

    if (!request || !(request->nr_Flags & NRF_SEND_SIGNAL) ||
        notify_unit(request->nr_Name, &unit) != 0) {
        SetIoErr(ERROR_OBJECT_NOT_FOUND);
        return DOSFALSE;
    }
    state = calloc(1, sizeof(*state));
    if (state && pthread_mutex_init(&state->lock, NULL) == 0) {
        mutex_ready = 1;
        state->request = request;
        state->unit = unit;
        if (clip_signature(unit, &state->previous) != 0) {
            pthread_mutex_destroy(&state->lock);
            free(state);
            state = NULL;
            mutex_ready = 0;
        }
    } else {
        free(state);
        state = NULL;
    }
    if (!state || !mutex_ready) {
        SetIoErr(ERROR_NO_FREE_STORE);
        return DOSFALSE;
    }
    pthread_mutex_lock(&notify_lock);
    state->next = notify_states;
    notify_states = state;
    pthread_mutex_unlock(&notify_lock);
    if (pthread_create(&state->thread, NULL, notify_thread, state) != 0) {
        pthread_mutex_lock(&notify_lock);
        if (notify_states == state)
            notify_states = state->next;
        pthread_mutex_unlock(&notify_lock);
        pthread_mutex_destroy(&state->lock);
        free(state);
        SetIoErr(ERROR_NO_FREE_STORE);
        return DOSFALSE;
    }
    return DOSTRUE;
}

void EndNotify(struct NotifyRequest *request)
{
    struct ace_notify_state **cursor;
    struct ace_notify_state *state = NULL;

    if (!request)
        return;
    pthread_mutex_lock(&notify_lock);
    cursor = &notify_states;
    while (*cursor && (*cursor)->request != request)
        cursor = &(*cursor)->next;
    if (*cursor) {
        state = *cursor;
        *cursor = state->next;
    }
    pthread_mutex_unlock(&notify_lock);
    if (!state)
        return;
    pthread_mutex_lock(&state->lock);
    state->stopping = 1;
    pthread_mutex_unlock(&state->lock);
    pthread_join(state->thread, NULL);
    pthread_mutex_destroy(&state->lock);
    free(state);
}
