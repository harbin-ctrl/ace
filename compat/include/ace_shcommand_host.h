#ifndef ACE_SHCOMMAND_HOST_H
#define ACE_SHCOMMAND_HOST_H

/* Force-included ahead of an AROS command that declares its arguments with
   the AROS_SHn macros, so that AROS's own
   compiler/include/aros/shcommands_notembedded.h can be compiled unmodified.

   That header expands into an AROS *process* entry point --

       __startup static AROS_PROCH(_entry, __argstr, argsize, SysBase)

   -- which on a real AROS build is what CreateProc() jumps to, with the
   command's argument line already in a register. A host command is an
   ordinary Linux process started by execv(), so something has to stand where
   CreateProc() stood. That is all this header is: the two attributes AROS's
   build configuration would have supplied, and a main() that calls _entry.

   Everything between those two points -- the template string, the RDArgs
   lifetime, ReadArgs(), FreeArgs(), PrintFault() on a parse failure, the
   SHArg() accessors -- is AROS's, unmodified. */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <utility/tagitem.h>

/* AROS puts the entry point in its own section and keeps the version string
   alive against the linker; neither applies here. __unused is the one that
   carries weight: the header declares local SysBase/DOSBase shadows a
   command need not touch, and ACE builds with -Werror. */
#define __startup
#ifndef __used
#define __used   __attribute__((used))
#endif
#ifndef __unused
#define __unused __attribute__((unused))
#endif

/* Stamped into the "$VER:" string. A real AROS build passes its own build
   date in; ACE has no such date to offer and the string is never read. */
#ifndef ADATE
#define ADATE "ACE"
#endif

struct ExecBase;

/* The entry point AROS_PROCH() is about to define. Declared here so the
   host main() below can be written before it, and so the two agree on the
   signature rather than relying on an implicit declaration. */
static SIPTR _entry(STRPTR argument_line, ULONG argument_size,
                    struct ExecBase *exec_base);

int ace_shcommand_start(int argc, char **argv,
                        SIPTR (*entry)(STRPTR, ULONG, struct ExecBase *));

int main(int argc, char **argv)
{
    return ace_shcommand_start(argc, argv, _entry);
}

#endif
