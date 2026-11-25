#include <proto/exec.h>
#include "asmstubs.h"

static void __attribute__((used)) __stub_run68k_code()
{
asm volatile(
"           .globl _Run68KCode                      \n"
"_Run68KCode:                                       \n"
"           movem.l d0-a6,-(a7)                     \n" //  ;a0 = PPStruct  a6 = SysBase
"           btst #%c[FPU_FLAG],%c[AttnFlags]+1(a6)  \n"
"           beq.s NoFPU1                            \n"

"           fmove.d fp0,-(a7)                       \n"
"           fmove.d fp1,-(a7)                       \n"
"           fmove.d fp2,-(a7)                       \n"
"           fmove.d fp3,-(a7)                       \n"
"           fmove.d fp4,-(a7)                       \n"
"           fmove.d fp5,-(a7)                       \n"
"           fmove.d fp6,-(a7)                       \n"
"           fmove.d fp7,-(a7)                       \n"

"           lea %c[PP_FREGS](a0),a2                 \n"

"           fmove.d (a2)+,fp0                       \n"
"           fmove.d (a2)+,fp1                       \n"
"           fmove.d (a2)+,fp2                       \n"
"           fmove.d (a2)+,fp3                       \n"
"           fmove.d (a2)+,fp4                       \n"
"           fmove.d (a2)+,fp5                       \n"
"           fmove.d (a2)+,fp6                       \n"
"           fmove.d (a2)+,fp7                       \n"

"NoFPU1:    link a1,#-16                            \n"
"           lea -16(a1),a1                          \n"
"           move.l a0,-(a7)                         \n"
"           move.l a0,a5                            \n"
"           tst.l %c[PP_STACK](a0)                  \n"

"           beq NoStckPtr                           \n"

"           move.l %c[PP_STACKSIZE](a0),d0          \n"
"           beq NoStckPtr                           \n"

"           move.l a1,a2                            \n"
"           move.l a1,a3                            \n"
"           move.l #0x2e7a0008,(a2)+                \n"
"           move.w #0x4ef9,(a2)+                    \n"
"           move.l #xBack,(a2)+                     \n"
"           move.l a7,(a2)+                         \n"

"           move.l %c[PP_STACK](a0),a0              \n"
"           lea 24(a0),a0                           \n" // Offset needs to be 24 according to Run68K docs
"           addq.l #3,d0                            \n"
"           and.l #0xfffffffc,d0                    \n" // Make it 4 aligned
"           move.l a7,a1                            \n"
"           sub.l d0,a1                             \n"
"           move.l a1,a7                            \n"
"           jsr %c[LVOCopyMem](a6)                  \n"
"           jsr %c[LVOCacheClearU](a6)              \n"

"           move.l a3,-(a7)                         \n" // Return function
"           bra StckPtr                             \n"
"NoStckPtr: pea xBack(pc)                           \n"
"StckPtr:   move.l %c[PP_CODE](a5),a0               \n"
"           add.l %c[PP_OFFSET](a5),a0              \n"
"           move.l a0,-(a7)                         \n"
"           lea %c[PP_REGS](a5),a6                  \n"
"           movem.l (a6)+,d0-a5                     \n"
"           move.l (a6),a6                          \n"
"           rts                                     \n"

"xBack:     move.l a6,-(a7)                         \n"
"           move.l 4(a7),a6                         \n"
"           lea %c[PP_REGS]+56(a6),a6               \n"
"           movem.l d0-a5,-(a6)                     \n"
"           move.l a6,a0                            \n"
"           move.l (a7)+,a6                         \n"
"           move.l a6,56(a0)                        \n" // a6 was decremented to the start of the block, and a0 is that: the a6 slot is 56 from it
                                                        // (PP_REGS+56 counted PP_REGS twice and wrote PP_FRegs[2]; REVIEW-LIB.md M5, 17.13b
                                                        // powerpc.s:4032-4051, WarpOS powerpc.s:4242-4249)

"           move.l 4.w,a6                           \n"
"           move.l (a7)+,a3                         \n"
"           btst #%c[FPU_FLAG],%c[AttnFlags]+1(a6)  \n"
"           beq.s NoFPU2                            \n"

"           lea %c[PP_FREGS](a3),a3                 \n"
"           fmove.d fp0,(a3)+                       \n"
"           fmove.d fp1,(a3)+                       \n"
"           fmove.d fp2,(a3)+                       \n"
"           fmove.d fp3,(a3)+                       \n"
"           fmove.d fp4,(a3)+                       \n"
"           fmove.d fp5,(a3)+                       \n"
"           fmove.d fp6,(a3)+                       \n"
"           fmove.d fp7,(a3)+                       \n"

"NoFPU2:    move.l a7,a1                            \n"
"           lea 16(a1),a1                           \n"
"           unlk a1                                 \n"
"           move.l 4.w,a6                           \n"
"           btst #%c[FPU_FLAG],%c[AttnFlags]+1(a6)  \n"
"           beq.s NoFPU3                            \n"

"           fmove.d (a7)+,fp7                       \n"
"           fmove.d (a7)+,fp6                       \n"
"           fmove.d (a7)+,fp5                       \n"
"           fmove.d (a7)+,fp4                       \n"
"           fmove.d (a7)+,fp3                       \n"
"           fmove.d (a7)+,fp2                       \n"
"           fmove.d (a7)+,fp1                       \n"
"           fmove.d (a7)+,fp0                       \n"

"NoFPU3:    movem.l (a7)+,d0-a6                     \n"
"           rts                                     \n"
:
:[FPU_FLAG]"i"(AFB_68881),
 [AttnFlags]"i"(offsetof(struct ExecBase, AttnFlags)),
 [PP_FREGS]"i"(offsetof(struct PPCArgs, PP_FRegs)),
 [PP_STACK]"i"(offsetof(struct PPCArgs, PP_Stack)),
 [PP_STACKSIZE]"i"(offsetof(struct PPCArgs, PP_StackSize)),
 [PP_CODE]"i"(offsetof(struct PPCArgs, PP_Code)),
 [PP_OFFSET]"i"(offsetof(struct PPCArgs, PP_Offset)),
 [PP_REGS]"i"(offsetof(struct PPCArgs, PP_Regs)),
 [LVOCopyMem]"i"(-624),
 [LVOCacheClearU]"i"(-636)
 
);

__builtin_unreachable();
}
