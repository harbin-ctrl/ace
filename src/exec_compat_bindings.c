#include "exec_compat.h"

#include <exec/compat.h>

APTR amiga_exec_compat_find_task(CONST_STRPTR name)
{
    (void)name;
    return amiga_exec_current_task();
}

struct Library *amiga_exec_compat_open_library(CONST_STRPTR name,
                                                ULONG version)
{
    return amiga_exec_open_library(name, version);
}

void amiga_exec_compat_close_library(struct Library *library)
{
    if (library)
        (void)amiga_exec_close_library_base(library);
}

APTR amiga_exec_compat_alloc_mem(ULONG size, ULONG flags)
{
    return amiga_exec_alloc_mem(size, flags);
}

void amiga_exec_compat_free_mem(APTR memory, ULONG size)
{
    amiga_exec_free_mem(memory, size);
}

APTR amiga_exec_compat_alloc_vec(ULONG size, ULONG flags)
{
    return amiga_exec_alloc_vec(size, flags);
}

void amiga_exec_compat_free_vec(APTR memory)
{
    amiga_exec_free_vec(memory);
}

LONG amiga_exec_compat_alloc_signal(LONG signal_number)
{
    return amiga_exec_alloc_signal(amiga_exec_current_task(), signal_number);
}

void amiga_exec_compat_free_signal(LONG signal_number)
{
    (void)amiga_exec_free_signal(amiga_exec_current_task(), signal_number);
}

ULONG amiga_exec_compat_set_signal(ULONG set_mask, ULONG clear_mask)
{
    return amiga_exec_set_signal(amiga_exec_current_task(), set_mask,
                                 clear_mask);
}

ULONG amiga_exec_compat_check_signal(ULONG mask)
{
    return amiga_exec_check_signal(amiga_exec_current_task(), mask);
}

ULONG amiga_exec_compat_wait(ULONG mask)
{
    return amiga_exec_wait(amiga_exec_current_task(), mask);
}

void amiga_exec_compat_signal(APTR task, ULONG mask)
{
    (void)amiga_exec_signal(task, mask);
}

void amiga_exec_compat_forbid(void)
{
}

void amiga_exec_compat_permit(void)
{
}

struct amiga_exec_msg_port *amiga_exec_compat_create_msg_port(void)
{
    struct amiga_exec_msg_port *port = NULL;

    if (amiga_exec_create_msg_port(amiga_exec_current_task(), -1, NULL,
                                   &port) != 0)
        return NULL;
    return port;
}

void amiga_exec_compat_delete_msg_port(struct amiga_exec_msg_port *port)
{
    amiga_exec_delete_msg_port(port);
}

void amiga_exec_compat_put_msg(struct amiga_exec_msg_port *port,
                               struct amiga_exec_message *message)
{
    (void)amiga_exec_put_msg(port, message);
}

struct amiga_exec_message *amiga_exec_compat_get_msg(
    struct amiga_exec_msg_port *port)
{
    return amiga_exec_get_msg(port);
}

ULONG amiga_exec_compat_wait_port(struct amiga_exec_msg_port *port)
{
    return amiga_exec_wait_port_signal(port);
}
