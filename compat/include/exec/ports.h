#ifndef AMIGA_SHELL_EXEC_PORTS_H
#define AMIGA_SHELL_EXEC_PORTS_H

#ifdef AMIGA_EXEC_COMPAT_ENABLED
#define MsgPort amiga_exec_msg_port
#define Message amiga_exec_message
#else
struct MsgPort;
struct Message;
#endif

#endif
