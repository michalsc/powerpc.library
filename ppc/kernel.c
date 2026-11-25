

// Copyright (c) 2019-2021 Dennis van der Boon
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma pack(push,2)
#include <powerpc/powerpc.h>
#include <powerpc/powerpc_protos.h>
#include "../src/powerpc.h"
#pragma pack(pop)

#include "version.h"
#include "../src/libstructs.h"
#include "support.h"
#include "kernel.h"
#include "../src/msgframe.h"
#include "../src/constants.h"

static inline uint32_t lwarx(volatile uint32_t *p)
{
    uint32_t v;
    __asm__ __volatile__("lwarx %0,0,%1" : "=r"(v) : "r"(p) : "memory");
    return v;
}

struct XBoot *WaitForXBoot()
{
    volatile uint32_t *mbox = (volatile uint32_t *)STARTUP_DOORBELL;
    union {
        struct XBoot *boot;
        uint32_t u32;
    } u;
    
    do {
        while ((u.u32 = lwarx(mbox)) == 0)
            __asm__ __volatile__(".long 0x7c20007c" ::: "memory");   /* waitrsv translating to wfe */

        __atomic_thread_fence(__ATOMIC_ACQUIRE);                     /* lwsync: XBoot contents after the pointer */
        *mbox = 0;                                                   /* consumed */
    } while(u.boot == NULL || u.boot->magic != XBOOT_MAGIC || u.boot->git_hash != GIT_HASH);

    return u.boot;
}

void initSema(struct PrivatePPCBase* PowerPCBase, struct SignalSemaphorePPC* SemaphorePPC)
{
    NewListPPC((struct List*)&SemaphorePPC->ssppc_SS.ss_WaitQueue);
    struct LibSema* libSema = (struct LibSema*)SemaphorePPC;
	SemaphorePPC->ssppc_SS.ss_Owner = 0;
	SemaphorePPC->ssppc_SS.ss_NestCount = 0;
	SemaphorePPC->ssppc_SS.ss_QueueCount = -1;
    SemaphorePPC->ssppc_reserved = (APTR)&libSema->ls_Reserved;
                                             
    return;
}

void LibBaseSetup(struct PrivatePPCBase *PowerPCBase)
{
    NewListPPC((struct List*)&PowerPCBase->pp_WaitingTasks);
    NewListPPC((struct List*)&PowerPCBase->pp_AllTasks);
    NewListPPC((struct List*)&PowerPCBase->pp_Snoop);
    NewListPPC((struct List*)&PowerPCBase->pp_Semaphores);
    NewListPPC((struct List*)&PowerPCBase->pp_RemovedExc);
    NewListPPC((struct List*)&PowerPCBase->pp_ReadyExc);
    NewListPPC((struct List*)&PowerPCBase->pp_InstalledExc);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcInterrupt);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcIABR);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcPerfMon);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcTrace);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcSystemCall);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcDecrementer);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcFPUn);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcProgram);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcAlign);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcIAccess);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcDAccess);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcMCheck);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcSysMan);
    NewListPPC((struct List*)&PowerPCBase->pp_ExcTherMan);
    NewListPPC((struct List*)&PowerPCBase->pp_WaitTime);
    NewListPPC((struct List*)&PowerPCBase->pp_Ports);
    NewListPPC((struct List*)&PowerPCBase->pp_NewTasks);
    NewListPPC((struct List*)&PowerPCBase->pp_ReadyTasks);
    NewListPPC((struct List*)&PowerPCBase->pp_RemovedTasks);
    NewListPPC((struct List*)&PowerPCBase->pp_TaskLinks);           //a handler's SignalPPC, for HandleDeferred
    NewListPPC((struct List*)&PowerPCBase->pp_MsgQueue);

    initSema(PowerPCBase, (struct SignalSemaphorePPC*)&PowerPCBase->pp_SemWaitList);
    initSema(PowerPCBase, (struct SignalSemaphorePPC*)&PowerPCBase->pp_SemTaskList);
    initSema(PowerPCBase, (struct SignalSemaphorePPC*)&PowerPCBase->pp_SemSemList);
    initSema(PowerPCBase, (struct SignalSemaphorePPC*)&PowerPCBase->pp_SemPortList);
    initSema(PowerPCBase, (struct SignalSemaphorePPC*)&PowerPCBase->pp_SemSnoopList);
    initSema(PowerPCBase, (struct SignalSemaphorePPC*)&PowerPCBase->pp_SemMemory);

    PowerPCBase->pp_IdUsrTasks = 100;
    PowerPCBase->pp_BusyCounter = 24;

    PowerPCBase->pp_LowActivityPri = PRI_MAXDIST;
    PowerPCBase->pp_CPUInfo = getPVR();

    ULONG busclock = getCNTFRQ();
    ULONG quantum = busclock / 200;   /* 5ms */
    
    PowerPCBase->pp_LowActivityPriOffset = 0;

    PowerPCBase->pp_StdQuantum = quantum;
    PowerPCBase->pp_BusClock = busclock;

    setDEC(PowerPCBase->pp_StdQuantum);
}

void Start()
{
    /* We start in C as soon as possible. Reset MSR to known state first */
    uint32_t msr = getMSR();
    setMSR((msr & MSR_IP) | MSR_EE);

    /* Reset SPRG0..3 */
    asm volatile("mtsprg0 %0; mtsprg1 %0; mtsprg2 %0; mtsprg3 %0"::"r"(0));

    /* Nothing can be really done here until the PowerPC base is set up by m68k part */
    kprintf("[PPC] Booting Emu68-PowerPC Kernel\n");

    kprintf("[PPC] Waiting for powerpc.library to come up\n");

    volatile struct XBoot *boot = WaitForXBoot();
    
    kprintf("[PPC] Boot message received: %08x\n", boot);

    kprintf("[PPC] PowerPCBase = %08x\n", boot->ppcbase);

    struct PrivatePPCBase *PowerPCBase = (struct PrivatePPCBase *)boot->ppcbase;
    struct MFShared *shared = (struct MFShared *)boot->shared;

    setBASE(PowerPCBase);

    PatchLVOTable(&PowerPCBase->pp_Public);

    mf_side_init(&PowerPCBase->pp_MFSelf, shared, (struct MsgFrame *)boot->ppc_pool, boot->ppc_frames);

    LibBaseSetup(PowerPCBase);

    kprintf("[PPC] Ringing m68k back\n");

    boot->status = XBOOT_OK;

    xring_kick_m68k();

    msr = getMSR();
    setMSR(msr | MSR_EE);

    struct MsgFrame *msg = KCreateMsgFramePPC(PowerPCBase);
    msg->mf_Identifier = ID_SIG;
    msg->mf_Arg[0] = (ULONG)PowerPCBase->pp_MasterProcess;
    msg->mf_Signals = 1 << SIGB_SINGLE;

    KSendMsgFramePPC(PowerPCBase, msg);

    kprintf("[PPC] Packet sent\n");

    while(1) { asm volatile(".long 0x7c20007c" ::: "memory"); }   /* waitrsv translating to wfe */;
}

