#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <proto/intuition.h>
#include <intuition/intuition.h>

/* Keep this test independent of the full DOS runtime. Its purpose is the
   EasyRequest varargs hand-off; the production object uses ACE's RawDoFmt. */
void RawDoFmt(CONST_STRPTR format, va_list arguments,
              void (*put_character)(void), APTR data)
{
    char formatted[256];
    void (*put)(UBYTE, APTR) = (void (*)(UBYTE, APTR))put_character;
    size_t length;

    vsnprintf(formatted, sizeof(formatted), format, arguments);
    length = strlen(formatted);
    for (size_t index = 0; index < length; index++)
        put((UBYTE)formatted[index], data);
}

int main(void)
{
    struct EasyStruct easy_struct = {
        sizeof(struct EasyStruct), 0,
        "EasyRequest test",
        "Body %s %s",
        "Retry|Cancel"
    };

    if (EasyRequest(NULL, &easy_struct, NULL, "one", "two") != 0)
        return 1;
    return 0;
}
