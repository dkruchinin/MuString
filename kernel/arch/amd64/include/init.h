#ifndef __MSTRING_ARCH_INIT__
#define __MSTRING_ARCH_INIT__

#include <mstring/types.h>
#include <arch/boot.h>

INITCODE void arch_prepare(void);
INITCODE void arch_init(void);
INITCODE void arch_timers_init(void);
INITCODE void arch_cpu_init(cpu_id_t cpu);

extern void syscall_point(void);
extern int __bss_start, __bss_end,
    __bootstrap_start, __bootstrap_end, _kernel_end;

#endif /* __MSTRING_ARCH_INIT__ */
