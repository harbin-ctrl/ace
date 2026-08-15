#ifndef ACE_WAYWARD_BEEP_H
#define ACE_WAYWARD_BEEP_H

#include <stdint.h>

struct Screen;

/* The onscreen-windows compositor has six possible screens.  A bit in the
 * mask names the screen with that zero-based number. */
#define ACE_WAYWARD_BEEP_SCREEN_MASK ((uint32_t)0x3f)
#define ACE_WAYWARD_BEEP_ALL_SCREENS ACE_WAYWARD_BEEP_SCREEN_MASK

/* Send a visual beep request to the running Wayward/labwc compositor.  The
 * pointer is NULL for all screens; otherwise ACE's synthetic Screen value is
 * the six-bit selection mask. */
void WaywardBeep(struct Screen *screen);

#endif
