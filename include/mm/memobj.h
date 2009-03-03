#ifndef __MEMOBJ_H__
#define __MEMOBJ_H__

#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mlibc/types.h>

#ifndef CONFIG_MEMOBJS_MAX
#define CONFIG_MEMOBJS_MAX (PAGE_SIZE << 4)
#endif /* CONFIG_MEMOBJS_MAX */

typedef unsigned long memobj_id_t;

typedef enum __memobj_nature {
  MMO_NTR_GENERIC = 1,
  MMO_NTR_PAGECACHE,
  MMO_NTR_SRV, /* TODO DK: implement this fucking stuff... */
};

enum { /* memory object flags */
  MMO_FLG_SPIRIT    = 0x01,
  MMO_FLG_STICKY    = 0x02,
  MMO_FLG_LEECH     = 0x04,
  MMO_FLG_DPC       = 0x08,
  MMO_FLG_BACKENED  = 0x10,
  MMO_FLG_NOSHARED  = 0x20,
};

#define MMO_LIVE_MASK (MMO_FLG_SPIRIT | MMO_FLG_STICKY | MMO_FLG_LEECH)

struct __memobj;
struct __vmrange;

typedef struct __memobj_ops {
  int (*handle_page_fault)(struct __vmrange *vmr, pgoff_t addr, uint32_t pfmask);
  int (*populate_pages)(struct __vmrange *vmr, , pgoff_t offset, page_idx_t npages);
  int (*put_page)(struct __memobj *memobj, pgoff_t offset, page_frame_t *page);
  page_frame_t *(*get_page)(struct __memobj *memobj, pgoff_t offset);
} memobj_ops_t;

/* FIXME DK: and what about backend? */
typedef struct __memobj {
  memobj_id_t id;
  memobj_ops_t mops;
  pgoff_t size;
  list_node_t mmo_node;
  atomic_t users_count;
  memobj_nature_t nature;
  uint32_t flags;
  void *private;
} memobj_t;

#define GENERIC_MEMOBJ_ID 0
extern memobj_t generic_memobj;

void memobj_subsystem_initialize(void);
int memobj_create(memobj_nature_t mmo_nature, uint32_t flags, pgoff_t size, /* OUT */ memobj_t **out_memobj);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);

/* memobject nature-dependent initialization functions */
int generic_memobj_initialize(memobj_t *memobj, uint32_t flags);

#endif /* __MEMOBJ_H__ */
