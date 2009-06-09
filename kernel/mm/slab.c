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
 * mm/slab.c: SLAB allocator internals.
 *
 */

#include <ds/list.h>
#include <mstring/stddef.h>
#include <mstring/string.h>
#include <mm/page.h>
#include <mm/idalloc.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <sync/spinlock.h>
#include <sync/rwsem.h>
#include <mstring/errno.h>
#include <mstring/types.h>

/* POW2 generic memory caches(memalloc allocates memory from them) */
static memcache_t *generic_memcaches[SLAB_GENERIC_CACHES];

/* memory cache for memcache_t structures */
static memcache_t caches_memcache;

/* memory cache for slab_t structures */
static memcache_t slabs_memcache;

/* RW semaphore for protection of memcaches_list */
static RWSEM_DEFINE(memcaches_rwlock);

/* Sorted by objects size list of all registered memory caches in a system. */
static LIST_DEFINE(memcaches_list);


static void destroy_slab(slab_t *slab);
static void free_slab_object(slab_t *slab, void *obj);

/**************************************************************
 * >>> LOCAL LOCKING FUNCTIONS
 */

/* memory cache locking API */
#define memcache_lock(cache)                            \
  spinlock_lock_bit(&(cache)->flags, bitnumber(__SMCF_BIT_LOCK))
#define memcache_unlock(cache)                              \
  spinlock_unlock_bit(&(cache)->flags, bitnumber(__SMCF_BIT_LOCK))

/* SLAB locking API */
#define __lock_slab_page(pg)                    \
  lock_page_frame(pg, PF_LOCK)
#define __unlock_slab_page(pg)                  \
  unlock_page_frame(pg, PF_LOCK)
#define slab_lock(slab)                         \
  __lock_slab_page((slab)->pages)
#define slab_unlock(slab)                       \
  __unlock_slab_page((slab)->pages)

/***************************************************************/

#define __get_percpu_slab(memcache)             \
  ((memcache)->active_slabs[cpu_id()])
#define __set_percpu_slab(memcache, slab)       \
  ((memcache)->active_slabs[cpu_id()] = (slab))

/**************************************************************
 * >>> CONFIG_DEBUG_SLAB_MARK_PAGES functions
 */
#ifdef CONFIG_DEBUG_SLAB_MARK_PAGES
static int mark_version = SLAB_FIRST_VERSION;
static SPINLOCK_DEFINE(version_lock);

static inline void memcache_dbg_set_version(memcache_t *memcache)
{
  memcache->mark_version = mark_version;
  spinlock_lock(&version_lock);
  mark_version++;
  spinlock_unlock(&version_lock);
}

static inline void *__objrealaddr(void *obj, int add)
{
  char *p = obj;

  /*
   * If object address is page aligned or <obj> + <add>
   * covers both current and next page - take care about
   * per-page guard.
   */
  if (!((uintptr_t)p & PAGE_MASK) ||
      (PAGE_ALIGN(p) != PAGE_ALIGN(p + add))) {
    p = (char *)(PAGE_ALIGN(p) + SLAB_PAGE_MARK_SIZE);
  }

  return (p + add);
}

/*
 * Get number of objects fitting in slabs of given memcache
 * taking into account per-page guard.
 */
static inline int __calc_objects_per_slab(memcache_t *memcache)
{
  return (((PAGE_SIZE - SLAB_PAGE_MARK_SIZE)
           / memcache->object_size) * memcache->pages_per_slab);
}

static void slab_dbg_mark_pages(memcache_t *memcache, page_frame_t *pages)
{
  unsigned int page_mark = SLAB_PAGE_MARK_BASE + memcache->mark_version;
  int i;
  char *p;

  p = pframe_to_virt(pages);
  for (i = 0; i < memcache->pages_per_slab; i++, p += PAGE_SIZE) {
    *(unsigned int *)p = page_mark;
  }
}

/*
 * Check if address "addr" belongs to any registered memory cache.
 * If it's not, raise a BUG.
 */
static void slab_dbg_check_address(void *addr)
{
  unsigned int page_mark = *(unsigned int *)PAGE_ALIGN_DOWN((uintptr_t)addr);
  memcache_t *mc;
  bool found_owner = false;

  rwsem_down_read(&memcaches_rwlock);
  list_for_each_entry(&memcaches_list, mc, memcache_node) {
    if (page_mark == (SLAB_PAGE_MARK_BASE + mc->mark_version)) {
      found_owner = true;
      break;
    }
  }

  rwsem_up_read(&memcaches_rwlock);
  if (!found_owner) {
    BUG("Failed to locate memory cache owning by address %p\n"
        "  -> [Page mark valude: %#x]\n"
        "  -> SLAB_PAGE_MARK_BASE = %#x\n",
        addr, page_mark, SLAB_PAGE_MARK_BASE);
  }
}

