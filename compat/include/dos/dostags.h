#ifndef AMIGA_SHELL_DOS_DOSTAGS_H
#define AMIGA_SHELL_DOS_DOSTAGS_H

#include <utility/tagitem.h>

/* CreateNewProc() tags.  Values are AROS's own, from
   compiler/include/dos/dostags.h -- they are ABI, not a local convention, so
   they are reproduced verbatim rather than renumbered.  ACE previously
   carried only NP_Dummy and NP_StackSize, which is every tag the shell
   itself had needed; the rest are here because AROS sources that create
   processes use them -- Regina's os_amiga.c among them.

   Declaring a tag is not implementing it.  CreateNewProcTags() decides which
   of these it honours; a tag listed here that the implementation ignores is
   a silent wrong answer rather than a compile error, so the two must be read
   together. */
#define NP_Dummy        (TAG_USER + 1000)
/* Exactly one of NP_Seglist or NP_Entry must be given. */
#define NP_Seglist      (NP_Dummy + 1)
#define NP_FreeSeglist  (NP_Dummy + 2)
#define NP_Entry        (NP_Dummy + 3)
/* The three standard streams, each with its own close-on-exit ownership
   flag.  The Close* default is TRUE: the new process owns what it is given
   unless the caller says otherwise. */
#define NP_Input        (NP_Dummy + 4)
#define NP_Output       (NP_Dummy + 5)
#define NP_CloseInput   (NP_Dummy + 6)
#define NP_CloseOutput  (NP_Dummy + 7)
#define NP_Error        (NP_Dummy + 8)
#define NP_CloseError   (NP_Dummy + 9)
#define NP_CurrentDir   (NP_Dummy + 10)
#define NP_StackSize    (NP_Dummy + 11)
#define NP_Name         (NP_Dummy + 12)
#define NP_Priority     (NP_Dummy + 13)
#define NP_ConsoleTask  (NP_Dummy + 14)
#define NP_WindowPtr    (NP_Dummy + 15)
#define NP_HomeDir      (NP_Dummy + 16)
#define NP_CopyVars     (NP_Dummy + 17)
#define NP_Cli          (NP_Dummy + 18)
/* CLI processes only. */
#define NP_Path         (NP_Dummy + 19)
#define NP_CommandName  (NP_Dummy + 20)
#define NP_Arguments    (NP_Dummy + 21)
#define NP_NotifyOnDeath (NP_Dummy + 22)
#define NP_Synchronous  (NP_Dummy + 23)
#define NP_ExitCode     (NP_Dummy + 24)
#define NP_ExitData     (NP_Dummy + 25)
/* AROS-specific. */
#define NP_UserData     (NP_Dummy + 26)

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
