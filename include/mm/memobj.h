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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/mm/memobj.h - Memory objects API.
 *
 */

#ifndef __MEMOBJ_H__
#define __MEMOBJ_H__

#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/memobjctl.h>
#include <mlibc/types.h>
#include <eza/spinlock.h>

#ifndef CONFIG_MEMOBJS_MAX
#define CONFIG_MEMOBJS_MAX (PAGE_SIZE << 4)
#endif /* CONFIG_MEMOBJS_MAX */

typedef enum __memobj_flags { /* memory object flags */
  MMO_FLG_SPIRIT     = 0x01,
  MMO_FLG_STICKY     = 0x02,
  MMO_FLG_LEECH      = 0x04,
  MMO_FLG_IMMORTAL   = 0x08,
  MMO_FLG_DPC        = 0x10,
  MMO_FLG_BACKENDED  = 0x20,
  MMO_FLG_NOSHARED   = 0x40,
  MMO_FLG_INACTIVE   = 0x80,
} memobj_flags_t;

#define MMO_LIFE_MASK   (MMO_FLG_SPIRIT | MMO_FLG_STICKY | MMO_FLG_LEECH | MMO_FLG_IMMORTAL)
#define MMO_FLAGS_MASK  (MMO_FLG_DPC | MMO_FLG_NOSHARED | MMO_FLG_BACKENDED)
#define MMO_FLAGS_SHIFT 4

struct __memobj;
struct __vmrange;
struct __ipc_channel;

typedef struct __memobj_backend {
  spinlock_t lock;
  struct __ipc_channel *channel;
} memobj_backend_t;

typedef struct __memobj_ops {
  int (*handle_page_fault)(struct __vmrange *vmr, uintptr_t addr, uint32_t pfmask);
  int (*populate_pages)(struct __vmrange *vmr, uintptr_t addr, page_idx_t npages);
  int (*truncate)(struct __memobj *memobj, pgoff_t new_offset);
  int (*prepare_page_cow)(struct __vmrange *vmr, page_idx_t pidx, uintptr_t addr);
  void (*cleanup)(struct __memobj *memobj);
} memobj_ops_t;

struct __task_struct;

/* FIXME DK: and what about backend? */
typedef struct __memobj {
  memobj_id_t id;
  memobj_ops_t *mops;
  pgoff_t size;
  list_node_t mmo_node;
  memobj_backend_t *backend;
  spinlock_t members_lock;
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

#define NUM_RSRV_MEMOBJ_IDS 2
#define GENERIC_MEMOBJ_ID   0
#define COW_MEMOBJ_ID       1
#define SRV_MEMOBJ_ID       2

#define memobj_kernel_nature(ntr) ((ntr) < NUM_RSRV_MEMOBJ_IDS)
#define memobj_kernel_nature2id(ntr) ((ntr) - 1)
#define memobj_is_generic(memobj) ((memobj) == generic_memobj)

extern memobj_t *generic_memobj;

#define memobj_method_call(memobj, method, args...)                     \
  ({ int __ret = -ECANCELED;                                            \
     if (likely(!atomic_bit_test(&(memobj)->flags,                      \
                                 bitnumber(MMO_FLG_INACTIVE)))) {       \
       if (liekely((memobj)->mops->method != NULL)) {                   \
         __ret = (memobj)->mops->method(args);                          \
       }                                                                \
       else {                                                           \
         __ret = -ENOTSUP;                                              \
       }                                                                \
     }                                                                  \
     __ret; })


void memobj_subsystem_initialize(void);
int memobj_create(memobj_nature_t mmo_nature, uint32_t flags, pgoff_t size, /* OUT */ memobj_t **out_memobj);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);
memobj_t *memobj_pin_by_id(memobj_id_t memobj_id);
int memobj_create_backend(memobj_t *memobj,  struct __task_struct *server_task, ulong_t port_id);
void memobj_release_backend(struct __ipc_channel *backend);
int memobj_prepare_page_raw(memobj_t *memobj, page_frame_t **page);
int memobj_prepare_page_backended(memobj_t *memobj, pgoff_t offset, page_frame_t **page);
bool __try_destroy_memobj(memobj_t *memobj);
int sys_memobj_create(struct memobj_info *user_mmo_info);

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
