#ifndef AMIGA_SHELL_EXEC_COMPAT_H
#define AMIGA_SHELL_EXEC_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#define AMIGA_EXEC_SIGNAL_COUNT 32
#define AMIGA_EXEC_SIGBREAKF_CTRL_C (1u << 12)
#define AMIGA_EXEC_SIGBREAKF_CTRL_D (1u << 13)
#define AMIGA_EXEC_SIGBREAKF_CTRL_E (1u << 14)
#define AMIGA_EXEC_SIGBREAKF_CTRL_F (1u << 15)

#define AMIGA_EXEC_MEMF_CLEAR (1u << 0)

struct amiga_exec_task;
struct amiga_exec_msg_port;
struct amiga_exec_library;
struct amiga_exec_device;

typedef void *(*amiga_exec_task_entry)(void *context);

int amiga_exec_register_current_task(const char *name,
                                     struct amiga_exec_task **task_out);
void amiga_exec_unregister_current_task(struct amiga_exec_task *task);
struct amiga_exec_task *amiga_exec_current_task(void);
const char *amiga_exec_task_name(const struct amiga_exec_task *task);

int amiga_exec_create_task(const char *name, amiga_exec_task_entry entry,
                           void *context, struct amiga_exec_task **task_out);
int amiga_exec_join_task(struct amiga_exec_task *task, void **result_out);
void amiga_exec_delete_task(struct amiga_exec_task *task);

int amiga_exec_alloc_signal(struct amiga_exec_task *task, int signal_number);
int amiga_exec_free_signal(struct amiga_exec_task *task, int signal_number);
uint32_t amiga_exec_set_signal(struct amiga_exec_task *task,
                               uint32_t set_mask, uint32_t clear_mask);
uint32_t amiga_exec_check_signal(struct amiga_exec_task *task,
                                 uint32_t mask);
uint32_t amiga_exec_wait(struct amiga_exec_task *task, uint32_t mask);
int amiga_exec_signal(struct amiga_exec_task *task, uint32_t mask);
uint32_t amiga_exec_wait_port_signal(struct amiga_exec_msg_port *port);

struct amiga_exec_message {
    struct amiga_exec_message *next;
    void *payload;
};

int amiga_exec_create_msg_port(struct amiga_exec_task *owner,
                               int signal_number, const char *name,
                               struct amiga_exec_msg_port **port_out);
void amiga_exec_delete_msg_port(struct amiga_exec_msg_port *port);
int amiga_exec_put_msg(struct amiga_exec_msg_port *port,
                       struct amiga_exec_message *message);
struct amiga_exec_message *amiga_exec_get_msg(struct amiga_exec_msg_port *port);
struct amiga_exec_message *amiga_exec_wait_port(struct amiga_exec_msg_port *port);

void *amiga_exec_alloc_mem(size_t size, uint32_t flags);
void amiga_exec_free_mem(void *memory, size_t size);
void *amiga_exec_alloc_vec(size_t size, uint32_t flags);
void amiga_exec_free_vec(void *memory);

int amiga_exec_register_library(const char *name, unsigned long version,
                                void *base);
void *amiga_exec_open_library(const char *name, unsigned long minimum_version);
int amiga_exec_close_library(const char *name, void *base);
int amiga_exec_close_library_base(void *base);
int amiga_exec_unregister_library(const char *name, void *base);

typedef int (*amiga_exec_device_open)(void *context, unsigned long unit,
                                      unsigned long flags, void **handle_out);
typedef void (*amiga_exec_device_close)(void *context, void *handle);

int amiga_exec_register_device(const char *name, void *context,
                               amiga_exec_device_open open,
                               amiga_exec_device_close close);
int amiga_exec_open_device(const char *name, unsigned long unit,
                           unsigned long flags, void **handle_out);
int amiga_exec_close_device(const char *name, void *handle);
int amiga_exec_unregister_device(const char *name);

#endif
