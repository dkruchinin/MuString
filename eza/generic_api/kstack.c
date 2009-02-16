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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/kstack.c: functions for dealing with kernel stack management.
 */

#include <mm/pfalloc.h>
#include <eza/kernel.h>
#include <eza/arch/bits.h>
#include <eza/errno.h>
#include <mlibc/kprintf.h>
#include <mlibc/stddef.h>
#include <eza/arch/page.h>
#include <eza/task.h>
#include <eza/kstack.h>
#include <eza/arch/mm.h>

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

static void initialize_stack_chunk(kernel_stack_chunk_t *chunk, uint32_t id)
{
  list_init_node(&chunk->l);
  chunk->total_items = chunk->free_items = BITMAP_ENTRIES_COUNT;
  chunk->high_address = starting_kernel_stack_address - id*KERNEL_STACK_CHUNK_SIZE;
  chunk->low_address = chunk->high_address - KERNEL_STACK_CHUNK_SIZE;
  initialize_bitmap(chunk->bitmap, BITMAP_ENTRIES_COUNT);
}

static void initialize_stack_allocator_context(kernel_stack_allocator_context_t *ctx)
{
  page_frame_t *page = alloc_page(AF_MMP_GEN);
  kernel_stack_chunk_t *ch1 = (kernel_stack_chunk_t *)pframe_to_virt(page);

  if( ch1 == NULL ) {
    panic( "initialize_stack_allocator_context(): Can't initialize stack context !" );
  }

  spinlock_initialize(&ctx->lock);
  list_init_head(&ctx->chunks);
  initialize_stack_chunk(ch1,0);

  ctx->total_items = ch1->total_items;
  ctx->free_items = ch1->free_items;
  ctx->num_chunks = 1;

  list_add2tail(&ctx->chunks, &ch1->l);
}

int allocate_kernel_stack(kernel_stack_t *stack)
{
  int r = -ENOENT;
  bit_idx_t idx;
  kernel_stack_chunk_t *chunk;

  LOCK_STACK_CTX(&main_stack_ctx);

  if(main_stack_ctx.free_items > 0) {
    chunk = container_of( list_node_first(&main_stack_ctx.chunks),
                                                kernel_stack_chunk_t, l );
    idx = find_first_bit_mem( chunk->bitmap, BITMAP_ENTRIES_COUNT );

    if( idx != INVALID_BIT_INDEX ) {
      main_stack_ctx.free_items--;
      reset_and_test_bit_mem( chunk->bitmap, idx );
      r = 0;
    }
  }

  UNLOCK_STACK_CTX(&main_stack_ctx);

  if( r == 0 ) {
    stack->high_address = chunk->high_address - idx*KERNEL_STACK_STEP - KERNEL_STACK_FRONT_GAP;
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
  starting_kernel_stack_address = __allocate_vregion(KERNEL_STACK_PAGES * NUM_PIDS * 4);
  initialize_stack_allocator_context(&main_stack_ctx);
}


