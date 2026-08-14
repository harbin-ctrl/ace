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
#define EXCLUSIVE_LOCK -1
#define ACCESS_READ  -2
#define ACCESS_WRITE -1
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
#define ERROR_KEY_NEEDS_ARG    117
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
#define ERROR_DIR_NOT_FOUND     204
#define ERROR_RENAME_ACROSS_DEVICES 215
#define ERROR_DISK_FULL         221
#define ERROR_NO_MORE_ENTRIES   232
#define ERROR_NOT_IMPLEMENTED   236
#define ERROR_BUFFER_OVERFLOW   303
#define ERROR_IS_SOFT_LINK      201
#define ERROR_INVALID_RESIDENT_LIBRARY 122
#define ERROR_DISK_WRITE_PROTECTED 214
#define ERROR_DIRECTORY_NOT_EMPTY 216
#define ERROR_COMMENT_TOO_BIG   220
#define ERROR_DELETE_PROTECTED  222
#define ERROR_WRITE_PROTECTED   223

#define DOSNAME "dos.library"

#define SIGBREAKF_CTRL_C (1u << 12)
#define SIGBREAKF_CTRL_D (1u << 13)
#define SIGBREAKF_CTRL_E (1u << 14)
#define SIGBREAKF_CTRL_F (1u << 15)

#define DOS_FIB          1
#define DOS_RDARGS       2
#define DOS_EXALLCONTROL 3

#define BNULL ((BPTR)0)

#define LOCK_SAME         0
#define LOCK_SAME_VOLUME  1
#define LOCK_DIFFERENT   (-1)

/* Real, from dos/dosextens.h: only the one field the pattern-matching
   engine reads (DOSBase->dl_Root->rn_Flags & RNF_WILDSTAR, whether '*' is
   also a wildcard). ACE has no library-open protocol to fill in the rest,
   so this is the whole struct rather than a shadow of a bigger real one. */
struct MsgPort;

struct RootNode {
    ULONG rn_Flags;
    struct MsgPort *rn_BootProc;
};

struct DosLibrary {
    struct RootNode *dl_Root;
};

#define RNB_WILDSTAR 24
#define RNF_WILDSTAR (1L << RNB_WILDSTAR)

/* Real, from dos/dos.h: the datestamp real FileInfoBlock/ExAllData embed. */
struct DateStamp {
    LONG ds_Days;
    LONG ds_Minute;
    LONG ds_Tick;
};

#define MAXFILENAMELENGTH 108
#define MAXCOMMENTLENGTH  80

/* Real, from dos/dosextens.h: file/dir type constants shared by
   FileInfoBlock's fib_DirEntryType and ExAllData's ed_Type. */
#define ST_PIPEFILE -5
#define ST_LINKFILE -4
#define ST_FILE     -3
#define ST_ROOT      1
#define ST_USERDIR   2
#define ST_SOFTLINK  3
#define ST_LINKDIR   4

/* Real, from dos/dos.h. */
struct FileInfoBlock {
    IPTR  fib_DiskKey;
    LONG  fib_DirEntryType;
    UBYTE fib_FileName[MAXFILENAMELENGTH];
    LONG  fib_Protection;
    LONG  fib_EntryType;
    LONG  fib_Size;
    LONG  fib_NumBlocks;
    struct DateStamp fib_Date;
    UBYTE fib_Comment[MAXCOMMENTLENGTH];
    UWORD fib_OwnerUID;
    UWORD fib_OwnerGID;
};

#define FIBB_DELETE  0
#define FIBB_EXECUTE 1
#define FIBB_WRITE   2
#define FIBB_READ    3
#define FIBB_ARCHIVE 4
#define FIBB_PURE    5
#define FIBB_SCRIPT  6
#define FIBB_HOLD    7

#define FIBF_DELETE  (1u << FIBB_DELETE)
#define FIBF_EXECUTE (1u << FIBB_EXECUTE)
#define FIBF_WRITE   (1u << FIBB_WRITE)
#define FIBF_READ    (1u << FIBB_READ)
#define FIBF_ARCHIVE (1u << FIBB_ARCHIVE)
#define FIBF_PURE    (1u << FIBB_PURE)
#define FIBF_SCRIPT  (1u << FIBB_SCRIPT)
#define FIBF_HOLD    (1u << FIBB_HOLD)

/* Real, from dos/dosasl.h: pattern-matching's own two structs, used with
   MatchFirst()/MatchNext()/MatchEnd(). */
struct AChain {
    struct AChain *an_Child;
    struct AChain *an_Parent;
    BPTR                  an_Lock;
    struct FileInfoBlock  an_Info;
    BYTE                  an_Flags;
    UBYTE                 an_String[1];
};

#define DDB_PatternBit  0
#define DDB_ExaminedBit 1
#define DDB_Completed   2
#define DDB_AllBit      3
#define DDB_Single      4
#define DDF_PatternBit  (1 << DDB_PatternBit)
#define DDF_ExaminedBit (1 << DDB_ExaminedBit)
#define DDF_Completed   (1 << DDB_Completed)
#define DDF_AllBit      (1 << DDB_AllBit)
#define DDF_Single      (1 << DDB_Single)

