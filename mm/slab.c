#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mlibc/string.h>
#include <mm/page.h>
#include <mm/idalloc.h>
#include <mm/pfalloc.h>
#include <eza/kernel.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/types.h>

static int generic_caches_num = -1;
static memcache_t *generic_caches = NULL;
static memcache_t caches_cache;
static memcache_t slabs_cache;

#ifdef DEBUG_SLAB
typedef enum __memcache_type {
  MEMCACHE_GENERIC = 1,
  MEMCACHE_NORMAL,
} memcache_type_t;

static int next_memcache_type = SLAB_FIRST_TYPE;
static RW_SPINLOCK_DEFINE(memcaches_lock);
static list_head_t memcaches_lst;
#endif /* DEBUG_SLAB */

static void free_slab_object(slab_t *slab, void *obj);

#define __slab_first_addr(slab)                     \
  ((pframe_to_virt(slab->pages) + SLAB_PAGE_OFFS))
#define __get_percpu_slab(cache)                \
  ((cache)->cpu_slabs[cpu_id()])
#define __set_percpu_slab(cache, slab)          \
  do {                                          \
    ((cache)->cpu_slabs[cpu_id()] = (slab));    \
    (slab)->pages->__flags |= SLAB_PERCPU;      \
  } while (0)
#define __page2slab(page)                       \
  container_of(page, slab_t, pages)
#define __get_slab_by_addr(addr)                \
  __page2slab(round_down(addr, PAGE_SIZE))

/* slab states control */
#define __set_slab_state(slab, state)               \
  do {                                              \
    (slab)->pages->__flags &= ~SLAB_STATES_MASK;    \
    (slab)->pages->__flags |= state;                \
  } while (0)
#define __slab_is_empty(slab)                   \
  ((slab)->pages->__flags & SLAB_EMPTY)
#define __slab_is_full(slab)                    \
  ((slab)->pages->__flags & SLAB_FULL)
#define __slab_is_partial(slab)                 \
  ((slab)->pages->__flags & SLAB_FULL)
#define __slab_is_percpu(slab)                  \
  ((slab)->pages->__flags & SLAB_PERCPU)

/* slab locking and unlocking macros */
#define __lock_slab_page(pg)                    \
  spinlock_lock_bit((pg)->__flags, bitnumber(SLAB_LOCK))
#define __unlock_slab_page(pg)                  \
  spinlock_unlock_bit((pg)->__flags, bitnumber(SLAB_LOCK))
#define __slab_lock(slab)                       \
  __lock_slab_page((slab)->pages)
#define __slab_unlock(slab)                     \
  __unlock_slab_page((slab)->pages)

#ifdef DEBUG_SLAB
static int __register_memcache_dbg(memcache_t *cache, const char *name, memcache_type_t type)
{
  switch (type) {
      case MEMCACHE_GENERIC:
        cache->dbg.name = idalloc(strlen(name));        
        break;
      case MEMCACHE_NORMAL:
        /* FIXME: allocate needful size for cache name from slabs */
        cache->dbg.name = NULL;
        break;
      default:
        panic("__register_memcache_dbg: Unknown memory cache type: %d\n", type);
  }
  if (!cache->dbg.name)
    return -ENOMEM;
  
  strcpy(cache->dbg.name, name);
  spinlock_lock_irsafe(&memcaches_lock);
  cache->dbg.type = next_memcache_type++;
  list_add2tail(&memcaches_lsg, &cache->dbg.n);
  spinlock_unlock_irqsafe(&memcaches_lock);

  return 0;
}

static void __prepare_slab_pages_dbg(memcache_t *cache, page_frame_t *pages)
{
  unsigned int slab_page_magic = SLAB_MAGIC_BASE + cache->dbg.type;
  char *startpage, *first_addr = pframe_to_virt(pages);

  for (startpage = first_addr; startpage < (first_addr + (cache->pages_per_slab << PAGE_WIDTH));
       startpage += PAGE_SIZE) {
    *(unsigned int *)startpage = slab_page_magic;
  }
}

