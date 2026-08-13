#ifndef AMIGA_SHELL_DOS_H
#define AMIGA_SHELL_DOS_H

#include <exec/types.h>
#include <dos/var.h>

#define DOSFALSE 0L
#define DOSTRUE (-1L)

#define MODE_OLDFILE 1005
#define MODE_NEWFILE 1006
#define MODE_READWRITE 1004
#define SHARED_LOCK  -2
#define ACCESS_READ  -2
#define CHANGE_LOCK  1
#define OFFSET_BEGINNING (-1)
#define OFFSET_CURRENT 0
#define OFFSET_END 1

#define RETURN_OK     0
#define RETURN_WARN   5
#define RETURN_ERROR 10
#define RETURN_FAIL  20

#define ERROR_FILE_NOT_OBJECT   121
#define ERROR_OBJECT_NOT_FOUND  205
#define ERROR_OBJECT_WRONG_TYPE 212
#define ERROR_NO_FREE_STORE     103
#define ERROR_BAD_TEMPLATE      114
#define ERROR_REQUIRED_ARG_MISSING 116
#define ERROR_LINE_TOO_LONG     120
#define ERROR_TOO_MANY_LEVELS   124
#define ERROR_TOO_MANY_ARGS      118
#define ERROR_BAD_NUMBER        115
#define ERROR_ACTION_NOT_KNOWN  209
#define ERROR_BREAK             304
#define ERROR_OBJECT_IN_USE     202
#define ERROR_INVALID_COMPONENT_NAME  210
#define ERROR_NOT_EXECUTABLE    305
#define ERROR_OBJECT_EXISTS     203
#define ERROR_DEVICE_NOT_MOUNTED 218

#define SIGBREAKF_CTRL_C (1u << 12)
#define SIGBREAKF_CTRL_D (1u << 13)
#define SIGBREAKF_CTRL_E (1u << 14)
#define SIGBREAKF_CTRL_F (1u << 15)

#define DOS_FIB    1
#define DOS_RDARGS 2

#define BNULL ((BPTR)0)

struct DosLibrary {
    int unused;
};

struct FileInfoBlock {
    LONG fib_DirEntryType;
    ULONG fib_Protection;
};

#define FIBF_SCRIPT (1u << 6)

#define FIBB_SCRIPT 6

struct CSource;

#define ITEM_NOTHING 0
#define ITEM_UNQUOTED 1
#define ITEM_QUOTED 2

BPTR AllocDosObject(LONG type, APTR tags);
void FreeDosObject(LONG type, APTR object);
BPTR Lock(CONST_STRPTR name, LONG mode);
LONG UnLock(BPTR lock);
BPTR CreateDir(CONST_STRPTR name);
LONG ChangeMode(LONG type, BPTR object, LONG mode);
LONG Examine(BPTR lock, struct FileInfoBlock *fib);
BPTR CurrentDir(BPTR lock);
LONG NameFromLock(BPTR lock, STRPTR buffer, LONG length);
void SetCurrentDirName(CONST_STRPTR name);

BPTR Output(void);
BPTR Input(void);
BPTR Open(CONST_STRPTR name, LONG mode);
LONG Close(BPTR file);
LONG DeleteFile(CONST_STRPTR name);
BOOL IsInteractive(BPTR file);
LONG FPutC(BPTR file, LONG character);
LONG FPuts(BPTR file, CONST_STRPTR string);
STRPTR FGets(BPTR file, STRPTR buffer, LONG length);
LONG Flush(BPTR file);
LONG IoErr(void);
void SetIoErr(LONG error);
LONG Fault(LONG error, CONST_STRPTR header, STRPTR buffer, LONG length);
BOOL PrintFault(LONG error, CONST_STRPTR header);
LONG Printf(CONST_STRPTR format, ...);

void CopyMem(CONST_APTR source, APTR destination, ULONG length);
STRPTR FilePart(CONST_STRPTR path);
STRPTR PathPart(CONST_STRPTR path);
BOOL AddPart(STRPTR dirname, CONST_STRPTR filename, ULONG size);
LONG PutStr(CONST_STRPTR string);
LONG FGetC(BPTR file);
LONG Read(BPTR file, APTR buffer, LONG length);
LONG FRead(BPTR file, APTR buffer, LONG block_size, LONG block_count);
LONG UnGetC(BPTR file, LONG character);
BPTR SelectInput(BPTR file);
BPTR SelectOutput(BPTR file);
LONG ReadItem(STRPTR buffer, LONG size, struct CSource *source);
LONG Seek(BPTR file, LONG position, LONG mode);
void SetProgramName(CONST_STRPTR name);
BPTR SetProgramDir(BPTR lock);
struct Segment;
struct Segment *FindSegment(CONST_STRPTR name, struct Segment *last,
                            BOOL system);
BPTR LoadSeg(CONST_STRPTR name);
void UnLoadSeg(BPTR segment);
LONG RunCommand(BPTR segment, ULONG stack, STRPTR arguments, LONG length);
BPTR ParentOfFH(BPTR file);
LONG ExamineFH(BPTR file, struct FileInfoBlock *fib);
struct RDArgs;
struct RDArgs *ReadArgs(CONST_STRPTR template, IPTR *arguments,
                        struct RDArgs *rdargs);
void FreeArgs(struct RDArgs *rdargs);

#endif
