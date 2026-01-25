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

struct StartupMessage {
    struct Message msg;
    struct PrivatePPCBase *PPCBase;
};

void powerpc_proc()
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    struct Library *DOSBase = OpenLibrary("dos.library", 0);
    struct Process *self = (struct Process *)FindTask(NULL);

    bug("[PPC.m68k] powerpc.library process\n");

    /* Get startupt message with PPCBase */
    struct StartupMessage *msg = (struct StartupMessage *)GetMsg(&self->pr_MsgPort);
    struct PrivatePPCBase * PPCBase = msg->PPCBase;

    ReplyMsg((struct Message *)msg);

    PPCBase->pp_WaitingTask = SysBase->ThisTask;
    PPCBase->pp_WaitingTaskBit = SIGB_SINGLE;

    PPCBase->pp_iFrame = AllocMem(sizeof(struct iframe), MEMF_PUBLIC);

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

    *(volatile ULONG *)0xffefff80 = (ULONG)PPCBase;

    /*
        Wait for a signal from interrupt
    */
    bug("[PPC.m68k] Waiting for signal from interrupt\n");

    Wait(SIGF_SINGLE);

    bug("[PPC.m68k] Interrupt received\n");

    while(1) {
        Wait(SIGF_SINGLE);
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
        { TAG_DONE,     0UL },
    };

    struct StartupMessage message;
    message.PPCBase = PPCBase;
    message.msg.mn_Length = sizeof(struct StartupMessage);
    message.msg.mn_ReplyPort = CreateMsgPort();
    message.msg.mn_Node.ln_Type = NT_MESSAGE;

    PPCBase->pp_PPCProcess = CreateNewProc(tags);
    PutMsg(&PPCBase->pp_PPCProcess->pr_MsgPort, (struct Message *)&message);
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
