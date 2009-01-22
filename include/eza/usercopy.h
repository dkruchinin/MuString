#ifndef __USERCOPY_H__
#define __USERCOPY_H__

#include <mm/page.h>
#include <mlibc/types.h>

#define __user

#define USPACE_ADDR(ksym,ubase) (((unsigned long)(ksym) & PAGE_OFFSET_MASK) + (ubase))

int copy_to_user(void *dest,void *src,ulong_t size);
int copy_from_user(void *dest,void *src,ulong_t size);
#define get_user(a,_uptr)  copy_from_user(&(a),_uptr,sizeof((a)))
#define put_user(a,_uptr)  copy_to_user(_uptr,&(a),sizeof((a)))

#endif /* __USERCOPY_H__ */
