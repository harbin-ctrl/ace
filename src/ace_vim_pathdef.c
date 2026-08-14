#include "vim.h"

/* ACE supplies the runtime beside the executable, so Vim's generated Unix
   installation paths are deliberately replaced by local runtime metadata.
   This is an ACE build object, not a modification of Vim's source tree. */
char_u *default_vim_dir = (char_u *)"PROGDIR:runtime";
char_u *default_vimruntime_dir = (char_u *)"";
char_u *all_cflags = (char_u *)"ACE Vim Amiga build";
char_u *all_lflags = (char_u *)"ACE runtime";
char_u *compiled_arch = (char_u *)"ACE/Linux host";
char_u *compiled_user = (char_u *)"ACE";
char_u *compiled_sys = (char_u *)"ACE";
