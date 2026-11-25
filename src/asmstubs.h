#ifndef _ASMSTUBS_H
#define _ASMSTUBS_H

#include <exec/execbase.h>
#include <common/compiler.h>
#include "powerpc.h"

void Run68KCode(REGARG(struct ExecBase* SysBase, "a6"), REGARG(struct PPCArgs* args, "a0"));

#endif /* _ASMSTUBS_H */
