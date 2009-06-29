#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#include <ds/list.h>

#define skiplist_add(ptr,lh,type,ln,plh,cv)       \
  do {                                            \
     if( list_is_empty((lh)) ) {                     \
      list_add2tail((lh),&((type*)(ptr))->ln);    \
    } else {                                      \
      list_head_t *_lh=(list_head_t *)lh;           \
      type *da;                                     \
      bool inserted=false;                          \
      type *a=(type *)ptr;                          \
      list_node_t *_ln;                             \
                                                    \
      list_for_each(_lh,_ln) {                           \
        da=container_of(_ln,type,ln);                    \
        if( da->cv > a->cv ) {                      \
            list_add_before(_ln, &a->ln);           \
          inserted=true;                            \
          break;                                    \
        } else if( da->cv == a->cv ) {              \
          inserted=true;                            \
          list_add2tail(&da->plh,&a->ln);           \
          break;                                        \
        }                                               \
      }                                                 \
      if( !inserted ) {                                 \
        list_add2tail(_lh,&a->ln);                      \
      }                                                 \
    }                                                   \
  } while(0)

#define skiplist_del(ptr,type,lh,ln)              \
  do {                                            \
    list_node_t *prev=((type *)ptr)->ln.prev;     \
    type *at=(type *)ptr;                         \
    list_del(&at->ln);                            \
                                                  \
    if( !list_is_empty(&at->lh) ) {                             \
      type *a=container_of(list_node_first(&at->lh),type,ln);   \
      list_del(&a->ln);                                         \
      if( !list_is_empty(&at->lh) ) {                           \
        list_move2head(&a->lh,&at->lh);                         \
      }                                                         \
      a->ln.prev=prev;                                          \
      a->ln.next=prev->next;                                    \
      prev->next->prev=&a->ln;                                  \
      prev->next=&a->ln;                                        \
    }                                                           \
  } while(0)

#endif
