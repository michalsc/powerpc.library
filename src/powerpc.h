#ifndef _234234_POWERPC_H
#define _234234_POWERPC_H

#include <exec/lists.h>
#include <powerpc/powerpc.h>

#include "libstructs.h"
#include "msgframe.h"

#define PPC_PRIORITY     0
#define PPC_VERSION      17
#define PPC_REVISION     15

struct PrivatePPCBase {
    struct PPCBase      pp_Public;
    struct MinList      pp_MirrorList;

    /* main process */
    struct Process *    pp_MasterProcess;
    struct MsgPort*     pp_MasterControl;

    ULONG               pp_CPUSpeed;
    ULONG               pp_CPUInfo;

    /* Task management */
    struct TaskPPC*     pp_ThisPPCProc;
    struct TaskPPC*     pp_TaskExcept;

    struct MinList      pp_WaitingTasks;
    struct MinList      pp_ReadyTasks;
    struct MinList      pp_NewTasks;
    struct MinList      pp_RemovedTasks;
    struct MinList      pp_AllTasks;
    struct MinList      pp_Snoop;
    struct MinList      pp_Semaphores;
    struct MinList      pp_RemovedExc;
    struct MinList      pp_ReadyExc;
    struct MinList      pp_InstalledExc;
    struct MinList      pp_ExcInterrupt;
    struct MinList      pp_ExcIABR;
    struct MinList      pp_ExcPerfMon;
    struct MinList      pp_ExcTrace;
    struct MinList      pp_ExcSystemCall;
    struct MinList      pp_ExcDecrementer;
    struct MinList      pp_ExcFPUn;
    struct MinList      pp_ExcProgram;
    struct MinList      pp_ExcAlign;
    struct MinList      pp_ExcIAccess;
    struct MinList      pp_ExcDAccess;
    struct MinList      pp_ExcMCheck;
    struct MinList      pp_ExcSysMan;
    struct MinList      pp_ExcTherMan;
    struct MinList      pp_WaitTime;
    struct MinList      pp_Ports;
    struct MinList      pp_MsgQueue;
    struct LibSema      pp_SemWaitList;
    struct LibSema      pp_SemTaskList;
    struct LibSema      pp_SemSemList;
    struct LibSema      pp_SemPortList;
    struct LibSema      pp_SemSnoopList;
    struct LibSema      pp_SemMemory;


    LONG                pp_LowActivityPri;
    ULONG               pp_IdSysTasks;
    ULONG               pp_IdUsrTasks;


    UBYTE               pp_BusyCounter;

    /* Placeholder for NULL-Task context */
    APTR                pp_iFrame;
    ULONG               pp_pvr;

    volatile UBYTE      pp_ExceptionMode;

    LONG                pp_LowActivityPriOffset;
    ULONG               pp_BusClock;
    ULONG               pp_Quantum;
    ULONG               pp_StdQuantum;
    ULONG               pp_NumRun68k;

    struct MinList      pp_TaskLinks;

    /* Private per core data */
    struct MFSide       pp_SideM68k;
    UBYTE               pp_PadM68k[64 - sizeof(struct MFSide)];
    struct MFSide       pp_SidePPC;
    UBYTE               pp_PadPPC[64 - sizeof(struct MFSide)];
};

_Static_assert(sizeof(struct MFSide) <= 64, "MFSide fits one line");
_Static_assert(offsetof(struct PrivatePPCBase, pp_SidePPC) -
               offsetof(struct PrivatePPCBase, pp_SideM68k) == 64, "sides on separate lines");

#if defined(__mc68000__)
#  define pp_MFSelf   pp_SideM68k
#  pragma GCC poison  pp_SidePPC
#else
#  define pp_MFSelf   pp_SidePPC
#  pragma GCC poison  pp_SideM68k
#endif

#define RED_ZONE_SIZE           256
#define STACK_ALLOC_SIZE        (RED_ZONE_SIZE + 32)

#define LIB_POSSIZE             sizeof(struct PrivatePPCBase)
#define LIB_NEGSIZE             ((NEGSIZE + 3) & -4)

#define STATUS_IDLE             0x69646c65
#define STATUS_MSG              0x006d7367
#define STATUS_ACK              0x0061636b

enum XMsgType {
    XMSG_SIGNAL_M68K_TASK = 1,
    XMSG_CAUSE,
    XMSG_CALL_M68K_IN_SUPERSTATE,
    XMSG_KPRINTF,
};

struct XMessage {
    enum XMsgType id;
    union {
        struct {
            struct Task*        task;
            ULONG               sigset;
        } xmSignalM68kTask;
        struct {
            struct Interrupt*   interrupt;
        } xmCause;
        struct {
            ULONG               code;
            ULONG               offset;
            ULONG               dn[8];
            ULONG               an[7];
        } xmCallM68kInSuperstate;
        struct {
            const char *        msg;
            void *              args;
        } xmKPrintF;
    };
};

#endif /* _234234_POWERPC_H */
