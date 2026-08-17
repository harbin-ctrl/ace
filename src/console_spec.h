#ifndef ACE_CONSOLE_SPEC_H
#define ACE_CONSOLE_SPEC_H

#include <stddef.h>

/* The window part of an Amiga CON: name.  Options after the title are kept
 * in the original specification for the handler; the GUI only needs the
 * geometry and title at this stage. */
struct ace_console_spec {
    int x;
    int y;
    int width;
    int height;
    int has_position;
    int has_size;
    char title[256];
};

/* Parse CON:x/y/w/h/title/... . Empty or omitted fields use host defaults. */
int ace_console_spec_parse(const char *text, struct ace_console_spec *spec);

#endif
