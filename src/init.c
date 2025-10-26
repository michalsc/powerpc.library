#include <exec/types.h>
#include <exec/exec.h>
#include <exec/libraries.h>
#include <exec/nodes.h>
#include <exec/lists.h>
#include <common/compiler.h>
#include <powerpc/powerpc.h>

#include <hardware/intbits.h>

#include <proto/exec.h>
#include <proto/expansion.h>
#include <proto/devicetree.h>

#include "libstructs.h"
#include "powerpc.h"
#include "powerpc_private.h"

#define RESERVED_PPC    (APTR)0xcafebabe

static void putch(REGARG(UBYTE data, "d0"), REGARG(APTR ignore, "a3"))
{
    (void)ignore;
    *(UBYTE*)0xdeadbeef = data;
}

void kprintf(REGARG(const char * msg, "a0"), REGARG(void * args, "a1")) 
{
    struct ExecBase *SysBase = *(struct ExecBase **)4UL;
    RawDoFmt(msg, args, (APTR)putch, NULL);
}

ULONG L_Reserved()
{
    return 0;
}

struct Library * L_Open(REGARG(struct PPCBase * PowerPCBase, "a6"))
{
    if (PowerPCBase)
    {
        struct ExecBase *SysBase = PowerPCBase->PPC_SysLib;
        struct Task *thisTask = SysBase->ThisTask;
        
        thisTask->tc_Flags |= TF_PPC;

        PowerPCBase->PPC_LibNode.lib_OpenCnt++;
        PowerPCBase->PPC_LibNode.lib_Flags &= ~LIBF_DELEXP;
    }

    return &PowerPCBase->PPC_LibNode;
}

BPTR L_Expunge(REGARG(struct PPCBase * PowerPCBase, "a6"))
{
    /* We cannot expunge, delete the DELEXP flag and return 0 */
    PowerPCBase->PPC_LibNode.lib_Flags &= ~LIBF_DELEXP;
    return 0;
}

BPTR L_Close(REGARG(struct PPCBase * PowerPCBase, "a6"))
{
    if (PowerPCBase->PPC_LibNode.lib_OpenCnt) {
        PowerPCBase->PPC_LibNode.lib_OpenCnt--;
    }

    if (PowerPCBase->PPC_LibNode.lib_OpenCnt == 0) {
        if (PowerPCBase->PPC_LibNode.lib_Flags & LIBF_DELEXP) {
            return L_Expunge(PowerPCBase);
        }
    }

    return 0;
}

ULONG L_ExtFunc()
{
    return 0;
}

APTR L_AllocVec32(REGARG(ULONG bytesize, "d0"), REGARG(ULONG attributes, "d1"), REGARG(struct PrivatePPCBase * PPCBase, "a6"))
{
    struct ExecBase *SysBase;
    APTR buffer, alignedbuffer;

    if (PPCBase) SysBase = PPCBase->pp_Public.PPC_SysLib;
    else SysBase = *(struct ExecBase**)4UL;

    bytesize += 0x38;

    buffer = AllocVec(bytesize, attributes);

    if (buffer) {
        alignedbuffer = (APTR)(((ULONG)buffer + 31 + 8) & ~31);
        *(APTR*)((ULONG)alignedbuffer - 4) = buffer;
        return alignedbuffer;
    }

    return NULL;
}

void L_FreeVec32(REGARG(APTR buffer, "a1"), REGARG(struct PrivatePPCBase *PPCBase, "a6"))
{
    struct ExecBase *SysBase;

    if (PPCBase) SysBase = PPCBase->pp_Public.PPC_SysLib;
    else SysBase = *(struct ExecBase**)4UL;

    APTR original = *(APTR*)((ULONG)buffer - 4);
    
    FreeVec(original);
}

