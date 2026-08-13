#ifndef ACE_AROS_REAL_CONUNIT_H
#define ACE_AROS_REAL_CONUNIT_H

/* The real console-unit definition is supplied by the future console.device
   compatibility layer.  The handler only uses it through IORequest fields. */

#define CONU_LIBRARY  (-1)
#define CONU_STANDARD 0
#define CONU_CHARMAP  1
#define CONU_SNIPMAP  3

struct Window;
struct ConUnit {
    struct Window *cu_Window;
};

#endif
