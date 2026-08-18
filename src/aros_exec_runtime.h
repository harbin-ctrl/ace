#ifndef ACE_AROS_EXEC_RUNTIME_H
#define ACE_AROS_EXEC_RUNTIME_H

#include <stddef.h>

#include <exec/io.h>
#include <exec/ports.h>

/* Host-backed Exec/console entry points used by the real AROS handler. */
struct MsgPort *CreateMsgPort(void);
void DeleteMsgPort(struct MsgPort *port);
void PutMsg(struct MsgPort *port, struct Message *message);
struct Message *GetMsg(struct MsgPort *port);
void WaitPort(struct MsgPort *port);
ULONG Wait(ULONG signals);
struct Task;
int ace_aros_runtime_register_task(struct Task *task);
void ace_aros_runtime_unregister_task(struct Task *task);
struct Task *ace_aros_runtime_find_task(CONST_STRPTR name);
void ace_aros_runtime_set_current_task(struct Task *task);
void ace_aros_runtime_signal(ULONG signals);
void ace_aros_runtime_signal_task(struct Task *task, ULONG signals);
void ace_aros_runtime_signal_local_tasks(ULONG signals);
ULONG ace_aros_runtime_set_signal(ULONG set_mask, ULONG clear_mask);
ULONG ace_aros_runtime_check_signal(ULONG mask);
LONG ace_aros_runtime_alloc_signal(LONG signal_number);
void ace_aros_runtime_free_signal(LONG signal_number);
/* Safe to call from a host signal handler.  The bits are merged into the
   normal Exec signal state on the next runtime operation. */
void ace_aros_runtime_raise_from_host(ULONG signals);

APTR CreateIORequest(struct MsgPort *reply_port, ULONG size);
void DeleteIORequest(struct IORequest *request);
LONG OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *request,
                ULONG flags);
void CloseDevice(struct IORequest *request);
void SendIO(struct IORequest *request);
LONG DoIO(struct IORequest *request);
LONG WaitIO(struct IORequest *request);
struct IORequest *CheckIO(struct IORequest *request);
void AbortIO(struct IORequest *request);

void *ace_aros_console_last(void);
int ace_aros_console_feed(void *console, const void *data, size_t length);
size_t ace_aros_console_take_output(void *console, void *data, size_t length);

#endif
