#ifndef AMIGA_SHELL_DEBUG_H
#define AMIGA_SHELL_DEBUG_H
#define D(x) do { } while (0);
#define DB2(x) do { } while (0);
#define ASSERT_VALID_PTR(pointer) do { (void)(pointer); } while (0)
#define ASSERT_VALID_PTR_OR_NULL(pointer) do { (void)(pointer); } while (0)
void bug(const char *format, ...);
#endif
