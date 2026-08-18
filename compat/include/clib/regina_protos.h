#ifndef CLIB_REGINA_PROTOS_H
#define CLIB_REGINA_PROTOS_H

#include <rexx/storage.h>
#include <rexxsaa.h>

/* ACE uses the AROS Regina vectors as ordinary host C entry points. */
APIRET APIENTRY RexxStart(LONG ArgCount, PRXSTRING ArgList,
                          PCSZ ProgName, PRXSTRING Instore, PCSZ EnvName,
                          LONG CallType, PRXSYSEXIT Exits,
                          PSHORT ReturnCode, PRXSTRING Result);
BOOL IsReginaMsg(struct RexxMsg *msg);

#endif
