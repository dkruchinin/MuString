#include <kernel/vm.h>
#include <mlibc/string.h>

status_t copy_to_user(void *dest,void *src,ulong_t size)
{
  memcpy(dest,src,size);
  return 0;
}

status_t copy_from_user(void *dest,void *src,ulong_t size)
{
  memcpy(dest,src,size);
  return 0;
}

status_t copy_user(void *dest,void *src,ulong_t size)
{
  status_t r;

  /*
   *
   */
  __asm__ __volatile__(
    "0: cmp $0,%%rdx\n"                               \
    "jz 50f\n"                                        \
    "movb (%%rsi), %%al\n"                            \
    "movb %%al,(%%rdi)\n"                             \
    "incq %%rdi\n"                                    \
    "incq %%rsi\n"                                    \
    "decq %%rdx\n"                                    \
    "jmp 0b\n"                                        \
    "50:\n"                                           \
    "movq %%rbx, %%rax\n"                             \
    "orq %%rcx, %%rax\n"                              \
    "jz 1000f\n"                                      \
    "cmpq $0, %%rcx\n"                                \
    "jz 100f\n"                                       \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "decq %%rcx\n"                                    \
    "jmp 50b\n"                                       \
    "100: cmpq $32,%%rbx\n"                           \
    "jc 200f\n"                                       \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "movq (%%rsi),%%rax\n"                            \
    "movq %%rax,(%%rdi)\n"                            \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "subq $32,%%rbx\n"                                \
    "200: cmpq $16, %%rbx\n"                          \
    "1000:\n"                                         \
    :"=a"(r)                                          \
    : "b"((size-(size&7)) & 0x3F), "c"((size-(size&7)) >> 6),   \
      "D"(dest), "S"(src),             \
      "d"(size & 7)                    \
    );
  
  return r;
}