void Exception_Entry(struct PrivatePPCBase * PowerPCBase, struct iframe *iframe)
{
    PowerPCBase->pp_ExceptionMode = -1;

    /* the task running at entry, so that the exit of an external interrupt can tell whether one was switched in (T1); only the pointer is read */
    //struct TaskPPC* taskAtEntry = PowerPCBase->pp_ThisPPCProc;

    PowerPCBase->pp_Quantum = PowerPCBase->pp_StdQuantum;

    /* Get the vector we are in, recaltulate the fields to match what's expected */
    ULONG ExceptionVector = iframe->if_Context.ec_ExcID & 0xfff0;
    iframe->if_ExcNum = ExceptionVector >> 8;
    iframe->if_Context.ec_ExcID = 1 << iframe->if_ExcNum;

//    kprintf("[PPC] ExceptionEntry if_ExcNum=%x, excid=%d\n", iframe->if_ExcNum, iframe->if_Context.ec_ExcID);

//asm volatile("\n1: b 1b");

    switch(ExceptionVector)
    {
        case VEC_EXTERNAL:
        {
            #if 0
            CPUStats(PowerPCBase);

            ULONG currAddress = iframe->if_Context.ec_UPC.ec_SRR0;
            if ((PowerPCBase->pp_LowerLimit <= currAddress) && (currAddress < PowerPCBase->pp_UpperLimit))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            if ((PowerPCBase->pp_Mutex) || (PowerPCBase->pp_FlagForbid))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            struct TaskPPC* currTask = PowerPCBase->pp_ThisPPCProc;

            if (currTask)
            {
                if (currTask->tp_Task.tc_State == TS_ATOMIC)            //never true: no tc_State write in lib/ stores TS_ATOMIC; a running atomic task is kept in SwitchPPC, as WarpOS (R3)
                {
                    break;
                }
            }

            HandleDeferred(PowerPCBase);    //what a handler left: FIXES.md "#1 PART B DESIGNED"
            HandleMsgs(PowerPCBase);
            TaskCheck(PowerPCBase);
            SwitchPPC(PowerPCBase, iframe);
            break;
            #endif
        }

        case VEC_PROGRAM:
        {
            #if 0
/*
             * SUPER() IS KNOWN BY ITS ADDRESS: the one mfpvr at SuperLocation
             * (supportppc.s), as WarpOS's Exc7 compares SRR0 with SuperLocation
             * (warp.p:4588-4602) and 17.13b's .Private with ViolationAddress
             * (setupppc.p:6302-6317). The address comes from the base, where
             * libinit.c stored it: this hunk cannot take it itself. r0 = 0 is
             * the status SuperTrap returns through "mr r3,r0". Any other
             * program exception, whatever r4 holds, goes to the handlers. It
             * used to test GPR[4] == SUPERKEY. FIXES.md "ITEM 3, Super()",
             * 31 Oct 2026.
             */
            if ((PowerPCBase->pp_SuperLocation) && (iframe->if_Context.ec_UPC.ec_SRR0 == PowerPCBase->pp_SuperLocation))
            {
                iframe->if_Context.ec_UPC.ec_SRR0  += 4;
                iframe->if_Context.ec_SRR1         &= ~PSL_PR;
                iframe->if_Context.ec_GPR[0]        = 0;            //the SuperKey, returned by SuperTrap
                //PowerPCBase->pp_Quantum = getDEC() + 1000;
            }
            else
            {
                CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcProgram);
            }
            break;
            #else
            extern void* L_Super_Addr;
            
            if (iframe->if_Context.ec_GPR[4] == SUPERKEY && iframe->if_Context.ec_UPC.ec_SRR0 == (ULONG)&L_Super_Addr) {
                iframe->if_Context.ec_UPC.ec_SRR0 += 4;
                iframe->if_Context.ec_SRR1 &= ~MSR_PR;
                iframe->if_Context.ec_GPR[3] = 0;
            }
            else {
                kprintf("[PPC] Program exception @ %08x\n", iframe->if_Context.ec_UPC.ec_SRR0, &L_Super_Addr);
                for (int i=0; i < 32; i+=4) {
                    kprintf("[PPC]    r%02d = 0x%08x   r%02d = 0x%08x   r%02d = 0x%08x   r%02d = 0x%08x\n",
                    i, iframe->if_Context.ec_GPR[i], i+1, iframe->if_Context.ec_GPR[i+1],
                    i+2, iframe->if_Context.ec_GPR[i+2], i+3, iframe->if_Context.ec_GPR[i+3]);
                }
                kprintf("[PPC]\n[PPC]     LR = 0x%08x    CR = 0x%08x   CTR = 0x%08x   XER = 0x%08x\n",
                    iframe->if_Context.ec_LR, iframe->if_Context.ec_CR, iframe->if_Context.ec_CTR, iframe->if_Context.ec_XER);
                kprintf("[PPC]  SPRG0 = 0x%08x SPRG1 = 0x%08x\n",
                    iframe->if_Context.ec_UPC.ec_SRR0, iframe->if_Context.ec_SRR1);
                while(1);
            }
            
            break;
            #endif
        }

        case VEC_DECREMENTER:
        {
            #if 0
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcDecrementer);

            /*
             * CauseInterrupt (E5): THE EXCF_INTERRUPT HANDLERS, AFTER THE
             * DECREMENTER LIST. Only the external arms above ran
             * pp_ExcInterrupt, so a handler set with EXCF_INTERRUPT was never
             * called for CauseInterrupt, which arms this decrementer. The flag is
             * cleared before the list runs, as WarpOS (powerpc.p:3949-3962) and
             * 17.13b (setupppc.p:2528-2533), and the handlers see the exception
             * ID they asked for: CommonExcHandler matches them by
             * "ed_ExcID & ec_ExcID" (below) and _SmallExcHandler hands the
             * handler that ID, so it is EXCF_INTERRUPT for the call and the
             * decrementer's again after it. WarpOS runs the same list from
             * Exception5 with XC_EXCID = EXCF_INTERRUPT (powerpc.p:6136-6168).
             * CommonExcError returns for both IDs, so a list nobody is on adds
             * no error window. CauseInterrupt waits for the flag.
             */
            if (PowerPCBase->pp_ExternalInt)
            {
                PowerPCBase->pp_ExternalInt = 0;
                iframe->if_Context.ec_ExcID = EXCF_INTERRUPT;
                CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcInterrupt);
                iframe->if_Context.ec_ExcID = EXCF_DECREMENTER;
            }

            CPUStats(PowerPCBase);

            ULONG currAddress = iframe->if_Context.ec_UPC.ec_SRR0;
            if ((PowerPCBase->pp_LowerLimit <= currAddress) && (currAddress < PowerPCBase->pp_UpperLimit))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            /*
             * T6: THE TWO MOVES BELOW COME AFTER THE WINDOW TEST ABOVE. The
             * moves are not interrupted themselves (an exception runs with
             * EE off); what was not protected is the code they interrupt.
             * LinkExcList (supportppc.c, one insert per entry of a
             * SetExcHandler) and RemExcHandler (functionsppc.c) put an entry
             * on pp_ReadyExc or pp_RemovedExc with myAddHeadPPC, in user mode
             * with EE on, under pp_Mutex. As written, myAddHeadPPC points the
             * list head at the new node BEFORE it sets the node's own links
             * (functionsppc.c). pp_Mutex is a flag and not an interrupt mask:
             * this arm tests it below, to keep a task switch away, after these
             * loops. So a decrementer that landed between those stores ran the
             * loops on a list whose head was a node with no links yet, and
             * RemHeadPPC followed them. The window above is the code between
             * the jump-table targets of InsertPPC and FindNamePPC
             * (pp_LowerLimit and pp_UpperLimit, setupppc.c), which holds
             * myAddHeadPPC: it is how this arm tells that the interrupted
             * code is inside a list function, and the loops ran ahead of it.
             * A caller outside the window has its entries moved at this tick,
             * as before; one inside it at the next, 0x1000 timebase ticks on
             * (the quantum set above). The moves stay AHEAD of the mutex and
             * Forbid test below, so that a SetExcHandler or RemExcHandler that
             * holds pp_Mutex, or runs under Forbid, still has its entries
             * moved and its wait for EXCF_ACTIVE ends.
             * 17.13b has the same order and the same exposure: its AddHeadPPC
             * stores the head first (ppcfunctions.p:598-603) and its moves
             * (setupppc.p:4316 and :4423) come before .StartQ (:2766-2780), which
             * loads the atomic flag (:2768) and the window limits (:2774, :2778);
             * a search of the whole 17.13b tree finds no other load of either,
             * and every other use of the flag is a lock or unlock call
             * (ppcfunctions.p, 90 sites, AtomicTest and AtomicDone). WarpOS
             * tests FORBID first (powerpc.p:4187-4220) and inserts under FORBID
             * (powerpc.p:11425-11428, :11480-11484). tests/excstage.
             */
            struct ExcData* eData;

            while (eData = (struct ExcData*)RemHeadPPC((struct List*)&PowerPCBase->pp_ReadyExc))
            {
                if (eData->ed_ExcID & EXCF_MCHECK)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcMCheck, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_DACCESS)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcDAccess, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_IACCESS)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcIAccess, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_INTERRUPT)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcInterrupt, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_ALIGN)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcAlign, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_PROGRAM)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcProgram, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_FPUN)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcFPUn, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_DECREMENTER)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcDecrementer, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_SC)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcSystemCall, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_TRACE)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcTrace, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_PERFMON)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcPerfMon, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_IABR)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcIABR, (struct Node*)eData);
                eData->ed_Flags |= EXCF_ACTIVE;
            }

            while (eData = (struct ExcData*)RemHeadPPC((struct List*)&PowerPCBase->pp_RemovedExc))
            {
                ULONG nodeAddr = (ULONG)&eData->ed_LastExc;
                for (int i=0; i<12; i++)
                {
                    nodeAddr += 4;
                    ULONG actNode = *((ULONG*)(nodeAddr));
                    if (actNode)
                    {
                        RemovePPC((struct Node*)actNode);
                    }
                }
                struct ExcData* lastExc = eData->ed_LastExc;
                lastExc->ed_Flags &= ~EXCF_ACTIVE;
            }

            if ((PowerPCBase->pp_Mutex) || (PowerPCBase->pp_FlagForbid))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            struct TaskPPC* currTask = PowerPCBase->pp_ThisPPCProc;

            if (currTask)
            {
                if (currTask->tp_Task.tc_State == TS_ATOMIC)            //never true: no tc_State write in lib/ stores TS_ATOMIC; a running atomic task is kept in SwitchPPC, as WarpOS (R3)
                {
                    break;
                }
            }

            HandleDeferred(PowerPCBase);    //what a handler left: FIXES.md "#1 PART B DESIGNED"
            HandleMsgs(PowerPCBase);
            TaskCheck(PowerPCBase);
            SwitchPPC(PowerPCBase, iframe);
            break;
            #else
            ULONG tbl, tbu;
            asm volatile("mftbl %0; mftbu %1":"=r"(tbl),"=r"(tbu));
            kprintf("[PPC] Decrementer, tb=%08x%08x\n", tbu, tbl);
            ULONG dec;
            asm volatile("mfspr %0, 904":"=r"(dec));
            kprintf("[PPC]   programming next decrementer to %d (1 second)\n", dec);
            asm volatile("mtdec %0"::"r"(dec));
            break;
            #endif
        }

        case VEC_SYSTEMCALL:
        {
            #if 0
            ULONG scReg = iframe->if_Context.ec_GPR[3];

            /*
             * r3 is a struct SysCall* only for OUR callers. The sc instruction
             * is not ours alone: iFUSION issues it with 'HOST'/'SERV' in r3/r4
             * and a selector in r5, and dereferencing that as a pointer faults
             * inside the exception handler, which is fatal.
             *
             * So anything that is not plainly one of ours is handed to the
             * registered handlers untouched, without being read through. The
             * magic pair is recognised explicitly because it is a known
             * caller; the null and alignment tests catch the obvious rest.
             * This is not a full validity check -- an arbitrary aligned
             * non-null value would still be dereferenced below -- but it
             * removes the case that is known to occur.
             */
            if ((scReg == 0) || (scReg & 3) ||
                ((scReg == HOSTSERV_MAGIC_R3) &&
                 (iframe->if_Context.ec_GPR[4] == HOSTSERV_MAGIC_R4)))
            {
                CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcSystemCall);
                break;
            }

            struct SysCall* sc = (struct SysCall*)scReg;

            /* the six calls are KSystemCall's, which a handler calls
             * directly (REVIEW-LIB.md K1); r3 comes back as it did: the frame
             * for CREATEMSG and GETMSG, untouched for the other four */
            switch (sc->sc_Function)
            {
                case SC_CREATEMSG:
                case SC_GETMSG:
                {
                    iframe->if_Context.ec_GPR[3] = KSystemCall(PowerPCBase, sc);
                    break;
                }
                case SC_SENDMSG:
                case SC_FREEMSG:
                case SC_SETCACHE:
                case SC_GETCPUSTATE:
                {
                    KSystemCall(PowerPCBase, sc);
                    break;
                }
                default:
                {
                    CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcSystemCall);
                    break;
                }
            }
            break;
            #else
            union {
                struct SysCall *sc;
                uint32_t u32;
            } sc;
            sc.u32 = iframe->if_Context.ec_GPR[3];
            kprintf("[PPC] SystemCall(%08x)\n", sc.u32);
            iframe->if_Context.ec_GPR[3] = KSystemCall(PowerPCBase, sc.sc);
            #endif
        }
        default:
        {
            //CommonExcError(PowerPCBase, iframe);
        }
    }

    PowerPCBase->pp_ExceptionMode = 0;

    /*
     * THE DECREMENTER IS PROGRAMMED WHERE WarpOS PROGRAMS IT AND NOWHERE ELSE:
     * at the end of the decrementer exception, after the scheduler and the
     * DEC handler list (powerpc.p:5315), and in the external interrupt, where
     * WarpOS forces a reschedule with "mtdec 0" (powerpc.p:5769, :5802) and
     * this library switches tasks itself (SwitchPPC above). Those are the
     * only two cases that switch tasks. Every other exception leaves DEC
     * counting. Until 24 Oct 2026 it was set here after ALL of them, from the
     * pp_StdQuantum Exception_Entry puts back first, so every exception
     * restarted the time slice: a task taking exceptions faster than its
     * quantum was never preempted and no timed handler ran -- iFUSION's guest
     * traps on every privileged instruction -- and a handler's own DEC write
     * was thrown away: iFUSION's mtdec emulation sets DEC to 100 at 0x01DDAC
     * to be called back. FIXES.md "STAGE 5".
     *
     * T1: the external interrupt no longer restarts the slice. A decrementer
     * exception writes the next slice as ever; an external interrupt that
     * left the same task running only SHORTENS what is left (decAfterExternal,
     * above), and writes the incoming task's slice when one was switched in.
     */
    if (ExceptionVector == VEC_DECREMENTER)
    {
        //setDEC(decShorten(PowerPCBase, PowerPCBase->pp_Quantum));
    }
    else if (ExceptionVector == VEC_EXTERNAL)
    {
        //decAfterExternal(PowerPCBase, taskAtEntry);
    }
}

