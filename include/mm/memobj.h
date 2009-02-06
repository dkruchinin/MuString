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
  MEMOBJ_NCACHE  = 0x01,
  MEMOBJ_LAZY    = 0x02,
  MEMOBJ_STICKY  = 0x04,
  MEMOBJ_GENERIC = 0x08,
} memobj_nature_t;

struct __memobj;
struct __vmrange;

typedef struct __memobj_ops {
  int (*handle_page_fault)(struct __memobj *memobj, struct __vmrange *vmr, off_t offs, uint32_t pfmask);  
} memobj_ops_t;


typedef struct __memobj {
  memobj_id_t id;
  memobj_ops_t mops;
  ttree_t pagemap;
  mutex_t mutex;
  off_t size;
  atomic_t users_count;
  memobj_nature_t nature;  
  void *ctx;
} memobj_t;

#define NULL_MEMOBJ_ID 0
extern memobj_t null_memobj;

void memobj_subsystem_initialize(void);
memobj_t *memobj_find_by_id(memobj_id_t memobj_id);

#endif /* __MEMOBJ_H__ */