static inline void *__getobjrealaddr(slab_t *slab, void *obj)
{
  char *p = obj;

  if (unlikely(p == PAGE_ALIGN(p)))
    p += SLAB_PAGE_OFFS;
  if (unlikely(slab->memcache->object_size < SLAB_OBJDEBUG_MINSIZE))
    return p;
  
  return (p + sizeof(int));
}

static void __slab_setfreeobj_dbg(slab_t *slab, void *obj)
{
  if (likely(slab->memcache->object_size >= SLAB_OBJDEBUG_MINSIZE)) {
    char *p = (char *)obj - sizeof(int);
      
    *(unsigned int *)p = SLAB_OBJLEFT_GUARD;
    p += sizeof(int) + sizeof(char *);
    *(unsigned int *)p = SLAB_OBJRIGHT_GUARD;
  }
}

static bool __slab_page_is_valid_dbg(slab_t *slab, char *page)
{
  unsigned int page_guard = *(unsigned int *)page;

  if ((page_guard - slab->memcache->dbg.type) == SLAB_MAGIC_BASE)
    return true;

  return false;
}

static void __validate_address_dbg(void *addr)
{
  unsigned int page_guard = *(unsigned int *)round_down((uintptr_t)addr, PAGE_SIZE);
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
          " SLAB_MAGIC_BASE = %#x\n",
          " slab's cache type = %#x\n",
          " slab's page guard = %#x",
          slab, cache->dbg.name, virt_to_pframe_id(page),
          SLAB_MAGIC_BASE, cache->dbg.type,
          (*(unsigned int)*pframe_to_virt(slab->pages + i)));
  }
}

static void __validate_slab_object_dbg(slab_t *slab, void *obj)
{
  if (unlikely(slab->memcache->size) < SLAB_OBJDEBUG_MINSIZE)
    return;
  else {
    char *p = (char *)obj - sizeof(int);
    int i = 0;

    if (*(unsigned int *)p != SLAB_OBJLEFT_GUARD) 
      goto inval;

    p += sizeof(char *) + sizeof(int);
    if (*(unsigned int *)p != SLAB_OBJRIGHT_GUARD) {
      i++;
      goto inval;
    }

    return;
    
    inval:
    panic("Invalid slab object %p:\n"
          "%sGUARD was modified!\n"
          "rightguard = %#x, leftguard = %#x\n",
          obj, (i ? "RIGHT", "LEFT"),
          *(unsigned int *)obj, *(unsigned int *)p);
  }
}

#else
#define __register_memcache_dbg(cache, name) (0)
#define __prepare_slab_pages_dbg(cache, pages)
#define __getobjrealaddr(slab, obj) (obj)
#define __slab_setfreeobj_dbg(slab, obj)
#define __slab_page_is_valid_dbg(slab, page) (true)
#define __validate_slab_page_dbg(slab)
#define __validate_slab_object_dbg(slab, obj)
#define __validate_address_dbg(addr)
#endif /* DEBUG_SLAB */

static void __slab_setfreeobj(slab_t *slab, void *obj)
{
  char *p = obj;

  if (unlikely(!slab->objects))
    *(char **)p = SLAB_OBJLIST_END;
  else
    *(char **)p = *(char **)slab->objects;
  
  slab->objects = &p;
  __slab_setfreeobj_dbg(slab, obj);
  slab->nobjects++;  
}

static void *__slab_getfreeobj(slab_t *slab)
{
  char *obj;

  if (!slab->objects) {
    kprintf(KO_ERROR "Attemption to allocate object of size %zd from empty slab", cache->obj_size);
    return NULL;
  }
  
  obj = *slab->objects;
  if (*(char **)obj == SLAB_OBJLIST_END)
    slab->objects = NULL;
  else
    slab->objects = *(char **)obj;

  slab->nobjects--;
  return __getobjrealaddr(slab, obj);
}

