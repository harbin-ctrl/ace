#include <exec/types.h>

struct Task;

/* The graphical console's editor has no native DOS process beside it, so it
   needs the two Exec calls that native_dos.c supplies for command processes.
   Keep these stubs graphical-only: native commands use their process
   implementations instead, which avoids two definitions in one executable. */
APTR FindTask(CONST_STRPTR name)
{
    (void)name;
    return NULL;
}

ULONG SetSignal(ULONG set_mask, ULONG clear_mask)
{
    (void)set_mask;
    (void)clear_mask;
    return 0;
}

void Signal(struct Task *task, ULONG signal_set)
{
    (void)task;
    (void)signal_set;
}
