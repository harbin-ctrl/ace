#ifndef AMIGA_SHELL_DOSEXTENS_H
#define AMIGA_SHELL_DOSEXTENS_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <dos/cliinit.h>
#include <string.h>

#define AROS_BSTR_ADDR(value) (value)
#define AROS_BSTR_strlen(value) ((value) ? strlen(value) : 0)
#define AROS_BSTR_getchar(value, index) ((value)[index])
#define AROS_BSTR_setstrlen(value, length) ((void)(value), (void)(length))

struct Task {
    struct Node tc_Node;
    ULONG tc_SigAlloc;
};

struct Process {
    struct Task pr_Task;
    struct MsgPort *pr_MsgPort;
    BPTR pr_CurrentDir;
    BPTR pr_CLI;
    LONG pr_Result2;
    APTR pr_WindowPtr;
    BPTR pr_CES;
    LONG pr_TaskNum;
    BPTR pr_HomeDir;
    struct List pr_LocalVars;
};

struct CommandLineInterface {
    LONG cli_Result2;
    BSTR cli_SetName;
    BPTR cli_CommandDir;
    LONG cli_ReturnCode;
    BSTR cli_CommandName;
    LONG cli_FailLevel;
    BSTR cli_Prompt;
    BPTR cli_StandardInput;
    BPTR cli_CurrentInput;
    BSTR cli_CommandFile;
    LONG cli_Interactive;
    LONG cli_Background;
    BPTR cli_CurrentOutput;
    LONG cli_DefaultStack;
    BPTR cli_StandardOutput;
    BPTR cli_Module;
    BPTR cli_StandardError;
};

struct CSource {
    STRPTR CS_Buffer;
    LONG CS_Length;
    LONG CS_CurChr;
};

/* The real structure contains more bookkeeping.  The Shell only needs an
   owning object that can be released by FreeDosObject(DOS_RDARGS). */
struct RDArgs {
    struct CSource RDA_Source;
    APTR RDA_Buffer;
    ULONG RDA_BufferSize;
    IPTR *RDA_Arguments;
    ULONG RDA_ArgumentCount;
    APTR RDA_Values[32];
    UBYTE RDA_Multiple[32];
    UBYTE RDA_Owned;
};

#endif
