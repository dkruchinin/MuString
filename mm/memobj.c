#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <ds/idx_allocator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

static idx_allocator_t __memobjs_ida;
static memcache_t *__memobjs_memcache = NULL;
static ttree_t __memobjs_tree;

static int __memobjs_cmp_func(void *k1, void *k2)
{
  return (*(long *)k1 - *(long *)k2);
}

void memobj_subsystem_initialize(void)
{
  kprintf("[MM] Initializing memory objects subsystem...\n");
  idx_allocator_init(&__memobjs_ida, CONFIG_MEMOBJS_MAX);
  kprintf("-2\n");
  idx_reserve(&__memobjs_ida, 0);
  kprintf("-1\n");
  __memobjs_memcache = create_memcache("Mmemory objects cache", sizeof(memobj_t),
                                       DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  kprintf("-3\n");
  if (!__memobjs_memcache)
    panic("memobj_subsystem_initialize: Can't create memory cache for memory objects. ENOMEM.");

  kprintf("1\n");
  ttree_init(&__memobjs_tree, __memobjs_cmp_func, memobj_t, id);
  kprintf("2\n");
}

memobj_t *memobj_find_by_id(memobj_id_t memobj_id)
{
  return ttree_lookup(&__memobjs_tree, &memobj_id, NULL);
}