ULONG KSystemCall(struct PrivatePPCBase *PowerPCBase, struct SysCall *sc)
{
    ULONG result = 0;

    switch (sc->sc_Function) {
        case SC_CREATEMSG:
        {
            return (ULONG)KCreateMsgFramePPC(PowerPCBase);
        }
        case SC_GETMSG:
        {
            return (ULONG)KGetMsgFramePPC(PowerPCBase);
        }
        case SC_FREEMSG:
        {
            KFreeMsgFramePPC(PowerPCBase, (struct MsgFrame *)sc->sc_Arg[0]);
            break;
        }
        case SC_SENDMSG:
        {
            KSendMsgFramePPC(PowerPCBase, (struct MsgFrame *)sc->sc_Arg[0]);
            break;
        }
        default:
        {
            kprintf("[PPC] Unknown system call %d\n", sc->sc_Function);
            break;
        }
    }

    return result;
}

struct MsgFrame *KCreateMsgFramePPC(struct PrivatePPCBase *PowerPCBase)
{
    return mf_create(&PowerPCBase->pp_MFSelf);
}

void KFreeMsgFramePPC(struct PrivatePPCBase* PowerPCBase, struct MsgFrame* msgFrame)
{
    mf_free(&PowerPCBase->pp_MFSelf, msgFrame);
}


struct MsgFrame* KGetMsgFramePPC(struct PrivatePPCBase* PowerPCBase)
{
    return mf_get(&PowerPCBase->pp_MFSelf);
}

void KSendMsgFramePPC(struct PrivatePPCBase* PowerPCBase, struct MsgFrame* msgFrame)
{
    mf_send(&PowerPCBase->pp_MFSelf, msgFrame);
    xring_kick_m68k();
}

#if 0
/********************************************************************************************
*
*
*
*********************************************************************************************/

VOID CPUStats(struct PrivatePPCBase* PowerPCBase)
{
    ULONG currentTBL = getTBL();
    ULONG prevTBL = PowerPCBase->pp_CurrentTBL;
    PowerPCBase->pp_CurrentTBL = currentTBL;

    if (prevTBL)
    {
        ULONG systemLoad = 0;
        ULONG cpuLoad = 0;
        ULONG cpuUsage = 0;

        ULONG cpuTime = currentTBL - prevTBL;

        struct TaskPtr* currTask = (struct TaskPtr*)PowerPCBase->pp_AllTasks.mlh_Head;
        struct TaskPtr* nextTask;

        while ((nextTask = (struct TaskPtr*)currTask->tptr_Node.ln_Succ))
        {
            struct TaskPPC* ppcTask = (struct TaskPPC*)currTask->tptr_Task;

            ppcTask->tp_Totalelapsed += cpuTime;
            if ((ppcTask->tp_Task.tc_State == TS_RUN) || (ppcTask->tp_Task.tc_State == TS_CHANGING))
            {
                ppcTask->tp_Elapsed += cpuTime;
            }
            else if (ppcTask->tp_Task.tc_State == TS_WAIT)
            {
                ppcTask->tp_Elapsed2 += cpuTime;
            }

            if (!(PowerPCBase->pp_BusyCounter))
            {
                ULONG elapsed = ppcTask->tp_Elapsed;
                ULONG elapsed2 = ppcTask->tp_Elapsed2;
                ULONG total = ppcTask->tp_Totalelapsed;

                ULONG activity = (elapsed + elapsed2) >> 10;
                ULONG actelps = (elapsed >> 10) * 10000;

                if (activity)
                {
                    activity = actelps / activity;
                }

                ppcTask->tp_Activity = activity;

                LONG busy = (total >> 10) - (elapsed2 >> 10);

                if (busy < 0)
                {
                    busy = 0;
                }

                busy = (busy * 10000) / (total >> 10);

                ppcTask->tp_Busy = busy;

                systemLoad += busy;

                ULONG total10 = total >> 10;

                if (total10)
                {
                    cpuUsage = actelps / total10;
                }

                if (cpuUsage > 10000)
                {
                   cpuUsage = 10000;
                }

                ppcTask->tp_CPUusage = cpuUsage;

                cpuLoad += cpuUsage;

                ppcTask->tp_Elapsed = 0;
                ppcTask->tp_Elapsed2 = 0;
                ppcTask->tp_Totalelapsed = 0;
            }
            currTask = nextTask;
        }

        if (!(PowerPCBase->pp_BusyCounter))
        {
            PowerPCBase->pp_BusyCounter = 25;
            PowerPCBase->pp_SystemLoad = systemLoad;
            PowerPCBase->pp_CPULoad = cpuLoad;
        }

        PowerPCBase->pp_BusyCounter -= 1;

    }
    return;
}

#if 0

#pragma pack(push,2)
#include <proto/powerpc.h>
#include <powerpc/powerpc.h>
#include "constants.h"
#include "libstructs.h"
#include "Internalsppc.h"
#pragma pack(pop)

/********************************************************************************************
*
*	Entry point after kernel.s. This directs to the correct exception code.
*
*********************************************************************************************/

