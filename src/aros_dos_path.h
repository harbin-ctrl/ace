#ifndef ACE_AROS_DOS_PATH_H
#define ACE_AROS_DOS_PATH_H

#include <stddef.h>

#include <dos/dosextens.h>

/* These are the real AROS DOS dispatcher entry points, compiled under
 * private ACE names so the host-side DOS API can call them without changing
 * the imported sources. */
struct DevProc *ace_aros_GetDeviceProc(CONST_STRPTR name,
                                       struct DevProc *previous);
void ace_aros_FreeDeviceProc(struct DevProc *device);

#endif
