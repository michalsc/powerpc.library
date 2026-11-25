/*
    xring.h - M68k <-> PowerPC message rings for Emu68 (proposal, v3)

    One ring per direction. A slot holds a 32-bit pointer to a message;
    0 means "empty", so no shared head/tail indices are needed
    (FastForward-style queue). Each side keeps its own index privately.

        producer:  slot[t] == 0 ?    -> RELEASE, slot[t] = msg, t++
        consumer:  p = slot[h] != 0 ? -> ACQUIRE, slot[h] = 0, h++

    Notification is separate and optional:

      - producer pushes one or more messages, then may kick the other
        CPU (xring_kick_ppc / xring_kick_m68k). Kicks raised before the
        receiver acknowledges collapse into one interrupt (level flag).
      - receiver: ACK, FULL fence, drain until empty. Ack-before-drain
        is what prevents lost wakeups: a push the drain misses was
        published before its kick, and that kick lands after our ack.
      - a consumer may also poll whenever it wants, with or without
        interrupts (PPC: poll with MSR[EE]=0; m68k: let the server only
        ack and Signal() a dispatcher task that polls).

    Serialisation: one producer and one consumer per ring per CPU. If
    several tasks/interrupts on one CPU produce (or consume), wrap the
    call in Disable()/Enable() (m68k) or MSR[EE]=0 (PPC).

    Compiled by m68k-amigaos-gcc (int align 2) and by the PPC compiler
    (int align 4) - layout is explicit and checked.
*/

#ifndef XRING_H
#define XRING_H

#include <stdint.h>

#define XRING_LINE      64u
#define XRING_SLOTS     256u                /* power of two, 1 KiB */
#define XRING_MASK      (XRING_SLOTS - 1u)

struct XRing {
    volatile uint32_t   slot[XRING_SLOTS];          /* 0 = empty */
};

/* Private per-side state, keep it in the side's own base, not here */
struct XProducer { uint32_t tail; };
struct XConsumer { uint32_t head; };

_Static_assert(sizeof(struct XRing) % XRING_LINE == 0, "ring must be whole cache lines");

/* ------------------------------------------------------------------ */
/* Barriers, mapped to what the Emu68 JITs emit                         */
/* ------------------------------------------------------------------ */
#if defined(__mc68000__)
/* NOP -> DSB SY in Emu68: the only fence available, always full */
#  define XR_ACQUIRE()  __asm__ __volatile__("nop" ::: "memory")
#  define XR_RELEASE()  __asm__ __volatile__("nop" ::: "memory")
#  define XR_FULL()     __asm__ __volatile__("nop" ::: "memory")
#elif defined(__PPC__) || defined(__powerpc__)
/* fences rather than acquire loads: GCC's lwz;cmp;bne;isync ends the
   Emu68 translation block at isync (ISB + MARKER_STOP) */
#  define XR_ACQUIRE()  __atomic_thread_fence(__ATOMIC_ACQUIRE)   /* lwsync -> dmb ish */
#  define XR_RELEASE()  __atomic_thread_fence(__ATOMIC_RELEASE)   /* lwsync -> dmb ish */
#  define XR_FULL()     __atomic_thread_fence(__ATOMIC_SEQ_CST)   /* sync   -> dmb sy  */
#else
#  error "xring.h: unknown CPU"
#endif

static inline void xring_init(struct XRing *r)
{
    volatile uint32_t *s = r->slot;
    for (uint32_t i = 0; i < XRING_SLOTS; i++) {
        *s++ = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Producer                                                             */
/* ------------------------------------------------------------------ */

/* msg must be non-zero. Returns 0 if the ring is full. */
static inline int xring_push(struct XRing *r, struct XProducer *p, uint32_t msg)
{
    volatile uint32_t *s = &r->slot[p->tail & XRING_MASK];

    if (*s != 0)
        return 0;

    XR_RELEASE();               /* message body before the pointer */
    *s = msg;
    p->tail++;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Consumer                                                             */
/* ------------------------------------------------------------------ */

/* Returns the next message pointer, or 0 if the ring is empty. */
static inline uint32_t xring_poll(struct XRing *r, struct XConsumer *c)
{
    volatile uint32_t *s = &r->slot[c->head & XRING_MASK];
    uint32_t msg = *s;

    if (msg == 0)
        return 0;

    XR_ACQUIRE();               /* pointer before message body */
    *s = 0;                     /* slot back to producer */
    c->head++;
    return msg;
}

/* ------------------------------------------------------------------ */
/* Interrupts, using the existing JIT_CONTROL2 bits only                */
/* ------------------------------------------------------------------ */
#if defined(__mc68000__)

#define JC2_RING_PPC    0x80000000u     /* write 1: raise PPC external IRQ */
#define JC2_PPC_PENDING 0x40000000u     /* read: IRQ from PPC; write 1: ack */
#define JC2_ARM_PENDING 0x20000000u     /* read: ARM IRQ;      write 1: ack */
#define JC2_CTRL_MASK   0x1fffffffu

/*
    Supervisor() target - expand XRING_DEFINE_KICK_PPC in exactly one .c
    file, then call Supervisor((APTR)xring_kick_ppc) after pushing.
    The nop is the release fence (ring stores before the flag store).
    The read-modify-write MUST drop bits 29/30, otherwise a pending ARM
    or PPC interrupt is acknowledged and lost.
*/
#define XRING_DEFINE_KICK_PPC                       \
    __asm__(                                        \
        "   .text                           \n"     \
        "   .globl  _xring_kick_ppc         \n"     \
        "_xring_kick_ppc:                   \n"     \
        "   move.l  d0,-(a7)                \n"     \
        "   nop                             \n"     \
        "   movec   #0x1e0,d0               \n"     \
        "   and.l   #0x1fffffff,d0          \n"     \
        "   bset    #31,d0                  \n"     \
        "   movec   d0,#0x1e0               \n"     \
        "   move.l  (a7)+,d0                \n"     \
        "   rte                             \n");
extern void xring_kick_ppc(void);

/*
    First thing in the INTB_PORTS server (supervisor). Returns 0 if the
    interrupt was not from PPC. On return 1 the flag is acked and fenced;
    now drain the ring until xring_poll() returns 0 (or Signal() the
    task that does it).
*/
static inline int xring_ack_ppc_irq(void)
{
    uint32_t v;

    __asm__ __volatile__("movec #0x1e0,%0" : "=d"(v) : : "memory");
    if (!(v & JC2_PPC_PENDING))
        return 0;

    v = (v & JC2_CTRL_MASK) | JC2_PPC_PENDING;
    __asm__ __volatile__(
        "movec  %0,#0x1e0   \n\t"       /* INTF.PPC = 0 */
        "nop                \n\t"       /* ack store before ring loads */
        : : "d"(v) : "memory");
    return 1;
}

#else /* PPC */

/* Supervisor only. Call after pushing. */
static inline void xring_kick_m68k(void)
{
    uint32_t v;

    XR_RELEASE();               /* ring stores before the flag store */
    __asm__ __volatile__("mfspr %0,921" : "=r"(v));
    __asm__ __volatile__("mtspr 921,%0" : : "r"(v | 0x80000000u) : "memory");
}

/*
    First thing in the vector 0x500 handler. Emu68 clears INT.EXT
    itself on delivery, so only the fence is needed before draining.
*/
static inline void xring_ack_m68k_irq(void)
{
    XR_FULL();                  /* INT.EXT clear before ring loads */
}

#endif

#endif /* XRING_H */
