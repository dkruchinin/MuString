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
  MMO_NTR_COW,
  MMO_NTR_SRV,
  MMO_NTR_PAGECACHE,
  MMO_NTR_PROXY,
  MMO_NTR_STATIC,  
};

enum { /* memory object flags */
  MMO_FLG_SPIRIT    = 0x01,
  MMO_FLG_STICKY    = 0x02,
  MMO_FLG_LEECH     = 0x04,
  MMO_FLG_IMMORTAL  = 0x08,
  MMO_FLG_DPC       = 0x10,
  MMO_FLG_BACKENED  = 0x20,
  MMO_FLG_NOSHARED  = 0x40,
};

#define MMO_LIVE_MASK (MMO_FLG_SPIRIT | MMO_FLG_STICKY | MMO_FLG_LEECH | MMO_FLG_IMMORTAL)

struct __memobj;
struct __vmrange;

typedef struct __memobj_ops {
  int (*handle_page_fault)(struct __vmrange *vmr, pgoff_t addr, uint32_t pfmask);
  int (*populate_pages)(struct __vmrange *vmr, , pgoff_t offset, page_idx_t npages);
  int (*put_page)(struct __memobj *memobj, pgoff_t offset, page_frame_t *page);
  page_frame_t *(*get_page)(struct __memobj *memobj, pgoff_t offset);
} memobj_ops_t;

struct __task_struct;

typedef struct __memobj_backend {
  ulong_t port_id;
  struct __task_struct *server;
} memobj_backend_t;

/* FIXME DK: and what about backend? */
typedef struct __memobj {
  memobj_id_t id;
  memobj_ops_t *mops;
  pgoff_t size;
  list_head_t pagelist;
  list_node_t mmo_node;  
  memobj_backend_t *backend;
  void *private;
  atomic_t users_count;
  memobj_nature_t nature;
  uint32_t flags;
} memobj_t;

#define NUM_RSRV_MEMOBJ_IDS 3
#define GENERIC_MEMOBJ_ID   0
#define COW_MEMOBJ_ID       1
#define SRV_MEMOBJ_ID       2

#define memobj_kernel_nature(ntr) ((ntr) < NUM_RESRV_MEMOBJ_IDS)
#define memobj_kernel_nature2id(ntr) (ntr)

extern memobj_t generic_memobj;

void memobj_subsystem_initialize(void);
int memobj_create(memobj_nature_t mmo_nature, uint32_t flags, pgoff_t size, /* OUT */ memobj_t **out_memobj);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);

/* memobject nature-dependent initialization functions */
int generic_memobj_initialize(memobj_t *memobj, uint32_t flags);

#endif /* __MEMOBJ_H__ */
