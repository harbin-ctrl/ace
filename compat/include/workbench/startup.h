#ifndef AMIGA_SHELL_WORKBENCH_STARTUP_H
#define AMIGA_SHELL_WORKBENCH_STARTUP_H

/* Real, from workbench/startup.h: the message a program launched by
   double-clicking a Workbench icon receives instead of a CLI argument line.
   ACE has no Workbench and no icon launch path -- every ACE program starts
   from a CLI -- so nothing here is ever populated or read; it exists only
   because a source file written against real AmigaOS headers may include it
   unconditionally, as Vim's os_amiga.c does. */

#include <exec/ports.h>
#include <exec/types.h>
#include <dos/dos.h>

struct WBStartup {
    struct Message sm_Message;
    struct MsgPort *sm_Process;
    BPTR sm_Segment;
    LONG sm_NumArgs;
    char *sm_ToolWindow;
    struct WBArg *sm_ArgList;
};

struct WBArg {
    BPTR wa_Lock;
    BYTE *wa_Name;
};

#endif
