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
#include <mlibc/stddef.h>
#include <mlibc/string.h>
#include <mm/page.h>
#include <mm/idalloc.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <eza/kernel.h>
#include <eza/spinlock.h>
#include <eza/rwsem.h>
#include <eza/errno.h>
#include <eza/arch/types.h>

#define SLAB_GENERIC_FLAGS (SMCF_UNIQUE | SMCF_IMMORTAL)

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

/*
 * When DEBUG_SLAB is enabled, each memoty cache contains its unique ID.
 * When new slab is created by given memory cache all slab's pages contain
 * magic number at the very first page address. Page magic is equal to
 * SLAB_MAGIC_BASE + <cache_unique_type>. Such protection mechanism allows
 * to determine if some virtual address belongs to valid existing slab.
 *
 * Each slab contains a list of free objects. When DEBUG_SLAB is enable
 * each address of next object in objects list is protected by left and right
 * object guards. So if someone rewrite a some part of objects list, SLAB allocator
 * can easily determine it. Left object guard typically located at the start address
 * of object. Object left guard guaranties that if someone tryes to dereference already
 * free slab object, PF will be generated.
 */
#ifdef CONFIG_DEBUG_SLAB
static int next_memcache_type = SLAB_FIRST_TYPE;
static SPINLOCK_DEFINE(memcaches_lock);
static SPINLOCK_DEFINE(verbose_lock);
static bool verbose = true; /* enable/distable verbose mode */
static LIST_DEFINE(memcaches_lst); /* A list of all registered memory caches in system */
#endif /* CONFIG_DEBUG_SLAB */

static void destroy_slab(slab_t *slab);
static void free_slab_object(slab_t *slab, void *obj);

/* translate page frame to slab it belongs to */
#define __page2slab(pf)                         \
  container_of((page_frame_t *)(pf)->slab_pages_start, slab_t, pages)

/* get slab by virtual address of its object */
#define __get_slab_by_addr(addr)                \
  __page2slab(virt_to_pframe((void *)align_down((uintptr_t)(addr), PAGE_SIZE)))

/* slab locking and unlocking macros */
#define __lock_slab_page(pg) lock_page_frame(pg, PF_LOCK)
#define __unlock_slab_page(pg) unlock_page_frame(pg, PF_LOCK)

#define __slab_lock(slab)                       \
  __lock_slab_page((slab)->pages)
#define __slab_unlock(slab)                     \
  __unlock_slab_page((slab)->pages)

