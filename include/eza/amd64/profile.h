#ifndef __AMD64_PROFILE__
#define  __AMD64_PROFILE__

#define __READ_TIMESTAMP_COUNTER(c)             \
    __asm__ __volatile__(                       \
        "xorq %%rdx,%%rdx\n"                    \
        "xorq %%rax,%%rax\n"                    \
        "rdtsc\n"                               \
        "shl $32, %%rdx\n"                      \
        "orq %%rdx,%%rax\n"                     \
        : "=a"(c):: "%rdx" )

#endif
