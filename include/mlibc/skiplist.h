#ifndef __SKIPLIST_H__
#define __SKIPLIST_H__

#include <ds/list.h>

#define skiplist_add(ptr,lh,type,ln,plh,cv)       \
  do {                                            \
    if( list_is_empty((lh)) ) {                   \
      list_add2tail((lh),&((type*)(ptr))->ln);    \
    } else {                                      \
      list_node_t *next=((list_head_t *)lh)->head.next,*prev=NULL;      \
      type *da;                                     \
      bool inserted=false;                          \
      type *a=container_of(ptr,type,ln);            \
                                                    \
      do {                                          \
        da=container_of(next,type,ln);              \
        if( da->cv > a->cv ) {                      \
          break;                                    \
        } else if( da->cv == a->cv ) {              \
          list_add2tail(&da->plh,&a->ln);           \
          inserted=true;                            \
          break;                                    \
        }                                           \
        prev=next;                                  \
        next=next->next;                            \
      } while(next != list_head((list_head_t *)lh));    \
                                                        \
      if( !inserted ) {                                 \
        if( prev != NULL ) {                            \
          a->ln.next=prev->next;                          \
          prev->next->prev=&a->ln;                        \
          prev->next=&a->ln;                              \
          a->ln.prev=prev;                                      \
        } else {                                                \
          list_add2head((list_head_t *)lh,&a->ln);              \
        }                                                       \
      }                                                         \
    }                                                           \
  } while(0)

#endif
