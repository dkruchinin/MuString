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
} memobj_nature_t;

enum { /* memory object flags */
  MMO_FLG_SPIRIT     = 0x01,
  MMO_FLG_STICKY     = 0x02,
  MMO_FLG_LEECH      = 0x04,
  MMO_FLG_IMMORTAL   = 0x08,
  MMO_FLG_DPC        = 0x10,
  MMO_FLG_BACKENDED  = 0x20,
  MMO_FLG_NOSHARED   = 0x40,
  MMO_FLG_INACTIVE   = 0x80,
};

#define MMO_LIVE_MASK (MMO_FLG_SPIRIT | MMO_FLG_STICKY | MMO_FLG_LEECH | MMO_FLG_IMMORTAL)

struct __memobj;
struct __vmrange;

typedef struct __memobj_ops {
  int (*handle_page_fault)(struct __vmrange *vmr, uintptr_t addr, uint32_t pfmask);
  int (*populate_pages)(struct __vmrange *vmr, uintptr_t addr, page_idx_t npages);
  int (*put_page)(struct __memobj *memobj, pgoff_t offset, page_frame_t *page);
  int (*get_page)(struct __memobj *memobj, pgoff_t offset, page_frame_t **page);
  void (*cleanup)(struct __memobj *memobj);
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
  list_node_t mmo_node;  
  memobj_backend_t *backend;
  void *private;
  atomic_t users_count;
  memobj_nature_t nature;
  uint32_t flags;
  struct {
    uid_t owner_uid;
    gid_t owner_gid;
    uid_t manager_uid;
    gid_t manager_gid;
    mode_t acc_mode;
  } acc;
} memobj_t;

#define NUM_RSRV_MEMOBJ_IDS 3
#define GENERIC_MEMOBJ_ID   0
#define COW_MEMOBJ_ID       1
#define SRV_MEMOBJ_ID       2

#define memobj_kernel_nature(ntr) ((ntr) < NUM_RSRV_MEMOBJ_IDS)
#define memobj_kernel_nature2id(ntr) (ntr)
#define memobj_is_generic(memobj) ((memobj) == generic_memobj)

extern memobj_t *generic_memobj;

#define memobj_method_call(memobj, method, args...)                     \
  ({ int __ret = -ECANCELED;                                            \
     if (likely(!atomic_bit_test(&(memobj)->flags,                      \
                                 bitnumber(MMO_FLG_INACTIVE)))) {       \
       __ret = (memobj)->mops->method(args);                            \
     }                                                                  \
     __ret; })


void memobj_subsystem_initialize(void);
int memobj_create(memobj_nature_t mmo_nature, uint32_t flags, pgoff_t size, /* OUT */ memobj_t **out_memobj);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);
memobj_t *memobj_pin_by_id(memobj_id_t memobj_id);
memobj_backend_t *memobj_create_backend(void);
void memobj_release_backend(memobj_backend_t *backend);
int memobj_prepare_page_raw(memobj_t *memobj, page_frame_t **page);
int memobj_prepare_page_backended(memobj_t *memobj, page_frame_t **page);
bool __try_destroy_memobj(memobj_t *memobj);

static inline void pin_memobj(memobj_t *memobj)
{
  if (!(memobj->flags & MMO_FLG_IMMORTAL))
    atomic_inc(&memobj->users_count);
}

static inline bool unpin_memobj(memobj_t *memobj)
{
  if (memobj->flags & MMO_FLG_IMMORTAL)
    return false;
  if (unlikely(memobj->flags & MMO_FLG_INACTIVE))
    return true;
  if (atomic_dec_and_test(&memobj->users_count)) {
    memobj->flags |= MMO_FLG_INACTIVE;
    return __try_destroy_memobj(memobj);
  }

  return false;
}

/* memobject nature-dependent initialization functions */
int generic_memobj_initialize(memobj_t *memobj, uint32_t flags);
int pagecache_memobj_initialize(memobj_t *memobj, uint32_t flags);

#endif /* __MEMOBJ_H__ */
