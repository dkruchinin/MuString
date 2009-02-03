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
          list_insert_before(&a->ln,_ln);           \
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

#endif
