/*
 * Arch-specific interrupt processing logic constants.
 *
 */

#ifndef __ARCH_INTERRUPT_H__
#define __ARCH_INTERRUPT_H__ 

#include <eza/arch/asm.h>
#include <eza/arch/types.h>

#define HZ  1000 /* Timer frequency. */
#define NUM_IRQS  16  /* Maximum number of hardware IRQs in the system. */
#define IRQ_BASE 0x20 /* First vector in IDT for IRQ #0. */

/* AMD 64 interrupt/exception stack frame */
typedef struct __interrupt_stack_frame {
  uint64_t rip, cs;
  uint64_t rflags, old_rsp, old_ss;
} interrupt_stack_frame_t;

typedef struct __interrupt_stack_frame_err {
  uint64_t error_code;
  uint64_t rip, cs;
  uint64_t rflags, old_rsp, old_ss;
} interrupt_stack_frame_err_t;

#endif