static void __init_slab_objects(slab_t *slab, int skip_items)
{
  memcache_t *cache = slab->memcache;
  char *mem = __slab_first_addr(slab) + skip_items * cache->object_size;
  char *obj, *next = NULL;

  slab->objects = NULL;
  for (obj = mem;
       obj < mem + (cache->pages_per_slab << PAGE_WIDTH);
       obj = next) {
    __slab_setfreeobj(slab, __getobjrealaddr(obj));
    next = obj + slab->object_size;
  }

  ASSERT(((cache->pages_per_slab << PAGE_WIDTH) /
          cache->object_size - skip_items) == slab->nobjects);
}

/*
 * alloc slab pages doesn't care about memory cache locking
 * So, before calling alloc_slab_pages, user should lock cache
 * by himself. (if cache locking is necessary)
 */
static page_frame_t *alloc_slab_pages(memcache_t *cache)
{
  page_frame_t *pages;

  pages = pfalloc(cache->pages_per_slab);
  if (pages) {
    memset(pages->__flags, 0, sizeof(pages->__flags));
    __prepare_slab_pages_dbg(cache, pages);
  }
  
  return pages;
}

static inline void free_slab_pages(page_frame_t *pages)
{
  free_pages(pages);
}

static void register_slab(slab_t *slab)
{
  atomic_inc(&slab->memche->nslabs);
  spinlock_lock(&slab->memcache->lock);
  list_add(&slab->memcache->slabs, list_node(&slab->pages->head));
  spinlock_unlock(&slab->memcahce->lock);  
}

static void unregister_slab(slab_t *slab)
{
  atomic_dec(&slab->memcache->nslabs);
  spinlock_lock(&slab->memcache->lock);
  list_del(list_node(&slab->pages->head));
  spinlock_unlock(&slab->memcache->lock);  
}

/*
 * This function doesn't care about memory cache locking,
 * so if locking is really necessary, it should be set manually
 * befure calling this function.
 */
static int prepare_slab(memcache_t *cache, slab_t *slab)
{
  memset(slab, 0, sizeof(*slab));
  slab->memcache = cache;
  slab->pages = alloc_slab_pages(cache->pages_per_slab);
  if (!slab->pages)
    return -ENOMEM;
  
  __init_slab_objects(slab, 0);
  return 0;
}

static slab_t *__create_slabs_slab(void)
{
  page_frame_t *pages = alloc_slab_pages(slabs_memcache.pages_per_slab);
  slab_t *slab = NULL;

  if (!pages)
    goto out;

  slab = pframe_to_virt(pages) + SLAB_PAGE_OFFS;
  slab->memcache = &slabs_memcache;
  slab->pages = pages;
  __init_slab_objects(slab, 1);
  
  return slab;
}

static slab_t *create_new_slab(memcache_t *cache)
{
  slab_t *new_slab = NULL;
  slab_state_t state;

  /*
   * There may be suitable slab in the partial slabs list
   * and in the empty slabs list.
   * Typical stratagy: try to get partial slab first. If there
   * are no any partial slabs, try to get an empty one.
   */
  if (atomic_get(&cache->npartial_slabs)) {
    atomic_dec(&cache->npartial_slabs);
    spinlock_lock(&cache->lock);
    new_slab =
      __page2slab((page_frame_t *)list_entry(list_node_first(&cache->inuse), page_frame_t, node));
    list_del(&new_slab->pages->node);
    spinlock_unlock(&cache->lock);
  }
  else if (atomic_get(&cache->nempty_slabs)) {
    atomic_dec(&cache->nempty_slabs);
    spinlock_lock(&cache->lock);
    new_slab =
      __page2slab((page_frame_t *)list_entry(list_node_last(&cache->inuse), page_frame_t, node));
    list_del(&new_slab->pages->node);
    spin_unlock(&cache->lock);
  }
  if (new_slab)
    goto out;

  /*
   * Unfortunaly, there are no any partial or empty slabs available,
   * so we have to allocate a new one.
   * Note: One memory cache(slabs_memcache) has specific allocation policy
   * different from allocation policy of other slabs...
   */
  if (unlikely(cache == &slabs_cache))
    new_slab = __create_slabs_slab();
  else {
    int ret;
    
    new_slab = alloc_from_memcache(&slabs_memcache);
    ret = prepare_slab(cache, new_slab);
    if (ret) {
      destroy_slab(new_slab);
      new_slab = NULL;
    }
  }

  register_slab(new_slab);
  out:
  return new_slab;
}

