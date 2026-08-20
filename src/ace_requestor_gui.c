#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "ace_requestor_gui.h"
#include "ace_requestor_protocol.h"
#include "broker_client.h"
#include "broker_protocol.h"

struct gui_request {
    char *title;
    char *text;
    char *gadgets;
    pthread_mutex_t lock;
    pthread_cond_t condition;
    int result;
    int done;
};

static uint64_t requestor_port_id;
static GtkWidget *requestor_parent;

static void free_gui_request(struct gui_request *request)
{
    pthread_cond_destroy(&request->condition);
    pthread_mutex_destroy(&request->lock);
    free(request->title);
    free(request->text);
    free(request->gadgets);
    free(request);
}

static int copy_wire_string(const char *payload, size_t payload_length,
                            size_t *offset, uint32_t string_length,
                            char **result)
{
    if (string_length == 0 || string_length > payload_length - *offset)
        return -1;
    if (payload[*offset + string_length - 1] != '\0')
        return -1;
    *result = malloc(string_length);
    if (!*result)
        return -1;
    memcpy(*result, payload + *offset, string_length);
    *offset += string_length;
    return 0;
}

static struct gui_request *decode_request(const char *payload,
                                          size_t payload_length)
{
    struct ace_requestor_wire wire;
    struct gui_request *request;
    size_t offset = sizeof(wire);

    if (payload_length < sizeof(wire))
        return NULL;
    memcpy(&wire, payload, sizeof(wire));
    if (wire.magic != ACE_REQUESTOR_MAGIC ||
        wire.title_length > payload_length - offset)
        return NULL;
    offset += wire.title_length;
    if (wire.text_length > payload_length - offset)
        return NULL;
    offset += wire.text_length;
    if (wire.gadgets_length > payload_length - offset ||
        offset + wire.gadgets_length != payload_length)
        return NULL;
    request = calloc(1, sizeof(*request));
    if (!request)
        return NULL;
    offset = sizeof(wire);
    if (copy_wire_string(payload, payload_length, &offset, wire.title_length,
                         &request->title) != 0 ||
        copy_wire_string(payload, payload_length, &offset, wire.text_length,
                         &request->text) != 0 ||
        copy_wire_string(payload, payload_length, &offset,
                         wire.gadgets_length, &request->gadgets) != 0) {
        free(request->title);
        free(request->text);
        free(request->gadgets);
        free(request);
        return NULL;
    }
    pthread_mutex_init(&request->lock, NULL);
    pthread_cond_init(&request->condition, NULL);
    return request;
}

static gboolean show_requestor(gpointer data)
{
    struct gui_request *request = data;
    GtkWidget *dialog;
    GtkWidget *label;
    GtkWidget *content;
    gchar **gadgets;
    int button_count = 0;
    int response;
    int result = 0;

    dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(dialog), request->title);
    if (requestor_parent) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                     GTK_WINDOW(requestor_parent));
        gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    }
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    label = gtk_label_new(request->text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(label, 18);
    gtk_widget_set_margin_end(label, 18);
    gtk_widget_set_margin_top(label, 18);
    gtk_widget_set_margin_bottom(label, 12);
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 0);

    gadgets = g_strsplit(request->gadgets, "|", -1);
    for (button_count = 0; gadgets[button_count]; button_count++)
        gtk_dialog_add_button(GTK_DIALOG(dialog),
                              gadgets[button_count][0]
                                  ? gadgets[button_count]
                                  : "OK",
                              button_count);
    if (button_count == 0) {
        gtk_dialog_add_button(GTK_DIALOG(dialog), "OK", 0);
        button_count = 1;
    }
    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response >= 0 && response < button_count)
        result = button_count - 1 - response;
    gtk_widget_destroy(dialog);
    g_strfreev(gadgets);

    pthread_mutex_lock(&request->lock);
    request->result = result;
    request->done = 1;
    pthread_cond_signal(&request->condition);
    pthread_mutex_unlock(&request->lock);
    return G_SOURCE_REMOVE;
}

static void requestor_gui_handler(uint32_t operation, uint64_t message_id,
                                  uint64_t port_id, const char *payload,
                                  size_t payload_length, int stdin_fd,
                                  int stdout_fd, void *context)
{
    struct gui_request *request;
    struct ace_requestor_reply reply;

    (void)context;
    if (stdin_fd >= 0)
        close(stdin_fd);
    if (stdout_fd >= 0)
        close(stdout_fd);
    if (operation != AMIGA_BROKER_PORT_PUT || port_id != requestor_port_id)
        return;
    request = decode_request(payload, payload_length);
    if (!request) {
        reply.magic = ACE_REQUESTOR_REPLY_MAGIC;
        reply.status = EPROTO;
        reply.result = 0;
        (void)native_broker_port_reply(message_id, &reply, sizeof(reply));
        return;
    }
    g_idle_add(show_requestor, request);
    pthread_mutex_lock(&request->lock);
    while (!request->done)
        pthread_cond_wait(&request->condition, &request->lock);
    reply.magic = ACE_REQUESTOR_REPLY_MAGIC;
    reply.status = 0;
    reply.result = request->result;
    pthread_mutex_unlock(&request->lock);
    (void)native_broker_port_reply(message_id, &reply, sizeof(reply));
    free_gui_request(request);
}

int ace_requestor_gui_start(const char *session, GtkWidget *parent)
{
    char port_name[NAME_MAX];

    requestor_parent = parent;
    if (ace_requestor_port_name(session, port_name, sizeof(port_name)) != 0 ||
        native_broker_port_attach(requestor_gui_handler, NULL, NULL) != 0 ||
        native_broker_port_add(port_name, &requestor_port_id) != 0) {
        fprintf(stderr, "ace-console: EasyRequest GUI port unavailable: %s\n",
                strerror(errno));
        requestor_parent = NULL;
        requestor_port_id = 0;
        return -1;
    }
    return 0;
}

void ace_requestor_gui_stop(void)
{
    if (requestor_port_id != 0) {
        (void)native_broker_port_remove(requestor_port_id);
        requestor_port_id = 0;
    }
    requestor_parent = NULL;
}
