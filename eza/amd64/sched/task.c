
#include <eza/task.h>
#include <eza/arch/context.h>
#include <mlibc/string.h>
#include <eza/arch/page.h>

status_t arch_copy_process(task_t *parent,task_t *newtask,void *arch_ctx,
                           task_creation_flags_t flags)
{
  return 0;
}

status_t kernel_thread(void (*fn)(void *), void *data)
{
  uintptr_t delta;
  char stack[1024];
  regs_t *regs = (regs_t *)(&stack[1024] - sizeof(regs_t));
  char *rsp = (char *)regs;
  char *fsave;
  uint64_t flags;

  /* 0x1000 means 'any page-aligned address'. */
  delta = (0x1000 - sizeof(regs_t)) - ((0x1000 - sizeof(regs_t)) & 0xe00);
  delta -= 512;

  /* Prepare a fake CPU-saved context */
  memset( regs, 0, sizeof(regs_t) );
  regs->old_ss = 0;
  regs->old_rsp = NULL; /* Will be initialized later. */

  __asm__ volatile (
    "pushfq\n"
    "popq %0\n"
    : "=r" (flags) );

  regs->rflags = flags;
  regs->cs = gdtselector(KTEXT_DES);
  regs->rip = 0;

  kprintf( "Sizeof (regs_t): %d, delta: %d\n",
           sizeof(regs_t), delta );

  return 0;
}

