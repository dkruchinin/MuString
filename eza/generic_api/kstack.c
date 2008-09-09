/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/kstack.c: functions for dealing with kernel stack management.
 */

#include <mm/pagealloc.h>
#include <eza/kernel.h>
#include <eza/arch/bits.h>
#include <eza/errno.h>
#include <eza/container.h>
#include <mlibc/kprintf.h>
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
  init_list_head(&chunk->l);
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
  init_list_head(&ctx->chunks);
  initialize_stack_chunk(ch1);

  ctx->total_items = ch1->total_items;
  ctx->free_items = ch1->free_items;
  ctx->num_chunks = 1;

  list_add_tail(&ch1->l,&ctx->chunks);
}

int allocate_kernel_stack(kernel_stack_t *stack)
{
  int r = -ENOENT;
  bit_idx_t idx;

  LOCK_STACK_CTX(&main_stack_ctx);

  if(main_stack_ctx.free_items > 0) {
    kernel_stack_chunk_t *chunk = container_of( main_stack_ctx.chunks.next,
                                                kernel_stack_chunk_t, l );
    idx = find_first_bit_mem_64( chunk->bitmap, BITMAP_ENTRIES_COUNT );

    if( idx != INVALID_BIT_INDEX ) {
      main_stack_ctx.free_items--;

      reset_and_test_bit_mem_64( chunk->bitmap, idx );
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
  /* TODO: [mt] Implement proper kernel stack deallocation. */
  return 0;
}

void initialize_kernel_stack_allocator(void)
{
  starting_kernel_stack_address = KERNEL_BASE;
  initialize_stack_allocator_context(&main_stack_ctx);
}


