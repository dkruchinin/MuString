#ifndef __MEMOBJ_H__
#define __MEMOBJ_H__

#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <mm/page.h>
#include <eza/task.h>
#include <mlibc/types.h>

#ifndef CONFIG_MEMOBJS_MAX
#define CONFIG_MEMOBJS_MAX (PAGE_SIZE << 3)
#endif /* CONFIG_MEMOBJS_MAX */

typedef unsigned long memobj_id;

typedef enum __memobj_nature {
  MEMOBJ_SHARED     = 0x01,
  MEMOBJ_TYPED      = 0x02,
  MEMOBJ_PERSISTENT = 0x04,
} memobj_nature_t;

struct __memobj_ops;

typedef struct __memobj {
  memobj_id id;
  ttree_t pages_cache;
  list_head_t pagelist_lru;
  struct __memobj_ops mops;
  task_t *backend;
  atomic_t users_count;
  memobj_nature_t nature;
} memobj_t;

/* TODO DK: extends basic memobjs operations... */
typedef struct __memobj_ops {
  status_t (*synchronize)(memobj_t *memobj);
  status_t (*truncate)(memobj_t *memobj);
  status_t (*putpage)(memobj_t *memobj);
  status_t (*getpage)(memobj_t *memobj);
} memobj_ops_t;

#endif /* __MEMOBJ_H__ */
