#ifndef _SUPPORT_H
#define _SUPPORT_H

#include <exec/types.h>
#include "../src/libstructs.h"

#define __used__ __attribute__((__used__))

void kprintf(const char * format, ...);
int strcmp(const char *s1, const char *s2);
char * strcpy(char *s1, const char *s2);

#define MSR_LE      0x000001
#define MSR_RI      0x000002
#define MSR_DR      0x000010
#define MSR_IR      0x000020
#define MSR_IP      0x000040
#define MSR_FE1     0x000100
#define MSR_BE      0x000200
#define MSR_SE      0x000400
#define MSR_FE0     0x000800
#define MSR_ME      0x001000
#define MSR_FP      0x002000
#define MSR_PR      0x004000
#define MSR_EE      0x008000
#define MSR_ILE     0x010000
#define MSR_POW     0x040000

#define SPR_BASEREG 944

static inline void sync()
{
    asm volatile("sync":::"memory");
}

static inline void lwsync()
{
    asm volatile("lwsync":::"memory");
}

static inline void isync()
{
    asm volatile("isync");
}

static inline APTR getBASE()
{
    APTR base;
    asm volatile("mfspr %0, %i":"=r"(base):"i"(SPR_BASEREG));
    return base;
}

static inline void setBASE(APTR base)
{
    asm volatile("mtspr %0, %1"::"i"(SPR_BASEREG), "r"(base));
}

static inline ULONG getDEC()
{
    ULONG dec;
    asm volatile("mfdec %0, 22":"=r"(dec));
    return dec;
}
static inline void setDEC(ULONG dec)
{
    asm volatile("mtdec %0"::"r"(dec));
}

static inline ULONG getCNTFRQ()
{
    ULONG cntfrq;
    asm volatile("mfspr %0, 904":"=r"(cntfrq));
    return cntfrq;
}

static inline ULONG getPVR()
{
    ULONG pvr;
    asm volatile("mfspr %0, 287":"=r"(pvr));
    return pvr;
}

static inline ULONG getTBL()
{
    ULONG tbl;
    asm volatile("mftbl %0":"=r"(tbl));
    return tbl;
}

static inline ULONG getTBU()
{
    ULONG tbl;
    asm volatile("mftbu %0":"=r"(tbl));
    return tbl;
}

static inline ULONG getMSR()
{
    ULONG msr;
    asm volatile("mfmsr %0":"=r"(msr));
    return msr;
}

static inline void setMSR(ULONG msr)
{
    asm volatile("mtmsr %0; isync"::"r"(msr));
}

static inline ULONG SystemCall(struct SysCall *sysCall)
{
    register ULONG ret asm("r3");
    register APTR sc asm("r3") = sysCall;

    asm volatile("sc":"+r"(ret):"r"(sc):"cr0", "memory");

    return ret;
}

#endif /* _SUPPORT_H */