static void slab_dbg_check_page(slab_t *slab, void *page_start)
{
  unsigned int page_mark = *(unsigned int *)page_start;

  slab_lock(slab);
  if (page_mark != (SLAB_PAGE_MARK_BASE + slab->memcache->mark_version)) {
    BUG("Slab %p of memory cache \"%s\" (size = %d) has "
        "invalid page frame №%#x.\n"
        "  -> Expected page mark  = %#x\n"
        "  -> Given page mark     = %#x\n"
        "  -> SLAB_PAGE_MARK_BASE = %#x\n",
        slab, slab->memcache->name, slab->memcache->object_size,
        virt_to_pframe_id(page_start),
        (SLAB_PAGE_MARK_BASE + slab->memcache->mark_version),
        page_mark, SLAB_PAGE_MARK_BASE);
  }
  
  slab_unlock(slab);
}
#else
#define __objrealaddr(obj, add) ((void *)((char *)(obj) + (add)))

/* Get number of objects fitting in slabs of given memory cache */
static inline int __calc_objects_per_slab(memcache_t *memcache)
{
  return (((uintptr_t)memcache->pages_per_slab << PAGE_WIDTH) /
          memcache->object_size);

}

#define memcache_dbg_set_version(memcache)
#define slab_dbg_mark_pages(memcache, pages)
#define slab_dbg_check_address(addr)
#define slab_dbg_check_page(slab, page_start)
#endif /* CONFIG_DEBUG_SLAB_MARK_PAGES */

/**************************************************************
 * >>> CONFIG_DEBUG_SLAB_OBJGUARDS functions
 */
#ifdef CONFIG_DEBUG_SLAB_OBJGUARDS
static inline void slab_dbg_set_objguards(slab_t *slab, void *obj)
{
  if (likely(slab->memcache->object_size >= SLAB_OBJDEBUG_MINSIZE)) {
    *(unsigned int *)obj = SLAB_OBJLEFT_GUARD;
    *(unsigned int *)((uintptr_t)obj +
                      SLAB_RIGHTGUARD_OFFS) = SLAB_OBJRIGHT_GUARD;
  }
}

/*
 * Renturn an object + offset of its left guard. If <leftright> < 0,
 * guard's offset will be substracted and added otherwise.
 */
static inline void *__objoffs(slab_t *slab, void *obj, int leftright)
{
  if (unlikely(slab->memcache->object_size < SLAB_OBJDEBUG_MINSIZE))
    return obj;

  return ((char *)obj + ((leftright < 0) ?
                         -SLAB_LEFTGUARD_OFFS : SLAB_LEFTGUARD_OFFS));
}

/* Check if free slab object is valid one and panic if not so. */
static void slab_dbg_check_object(slab_t *slab, void *obj)
{
  if (unlikely(slab->memcache->object_size) < SLAB_OBJDEBUG_MINSIZE)
    return;
  else {
    char *p = (char *)obj;
    int i = 0;

    slab_lock(slab);
    if (*(unsigned int *)p != SLAB_OBJLEFT_GUARD)
      goto inval;

    p += SLAB_RIGHTGUARD_OFFS;
    if (*(unsigned int *)p != SLAB_OBJRIGHT_GUARD) {
      i++;
      goto inval;
    }

    slab_unlock(slab);
    return;

inval:
    BUG("Invalid slab object %p:\n"
        "%sGUARD was modified!\n"
        "leftguard = %#x, rightguard = %#x",
        obj, (i ? "RIGHT" : "LEFT"),
        *(unsigned int *)(p - SLAB_RIGHTGUARD_OFFS), *(unsigned int *)p);
  }
}

#else
#define slab_dbg_set_objguards(slab, obj)
#define __objoffs(slab, obj, leftright) (obj)
#define slab_dbg_check_object(slab, obj)
#endif /* CONFIG_DEBUG_SLAB_OBJGUARDS */

/**************************************************************
 * >>> CONFIG_DEBUG_SLAB functions
 */
