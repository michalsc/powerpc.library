#ifndef _SPINLOCK_H
#define _SPINLOCK_H

#include <stdbool.h>

typedef struct {
    volatile char lock;
} spinlock_t;

static inline void spinlock_init(spinlock_t *s) {
    s->lock = 0;
}

static inline void spinlock_acquire(spinlock_t *s) {
    char result;
    
    /* TAS.B atomically tests and sets bit 7 of the byte
     * Sets Z flag if byte was 0 (unlocked)
     */
    do {
        __asm__ __volatile__(
            "tas.b  (%1)\n\t"
            "sne    %0"          /* result = 1 if lock was held (Z clear) */
            : "=d" (result)
            : "a" (&s->lock)
            : "cc", "memory"
        );
        
        if (result) {
            /* Lock was held, spin with low-power hint */
            __asm__ __volatile__("nop");
        }
    } while (result);
}

static inline void spinlock_release(spinlock_t *s) {
    /* Memory barrier to ensure all critical section ops complete
     * before releasing the lock */
    __asm__ __volatile__(
        "move.b %0,(%1)"
        :
        : "d" (0), "a" (&s->lock)
        : "memory"
    );
}

static inline bool spinlock_try_acquire(spinlock_t *s) {
    char result;
    
    __asm__ __volatile__(
        "tas.b  (%1)\n\t"
        "seq    %0\n\t"          /* result = 0xFF if we got lock (Z set) */
        "neg.b  %0"              /* Convert 0xFF to 0x01, 0x00 stays 0x00 */
        : "=d" (result)
        : "a" (&s->lock)
        : "cc", "memory"
    );
    
    /* Return true if we acquired the lock */
    return result != 0;
}

#endif /* _SPINLOCK_H */
