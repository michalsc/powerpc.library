#include <common/compiler.h>
#include <exec/types.h>
#include <proto/exec.h>

#include "powerpc.h"
#include "xring.h"
#include "constants.h"

#include "msgframe.h"

int OnInterrupt(REGARG(struct PrivatePPCBase *PPCBase, "a1"))
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    struct MsgFrame *msgFrame = NULL;

    /* Check (and ack) whether the interrupt came from the PPC */
    if (xring_ack_ppc_irq())
    {
        /* Drain messages from the queue */
        while ((msgFrame = GetMsgFrame(PPCBase)) != NULL)
        {
            switch (msgFrame->mf_Identifier)
            {
                case ID_T68K:
                case ID_END:
                {
                    struct MsgPort* mirrorPort;
                    if (mirrorPort = msgFrame->mf_MirrorPort)
                    {
                        if (msgFrame->mf_Identifier != ID_END)
                        {
                            /* assign, keeping the mirror's port bit -- as
                                * Sonnet's MsgT68k does from the same place,
                                * powerpc.s:2536; see functions68k.c */
                            struct Task* sigTask = mirrorPort->mp_SigTask;
                            sigTask->tc_SigAlloc = msgFrame->mf_Arg[0]
                                                    | (1 << (ULONG)mirrorPort->mp_SigBit);
                        }
                        PutMsg(mirrorPort, &msgFrame->mf_Message);
                    }
                    else
                    {
                        PutMsg(PPCBase->pp_MasterControl, &msgFrame->mf_Message);
                    }
                    break;
                }
                case ID_FPPC:
                {
                    struct MsgPort* replyPort = msgFrame->mf_Message.mn_ReplyPort;
                    /* assign, keeping the caller's port bit -- Sonnet's
                        * MsgFPPC, powerpc.s:2546 */
                    struct Task* sigTask = replyPort->mp_SigTask;
                    sigTask->tc_SigAlloc = msgFrame->mf_Arg[0]
                                            | (1 << (ULONG)replyPort->mp_SigBit);
                    ReplyMsg(&msgFrame->mf_Message);
                    break;
                }
                case ID_XMSG:
                {
                    struct MsgPort* xPort = (struct MsgPort*)msgFrame->mf_Arg[0];
                    struct Message* xMessage = (struct Message*)msgFrame->mf_Arg[1];
                    xMessage->mn_Node.ln_Name = (APTR)xMessage->mn_ReplyPort;
                    xMessage->mn_ReplyPort = PPCBase->pp_MasterControl;
                    FreeMsgFrame(PPCBase, msgFrame);
                    /*
                        * F6: THE RECEIVER SEES THE XMsg TYPE, NT_XMSGPPC, as on
                        * WarpOS, whose PutXMsg68K links the message without
                        * touching it (powerpc.s:6207-6212). exec's PutMsg sets
                        * NT_MESSAGE, so the type is put back before anyone sees
                        * the message: GortInt runs as an interrupt (libinit.c's
                        * server, and ZenInt's below), so no task or soft
                        * interrupt runs before it returns. myFreeXMsg
                        * (functions68k.c) reads it.
                        */
                    PutMsg(xPort, xMessage);
                    xMessage->mn_Node.ln_Type = NT_XMSGPPC;
                    break;
                }
                case ID_SIG:
                {
                    Signal((struct Task*)msgFrame->mf_Arg[0], msgFrame->mf_Signals);
                    FreeMsgFrame(PPCBase, msgFrame);
                    break;
                }
                case ID_RX68:
                {
                    struct Message* xMessage = (struct Message*)msgFrame->mf_Arg[0];
                    struct MsgPort* xPort = xMessage->mn_ReplyPort;
                    FreeMsgFrame(PPCBase, msgFrame);
                    PutMsg(xPort, xMessage);
                    break;
                }
                case ID_GETV:
                {
                    msgFrame->mf_Arg[0] = *((ULONG*)(msgFrame->mf_Arg[1]));
                    msgFrame->mf_Identifier = ID_DONE;
                    FreeMsgFrame(PPCBase, msgFrame);
                    break;
                }
                case ID_PUTW:
                {
                    *((ULONG*)(msgFrame->mf_Arg[1])) = msgFrame->mf_Arg[0];
                    msgFrame->mf_Identifier = ID_DONE;
                    FreeMsgFrame(PPCBase, msgFrame);
                    break;
                }
                case ID_PUTH:
                {
                    *((USHORT*)(msgFrame->mf_Arg[1])) = (USHORT)msgFrame->mf_Arg[0];
                    msgFrame->mf_Identifier = ID_DONE;
                    FreeMsgFrame(PPCBase, msgFrame);
                    break;
                }
                case ID_PUTB:
                {
                    *((UBYTE*)(msgFrame->mf_Arg[1])) = (UBYTE)msgFrame->mf_Arg[0];
                    msgFrame->mf_Identifier = ID_DONE;
                    FreeMsgFrame(PPCBase, msgFrame);
                    break;
                }
                default:
                {
                    PutMsg(PPCBase->pp_MasterControl, (struct Message*)msgFrame);
                    break;
                }
            }
        }

        return 1;
    }
    return 0;
}