struct Message *L_AllocXMsg(REGARG(ULONG bodysize, "d0"), REGARG(struct MsgPort * replyport, "a0"), REGARG(struct PrivatePPCBase * PPCBase, "a6"))
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    struct Message *msg;

    if (bodysize > 65535 - sizeof(struct Node)) bodysize = 65535 - sizeof(struct Node);

    bodysize = (bodysize + 31) & ~31;

    msg = AllocVec(bodysize + sizeof(struct Node), MEMF_PUBLIC | MEMF_CLEAR);

    if (msg) {
        msg->mn_Length = bodysize;
        msg->mn_ReplyPort = replyport;
    }

    return msg;
}

void L_FreeXMsg(REGARG(struct Message * msg, "a0"), REGARG(struct PrivatePPCBase *PPCBase, "a6"))
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    FreeVec(msg);
}

ULONG L_GetCPU()
{
    return CPUF_G3;
}

static const APTR relFuncTable[TOTAL_FUNCS + 1] =
{
    // Library part
    L_Open,
    L_Close,
    L_Expunge,
    L_ExtFunc,

    // M68k functions
    L_Reserved,     // (APTR)myRunPPC,
    L_Reserved,     // (APTR)myWaitForPPC,
    L_GetCPU,
    L_Reserved,     // (APTR)myPowerDebugMode,
    L_AllocVec32,
    L_FreeVec32,
    L_Reserved,     // (APTR)mySPrintF68K,
    L_AllocXMsg,
    L_FreeXMsg,
    L_Reserved,     // (APTR)myPutXMsg,
    L_Reserved,     // (APTR)myGetPPCState,
    L_Reserved,     // (APTR)mySetCache68K,
    L_Reserved,     // (APTR)myCreatePPCTask,
    L_Reserved,     // (APTR)myCausePPCInterrupt,

    // Reserved M68k functions
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,
    L_Reserved,     // (APTR)myReserved,

    // PPC Functions, will be patched by PPC side on init
    RESERVED_PPC,   // (APTR)myRun68K,
    RESERVED_PPC,   // (APTR)myWaitFor68K,
    RESERVED_PPC,   // (APTR)mySPrintF,
    RESERVED_PPC,   // (APTR)myRun68KLowLevel,
    RESERVED_PPC,   // (APTR)myAllocVecPPC,
    RESERVED_PPC,   // (APTR)myFreeVecPPC,
    RESERVED_PPC,   // (APTR)myCreateTaskPPC,
    RESERVED_PPC,   // (APTR)myDeleteTaskPPC,
    RESERVED_PPC,   // (APTR)myFindTaskPPC,
    RESERVED_PPC,   // (APTR)myInitSemaphorePPC,
    RESERVED_PPC,   // (APTR)myFreeSemaphorePPC,
    RESERVED_PPC,   // (APTR)myAddSemaphorePPC,
    RESERVED_PPC,   // (APTR)myRemSemaphorePPC,
    RESERVED_PPC,   // (APTR)myObtainSemaphorePPC,
    RESERVED_PPC,   // (APTR)myAttemptSemaphorePPC,
    RESERVED_PPC,   // (APTR)myReleaseSemaphorePPC,
    RESERVED_PPC,   // (APTR)myFindSemaphorePPC,
    RESERVED_PPC,   // (APTR)myInsertPPC,
    RESERVED_PPC,   // (APTR)myAddHeadPPC,
    RESERVED_PPC,   // (APTR)myAddTailPPC,
    RESERVED_PPC,   // (APTR)myRemovePPC,
    RESERVED_PPC,   // (APTR)myRemHeadPPC,
    RESERVED_PPC,   // (APTR)myRemTailPPC,
    RESERVED_PPC,   // (APTR)myEnqueuePPC,
    RESERVED_PPC,   // (APTR)myFindNamePPC,
    RESERVED_PPC,   // (APTR)myFindTagItemPPC,
    RESERVED_PPC,   // (APTR)myGetTagDataPPC,
    RESERVED_PPC,   // (APTR)myNextTagItemPPC,
    RESERVED_PPC,   // (APTR)myAllocSignalPPC,
    RESERVED_PPC,   // (APTR)myFreeSignalPPC,
    RESERVED_PPC,   // (APTR)mySetSignalPPC,
    RESERVED_PPC,   // (APTR)mySignalPPC,
    RESERVED_PPC,   // (APTR)myWaitPPC,
    RESERVED_PPC,   // (APTR)mySetTaskPriPPC,
    RESERVED_PPC,   // (APTR)mySignal68K,
    RESERVED_PPC,   // (APTR)mySetCache,
    RESERVED_PPC,   // (APTR)mySetExcHandler,
    RESERVED_PPC,   // (APTR)myRemExcHandler,
    RESERVED_PPC,   // (APTR)mySuper,
    RESERVED_PPC,   // (APTR)myUser,
    RESERVED_PPC,   // (APTR)mySetHardware,
    RESERVED_PPC,   // (APTR)myModifyFPExc,
    RESERVED_PPC,   // (APTR)myWaitTime,
    RESERVED_PPC,   // (APTR)myChangeStack,
    RESERVED_PPC,   // (APTR)myLockTaskList,
    RESERVED_PPC,   // (APTR)myUnLockTaskList,
    RESERVED_PPC,   // (APTR)mySetExcMMU,
    RESERVED_PPC,   // (APTR)myClearExcMMU,
    RESERVED_PPC,   // (APTR)myChangeMMU,
    RESERVED_PPC,   // (APTR)myGetInfo,
    RESERVED_PPC,   // (APTR)myCreateMsgPortPPC,
    RESERVED_PPC,   // (APTR)myDeleteMsgPortPPC,
    RESERVED_PPC,   // (APTR)myAddPortPPC,
    RESERVED_PPC,   // (APTR)myRemPortPPC,
    RESERVED_PPC,   // (APTR)myFindPortPPC,
    RESERVED_PPC,   // (APTR)myWaitPortPPC,
    RESERVED_PPC,   // (APTR)myPutMsgPPC,
    RESERVED_PPC,   // (APTR)myGetMsgPPC,
    RESERVED_PPC,   // (APTR)myReplyMsgPPC,
    RESERVED_PPC,   // (APTR)myFreeAllMem,
    RESERVED_PPC,   // (APTR)myCopyMemPPC,
    RESERVED_PPC,   // (APTR)myAllocXMsgPPC,
    RESERVED_PPC,   // (APTR)myFreeXMsgPPC,
    RESERVED_PPC,   // (APTR)myPutXMsgPPC,
    RESERVED_PPC,   // (APTR)myGetSysTimePPC,
    RESERVED_PPC,   // (APTR)myAddTimePPC,
    RESERVED_PPC,   // (APTR)mySubTimePPC,
    RESERVED_PPC,   // (APTR)myCmpTimePPC,
    RESERVED_PPC,   // (APTR)mySetReplyPortPPC,
    RESERVED_PPC,   // (APTR)mySnoopTask,
    RESERVED_PPC,   // (APTR)myEndSnoopTask,
    RESERVED_PPC,   // (APTR)myGetHALInfo,
    RESERVED_PPC,   // (APTR)mySetScheduling,
    RESERVED_PPC,   // (APTR)myFindTaskByID,
    RESERVED_PPC,   // (APTR)mySetNiceValue,
    RESERVED_PPC,   // (APTR)myTrySemaphorePPC,
    RESERVED_PPC,   // (APTR)myAllocPrivateMem,
    RESERVED_PPC,   // (APTR)myFreePrivateMem,
    RESERVED_PPC,   // (APTR)myResetPPC,
    RESERVED_PPC,   // (APTR)myNewListPPC,
    RESERVED_PPC,   // (APTR)mySetExceptPPC,
    RESERVED_PPC,   // (APTR)myObtainSemaphoreSharedPPC,
    RESERVED_PPC,   // (APTR)myAttemptSemaphoreSharedPPC,
    RESERVED_PPC,   // (APTR)myProcurePPC,
    RESERVED_PPC,   // (APTR)myVacatePPC,
    RESERVED_PPC,   // (APTR)myCauseInterrupt,
    RESERVED_PPC,   // (APTR)myCreatePoolPPC,
    RESERVED_PPC,   // (APTR)myDeletePoolPPC,
    RESERVED_PPC,   // (APTR)myAllocPooledPPC,
    RESERVED_PPC,   // (APTR)myFreePooledPPC,
    RESERVED_PPC,   // (APTR)myRawDoFmtPPC,
    RESERVED_PPC,   // (APTR)myPutPublicMsgPPC,
    RESERVED_PPC,   // (APTR)myAddUniquePortPPC,
    RESERVED_PPC,   // (APTR)myAddUniqueSemaphorePPC,
    RESERVED_PPC,   // (APTR)myIsExceptionMode,

    RESERVED_PPC,   // (APTR)SystemStart,           //PRIVATE
    RESERVED_PPC,   // (APTR)StartTask,             //PRIVATE Should not be jumped to, just a holder for the address
    RESERVED_PPC,   // (APTR)EndTask,               //PRIVATE

    (APTR)-1
};

