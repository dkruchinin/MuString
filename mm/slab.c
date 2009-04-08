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
#include <eza/spinlock.h>
#include <eza/rwsem.h>
#include <eza/errno.h>
#include <mlibc/types.h>

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

static void destroy_slab(slab_t *slab);
static void free_slab_object(slab_t *slab, void *obj);

#ifdef CONFIG_DEBUG_SLAB_MARK_PAGES
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
static inline int __count_objects_per_slab(memcache_t *memcache)
{
  return (((PAGE_SIZE - SLAB_PAGE_MARK_SIZE)
           / memcache->object_size) * memcache->pages_per_slab);
}

static void slab_dbg_mark_pages(memcache_t *memcache, page_frame_t *pages)
{
  unsigned int page_mark = SLAB_PAGE_MARK_BASE + memcache->mark_vers;
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
    if (page_mark == (SLAB_PAGE_MARK_BASE + memcache->mark_vers)) {
      found_owner = true;
      break;
    }
  }
  
  rwsem_up_read(&memcaches_rwlock);
  if (!found_owner) {
    kprintf(KO_ERROR "Failed to locate memory cache owning by address %p\n"
            "  -> [Page mark valude: %#x], SLAB_PAGE_MARK_BASE = %#x\n",
            addr, page_mark, SLAB_PAGE_MARK_BASE);
    BUG();
  }
}

static void slab_dbg_check_page(slab_t *slab, void *page_start)
{
  unsigned int page_mark = *(unsigned int *)page_start;
  
  __slab_lock(slab);
  if (page_mark != (SLAB_PAGE_MARK_BASE + slab->memcache->merk_vers)) {
    kprintf(KO_ERROR "Slab %p of memory cache \"%s\" (size = %d) has "
            "invalid page frame №%#x.\n"
            "  -> Expected page mark  = %#x\n"
            "  -> Given page mark     = %#x\n"
            "  -> SLAB_PAGE_MARK_BASE = %#x\n",
            slab, slab->memcache->name, slab->memcache->object_size,
            virt_to_pframe_id(page_start),
            (SLAB_PAGE_MARK_BASE + slab->memcache->merk_vers),
            page_mark, SLAB_PAGE_MARK_BASE);
    BUG();
  }
  
  __slab_unlock(slab);
}
#else
#define __objrealaddr(obj, add) ((char *)(obj) + (add))

/* Get number of objects fitting in slabs of given memory cache */
static inline int __count_objects_per_slab(memcache_t *memcache)
{
  return (((uintptr_t)cache->pages_per_slab << PAGE_WIDTH) /
          cache->object_size);
}

#define slab_dbg_mark_pages(memcache, pages)
#define slab_dbg_check_address(addr)
#define slab_dbg_check_page(slab, page_start)
#endif /* CONFIG_DEBUG_SLAB_MARK_PAGES */

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

  return ((char *)obj + ((leftright < 0) ? -SLAB_LEFTGUARD_OFFS : SLAB_LEFTGUARD_OFFS));
}

