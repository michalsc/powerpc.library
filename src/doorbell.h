#ifndef _DOORBELL_H
#define _DOORBELL_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    volatile uint32_t val;
} doorbell_t;

static inline void doorbell_init(doorbell_t *d) {
    d->val = 0;
}

static inline uint32_t doorbell_wait(doorbell_t *d) {
    uint32_t msg;
    uint32_t zero = 0;
    uint32_t success;

    do {
        /* Spin until we see a non-zero value */
        while ((msg = d->val) == 0) {
            __asm__ __volatile__("nop");
        }

        success = 0;
        __asm__ __volatile__(
            "cas.l  %1,%2,(%3)\n\t"  /* CAS Dc,Du,(An): if [An]==Dc → [An]=Du */
            "seq    %0\n\t"          /* success flag = (Z==1)?0xFF:0x00 */
            "neg.b  %0\n\t"          /* 0xFF→1, 0x00→0 */
            : "=d"(success), "+d"(msg)
            : "d"(zero), "a"(&d->val)
            : "cc", "memory"
        );

    } while (!success);

    return msg;
}

static inline void doorbell_send(doorbell_t *d, uint32_t msg) {
    if (msg == 0)
        return;

    __asm__ __volatile__(
        "move.l %0,(%1)"
        :
        : "d"(msg), "a"(&d->val)
        : "memory"
    );
}

#endif /* _DOORBELL_H */