#ifndef __MEMOBJ_H__
#define __MEMOBJ_H__

#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <mm/page.h>
#include <eza/mutex.h>
#include <mlibc/types.h>

#ifndef CONFIG_MEMOBJS_MAX
#define CONFIG_MEMOBJS_MAX (PAGE_SIZE << 3)
#endif /* CONFIG_MEMOBJS_MAX */

typedef unsigned long memobj_id_t;

typedef enum __memobj_nature {
  MEMOBJ_STICKY   = 0x01,
  MEMOBJ_LEECH    = 0x02,
  MEMOBJ_IMMORTAL = 0x04,
  MEMOBJ_CDP      = 0x08,
  MEMOBJ_SHARED   = 0x10,
} memobj_nature_t;

#define MEMOBJ_BEHAVIOUR_MASK (MEMOBJ_LEECH | MEMOBJ_STICKY | MEMOBJ_IMMORTAL)

struct __memobj;
struct __vmrange;

typedef struct __memobj_ops {
  int (*handle_page_fault)(struct __vmrange *vmr, uintptr_t addr, uint32_t pfmask);
  int (*populate_pages)(struct __vmrange *vmr, uintptr_t addr, page_idx_t npages, off_t offs_pages);
  int (*put_page)(struct __memobj *memobj, pgoff_t offs, page_frame_t *page);
  page_frame_t *(*get_page)(struct __memobj *memobj, pgoff_t offs);
} memobj_ops_t;


typedef struct __memobj {
  memobj_id_t id;
  memobj_ops_t mops;
  hat_t pagecache;
  list_head_t *dirty_pages;
  pgoff_t size;
  atomic_t users_count;
  memobj_nature_t nature;  
  void *ctx;
} memobj_t;

#define NULL_MEMOBJ_ID 0
extern memobj_t null_memobj;

void memobj_subsystem_initialize(void);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);

#endif /* __MEMOBJ_H__ */
