#include <kernel/vm.h>
#include <mlibc/string.h>

status_t copy_to_user(void *dest,void *src,ulong_t size)
{
  memcpy(dest,src,size);
  return 0;
}

status_t copy_from_user(void *dest,void *src,ulong_t size)
{
  memcpy(dest,src,size);
  return 0;
}
