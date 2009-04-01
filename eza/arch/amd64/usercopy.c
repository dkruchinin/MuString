#include <eza/usercopy.h>
#include <mm/vmm.h>
#include <mlibc/string.h>

int copy_user(void *dest,void *src,ulong_t size)
{
  int r;
  
  __asm__ __volatile__(
    "0: cmp $0,%5\n"                                  \
    "jz 50f\n"                                        \
    "601: movb (%4), %%al\n"                          \
    "602: movb %%al,(%3)\n"                           \
    "incq %4\n"                                       \
    "incq %3\n"                                       \
    "decq %5\n"                                       \
    "jmp 0b\n"                                        \
    "50:\n"                                           \
    "movq %1, %%rax\n"                                \
    "orq %2, %%rax\n"                                 \
    "jz 1000f\n"                                      \
    "cmpq $0, %2\n"                                   \
    "jz 100f\n"                                       \
    "603: movq (%4),%%rax\n"                          \
    "604: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "605: movq (%4),%%rax\n"                          \
    "606: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "607: movq (%4),%%rax\n"                          \
    "608: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "609: movq (%4),%%rax\n"                          \
    "610: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "611: movq (%4),%%rax\n"                          \
    "612: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "613: movq (%4),%%rax\n"                          \
    "614: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "615: movq (%4),%%rax\n"                          \
    "616: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "617: movq (%4),%%rax\n"                          \
    "618: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "decq %2\n"                                       \
    "jmp 50b\n"                                       \
    "100: orq %1,%1\n"                                \
    "jz 1000f\n"                                      \
    "cmpq $32,%1\n"                                   \
    "jc 200f\n"                                       \
    "619: movq (%4),%%rax\n"                          \
    "620: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "621: movq (%4),%%rax\n"                          \
    "622: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "623: movq (%4),%%rax\n"                          \
    "624: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "625: movq (%4),%%rax\n"                          \
    "626: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "subq $32,%1\n"                                   \
    "jz 1000f\n"                                      \
    "200: cmpq $16, %1\n"                             \
    "jc 300f\n"                                       \
    "627: movq (%4),%%rax\n"                          \
    "628: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "629: movq (%4),%%rax\n"                          \
    "630: movq %%rax,(%3)\n"                          \
    "addq $8, %3\n"                                   \
    "addq $8, %4\n"                                   \
    "subq $16,%1\n"                                   \
    "jz 1000f\n"                                      \
    "300: movq (%4),%%rax\n"                          \
    "301: movq %%rax,(%3)\n"                          \
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
    ".quad 601b,900b\n"                               \
    ".quad 602b,900b\n"                               \
    ".quad 603b,900b\n"                               \
    ".quad 604b,900b\n"                               \
    ".quad 605b,900b\n"                               \
    ".quad 606b,900b\n"                               \
    ".quad 607b,900b\n"                               \
    ".quad 608b,900b\n"                               \
    ".quad 609b,900b\n"                               \
    ".quad 610b,900b\n"                               \
    ".quad 611b,900b\n"                               \
    ".quad 612b,900b\n"                               \
    ".quad 613b,900b\n"                               \
    ".quad 614b,900b\n"                               \
    ".quad 615b,900b\n"                               \
    ".quad 616b,900b\n"                               \
    ".quad 617b,900b\n"                               \
    ".quad 618b,900b\n"                               \
    ".quad 619b,900b\n"                               \
    ".quad 620b,900b\n"                               \
    ".quad 621b,900b\n"                               \
    ".quad 622b,900b\n"                               \
    ".quad 623b,900b\n"                               \
    ".quad 624b,900b\n"                               \
    ".quad 625b,900b\n"                               \
    ".quad 626b,900b\n"                               \
    ".quad 627b,900b\n"                               \
    ".quad 628b,900b\n"                               \
    ".quad 629b,900b\n"                               \
    ".quad 630b,900b\n"                               \
    ".previous"                                       \
    :"=a"(r)                                          \
    : "r"((size-(size&7)) & 0x3F), "r"((size-(size&7)) >> 6),   \
      "r"(dest), "r"(src),             \
      "r"(size & 7), "a"(0)          \
    );
  
  return r;
}

/* FIXME DK: check if address is not in mandatory mapping area */
int copy_to_user(void *dest,void *src,ulong_t size)
{
  /*if (!valid_user_address_range((uintptr_t)dest, size))
    return -EFAULT;*/
  
  return copy_user(dest,src,size);
}

int copy_from_user(void *dest,void *src,ulong_t size)
{
  /*if (!valid_user_address_range((uintptr_t)src, size))
    return -EFAULT;*/

  return copy_user(dest,src,size);
}