struct AnchorPath {
    struct AChain *ap_Base;
    struct AChain *ap_Last;
    LONG                  ap_BreakBits;
    LONG                  ap_FoundBreak;
    BYTE                  ap_Flags;
    BYTE                  ap_Reserved;
    WORD                  ap_Strlen;
    struct FileInfoBlock  ap_Info;
    UBYTE                 ap_Buf[1];
};

/* Real, from dos/dosasl.h: these fields are addressed under both names. */
#define ap_First   ap_Base
#define ap_Current ap_Last
#define ap_Length  ap_Flags

#define APB_DOWILD       0
#define APB_ITSWILD      1
#define APB_DODIR        2
#define APB_DIDDIR       3
#define APB_NOMEMERR     4
#define APB_DODOT        5
#define APB_DirChanged   6
#define APB_FollowHLinks 7
#define APF_DOWILD       (1 << APB_DOWILD)
#define APF_ITSWILD      (1 << APB_ITSWILD)
#define APF_DODIR        (1 << APB_DODIR)
#define APF_DIDDIR       (1 << APB_DIDDIR)
#define APF_NOMEMERR     (1 << APB_NOMEMERR)
#define APF_DODOT        (1 << APB_DODOT)
#define APF_DirChanged   (1 << APB_DirChanged)
#define APF_FollowHLinks (1 << APB_FollowHLinks)

/* Real, from dos/dosasl.h: tokens ParsePattern() writes into its output
   buffer in place of wildcard characters. */
#define P_ANY      0x80
#define P_SINGLE   0x81
#define P_ORSTART  0x82
#define P_ORNEXT   0x83
#define P_OREND    0x84
#define P_NOT      0x85
#define P_NOTEND   0x86
#define P_NOTCLASS 0x87
#define P_CLASS    0x88
#define P_REPBEG   0x89
#define P_REPEND   0x8a
#define P_STOP     0x8b

struct Hook;

struct CSource;

#define ITEM_NOTHING 0
#define ITEM_UNQUOTED 1
#define ITEM_QUOTED 2
#define ITEM_ERROR (-1)
#define ITEM_EQUAL (-2)

BPTR AllocDosObject(LONG type, APTR tags);
void FreeDosObject(LONG type, APTR object);
BPTR Lock(CONST_STRPTR name, LONG mode);
struct DevProc *GetDeviceProc(CONST_STRPTR name, struct DevProc *dp);
void FreeDeviceProc(struct DevProc *dp);
LONG UnLock(BPTR lock);
BPTR CreateDir(CONST_STRPTR name);
LONG ChangeMode(LONG type, BPTR object, LONG mode);
LONG Examine(BPTR lock, struct FileInfoBlock *fib);
BPTR CurrentDir(BPTR lock);
LONG NameFromLock(BPTR lock, STRPTR buffer, LONG length);
void SetCurrentDirName(CONST_STRPTR name);
BPTR DupLock(BPTR lock);
LONG ExNext(BPTR lock, struct FileInfoBlock *fib);

struct ExAllData;
struct ExAllControl;
BOOL ExAll(BPTR lock, struct ExAllData *buffer, LONG size, LONG data,
          struct ExAllControl *control);

LONG ParsePattern(CONST_STRPTR source, STRPTR dest, LONG dest_length);
LONG ParsePatternNoCase(CONST_STRPTR source, STRPTR dest, LONG dest_length);
BOOL MatchPattern(CONST_STRPTR pattern, CONST_STRPTR string);
BOOL MatchPatternNoCase(CONST_STRPTR pattern, CONST_STRPTR string);
LONG MatchFirst(CONST_STRPTR pattern, struct AnchorPath *anchor);
LONG MatchNext(struct AnchorPath *anchor);
void MatchEnd(struct AnchorPath *anchor);

BPTR Output(void);
BPTR Input(void);
BPTR Open(CONST_STRPTR name, LONG mode);
LONG Close(BPTR file);
LONG DeleteFile(CONST_STRPTR name);
LONG SetProtection(CONST_STRPTR name, ULONG protection);
LONG SetComment(CONST_STRPTR name, CONST_STRPTR comment);
LONG Rename(CONST_STRPTR old_name, CONST_STRPTR new_name);
LONG SameLock(BPTR lock1, BPTR lock2);
BOOL IsInteractive(BPTR file);
LONG FPutC(BPTR file, LONG character);
LONG FPuts(BPTR file, CONST_STRPTR string);
STRPTR FGets(BPTR file, STRPTR buffer, LONG length);
LONG Flush(BPTR file);
LONG IoErr(void);
void SetIoErr(LONG error);
LONG Write(BPTR file, CONST_APTR buffer, LONG length);
LONG Fault(LONG error, CONST_STRPTR header, STRPTR buffer, LONG length);
BOOL PrintFault(LONG error, CONST_STRPTR header);
LONG Printf(CONST_STRPTR format, ...);
LONG SplitName(CONST_STRPTR path, LONG separator, STRPTR buffer,
               LONG buffer_position, LONG buffer_size);

void CopyMem(CONST_APTR source, APTR destination, ULONG length);
STRPTR FilePart(CONST_STRPTR path);
STRPTR PathPart(CONST_STRPTR path);
BOOL AddPart(STRPTR dirname, CONST_STRPTR filename, ULONG size);
STRPTR stccpy(STRPTR destination, CONST_STRPTR source, LONG length);
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