extern const char deviceName[];
extern const char deviceIdString[];

extern ULONG interruptPPC();
asm(".text\n_interruptPPC: move.l d0,-(a7);movec #0x1e0, d0; ori.l #0x80000000, d0; movec d0, #0x1e0; move.l (a7)+,d0; rte ");

void SendPacketMessage(struct PrivatePPCBase * PPCBase, APTR message)
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    ULONG reg;

    /* Send message */
    doorbell_send(&PPCBase->M68k_to_PPC, STATUS_MSG);
    
    /* Fire interrupt */
    Supervisor(&interruptPPC);

    /* Wait for ACK */
    while(doorbell_wait(&PPCBase->PPC_to_M68k) != STATUS_ACK);

    /* Send message address */
    doorbell_send(&PPCBase->M68k_to_PPC, (ULONG)message);

    /* Wait for ACK */
    while(doorbell_wait(&PPCBase->PPC_to_M68k) != STATUS_ACK);
}

APTR StartRecievingMessage(struct PrivatePPCBase * PPCBase)
{
    while(doorbell_wait(&PPCBase->PPC_to_M68k) != STATUS_MSG);

    doorbell_send(&PPCBase->M68k_to_PPC, STATUS_ACK);

    return (APTR)doorbell_wait(&PPCBase->PPC_to_M68k);
}

