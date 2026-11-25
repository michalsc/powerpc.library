#include <exec/types.h>
#include <proto/exec.h>

#include "xring.h"

#include "powerpc.h"
#include "msgframe.h"

XRING_DEFINE_KICK_PPC

struct MsgFrame *CreateMsgFrameNoClear(struct PrivatePPCBase *PPCBase)
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    struct MsgFrame *msgFrame;
    
    Disable(); 
    
    while((msgFrame = mf_create(&PPCBase->pp_MFSelf)) == NULL)
    {
        /* All messages are in use, Enable interrupts and disable them again
        - maybe some will arrive soon */
        Enable();
        Disable();
    }

    Enable();

    return msgFrame;
}

struct MsgFrame *CreateMsgFrame(struct PrivatePPCBase *PPCBase)
{
    union {
        struct MsgFrame *msgFrame;
        uint32_t *u32;
    } u;
    u.msgFrame = CreateMsgFrameNoClear(PPCBase);

    if (u.msgFrame != NULL) {
        /* Clear the entire frame to avoid uninitialized data */
        uint32_t *clearFrame = u.u32;
        for (unsigned int i = 0; i < sizeof(struct MsgFrame); i += 4)
        {
            *clearFrame++ = 0;
        }
    }

    return u.msgFrame;
}

struct MsgFrame* GetMsgFrame(struct PrivatePPCBase* PPCBase)
{
    return mf_get(&PPCBase->pp_MFSelf);
}

void SendMsgFrame(struct PrivatePPCBase* PPCBase, struct MsgFrame* msgFrame)
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    mf_send(&PPCBase->pp_MFSelf, msgFrame);
    Supervisor(xring_kick_ppc);
}

void FreeMsgFrame(struct PrivatePPCBase* PPCBase, struct MsgFrame* msgFrame)
{
    mf_free(&PPCBase->pp_MFSelf, msgFrame);
}