static void destroy_slab(slab_t *slab)
{
  page_frame_t *slab_page = slab->pages;

  if (unlikely(slab->memcahce != &slabs_memcache)) {
    slab_t *slabs_slab = __get_slab_by_addr(slab);
    free_slab_object(slabs_slab, slab);
  }

  free_slab_pages(slab_page);
}

static void free_slab_object(slab_t *slab, void *obj)
{
  memcahce_t *cache = slab->memcache;

  __slab_lock(slab);
  __slab_setfreeobj(slab, obj);
  /*
   * If given slab is not percpu guy, it may
   * become partial(if it was free) or empty(if it was partial)
   * after its object is freed. This means slab may be moved to another
   * list in memory cache or siply freed.
   */
  if (!__slab_is_percpu(slab)) {
    int objects_per_slab = cache->pages_per_slab << PAGE_WIDHT) / cache->object_size;
    
    if (__slab_is_full(slab)) {
      /* Slab became full, so it is moved to "partial" list */
      __set_slab_state(slab, SLAB_PARTIAL);
      atomic_inc(&cache->npartial_slabs);
      spinlock_lock(&cache->lock);
      list_add2head(&cache->inuse, &slab->pages->node);
      spinlock_unlock(&cache->lock);
    }
    else if (likely(slab->nobjects == objects_per_slab) ||
             unlikely(slab->objects == (objects_per_slab - 1))) {
      /*
       * Slab became empty, so it either is moved to "empty" list or
       * it is freed(if parent memory cache has too much empty slabs).
       * Also it may occur slab just has became free is a "child" of slabs
       * memory cache that has a little different allocating and freeing
       * policyes than all other memory caches.
       */
      __set_slab_state(slab, SLAB_EMPTY);
      if (unlikely((atomic_get(&cache->nempty_slabs) + 1) >= SLAB_EMPTYSLABS_MAX)) {
        unregister_slab(slab);
        __slab_unlock(slab);
        desctroy_slab(slab);          
        return;
      }

      atomic_inc(&cache->nempty_slabs);
      spinlock_lock(&cache->lock);
      list_add2tail(&cache->inuse, &slab->pages->node);
      spinlock_unlock(&cache->lock);
    }
  }

  __unlock_slab(slab);
}

static void prepare_memcache(memcache_t *cache, size_t object_size, int pages_per_slab)
{
  memset(cache, 0, sizeof(*cache));
  cache->pages_per_slab = pages_per_slab;

  /* cache->slabs list contains all slabs memory cache owns */
  list_init_head(&cache->slabs);
  /*
   * cache->inuse list contains partial and empty slabs
   * memory caceh owns. Note, that partial cache are always at the
   * end of the list and empty cache are allways at the head.
   */
  list_init_head(&cache->inuse);
  atomic_set(&cache->nslabs, 0);
  atomic_set(&cache->nempty_slabs, 0);
  atomic_set(&cache->npartial_slabs, 0);
  spinlock_initialize(&cache->lock); /* for memcache lists protecting */
}

/*
 * There are only two "heart" caches: one for memcache_t structures and another
 * one for slab_t strcutures.
 * __create_hear_cache creates caches slab core system needs very much.
 */
