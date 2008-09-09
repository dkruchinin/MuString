#ifndef __KSTACK_H__
#define __KSTACK_H__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/arch/page.h>
#include <ds/list.h>

#define KS_AF_DONT_EXPAND  0x1  /* Don't allocate additional bitmap arrays. */

#define KERNEL_STACK_PAGES  4   /* Pages per kernel stack. */
#define KERNEL_STACK_SIZE  (KERNEL_STACK_PAGES * PAGE_SIZE)
#define KERNEL_STACK_GAP  PAGE_SIZE
#define KERNEL_STACK_STEP (8 * PAGE_SIZE)
#define KERNEL_STACK_MASK  ~((uintptr_t)KERNEL_STACK_STEP - 1)

#define KERNEL_STACK_PAGE_FLAGS  0

#define BITMAP_INIT_PATTERN ~((uintptr_t)0) /* Initial bitmap pattern. */
#define BITMAP_ADDRESS_CHECK_MASK (0x7) /* For checking bitmap alignment */
#define INVALID_STACK_ID  INVALID_BIT_INDEX

/* Starting address for kernel stack allocation (will grow down) */
extern uintptr_t starting_kernel_stack_address;

/* NOTE: This structure _must_ be 64-bit aligned ! */
typedef struct __kernel_stack_chunk {
  list_node_t l;
  uint32_t total_items, free_items;
  uintptr_t high_address, low_address; /* Range of virtual memory this chunk covers. */
  uint64_t bitmap[];    /* Must be 64-bit aligned ! */
} kernel_stack_chunk_t;

typedef struct __kernel_stack {
  bit_idx_t id;
  uintptr_t high_address, low_address;
} kernel_stack_t;

typedef struct __kernel_stack_allocator_context {
  spinlock_t lock;
  uint32_t total_items, free_items, num_chunks;
  list_head_t chunks;
} kernel_stack_allocator_context_t;

int allocate_kernel_stack(kernel_stack_t *stack);
int free_kernel_stack(uint32_t id);
void initialize_kernel_stack_allocator(void);

#define BITMAP_ENTRIES_COUNT ( (PAGE_SIZE - (sizeof(kernel_stack_chunk_t))) / sizeof(uint64_t) )

#endif