#ifdef CONFIG_DEBUG_SLAB
#define SLAB_DBG_ASSERT(cond) ASSERT(cond)
/* FIXME: DK */
#define SLAB_VERBOSE(__memcache, fmt, args...)          \
  do {                                                  \
    if (true) {                                         \
      kprintf("[SLAB VERBOSE] |%s|: ", __FUNCTION__);   \
      kprintf(fmt, ##args);                             \
    }                                                   \
  } while (0)

#define SLAB_PRINT_ERROR(fmt, args...)                       \
  do {                                                       \
    kprintf("[SLAB ERROR] |%s|: ", __FUNCTION__);            \
    kprintf(fmt, ##args);                                    \
  } while (0)

/*
 * Display all slab addressess located in given list <lst>
 */
#define __dump_slabs_lst(lst)                               \
  do {                                                      \
    page_frame_t *__pf;                                     \
    list_for_each_entry((lst), __pf, node) {                \
      kprintf("%p -> ", __page2slab(__pf));                 \
    }                                                       \
    kprintf("\n");                                          \
  } while (0)

#define memcache_dbg_set_name(memcache, name)   \
  strncpy((memcache)->name, name, MEMCACHE_DBG_NAME_MAX)

/* display cache statistics */
#if 0
static inline void __display_statistics(memcache_t *cache)
{
  int full_slabs = atomic_get(&cache->nslabs) -
    atomic_get(&cache->nempty_slabs) - atomic_get(&cache->npartial_slabs);
  SLAB_VERBOSE("%s: (T: %d; F: %d; P: %d; E: %d)\n",
               cache->dbg.name, atomic_get(&cache->nslabs), full_slabs,
               atomic_get(&cache->npartial_slabs), atomic_get(&cache->nempty_slabs));
}
#endif

#else
#define SLAB_DBG_ASSERT(cond)
#define SLAB_VERBOSE(memcache, fmt, args...)
#define SLAB_PRINT_ERROR(fmt, args...)
#define memcache_dbg_set_name(memcache, name)
#endif /* CONFIG_DEBUG_SLAB */


/***************************************************************
 * >>> Register/unregister functions for different slab states
 */

#define slab_register_full(memcache, slab)      \
  list_add2tail(&(memcache)->full_slabs, &(slab)->pages->node)
#define slab_unregister_full(slab)              \
  list_del(&(slab)->pages->node)
#define slab_register_partial(memcache, slab)   \
  list_add2head(&(memcache)->avail_slabs, &(slab)->pages->node)
#define slab_unregister_partial(slab)           \
  list_del(&(slab)->pages->node)
#define slab_register_empty(memcache, slab)     \
  list_add2tail(&(memcache)->avail_slabs, &(slab)->pages->node)
#define slab_unregister_empty(slab)             \
  list_del(&(slab)->pages->node)


/***************************************************************
 * >>> Miscellaeous slab functions
 */

/* Return slab owning by page "page" */
#define __page2slab(page)                       \
  ((slab_t *)((page)->slab_ptr))

/* Return slab owning by page address "addr" fits in. */
#define __slab_get_by_addr(addr)                \
  __page2slab((page_frame_t *)virt_to_pframe((void *)PAGE_ALIGN_DOWN((uintptr_t)(addr))))

#ifdef CONFIG_DEBUG_SLAB
static char *__slab_stat_names[] = {
  "SLAB_EMPTY", "SLAB_PARTIAL", "SLAB_FULL", "SLAB_ACTIVE", "!!UNKNOWN!!" };
#define __slab_state_to_string(stat)           \
  (__slab_stat_names[((stat) - 1) % 5])
#endif /* CONFIG_DEBUG_SLAB */

/***************************************************************
 * >>> Slab's freelist management functions
 */
static inline void slab_add_free_obj_lazy(slab_t *slab, void *obj)
{
  uintptr_t *p = __objoffs(slab, obj, 1);

  *p = (uintptr_t)slab->pages->slab_lazy_freelist;
  slab->pages->slab_lazy_freelist = p;
  slab->pages->slab_num_lazy_objs++;
  slab_dbg_set_objguards(slab, obj);
}

static inline void slab_add_free_obj(slab_t *slab, void *obj)
{
  uintptr_t *p = __objoffs(slab, obj, 1);

  *p = (uintptr_t)slab->objects;
  slab->objects = p;    
  slab->nobjects++;
  slab_dbg_set_objguards(slab, obj);
}

static inline void *slab_get_free_obj(slab_t *slab)
{
  uintptr_t *p = slab->objects;

  SLAB_DBG_ASSERT(slab->objects != NULL);  
  slab->objects = (void *)*p;
  slab->nobjects--;
  SLAB_DBG_ASSERT(slab->nobjects >= 0);
  return __objoffs(slab, p, -1);
}

static inline void slab_swap_freelist(slab_t *slab)
{
  SLAB_DBG_ASSERT((slab->objects == NULL) &&
                  (slab->nobjects == 0));
  slab->objects = slab->pages->slab_lazy_freelist;
  slab->pages->slab_lazy_freelist = NULL;
  slab->nobjects += slab->pages->slab_num_lazy_objs;
  slab->pages->slab_num_lazy_objs = 0;
}

/*
 * Split available space in slab into equal objects
 * of fixed size(regarding the value of "slab->memcache->object_size").
 * Mark each object as free for use. Depending of slab allocator config,
 * various additional debug actions may be performed.
 */
static void __init_slab_objects(slab_t *slab, int skip_objs)
{
  void *obj, *next;
  int cur = __calc_objects_per_slab(slab->memcache) - skip_objs;


  next = __objrealaddr(pframe_to_virt(slab->pages),
                      skip_objs * slab->memcache->object_size);
  while (cur > 0) {
    obj = next;
    slab_add_free_obj(slab, obj);
    next = __objrealaddr(obj, slab->memcache->object_size);
    cur--;
  }
}

/*
 * alloc slab pages doesn't care about memory cache locking
 * So, before calling alloc_slab_pages, user should lock cache
 * by himself. (if cache locking is necessary)
 */
static page_frame_t *alloc_slab_pages(memcache_t *memcache,
                                      pfalloc_flags_t pfa_flags)
{
  page_frame_t *pages = NULL;

  //TODO: pfa_flags |= (memcache->flags & MMPOOLS_MASK);
  pages = alloc_pages(memcache->pages_per_slab, pfa_flags);
  if (!pages) {
    return NULL;
  }

  slab_dbg_mark_pages(memcache, pages);
  return pages;
}

static inline void free_slab_pages(memcache_t *memcache, page_frame_t *pages)
{
  free_pages(pages, memcache->pages_per_slab);
}

static inline void prepare_slab_pages(slab_t *slab)
{
  int i;
  page_frame_t *page;

  /*
   * There must be an ability to determine slab by any page
   * it owns. Thus each slab's page must point to its owner.
   */
  for (i = 0; i < slab->memcache->pages_per_slab; i++) {
    page = slab->pages + i;
    page->slab_ptr = (void *)slab;
  }
}

static int prepare_slab(memcache_t *memcache, slab_t *slab,
                        pfalloc_flags_t pfa_flags)
{
  memset(slab, 0, sizeof(*slab));
  slab->memcache = memcache;
  slab->pages = alloc_slab_pages(memcache, pfa_flags);
  if (!slab->pages) {
    SLAB_PRINT_ERROR("(%s) Failed to allocate %d pages for slab "
                     "%p. [pfa_flags = %#x]\n", memcache->name,
                     memcache->pages_per_slab, slab, pfa_flags);
    return -ENOMEM;
  }
  else {
    prepare_slab_pages(slab);
    slab->pages->slab_lazy_freelist = NULL;
    slab->pages->slab_num_lazy_objs = 0;
  }

  __init_slab_objects(slab, 0);
  return 0;
}

/*
 * Slab for memory cache managing slab_t structures is
 * allocated and fried in a little diffrent way.
 */
static slab_t *__create_slabs_slab(pfalloc_flags_t pfa_flags)
{
  page_frame_t *pages = alloc_slab_pages(&slabs_memcache, pfa_flags);
  slab_t *slab = NULL;

  if (!pages) {
    goto out;
  }

  /*
   * When creating a new slab for slab_t structures, the very
   * first object in the slab must be reserverd for slab_t itself.
   * Thus it will be fried only after given slab is destroyed.
   */
  slab = __objrealaddr(pframe_to_virt(pages), 0);
  slab->memcache = &slabs_memcache;
  slab->objects = NULL;
  slab->nobjects = 0;
  slab->pages = pages;

  prepare_slab_pages(slab);
  __init_slab_objects(slab, 1);

  SLAB_VERBOSE(&slabs_memcache, ">> Created new slab %p with %d free "
               "objects of size %d\n", slab, slab->nobjects,
               slab->memcache->object_size);

out:
  return slab;
}

static slab_t *create_new_slab(memcache_t *memcache, int alloc_flags)
{
  slab_t *new_slab = NULL;
  pfalloc_flags_t pfa_flags = 0;

  pfa_flags |= (!!(alloc_flags & SAF_ATOMIC) << bitnumber(AF_ATOMIC));
  if (likely(memcache != &slabs_memcache)) {
    int ret;

    /*
     * In worst case "slabs_memcache" have to allocate new slab
     * for its slabs. If so, "create_new_slab" will be called again
     * from "alloc_from_memcache". It'll call __create_slabs_slab(see below).
     * TODO DK: Measure "alloc_from_memcache" execution time in worst case
     * (described above). Are there any ways to reduce such crappy crap?
     */
    new_slab = alloc_from_memcache(&slabs_memcache, alloc_flags);
    ret = prepare_slab(memcache, new_slab, pfa_flags);
    if (ret) {
      SLAB_PRINT_ERROR("(%s) Failed to allocate new slab: [RET = %d]\n",
                       memcache->name, ret);

      destroy_slab(new_slab);
      return NULL;
    }
  }
  else {
    new_slab = __create_slabs_slab(pfa_flags);
  }

  return new_slab;
}

static void destroy_slab(slab_t *slab)
{
  page_frame_t *slab_pages = slab->pages;
  memcache_t *memcache = slab->memcache;

  SLAB_VERBOSE(memcache, ">> destroy slab %p(state = %s)\n",
               slab,  __slab_state_to_string(slab->state));

  if (likely(slab->memcache != &slabs_memcache)) {
    slab_t *slabs_slab;

    slab_dbg_check_address(slab);
    slabs_slab = __slab_get_by_addr(slab);
    slab_dbg_check_page(slabs_slab,
                        (void *)PAGE_ALIGN_DOWN((uintptr_t)slab));    
    free_slab_object(slabs_slab, slab);
  }

  free_slab_pages(memcache, slab_pages);
}

static void free_slab_object(slab_t *slab, void *obj)
{
  memcache_t *memcache = slab->memcache;
  int irqstat;

  interrupts_save_and_disable(irqstat);

  /*
   * If given slab has SLAB_ACTIVE state, it wouldn't become
   * empty or partial. Also we don't need to lock it, because
   * local interrupts are already disabled and SLAB_ACTIVE state
   * tells us that given slab is per-cpu guy.
   */
  if (slab->state == SLAB_ACTIVE) {
    if (__get_percpu_slab(memcache) == slab) {
      slab_add_free_obj(slab, obj);
    }
    else {
      slab_lock(slab);

      /*
       * Check if slab state didn't change. If it didn't, put
       * objects in lazy freelist.
       */
      if (likely(slab->state == SLAB_ACTIVE)) {
        slab_add_free_obj_lazy(slab, obj);
        SLAB_VERBOSE(memcache, ">> (%s) free object %p into lazy freelist of "
                     "percpu slab %p (CPU №%d)\n", memcache->name,
                     obj, slab, cpu_id());
      }
      else {
        goto free_to_inactive_slab;
      }

      slab_unlock(slab);
    }

    goto out;
  }

  /*
   * Otherwise given slab may have either SLAB_FULL or SLAB_PARTIAL
   * state. Here we have to manually lock the slab.
   */
  slab_lock(slab);
  if (unlikely(slab->state == SLAB_ACTIVE)) {
    slab_add_free_obj_lazy(slab, obj);
    
    SLAB_VERBOSE(memcache, ">> free object %p into lazy freelist of "
                 "percpu slab %p (CPU №%d)\n", obj, slab, cpu_id());
    
    slab_unlock(slab);
    goto out;
  }

free_to_inactive_slab:
  slab_add_free_obj(slab, obj);

  switch (slab->state) {
      case SLAB_FULL:
        /* If slab was full, it becomes partial after item is fried */
        slab->state = SLAB_PARTIAL;
        memcache_lock(memcache);
        slab_unregister_full(slab);
        slab_register_partial(memcache, slab);
        memcache->stat.npartial_slabs++;
        
        SLAB_VERBOSE(memcache, ">> (%s) move slab %p from %s to %s state. "
                     "Partial slabs: %d\n", memcache->name,
                     slab, __slab_state_to_string(SLAB_FULL),
                     __slab_state_to_string(SLAB_PARTIAL),
                     memcache->stat.npartial_slabs);
        
        memcache_unlock(memcache);
        break;
      case SLAB_PARTIAL:
      {
        int objs_per_slab = __calc_objects_per_slab(memcache);

        /* We don't care about partial slab until it becomes empty. */
        if ((slab->nobjects == objs_per_slab) ||
            ((memcache == &slabs_memcache) &&
             (slab->nobjects == (objs_per_slab - 1)))) {
          memcache_lock(memcache);
          memcache->stat.npartial_slabs--;
          slab_unregister_partial(slab);

          /*
           * If number of empty slabs exceeds SLAB_EMPTYSLABS_MAX,
           * given slab will be fried. Otherwise it will be added
           * to memory cache empty slabs use for future use.
           */
          if (memcache->stat.nempty_slabs == MEMCACHE_EMPTYSLABS_MAX) {
            memcache->stat.nslabs--;

            SLAB_VERBOSE(memcache, ">> (%s) drop slab %p(became %s from %s). "
                         "%d empty slabs rest.\n", memcache->name, slab,
                         __slab_state_to_string(SLAB_EMPTY),
                         __slab_state_to_string(SLAB_PARTIAL),
                         memcache->stat.nempty_slabs);

            memcache_unlock(memcache);
            slab_unlock(slab);
            interrupts_restore(irqstat);
            destroy_slab(slab);
            return;
          }
          else {
            slab->state = SLAB_EMPTY;
            memcache->stat.nempty_slabs++;
            slab_register_empty(memcache, slab);

            SLAB_VERBOSE(memcache, ">> (%s) move slab %p from %s to %s state. "
                         "Empty slabs: %d\n", memcache->name, slab,
                         __slab_state_to_string(SLAB_PARTIAL),
                         __slab_state_to_string(SLAB_EMPTY),
                         memcache->stat.nempty_slabs);

            memcache_unlock(memcache);
          }
        }

        break;
      }
      default:
        /*
         * Slab object is fried from may be either SLAB_FULL or SLAB_PARTIAL
         * only. And it's actually a bug if any other type is met.
         */
        BUG("Unexpected slab state was met during slab object "
            "freeing (%d)\n", slab->state);
  }

  slab_unlock(slab);
  
out:
  interrupts_restore(irqstat);
}

static void prepare_memcache(memcache_t *cache, size_t object_size,
                             int pages_per_slab)
{
  memset(cache, 0, sizeof(*cache));
  cache->object_size = object_size;
  cache->pages_per_slab = pages_per_slab;
  list_init_head(&cache->avail_slabs);
  list_init_head(&cache->full_slabs);
  cache->usecount = 1;
}

/*
 * Register new memory cache. By default all memory caches
 * linked in one sorted "memcaches_list".
 */
static void register_memcache(memcache_t *memcache, const char *name)
{
  memcache_t *c;
  bool added = false;
  
  memcache_dbg_set_name(memcache, name);
  memcache_dbg_set_version(memcache);
  rwsem_down_write(&memcaches_rwlock);
  list_for_each_entry(&memcaches_list, c, memcache_node) {
    if (c->object_size > memcache->object_size) {
      list_add_before(&c->memcache_node, &memcache->memcache_node);
      added = true;
      break;
    }
  }
  if (!added)
    list_add2tail(&memcaches_list, &memcache->memcache_node);
  
  rwsem_up_write(&memcaches_rwlock);
}

/*
 * Try to find out if there are empty or partial slabs
 * in avail_slabs list of memory cache. If so, one of them will be
 * used. By default when slab becomes partial it is added at the
 * head of the list, if empty - at the tail. Here we take the very
 * first slab from the list.
 * NOTE: Assume that local interrupts are disabled and memcache is locked.
 */
static inline slab_t *try_get_avail_slab(memcache_t *memcache)
{
  slab_t *slab;
  page_frame_t *p;
  
  if (list_is_empty(&memcache->avail_slabs))
    return NULL;

  p = list_entry(list_node_first(&memcache->avail_slabs),
                                 page_frame_t, node);
  slab = __page2slab(p);
  slab_lock(slab);
  switch (slab->state) {
      case SLAB_PARTIAL:
        slab_unregister_partial(slab);
        memcache->stat.npartial_slabs--;
        break;
      case SLAB_EMPTY:
        slab_unregister_empty(slab);
        memcache->stat.nempty_slabs--;
        break;
      default:
        /*
         * Only empty or partial slabs may be linked in avail_slabs
         * list. If there exist slab of any other type, it's a BUG.
         */
        BUG("Slab with unknown state(%d) was found in "
            "avail_slabs list of memcache %p!\n", memcache);
      }

  SLAB_VERBOSE(memcache, ">> set percpu slab %p for (CPU №%d). "
               "Old state = %s.\n", slab, cpu_id(),
               __slab_state_to_string(slab->state));

  slab->state = SLAB_ACTIVE;
  __set_percpu_slab(memcache, slab);
  slab_unlock(slab);

  return slab;
}

/*
 * There are only two "heart" caches: one for memcache_t structures and another
 * one for slab_t strcures.
 */
static void __create_heart_cache(memcache_t *cache, size_t size,
                                 const char *name)
{
  int ret, i;

  SLAB_VERBOSE(cache, ">> Creating heart cache \"%s\", object size=%d\n",
               name, size);

  /*
   * All "heart" caches are predefined, so we don't need to allocate
   * them dynamically.
   */
  prepare_memcache(cache, size, GENERIC_SLAB_PAGES);
  cache->flags = GENERAL_POOL_TYPE | SMCF_IMMORTAL | SMCF_UNIQUE;
  bit_clear(&cache->flags, bitnumber(__SMCF_BIT_LOCK));
  
  /*
   * avail_slabs list holds all slabs that may be used for
   * objects allocation. Partial slabs located before empty
   * slabs. Empty slabs always placed at the tail of the list.
   */

  register_memcache(cache, name);
  SLAB_VERBOSE(cache, "memory cache %s was initialized. (size = %d, pages per slab = %d)\n",
               cache->name, cache->object_size, cache->pages_per_slab);
  ret = -ENOMEM;
  for_each_cpu(i) {
    slab_t *slab;

    /*
     * For this moment we can not allocate slab_t structure via slab,
     * so we are allocating it using init-data allocator for each CPU.
     */
    if (cache == &slabs_memcache) {
      slab = __create_slabs_slab(0);
      if (!slab)
        goto err;
    }
    else {
      slab = alloc_from_memcache(&slabs_memcache, 0);
      if (!slab)
        goto err;

      ret = prepare_slab(cache, slab, 0);
      if (ret)
        goto err;
    }

    SLAB_VERBOSE(cache, "Created percpu slab for %s, cpu=%d\n", name, i);
    slab->state = SLAB_ACTIVE;
    cache->active_slabs[i] = slab;
    cache->stat.nslabs++;
  }

  kprintf(" Cache \"%s\" (size=%zd) was successfully initialized\n",
          name, size);
  return;
  
err:
  panic("Can't create slab memory cache for \"%s\" elements! (err=%d)", name, ret);
}

/*
 * Generic memory caches are a set of caches with sizes equal to
 * multiple of power of 2. These caches are used by memalloc function,
 * which tries to find out generic memory cache of size greater or equal
 * to requested one.
 * There is constant number of generic caches in a system equals to number
 * of all power of 2 in a range [SLAB_OBJECT_MIN_SIZE, SLAB_OBJECT_MAX_SIZE].
 */
static void __create_generic_caches(void)
{
  size_t size = SLAB_OBJECT_MIN_SIZE;
  int i = 0;
  char cache_name[16] = "size ";

  ASSERT(is_powerof2(SLAB_OBJECT_MAX_SIZE));
  memset(generic_memcaches, 0, sizeof(*generic_memcaches) *
         SLAB_GENERIC_CACHES);
  while (size <= SLAB_OBJECT_MAX_SIZE) {
    ASSERT((size >= SLAB_OBJECT_MIN_SIZE) &&
           (size <= SLAB_OBJECT_MAX_SIZE));

    sprintf(cache_name + 5, "%3d", size);
    *(cache_name + 9) = '\0';

    generic_memcaches[i] = create_memcache(cache_name, size, 1,
                                           GENERAL_POOL_TYPE | SMCF_IMMORTAL);
    if (!generic_memcaches[i])
      panic("Can't greate generic cache for size %zd: (ENOMEM)", size);

    size <<= 1;
    i++;
  }

  return;
}

void slab_allocator_init(void)
{
  kprintf("[MM] Initializing slab allocator\n");

  CT_ASSERT(sizeof(atomic_t) >= sizeof(uintptr_t));
  CT_ASSERT((1 << FIRST_GENSLABS_POW2) == SLAB_OBJECT_MIN_SIZE);
  CT_ASSERT((1 << LAST_GENSLABS_POW2) == SLAB_OBJECT_MAX_SIZE);

  /* create default cache for slab_t structures */
  __create_heart_cache(&slabs_memcache, sizeof(slab_t), "slab_t");
  
  /* create default cache for memcache_t structures */
  __create_heart_cache(&caches_memcache, sizeof(memcache_t), "memcache_t");
  __create_generic_caches();
}

int destroy_memcache(memcache_t *memcache)
{
  int c;
  list_node_t *n, *safe;
  page_frame_t *page;

  if (unlikely(memcache->flags & SMCF_IMMORTAL))
    return -EINVAL;
  
  rwsem_down_write(&memcaches_rwlock);
  memcache->usecount--;
  SLAB_DBG_ASSERT(memcache->usecount >= 0);
  if (!memcache->usecount) {
    list_del(&memcache->memcache_node);
  }
  else {
    return 0;
  }
  
  rwsem_up_write(&memcaches_rwlock);
  /* Drop per-cpu slabs */
  for_each_cpu(c) {
    if (memcache->active_slabs[c])
      destroy_slab(memcache->active_slabs[c]);
  }

  /* Destroy full slabs if any */
  list_for_each_safe(&memcache->full_slabs, n, safe) {
    page = list_entry(n, page_frame_t, node);
    destroy_slab(__page2slab(page));
  }

  /* Destroy partial and empty slabs */
  list_for_each_safe(&memcache->avail_slabs, n, safe) {
    page = list_entry(n, page_frame_t, node);
    destroy_slab(__page2slab(page));
  }

  /* Finally, destroy the memcache itself */
  memfree(memcache);  
  return 0;
}

#define MEMCACHE_MERGE_MASK (MMPOOLS_MASK | SMCF_UNIQUE)
memcache_t *create_memcache(const char *name, size_t size,
                            int pages, memcache_flags_t flags)
{
  memcache_t *memcache = NULL;

  flags &= SMCF_MASK;
  if (size < SLAB_OBJECT_MIN_SIZE) {
    size = SLAB_OBJECT_MIN_SIZE;
  }
  else if (size > SLAB_OBJECT_MAX_SIZE) {
    kprintf(KO_ERROR "Failed to create memory cache \"%s\" of size %d: "
            "required size exeeds max available slab object size(%d)!\n",
            name, size, SLAB_OBJECT_MAX_SIZE);
    goto out;
  }

  if (!(flags & SMCF_UNIQUE)) {
    /* Try to attach to alredy registered cache if any */
    memcache_t *victim;
    int irqstat;

    rwsem_down_read(&memcaches_rwlock);
    list_for_each_entry(&memcaches_list, victim, memcache_node) {
      if ((victim->object_size == size) &&
          ((victim->flags & MEMCACHE_MERGE_MASK) ==
           (flags & MEMCACHE_MERGE_MASK))) {
        
        interrupts_save_and_disable(irqstat);
        memcache_lock(victim);
        victim->usecount++;        
        memcache_unlock(victim);
        interrupts_restore(irqstat);
        memcache = victim;        

        break;
      }
    }

    rwsem_up_read(&memcaches_rwlock);
    if (memcache) { /* was successfully merged */
      SLAB_VERBOSE(NULL, ">> Merge size %d with memory cache %s\n",
                   size, memcache->name);
      goto out;
    }
  }

  
  memcache = alloc_from_memcache(&caches_memcache, 0);
  if (!memcache) {
    SLAB_PRINT_ERROR("Failed to allocate memory cache \"%s\" of size %zd. "
                     "ENOMEM\n", name, size);
    goto out;
  }

  prepare_memcache(memcache, size, pages);
  memcache->flags = flags;
  bit_clear(&memcache->flags, bitnumber(__SMCF_BIT_LOCK));
  register_memcache(memcache, name);
  
  SLAB_VERBOSE(memcache, ">> Created memory cache %s. "
               "[Size = %d, pages_per_slab = %d, mark = %#x]\n", memcache->name,
               memcache->pages_per_slab, memcache->mark_version);

  if (!(flags & SMCF_LAZY)) {
    int c;
    slab_t *slab;

    for_each_cpu(c) {
      slab = create_new_slab(memcache, 0);
      if (!slab) {
        SLAB_PRINT_ERROR("Failed to allocate slab for memcache %s "
                         "of size %d (CPU №%d)\n",
                         memcache->name, memcache->object_size, cpu_id());
        goto error;
      }

      slab->state = SLAB_ACTIVE;
      memcache->stat.nslabs++;
      memcache->active_slabs[c] = slab;
    }
  }
    
out:
  return memcache;

error:
  destroy_memcache(memcache);
  return NULL;
}

void *alloc_from_memcache(memcache_t *memcache, int alloc_flags)
{
  slab_t *slab, *slab_tmp;
  void *obj = NULL;
  int irqstat;

  SLAB_DBG_ASSERT(memcache != NULL);
  interrupts_save_and_disable(irqstat);
  slab = __get_percpu_slab(memcache);
  if (unlikely(slab == NULL)) {
    memcache_lock(memcache);
    goto take_new_slab;
  }
  if (likely(slab->nobjects)) {
    goto alloc_obj;
  }

  /*
   * Check if there eixist some free objects in lazy freelist.
   * If so, we just swap objects from lazy list to slab freelist.
   */
  slab_lock(slab);
  if (slab->pages->slab_num_lazy_objs) {
    SLAB_VERBOSE(memcache, ">> taking %d objects from lazy freelist for "
                 "percpu slab %p (CPU №%d)\n", slab->pages->slab_num_lazy_objs,
                 slab, cpu_id());

    slab_swap_freelist(slab);
    SLAB_DBG_ASSERT(slab->nobjects > 0);
    slab_unlock(slab);
    goto alloc_obj;
  }

  slab->state = SLAB_FULL;
  memcache_lock(memcache);
  slab_register_full(memcache, slab);
  slab_unlock(slab);

  SLAB_VERBOSE(memcache, ">> move precpu slab %p (CPU №%d) %s to %s state\n",
               slab, cpu_id(), __slab_state_to_string(SLAB_ACTIVE),
               __slab_state_to_string(SLAB_FULL));

take_new_slab:
  slab = try_get_avail_slab(memcache);
  if (slab) {
    memcache_unlock(memcache);
    goto alloc_obj;
  }

  __set_percpu_slab(memcache, NULL);
  memcache_unlock(memcache);

  /*
   * Unfortunaly, there are no any partial or empty slabs available,
   * so we have to allocate a new one.
   * But memory cache won't grow if caller forbid as from doing that
   * by setting SAF_DONT_GROW flag.
   */    
  if (alloc_flags & SAF_DONT_GROW) {
    SLAB_VERBOSE(memcache, ">> Slab growing failed, cuz SAF_DONT_GROW "
                 "flags set.\n");
    interrupts_restore(irqstat);
    return NULL;
  }
  if (!(alloc_flags & SAF_ATOMIC))
    interrupts_restore(irqstat);

  slab = create_new_slab(memcache, alloc_flags);
  if (!slab) {
    SLAB_VERBOSE(memcache, ">> Slab growing failed: can't create new slab. "
                 "[ENOMEM]\n");
    if (!(alloc_flags & SAF_ATOMIC))
      interrupts_restore(irqstat);
    
    return NULL;
  }

  slab->state = SLAB_ACTIVE;
  if (!(alloc_flags & SAF_ATOMIC))
    interrupts_save_and_disable(irqstat);
    
  memcache_lock(memcache);
  slab_tmp = __get_percpu_slab(memcache);
  if (likely(slab_tmp == NULL)) {
    memcache->stat.nslabs++;
    __set_percpu_slab(memcache, slab);
  }
  else {
    /* TODO DK: write comments... */
    if (memcache->stat.nempty_slabs < MEMCACHE_EMPTYSLABS_MAX) {
      slab->state = SLAB_EMPTY;
      slab_register_empty(memcache, slab);
      memcache->stat.nempty_slabs++;
    }
    else {
      destroy_slab(slab);
    }

    slab = slab_tmp;
  }
  
  memcache_unlock(memcache);

alloc_obj:
  obj = slab_get_free_obj(slab);
  interrupts_restore(irqstat);

  /*SLAB_VERBOSE(memcache, ">> (%s) Allocated object %p from slab %p "
    "(%d objects rest)\n", memcache->name, obj, slab, slab->nobjects);*/
  
  slab_dbg_check_page(slab, (void *)PAGE_ALIGN_DOWN((uintptr_t)obj));
  slab_dbg_check_object(slab, obj);
  
  if (alloc_flags & SAF_MEMNULL) {
    memset(obj, 0, memcache->object_size);
  }

  return obj;
}

void *__memalloc(size_t size, int alloc_flags)
{
  int idx = bit_find_msf(size);

  if (!is_powerof2(size))
    idx++;
  if ((idx < 0) || (idx > LAST_GENSLABS_POW2)) {
    SLAB_PRINT_ERROR(NULL, ">> Failed to allocate object of size %zd.\n"
                     "  --> ((idx < 0) || (idx > LAST_GENSLABS_POW2))\n",
                     size);
    return NULL;
  }

  if (idx > FIRST_GENSLABS_POW2) {
    idx -= FIRST_GENSLABS_POW2;
  }
  else {
    idx = 0;
  }

  return alloc_from_memcache(generic_memcaches[idx], alloc_flags);
}

void *memalloc(size_t size)
{
  return __memalloc(size, 0);
}

void memfree(void *mem)
{
  slab_t *slab;

  slab_dbg_check_address(mem);
  slab = __slab_get_by_addr(mem);
  slab_dbg_check_page(slab, (void *)PAGE_ALIGN_DOWN((uintptr_t)mem));
  free_slab_object(slab, mem);
}

#if 0 /* TODO DK! */
#ifdef CONFIG_DEBUG_SLAB
void slab_verbose_enable(void)
{
  spinlock_lock(&verbose_lock);
  verbose = true;
  spinlock_unlock(&verbose_lock);
}

void slab_verbose_disable(void)
{
  spinlock_lock(&verbose_lock);
  verbose = false;
  spinlock_unlock(&verbose_lock);
}
#endif /* CONFIG_DEBUG_SLAB */
#endif
