#include "../src/msgframe.h"
#include "../src/constants.h"
#include "kernel.h"

struct MsgFrame* CreateMsgFramePPC(struct PrivatePPCBase* PowerPCBase)
{
    struct SysCall sc;
    sc.sc_Function = SC_CREATEMSG;

    ULONG msgFrame = CallKernel(PowerPCBase, &sc);

    if (msgFrame)
    {
        ULONG clearFrame = msgFrame;
        for (unsigned int i=0; i<sizeof(struct MsgFrame); i+=4)
        {
            *((ULONG*)(clearFrame)) = 0;
            clearFrame += 4;
        }
    }

    return (struct MsgFrame*)msgFrame;
}

struct MsgFrame* GetMsgFramePPC(struct PrivatePPCBase* PowerPCBase)
{
    struct SysCall sc;
    sc.sc_Function = SC_GETMSG;

    ULONG msgFrame = CallKernel(PowerPCBase, &sc);

    return (struct MsgFrame*)msgFrame;
}

void SendMsgFramePPC(struct PrivatePPCBase* PowerPCBase, struct MsgFrame* msgFrame)
{
    struct SysCall sc;
    sc.sc_Function = SC_SENDMSG;
    sc.sc_Arg[0] = (ULONG)msgFrame;

    CallKernel(PowerPCBase, &sc);

    return;
}
void FreeMsgFramePPC(struct PrivatePPCBase* PowerPCBase, struct MsgFrame* msgFrame)
{
    struct SysCall sc;
    sc.sc_Function = SC_FREEMSG;
    sc.sc_Arg[0] = (ULONG)msgFrame;

    CallKernel(PowerPCBase, &sc);

    return;
}