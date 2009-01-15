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

static void reserve_memobjs();

void memobj_subsystem_initialize(void)
{
  kprintf("[MM] Initializing memory objects subsystem...\n");
  idx_allocator_init(&__memobjs_ida, CONFIG_MEMOBJS_MAX);
  __memobjs_memcache = create_memcache("Mmemory objects cache", sizeof(memobj_t),
                                       DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__memobjs_memcache)
    panic("memobj_subsystem_initialize: Can't create memory cache for memory objects. ENOMEM.");
}
