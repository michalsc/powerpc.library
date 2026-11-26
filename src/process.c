#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <powerpc/powerpc.h>

#include <proto/exec.h>
#include <proto/dos.h>

#include "powerpc.h"
#include "powerpc_private.h"
#include "libstructs.h"
#include "support.h"
#include "constants.h"
#include "asmstubs.h"

struct StartupMessage {
    struct Message msg;
    struct PrivatePPCBase *PPCBase;
};

/********************************************************************************************
*
*	Process that handles messages from the PPC.
*
*********************************************************************************************/

void MirrorTask(void)
{
    struct ExecBase* SysBase = *(struct ExecBase **)4UL;
    struct Task* myTask = FindTask(NULL);
    struct StartupMessage *msg = (struct StartupMessage *)GetMsg(&((struct Process*)myTask)->pr_MsgPort);
    struct PrivatePPCBase * PowerPCBase = msg->PPCBase;

    struct MsgFrame* myFrame;
    struct MsgPort* mirrorPort;
    struct MirrorTask* myMirror = NULL;

    ReplyMsg((struct Message *)msg);

    BPTR startDir = ((struct Process*)myTask)->pr_CurrentDir;

    ULONG mySignal = Wait(SIGBREAKF_CTRL_F);

    myTask->tc_Flags &= ~TF_PPC;

    if (!(mirrorPort = CreateMsgPort()))
    {
        ((struct Process*)myTask)->pr_CurrentDir = startDir;
        return;
    }

    myTask->tc_Flags |= TF_PPC;

    if (myFrame = (struct MsgFrame*)myTask->tc_UserData)
    {
        PutMsg(mirrorPort, (struct Message*)myFrame);
    }

    if (myMirror = (struct MirrorTask*)AllocVec(sizeof(struct MirrorTask),
                                                (MEMF_PUBLIC | MEMF_CLEAR)))
    {
        myMirror->mt_Task    = myTask;
        myMirror->mt_Port    = mirrorPort;
        myMirror->mt_PPCTask = myFrame ? myFrame->mf_PPCTask : NULL;

        Disable();

        AddHead((struct List*)&PowerPCBase->pp_MirrorList,
                (struct Node*)myMirror);

        Enable();
    }

    ULONG andTemp = ~(0xfff + (1 << (ULONG)mirrorPort->mp_SigBit));

    while (1)
    {
        mySignal = IsListEmpty((struct List*)&mirrorPort->mp_MsgList)
                    ? Wait((ULONG)myTask->tc_SigAlloc & 0xfffff000)
                    : (ULONG)(1 << (ULONG)mirrorPort->mp_SigBit);

        if (mySignal & (1 << (ULONG)mirrorPort->mp_SigBit))
        {
            myTask->tc_SigRecvd |= (mySignal & andTemp);
            while (myFrame = (struct MsgFrame*)GetMsg(mirrorPort))
            {
                if (myFrame->mf_Identifier == ID_T68K)
                {
                    myTask->tc_SigAlloc = myFrame->mf_Arg[0]
                                        | (1 << (ULONG)mirrorPort->mp_SigBit);
                    myTask->tc_SigRecvd |= myFrame->mf_Signals;
                    myFrame->mf_MirrorPort = mirrorPort;

                    Run68KCode(SysBase, &myFrame->mf_PPCArgs);

                    /* not cleared: the copy below overwrites all 192 bytes (G1 D) */
                    struct MsgFrame* doneFrame = CreateMsgFrameNoClear(PowerPCBase);

                    CopyMemQuick((APTR)myFrame, (APTR)doneFrame, sizeof(struct MsgFrame));

                    doneFrame->mf_Identifier = ID_DONE;

                    /* atomic fetch-and-clear -- FIXES.md item 20, the same race
                    * as functions68k.c's ID_T68K handler, which carries the
                    * reasoning. A signal arriving between the capture and the
                    * clear was discarded by the xor. */
                    doneFrame->mf_Signals    = SetSignal(0L, andTemp) & andTemp;
                    doneFrame->mf_Arg[0]     = (ULONG)myTask;
                    doneFrame->mf_Arg[1]     = myTask->tc_SigAlloc;

                    SendMsgFrame(PowerPCBase, doneFrame);
                    FreeMsgFrame(PowerPCBase, myFrame);
                }
                else if (myFrame->mf_Identifier == ID_END)
                {
                    FreeMsgFrame(PowerPCBase, myFrame);

                    /* take the entry registered above back out, so the list
                    * holds only live mirrors. The port is left alone: this
                    * function never deleted it, and changing that is a separate
                    * question from registering the pairing. */
                    if (myMirror)
                    {
                        Disable();
                        Remove((struct Node*)myMirror);
                        Enable();
                        FreeVec(myMirror);
                    }
                    ((struct Process*)myTask)->pr_CurrentDir = startDir;
                    return;
                }
                else
                {
                    FreeMsgFrame(PowerPCBase, myFrame);
                }
            }
        }
        else if (mySignal & andTemp)
        {
            struct MsgFrame* crossFrame = CreateMsgFrame(PowerPCBase);
            crossFrame->mf_Identifier   = ID_LLPP;
            crossFrame->mf_Signals      = (mySignal & andTemp);
            crossFrame->mf_Arg[0]       = (ULONG)myTask;
            SendMsgFrame(PowerPCBase, crossFrame);
        }
    }
}

