#ifndef ACE_AROS_REAL_EXECBASE_H
#define ACE_AROS_REAL_EXECBASE_H

#include <exec/lists.h>

struct ExecBase {
    struct List DeviceList;
    struct List PortList;
};

#endif
