#define _POSIX_C_SOURCE 200809L

#include "exec_compat.h"

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define EXEC_NAME_SIZE 64

struct amiga_exec_task {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    uint32_t allocated_signals;
    uint32_t pending_signals;
    char name[EXEC_NAME_SIZE];
    amiga_exec_task_entry entry;
    void *context;
    void *result;
    int thread_started;
    int joined;
    int alive;
    struct amiga_exec_task *next;
};

struct amiga_exec_msg_port {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    struct amiga_exec_task *owner;
    int signal_number;
    char name[EXEC_NAME_SIZE];
    struct amiga_exec_message *head;
    struct amiga_exec_message *tail;
    size_t waiters;
    int closing;
};

struct amiga_exec_library {
    char name[EXEC_NAME_SIZE];
    unsigned long version;
    void *base;
    unsigned long opens;
    struct amiga_exec_library *next;
};

struct amiga_exec_device {
    char name[EXEC_NAME_SIZE];
    void *context;
    amiga_exec_device_open open;
    amiga_exec_device_close close;
    struct amiga_exec_device *next;
};

static pthread_mutex_t task_list_lock = PTHREAD_MUTEX_INITIALIZER;
static struct amiga_exec_task *task_list;
static _Thread_local struct amiga_exec_task *current_task;

static pthread_mutex_t registry_lock = PTHREAD_MUTEX_INITIALIZER;
static struct amiga_exec_library *library_list;
static struct amiga_exec_device *device_list;

static void copy_name(char *destination, const char *source)
{
    if (!source)
        source = "";
    strncpy(destination, source, EXEC_NAME_SIZE - 1);
    destination[EXEC_NAME_SIZE - 1] = '\0';
}

static struct amiga_exec_task *allocate_task(const char *name)
{
    struct amiga_exec_task *task = calloc(1, sizeof(*task));
    int lock_initialized = 0;

    if (!task)
        return NULL;
    if (pthread_mutex_init(&task->lock, NULL) == 0)
        lock_initialized = 1;
    if (!lock_initialized || pthread_cond_init(&task->condition, NULL) != 0) {
        if (lock_initialized)
            pthread_mutex_destroy(&task->lock);
        free(task);
        return NULL;
    }
    copy_name(task->name, name);
    task->alive = 1;
    return task;
}

static void add_task(struct amiga_exec_task *task)
{
    pthread_mutex_lock(&task_list_lock);
    task->next = task_list;
    task_list = task;
    pthread_mutex_unlock(&task_list_lock);
}

static void remove_task(struct amiga_exec_task *task)
{
    struct amiga_exec_task **cursor;

    pthread_mutex_lock(&task_list_lock);
    cursor = &task_list;
    while (*cursor && *cursor != task)
        cursor = &(*cursor)->next;
    if (*cursor == task)
        *cursor = task->next;
    pthread_mutex_unlock(&task_list_lock);
}

int amiga_exec_register_current_task(const char *name,
                                     struct amiga_exec_task **task_out)
{
    struct amiga_exec_task *task;

    if (!task_out || current_task)
        return EINVAL;
    task = allocate_task(name);
    if (!task)
        return ENOMEM;
    task->thread = pthread_self();
    add_task(task);
    current_task = task;
    *task_out = task;
    return 0;
}

void amiga_exec_unregister_current_task(struct amiga_exec_task *task)
{
    if (!task || current_task != task)
        return;
    remove_task(task);
    pthread_cond_destroy(&task->condition);
    pthread_mutex_destroy(&task->lock);
    current_task = NULL;
    free(task);
}

struct amiga_exec_task *amiga_exec_current_task(void)
{
    return current_task;
}

const char *amiga_exec_task_name(const struct amiga_exec_task *task)
{
    return task ? task->name : NULL;
}

static void *task_start(void *context)
{
    struct amiga_exec_task *task = context;

    current_task = task;
    task->result = task->entry(task->context);
    pthread_mutex_lock(&task->lock);
    task->alive = 0;
    pthread_cond_broadcast(&task->condition);
    pthread_mutex_unlock(&task->lock);
    return task->result;
}

