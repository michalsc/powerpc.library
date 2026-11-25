/*
    msgframe.h - MsgFrame transport between the M68k and PowerPC sides
    (proposal, step 1: header only, nothing wired in yet)

    Same API shape as the PCI powerpc.library (Create/Send/Get/Free),
    built on four SPSC pointer rings from xring.h:

        post_to_ppc   m68k -> PPC   frames for the PPC          (kick)
        post_to_m68k  PPC  -> m68k  frames for the m68k         (kick)
        free_to_ppc   m68k -> PPC   PPC-owned frames going home (no kick)
        free_to_m68k  PPC  -> m68k  m68k-owned frames going home(no kick)

    Ownership:
      - every frame belongs to the pool of one side (by address range)
      - a frame may travel freely; a reply can reuse the request frame
      - FreeMsgFrame puts an own frame on the private free list, a peer's
        frame on the free ring towards its owner
      - after mf_send()/mf_free() the caller must not touch the frame

    Every ring has >= frames of both pools together, so a push can never
    fail: send and free never block. Only mf_create() can come back empty.

    Locking: none inside. One producer and one consumer per ring end per
    CPU - the caller wraps calls in Disable()/Enable() (m68k) or runs
    with MSR[EE]=0 (PPC) when tasks and interrupts share an end.
*/

#ifndef MSGFRAME_H
#define MSGFRAME_H

#include <stdint.h>
#include <stddef.h>

/*
    The 68k layout of struct Node/Message (2-byte alignment) is the
    shared one. A PPC compiler aligns ln_Name to 4 and makes struct
    Message 24 bytes instead of 20 - pack(2) on both sides.
*/
#pragma pack(push,2)
#include <exec/ports.h>
#include <powerpc/powerpc.h>
#pragma pack(pop)

#include "libstructs.h"
#include "xring.h"

_Static_assert(sizeof(struct Message) == 20, "struct Message must have 68k layout");
_Static_assert(sizeof(struct MsgFrame) == 192, "MsgFrame must be 192 bytes");
_Static_assert(offsetof(struct MsgFrame, mf_PPCArgs) == 32, "mf_PPCArgs offset");
_Static_assert(offsetof(struct MsgFrame, mf_PPCArgs.PP_FRegs) % 8 == 0, "PP_FRegs 8-aligned");

#ifndef ID_FREE
#define ID_FREE         0x46524545      /* 'FREE' */
#endif

#define MF_POOL_FRAMES  64u             /* per side */

_Static_assert(XRING_SLOTS >= 2 * MF_POOL_FRAMES, "rings must hold all frames");

/* ------------------------------------------------------------------ */
/* Shared memory (allocated and initialised by the m68k side)          */
/* ------------------------------------------------------------------ */

struct MFShared {
    struct XRing        post_to_ppc;
    struct XRing        post_to_m68k;
    struct XRing        free_to_ppc;
    struct XRing        free_to_m68k;
};

/* Pointer to this goes into the bootstrap mailbox at 0xffefff80 */
#define XBOOT_MAGIC     0x58524E47      /* 'XRNG' */

#define XBOOT_PENDING   0
#define XBOOT_OK        1
#define XBOOT_BAD_MAGIC 2
#define XBOOT_BAD_HASH  3

struct XBoot {
    uint32_t            magic;
    uint32_t            git_hash;
    uint32_t            ppcbase;        /* struct PrivatePPCBase * */
    uint32_t            shared;         /* struct MFShared *, 64-byte aligned */
    uint32_t            ppc_pool;       /* PPC-owned frames, 64-byte aligned */
    uint32_t            ppc_frames;
    volatile uint32_t   status;         /* written by PPC only */
};

/* ------------------------------------------------------------------ */
/* Per-CPU private state                                                */
/* ------------------------------------------------------------------ */

struct MFSide {
    struct XRing *      post_out;       /* my frames to the peer */
    struct XRing *      post_in;        /* peer's frames to me */
    struct XRing *      free_out;       /* peer-owned frames going home */
    struct XRing *      free_in;        /* my frames coming home */
    struct XProducer    post_out_p;
    struct XConsumer    post_in_c;
    struct XProducer    free_out_p;
    struct XConsumer    free_in_c;
    uint32_t            pool_lo;        /* my pool: [pool_lo, pool_hi) */
    uint32_t            pool_hi;
    struct MsgFrame *   free_list;      /* linked through mn_Node.ln_Succ */
};

