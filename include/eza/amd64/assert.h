#ifndef __ARCH_ASSERT_H__
#define __ARCH_ASSERT_H__

#include <mlibc/types.h>

#define ASSERT_MAGIC 0xDEADBEAF
static always_inline void ASSERT_LOW_LEVEL(const char *assert_msg, const char *file,
                                           const char *function, int line)
{
  __asm__ volatile("movq %1, %%r10\n\t"
                   "movq %2, %%r11\n\t"
                   "movq %3, %%r12\n\t"
                   "movq %4, %%r13\n\t"
                   "ud2"                   
                   :: "a" (ASSERT_MAGIC), "m" (assert_msg),
                   "m" (file), "m" (function), "Ir" ((long)line)
                   : "memory");
}

#endif /* __ARCH_ASSERT_H__ */