PPCKERNEL void Exception_Entry(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") struct iframe* iframe)
{
    PowerPCBase->pp_ExceptionMode = -1;
    PowerPCBase->pp_Quantum = PowerPCBase->pp_StdQuantum;
    PowerPCBase->pp_CPUSDR1 = getSDR1();
    PowerPCBase->pp_CPUHID0 = getHID0();

    if ((PowerPCBase->pp_DeviceID != DEVICE_MPC8343E) && (PowerPCBase->pp_DeviceID != DEVICE_MPC8314E))
    {
       PowerPCBase->pp_L2State = getL2State();
    }

    ULONG ExceptionVector = iframe->if_Context.ec_ExcID - (PPC_VECLEN * OPCODE_LEN);
    iframe->if_ExcNum = ExceptionVector >> 8;
    iframe->if_Context.ec_ExcID = 1 << iframe->if_ExcNum;

    switch (ExceptionVector)
    {
        case VEC_EXTERNAL:
        {
            switch (PowerPCBase->pp_DeviceID)
	        {
		        case DEVICE_HARRIER:
		        {
			        if (readmemLongPPC(PPC_XCSR_BASE, XCSR_FEST) & XCSR_FEST_MIM0)
                    {
                        writememLongPPC(PPC_XCSR_BASE, XCSR_FECL, XCSR_FECL_MIM0);
                        CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcInterrupt);
                    }

                    if (readmemLongPPC(PowerPCBase->pp_BridgeMPIC, XMPI_P0IAC) == 0xff)
                    {
                        PowerPCBase->pp_ExceptionMode = 0;
                        setDEC(PowerPCBase->pp_Quantum);
                        return;
                    }

                    struct MsgFrame* msgFrame;

                    while (msgFrame = KGetMsgFramePPC(PowerPCBase))
                    {
                        AddTailPPC((struct List*)&PowerPCBase->pp_MsgQueue , (struct Node*)msgFrame);
                    }
                    writememLongPPC(PowerPCBase->pp_BridgeMPIC, XMPI_P0EOI, 0);
                    break;
		        }

		        case DEVICE_MPC8343E:
                case DEVICE_MPC8314E:
		        {
                    if (loadPCI(IMMR_ADDR_DEFAULT, IMMR_IMISR) & IMMR_IMISR_IDI)
                    {
                        storePCI(IMMR_ADDR_DEFAULT, IMMR_IDR, 1);
                        CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcInterrupt);
                    }
                    struct MsgFrame* msgFrame;

                    while (msgFrame = KGetMsgFramePPC(PowerPCBase))
                    {
                        AddTailPPC((struct List*)&PowerPCBase->pp_MsgQueue, (struct Node*)msgFrame);
                    }

                    if (loadPCI(IMMR_ADDR_DEFAULT, IMMR_IMISR) & IMMR_IMISR_IM0I)
                    {
                        storePCI(IMMR_ADDR_DEFAULT, IMMR_IMISR, IMMR_IMISR_IM0I);
                    }
                    break;
		        }

		        case DEVICE_MPC107:
		        {
                    if (loadPCI(PPC_EUMB_BASE, MPC107_IMISR) & MPC107_IMISR_IM0I)
                    {
                        storePCI(PPC_EUMB_BASE, MPC107_IMISR, (MPC107_IMISR_IM0I | MPC107_IMISR_IM1I));
                        CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcInterrupt);
                    }

                    if ((readmemLongPPC(PPC_EUMB_EPICPROC, EPIC_IACK) >> 24) == 0xff)
                    {
                        PowerPCBase->pp_ExceptionMode = 0;
                        setDEC(PowerPCBase->pp_Quantum);
                        return;
                    }
                    struct MsgFrame* msgFrame;

                    while (msgFrame = KGetMsgFramePPC(PowerPCBase))
                    {
                        AddTailPPC((struct List*)&PowerPCBase->pp_MsgQueue, (struct Node*)msgFrame);
                    }
                    storePCI(PPC_EUMB_BASE, MPC107_IMISR, MPC107_IMISR_IPQI);
                    writememLongPPC(PPC_EUMB_EPICPROC, EPIC_EOI, 0);
			        break;
		        }
	        }

            CPUStats(PowerPCBase);

            ULONG currAddress = iframe->if_Context.ec_UPC.ec_SRR0;
            if ((PowerPCBase->pp_LowerLimit <= currAddress) && (currAddress < PowerPCBase->pp_UpperLimit))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            if ((PowerPCBase->pp_Mutex) || (PowerPCBase->pp_FlagForbid))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            struct TaskPPC* currTask = PowerPCBase->pp_ThisPPCProc;

            if (currTask)
            {
                if (currTask->tp_Task.tc_State == TS_ATOMIC)
                {
                    break;
                }
            }

            HandleMsgs(PowerPCBase);
            TaskCheck(PowerPCBase);
            SwitchPPC(PowerPCBase, iframe);
            break;
        }
        case VEC_DECREMENTER:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcDecrementer);

            struct ExcData* eData;

            while (eData = (struct ExcData*)RemHeadPPC((struct List*)&PowerPCBase->pp_ReadyExc))
            {
                if (eData->ed_ExcID & EXCF_MCHECK)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcMCheck, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_DACCESS)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcDAccess, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_IACCESS)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcIAccess, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_INTERRUPT)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcInterrupt, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_ALIGN)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcAlign, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_PROGRAM)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcProgram, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_FPUN)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcFPUn, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_DECREMENTER)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcDecrementer, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_SC)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcSystemCall, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_TRACE)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcTrace, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_PERFMON)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcPerfMon, (struct Node*)eData);
                if (eData->ed_ExcID & EXCF_IABR)
                    EnqueuePPC((struct List*)&PowerPCBase->pp_ExcIABR, (struct Node*)eData);
                eData->ed_Flags |= EXCF_ACTIVE;
            }

            while (eData = (struct ExcData*)RemHeadPPC((struct List*)&PowerPCBase->pp_RemovedExc))
            {
                ULONG nodeAddr = (ULONG)&eData->ed_LastExc;
                for (int i=0; i<12; i++)
                {
                    nodeAddr += 4;
                    ULONG actNode = *((ULONG*)(nodeAddr));
                    if (actNode)
                    {
                        RemovePPC((struct Node*)actNode);
                    }
                }
                struct ExcData* lastExc = eData->ed_LastExc;
                lastExc->ed_Flags &= ~EXCF_ACTIVE;
            }

            CPUStats(PowerPCBase);

            ULONG currAddress = iframe->if_Context.ec_UPC.ec_SRR0;
            if ((PowerPCBase->pp_LowerLimit <= currAddress) && (currAddress < PowerPCBase->pp_UpperLimit))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            if ((PowerPCBase->pp_Mutex) || (PowerPCBase->pp_FlagForbid))
            {
                PowerPCBase->pp_Quantum = 0x1000;
                break;
            }

            struct TaskPPC* currTask = PowerPCBase->pp_ThisPPCProc;

            if (currTask)
            {
                if (currTask->tp_Task.tc_State == TS_ATOMIC)
                {
                    break;
                }
            }

            HandleMsgs(PowerPCBase);
            TaskCheck(PowerPCBase);
            SwitchPPC(PowerPCBase, iframe);
            break;
        }
        case VEC_DATASTORAGE:
        {
            if (PowerPCBase->pp_EnDAccessExc)  //not standard WOS behaviour
            {
                CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcDAccess);
            }

            if ((iframe->if_Context.ec_DAR == 4) || ((iframe->if_Context.ec_DAR < 0xfffff800) && (iframe->if_Context.ec_DAR > 0x800)))
            {
                if (iframe->if_Context.ec_UPC.ec_SRR0 > 0x10000)
                {
                    if(!(PowerPCBase->pp_DataExcLow += 1))
                    {
                        PowerPCBase->pp_DataExcHigh += 1;
                    }
                    struct DataMsg myData;
                    ULONG Status = DoDataStore(iframe, iframe->if_Context.ec_UPC.ec_SRR0, &myData);
                    if (Status)
                    {
                        struct MsgFrame* myFrame = KCreateMsgFramePPC(PowerPCBase);
                        myFrame->mf_Identifier = myData.dm_Type;
                        myFrame->mf_Arg[1] = myData.dm_Address;
                        myFrame->mf_Arg[0] = myData.dm_Value;
                        KSendMsgFramePPC(PowerPCBase, myFrame);
                        if (!(myData.dm_LoadFlag))
                        {
                            while (myFrame->mf_Identifier != ID_DONE);
#ifdef DEBUG
                            if (myFrame->mf_Arg[0] == ERR_EMEM)
                            {
                                CommonExcError(PowerPCBase, iframe);
                                break;
                            }
#endif
                            if (!(FinDataStore(myFrame->mf_Arg[0], iframe, iframe->if_Context.ec_UPC.ec_SRR0, &myData)))
                            {
                                CommonExcError(PowerPCBase, iframe);
                                break;
                            }
                        }
                        iframe->if_Context.ec_UPC.ec_SRR0  += 4;
                        //PowerPCBase->pp_Quantum = getDEC() + 1000; //Do not start quantum all over
                        break;
                    }
                }
            }
            CommonExcError(PowerPCBase, iframe);
            break;
        }
        case VEC_PROGRAM:
        {
            if (iframe->if_Context.ec_GPR[4] == SUPERKEY)
            {
                iframe->if_Context.ec_UPC.ec_SRR0  += 4;
                iframe->if_Context.ec_SRR1         &= ~PSL_PR;
                iframe->if_Context.ec_GPR[3]        = 0;            //Set SuperKey
                //PowerPCBase->pp_Quantum = getDEC() + 1000;
            }
            else
            {
                CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcProgram);
            }
            break;
        }
        case VEC_ALIGNMENT:
        {

            if ((PowerPCBase->pp_EnAlignExc) || (iframe->if_Context.ec_DAR < 0x200000))
            {
                CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcAlign);
            }
            else
            {
                if (DoAlign(iframe, iframe->if_Context.ec_UPC.ec_SRR0))
                {
                    iframe->if_Context.ec_UPC.ec_SRR0  += 4;
                    if (!(PowerPCBase->pp_AlignmentExcLow += 1))
                    {
                        PowerPCBase->pp_AlignmentExcHigh += 1;
                    }
                    //PowerPCBase->pp_Quantum = getDEC() + 1000;
                }
                else
                {
                    CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcAlign);
                }
            }
            break;
        }
        case VEC_ALTIVECUNAV:
        {
            iframe->if_Context.ec_ExcID = EXCF_ALTIVECUNAV;
            iframe->if_ExcNum = EXCB_ALTIVECUNAV;
            if (PowerPCBase->pp_EnAltivec)
            {
                iframe->if_Context.ec_SRR1 |= PSL_VEC;
            }
            else
            {
                CommonExcError(PowerPCBase, iframe);
            }
            break;
        }
        case VEC_MACHINECHECK:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcMCheck);
            break;
        }
        case VEC_INSTSTORAGE:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcIAccess);
            break;
        }
        case VEC_FPUNAVAILABLE:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcFPUn);
            break;
        }
        case VEC_SYSTEMCALL:
        {
            struct SysCall* sc = (struct SysCall*)iframe->if_Context.ec_GPR[3]; //no checking of valid address

            switch (sc->sc_Function)
            {
                case SC_CREATEMSG:
                {
                    iframe->if_Context.ec_GPR[3] = (ULONG)KCreateMsgFramePPC(PowerPCBase);
                    break;
                }
                case SC_GETMSG:
                {
                    iframe->if_Context.ec_GPR[3] = (ULONG)KGetMsgFramePPC(PowerPCBase);
                    break;
                }
                case SC_SENDMSG:
                {
                    KSendMsgFramePPC(PowerPCBase, (struct MsgFrame*)sc->sc_Arg[0]);
                    break;
                }
                case SC_FREEMSG:
                {
                    KFreeMsgFramePPC(PowerPCBase, (struct MsgFrame*)sc->sc_Arg[0]);
                    break;
                }
                case SC_SETCACHE:
                {
                    KSetCache(PowerPCBase, sc->sc_Arg[0], (APTR)sc->sc_Arg[1], sc->sc_Arg[2]);
                    break;
                }
                default:
                {
                    CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcSystemCall);
                    break;
                }
            }
            break;
        }
        case VEC_TRACE:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcTrace);
            break;
        }
        case VEC_PERFMONITOR:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcPerfMon);
            break;
        }
        case VEC_IBREAKPOINT:
        {
            CommonExcHandler(PowerPCBase, iframe, (struct List*)&PowerPCBase->pp_ExcIABR);
            break;
        }
        default:
        {
            CommonExcError(PowerPCBase, iframe);
        }
    }
    PowerPCBase->pp_ExceptionMode = 0;
    setDEC(PowerPCBase->pp_Quantum);
    return;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCFUNCTION void TaskCheck(__reg("r3") struct PrivatePPCBase* PowerPCBase)
{
    ULONG mask;
    struct TaskPPC* currTask = PowerPCBase->pp_ThisPPCProc;

    if(currTask)
    {
        if ((PowerPCBase->pp_TaskExcept) && (PowerPCBase->pp_TaskExcept == currTask) &&
            (currTask->tp_Task.tc_ExceptData) && (mask = currTask->tp_Task.tc_SigRecvd & currTask->tp_Task.tc_SigExcept))
        {
            currTask->tp_Task.tc_SigRecvd &= ~mask;
            currTask->tp_Task.tc_SigExcept &= ~mask;

            ULONG (*ExcHandler)(__reg("r2") ULONG, __reg("r3") ULONG) = currTask->tp_Task.tc_ExceptCode;
            ULONG tempR2 = getR2();
            ULONG signal = ExcHandler((ULONG)currTask->tp_Task.tc_ExceptData, mask);
            storeR2(tempR2);

            currTask->tp_Task.tc_SigExcept |= signal;
        }
    }

    if (!(PowerPCBase->pp_FlagWait))
    {
        struct WaitTime* currWait = (struct WaitTime*)PowerPCBase->pp_WaitTime.mlh_Head;
        struct WaitTime* nxtWait;

        while (nxtWait = (struct WaitTime*)currWait->wt_Node.ln_Succ)
        {
            if ((currWait->wt_TimeUpper < getTBU()) || ((currWait->wt_TimeUpper == getTBU()) && (currWait->wt_TimeLower < getTBL())))
            {
                if ((currTask) && (currTask == currWait->wt_Task))
                {
                    currWait->wt_Task->tp_Task.tc_State = TS_RUN;
                }
                else
                {
                    currWait->wt_Task->tp_Task.tc_State = TS_READY;
                }
                currWait->wt_Task->tp_Task.tc_SigRecvd |= SIGF_WAIT;
                RemovePPC((struct Node*)currWait);
            }
            currWait = nxtWait;
        }
    }

    struct TaskPPC* currWTask = (struct TaskPPC*)PowerPCBase->pp_WaitingTasks.mlh_Head;
    struct TaskPPC* nxtWTask;

    while (nxtWTask = (struct TaskPPC*)currWTask->tp_Task.tc_Node.ln_Succ)
    {
        if ((currWTask->tp_Task.tc_State == TS_READY) || (currWTask->tp_Task.tc_SigWait & currWTask->tp_Task.tc_SigRecvd))
        {
            currWTask->tp_Task.tc_State = TS_READY;
            RemovePPC((struct Node*)currWTask);
            AddTailPPC((struct List*)&PowerPCBase->pp_ReadyTasks, (struct Node*)currWTask); //Enqueue on pri?
        }
        currWTask = nxtWTask;
    }
    return;
}

