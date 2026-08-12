#ifndef AMIGA_SHELL_EXEC_TYPES_H
#define AMIGA_SHELL_EXEC_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int8_t BYTE;
typedef uint8_t UBYTE;
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int BOOL;
typedef intptr_t IPTR;
typedef uintptr_t ULONGPTR;
typedef void *APTR;
typedef const void *CONST_APTR;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
typedef void *BPTR;
typedef char *BSTR;
typedef int64_t QUAD;
typedef uint64_t UQUAD;
typedef intptr_t SIPTR;
typedef char TEXT;
typedef void *RAWARG;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#endif
