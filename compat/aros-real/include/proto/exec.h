#ifndef ACE_AROS_REAL_PROTO_EXEC_H
#define ACE_AROS_REAL_PROTO_EXEC_H

struct Message;
struct MsgPort;

struct Message *GetMsg(struct MsgPort *port);

#endif
