#ifndef AMIGA_SHELL_EXEC_PORTS_H
#define AMIGA_SHELL_EXEC_PORTS_H

#include <exec/nodes.h>

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#define MsgPort amiga_exec_msg_port
#define Message amiga_exec_message
#else
struct MsgPort;

/* Real, from exec/ports.h. Unlike MsgPort, which every ACE caller only ever
   holds by pointer, Message is embedded by value in real AmigaOS structures
   -- workbench/startup.h's WBStartup does this, and Vim's os_amiga.c
   declares one that way -- so an opaque forward declaration is not enough
   for a source file that includes such a header unmodified. */
struct Message {
    struct Node mn_Node;
    struct MsgPort *mn_ReplyPort;
    UWORD mn_Length;
};
#endif

#endif
