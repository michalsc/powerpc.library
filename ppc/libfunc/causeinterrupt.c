#pragma pack(push,2)
#include <exec/types.h>
#include <utility/tagitem.h>
#include <powerpc/powerpc.h>
#include "../../src/libstructs.h"
#pragma pack(pop)

#include "../support.h"
#include "../../src/powerpc.h"

#define DFUNC(x) x

void L_CauseInterrupt(struct PPCBase *)
{
    struct SysCall sc;
    //sc.sc_Function = SC_CAUSE;
    
    DFUNC(kprintf("[PPC] powerpc.library/CauseInterrupt()\n"));

    SystemCall(&sc);
}
