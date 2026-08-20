#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <intuition/intuition.h>

#include "ace_requestor_protocol.h"
#include "assign_compat.h"
#include "broker_client.h"
#include "broker_protocol.h"

struct requestor_reply_waiter {
    uint64_t message_id;
    int status;
    int result;
    struct requestor_reply_waiter *next;
};

static pthread_mutex_t reply_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reply_condition = PTHREAD_COND_INITIALIZER;
static struct requestor_reply_waiter *reply_waiters;
static pthread_once_t requestor_handler_once = PTHREAD_ONCE_INIT;
static int requestor_handler_status;

static void requestor_reply_handler(uint32_t operation, uint64_t message_id,
                                    uint64_t port_id, const char *payload,
                                    size_t payload_length, int stdin_fd,
                                    int stdout_fd, void *context)
{
    struct ace_requestor_reply reply;
    struct requestor_reply_waiter *waiter;

    (void)port_id;
    (void)context;
    if (stdin_fd >= 0)
        close(stdin_fd);
    if (stdout_fd >= 0)
        close(stdout_fd);
    if (operation == AMIGA_BROKER_PORT_ABANDONED) {
        reply.status = ESRCH;
        reply.result = 0;
    } else if (operation == AMIGA_BROKER_PORT_REPLY &&
               payload_length == sizeof(reply)) {
        memcpy(&reply, payload, sizeof(reply));
        if (reply.magic != ACE_REQUESTOR_REPLY_MAGIC)
            return;
    } else {
        return;
    }
    waiter = calloc(1, sizeof(*waiter));
    if (!waiter)
        return;
    waiter->message_id = message_id;
    waiter->status = reply.status;
    waiter->result = reply.result;
    pthread_mutex_lock(&reply_lock);
    waiter->next = reply_waiters;
    reply_waiters = waiter;
    pthread_cond_broadcast(&reply_condition);
    pthread_mutex_unlock(&reply_lock);
}

static void install_requestor_handler(void)
{
    requestor_handler_status =
        native_broker_port_add_handler(requestor_reply_handler, NULL);
}

static void count_character(UBYTE character, APTR data)
{
    size_t *length = data;

    (void)character;
    (*length)++;
}

struct format_buffer {
    char *data;
    size_t offset;
    size_t capacity;
};

static void append_character(UBYTE character, APTR data)
{
    struct format_buffer *buffer = data;

    if (buffer->offset < buffer->capacity)
        buffer->data[buffer->offset++] = (char)character;
}

/* RawDoFmt consumes the va_list it is given. Formatting the text first and
   the gadget labels second is therefore exactly the EasyRequest varargs
   contract: the gadget format sees the arguments left by the text format. */
static int format_requestor_string(const char *format, va_list *arguments,
                                   char **result)
{
    va_list count_arguments;
    size_t length = 0;
    struct format_buffer buffer;

    *result = NULL;
    if (!format)
        format = "";
    va_copy(count_arguments, *arguments);
    RawDoFmt(format, count_arguments, (void (*)(void))count_character, &length);
    va_end(count_arguments);
    buffer.data = calloc(length + 1, 1);
    if (!buffer.data)
        return -1;
    buffer.offset = 0;
    buffer.capacity = length;
    RawDoFmt(format, *arguments, (void (*)(void))append_character, &buffer);
    buffer.data[buffer.offset] = '\0';
    *result = buffer.data;
    return 0;
}

static int requestor_send(const char *title, const char *text,
                          const char *gadgets, int *delivered)
{
    struct ace_requestor_wire wire;
    struct requestor_reply_waiter *waiter;
    char port_name[NAME_MAX];
    char *message;
    size_t title_length = strlen(title) + 1;
    size_t text_length = strlen(text) + 1;
    size_t gadgets_length = strlen(gadgets) + 1;
    size_t message_length;
    uint64_t message_id;

    *delivered = 0;
    if (title_length > UINT32_MAX || text_length > UINT32_MAX ||
        gadgets_length > UINT32_MAX ||
        title_length > SIZE_MAX - text_length ||
        title_length + text_length > SIZE_MAX - gadgets_length ||
        sizeof(wire) > SIZE_MAX - title_length - text_length - gadgets_length)
        return 0;
    message_length = sizeof(wire) + title_length + text_length + gadgets_length;
    if (message_length > AMIGA_BROKER_MAX_PAYLOAD)
        return 0;
    message = malloc(message_length);
    if (!message)
        return 0;
    wire.magic = ACE_REQUESTOR_MAGIC;
    wire.title_length = (uint32_t)title_length;
    wire.text_length = (uint32_t)text_length;
    wire.gadgets_length = (uint32_t)gadgets_length;
    memcpy(message, &wire, sizeof(wire));
    memcpy(message + sizeof(wire), title, title_length);
    memcpy(message + sizeof(wire) + title_length, text, text_length);
    memcpy(message + sizeof(wire) + title_length + text_length, gadgets,
           gadgets_length);
    if (ace_requestor_port_name(getenv("ACE_SESSION"), port_name,
                                 sizeof(port_name)) != 0 ||
        native_broker_port_put(port_name, message, message_length, -1, -1,
                                &message_id) != 0) {
        free(message);
        return 0;
    }
    free(message);

    *delivered = 1;
    pthread_mutex_lock(&reply_lock);
    for (;;) {
        struct requestor_reply_waiter **previous = &reply_waiters;

        while (*previous && (*previous)->message_id != message_id)
            previous = &(*previous)->next;
        if (*previous) {
            int result = (*previous)->status == 0 ? (*previous)->result : 0;

            waiter = *previous;
            *previous = waiter->next;
            free(waiter);
            pthread_mutex_unlock(&reply_lock);
            return result;
        }
        pthread_cond_wait(&reply_condition, &reply_lock);
    }
}

LONG EasyRequest(struct Window *window, struct EasyStruct *easy_struct,
                 ULONG *idcmp, ...)
{
    va_list arguments;
    char *text = NULL;
    char *gadgets = NULL;
    int delivered = 0;
    LONG result;

    (void)window;
    (void)idcmp;
    if (!easy_struct)
        return 0;
    va_start(arguments, idcmp);
    if (format_requestor_string(easy_struct->es_TextFormat, &arguments,
                                &text) != 0 ||
        format_requestor_string(easy_struct->es_GadgetFormat, &arguments,
                                &gadgets) != 0) {
        va_end(arguments);
        free(text);
        free(gadgets);
        return 0;
    }
    (void)pthread_once(&requestor_handler_once, install_requestor_handler);
    if (requestor_handler_status != 0 || native_broker_ensure() != 0)
        result = 0;
    else
        result = requestor_send(easy_struct->es_Title
                                    ? easy_struct->es_Title
                                    : "System Request",
                                text, gadgets, &delivered);
    if (!delivered)
        fprintf(stderr, "EasyRequest: %s\n%s\n[%s]\n",
                easy_struct->es_Title ? easy_struct->es_Title
                                       : "System Request",
                text, gadgets);
    free(text);
    free(gadgets);
    va_end(arguments);
    return result;
}