static void __create_heart_cache(memcache_t *cache, size_t size, const char *name)
{
  int ret, i;

  /* we don't need allocate "heart" caches dynamically, they are predefined */
  prepare_memcache(cache, size, GENERIC_SLAB_PAGES);
  ret = __register_memcache_dbg(cache, name);
  if (ret)
    goto err;

  ret = -ENOMEM;
  for_each_cpu(i) {
    slab_t *slab;

    /*
     * For this moment we can not allocate slab_t structure via slab,
     * so we are allocating it using init-data allocator for each CPU.
     */
    slab = idalloc(sizeof(*slab));
    if (!slab)
      goto err;

    ret = prepare_slab(cache, slab, size);
    if (!slab->pages)
      goto err;
    
    cache->cpu_slabs[i] = slab;
    register_slab(slab);
  }

  kprintf(" Slab %s (size=%zd) was successfully initialized\n",
          name, size);
  return;
  err:
  panic("Can't create slab memory cache for \"%s\" elements! (err=%d)", name, ret);
}

static void __create_generic_caches(void)
{
  size_t size = FIRST_GENSLABS_POW2;
  int i = 0, err = 0, c;
  char cache_name[16] = "size ";
  
  generic_caches_num = LAST_GENSLABS_POW2 - FIRST_GENSLABS_POW2 + 1;
  ASSERT(generic_caches_num > 0);
  generic_caches = idalloc(sizeof(*generic_caches) * generic_caches_num);
  if (!generic_caches) {
    err = -ENOMEM;
    goto err;
  }
  while (size <= LAST_GENSLABS_POW2) {
    ASSERT((size >= SLAB_OBJECT_MIN_SIZE) &&
           (size <= SLAB_OBJECT_MAX_SIZE));
    memset(cache_name + 5, 0, 11);
    sprintf(cache_name + 5, "%d", size);
    memcache_prepare(generic_caches + i, size, GENERIC_SLAB_PAGES);
    err = register_memcache(generic_caches + i, cache_name, MEMCACHE_GENERIC);
    if (err)
      goto err;
    for_each_cpu(c) {
      slab_t *slab;

      slab = create_new_slab(generic_caches + i);
    }
    
    size <<= 1;
    i++;
  }

  err:
  panic("Can't greate generic cache for size %zd: (err = %d)", size, err);
}

void slab_allocator_init(void)
{
  kprintf("[MM] Initializing slab allocator\n");
  list_init_head(&slab_caches);
  
  /* create default cache for slab_cache_t structures */
  __create_heart_cache(&caches_cache, sizeof(memcache_t), "memcache_t");
  /* create default cache for slab_t structures */
  __create_heart_cache(&caches_cache, sizeof(slab_t), "slab_t");
  __create_generic_caches();
}

void *alloc_from_memcache(memcache_t *cache, uint8_t flags)
{
  slab_t *slab;
  void *obj = NULL;

  /* firstly try to getper cpu slab */
  slab = __get_percpu_slab(cache);
  
  if (unlikely(!slab->nobjects)) {
    /*
     * Unfortunately our percpu guy is already full,
     * this means it must be replaces(if there is enough memory
     * to allocate it :)).
     */
    __lock_slab(slab); /* Full slab is locked only for disabling preemption */
    atomic_dec(&cache->npartial_slabs);
    slab = create_new_slab(cache);
    if (!slab)
      goto out;
    
    __set_percpu_slab(cache, slab);
    __unlock_slab(slab);
  }

  __lock_slab(slab);
  obj = __slab_getfreeobj(slab);
  __validate_slab_page_dbg(slab, (char *)align_down((uintptr_t)obj, PAGE_SIZE));
  __validate_slab_object_dbg(slab, obj);
  if (unlikely(__slab_is_empty(slab)))
    __set_slab_state(slab, SLAB_PARTIAL);

  __unlock_slab(slab);
  out:
  return obj;
}

void memfree(void *mem)
{
  slab_t *slab;
  
  /* basic validation procedures (if DEBUG_SLAB is enabled...) */
  __validate_address_dbg(mem);
  slab = __get_slab_by_addr(mem);
  __validate_slab_page_dbg(slab, round_down(mem, PAGE_SIZE));
  free_slab_object(slab, mem);
}
