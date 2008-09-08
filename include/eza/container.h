/*
 * Base implementation of container logic.
 */

#ifndef __CONTAINER_H__
#define __CONTAINER_H__ 

#define container_of(ptr, type, member) \
   (type *)((char *)ptr - ((char *)&((type *)(0))->member - (char *)0))

#endif