static inline int mf_is_mine(const struct MFSide *s, const struct MsgFrame *f)
{
    uint32_t a = (uint32_t)f;
    return a >= s->pool_lo && a < s->pool_hi;
}

static inline void mf_list_put(struct MFSide *s, struct MsgFrame *f)
{
    f->mf_Message.mn_Node.ln_Succ = (struct Node *)s->free_list;
    s->free_list = f;
}

/*
    m68k side only, before the XBoot pointer is published: all four rings
    empty. The publish itself (nop; move.l boot,0xffefff80) orders it.
*/
static inline void mf_shared_init(struct MFShared *sh)
{
    xring_init(&sh->post_to_ppc);
    xring_init(&sh->post_to_m68k);
    xring_init(&sh->free_to_ppc);
    xring_init(&sh->free_to_m68k);
}

/* Both sides: wire up own view of the rings, put own pool on the free list. */
static inline void mf_side_init(struct MFSide *s, struct MFShared *sh,
                                struct MsgFrame *pool, uint32_t frames)
{
#if defined(__mc68000__)
    s->post_out = &sh->post_to_ppc;
    s->post_in  = &sh->post_to_m68k;
    s->free_out = &sh->free_to_ppc;
    s->free_in  = &sh->free_to_m68k;
#else
    s->post_out = &sh->post_to_m68k;
    s->post_in  = &sh->post_to_ppc;
    s->free_out = &sh->free_to_m68k;
    s->free_in  = &sh->free_to_ppc;
#endif
    s->post_out_p.tail = 0;
    s->post_in_c.head  = 0;
    s->free_out_p.tail = 0;
    s->free_in_c.head  = 0;

    s->pool_lo   = (uint32_t)pool;
    s->pool_hi   = (uint32_t)(pool + frames);
    s->free_list = NULL;

    for (uint32_t i = 0; i < frames; i++) {
        pool[i].mf_Identifier = ID_FREE;
        mf_list_put(s, &pool[i]);
    }
}

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/* CreateMsgFrame: a free own frame, not cleared. NULL if none left. */
static inline struct MsgFrame *mf_create(struct MFSide *s)
{
    if (s->free_list == NULL) {
        uint32_t p;
        while ((p = xring_poll(s->free_in, &s->free_in_c)) != 0)
            mf_list_put(s, (struct MsgFrame *)p);
    }

    struct MsgFrame *f = s->free_list;
    if (f)
        s->free_list = (struct MsgFrame *)f->mf_Message.mn_Node.ln_Succ;
    return f;
}

/* SendMsgFrame: hand the frame to the peer. Kick separately if wanted. */
static inline void mf_send(struct MFSide *s, struct MsgFrame *f)
{
    xring_push(s->post_out, &s->post_out_p, (uint32_t)f);   /* cannot fail */
}

/* GetMsgFrame: next frame from the peer, NULL if none. */
static inline struct MsgFrame *mf_get(struct MFSide *s)
{
    return (struct MsgFrame *)xring_poll(s->post_in, &s->post_in_c);
}

/* FreeMsgFrame: back to its owner. Never blocks, no kick. */
static inline void mf_free(struct MFSide *s, struct MsgFrame *f)
{
    f->mf_Identifier = ID_FREE;

    if (mf_is_mine(s, f))
        mf_list_put(s, f);
    else
        xring_push(s->free_out, &s->free_out_p, (uint32_t)f); /* cannot fail */
}

struct PrivatePPCBase;

#if defined(__mc68000__)
struct MsgFrame *CreateMsgFrameNoClear(struct PrivatePPCBase *PPCBase);
struct MsgFrame *CreateMsgFrame(struct PrivatePPCBase *PPCBase);
struct MsgFrame* GetMsgFrame(struct PrivatePPCBase* PPCBase);
void SendMsgFrame(struct PrivatePPCBase* PPCBase, struct MsgFrame* msgFrame);
void FreeMsgFrame(struct PrivatePPCBase* PPCBase, struct MsgFrame* msgFrame);
#else
struct MsgFrame *CreateMsgFramePPC(struct PrivatePPCBase *PPCBase);
struct MsgFrame* GetMsgFramePPC(struct PrivatePPCBase* PPCBase);
void SendMsgFramePPC(struct PrivatePPCBase* PPCBase, struct MsgFrame* msgFrame);
void FreeMsgFramePPC(struct PrivatePPCBase* PPCBase, struct MsgFrame* msgFrame);
#endif

#endif /* MSGFRAME_H */