void powerpc_proc()
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    struct Library *DOSBase = OpenLibrary("dos.library", 0);
    struct Process *self = (struct Process *)FindTask(NULL);
    struct MFShared *shared;
    struct XBoot *boot;
    struct MsgPort *masterPort = CreateMsgPort();

    bug("[PPC.m68k] powerpc.library process\n");

    WaitPort(&self->pr_MsgPort);

    /* Get startupt message with PPCBase */
    struct StartupMessage *msg = (struct StartupMessage *)GetMsg(&self->pr_MsgPort);
    struct PrivatePPCBase * PowerPCBase = msg->PPCBase;

    ReplyMsg((struct Message *)msg);

    PowerPCBase->pp_MasterControl = masterPort;
    PowerPCBase->pp_iFrame = AllocMem(sizeof(struct iframe), MEMF_PUBLIC);

    bug("[PPC.m68k] Exception frame at %08lx\n", (ULONG)PowerPCBase->pp_iFrame);

    /*
        Prepare storage for MsgFrame pool and rings, 64-byte aligned
    */
    shared = AllocMem(sizeof(struct MFShared) + 64, MEMF_PUBLIC | MEMF_CLEAR);
    shared = (struct MFShared *)(((ULONG)shared + 63) & ~63);
    
    bug("[PPC.m68k] Message rings at %08lx\n", (ULONG)shared);

    mf_shared_init(shared);

    boot = AllocVec(sizeof(struct XBoot), MEMF_PUBLIC | MEMF_CLEAR);
    boot->magic = XBOOT_MAGIC;
    boot->git_hash = GIT_HASH;
    boot->ppcbase = (uint32_t)PowerPCBase;
    boot->shared = (uint32_t)shared;
    boot->status = XBOOT_PENDING;
    
    boot->ppc_pool = (uint32_t)AllocVec(MF_POOL_FRAMES * sizeof(struct MsgFrame), MEMF_PUBLIC | MEMF_CLEAR);
    boot->ppc_frames = MF_POOL_FRAMES;

    bug("[PPC.m68k] Boot message for PPC kernel at %08lx\n", (ULONG)boot);

    mf_side_init(&PowerPCBase->pp_MFSelf, shared, (struct MsgFrame *)AllocVec(MF_POOL_FRAMES * sizeof(struct MsgFrame), MEMF_PUBLIC | MEMF_CLEAR), MF_POOL_FRAMES);

    /*
        Initialize PowerPC side now, based on the information we know:
        - PPC ROM is at its high prefix address 0xfff00000
        - PPC has started on boot from RESET vector at 0xfff00100
        - PPC is spinning on 0xffefff80, waiting for PPCBase at that address
        - Once PPC receives library base there, it starts its initialization,
            patches the library functions at PPCBase and calls back sending a 
            XMessage signalling the pp_WaitingTask with pp_WaitingTaskBit signal
    */

    bug("[PPC.m68k] Ringing on doorbell to wake up PPC...\n");

    asm volatile("nop; move.l %0,%1"::"r"(boot),"m"(*(volatile uint32_t *)STARTUP_DOORBELL):"memory","cc");

    /*
        Wait for a signal from interrupt
    */
    bug("[PPC.m68k] Waiting for signal from interrupt\n");

    Wait(SIGF_SINGLE);

    bug("[PPC.m68k] Interrupt received\n");

    bug("[PPC.m68k] Entering main loop\n");

    while(1)
    {
        WaitPort(masterPort);
        struct MsgFrame *msgFrame;

        while ((msgFrame = (struct MsgFrame*)GetMsg(masterPort)) != NULL)
        {
            self->pr_Task.tc_Flags |= TF_PPC;

            if (msgFrame->mf_Message.mn_Node.ln_Type == NT_REPLYMSG)
            {
                if(msgFrame->mf_Message.mn_Node.ln_Name)
                {
                    struct MsgFrame* newFrame = CreateMsgFrame(PowerPCBase);
                    newFrame->mf_Identifier = ID_XMSG;
                    newFrame->mf_Arg[0] = (ULONG)msgFrame;
                    newFrame->mf_Message.mn_ReplyPort = (struct MsgPort*)msgFrame->mf_Message.mn_Node.ln_Name;
                    msgFrame->mf_Message.mn_ReplyPort = (struct MsgPort*)msgFrame->mf_Message.mn_Node.ln_Name;
                    SendMsgFrame(PowerPCBase, newFrame);
                }
                else
                {
                    bug("[PPC.m68k] MasterControl received illegal reply message\n");
                }
            }
            else
            {
                switch (msgFrame->mf_Identifier)
                {
                    case ID_T68K:
                    {
                        char *ppcName = msgFrame->mf_PPCTask->tp_Task.tc_Node.ln_Name;
                        char nameAppend[] = "_68K\0";
                        ULONG nameLen = 0;
                        char *Name68k;
                        struct Process* myProc;

                        while (ppcName[nameLen])
                        {
                            nameLen++;
                        }

                        self->pr_Task.tc_Flags &= ~TF_PPC;

                        while (!(Name68k = AllocVec(nameLen + 5, MEMF_PUBLIC)))
                        {
                            Delay(50);
                        }

                        CopyMem(ppcName, Name68k, nameLen);
                        CopyMem(&nameAppend, &Name68k[nameLen], 5);

                        struct TagItem tags[] = {
                            NP_Entry, (ULONG)&MirrorTask,
                            NP_Name, (ULONG)Name68k,
                            NP_Priority, 0,
                            NP_StackSize, 0x20000,
                            TAG_DONE, 0UL
                        };

                        while (!(myProc = CreateNewProcTagList(tags)))
                        {
                            Delay(50);
                        }

                        struct StartupMessage message;
                        message.PPCBase = PowerPCBase;
                        message.msg.mn_Length = sizeof(struct StartupMessage);
                        message.msg.mn_ReplyPort = CreateMsgPort();
                        message.msg.mn_Node.ln_Type = NT_MESSAGE;

                        PutMsg(&myProc->pr_MsgPort, (struct Message *)&message);
                        WaitPort(message.msg.mn_ReplyPort);
                        GetMsg(message.msg.mn_ReplyPort);
                        DeleteMsgPort(message.msg.mn_ReplyPort);

                        FreeVec(Name68k);
                        self->pr_Task.tc_Flags |= TF_PPC;

                        myProc->pr_Task.tc_UserData = (APTR)msgFrame;
                        
                        myProc->pr_Task.tc_SigAlloc |= msgFrame->mf_Arg[0];
                        Signal((struct Task*)myProc, SIGBREAKF_CTRL_F);
                        break;
                    }
                    case ID_LL68:
                    {
                        ULONG (*DoLL68K_ptr)(REGARG(ULONG, "d0"), REGARG(ULONG, "d1"), REGARG(ULONG, "a0"),
                                REGARG(ULONG, "a1"), REGARG(ULONG, "a6")) =
                                (APTR)(msgFrame->mf_PPCArgs.PP_Regs[0] + msgFrame->mf_PPCArgs.PP_Regs[1]);
                        ULONG result = (*DoLL68K_ptr)(msgFrame->mf_PPCArgs.PP_Regs[4], msgFrame->mf_PPCArgs.PP_Regs[5],
                                msgFrame->mf_PPCArgs.PP_Regs[2], msgFrame->mf_PPCArgs.PP_Regs[3],
                                msgFrame->mf_PPCArgs.PP_Regs[0]);
                        struct MsgFrame* newFrame = CreateMsgFrame(PowerPCBase);
                        newFrame->mf_PPCArgs.PP_Regs[0] = result;
                        newFrame->mf_Identifier = ID_DNLL;
                        newFrame->mf_PPCTask = msgFrame->mf_PPCTask;

                        SendMsgFrame(PowerPCBase, newFrame);
                        FreeMsgFrame(PowerPCBase, msgFrame);

                        break;
                    }
                    case ID_DBGS:
                    {
                        #if 0
                        struct DebugArgs* dbargs = (struct DebugArgs*)&myFrame->mf_PPCArgs;
                        ULONG numargs = dbargs->db_Function >> 8;
                        dbargs->db_Function = FuncStrings[(dbargs->db_Function & 0xff)];
                        switch (numargs)
                        {
                            case 1:
                            {
                                mySPrintF68K((struct PPCBase*)PowerPCBase, "%s -> %s(%08lx)\n\0", (APTR)dbargs);
                                break;
                            }
                            case 2:
                            {
                                mySPrintF68K((struct PPCBase*)PowerPCBase, "%s -> %s(%08lx, %08lx)\n\0", (APTR)dbargs);
                                break;
                            }
                            case 3:
                            {
                                mySPrintF68K((struct PPCBase*)PowerPCBase, "%s -> %s(%08lx, %08lx, %08lx)\n\0", (APTR)dbargs);
                                break;
                            }
                            case 4:
                            {
                                mySPrintF68K((struct PPCBase*)PowerPCBase, "%s -> %s(%08lx, %08lx, %08lx, %08lx)\n\0", (APTR)dbargs);
                                break;
                            }
                            default:
                            {
                                mySPrintF68K((struct PPCBase*)PowerPCBase, "%s -> %s()\n\0", (APTR)dbargs);
                                break;
                            }

                        }
                        #endif
                        FreeMsgFrame(PowerPCBase, msgFrame);
                        break;
                    }
                    case ID_DBGE:
                    {
                        #if 0
                        struct DebugArgs* dbargs = (struct DebugArgs*)&myFrame->mf_PPCArgs;
                        dbargs->db_Function = FuncStrings[(dbargs->db_Function & 0xff)];
                        mySPrintF68K((struct PPCBase*)PowerPCBase, "%s -> %s() = %08lx\n\0", (APTR)dbargs);
                        #endif
                        FreeMsgFrame(PowerPCBase, msgFrame);
                        break;
                    }
                    case ID_CRSH:
                    {
                        //struct DosLibrary* DOSBase = PowerPCBase->pp_PowerPCBase.PPC_DosLib;
                        FreeMsgFrame(PowerPCBase, msgFrame);
                        #if 0
                        BPTR excWindow;
                        if (!(excWindow = Open("CON:0/20/680/250/PowerPC Exception/AUTO/CLOSE/WAIT/INACTIVE", MODE_NEWFILE)))
                        {
                            PrintError(SysBase, "PPC crashed but could not output crash window");
                            break;
                        }
                        if(!(*((ULONG*)(PowerPCBase->pp_PPCMemBase + (ULONG)(FIFO_END + 0x100)))))
                        {
                            *((ULONG*)(PowerPCBase->pp_PPCMemBase + (ULONG)(FIFO_END + 0x100))) = (ULONG)&msgPanic;
                        }
                        ULONG* errorData = (ULONG*)(PowerPCBase->pp_PPCMemBase + (ULONG)(FIFO_END + 0x100));
                        errorData[2] = ExcStrings[(errorData[2])];

                        VFPrintf(excWindow, (STRPTR)&CrashMessage, (LONG*)(PowerPCBase->pp_PPCMemBase + (ULONG)(FIFO_END + 0x100)));
    #ifdef DEBUG
                        RawDoFmt((STRPTR)&CrashMessage, (APTR)errorData, &DPutChProc, (APTR)SysBase);
    #endif
                        switch (*((ULONG*)(PowerPCBase->pp_PPCMemBase + (ULONG)(FIFO_END + 0x14c))))
                        {
                            case ERR_ESEM:
                            {
                                PrintError(SysBase, "PPC Semaphore in illegal state");
                                D(("PPC Semaphore in illegal state\n"));
                                break;
                            }
                            case ERR_EFIF:
                            {
                                PrintError(SysBase, "PPC received an illegal command packet");
                                D(("PPC received an illegal command packet\n"));
                                break;
                            }
                            case ERR_ESNC:
                            {
                                PrintError(SysBase, "Async Run68K function not supported");
                                D(("Async Run68K function not supported\n"));
                                break;
                            }
                            case ERR_EMEM:
                            {
                                PrintError(SysBase, "PPC CPU ran out of memory");
                                D(("PPC CPU ran out of memory\n"));
                                break;
                            }
                            case ERR_ECOR:
                            {
                                PrintError(SysBase, "PPC Memory corruption detected during freeing");
                                D(("PPC Memory corruption detected during freeing\n"));
                                break;
                            }
                            case ERR_ETIM:
                            {
                                PrintError(SysBase, "PPC timed out while waiting on 68K");
                                D(("PPC timed out while waiting on 68K\n"));
                                break;
                            }
                            case ERR_R68K:
                            {
                                PrintError(SysBase, "Illegal Run68K function execution detected");
                                D(("Illegal Run68K function execution detected\n"));
                                break;
                            }
                        }
                            #endif
                        break;
                    }
                    default:
                    {
                        #if 0
                        PrintError(SysBase, "68K received an illegal command packet");
                        D(("68K received an illegal command packet\n"));
                        #endif
                        break;
                    }
                }
            }
        }
    }
}

