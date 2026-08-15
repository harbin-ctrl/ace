#ifndef AMIGA_SHELL_DOS_DATETIME_H
#define AMIGA_SHELL_DOS_DATETIME_H

#include <dos/dos.h>

struct DateTime
{
    struct DateStamp dat_Stamp;
    UBYTE dat_Format;
    UBYTE dat_Flags;
    UBYTE *dat_StrDay;
    UBYTE *dat_StrDate;
    UBYTE *dat_StrTime;
};

#define LEN_DATSTRING 16

#define FORMAT_DOS 0
#define FORMAT_INT 1
#define FORMAT_USA 2
#define FORMAT_CDN 3
#define FORMAT_DEF 4
#define FORMAT_MAX FORMAT_CDN

#define DTB_SUBST  0
#define DTB_FUTURE 1
#define DTF_SUBST  (1 << DTB_SUBST)
#define DTF_FUTURE (1 << DTB_FUTURE)

#endif
