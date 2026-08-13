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

APTR CreateIORequest(struct MsgPort *reply_port, ULONG size);
void DeleteIORequest(struct IORequest *request);
LONG OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *request,
                ULONG flags);
void CloseDevice(struct IORequest *request);
void SendIO(struct IORequest *request);
LONG DoIO(struct IORequest *request);
LONG WaitIO(struct IORequest *request);
void AbortIO(struct IORequest *request);

void *ace_aros_console_last(void);
int ace_aros_console_feed(void *console, const void *data, size_t length);
size_t ace_aros_console_take_output(void *console, void *data, size_t length);

#endif