#ifdef CONFIG_DEBUG_SLAB
#define SLAB_VERBOSE(fmt, args...)                      \
  do {                                                  \
    if (verbose) {                                      \
      kprintf("[SLAB VERBOSE] {%s}: ", __FUNCTION__);   \
      kprintf(fmt, ##args);                             \
    }                                                   \
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

/* display cache statistics */
static inline void __display_statistics(memcache_t *cache)
{
  int full_slabs = atomic_get(&cache->nslabs) -
    atomic_get(&cache->nempty_slabs) - atomic_get(&cache->npartial_slabs);
  SLAB_VERBOSE("%s: (T: %d; F: %d; P: %d; E: %d)\n",
               cache->dbg.name, atomic_get(&cache->nslabs), full_slabs,
               atomic_get(&cache->npartial_slabs), atomic_get(&cache->nempty_slabs));
}

/* When CONFIG_DEBUG_SLAB enabled, each memory cache has "name" field. */
static inline void __register_memcache_dbg(memcache_t *cache, const char *name)
{
  strmcpy(cache->name, name, MEMCACHE_DBG_NAME_MAX);
}

static void __unregister_memcache_dbg(memcache_t *cache)
{
  if (cache->dbg.name && !(cache->flags & SMCF_GENERIC))
    memfree(cache->dbg.name);

  spinlock_lock(&memcaches_lock);
  list_del(&cache->dbg.n);
  spinlock_unlock(&memcaches_lock);
}

/* write slab page magit to the start of each slab's page */
static void __prepare_slab_pages_dbg(memcache_t *cache, page_frame_t *pages)
{
  unsigned int slab_page_magic = SLAB_MAGIC_BASE + cache->dbg.type;
  char *startpage, *first_addr = pframe_to_virt(pages);

  for (startpage = first_addr; startpage < (first_addr + (cache->pages_per_slab << PAGE_WIDTH));
       startpage += PAGE_SIZE) {
    *(unsigned int *)startpage = slab_page_magic;
  }
}

/*
 * Return real address of a slab object <obj>. If <add> is not 0,
 * add it to <obj> address and return the result.
 */
static inline void *__getobjrealaddr(slab_t *slab, void *obj, uint32_t add)
{
  char *p = obj;

  if (unlikely((uintptr_t)p & PAGE_ALIGN))
    p += SLAB_PAGE_OFFS;
  if (add) {
    /*
     * <obj> and <obj + add> may be located at different pages
     * In debug mode we're not very happy when such crap occurs...
     */
    if (unlikely(PAGE_ALIGN(p) != PAGE_ALIGN(p + add)))
      p = (char *)PAGE_ALIGN(p) + SLAB_PAGE_OFFS + add;
    else
      p += add;
  }

  return p;
}

/*
 * Renturn an object + offset of its left guard. If <leftright> < 0,
 * guard's offset will be substracted and added otherwise.
 */
static inline void *__objoffs(slab_t *slab, void *obj, int leftright)
{
  if (unlikely(slab->memcache->object_size < SLAB_OBJDEBUG_MINSIZE))
    return obj;

  return ((char *)obj + ((leftright < 0) ? -SLAB_LEFTGUARD_OFFS : SLAB_LEFTGUARD_OFFS));
}

/* When slab's object is freed we protect it by left and right guards. */
static void __slab_setfreeobj_dbg(slab_t *slab, void *obj)
{
  if (likely(slab->memcache->object_size >= SLAB_OBJDEBUG_MINSIZE)) {
    *(unsigned int *)obj = SLAB_OBJLEFT_GUARD;
    *(unsigned int *)((char *)obj + SLAB_RIGHTGUARD_OFFS) = SLAB_OBJRIGHT_GUARD;
  }
}

/* check if page guard is valid */
static bool __slab_page_is_valid_dbg(slab_t *slab, char *page)
{
  unsigned int page_guard = *(unsigned int *)page;

  if ((page_guard - slab->memcache->dbg.type) == SLAB_MAGIC_BASE)
    return true;

  return false;
}

/*
 * Validate address <addr>:
 * If address belongs to some slab, a page owning this address
 * must have valid page guard. If so, corresponding memory cache can
 * be found.
 */
static void __validate_address_dbg(void *addr)
{
  unsigned int page_guard = *(unsigned int *)align_down((uintptr_t)addr, PAGE_SIZE);
  bool cache_found = false;
  struct memcache_debug_info *dbg;

  spinlock_lock(&memcaches_lock);
  list_for_each_entry(&memcaches_lst, dbg, n) {
    if ((SLAB_MAGIC_BASE + dbg->type) == page_guard) {
      cache_found = true;
      break;
    }
  }

  spinlock_unlock(&memcaches_lock);
  if (!cache_found) {
    panic("Address %p doesn't not belong to any registered in slabs page\n"
          "Corresponding memory cache was not found.\n"
          "Value of page's guard = %#x\n",
          addr, page_guard);
  }
}

static void __validate_slab_page_dbg(slab_t *slab, char *page)
{
  memcache_t *cache = slab->memcache;

  if (!__slab_page_is_valid_dbg(slab, page)) {
    panic("Slab %p in cache %s has invalid page frame #%d\n"
          "Additional information:\n"
          " SLAB_MAGIC_BASE = %#x\n"
          " slab's cache type = %#x\n"
          " slab's page guard = %#x\n",
          slab, cache->dbg.name, virt_to_pframe_id(page),
          SLAB_MAGIC_BASE, cache->dbg.type,
          (*(unsigned int *)pframe_to_virt(slab->pages)));
  }
}

/* Check if free slab object is valid one and panic if not so. */
static void __validate_slab_object_dbg(slab_t *slab, void *obj)
{
  if (unlikely(slab->memcache->object_size) < SLAB_OBJDEBUG_MINSIZE)
    return;
  else {
    char *p = (char *)obj;
    int i = 0;

    if (*(unsigned int *)p != SLAB_OBJLEFT_GUARD)
      goto inval;

    p += SLAB_RIGHTGUARD_OFFS;
    if (*(unsigned int *)p != SLAB_OBJRIGHT_GUARD) {
      i++;
      goto inval;
    }

    return;
    
    inval:
    panic("Invalid slab object %p:\n"
          "%sGUARD was modified!\n"
          "leftguard = %#x, rightguard = %#x",
          obj, (i ? "RIGHT" : "LEFT"),
          *(unsigned int *)(p - SLAB_RIGHTGUARD_OFFS), *(unsigned int *)p);
  }
}

#else
#define SLAB_VERBOSE(fmt, args...)
#define __display_statistics(cache)
#define __register_memcache_dbg(cache, name) (0)
#define __prepare_slab_pages_dbg(cache, pages)
#define __getobjrealaddr(slab, obj, add) ((obj) + (add))
#define __objoffs(slab, obj, leftright) (obj)
#define __slab_setfreeobj_dbg(slab, obj)
#define __slab_page_is_valid_dbg(slab, page) (true)
#define __validate_slab_page_dbg(slab, page)
#define __validate_slab_object_dbg(slab, obj)
#define __validate_address_dbg(addr)
#define __unregister_memcache_dbg(cache)
#endif /* CONFIG_DEBUG_SLAB */

#ifdef CONFIG_SLAB_COLLECT_STAT
#define memcache_stat_inc(cache, ___memb)          \
  ((cache)->___memb++)
#define memcache_stat_dec(cache, ___memb)       \
  ((cache)->___memb--)
#else
#define memcache_stat_inc(cache, ___memb)
#define memcache_stat_dec(cache, ___memb)
#endif /* CONFIG_SLAB_COLLECT_STAT */

static inline int __count_objects_per_slab(memcache_t *cache)
{
  /*
   * In debug mode each page has guard written at its very first address,
   * Thus each slab has leaster objects than in non-debug mode.
   */
  if (unlikely(SLAB_PAGE_OFFS)) {
    return (((PAGE_SIZE - SLAB_PAGE_OFFS) /
             cache->object_size) * cache->pages_per_slab);
  }
  else {
    return ((cache->pages_per_slab << PAGE_WIDTH)
            / cache->object_size);
  }
}

#define __get_percpu_slab(cache)                \
  ((cache)->active_slabs[cpu_id()])

#define __lock_memcache(cache)                  \
  (spinlock_lock_bit(&(cache)->flags, __SMCF_LOCK_BIT))
#define __unlock_memcache(cache)                \
  (spinlock_unlock_bit(&(cache)->flags, __SMCF_LOCK_BIT))

/* set new percpu slab. */
static inline void __set_percpu_slab(memcache_t *cache, slab_t *slab)
{
  slab->state = SLAB_ACTIVE;
  spinlock_lock(&cache->lock);
  list_add2tail(&cache->inuse_slabs, &slab->pages->node);
  spinlock_unlock(&cache->lock);
  cache->active_slabs[cpu_id()] = slab;
}

/*
 * Each slab contains list of free objects. Each free object
 * is a chunk of memory, containing an address of next free object.
 * If there is only one free object in the list, the pointer to the
 * next free object will be equal to SLAB_OBJLIST_END.
 */

static void __slab_setfreeobj(slab_t *slab, void *obj)
{
  char *p = __objoffs(slab, obj, 1);
  
  if (unlikely(!slab->objects))
    *(char **)p = SLAB_OBJLIST_END;
  else
    *(char **)p = (char *)slab->objects;
  
  slab->objects = (char **)p;
  __slab_setfreeobj_dbg(slab, obj);
  slab->nobjects++;
}

static void *__slab_getfreeobj(slab_t *slab)
{
  char *obj = (char *)slab->objects;

  if (!slab->objects)
    return NULL;
  if (*(char **)obj == SLAB_OBJLIST_END)
    slab->objects = NULL;
  else
    slab->objects = (char **)(*(char **)obj);

  slab->nobjects--;
  return __objoffs(slab, obj, -1);
}

/*
 * Split available space in slab into slab objects
 * of fixed size. Objects are equal memory chunks
 * that will be used for allocation from given slab.
 */
static void __init_slab_objects(slab_t *slab, int skip_objs)
{
  memcache_t *cache = slab->memcache;
  char *obj = __getobjrealaddr(slab, pframe_to_virt(slab->pages),
                               skip_objs * cache->object_size);
  char *next = obj;
  int cur = __count_objects_per_slab(cache) - skip_objs;

  for (; cur > 0; cur--, obj = next) {
    __slab_setfreeobj(slab, obj);
    next = __getobjrealaddr(slab, obj, cache->object_size);
  }

  SLAB_VERBOSE("[%s] slab %p of %s has %zd free objects\n",
               slab->memcache->name, slab, cache->dbg.name, slab->nobjects);
}

static inline void slab_mark_as_full(slab_t *slab)
{
  slab->state = SLAB_FULL;
  
#ifdef CONFIG_DEBUG_SLAB
  list_add2tail(&slab->memcache->full_slabs, &slab->node);
#endif /* CONFIG_DEBUG_SLAB */
}

/*
 * alloc slab pages doesn't care about memory cache locking
 * So, before calling alloc_slab_pages, user should lock cache
 * by himself. (if cache locking is necessary)
 */
static page_frame_t *alloc_slab_pages(memcache_t *cache)
{
  page_frame_t *pages = NULL;

  pages = alloc_pages(cache->pages_per_slab, 0);
  if (pages)
    __prepare_slab_pages_dbg(cache, pages);

  return pages;
}

static inline void free_slab_pages(memcache_t *cache, page_frame_t *pages)
{
  free_pages(pages, cache->pages_per_slab);
}

static void prepare_slab_pages(slab_t *slab)
{
  int i;
    
  /*
   * All page frames in slab should have possibility to determine where the fist page frame is.
   * This is needed when we want to find slab that owns the object by
   * object virtual address.
   */
  for (i = 0; i < slab->memcache->pages_per_slab; i++) {
    page_frame_t *p = slab->pages + i;
    p->slab_pages_start = &slab->pages;
  }
}

static int prepare_slab(memcache_t *cache, slab_t *slab)
{
  memset(slab, 0, sizeof(*slab));
  slab->memcache = cache;
  slab->pages = alloc_slab_pages(cache);
  if (!slab->pages) {
    return -ENOMEM;
  }
  else {
    prepare_slab_pages(slab);
  }

  __init_slab_objects(slab, 0);  
  return 0;
}

/*
 * Slab for memory cache managing slab_t structures is
 * allocated and fried in a little diffrent way.
 */
static slab_t *__create_slabs_slab(void)
{
  page_frame_t *pages = alloc_slab_pages(&slabs_memcache);
  slab_t *slab = NULL;

  if (!pages)
    goto out;

  /*
   * When creating a new slab for slab_t structures, the very
   * first object in the slab must be reserverd for itself.
   * Thus it will be fried only after given slab is destroyed.
   */
  slab = pframe_to_virt(pages) + SLAB_PAGE_OFFS;
  slab->memcache = &slabs_memcache;
  slab->objects = NULL;
  slab->nobjects = 0;
  slab->pages = pages;
  prepare_slab_pages(slab);
  __init_slab_objects(slab, 1);
  SLAB_VERBOSE("Create new slab(%p), (objs=%d) for slabs memory cache(sz=%d)\n",
               slab, slab->nobjects, slab->memcache->object_size);

out:
  return slab;
}

static slab_t *create_new_slab(memcache_t *memcache)
{
  slab_t *new_slab = NULL;

  __lock_memcache(memcache);
  
  /*
   * At first, try to find out if there are empty or partial slabs
   * in avail_slabs list of memory cache. If so, one of them will be
   * used. By default when slab becomes partial it is added at the
   * head of the list, if empty - at the tail. Here we take the very
   * first slab in the list. If partial slabs exist it will be partial,
   * otherwise it will be empty.
   */
  if (!list_is_empty(&memcache->avail_slabs)) {
    page_frame_t *p = list_entry(list_node_first(&memcache->avail_slabs,
                                                 page_frame_t, node));

    list_del(&p->node);
    new_slab = __page2slab(p);
    switch (new_slab->state) {
        case SLAB_PARTIAL:
          memcache->npartial_slabs--;
          SLAB_VERBOSE("[%s] slab %p was taken from partial "
                       "slabs(avail objs=%d)\n", memcache->name, new_slab,
                       new_slab->nobjects);
          break;
        case SLAB_EMPTY:
          memcache->nempty_slabs--;
          SLAB_VERBOSE("[%s] slab %p was taken from empty "
                       "slabs(avail objs=%d)\n", memcache->name, new_slab,
                       new_slab->nobjects);
          break;
        default:
          /*
           * Only empty or partial slabs may be linked in avail_slabs
           * list. If there exist slab of any other type, it's a BUG.
           */
          panic("Slab with unknown state(%d) was found in "
                "avail_slabs list of memcache %p!\n", memcache);
    }

    __unlock_memcache(memcache);
    goto out;
  }
  
  /*
   * Unfortunaly, there are no any partial or empty slabs available,
   * so we have to allocate a new one.
   * Note: One memory cache(slabs_memcache) has specific allocation policy
   * different from allocation policy of other slabs...
   */
  __unlock_memcache(memcache);
  if (likely(memcache != &slabs_memcache)) {
    int ret;

    if (memcache->flags & SMCF_CONST) {
      new_slab = NULL;
      goto out;
    }

    new_slab = alloc_from_memcache(&slabs_memcache);
    ret = prepare_slab(memcache, new_slab);
    if (ret) {
      SLAB_VERBOSE("[%s] Failed to allocate new slab: [RET = %d]\n",
                   memcache->name, ret);
      
      destroy_slab(new_slab);
      new_slab = NULL;
    }

    SLAB_VERBOSE("[%s] New slab created.\n", memcache->name);
  }

out:
  __display_statistics(cache);
  return new_slab;
}

static void destroy_slab(slab_t *slab)
{
  page_frame_t *slab_page = slab->pages;
  memcache_t *cache = slab->memcache;

  SLAB_VERBOSE("(destroy_slab): %s's slab %p destroying\n",
               slab->memcache->dbg.name, slab);    
  if (slab->memcache != &slabs_memcache) {
    slab_t *slabs_slab = __get_slab_by_addr(slab);
    free_slab_object(slabs_slab, slab);
  }

  free_slab_pages(cache, slab_page);
}

static void free_slab_object(slab_t *slab, void *obj)
{
  memcache_t *cache = slab->memcache;

  __slab_lock(slab);
  __slab_setfreeobj(slab, obj);

  /*
   * If given slab is not a percpu guy, it may
   * become partial(if it was full) or empty(if it was partial)
   * after its object is freed. This means slab may be moved to another
   * list in memory cache or simply freed.
   * Note: active slabs(that are currently used as percpu guys) are not
   * become partial or empty until they become full and replaced with another slabs.
   */
  if (slab->state != SLAB_ACTIVE) {
    int objects_per_slab = __count_objects_per_slab(cache);

    if (slab->state == SLAB_FULL) {
      /* Slab became full, so it is moved to "partial" list */
      atomic_inc(&cache->npartial_slabs);
      spinlock_lock(&cache->lock);
      slab->state = SLAB_PARTIAL;
      list_del(&slab->pages->node);
      list_add2head(&cache->available_slabs, &slab->pages->node);
      spinlock_unlock(&cache->lock);
      SLAB_VERBOSE("(free_slab_object) %s's slab %p(nobjs=%d) became partial\n",
                   cache->dbg.name, slab, slab->nobjects);
      __display_statistics(cache);
    }
    else if (likely(slab->nobjects == objects_per_slab) ||
             unlikely((cache == &slabs_memcache) &&
                      (slab->nobjects == objects_per_slab - 1))) {
      /*
       * Slab became empty, so it either is moved to "empty" list or
       * it is freed(if parent memory cache has too much empty slabs).
       * Also it may occur slab just has became free is a "child" of slabs
       * memory cache that has a little different allocating and freeing
       * policyes than all other memory caches.
       */      
      if (unlikely((atomic_get(&cache->nempty_slabs) + 1) > SLAB_EMPTYSLABS_MAX)) {
        SLAB_VERBOSE("(free_slab_object) %s's slab %p(nobjs=%d) became empty and it will be destroyed\n",
                     cache->dbg.name, slab, slab->nobjects);
        atomic_dec(&cache->npartial_slabs);
        spinlock_lock(&cache->lock);
        list_del(&slab->pages->node);
        spinlock_unlock(&cache->lock);
        atomic_dec(&cache->nslabs);
        __slab_unlock(slab);
        destroy_slab(slab);
        __display_statistics(cache);        
        return;
      }

      SLAB_VERBOSE("(free_slab_object) %s's slab %p(nobjs=%d) became empty\n",
                   cache->dbg.name, slab, slab->nobjects);      
      atomic_inc(&cache->nempty_slabs);
      spinlock_lock(&cache->lock);
      slab->state = SLAB_EMPTY;
      atomic_dec(&cache->npartial_slabs);
      list_del(&slab->pages->node);
      list_add2tail(&cache->available_slabs, &slab->pages->node);
      spinlock_unlock(&cache->lock);
      __display_statistics(cache);
    }
  }

  __slab_unlock(slab);
}

static void prepare_memcache(memcache_t *cache, size_t object_size,
                             int pages_per_slab)
{
  memset(cache, 0, sizeof(*cache));
  cache->object_size = object_size;
  cache->pages_per_slab = pages_per_slab;

  /*
   * avail_slabs list holds all slabs that may be used for
   * objects allocation. Partial slabs located before empty
   * slabs. Empty slabs always placed at the tail of the list.
   */
  list_init_head(&cache->avail_slabs);  
}

/*
 * Register new memory cache. By default all memory caches
 * linked in one sorted "memcaches_list".
 */
static void register_memcache(memcache_t *memcache, const char *name)
{
  __register_memcache_dbg(memcache, name);
  rwsem_down_write(&memcaches_rwlock);
  if (likely(!list_is_empty(&memcaches_list))) {
    memcache_t *c;

    list_for_each_entry(&memcaches_list, c, memcache_node) {
      if (c->object_size > memcache->object_size) {
        list_add_before(&c->memcache_node, &memcache->memcache_node);
        break;
      }
    }
  }
  else {
    list_add2tail(&memcaches_list, &memcache->memcache_node);
  }
  
  rwsem_up_write(&memcaches_rwlock);
} 

static int occupy_percpu_slabs(memcache_t *memcache)
{
  return 0; /* TODO DK: */
}

/*
 * There are only two "heart" caches: one for memcache_t structures and another
 * one for slab_t strcutures.
 */
static void __create_heart_cache(memcache_t *cache, size_t size,
                                 const char *name)
{
  int ret, i;

  SLAB_VERBOSE("creating heart cache %s, object size=%d\n", name, size);

  /*
   * All "heart" caches are predefined, so we don't need to allocate
   * them dynamically.
   */
  prepare_memcache(cache, size, GENERIC_SLAB_PAGES);
  cache->flags = SLAB_GENERIC_FLAGS;
  bit_clear(&cache->flags, __MMC_LOCK_BIT);
  register_memcache(cache, name);
  SLAB_VERBOSE("memory cache %s was initialized. (size = %d, pages per slab = %d)\n",
               cache->name, cache->object_size, cache->pages_per_slab);
  ret = -ENOMEM;
  for_each_cpu(i) {
    slab_t *slab;

    /*
     * For this moment we can not allocate slab_t structure via slab,
     * so we are allocating it using init-data allocator for each CPU.
     */
    if (cache == &slabs_memcache) {
      slab = __create_slabs_slab();
      if (!slab)
        goto err;
    }
    else {
      slab = alloc_from_memcache(&slabs_memcache);
      if (!slab)
        goto err;

      ret = prepare_slab(cache, slab);
      if (ret)
        goto err;
    }

    SLAB_VERBOSE("Created percpu slab for %s, cpu=%d\n", name, i);
    cache->active_slabs[i] = slab;
    __stat_inc_nslabs(cache);    
    __display_statistics(cache);
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
  size_t size = SLAB_OBJECT_MAX_SIZE;
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

    generic_memcaches[i] = create_memcache(cache_name, size,
                                           CONFIG_SLAB_DEFAULT_NUMPAGES,
                                           SLAB_GENERIC_FLAGS);
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
  /* create default cache for slab_t structures */
  __create_heart_cache(&slabs_memcache, sizeof(slab_t), "slab_t");
  /* create default cache for memcache_t structures */
  __create_heart_cache(&caches_memcache, sizeof(memcache_t), "memcache_t");
  __create_generic_caches();
}

memcache_t *create_memcache(const char *name, size_t size,
                            int pages, memcache_flags_t flags)
{
  memcache_t *cache = NULL;
  int ret, i;
  bool was_const = false;

  if (size < SLAB_OBJECT_MIN_SIZE)
    size = SLAB_OBJECT_MIN_SIZE;
  else if (size > SLAB_OBJECT_MAX_SIZE) {
    SLAB_VERBOSE("Failed to create slab %s of size %zd: Slab size "
                 "exeeds max allowed object size (%d).\n",
                 name, size, SLAB_OBJECT_MAX_SIZE);
    goto err;
  }

  cache = alloc_from_memcache(&caches_memcache);
  if (!cache)
    goto err;

  prepare_memcache(cache, size, pages);
  cache->flags = flags;
  if (cache->flags & SMCF_CONST) {
    cache->flags &= ~SMCF_CONST;
    was_const = true;
  }
  
  ret = __register_memcache_dbg(cache, name);
  SLAB_VERBOSE("New cache %s(pages %d, sz=%d) was registered and prepared\n",
               name, pages, size);
  if (ret)
    goto err;
  for_each_cpu(i) {
    slab_t *slab;

    slab = create_new_slab(cache);
    if (!slab)
      goto err;

    slab->state = SLAB_ACTIVE;
    list_add2tail(&cache->inuse_slabs, &slab->pages->node);
    cache->active_slabs[i] = slab;
    SLAB_VERBOSE("Cache %s: new percpu slab was created for cpu #%d\n", name, i);    
    __display_statistics(cache);
  }
  if (was_const)
    cache->flags |= SMCF_CONST;

  kprintf("[SLAB] Memory cache \"%s\" was created\n", name);
  SLAB_VERBOSE(" [pages per slab:%d, slabs:%d, objsize=%d]\n",
               cache->pages_per_slab, atomic_get(&cache->nslabs), cache->object_size);
  return cache;
  
err:
  if (cache) {
    for_each_cpu(i) {
      slab_t *slab = cache->active_slabs[i];
      if (!slab)
        continue;

      destroy_slab(slab);
    }

    memfree(cache);
  }
  
  return NULL;
}

#define MEMCACHE_MERGE_MASK (SMCF_POISON | SMCF_ATOMIC | SMCF_CONST)
memcache_t *create_memcache_ext(const char *name, size_t size,
                                int pages, memcache_flags_t flags,
                                uint8_t mempool_type)
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

    rwsem_down_read(&memcaches_rwlock);
    list_for_each_entry(&memcaches_list, victim, memcache_node) {
      /* TODO DK: add description... */
      if (((victim->object_size - size) < MAX_ALLOWED_FRAG) &&
          !(victim->flags & SMCF_UNIQUE) &&
          ((vimctim->flags & MEMCACHE_MERGE_MASK) ==
           (flags & MEMCACHE_MERGE_MASK)) &&
          victim->mempool_type == mempool_type) {
        
        __lock_memcache(victim);
        victim->users++;
        memcache = victim;
        __unlock_memcache(victim);

        break;
      }
    }

    rwsem_up_read(&memcaches_rwlcok);
    if (memcache) /* was successfully merged */
      goto out;
  }

  
  memcache = alloc_from_memcache(&caches_memcache);
  if (!memcache) {
    SLAB_VERBOSE("Failed to allocate memory cache \"%s\" of size %zd. "
                 "ENOMEM\n", name, size);
    goto out;
  }

  prepare_memcache(memcache, size, pages);
  memcache->flags = flags;
  memcache->users = 1;
  if (flags & SMCF_OCCUPY) {
    occupy_percpu_slabs(memcache);
  }
  
  bit_clear(&memcache->flags, __SMCF_LOCK_BIT);
  register_memcache(memcache, name);
  
out:
  return memcache;
}

int destroy_memcache(memcache_t *cache)
{
  page_frame_t *pf;
  slab_t *slab;

  if (cache->flags & SMCF_GENERIC) {
    kprintf(KO_WARNING "Attemption to destroy *generic* memory cache %p\n", cache);
    return -EINVAL;
  }
  spinlock_lock(&cache->lock);
  SLAB_VERBOSE("Destroying memory cache %s...\n", cache->dbg.name);
  __unregister_memcache_dbg(cache);
  list_for_each_entry(&cache->inuse_slabs, pf, node) {
    __lock_slab_page(pf);
    destroy_slab(slab);
    __unlock_slab_page(pf);
  }
  if (atomic_get(&cache->npartial_slabs) || atomic_get(&cache->nempty_slabs)) {
    list_for_each_entry(&cache->inuse_slabs, pf, node) {
      __lock_slab_page(pf);
      destroy_slab(slab);
      __unlock_slab_page(pf);
    }
  }

  __unregister_memcache_dbg(cache);
  spinlock_unlock(&cache->lock);
  memfree(cache);
  
  return 0;
}

void *__alloc_from_memcache(memcache_t *memcache, int flags)
{
  slab_t *slab;
  void *obj = NULL;
  int irqstat;

  if (!(flags & SBF_FROM_INTR)) {
    preept_disable();
  }
  else {
    irqstat = is_interrupts_disable();
    interrupts_disable();
  }

  slab = __get_percpu_slab(memcache);
  if (likely(slab)) {
    if (likely(slab->objects != 0)) {
      goto alloc_from_slab;
    }
    else {
      slab_t *new_slab;

      __lock_memcache(memcache);
      slab_mark_as_full(slab);
      new_slab = create_new_slab(memcache);
    }
  }
}

void *alloc_from_memcache(memcache_t *cache, int alloc_flags)
{
  slab_t *slab;
  void *obj = NULL;
  int irqstat;

  /* firstly try to get percpu slab */
  slab = __get_percpu_slab(cache);
  if (unlikely(!slab->nobjects)) {
    slab_t *old_slab = slab;
    
    /*
     * Unfortunately our percpu guy is already full,
     * this means it must be replaced(if there is enough memory
     * to allocate it :)).
     */
    __slab_lock(old_slab); /* Full slab is locked only for disabling preemption */
    slab->state = SLAB_FULL;
    slab = create_new_slab(slab->memcache);
    if (!slab)
      goto out;

    SLAB_VERBOSE("(alloc_from_memcache): Create new percpu slab for cache %s, cpu = %d\n",
                 slab->memcache->dbg.name, cpu_id());
    __set_percpu_slab(cache, slab);
    __slab_unlock(old_slab);
    __display_statistics(cache);
  }

  __slab_lock(slab);
  obj = __slab_getfreeobj(slab);
  __validate_slab_page_dbg(slab, (char *)align_down((uintptr_t)obj, PAGE_SIZE));
  __validate_slab_object_dbg(slab, obj);
  __slab_unlock(slab);  
  
  out:
  return obj;
}

void *memalloc(size_t size)
{
  int idx = bit_find_msf(round_up_pow2(size));

  if ((idx < 0) || (idx > LAST_GENSLABS_POW2))
    return NULL;

  if (idx > FIRST_GENSLABS_POW2)
    idx -= FIRST_GENSLABS_POW2;
  else
    idx = 0;
  
  return alloc_from_memcache(generic_memcaches[idx]);
}

void memfree(void *mem)
{
  slab_t *slab;

  /* basic validation procedures (if CONFIG_DEBUG_SLAB is enabled...) */
  __validate_address_dbg(mem);
  slab = __get_slab_by_addr(mem);
  __validate_slab_page_dbg(slab, (char *)align_down((uintptr_t)mem, PAGE_SIZE));
  free_slab_object(slab, mem);
}

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
