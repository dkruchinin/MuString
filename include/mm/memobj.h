#ifndef __MEMOBJ_H__
#define __MEMOBJ_H__

#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mlibc/types.h>

#ifndef CONFIG_MEMOBJS_MAX
#define CONFIG_MEMOBJS_MAX (PAGE_SIZE << 3)
#endif /* CONFIG_MEMOBJS_MAX */

typedef unsigned long memobj_id_t;

typedef enum __memobj_nature {
  MEMOBJ_SHARED     = 0x01,
  MEMOBJ_TYPED      = 0x02,
  MEMOBJ_PERSISTENT = 0x04,
  MEMOBJ_FIXEDADDR  = 0x08,
  MEMOBJ_IMMORTAL   = 0x10,
} memobj_nature_t;

struct __memobj;
/* TODO DK: extends basic memobjs operations... */
typedef struct __memobj_ops {
  status_t (*synchronize)(struct __memobj *memobj);
  status_t (*truncate)(struct __memobj *memobj);
  status_t (*putpage)(struct __memobj *memobj);
  status_t (*getpage)(struct __memobj *memobj);
} memobj_ops_t;


typedef struct __memobj {
  memobj_id_t id;
  ttree_t pages_cache;
  list_head_t pagelist_lru;
  memobj_ops_t mops;
  atomic_t users_count;
  memobj_nature_t nature;
} memobj_t;

void memobj_subsystem_initialize(void);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);

#endif /* __MEMOBJ_H__ */
