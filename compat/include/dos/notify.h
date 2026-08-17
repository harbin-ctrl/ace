#ifndef AMIGA_SHELL_DOS_NOTIFY_H
#define AMIGA_SHELL_DOS_NOTIFY_H

#include <exec/types.h>

struct MsgPort;
struct Task;

struct NotifyRequest {
    STRPTR nr_Name;
    STRPTR nr_FullName;
    IPTR nr_UserData;
    ULONG nr_Flags;
    union {
        struct { struct MsgPort *nr_Port; } nr_Msg;
        struct { struct Task *nr_Task; UBYTE nr_SignalNum; UBYTE nr_pad[3]; } nr_Signal;
    } nr_stuff;
    IPTR nr_Reserved[4];
    ULONG nr_MsgCount;
    struct MsgPort *nr_Handler;
};

#define NRB_SEND_MESSAGE   0
#define NRB_SEND_SIGNAL    1
#define NRB_WAIT_REPLY     3
#define NRB_NOTIFY_INITIAL 4

#define NRF_SEND_MESSAGE   (1L << NRB_SEND_MESSAGE)
#define NRF_SEND_SIGNAL    (1L << NRB_SEND_SIGNAL)
#define NRF_WAIT_REPLY     (1L << NRB_WAIT_REPLY)
#define NRF_NOTIFY_INITIAL (1L << NRB_NOTIFY_INITIAL)

#endif
