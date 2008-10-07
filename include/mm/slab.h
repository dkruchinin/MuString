#ifndef __SLAB_H__
#define __SLAB_H__

#include <config.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mm/pages.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

#define DEBUG_SLAB

#define SLAB_MEMCACHE_GUARD  (0xF00ABBAL)
#define SLAB_EMPTYSLABS_MAX  (NR_CPUS)
#define SLAB_OBJLIST_END     ((char *)0xF00BAAF)
#define GENERIC_SLAB_PAGES   2
#define SLAB_DEFAULT_PAGES   1
#define SLAB_OBJECT_MIN_SIZE 8
#define SLAB_OBJECT_MAX_SIZE (PAGE_SIZE >> 3)
#define FIRST_DEFSLABS_POW2  3

#ifdef DEBUG_SLAB
#define SLAB_OBJDEBUG_MINSIZE ((sizeof(int) << 1) + sizeof(char *))
#define SLAB_OBJLEFT_GUARD    (0xABCD)
#define SLAB_OBJRIGHT_GUARD   (0xDCBA)
#define SLAB_PAGE_OFFS        (sizeof(unsigned int))
#define SLAB_MAGIC_BASE       (0xCAFDAF0)
#define SLAB_FIRST_TYPE       (1)

struct memcache_debug_info {
  char *name;
  unsigned int type;
  list_node_t n;
};
#else
#define SLAB_PAGE_OFFS (0)
#endif /* DEBUG_SLAB */


typedef uint16_t slab_state_t;

#define SLAB_EMPTY       0x01
#define SLAB_PARTIAL     0x02
#define SLAB_FULL        0x04
#define SLAB_PERCPU      0x08
#define SLAB_LOCK        0x10
#define SLAB_STATES_MASK 0x0F

typedef struct __memchace memcache_t;

typedef struct __slab {
  void **objects;
  page_frame_t *pages;
  int nobjects;
} slab_t;

struct __memcahce {
  list_head_t slabs;
  list_head_t inuse;
  atomic_t nslabs;
  atomic_t nempty_slabs;
  atomic_t npartial_slabs;  
  slab_t *cpu_slabs[NR_CPUS];
  size_t object_size;
  spinlock_t lock;
  int pages_per_slab;
#ifdef DEBUG_SLAB
  struct memcache_debug_info dbg;
#endif /* DEBUG_SLAB */
};

extern list_head_t slab_caches;

void slab_allocator_init(void);
void *alloc_from_memcache(memcache_t *cache);
void memfree(void *mem);

#endif /* __SLAB_H__ */
