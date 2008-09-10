/*
 * Base implementation of container logic.
 */

#ifndef __CONTAINER_H__
#define __CONTAINER_H__ 

#define offset_of(type, member) \
   ((uintptr_t)&(((type *)0)->member))

/* 
  #define container_of(ptr, type, member) \
   (type *)((uintptr_t)ptr - offset_of(type,member))
*/


#endif

