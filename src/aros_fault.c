/*
 * ACE port of the AROS dos.library Fault() and PrintFault() routines.
 *
 * The formatting and catalog text come from:
 *   /home/erik/aros/aros/rom/dos/fault.c
 *   /home/erik/aros/aros/rom/dos/printfault.c
 * and rom/dos/catalogs/dos.cd.
 *
 * The only host-facing hooks are Output(), FPuts(), and SetIoErr(), supplied
 * by the ACE DOS/stream layer.
 */

#include <stdio.h>
#include <string.h>

#include <dos/dos.h>

struct fault_string {
    LONG code;
    const char *text;
};

static const struct fault_string fault_strings[] = {
    {100, "undefined error"},
    {103, "no memory"},
    {116, "required argument missing"},
    {118, "too many arguments"},
    {120, "line too long"},
    {121, "file is not an object module"},
    {124, "too many levels"},
    {202, "object is in use"},
    {203, "object already exists"},
    {204, "directory not found"},
    {205, "object not found"},
    {206, "invalid window description"},
    {207, "object too large"},
    {209, "filesystem action type unknown"},
    {210, "object name invalid"},
    {211, "invalid object lock"},
    {212, "object is not of required type"},
    {213, "disk not validated"},
    {214, "disk is write-protected"},
    {215, "rename across devices attempted"},
    {216, "directory not empty"},
    {217, "too many levels"},
    {218, "device (or volume) is not mounted"},
    {219, "seek failure"},
    {220, "comment is too long"},
    {221, "disk is full"},
    {222, "object is protected from deletion"},
    {223, "file is write protected"},
    {224, "file is read protected"},
    {225, "not a valid DOS disk"},
    {226, "no disk in drive"},
    {232, "no more entries in directory"},
    {233, "object is soft link"},
    {234, "object is linked"},
    {235, "bad loadfile hunk"},
    {236, "function not implemented"},
    {240, "record not locked"},
    {241, "record lock collision"},
    {242, "record lock timeout"},
    {243, "record unlock error"},
    {303, "buffer overflow"},
    {304, "***Break"},
    {305, "file not executable"},
    {10000, "no matching Else or EndIf"},
    {10001, "this command is supposed to be used in command files only"},
};

static const char *ace_dos_get_string(LONG code)
{
    for (size_t index = 0; index < sizeof(fault_strings) /
                                  sizeof(fault_strings[0]); index++)
        if (fault_strings[index].code == code)
            return fault_strings[index].text;
    return NULL;
}

/*
 * This follows AROS rom/dos/fault.c.  In the complete AROS build,
 * DosGetString() supplies the catalog text; ACE supplies the same DOS 3.1
 * catalog entries locally until the catalog subsystem is connected.
 */
LONG Fault(LONG code, CONST_STRPTR header, STRPTR buffer, LONG length)
{
    LONG index = 0;
    const char *text;

    if (!buffer || length <= 0)
        return 0;
    if (code == 0) {
        *buffer = '\0';
        return 0;
    }

    length--;
    if (header) {
        while (index < length && *header)
            buffer[index++] = *header++;
        if (index < length)
            buffer[index++] = ':';
        if (index < length)
            buffer[index++] = ' ';
    }

    text = ace_dos_get_string(code);
    if (text) {
        while (index < length && *text)
            buffer[index++] = *text++;
    } else {
        char number[32];
        int written = snprintf(number, sizeof(number), "Error %ld",
                               (long)code);
        for (int offset = 0; offset < written && index < length; offset++)
            buffer[index++] = number[offset];
    }
    buffer[index] = '\0';
    return length - index + 1;
}

/*
 * This follows AROS rom/dos/printfault.c: formatting is delegated to Fault,
 * output goes through the current DOS Output() stream, and IoErr is restored
 * to the reported code.
 */
BOOL PrintFault(LONG code, CONST_STRPTR header)
{
    char buffer[80];

    (void)Fault(code, NULL, buffer, sizeof(buffer));
    if (code == 0)
        return DOSTRUE;
    if (header) {
        if (!FPuts(Output(), header) && !FPuts(Output(), ": ") &&
            !FPuts(Output(), buffer) && !FPuts(Output(), "\n")) {
            SetIoErr(code);
            return DOSTRUE;
        }
    } else if (!FPuts(Output(), buffer) && !FPuts(Output(), "\n")) {
        SetIoErr(code);
        return DOSTRUE;
    }
    SetIoErr(code);
    return DOSFALSE;
}