/* Check if free slab object is valid one and panic if not so. */
static void slab_dbg_check_object(slab_t *slab, void *obj)
{
  if (unlikely(slab->memcache->object_size) < SLAB_OBJDEBUG_MINSIZE)
    return;
  else {
    char *p = (char *)obj;
    int i = 0;

    __slab_lock(slab);
    if (*(unsigned int *)p != SLAB_OBJLEFT_GUARD)
      goto inval;

    p += SLAB_RIGHTGUARD_OFFS;
    if (*(unsigned int *)p != SLAB_OBJRIGHT_GUARD) {
      i++;
      goto inval;
    }

    __slab_unlock(slab);
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
#define slab_dbg_set_objguards()
#define slab_dbg_check_object(slab, obj)
#define __objoffs(slab, obj, leftright) (obj)
#endif /* CONFIG_DEBUG_SLAB_OBJGUARDS */

#ifdef CONFIG_DEBUG_SLAB
#define SLAB_VERBOSE(fmt, args...)                      \
  do {                                                  \
    if (verbose) {                                      \
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

#else
#define SLAB_VERBOSE(fmt, args...)
#define __display_statistics(cache)
#define __register_memcache_dbg(cache, name) (0)
#define __prepare_slab_pages_dbg(cache, pages)
#define __slab_page_is_valid_dbg(slab, page) (true)
#define __validate_slab_object_dbg(slab, obj)
#define __unregister_memcache_dbg(cache)
#endif /* CONFIG_DEBUG_SLAB */

/**************************************************************
 * >>> LOCAL LOCKING FUNCTIONS
 */

/* memory cache locking API */
#define __lock_memcache(cache)                  \
  (spinlock_lock_bit(&(cache)->flags, __SMCF_LOCK_BIT))
#define __unlock_memcache(cache)                \
  (spinlock_unlock_bit(&(cache)->flags, __SMCF_LOCK_BIT))

/* SLAB locking API */
#define __lock_slab_page(pg)                    \
  lock_page_frame(pg, PF_LOCK)
#define __unlock_slab_page(pg)                  \
  unlock_page_frame(pg, PF_LOCK)
#define __slab_lock(slab)                       \
  __lock_slab_page((slab)->pages)
#define __slab_unlock(slab)                     \
  __unlock_slab_page((slab)->pages)
/***************************************************************/

/* Per-cpu slabs management */
#define __get_percpu_slab(memcache)             \
  ((memcache)->active_slabs[cpu_id()])
#define __set_percpu_slab(memcache, slab)       \
  ((memcache)->active_slabs[cpu_id()] = (slab))

/***************************************************************
 * >>> Register/unregister functions for different slab states
 */
#ifdef CONFIG_DEBUG_SLAB
#define slab_register_full(memcache, slab)      \
  list_add2tail(&(memcache)->full_slabs, &(slab)->pages->node)
#define slab_unregister_full(slab)              \
  list_del(&(slab)->pages->node)
#else
#define slab_register_full(memcache, slab)
#define slab_unregister_full(slab)
#endif /*CONFIG_DEBUG_SLAB */

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
  ((slab_t *)(page)->slab_ptr)

/* Return slab owning by page address "addr" fits in. */
#define __slab_get_by_addr(addr)                \
  __page2slab(virt_to_pframe((void *)PAGE_ALIGN_DOWN(uintptr_t)(addr)))

static char *__slab_stat_names[] = {
  "SLAB_EMPTY", "SLAB_PARTIAL", "SLAB_FULL", "SLAB_ACTIVE", "!!UNKNOWN!!" };
#define __slab_state_to_string(stat)           \
  (__slabs_stat_names[((stat) - 1) % 5])

/*
 * Each slab contains list of free objects. Each free object
 * is a chunk of memory, containing an address of next free object.
 * If there is only one free object in the list, the pointer to the
 * next free object will be equal to SLAB_OBJLIST_END.
 */
static inline void slab_add_free_obj(slab_t *slab, void *obj)
{
  uintptr_t *p = __objoffs(slab, obj, 1);

  if (likely(slab->objects != NULL)) {
    *p = (uintptr_t)slab->objects;
  }
  else {
    *p = SLAB_OBJLIST_END;
  }
    
  slab->objects = (void *)p;
  slab_dbg_set_objguards(slab, obj);
  slab->nobjects++;
}

static inline void *slab_get_free_obj(slab_t *slab)
{
  uintptr_t *p = slab->objects;

  ASSERT_DBG(slab->objects != NULL);
  if (likely(*p != SLAB_OBJLIST_END)) {
    slab->objects = (void *)*p;
  }
  else {
    slab->objects = NULL;
  }

  slab->nobjects--;
  slab_dbg_check_objguards(slab, __objoffs(slab, p, -1));
  return __objoffs(slab, p, -1);
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
  int cur = __count_objects_per_slab(slab->memcache) - skip_objs;


  next = __objrealaddr(pframe_to_virt(slab->pages),
                      skip_objs * cache->object_size);
  while (cur > 0) {
    obj = next;
    slab_add_free_obj(slab, obj);
    next = __objrealaddr(obj, slab->memcache->object_size);
    cur--;
  }

  SLAB_VERBOSE("[%s] slab %p has %zd free objects\n",
               slab->memcache->name, slab, slab->nobjects);
}

/*
 * alloc slab pages doesn't care about memory cache locking
 * So, before calling alloc_slab_pages, user should lock cache
 * by himself. (if cache locking is necessary)
 */
static page_frame_t *alloc_slab_pages(memcache_t *memcache, pfalloc_flags_t pfa_flags)
{
  page_frame_t *pages = NULL;

  pfa_flags |= (memcache->flags & MMPOOL_MASK);
  pages = alloc_pages(memcache->pages_per_slab, pfa_flags);
  if (!pages) {
    return NULL;
  }

  slab_dbg_mark_pages(memcache, pages);
  return pages;
}

static inline void free_slab_pages(memcache_t *cache, page_frame_t *pages)
{
  free_pages(pages, cache->pages_per_slab);
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
static slab_t *__create_slabs_slab(pfalloc_flags_t pfa_flags)
{
  page_frame_t *pages = alloc_slab_pages(&slabs_memcache, pfa_flags);
  slab_t *slab = NULL;

  if (!pages) {
    SLAB_PRINT_ERROR("(%s) Failed to allocate %d pages for slab!\n",
                     slabs_memcache.name, slabs_memcache.pages_per_slab);
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
  
  SLAB_VERBOSE("Create new slab(%p), (objs=%d) for slabs memory cache(sz=%d)\n",
               slab, slab->nobjects, slab->memcache->object_size);

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

  __display_statistics(memcache);
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

/* This function must be called with interrupts disabled. */
static void free_slab_object(slab_t *slab, void *obj)
{
  memcache_t *memcache = slab->memcache;

  /*
   * If given slab has SLAB_ACTIVE state, it wouldn't become
   * empty or partial. Also we don't need to lock it, because
   * local interrupts are already disabled and SLAB_ACTIVE state
   * tells us that given slab is per-cpu guy.
   * FIXME DK: Is there a possibility of freeing object belonging
   * to SLAB_ACTIVE slab from another CPU? If so(and it seems so),
   * slab *must* be locked anyway!
   */
  if (slab->state == SLAB_ACTIVE) {
    slab_add_free_obj(slab, obj);
    SLAB_VERBOSE(memcache, ">> free object %p into %s slab %p\n",
                 obj, __slab_state_to_string(SLAB_INUSE), slab);
    return;
  }

  /*
   * Otherwise given slab may have either SLAB_FULL or SLAB_PARTIAL
   * state. Here we have to manually lock the slab.
   */
  __slab_lock(slab);
  slab_add_free_obj(slab, obj);

  SLAB_VERBOSE(memcache, ">> free object %p into %s slab %p\n", obj,
               __slab_state_to_string(slab->state), slab);

  switch (slab->state) {
      case SLAB_FULL:
        /* If slab was full, it becomes partial after item is fried */
        slab->state = SLAB_PARTIAL;
        __lock_memcache(memcache);
        slab_unregister_full(slab);
        slab_register_partial(memcache, slab);
        memcache->npartial_slabs++;
        
        SLAB_VERBOSE(memcache, ">> move slab %p from %s to %s state. "
                     "Partial slabs: %d\n",
                     slab, __slab_state_to_string(SLAB_FULL),
                     __slab_state_to_string(SLAB_PARTIAL),
                     memcache->npartial_slabs);
        
        __unlock_memcache(memcache);
        break;
      case SLAB_PARTIAL:
      {
        int objs_per_slab = __count_objects_per_slab(memcache);

        /* We don't care about partial slab until it becomes empty. */
        if ((slab->nobjects == objs_per_slab) ||
            ((memcache == &slabs_memcache) &&
             (slab->nobjects == (objs_per_slab - 1)))) {
          __lock_memcache(memcache);

          memcache->npartial_slabs--;
          slab_unregister_partial(slab);

          /*
           * If number of empty slabs exceeds SLAB_EMPTYSLABS_MAX,
           * given slab will be fried. Otherwise it will be added
           * to memory cache empty slabs use for future use.
           */
          if (memcache->nempty_slabs == SLAB_EMPTYSLABS_MAX) {
            memcache->nslabs--;

            SLAB_VERBOSE(memcache, ">> drop slab %p(became %s from %s). "
                         "%d empty slabs rest.\n", slab,
                         __slab_state_to_string(SLAB_EMPTY),
                         __slab_state_to_string(SLAB_PARTIAL),
                         memcache->nempty_slabs);
            
            __unlock_memcache(memcache);
            __unlock_slab(slab);
            
            destroy_slab(slab);
            return;
          }
          else {
            slab->state = SLAB_EMPTY;
            memcache->nempty_slabs++;
            slab_register_empty(memcache, slab);

            SLAB_VERBOSE(memcache, ">> move slab %p from %s to %s state. "
                         "Empty slabs: %d\n", slab,
                         __slab_state_to_string(SLAB_PARTIAL),
                         __slab_state_to_string(SLAB_EMPTY),
                         memcache->nempty_slabs);
            
            __unlock_memcache(memcache);
          }
        }

        break;
      }
      default:
        /*
         * Slab object is fried from may be either SLAB_FULL or SLAB_PARTIAL
         * only. And it's actually a bug if any other type is met.
         */
        panic("Unexpected slab state was met during slab object "
              "freeing (%d)\n", slab->state);
        BUG();
  }

  __slab_unlock(slab);
}

static void prepare_memcache(memcache_t *cache, size_t object_size,
                             int pages_per_slab)
{
  memset(cache, 0, sizeof(*cache));
  cache->object_size = object_size;
  cache->pages_per_slab = pages_per_slab;
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

/*
 * There are only two "heart" caches: one for memcache_t structures and another
 * one for slab_t strcutures.
 */
static void __create_heart_cache(memcache_t *cache, size_t size,
                                 const char *name)
{
  int ret, i;
  
  SLAB_VERBOSE("Creating heart cache \"%s\", object size=%d\n", name, size);

  /*
   * All "heart" caches are predefined, so we don't need to allocate
   * them dynamically.
   */
  prepare_memcache(cache, size, GENERIC_SLAB_PAGES);
  cache->flags = SLAB_GENERIC_FLAGS;
  bit_clear(&cache->flags, __MMC_LOCK_BIT);
  
  /*
   * avail_slabs list holds all slabs that may be used for
   * objects allocation. Partial slabs located before empty
   * slabs. Empty slabs always placed at the tail of the list.
   */

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
      slab = __create_slabs_slab(0);
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
    slab->state = SLAB_ACTIVE;
    cache->active_slabs[i] = slab;
    cache->nslabs++;
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
  CT_ASSERT((1 << FIRST_GENSLABS_POW2) == SLAB_OBJECT_MIN_SIZE);
  CT_ASSERT((1 << LAST_GENSLABS_POW2) == SLAB_OBJECT_MAX_SIZE);

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

void *alloc_from_memcache(memcache_t *memcache, int alloc_flags)
{
  slab_t *slab;
  void *obj = NULL;
  int irqstat;

  interrupts_save_and_disable(irqstat);
  slab = __get_percpu_slab(memcache);

  /* Check, if given slab can be used... */
  if (unlikely(!slab->nobjects)) {
    slab_t *new_slab = NULL;

    __lock_memcache(memcache);
    slab->state = SLAB_FULL;
    slab_register_full(memcache, slab);

    SLAB_VERBOSE(memcache, ">> move precpu slab %p from (CPU №%d) %s to %s\n",
                 slab, cpu_id(), __slab_state_to_string(SLAB_INUSE),
                 __slab_state_to_string(SLAB_FULL));

    /*
     * At first, try to find out if there are empty or partial slabs
     * in avail_slabs list of memory cache. If so, one of them will be
     * used. By default when slab becomes partial it is added at the
     * head of the list, if empty - at the tail. Here we take the very
     * first slab from the list.
     */
    if (!list_is_empty(&memcache->avail_slabs)) {
      page_frame_t *p = list_entry(list_node_first(&memcache->avail_slabs,
                                                   page_frame_t, node));
      new_slab = __page2slab(p);
      switch (new_slab->state) {
          case SLAB_PARTIAL:
            slab_unregister_partial(slab);
            memcache->npartial_slabs--;
            break;
          case SLAB_EMPTY:
            slab_unregister_empty(slab);
            memcache->nempty_slabs--;
            break;
          default:
            /*
             * Only empty or partial slabs may be linked in avail_slabs
             * list. If there exist slab of any other type, it's a BUG.
             */
            kpeintf(KO_ERROR "Slab with unknown state(%d) was found in "
                    "avail_slabs list of memcache %p!\n", memcache);
            BUG();
      }

      SLAB_VERBOSE(memcache, ">> set percpu slab %p for (CPU №%d). "
                   "Old state = %s.\n", slab, cpu_id(),
                   __slab_state_to_string(slab->state));

      __set_percpu_slab(memcache, new_slab);
    }

    __unlock_memcache(memcache);
    if (new_slab) {
      slab = new_slab;
      goto alloc_obj;
    }

    /*
     * Unfortunaly, there are no any partial or empty slabs available,
     * so we have to allocate a new one.
     * But memory cache won't grow if caller forbid as from doing that
     * by setting SAF_DONT_GROW flag.
     */
    interrupts_restore(irqstat);
    if (alloc_flags & SAF_DONT_GROW) {
      SLAB_VERBOSE(memcache, ">> Slab growing failed, cuz SAF_DONT_GROW "
                   "flags set.\n");
      goto out;
    }

    new_slab = create_new_slab(memcache, alloc_flags);
    if (!new_slab) {
      SLAB_VERBOSE(memcache, ">> Slab growing failed: can't create new slab. "
                   "[ENOMEM]\n");
      goto out;
    }


    interrupts_save_and_disable(irqstat);
    __lock_memcache(memcache);

    __set_percpu_slab(memcache, new_slab);
    memcache->nslabs++;

    __unlock_memcache(memcache);
    slab = new_slab;
  }

alloc_obj:
  obj = slab_get_free_obj(slab);
  interrupts_restore(irqstat);

  if (alloc_flags & SAF_MEMNULL) {
    memset(obj, 0, memcache->object_size);
  }


  slab_dbg_check_page(slab, (void *)PAGE_ALIGN_DOWN((uintptr_t)obj));
  slab_dbg_check_object(slab, obj);

out:
  return obj;
}

void *__memalloc(size_t size, int alloc_flags)
{
  int idx = bit_find_msf(size);

  if ((idx < 0) || (idx > LAST_GENSLABS_POW2)) {
    SLAB_PRINT_ERROR(NULL, ">> Failed to allocate object of size %zd.\n"
                     "  --> ((idx < 0) || (idx > LAST_GENSLABS_POW2))\n",
                     size);
    return NULL;
  }

  if (idx > FIRST_GENSLABS_POW2)
    idx -= FIRST_GENSLABS_POW2;
  else
    idx = 0;

  return alloc_from_memcache(generic_memcaches[idx], alloc_flags);
}

void *memalloc(size_t size)
{
  return __memalloc(size, 0);
}

void memfree(void *mem)
{
  slab_t *slab;
  int irqstat;

  slab_dbg_check_address(mem);
  slab = __get_slab_by_addr(mem);
  slab_dbg_check_page(page, PAGE_ALIGN_DOWN((uintptr_t)mem));

  interrupts_save_and_disable(irqstat);
  free_slab_object(slab, mem);
  interrupts_restore(irqstat);
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
