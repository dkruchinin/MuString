#ifndef __ARCH_BUG_H__
#define __ARCH_BUG_H__

static always_inline void BUG(void)
{
  __asm__ volatile("ud2\n");
}

#endif /* __ARCH_BUG_H__ */
