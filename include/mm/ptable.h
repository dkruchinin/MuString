#ifndef __PTABLE_H__
#define __PTABLE_H__

#include <mm/page.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

typedef struct __root_pagedir {
  page_frame_t *dir;
  mutex_t lock;
  atomic_t refcount;
} root_pagedir_t;

#endif /* __PTABLE_H__ */