/********************************************************************************************
*
*	This function dispatches new PPC tasks, switches between tasks and redirect signals.
*
*********************************************************************************************/

PPCKERNEL void HandleMsgs(__reg("r3") struct PrivatePPCBase* PowerPCBase)
{
    struct MsgFrame* currMsg = (struct MsgFrame*)PowerPCBase->pp_MsgQueue.mlh_Head;
    struct MsgFrame* nxtMsg;
    while (nxtMsg = (struct MsgFrame*)currMsg->mf_Message.mn_Node.ln_Succ)
    {
        switch (currMsg->mf_Identifier)
        {
            case ID_TPPC:
            case ID_DONE:
            case ID_END:
            case ID_DNLL:
            {
                struct TaskPPC* myTask = currMsg->mf_PPCTask;
                if ((currMsg->mf_Identifier == ID_TPPC) && (!(myTask)))
                {
                    RemovePPC((struct Node*)currMsg);
                    AddTailPPC((struct List*)&PowerPCBase->pp_NewTasks, (struct Node*)currMsg);
                }
                else
                {
                    struct MsgPortPPC* myPort = myTask->tp_Msgport;
                    if (myPort->mp_Semaphore.ssppc_SS.ss_QueueCount == -1)
                    {
                        RemovePPC((struct Node*)currMsg);

                        ULONG signal = 1 << (myPort->mp_Port.mp_SigBit);
                        myTask->tp_Task.tc_SigRecvd |= signal;

                        AddTailPPC(&myPort->mp_Port.mp_MsgList, (struct Node*)currMsg);
                        if (currMsg->mf_Identifier == ID_DONE)
                        {
                            myTask->tp_Task.tc_SigAlloc = currMsg->mf_Arg[1];
                        }
                        if (myTask == PowerPCBase->pp_ThisPPCProc)
                        {
                            myTask->tp_Task.tc_State = TS_RUN;
                        }
                        else
                        {
                            myTask->tp_Task.tc_State = TS_READY;
                        }
                    }
                }
                break;
            }
            case ID_XMSG:
            {
                struct MsgPortPPC* myPort = (struct MsgPortPPC*)currMsg->mf_Message.mn_ReplyPort;
                if (myPort->mp_Semaphore.ssppc_SS.ss_QueueCount != -1)
                {
                    break;
                }

                struct MsgFrame* oldMsg = (struct MsgFrame*)currMsg->mf_Arg[0];

                RemovePPC((struct Node*)currMsg);
                KFreeMsgFramePPC(PowerPCBase, currMsg);

                myPort = (struct MsgPortPPC*)oldMsg->mf_Message.mn_ReplyPort;
                struct TaskPPC* sigTask = (struct TaskPPC*)myPort->mp_Port.mp_SigTask;

                if (!(sigTask))
                {
                    break;
                }

                ULONG signal = 1 << (myPort->mp_Port.mp_SigBit);
                sigTask->tp_Task.tc_SigRecvd |= signal;
                AddTailPPC(&myPort->mp_Port.mp_MsgList, (struct Node*)oldMsg);

                if (sigTask == PowerPCBase->pp_ThisPPCProc)
                {
                    sigTask->tp_Task.tc_State = TS_RUN;
                }
                else
                {
                    sigTask->tp_Task.tc_State = TS_READY;
                }
                break;
            }
            case ID_XPPC:
            {
                struct MsgPortPPC* myPort = (struct MsgPortPPC*)currMsg->mf_Arg[0];
                if (myPort->mp_Semaphore.ssppc_SS.ss_QueueCount != -1)
                {
                    break;
                }

                struct Node* xMsg = (struct Node*)currMsg->mf_Arg[1];

                RemovePPC((struct Node*)currMsg);
                KFreeMsgFramePPC(PowerPCBase, currMsg);

                struct TaskPPC* sigTask = (struct TaskPPC*)myPort->mp_Port.mp_SigTask;

                if (!(sigTask))
                {
                    break;
                }

                ULONG signal = 1 << (myPort->mp_Port.mp_SigBit);
                sigTask->tp_Task.tc_SigRecvd |= signal;
                AddTailPPC(&myPort->mp_Port.mp_MsgList, xMsg);

                if (sigTask == PowerPCBase->pp_ThisPPCProc)
                {
                    sigTask->tp_Task.tc_State = TS_RUN;
                }
                else
                {
                    sigTask->tp_Task.tc_State = TS_READY;
                }
                break;
            }
            case ID_LLPP:
            {
                struct PrivateTask* myTask = (struct PrivateTask*)PowerPCBase->pp_ThisPPCProc;

                if ((myTask) && (myTask->pt_Mirror68K == (struct Task*)currMsg->mf_Arg[0]))
                {
                    myTask->pt_Task.tp_Task.tc_State = TS_RUN;
                    myTask->pt_Task.tp_Task.tc_SigRecvd |= currMsg->mf_Signals;
                }
                else
                {
                    struct PrivateTask* nxtTask;
                    struct PrivateTask* currTask = (struct PrivateTask*)PowerPCBase->pp_WaitingTasks.mlh_Head;
                    while (nxtTask = (struct PrivateTask*)currTask->pt_Task.tp_Task.tc_Node.ln_Succ)
                    {
                        if (currTask->pt_Mirror68K == (struct Task*)currMsg->mf_Arg[0])
                        {
                            currTask->pt_Task.tp_Task.tc_State = TS_READY;
                            currTask->pt_Task.tp_Task.tc_SigRecvd |= currMsg->mf_Signals;
                            break;
                        }
                        currTask = nxtTask;
                    }
                    if (!(nxtTask))
                    {
                        currTask = (struct PrivateTask*)PowerPCBase->pp_ReadyTasks.mlh_Head;
                        while (nxtTask = (struct PrivateTask*)currTask->pt_Task.tp_Task.tc_Node.ln_Succ)
                        {
                            if (currTask->pt_Mirror68K == (struct Task*)currMsg->mf_Arg[0])
                            {
                                currTask->pt_Task.tp_Task.tc_SigRecvd |= currMsg->mf_Signals;
                                break;
                            }
                            currTask = nxtTask;
                        }
                    }
                }
                RemovePPC((struct Node*)currMsg);
                KFreeMsgFramePPC(PowerPCBase, currMsg);
                break;
            }
            default:
            {
                RemovePPC((struct Node*)currMsg);
                KFreeMsgFramePPC(PowerPCBase, currMsg);
            }
        }
        currMsg = nxtMsg;
    }
    return;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL void SwitchPPC(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") struct iframe* iframe)
{
    struct TaskPPC* currTask = PowerPCBase->pp_ThisPPCProc;
    while (1)
    {
        if (!(currTask))
        {
            if (currTask = (struct TaskPPC*)RemHeadPPC((struct List*)&PowerPCBase->pp_NewTasks))
            {
                DispatchPPC(PowerPCBase, (struct MsgFrame*)currTask);
                break;
            }
            if (currTask = (struct TaskPPC*)RemHeadPPC((struct List*)&PowerPCBase->pp_ReadyTasks))
            {
                currTask->tp_Task.tc_State = TS_RUN;
                PowerPCBase->pp_ThisPPCProc = currTask;
                iframe = (struct iframe*)currTask->tp_ContextMem;
                currTask->tp_Task.tc_SPReg = (APTR)iframe->if_Context.ec_GPR[1];
                break;
            }
            break;
        }
        if (currTask->tp_Task.tc_State == TS_REMOVED)
        {
            AddTailPPC((struct List*)&PowerPCBase->pp_RemovedTasks, (struct Node*)currTask);
            currTask = NULL;
            PowerPCBase->pp_ThisPPCProc = currTask;
            iframe->if_Context.ec_SRR1 = MACHINESTATE_DEFAULT;
            iframe->if_Context.ec_UPC.ec_SRR0 = (PowerPCBase->pp_PPCMemBase) + OFFSET_SYSMEM;
        }
        else if (currTask->tp_Task.tc_State == TS_CHANGING)
        {
            currTask->tp_Task.tc_State = TS_WAIT;
            AddTailPPC((struct List*)&PowerPCBase->pp_WaitingTasks, (struct Node*)currTask);
            currTask = NULL;
            PowerPCBase->pp_ThisPPCProc = currTask;
        }
        else
        {
            if (currTask = (struct TaskPPC*)RemHeadPPC((struct List*)&PowerPCBase->pp_NewTasks))
            {
                struct TaskPPC* oldTask = PowerPCBase->pp_ThisPPCProc;
                oldTask->tp_Task.tc_State = TS_READY;
                AddTailPPC((struct List*)&PowerPCBase->pp_ReadyTasks, (struct Node*)oldTask);
                DispatchPPC(PowerPCBase, (struct MsgFrame*)currTask);
            }
            else if (currTask = (struct TaskPPC*)RemHeadPPC((struct List*)&PowerPCBase->pp_ReadyTasks))
            {
                struct TaskPPC* oldTask = PowerPCBase->pp_ThisPPCProc;
                oldTask->tp_Task.tc_State = TS_READY;
                currTask->tp_Task.tc_State = TS_RUN;
                AddTailPPC((struct List*)&PowerPCBase->pp_ReadyTasks, (struct Node*)oldTask);
                PowerPCBase->pp_ThisPPCProc = currTask;
                iframe = (struct iframe*)currTask->tp_ContextMem;
                currTask->tp_Task.tc_SPReg = (APTR)iframe->if_Context.ec_GPR[1];
            }
            break;
        }
    }
    return;
}
/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL void DispatchPPC(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") struct MsgFrame* myFrame)
{
    struct NewTask* newTask = (struct NewTask*)myFrame->mf_Arg[0];

    PowerPCBase->pp_IdUsrTasks += 1;

    newTask->nt_Task.pt_Task.tp_Id = PowerPCBase->pp_IdUsrTasks;
    newTask->nt_Task.pt_Task.tp_PowerPCBase = PowerPCBase;
    newTask->nt_Task.pt_Task.tp_Task.tc_Node.ln_Type = NT_PPCTASK;
    newTask->nt_Task.pt_Task.tp_Task.tc_State = TS_RUN;
    newTask->nt_Task.pt_Task.tp_ContextMem = &newTask->nt_Context;
    newTask->nt_Task.pt_Task.tp_BATStorage = &newTask->nt_BatStore;
    newTask->nt_Task.pt_Task.tp_Link.tl_Task = newTask;
    newTask->nt_Task.pt_Task.tp_Link.tl_Sig = 0xfff;
    newTask->nt_Task.pt_Task.tp_StackMem = (APTR)(myFrame->mf_Arg[0] + 2048);
    newTask->nt_Task.pt_Mirror68K = (struct Task*)myFrame->mf_Arg[2];
    newTask->nt_Task.pt_MirrorPort = myFrame->mf_MirrorPort;
    newTask->nt_Task.pt_Task.tp_Task.tc_Node.ln_Name = (APTR)&newTask->nt_Name;
    newTask->nt_Task.pt_Task.tp_StackSize = (ULONG)myFrame->mf_Message.mn_Node.ln_Name;
    newTask->nt_Task.pt_Task.tp_Task.tc_SPLower = newTask->nt_Task.pt_Task.tp_StackMem;
    newTask->nt_Task.pt_Task.tp_Task.tc_SPUpper = (APTR)((ULONG)newTask->nt_Task.pt_Task.tp_StackMem + newTask->nt_Task.pt_Task.tp_StackSize);
    newTask->nt_Task.pt_Task.tp_Task.tc_SPReg   = (APTR)((ULONG)newTask->nt_Task.pt_Task.tp_Task.tc_SPUpper - 32);

    NewListPPC((struct List*)&newTask->nt_Task.pt_Task.tp_Task.tc_MemEntry);
    newTask->nt_Task.pt_Task.tp_Task.tc_SigAlloc = myFrame->mf_Arg[1];
    PowerPCBase->pp_ThisPPCProc = (struct TaskPPC*)newTask;

    newTask->nt_Task.pt_Task.tp_TaskPtr = &newTask->nt_TaskPtr;
    newTask->nt_TaskPtr.tptr_Task = (struct TaskPPC*)newTask;
    newTask->nt_TaskPtr.tptr_Node.ln_Name = newTask->nt_Task.pt_Task.tp_Task.tc_Node.ln_Name;
    AddTailPPC((struct List*)&PowerPCBase->pp_AllTasks, (struct Node*)&newTask->nt_TaskPtr);

    PowerPCBase->pp_NumAllTasks += 1;

    NewListPPC((struct List*)&newTask->nt_Task.pt_Task.tp_TaskPools);
    NewListPPC((struct List*)&newTask->nt_Port.mp_IntMsg);
    NewListPPC((struct List*)&newTask->nt_Port.mp_Port.mp_MsgList);
    newTask->nt_Port.mp_Port.mp_SigBit = SIGB_DOS;
    NewListPPC((struct List*)&newTask->nt_Port.mp_Semaphore.ssppc_SS.ss_WaitQueue);
    newTask->nt_Port.mp_Semaphore.ssppc_SS.ss_Owner = NULL;
    newTask->nt_Port.mp_Semaphore.ssppc_SS.ss_NestCount = 0;
    newTask->nt_Port.mp_Semaphore.ssppc_SS.ss_QueueCount = -1;
    newTask->nt_Port.mp_Semaphore.ssppc_reserved = &newTask->nt_SSReserved1;
    newTask->nt_Port.mp_Port.mp_SigTask = newTask;
    newTask->nt_Port.mp_Port.mp_Flags = PA_SIGNAL;
    newTask->nt_Port.mp_Port.mp_Node.ln_Type = NT_MSGPORTPPC;
    newTask->nt_Task.pt_Task.tp_Msgport = &newTask->nt_Port;

    struct iframe* newFrame = (struct iframe*)&newTask->nt_Context;

    newFrame->if_Context.ec_SRR1 = MACHINESTATE_DEFAULT;
    newFrame->if_Context.ec_UPC.ec_SRR0 = *((ULONG*)((ULONG)PowerPCBase + 2 + _LVOStartTask));
    newFrame->if_Context.ec_GPR[1] = (ULONG)newTask->nt_Task.pt_Task.tp_Task.tc_SPReg;
    newFrame->if_Context.ec_GPR[2] = NULL;
    newFrame->if_Context.ec_GPR[3] = (ULONG)PowerPCBase;
    newFrame->if_Context.ec_GPR[4] = (ULONG)myFrame;

    ULONG* EndofStack = (APTR)newFrame->if_Context.ec_GPR[1];
    EndofStack[0] = 0; //Clear end of stack chain for SwapStack

    CopyMemPPC(&PowerPCBase->pp_SystemBATs, &newFrame->if_BATs, sizeof(struct BATArray) * 4);
    CopyMemPPC(&PowerPCBase->pp_SystemBATs, newTask->nt_Task.pt_Task.tp_BATStorage, sizeof(struct BATArray) * 4);

    for (int i = 0; i < 16; i++)
    {
        newFrame->if_Segments[i] = PowerPCBase->pp_SystemSegs[i];
    }

    return;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL void CommonExcHandler(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") struct iframe* iframe, __reg("r5") struct List* excList)
{
    struct ExcData* nxtNode;
    struct ExcData* currNode = (struct ExcData*)excList->lh_Head;
    ULONG status = EXCRETURN_NORMAL;

    while (nxtNode = (struct ExcData*)currNode->ed_Node.ln_Succ)
    {
        if (currNode->ed_ExcID & iframe->if_Context.ec_ExcID)
        {
            if ((currNode->ed_Flags & EXCF_GLOBAL) || ((currNode->ed_Flags & EXCF_LOCAL) && (currNode->ed_Task) && (currNode->ed_Task == PowerPCBase->pp_ThisPPCProc)))
            {
                if (currNode->ed_Flags & EXCF_LARGECONTEXT)
                {
                    ULONG (*ExcHandler)(__reg("r2") ULONG, __reg("r3") struct EXCContext*) = currNode->ed_Code;
                    ULONG tempR2 = getR2();
                    status = ExcHandler(currNode->ed_Data, &iframe->if_Context);
                    storeR2(tempR2);
                }
                else if (currNode->ed_Flags & EXCF_SMALLCONTEXT)
                {
                    status = SmallExcHandler(currNode, iframe);
                }
                if (status == EXCRETURN_ABORT)
                {
                    break;
                }
            }
        }
        currNode = nxtNode;
    }

    if ((status == EXCRETURN_NORMAL) && (iframe->if_Context.ec_ExcID != EXCF_DACCESS))
    {
        CommonExcError(PowerPCBase, iframe);
    }
    return;
}

#if 0
/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL VOID writememLongPPC(__reg("r3") ULONG Base, __reg("r4") ULONG offset, __reg("r5") ULONG value)
{
	*((ULONG*)(Base + offset)) = value;
	return;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL ULONG readmemLongPPC(__reg("r3") ULONG Base, __reg("r4") ULONG offset)
{
    ULONG res;
    res = *((ULONG*)(Base + offset));
    return res;
}
#endif


/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL struct MsgFrame* KGetMsgFramePPC(__reg("r3") struct PrivatePPCBase* PowerPCBase)
{
    ULONG msgFrame = 0;

    switch (PowerPCBase->pp_DeviceID)
	{
		case DEVICE_HARRIER:
		{
			ULONG msgOffset1 = readmemLongPPC(PPC_XCSR_BASE, XCSR_MIIPH);
            ULONG msgOffset2 = readmemLongPPC(PPC_XCSR_BASE, XCSR_MIIPT);
            if (msgOffset1 != msgOffset2)
            {
                writememLongPPC(PPC_XCSR_BASE, XCSR_MIIPT, (msgOffset2 + 4) & 0xffff3fff);
                msgFrame = readmemLongPPC(msgOffset2, 0);
            }
            break;
		}

		case DEVICE_MPC8343E:
        case DEVICE_MPC8314E:
		{
			struct killFIFO* myFIFO = (struct killFIFO*)((ULONG)(PowerPCBase->pp_PPCMemBase + FIFO_END));
			if (myFIFO->kf_MIIPH != myFIFO->kf_MIIPT)
            {
                msgFrame = *((ULONG*)(myFIFO->kf_MIIPT));
			    myFIFO->kf_MIIPT = (myFIFO->kf_MIIPT + 4) & 0xffff3fff;
            }
			break;
		}

		case DEVICE_MPC107:
		{
            ULONG msgOffset1 = loadPCI(PPC_EUMB_BASE, MPC107_IPHPR);
            ULONG msgOffset2 = loadPCI(PPC_EUMB_BASE, MPC107_IPTPR);
            if (msgOffset1 != msgOffset2)
            {
                storePCI(PPC_EUMB_BASE, MPC107_IPTPR, ((msgOffset2 + 4) | 0x4000) & 0xffff7fff);
                msgFrame = readmemLongPPC(msgOffset2, 0);
            }
			break;
		}

		default:
		{
			break;
		}
	}
	return (struct MsgFrame*)msgFrame;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL VOID KSendMsgFramePPC(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") struct MsgFrame* msgFrame)
{
	switch (PowerPCBase->pp_DeviceID)
	{
		case DEVICE_HARRIER:
		{
            ULONG msgOffset = readmemLongPPC(PPC_XCSR_BASE, XCSR_MIOPH);
            writememLongPPC(msgOffset, 0, (ULONG)msgFrame);
            writememLongPPC(PPC_XCSR_BASE, XCSR_MIOPH, (msgOffset + 4) & 0xffff3fff);
			break;
		}

		case DEVICE_MPC8343E:
        case DEVICE_MPC8314E:
		{
            struct killFIFO* myFIFO = (struct killFIFO*)((ULONG)(PowerPCBase->pp_PPCMemBase + FIFO_END));
			*((ULONG*)(myFIFO->kf_MIOPH)) = (ULONG)msgFrame;
			myFIFO->kf_MIOPH = (myFIFO->kf_MIOPH + 4) & 0xffff3fff;
			storePCI(IMMR_ADDR_DEFAULT, IMMR_OMR0, (ULONG)msgFrame);
            break;
		}

		case DEVICE_MPC107:
		{
            ULONG msgOffset = loadPCI(PPC_EUMB_BASE, MPC107_OPHPR);
            writememLongPPC(msgOffset, 0, (ULONG)msgFrame);
            storePCI(PPC_EUMB_BASE, MPC107_OPHPR, ((msgOffset + 4) & 0xbfff));
			break;
		}

		default:
		{
			break;
		}
	}
	return;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL VOID FlushDCache(__reg("r3") struct PrivatePPCBase* PowerPCBase)
{
    ULONG cacheSize;

    if (PowerPCBase->pp_CacheDisDFlushAll)
    {
        cacheSize = 0;
    }
    else
    {
        cacheSize = PowerPCBase->pp_CurrentL2Size;
    }

    cacheSize = (cacheSize >> 5) + CACHE_L1SIZE;
    //ULONG mem = ((PowerPCBase->pp_PPCMemSize - 0x400000) + PowerPCBase->pp_PPCMemBase);

    ULONG mem = 0;

    ULONG mem2 = mem;

    for (int i = 0; i < cacheSize; i++)
    {
        loadWord(mem);
        mem += CACHELINE_SIZE;
    }

    for (int i = 0; i < cacheSize; i++)
    {
        dFlush(mem2);
        mem2 += CACHELINE_SIZE;
    }

    return;
}

/********************************************************************************************
*
*
*
*********************************************************************************************/

PPCKERNEL VOID KSetCache(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") ULONG flags, __reg("r5") APTR start, __reg("r6") ULONG length)
{
    ULONG value;

    switch (flags)
    {
        case CACHE_DCACHEOFF:
        {
            if (!(PowerPCBase->pp_CacheDState))
            {
                FlushDCache(PowerPCBase);

                value = getHID0();
                value &= ~HID0_DCE;
                setHID0(value);
                PowerPCBase->pp_CacheDState = -1;
            }
            break;
        }
        case CACHE_DCACHEON:
        {
            PowerPCBase->pp_CacheDState = 0;
            value = getHID0();
            value |= HID0_DCE;
            setHID0(value);
            break;
        }
        case CACHE_DCACHELOCK:
        {
            if ((start) && (length) && !(PowerPCBase->pp_CacheDLockState))
            {
                FlushDCache(PowerPCBase);
                ULONG iterations = (ULONG)start + length;
                ULONG mask = -32;
                start = (APTR)((ULONG)start & mask);
                ULONG mem = (ULONG)start;
                iterations = (((iterations + 31) & mask) - (ULONG)start) >> 5;

                for (int i = 0; i < iterations; i++)
                {
                    loadWord(mem);
                    mem += CACHELINE_SIZE;
                }

                value = getHID0();
                value |= HID0_DLOCK;
                setHID0(value);
                PowerPCBase->pp_CacheDLockState = -1;
            }
            break;
        }
        case CACHE_DCACHEUNLOCK:
        {
            PowerPCBase->pp_CacheDLockState = 0;
            value = getHID0();
            value &= ~HID0_DLOCK;
            setHID0(value);
            break;
        }
        case CACHE_DCACHEFLUSH:
        {
            if (!(PowerPCBase->pp_CacheDState) && !(PowerPCBase->pp_CacheDLockState))
            {
                if (PowerPCBase->pp_L2Size)
                {
                    if ((start) && (length))
                    {
                        ULONG iterations = (ULONG)start + length;
                        ULONG mask = -32;
                        start = (APTR)((ULONG)start & mask);
                        ULONG mem = (ULONG)start;
                        iterations = (((iterations + 31) & mask) - (ULONG)start) >> 5;

                        for (int i = 0; i < iterations; i++)
                        {
                            dFlush(mem);
                            mem += CACHELINE_SIZE;
                        }
                        sync();
                    }
                    else
                    {
                        FlushDCache(PowerPCBase);
                    }
                }
                else
                {
                    ULONG mem = 0;
                    for (int i = 0; i < CACHE_L1SIZE; i++)
                    {
                        loadWord(mem);
                        mem += CACHELINE_SIZE;
                    }
                }
            }
            break;
        }
        case CACHE_ICACHEOFF:
        {
            value = getHID0();
            value &= ~HID0_ICE;
            setHID0(value);
            break;
        }
        case CACHE_ICACHEON:
        {
            value = getHID0();
            value |= HID0_ICE;
            setHID0(value);
            break;
        }
        case CACHE_ICACHELOCK:
        {
            value = getHID0();
            value |= HID0_ILOCK;
            setHID0(value);
            break;
        }
        case CACHE_ICACHEUNLOCK:
        {
            value = getHID0();
            value &= ~HID0_ILOCK;
            setHID0(value);
            break;
        }
        case CACHE_DCACHEINV:
        {
            if ((start) && (length))
            {
                if (PowerPCBase->pp_L2Size)
                {
                    ULONG iterations = (ULONG)start + length;
                    ULONG mask = -32;
                    start = (APTR)((ULONG)start & mask);
                    ULONG mem = (ULONG)start;
                    iterations = (((iterations + 31) & mask) - (ULONG)start) >> 5;

                    for (int i = 0; i < iterations; i++)
                    {
                        dInval(mem);
                        mem += CACHELINE_SIZE;
                    }
                    sync();
                }
                else
                {
                    ULONG mem = 0;
                    for (int i = 0; i < CACHE_L1SIZE; i++)
                    {
                        loadWord(mem);
                        mem += CACHELINE_SIZE;
                    }
                }
            }
            break;
        }
        case CACHE_ICACHEINV:
        {
            if ((start) && (length))
            {
                ULONG iterations = (ULONG)start + length;
                ULONG mask = -32;
                start = (APTR)((ULONG)start & mask);
                ULONG mem = (ULONG)start;
                iterations = (((iterations + 31) & mask) - (ULONG)start) >> 5;

                for (int i = 0; i < iterations; i++)
                {
                    iInval(mem);
                    mem += CACHELINE_SIZE;
                }
                isync();
            }
            else
            {
                FlushICache();
            }
            break;
        }
    }
    if ((PowerPCBase->pp_DeviceID != DEVICE_MPC8343E) && (PowerPCBase->pp_DeviceID != DEVICE_MPC8314E))
    {
        switch (flags) //To prevent SDA_BASE
        {
            case  CACHE_L2CACHEON:
            {
                value = getL2State();
                value |= L2CR_L2E;
                setL2State(value);
                PowerPCBase->pp_CurrentL2Size = PowerPCBase->pp_L2Size;
                break;
            }
            case CACHE_L2CACHEOFF:
            {
                FlushDCache(PowerPCBase);
                value = getL2State();
                value &= ~L2CR_L2E;
                setL2State(value);
                PowerPCBase->pp_CurrentL2Size = 0;
                break;
            }
            case CACHE_L2WTON:
            {
                value = getL2State();
                value |= L2CR_L2WT;
                setL2State(value);
                break;
            }
            case CACHE_L2WTOFF:
            {
                value = getL2State();
                value &= ~L2CR_L2WT;
                setL2State(value);
                break;
            }
            case CACHE_TOGGLEDFLUSH:
            {
                PowerPCBase->pp_CacheDisDFlushAll = !PowerPCBase->pp_CacheDisDFlushAll;
                break;
            }
        }
    }
    return;
}

/********************************************************************************************
*
*     Entry point to print an exception error in a window.
*
*********************************************************************************************/

PPCKERNEL void CommonExcError(__reg("r3") struct PrivatePPCBase* PowerPCBase, __reg("r4") struct iframe* iframe)
{
    if ((iframe->if_Context.ec_ExcID == EXCF_INTERRUPT) || (iframe->if_Context.ec_ExcID == EXCF_DECREMENTER)
     || (iframe->if_Context.ec_ExcID == EXCF_TRACE)     || (iframe->if_Context.ec_ExcID == EXCF_PERFMON))
    {
        return;
    }

    ULONG* errorData = (ULONG*)(PowerPCBase->pp_PPCMemBase + (ULONG)(FIFO_END + 0x100));
    errorData[0]  = (ULONG)PowerPCBase->pp_ThisPPCProc->tp_Task.tc_Node.ln_Name;
    errorData[1]  = (ULONG)PowerPCBase->pp_ThisPPCProc;
    errorData[2]  = iframe->if_ExcNum;
    errorData[3]  = iframe->if_Context.ec_UPC.ec_SRR0;
    errorData[4]  = iframe->if_Context.ec_SRR1;
    errorData[5]  = getMSR();
    errorData[6]  = getHID0();
    errorData[7]  = getPVR();
    errorData[8]  = iframe->if_Context.ec_DAR;
    errorData[9]  = iframe->if_Context.ec_DSISR;
    errorData[10] = getSDR1();
    errorData[11] = getDEC();
    errorData[12] = getTBU();
    errorData[13] = getTBL();
    errorData[14] = iframe->if_Context.ec_XER;
    errorData[15] = iframe->if_Context.ec_CR;
    errorData[16] = iframe->if_Context.ec_FPSCR;
    errorData[17] = iframe->if_Context.ec_LR;
    errorData[18] = iframe->if_Context.ec_CTR;
    errorData[19] = iframe->if_Context.ec_GPR[0];
    errorData[20] = iframe->if_Context.ec_GPR[1];
    errorData[21] = iframe->if_Context.ec_GPR[2];
    errorData[22] = iframe->if_Context.ec_GPR[3];
    errorData[23] = iframe->if_BATs[0].ba_ibatu;;
    errorData[24] = iframe->if_BATs[0].ba_ibatl;
    errorData[25] = iframe->if_Context.ec_GPR[4];
    errorData[26] = iframe->if_Context.ec_GPR[5];
    errorData[27] = iframe->if_Context.ec_GPR[6];
    errorData[28] = iframe->if_Context.ec_GPR[7];
    errorData[29] = iframe->if_BATs[1].ba_ibatu;
    errorData[30] = iframe->if_BATs[1].ba_ibatl;
    errorData[31] = iframe->if_Context.ec_GPR[8];
    errorData[32] = iframe->if_Context.ec_GPR[9];
    errorData[33] = iframe->if_Context.ec_GPR[10];
    errorData[34] = iframe->if_Context.ec_GPR[11];
    errorData[35] = iframe->if_BATs[2].ba_ibatu;
    errorData[36] = iframe->if_BATs[2].ba_ibatl;
    errorData[37] = iframe->if_Context.ec_GPR[12];
    errorData[38] = iframe->if_Context.ec_GPR[13];
    errorData[39] = iframe->if_Context.ec_GPR[14];
    errorData[40] = iframe->if_Context.ec_GPR[15];
    errorData[41] = iframe->if_BATs[3].ba_ibatu;
    errorData[42] = iframe->if_BATs[3].ba_ibatl;
    errorData[43] = iframe->if_Context.ec_GPR[16];
    errorData[44] = iframe->if_Context.ec_GPR[17];
    errorData[45] = iframe->if_Context.ec_GPR[18];
    errorData[46] = iframe->if_Context.ec_GPR[19];
    errorData[47] = iframe->if_BATs[0].ba_dbatu;
    errorData[48] = iframe->if_BATs[0].ba_dbatl;
    errorData[49] = iframe->if_Context.ec_GPR[20];
    errorData[50] = iframe->if_Context.ec_GPR[21];
    errorData[51] = iframe->if_Context.ec_GPR[22];
    errorData[52] = iframe->if_Context.ec_GPR[23];
    errorData[53] = iframe->if_BATs[1].ba_dbatu;
    errorData[54] = iframe->if_BATs[1].ba_dbatl;
    errorData[55] = iframe->if_Context.ec_GPR[24];
    errorData[56] = iframe->if_Context.ec_GPR[25];
    errorData[57] = iframe->if_Context.ec_GPR[26];
    errorData[58] = iframe->if_Context.ec_GPR[27];
    errorData[59] = iframe->if_BATs[2].ba_dbatu;
    errorData[60] = iframe->if_BATs[2].ba_dbatl;
    errorData[61] = iframe->if_Context.ec_GPR[28];
    errorData[62] = iframe->if_Context.ec_GPR[29];
    errorData[63] = iframe->if_Context.ec_GPR[30];
    errorData[64] = iframe->if_Context.ec_GPR[31];
    errorData[65] = iframe->if_BATs[3].ba_dbatu;
    errorData[66] = iframe->if_BATs[3].ba_dbatl;

    struct MsgFrame* myFrame = KCreateMsgFramePPC(PowerPCBase);
    myFrame->mf_Identifier = ID_CRSH;
    KSendMsgFramePPC(PowerPCBase, myFrame);

    PowerPCBase->pp_ThisPPCProc->tp_Task.tc_State = TS_REMOVED;
    PowerPCBase->pp_ThisPPCProc->tp_Flags |= TASKPPCF_CRASHED;
    RemovePPC((struct Node*)PowerPCBase->pp_ThisPPCProc->tp_TaskPtr);
    SwitchPPC(PowerPCBase, iframe);
}

#endif
#endif