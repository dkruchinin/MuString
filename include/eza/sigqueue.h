#ifndef __SIGQUEUE_H__
#define __SIGQUEUE_H__

#include <mlibc/types.h>
#include <ds/list.h>
#include <eza/errno.h>
#include <eza/arch/bitwise.h>
#include <mlibc/assert.h>

typedef uint64_t sigset_t;

#define __SQ_SHIFT  3
#define __SQ_GRANULARITY  (1<<__SQ_SHIFT)
#define __SQ_MAXID  (sizeof(sigset_t)*8)

typedef struct __sigqueue_header {
  list_node_t l;
  ulong_t idx;
} sq_header_t;

typedef struct __sigqueue {
  sigset_t *active_mask;
  list_head_t base[__SQ_MAXID];
} sigqueue_t;

static inline void sigqueue_initialize(sigqueue_t *sq,sigset_t *pmask)
{
  int i;

  sq->active_mask=pmask;
  for(i=0;i<__SQ_MAXID;i++) {
    list_init_head(&sq->base[i]);
  }
}

static inline int sigqueue_add_item(sigqueue_t *sq,sq_header_t *item)
{
  if( item->idx >= __SQ_MAXID ) {
    return -EINVAL;
  }

  list_init_node(&item->l);
  list_add2tail(&sq->base[item->idx],&item->l);

  arch_bit_set(sq->active_mask,item->idx);

  return 0;
}

static inline sq_header_t *sigqueue_remove_item(sigqueue_t *sq,long idx,
                                                 bool remove_all)
{
  sq_header_t *item;
  list_node_t *ln;
  list_head_t *lh;

  if( idx <0 || idx >= __SQ_MAXID ) {
    return NULL;
  }

  lh=&sq->base[idx];
  if( list_is_empty(lh) ) {
    return NULL;
  }

  ln=list_node_first(lh);
  item=container_of(ln,sq_header_t,l);

  if( remove_all ) {
    list_cut_sublist(ln,lh->head.prev);
  } else {
    list_del(ln);
  }

  if( list_is_empty(lh) ) {
    arch_bit_clear(sq->active_mask,idx);
  }

  return item;
}

static inline sq_header_t *sigqueue_remove_first_item(sigqueue_t *sq,
                                                      bool remove_all)
{
  long idx=arch_bit_find_lsf(*sq->active_mask);

  if( idx < 0 || idx >=__SQ_MAXID ) {
    return NULL;
  }
  return sigqueue_remove_item(sq,idx,remove_all);
}

#endif