void starter_task(struct PrivatePPCBase * PPCBase)
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    struct Library *DOSBase;
    struct MsgPort *msgPort = CreateMsgPort();
    struct timerequest *tr = CreateIORequest(msgPort, sizeof(struct timerequest));

    OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)tr, 0);

    bug("[PPC.m68k] Waiting for dos.library\n");

    while(!(DOSBase = OpenLibrary("dos.library", 0)))
    {
        tr->tr_time.tv_usec = 0;
        tr->tr_time.tv_sec = 1;
        tr->tr_node.io_Command = TR_ADDREQUEST;

        DoIO((struct IORequest *)tr);
    };

    bug("[PPC.m68k] dos.library opened at %08lx\n", (ULONG)DOSBase);

    const struct TagItem tags[] = {
        { NP_Entry,     (ULONG)powerpc_proc },
        { NP_Priority,  5 },
        { NP_Name,      (ULONG)PPCBase->pp_Public.PPC_LibNode.lib_Node.ln_Name },
        { NP_WindowPtr, -1 },
        { NP_StackSize,  0x20000 },
        { TAG_DONE,     0UL },
    };

    struct StartupMessage message;
    message.PPCBase = PPCBase;
    message.msg.mn_Length = sizeof(struct StartupMessage);
    message.msg.mn_ReplyPort = CreateMsgPort();
    message.msg.mn_Node.ln_Type = NT_MESSAGE;

    PPCBase->pp_MasterProcess = CreateNewProc(tags);
    PutMsg(&PPCBase->pp_MasterProcess->pr_MsgPort, (struct Message *)&message);
    WaitPort(message.msg.mn_ReplyPort);
    DeleteMsgPort(message.msg.mn_ReplyPort);

    CloseLibrary(DOSBase);
    CloseDevice((struct IORequest *)tr);
    DeleteIORequest(tr);
    DeleteMsgPort(msgPort);

    bug("[PPC.m68k] powerpc.library process initialized, starter task may die now\n");
}

void start_powerpc_proc(struct PrivatePPCBase * PPCBase)
{
    bug("[PPC.m68k] Starting powerpc.library process from task\n");

    NewCreateTask(
        TASKTAG_PC,    (ULONG)starter_task ,
        TASKTAG_NAME,  (ULONG)"powerpc.library delayed startup" ,
        TASKTAG_PRI,   50 ,
        TASKTAG_STACKSIZE, 8192 ,
        TASKTAG_ARG1, (ULONG)PPCBase ,
        TAG_DONE,      0
    );
}
