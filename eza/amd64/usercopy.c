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
    "601: movb (%%rsi), %%al\n"                       \
    "602: movb %%al,(%%rdi)\n"                        \
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
    "603: movq (%%rsi),%%rax\n"                       \
    "604: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "605: movq (%%rsi),%%rax\n"                        \
    "606: movq %%rax,(%%rdi)\n"                        \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "607: movq (%%rsi),%%rax\n"                       \
    "608: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "609: movq (%%rsi),%%rax\n"                       \
    "610: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "611: movq (%%rsi),%%rax\n"                       \
    "612: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "613: movq (%%rsi),%%rax\n"                       \
    "614: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "615: movq (%%rsi),%%rax\n"                       \
    "616: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "617: movq (%%rsi),%%rax\n"                       \
    "618: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "decq %%rcx\n"                                    \
    "jmp 50b\n"                                       \
    "100: orq %%rbx,%%rbx\n"                          \
    "jz 1000f\n"                                      \
    "cmpq $32,%%rbx\n"                                \
    "jc 200f\n"                                       \
    "619: movq (%%rsi),%%rax\n"                       \
    "620: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "621: movq (%%rsi),%%rax\n"                       \
    "622: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "623: movq (%%rsi),%%rax\n"                       \
    "624: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "625: movq (%%rsi),%%rax\n"                       \
    "626: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "subq $32,%%rbx\n"                                \
    "jz 1000f\n"                                      \
    "200: cmpq $16, %%rbx\n"                          \
    "jc 300f\n"                                       \
    "627: movq (%%rsi),%%rax\n"                       \
    "628: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "629: movq (%%rsi),%%rax\n"                       \
    "630: movq %%rax,(%%rdi)\n"                       \
    "addq $8, %%rdi\n"                                \
    "addq $8, %%rsi\n"                                \
    "subq $16,%%rbx\n"                                \
    "jz 1000f\n"                                      \
    "300: movq (%%rsi),%%rax\n"                       \
    "301: movq %%rax,(%%rdi)\n"                       \
    "1000: xorq %%rax,%%rax\n"                        \
    "1001:\n"                                         \
    ".section .fixup,\"ax\"\n"                        \
    "900: movq $1, %%rax\n"                           \
    "jmp 1001b\n"                                     \
    ".previous\n"                                     \
    ".section .ex_table,\"a\"\n"                      \
    ".align 8\n"                                      \
    ".quad 300b,900b\n"                               \
    ".quad 301b,900b\n"                               \
    ".previous"                                       \
    :"=a"(r)                                          \
    : "b"((size-(size&7)) & 0x3F), "c"((size-(size&7)) >> 6),   \
      "D"(dest), "S"(src),             \
      "d"(size & 7)                    \
    );
  
  return r;
}
