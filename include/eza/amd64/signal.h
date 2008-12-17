#ifndef __ARCH_SIGNAL_H__
#define  __ARCH_SIGNAL_H__

#include <eza/arch/types.h>
#include <eza/arch/bits.h>

#define signal_matches(m,s) arch_bit_test((m),(s))

#define set_signal(m,s)  arch_bit_set((m),(s))

#define clear_signal(m,s)  arch_bit_clear((m),(s))

#endif
