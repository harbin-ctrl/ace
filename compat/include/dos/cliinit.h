#ifndef AMIGA_SHELL_DOS_CLIINIT_H
#define AMIGA_SHELL_DOS_CLIINIT_H

#define FNF_VALIDFLAGS (1u << 31)
#define FNF_ASYNCSYSTEM (1u << 3)
#define FNF_SYSTEM (1u << 2)
#define FNF_USERINPUT (1u << 1)
#define FNF_RUNOUTPUT (1u << 0)
#define CLI_NEWCLI 1
#define CLI_RUN (-1)
#define CLI_SYSTEM (-2)
#define CLI_ASYSTEM (-3)
#define CLI_BOOT (-4)

#define CLI_DEFAULTSTACK_UNIT 4
#define AROS_STACKSIZE 8192

#endif