void EndReceivingMessage(struct PrivatePPCBase * PPCBase)
{
    doorbell_send(&PPCBase->M68k_to_PPC, STATUS_ACK);
}

int OnInterrupt(REGARG(struct PrivatePPCBase *PPCBase, "a1"))
{
    struct ExecBase *SysBase = PPCBase->pp_Public.PPC_SysLib;
    ULONG tmp;

    asm volatile("movec #0x1e0, %0":"=r"(tmp));
    
    if (tmp & 0x40000000)
    {
        struct XMessage *m = StartRecievingMessage(PPCBase);

        switch(m->id) {
            case XMSG_SIGNAL_TASK:
                bug("[PPC.m68k] Signalling task %lx with sigset %lx\n", (ULONG)m->SignalTask.task, m->SignalTask.sigset);
                Signal(m->SignalTask.task, m->SignalTask.sigset);
                break;
            default:
                bug("[PPC.m68k] no idea what to do with XMessage %ld\n", m->id);
                break;
        }

        EndReceivingMessage(PPCBase);

        /* Clear the bits used to signal interrupts */
        tmp &= 0x1fffffff;

        /* ACK the interrupt from PPC */
        asm volatile("movec %0, #0x1e0"::"r"(tmp | 0x40000000));
        return 1;
    }
    return 0;
}

extern void trampoline();
asm(".text\n_trampoline: bsr.w _OnInterrupt\n\tcmp #0, d0\n\trts");

