#ifndef AMIGA_SHELL_EXEC_COMPAT_BINDINGS_H
#define AMIGA_SHELL_EXEC_COMPAT_BINDINGS_H

#include <exec/types.h>

struct Library;
struct amiga_exec_msg_port;
struct amiga_exec_message;

#define AMIGA_EXEC_MEMF_CLEAR (1u << 0)

APTR amiga_exec_compat_find_task(CONST_STRPTR name);
struct Library *amiga_exec_compat_open_library(CONST_STRPTR name,
                                                ULONG version);
void amiga_exec_compat_close_library(struct Library *library);

APTR amiga_exec_compat_alloc_mem(ULONG size, ULONG flags);
void amiga_exec_compat_free_mem(APTR memory, ULONG size);
APTR amiga_exec_compat_alloc_vec(ULONG size, ULONG flags);
void amiga_exec_compat_free_vec(APTR memory);

LONG amiga_exec_compat_alloc_signal(LONG signal_number);
void amiga_exec_compat_free_signal(LONG signal_number);
ULONG amiga_exec_compat_set_signal(ULONG set_mask, ULONG clear_mask);
ULONG amiga_exec_compat_check_signal(ULONG mask);
ULONG amiga_exec_compat_wait(ULONG mask);
void amiga_exec_compat_signal(APTR task, ULONG mask);
void amiga_exec_compat_forbid(void);
void amiga_exec_compat_permit(void);

struct amiga_exec_msg_port *amiga_exec_compat_create_msg_port(void);
void amiga_exec_compat_delete_msg_port(struct amiga_exec_msg_port *port);
void amiga_exec_compat_put_msg(struct amiga_exec_msg_port *port,
                               struct amiga_exec_message *message);
struct amiga_exec_message *amiga_exec_compat_get_msg(
    struct amiga_exec_msg_port *port);
ULONG amiga_exec_compat_wait_port(struct amiga_exec_msg_port *port);

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#define FindTask amiga_exec_compat_find_task
#define OpenLibrary amiga_exec_compat_open_library
#define CloseLibrary amiga_exec_compat_close_library
#define AllocSignal amiga_exec_compat_alloc_signal
#define FreeSignal amiga_exec_compat_free_signal
#define SetSignal amiga_exec_compat_set_signal
#define CheckSignal amiga_exec_compat_check_signal
#define Wait amiga_exec_compat_wait
#define Signal amiga_exec_compat_signal
#define Forbid amiga_exec_compat_forbid
#define Permit amiga_exec_compat_permit
#define CreateMsgPort amiga_exec_compat_create_msg_port
#define DeleteMsgPort amiga_exec_compat_delete_msg_port
#define PutMsg amiga_exec_compat_put_msg
#define GetMsg amiga_exec_compat_get_msg
#define WaitPort amiga_exec_compat_wait_port
#endif

#endif