int amiga_exec_create_task(const char *name, amiga_exec_task_entry entry,
                           void *context, struct amiga_exec_task **task_out)
{
    struct amiga_exec_task *task;

    if (!entry || !task_out)
        return EINVAL;
    task = allocate_task(name);
    if (!task)
        return ENOMEM;
    task->entry = entry;
    task->context = context;
    add_task(task);
    if (pthread_create(&task->thread, NULL, task_start, task) != 0) {
        remove_task(task);
        pthread_cond_destroy(&task->condition);
        pthread_mutex_destroy(&task->lock);
        free(task);
        return EAGAIN;
    }
    task->thread_started = 1;
    *task_out = task;
    return 0;
}

int amiga_exec_join_task(struct amiga_exec_task *task, void **result_out)
{
    void *result;

    if (!task || !task->thread_started || task->joined)
        return EINVAL;
    if (pthread_join(task->thread, &result) != 0)
        return EINVAL;
    task->joined = 1;
    if (result_out)
        *result_out = result;
    return 0;
}

void amiga_exec_delete_task(struct amiga_exec_task *task)
{
    if (!task)
        return;
    if (task == current_task) {
        amiga_exec_unregister_current_task(task);
        return;
    }
    if (task->thread_started && !task->joined)
        (void)amiga_exec_join_task(task, NULL);
    remove_task(task);
    pthread_cond_destroy(&task->condition);
    pthread_mutex_destroy(&task->lock);
    free(task);
}

int amiga_exec_alloc_signal(struct amiga_exec_task *task, int signal_number)
{
    uint32_t bit;
    int result = -1;

    if (!task || signal_number < -1 || signal_number >= AMIGA_EXEC_SIGNAL_COUNT)
        return -1;
    pthread_mutex_lock(&task->lock);
    if (signal_number >= 0) {
        bit = 1u << signal_number;
        if (!(task->allocated_signals & bit)) {
            task->allocated_signals |= bit;
            result = signal_number;
        }
    } else {
        for (int index = 0; index < AMIGA_EXEC_SIGNAL_COUNT; index++) {
            bit = 1u << index;
            if (!(task->allocated_signals & bit)) {
                task->allocated_signals |= bit;
                result = index;
                break;
            }
        }
    }
    pthread_mutex_unlock(&task->lock);
    return result;
}

int amiga_exec_free_signal(struct amiga_exec_task *task, int signal_number)
{
    uint32_t bit;

    if (!task || signal_number < 0 || signal_number >= AMIGA_EXEC_SIGNAL_COUNT)
        return EINVAL;
    bit = 1u << signal_number;
    pthread_mutex_lock(&task->lock);
    task->allocated_signals &= ~bit;
    task->pending_signals &= ~bit;
    pthread_mutex_unlock(&task->lock);
    return 0;
}

uint32_t amiga_exec_set_signal(struct amiga_exec_task *task,
                               uint32_t set_mask, uint32_t clear_mask)
{
    uint32_t old;

    if (!task)
        return 0;
    pthread_mutex_lock(&task->lock);
    old = task->pending_signals;
    task->pending_signals |= set_mask;
    task->pending_signals &= ~clear_mask;
    if (set_mask)
        pthread_cond_broadcast(&task->condition);
    pthread_mutex_unlock(&task->lock);
    return old;
}

uint32_t amiga_exec_check_signal(struct amiga_exec_task *task, uint32_t mask)
{
    uint32_t result;

    if (!task)
        return 0;
    pthread_mutex_lock(&task->lock);
    result = task->pending_signals & mask;
    pthread_mutex_unlock(&task->lock);
    return result;
}

uint32_t amiga_exec_wait(struct amiga_exec_task *task, uint32_t mask)
{
    uint32_t result;

    if (!task || mask == 0)
        return 0;
    pthread_mutex_lock(&task->lock);
    while (!(task->pending_signals & mask) && task->alive)
        pthread_cond_wait(&task->condition, &task->lock);
    result = task->pending_signals & mask;
    task->pending_signals &= ~mask;
    pthread_mutex_unlock(&task->lock);
    return result;
}

int amiga_exec_signal(struct amiga_exec_task *task, uint32_t mask)
{
    if (!task)
        return EINVAL;
    (void)amiga_exec_set_signal(task, mask, 0);
    return 0;
}

int amiga_exec_create_msg_port(struct amiga_exec_task *owner,
                               int signal_number, const char *name,
                               struct amiga_exec_msg_port **port_out)
{
    struct amiga_exec_msg_port *port;
    int lock_initialized = 0;

