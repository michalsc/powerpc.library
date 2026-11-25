#ifndef _KERNEL_H
#define _KERNEL_H

#include "../src/powerpc.h"
#include "../src/libstructs.h"
#include "support.h"

void PatchLVOTable(struct PPCBase *ppcbase);

ULONG KSystemCall(struct PrivatePPCBase * PowerPCBase, struct SysCall *sc);
struct MsgFrame* KCreateMsgFramePPC(struct PrivatePPCBase *PowerPCBase);
struct MsgFrame* KGetMsgFramePPC(struct PrivatePPCBase* PowerPCBase);
void KFreeMsgFramePPC(struct PrivatePPCBase* PowerPCBase, struct MsgFrame* msgFrame);
void KSendMsgFramePPC(struct PrivatePPCBase* PowerPCBase, struct MsgFrame* msgFrame);

static inline ULONG CallKernel(struct PrivatePPCBase* PowerPCBase, struct SysCall *sc) {
    if (PowerPCBase->pp_ExceptionMode) {
        return KSystemCall(PowerPCBase, sc);
    } else {
        return SystemCall(sc);
    }
}

#endif /* _KERNEL_H */
