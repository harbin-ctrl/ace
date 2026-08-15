#ifndef AMIGA_SHELL_DOSEXTENS_H
#define AMIGA_SHELL_DOSEXTENS_H

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/ports.h>
#include <dos/cliinit.h>
#include <dos/dos.h>
#include <string.h>

/* A real BSTR carries its length in the byte before its characters. ACE has
   two kinds of field typed BSTR and they do not agree, so these macros are
   the default rather than the rule.

   The CLI's own strings -- cli_Prompt, cli_SetName, cli_CommandFile -- are
   plain C strings that src/native_dos.c points straight at its own buffers,
   and AROS's real Shell.c and cliPrompt.c read them through these macros, so
   this is the definition those need.

   A DosList entry's dol_Name is the other kind: src/assign_compat.c builds it
   with a real length byte, because AROS's Assign.c reads that byte back
   through a length macro of its own that ACE cannot redefine. Anything
   compiled against dol_Name therefore overrides these first --
   src/assign_compat.h does it, which is why they are guarded. */
#ifndef AROS_BSTR_ADDR
#define AROS_BSTR_ADDR(value) (value)
#endif
#ifndef AROS_BSTR_strlen
#define AROS_BSTR_strlen(value) ((value) ? strlen(value) : 0)
#endif
#ifndef AROS_BSTR_getchar
#define AROS_BSTR_getchar(value, index) ((value)[index])
#endif
#ifndef AROS_BSTR_setstrlen
#define AROS_BSTR_setstrlen(value, length) ((void)(value), (void)(length))
#endif

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
    BPTR pr_CIS;
    BPTR pr_COS;
    struct MsgPort *pr_ConsoleTask;
    LONG pr_TaskNum;
    BPTR pr_HomeDir;
    struct List pr_LocalVars;
};

struct Segment {
    BPTR seg_Next;
    LONG seg_UC;
    BPTR seg_Seg;
};

/* Real, from dos/dosextens.h. fl_Task is the filesystem-handler process a
   real Lock() would be answered by; ACE's Lock() has no handler process
   (see src/native_dos.c), so nothing here ever reads it meaningfully -- it
   exists so exall.c's real source, which declares a struct FileLock * over
   every BPTR lock, has a field to declare. */
struct FileLock {
    BPTR             fl_Link;
    IPTR             fl_Key;
    LONG             fl_Access;
    struct MsgPort  *fl_Task;
    BPTR             fl_Volume;
};

/* The EndCLI/EndShell commands only require these stream-position fields. */
struct FileHandle {
    LONG fh_Pos;
    LONG fh_End;
};

#define CMD_INTERNAL (-2)
#define CMD_DISABLED (-999)

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
    IPTR RDA_DAList;
    UBYTE *RDA_Buffer;
    LONG RDA_BufSiz;
    STRPTR RDA_ExtHelp;
    LONG RDA_Flags;
    IPTR *RDA_Arguments;
    ULONG RDA_ArgumentCount;
    APTR RDA_Values[32];
    UBYTE RDA_Multiple[32];
    UBYTE RDA_Owned;
    ULONG RDA_BufferSize;
};

#define RDAF_STDIN  (1L << 0)
#define RDAF_NOALLOC (1L << 1)
#define RDAF_NOPROMPT (1L << 2)
#define RDAF_ALLOCATED_BY_READARGS (1L << 31)

/* DosList is private inside AROS dos.library, but Assign.c uses the public
 * DosList API to inspect and mutate assignments.  ACE represents the nodes
 * locally and backs their contents with the broker session. */
struct AssignList;

struct DosList {
    BPTR dol_Next;
    LONG dol_Type;
    APTR dol_Task;
    BPTR dol_Lock;
    union {
        struct {
            BSTR dol_Handler;
            LONG dol_StackSize;
            LONG dol_Priority;
            BPTR dol_Startup;
            BPTR dol_SegList;
            BPTR dol_GlobVec;
        } dol_handler;
        struct {
            struct DateStamp dol_VolumeDate;
            BPTR dol_LockList;
            LONG dol_DiskType;
            BPTR dol_unused;
        } dol_volume;
        struct {
            STRPTR dol_AssignName;
            struct AssignList *dol_List;
        } dol_assign;
    } dol_misc;
    BSTR dol_Name;
};

struct DevProc {
    struct MsgPort *dvp_Port;
    BPTR dvp_Lock;
    ULONG dvp_Flags;
    struct DosList *dvp_DevNode;
};

#define DVPB_UNLOCK 0
#define DVPB_ASSIGN 1
#define DVPF_UNLOCK (1L << DVPB_UNLOCK)
#define DVPF_ASSIGN (1L << DVPB_ASSIGN)

#define DLT_DEVICE     0
#define DLT_DIRECTORY  1
#define DLT_VOLUME     2
#define DLT_LATE       3
#define DLT_NONBINDING 4

#define LDB_READ    0
#define LDB_WRITE   1
#define LDB_DEVICES 2
#define LDB_VOLUMES 3
#define LDB_ASSIGNS 4
#define LDB_ENTRY   5
#define LDB_DELETE  6
#define LDF_READ    (1L << LDB_READ)
#define LDF_WRITE   (1L << LDB_WRITE)
#define LDF_DEVICES (1L << LDB_DEVICES)
#define LDF_VOLUMES (1L << LDB_VOLUMES)
#define LDF_ASSIGNS (1L << LDB_ASSIGNS)
#define LDF_ENTRY   (1L << LDB_ENTRY)
#define LDF_DELETE  (1L << LDB_DELETE)
#define LDF_ALL     (LDF_DEVICES | LDF_VOLUMES | LDF_ASSIGNS)

struct AssignList {
    struct AssignList *al_Next;
    BPTR al_Lock;
};

#endif
