
#include <mm/pagealloc.h>
#include <eza/kernel.h>
#include <eza/arch/bits.h>
#include <eza/errno.h>
#include <mlibc/kprintf.h>
#include <mlibc/stddef.h>
#include <eza/arch/page.h>
#include <eza/kstack.h>

static kernel_stack_allocator_context_t main_stack_ctx;

#define LOCK_STACK_CTX(ctx) 
#define UNLOCK_STACK_CTX(ctx) 

/* Starting address for kernel stack allocation (will grow down) */
uintptr_t starting_kernel_stack_address;

static void initialize_bitmap( uint64_t *bitmap, uint32_t size )
{
  if( ((uintptr_t)bitmap & BITMAP_ADDRESS_CHECK_MASK) != 0 ) {
    kprintf( KO_WARNING "initialize_bitmap(): Non-aligned bitmap used: 0x%X\n",
            bitmap );
  }

  while( size != 0 ) {
    *bitmap++ = BITMAP_INIT_PATTERN;
    size--;
  }
}

static void initialize_stack_chunk( kernel_stack_chunk_t *chunk )
{
  list_init_node(&chunk->l);
  chunk->total_items = chunk->free_items = BITMAP_ENTRIES_COUNT;
  chunk->high_address = chunk->high_address;
  chunk->low_address = starting_kernel_stack_address -
                       (KERNEL_STACK_SIZE + KERNEL_STACK_GAP) * BITMAP_ENTRIES_COUNT;
  initialize_bitmap(chunk->bitmap, BITMAP_ENTRIES_COUNT);
}

static void initialize_stack_allocator_context(kernel_stack_allocator_context_t *ctx)
{
  kernel_stack_chunk_t *ch1 = (kernel_stack_chunk_t *)__alloc_page(0,0);

  if( ch1 == NULL ) {
    panic( "initialize_stack_allocator_context(): Can't initialize stack context !" );
  }

  spinlock_initialize(&ctx->lock, "Main kernel stack context lock");
  list_init_head(&ctx->chunks);
  initialize_stack_chunk(ch1);

  ctx->total_items = ch1->total_items;
  ctx->free_items = ch1->free_items;
  ctx->num_chunks = 1;

  list_add2tail(&ctx->chunks, &ch1->l);
}

int allocate_kernel_stack(kernel_stack_t *stack)
{
  int r = -ENOENT;
  bit_idx_t idx;

  LOCK_STACK_CTX(&main_stack_ctx);

  if(main_stack_ctx.free_items > 0) {
      kernel_stack_chunk_t *chunk = container_of( list_node_first(&main_stack_ctx.chunks),
                                                kernel_stack_chunk_t, l );
    idx = find_first_bit_mem_64( chunk->bitmap, BITMAP_ENTRIES_COUNT );

    if( idx != INVALID_BIT_INDEX ) {
      main_stack_ctx.free_items--;

      reset_bit_mem_64( chunk->bitmap, idx );
      r = 0;
    }
  }

  UNLOCK_STACK_CTX(&main_stack_ctx);

  if( r == 0 ) {
    stack->high_address = starting_kernel_stack_address -
                          idx * KERNEL_STACK_STEP - KERNEL_STACK_GAP;
    stack->low_address = stack->high_address - KERNEL_STACK_SIZE;
    stack->id = idx;
  } else {
    stack->high_address = KERNEL_INVALID_ADDRESS;
    stack->id = INVALID_STACK_ID;
  }

  return r;
}

int free_kernel_stack(bit_idx_t id)
{
  return 0;
}

void initialize_kernel_stack_allocator(void)
{
  starting_kernel_stack_address = KERNEL_BASE;
  initialize_stack_allocator_context(&main_stack_ctx);
}


