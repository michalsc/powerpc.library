#ifndef _POWERPC_PRIVATE_H
#define _POWERPC_PRIVATE_H

#include <common/compiler.h>

void kprintf(REGARG(const char * msg, "a0"), REGARG(void * args, "a1"));

#define bug(string, ...) \
    do { ULONG args[] = {0, __VA_ARGS__}; kprintf(string, &args[1]); } while(0)


#endif /* _POWERPC_PRIVATE_H */
