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
#define OFFSET_BEGINNING (-1)
#define OFFSET_CURRENT 0
#define OFFSET_END 1

#define RETURN_OK     0
#define RETURN_WARN   5
#define RETURN_ERROR 10
#define RETURN_FAIL  20

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
#define ERROR_INVALID_COMPONENT_NAME  212
#define ERROR_NOT_EXECUTABLE    232
#define ERROR_FILE_NOT_OBJECT   233

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

struct CSource {
    STRPTR CS_Buffer;
    LONG CS_Length;
    LONG CS_CurChr;
};

#define ITEM_NOTHING 0
#define ITEM_UNQUOTED 1
#define ITEM_QUOTED 2

BPTR AllocDosObject(LONG type, APTR tags);
void FreeDosObject(LONG type, APTR object);
BPTR Lock(CONST_STRPTR name, LONG mode);
LONG UnLock(BPTR lock);
LONG Examine(BPTR lock, struct FileInfoBlock *fib);
BPTR CurrentDir(BPTR lock);
LONG NameFromLock(BPTR lock, STRPTR buffer, LONG length);
void SetCurrentDirName(CONST_STRPTR name);

BPTR Output(void);
BPTR Input(void);
BPTR Open(CONST_STRPTR name, LONG mode);
LONG Close(BPTR file);
LONG FPutC(BPTR file, LONG character);
LONG FPuts(BPTR file, CONST_STRPTR string);
STRPTR FGets(BPTR file, STRPTR buffer, LONG length);
LONG Flush(BPTR file);
LONG IoErr(void);
void SetIoErr(LONG error);
void PrintFault(LONG error, CONST_STRPTR header);
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
struct RDArgs;
struct RDArgs *ReadArgs(CONST_STRPTR template, IPTR *arguments,
                        struct RDArgs *rdargs);

#endif
