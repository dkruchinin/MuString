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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mm/mmpool.c: MM-pools
 *
 */

#include <config.h>
#include <ds/list.h>
#include <mlibc/string.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <mm/slab.h>
#include <mm/tlsf.h>
#include <eza/kernel.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

mm_pool_t *mm_pools[CONFIG_NOF_MMPOOLS];
static LIST_DEFINE(__pools_list);
static SPINLOCK_INITIALIZE(__pools_lock);
static mm_pool_t __general_pool;
static ulong_t __idx_allocator_space;
static idx_allocator_t pools_ida;

#ifdef CONFIG_DMA_POOL
static mm_pool_t __dma_pool;
#endif /* CONFIG_DMA_POOL */

int mmpool_register(mm_pool_t *pool, const char *name, uint8_t type, uint8_t flags)
{
  memset(pool, 0, sizeof(*pool));
  if ((strlen(name) >= MMPOOL_NAME_LEN) || (type >= CONFIG_NOF_MMPOOLS))
    return -E2BIG;
  if (flags & MMP_ACTIVE)
    return -EINVAL;

  strcpy(pool->name, name);
  spinlock_initialize(&pool->lock);
  pool->type = type;
  pool->flags = (flags & MMPOOL_FLAGS_MASK);
  atomic_set(&pool->stat.free_pages, 0);
  spinlock_lock(&__pools_lock);
  ASSERT(mm_pools[type] == NULL);
  mm_pools[pool->type] = pool;
  list_add2tail(&__pools_list, &pool->pool_node);
  spinlock_unlock(&__pools_lock);
  
  return 0;
}

mm_pool_t *mmpool_create(const char *name, uint8_t type, uint8_t flags)
{
  int ret;
  uint8_t type;
  mm_pool_t *pool;

  pool = memalloc(sizeof(*pool));
  if (!pool) {
    ret = -ENOMEM;
    goto out;
  }
  
  spinlock_lock(&__pools_lock);
  type = idx_allocate(&pools_ida);
  if (type == (uint8_t)IDX_INVAL) {
    ret = -EBUSY;
    spinlock_unlock(&__pools_lock);
    goto out;
  }

  spinlock_unlock(&__pools_lock);
  ret = mmpool_register(pool, name, type, flags);
  
  out:
  if (ret && pool) {
    memfree(pool);
    if (type != (uint8_t)IDX_INVAL) {
      spinlock_lock(&__pools_lock);
      idx_free(&pools_ida, type);
      spinlock_unlock(&__pools_lock);
    }
  }
  
  return ret;
}

void mmpools_initialize(void)
{
  int ret;
  
  kprintf("[MM] Initializing memory pools.\n");
  memset(mm_pools, 0, sizeof(*mm_pools) * CONFIG_NOF_MMPOOLS);
  CT_ASSERT((sizeof(__idx_allocator_space) << 3) >= CONFIG_NOF_MMPOOLS);  
  memset(&__idx_allocator_space, 0, sizeof(__idx_allocator_space));
  memset(&pools_ida, 0, sizeof(pools_ida));
  CT_ASSERT(!(sizeof(__idx_allocator_space) & (sizeof(ulong_t) - 1)));
  pools_ida.size = sizeof(__idx_allocator_space);
  pools_ida.max_id = CONFIG_NOF_MMPOOLS;
  pools_ida.main_bmap = &__idx_allocator_space;
  idx_allocator_init_core(&pools_ida);  
  kprintf(" Creating Generic memory pool.\n");
  idx_reserve(&pools_ida, GENERAL_POOL_TYPE);
  ret = mmpool_register(&__general_pool, "General", GENERAL_POOL_TYPE, MMP_IMMORTAL);
  if (ret)
    panic("Can not register \"General\" memory pool! [err = %d]", ret);

#ifdef CONFIG_DMA_POOL
  kprintf(" Creating DMA memory pool.\n");
  idx_reserve(&pools_ida, DMA_POOL_TYPE);
  ret = mmpool_register(&__dma_pool, "DMA", DMA_POOL_TYPE, MMP_IMMORTAL);
  if (ret)
    panic("Can not register \"DMA\" memory pool! [err = %d]", ret);
#endif /* CONFIG_DMA_POOL */  
}

void mmpools_init_pool_allocator(mm_pool_t *pool)
{
  ASSERT(pool->is_active);
  switch (pool->type) {
      case POOL_GENERAL: case POOL_DMA:
        ASSERT(atomic_get(&pool->free_pages) > 0);
        tlsf_alloc_init(pool);
        break;
      default:
        panic("Unlnown memory pool type: %d", pool->type);
  }
}

