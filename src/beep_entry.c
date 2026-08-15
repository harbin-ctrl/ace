#include <dos/dos.h>
#include <exec/execbase.h>

SIPTR Start(STRPTR argument_line, ULONG argument_size,
            struct ExecBase *exec_base);

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return (int)Start(NULL, 0, NULL);
}
