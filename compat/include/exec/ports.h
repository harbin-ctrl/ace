#ifndef AMIGA_SHELL_EXEC_PORTS_H
#define AMIGA_SHELL_EXEC_PORTS_H

#include <exec/lists.h>

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#define MsgPort amiga_exec_msg_port
#define Message amiga_exec_message
#else
/* Real, from exec/ports.h. ClipboardHandle embeds message ports, so the
   public structure must be complete even though most ACE callers only keep
   a pointer to it. */
struct MsgPort {
    struct Node mp_Node;
    UBYTE mp_Flags;
    UBYTE mp_SigBit;
    void *mp_SigTask;
    struct List mp_MsgList;
};

/* Real, from exec/ports.h. Message is embedded by value in real AmigaOS
   structures -- workbench/startup.h's WBStartup does this, and Vim's
   os_amiga.c declares one that way -- so an opaque forward declaration is not
   enough for a source file that includes such a header unmodified. */
struct Message {
    struct Node mn_Node;
    struct MsgPort *mn_ReplyPort;
    UWORD mn_Length;
};
#endif

#endif
