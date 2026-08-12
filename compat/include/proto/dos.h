#ifndef AMIGA_SHELL_PROTO_DOS_H
#define AMIGA_SHELL_PROTO_DOS_H

#include <dos/dos.h>

struct DosLibrary;
extern struct DosLibrary *DOSBase;

struct CommandLineInterface;
APTR FindTask(CONST_STRPTR name);

struct CommandLineInterface *Cli(void);
BOOL SetPrompt(CONST_STRPTR prompt);
LONG VPrintf(CONST_STRPTR format, APTR arguments);

#endif