    if (!port_out || signal_number < -1 ||
        signal_number >= AMIGA_EXEC_SIGNAL_COUNT)
        return EINVAL;
    port = calloc(1, sizeof(*port));
    if (!port)
        return ENOMEM;
    if (pthread_mutex_init(&port->lock, NULL) == 0)
        lock_initialized = 1;
    if (!lock_initialized || pthread_cond_init(&port->condition, NULL) != 0) {
        if (lock_initialized)
            pthread_mutex_destroy(&port->lock);
        free(port);
        return ENOMEM;
    }
    port->owner = owner;
    port->signal_number = signal_number;
    copy_name(port->name, name);
    *port_out = port;
    return 0;
}

void amiga_exec_delete_msg_port(struct amiga_exec_msg_port *port)
{
    if (!port)
        return;
    pthread_mutex_lock(&port->lock);
    port->closing = 1;
    pthread_cond_broadcast(&port->condition);
    while (port->waiters != 0)
        pthread_cond_wait(&port->condition, &port->lock);
    pthread_mutex_unlock(&port->lock);
    pthread_cond_destroy(&port->condition);
    pthread_mutex_destroy(&port->lock);
    free(port);
}

int amiga_exec_put_msg(struct amiga_exec_msg_port *port,
                       struct amiga_exec_message *message)
{
    if (!port || !message)
        return EINVAL;
    pthread_mutex_lock(&port->lock);
    if (port->closing) {
        pthread_mutex_unlock(&port->lock);
        return EPIPE;
    }
    message->next = NULL;
    if (port->tail)
        port->tail->next = message;
    else
        port->head = message;
    port->tail = message;
    pthread_cond_signal(&port->condition);
    pthread_mutex_unlock(&port->lock);
    if (port->owner && port->signal_number >= 0)
        return amiga_exec_signal(port->owner, 1u << port->signal_number);
    return 0;
}

struct amiga_exec_message *amiga_exec_get_msg(struct amiga_exec_msg_port *port)
{
    struct amiga_exec_message *message;

    if (!port)
        return NULL;
    pthread_mutex_lock(&port->lock);
    message = port->head;
    if (message) {
        port->head = message->next;
        if (!port->head)
            port->tail = NULL;
        message->next = NULL;
    }
    pthread_mutex_unlock(&port->lock);
    return message;
}

struct amiga_exec_message *amiga_exec_wait_port(struct amiga_exec_msg_port *port)
{
    struct amiga_exec_message *message;

    if (!port)
        return NULL;
    pthread_mutex_lock(&port->lock);
    port->waiters++;
    while (!port->head && !port->closing)
        pthread_cond_wait(&port->condition, &port->lock);
    message = port->head;
    if (message) {
        port->head = message->next;
        if (!port->head)
            port->tail = NULL;
        message->next = NULL;
    }
    port->waiters--;
    if (port->closing && port->waiters == 0)
        pthread_cond_signal(&port->condition);
    pthread_mutex_unlock(&port->lock);
    return message;
}

uint32_t amiga_exec_wait_port_signal(struct amiga_exec_msg_port *port)
{
    uint32_t signal = 0;

    if (!port)
        return 0;
    pthread_mutex_lock(&port->lock);
    while (!port->head && !port->closing)
        pthread_cond_wait(&port->condition, &port->lock);
    if (port->head && port->owner && port->signal_number >= 0)
        signal = 1u << port->signal_number;
    pthread_mutex_unlock(&port->lock);
    return signal;
}

void *amiga_exec_alloc_mem(size_t size, uint32_t flags)
{
    void *memory = malloc(size ? size : 1);

    if (memory && (flags & AMIGA_EXEC_MEMF_CLEAR))
        memset(memory, 0, size);
    return memory;
}

void amiga_exec_free_mem(void *memory, size_t size)
{
    (void)size;
    free(memory);
}

void *amiga_exec_alloc_vec(size_t size, uint32_t flags)
{
    return amiga_exec_alloc_mem(size, flags);
}

void amiga_exec_free_vec(void *memory)
{
    amiga_exec_free_mem(memory, 0);
}

int amiga_exec_register_library(const char *name, unsigned long version,
                                void *base)
{
    struct amiga_exec_library *library;

    if (!name || !*name || !base)
        return EINVAL;
    library = calloc(1, sizeof(*library));
    if (!library)
        return ENOMEM;
    copy_name(library->name, name);
    library->version = version;
    library->base = base;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_library *cursor = library_list; cursor;
         cursor = cursor->next) {
        if (strcasecmp(cursor->name, name) == 0) {
            pthread_mutex_unlock(&registry_lock);
            free(library);
            return EEXIST;
        }
    }
    library->next = library_list;
    library_list = library;
    pthread_mutex_unlock(&registry_lock);
    return 0;
}