APTR Init(REGARG(struct ExecBase *SysBase, "a6"))
{
    struct PrivatePPCBase *PPCBase = NULL;
    struct ExpansionBase *ExpansionBase = NULL;
    struct CurrentBinding binding;
    APTR DeviceTreeBase;

    APTR base_pointer = NULL;
    
    /* Try to open devicetree.resource - if it fails, there is definitely no Emu68 */
    DeviceTreeBase = OpenResource("devicetree.resource");
    if (DeviceTreeBase == NULL) return NULL;

    bug("[PPC.m68k] powerpc.library init\n");

    /*  
        Open emu68 key, then find version property of it. If not found then either 
        emu68 key does not exist (impossible), or version property does not exist
        (too old emu68).
    */
    APTR emu68 = DT_OpenKey("/emu68");
    ULONG *emu68_ver = (ULONG*)DT_GetPropValue(DT_FindProperty(emu68, "version"));

    /* Property not found for reasons. Cannot continue now. */
    if (emu68_ver == NULL) return NULL;

    bug("[PPC.m68k] emu68 version: %ld.%ld.%ld\n", emu68_ver[0], emu68_ver[1], emu68_ver[2]);

    #if 0
    /* Order of check is important. For Emu68 versions < 1.0.9999 there is no support! */
    if (emu68_ver[0] <= 1 && emu68_ver[1] <= 1 && emu68_ver[2] < 999) {
        return NULL;
    }
    #endif

    ExpansionBase = (struct ExpansionBase *)OpenLibrary("expansion.library", 0);
    GetCurrentBinding(&binding, sizeof(binding));

    base_pointer = AllocMem(LIB_NEGSIZE + LIB_POSSIZE, MEMF_PUBLIC | MEMF_CLEAR);

    bug("[PPC.m68k] base pointer %08lx\n", (ULONG)base_pointer);

    if (base_pointer)
    {
        PPCBase = (struct PrivatePPCBase *)((UBYTE *)base_pointer + LIB_NEGSIZE);
        MakeFunctions(PPCBase, (APTR)relFuncTable, 0);
        
        PPCBase->pp_Public.PPC_LibNode.lib_Node.ln_Type = NT_LIBRARY;
        PPCBase->pp_Public.PPC_LibNode.lib_Node.ln_Pri = PPC_PRIORITY;
        PPCBase->pp_Public.PPC_LibNode.lib_Node.ln_Name = (STRPTR)deviceName;

        PPCBase->pp_Public.PPC_LibNode.lib_NegSize = LIB_NEGSIZE;
        PPCBase->pp_Public.PPC_LibNode.lib_PosSize = LIB_POSSIZE;
        PPCBase->pp_Public.PPC_LibNode.lib_Version = PPC_VERSION;
        PPCBase->pp_Public.PPC_LibNode.lib_Revision = PPC_REVISION;
        PPCBase->pp_Public.PPC_LibNode.lib_IdString = (STRPTR)deviceIdString;

        PPCBase->pp_Public.PPC_SysLib = SysBase;

        struct Interrupt * inter = AllocMem(sizeof(struct Interrupt), MEMF_CLEAR);
        inter->is_Data = PPCBase;
        inter->is_Code = trampoline;
        inter->is_Node.ln_Pri = 127;
        inter->is_Node.ln_Name = "PowerPC Interrupt";

        AddIntServer(INTB_PORTS, inter);

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

        /*
            Here, since the powerpc.library can come from Emu68 rom, the DOS might not be initialized yet.
            In that case we will need to postpone this process for later
        */
        
        PPCBase->pp_Public.PPC_DosLib = OpenLibrary("dos.library", 0);

        if (PPCBase->pp_Public.PPC_DosLib == NULL) {
            bug("[PPC.m68k] DOS library not ready yet, will finalize the setup later\n");
        }

        struct XMessage msg_cause;
        msg_cause.id = XMSG_CAUSE;

        //SendPacketMessage(PPCBase, &msg_cause);

        /*
            powerpc.library on Emu68 is in rom, SegList = 0 in that case
        */
        PPCBase->pp_Public.PPC_SegList = 0;

        SumLibrary((struct Library *)PPCBase);
        AddLibrary((struct Library *)PPCBase);
    }
}
