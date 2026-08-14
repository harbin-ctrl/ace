#define _POSIX_C_SOURCE 200809L

#include <time.h>
#include <unistd.h>

#include <exec/types.h>

struct Window;

/* Keep the timing primitive outside the console editor.  Native programs,
   including Vim, need DOS Delay() without pulling in the AROS line editor. */
void Delay(ULONG ticks)
{
    struct timespec request = {
        .tv_sec = ticks / 50,
        .tv_nsec = (long)(ticks % 50) * 20000000L,
    };

    nanosleep(&request, NULL);
}

/* ACE has no separate Intuition window title object.  The Amiga backend may
   call this while the Workbench window handle is absent; retaining the call
   as a harmless seam lets the terminal backend link unchanged. */
void SetWindowTitles(struct Window *window, CONST_STRPTR title,
                     CONST_STRPTR screen_title)
{
    (void)window;
    (void)title;
    (void)screen_title;
}

/* Vim's portable file code asks its platform layer for fsync(). */
int vim_fsync(int file_descriptor)
{
    return fsync(file_descriptor);
}
