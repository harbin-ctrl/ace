#ifndef AMIGA_SHELL_DOS_DOSTAGS_H
#define AMIGA_SHELL_DOS_DOSTAGS_H

#include <utility/tagitem.h>

#define NP_Dummy        (TAG_USER + 1000)
#define NP_StackSize    (NP_Dummy + 11)

#define SYS_Dummy       (TAG_USER + 32)
#define SYS_Input       (SYS_Dummy + 1)
#define SYS_Output      (SYS_Dummy + 2)
#define SYS_Asynch      (SYS_Dummy + 3)
#define SYS_UserShell   (SYS_Dummy + 4)
#define SYS_Error       (SYS_Dummy + 6)
#define SYS_ScriptInput (SYS_Dummy + 11)
#define SYS_Background  (SYS_Dummy + 12)

#define SYS_DupStream   1

#endif
