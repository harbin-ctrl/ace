#ifndef _LIBRARIES_LOCALE_H
#define _LIBRARIES_LOCALE_H

#include <exec/types.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>

struct Locale {
    int dummy;
};

#define SC_COLLATE1 0
#define SC_COLLATE2 1

static inline struct Locale *OpenLocale(STRPTR name) {
    static struct Locale dummy_locale;
    return &dummy_locale;
}

static inline void CloseLocale(struct Locale *locale) {
    (void)locale;
}

static inline ULONG IsUpper(struct Locale *locale, ULONG character) {
    return isupper((int)character);
}

static inline ULONG IsPrint(struct Locale *locale, ULONG character) {
    return isprint((int)character);
}

static inline ULONG ConvToUpper(struct Locale *locale, ULONG character) {
    return toupper((int)character);
}

static inline LONG StrnCmp(struct Locale *locale, STRPTR string1, STRPTR string2, LONG length, ULONG type) {
    if (type == SC_COLLATE2) {
        return strncmp((const char *)string1, (const char *)string2, length);
    } else {
        return strncasecmp((const char *)string1, (const char *)string2, length);
    }
}

#endif /* _LIBRARIES_LOCALE_H */
