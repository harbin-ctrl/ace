#ifndef AMIGA_SHELL_AROS_SYMBOLSETS_H
#define AMIGA_SHELL_AROS_SYMBOLSETS_H

/* AROS's symbol sets are link-time lists built from a private ELF section:
   the startup code walks the LIBS set and opens each library a program
   declared with ADD2LIBS() before main() runs. ACE has no such startup and
   no library-open protocol -- OpenLibrary() in src/native_dos.c hands back
   the one static base ACE keeps per library -- so a program here declares
   nothing into the set and there is nothing to walk.

   Only the three names AROS's shell-command macro header
   (compiler/include/aros/shcommands_notembedded.h) actually uses are
   defined. The rest of the real header is the set machinery itself, which
   would have nothing to operate on. */

#define THIS_PROGRAM_HANDLES_SYMBOLSET(x)

/* Non-zero means "every declared library opened", which is vacuously true
   for an empty set. The command macro runs its body only if this succeeds. */
#define set_open_libraries()  1
#define set_close_libraries() ((void)0)

#endif