void *amiga_exec_open_library(const char *name, unsigned long minimum_version)
{
    void *base = NULL;

    if (!name)
        return NULL;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_library *cursor = library_list; cursor;
         cursor = cursor->next) {
        if (strcasecmp(cursor->name, name) == 0 &&
            cursor->version >= minimum_version) {
            cursor->opens++;
            base = cursor->base;
            break;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return base;
}

int amiga_exec_close_library(const char *name, void *base)
{
    if (!name || !base)
        return EINVAL;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_library *cursor = library_list; cursor;
         cursor = cursor->next) {
        if (strcasecmp(cursor->name, name) == 0 && cursor->base == base) {
            if (cursor->opens > 0)
                cursor->opens--;
            pthread_mutex_unlock(&registry_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return ENOENT;
}

int amiga_exec_close_library_base(void *base)
{
    if (!base)
        return EINVAL;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_library *cursor = library_list; cursor;
         cursor = cursor->next) {
        if (cursor->base == base) {
            if (cursor->opens > 0)
                cursor->opens--;
            pthread_mutex_unlock(&registry_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return ENOENT;
}

int amiga_exec_unregister_library(const char *name, void *base)
{
    struct amiga_exec_library **cursor;

    if (!name || !base)
        return EINVAL;
    pthread_mutex_lock(&registry_lock);
    cursor = &library_list;
    while (*cursor && !(strcasecmp((*cursor)->name, name) == 0 &&
                         (*cursor)->base == base))
        cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&registry_lock);
        return ENOENT;
    }
    if ((*cursor)->opens != 0) {
        pthread_mutex_unlock(&registry_lock);
        return EBUSY;
    }
    {
        struct amiga_exec_library *removed = *cursor;
        *cursor = removed->next;
        free(removed);
    }
    pthread_mutex_unlock(&registry_lock);
    return 0;
}

int amiga_exec_register_device(const char *name, void *context,
                               amiga_exec_device_open open,
                               amiga_exec_device_close close)
{
    struct amiga_exec_device *device;

    if (!name || !*name || !open)
        return EINVAL;
    device = calloc(1, sizeof(*device));
    if (!device)
        return ENOMEM;
    copy_name(device->name, name);
    device->context = context;
    device->open = open;
    device->close = close;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_device *cursor = device_list; cursor;
         cursor = cursor->next) {
        if (strcasecmp(cursor->name, name) == 0) {
            pthread_mutex_unlock(&registry_lock);
            free(device);
            return EEXIST;
        }
    }
    device->next = device_list;
    device_list = device;
    pthread_mutex_unlock(&registry_lock);
    return 0;
}

int amiga_exec_open_device(const char *name, unsigned long unit,
                           unsigned long flags, void **handle_out)
{
    int result = ENOENT;

    if (!name || !handle_out)
        return EINVAL;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_device *cursor = device_list; cursor;
         cursor = cursor->next) {
        if (strcasecmp(cursor->name, name) == 0) {
            result = cursor->open(cursor->context, unit, flags, handle_out);
            break;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return result;
}

int amiga_exec_close_device(const char *name, void *handle)
{
    if (!name || !handle)
        return EINVAL;
    pthread_mutex_lock(&registry_lock);
    for (struct amiga_exec_device *cursor = device_list; cursor;
         cursor = cursor->next) {
        if (strcasecmp(cursor->name, name) == 0) {
            if (cursor->close)
                cursor->close(cursor->context, handle);
            pthread_mutex_unlock(&registry_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&registry_lock);
    return ENOENT;
}

int amiga_exec_unregister_device(const char *name)
{
    struct amiga_exec_device **cursor;

    if (!name)
        return EINVAL;
    pthread_mutex_lock(&registry_lock);
    cursor = &device_list;
    while (*cursor && strcasecmp((*cursor)->name, name) != 0)
        cursor = &(*cursor)->next;
    if (!*cursor) {
        pthread_mutex_unlock(&registry_lock);
        return ENOENT;
    }
    {
        struct amiga_exec_device *removed = *cursor;
        *cursor = removed->next;
        free(removed);
    }
    pthread_mutex_unlock(&registry_lock);
    return 0;
}
