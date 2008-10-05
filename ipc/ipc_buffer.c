#include <ipc/buffer.h>
#include <eza/task.h>
#include <kernel/vm.h>
#include <eza/errno.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <ds/linked_array.h>
#include <eza/limits.h>
#include <eza/vm.h>

static ipc_user_buffer_t *__alloc_buffer(ulong_t size)
{
  ipc_user_buffer_t *buf = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  return buf;
}

static void __free_buffer( ipc_user_buffer_t *buf )
{
  free_pages_addr(buf);
}

static status_t __setup_buffer_pages(task_t *owner,ipc_user_buffer_t *buf,
                                     uintptr_t start_addr, ulong_t size)
{
  page_directory_t *pd = &owner->page_dir;
  page_idx_t idx;
  status_t r = -EFAULT;

  LOCK_TASK_VM(owner);

  while(1) {
    idx = mm_pin_virtual_address(pd,start_addr);
    if( idx == INVALID_PAGE_IDX ) {
      r=-EFAULT;
      goto out;
    }
  }

  r = 0;
out:
  UNLOCK_TASK_VM(owner);
  return r;
}

status_t ipc_create_buffer(task_t *owner,uintptr_t start_addr, ulong_t size)
{
  ipc_user_buffer_t *buf;
  status_t r;
  ulong_t id;

  if( !owner->ipc ) {
    return -EINVAL;
  }

  if( !valid_user_address_range(start_addr,size) ) {
    return -EFAULT;
  }

  LOCK_IPC(owner->ipc);

  if( owner->ipc->num_buffers >=
      owner->limits->limits[LIMIT_IPC_MAX_USER_BUFFERS]) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* First buffer created ? */
  if( !linked_array_is_initialized(&owner->ipc->buffers_array) ) {
    r = linked_array_initialize(&owner->ipc->buffers_array,
                                owner->limits->limits[LIMIT_IPC_MAX_USER_BUFFERS]);
    if( r ) {
      r = -ENOMEM;
      goto out_unlock;
    }
  }

  r = -ENOMEM;
  /* First buffer created ? */
  if( owner->ipc->user_buffers ) {
    /* TODO: [mt] Allocate buffers via slabs ! */
    owner->ipc->user_buffers = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
    if( !owner->ipc->user_buffers ) {
      goto out_unlock;
    }
  }

  id = linked_array_alloc_item(&owner->ipc->buffers_array);
  if( id==INVALID_ITEM_IDX ) {
    goto out_unlock;
  }

  buf = __alloc_buffer(size);
  if( !buf ) {
    r = -ENOMEM;
    goto out_put_id;
  }

  /* OK, metadata is ready for this buffer. So pin all pages
   * for this buffer.
   */
  r = __setup_buffer_pages(owner,buf,start_addr,size);
  if( r ) {
    goto out_free_buffer;
  }

  /* Now install the buffer. */
  IPC_LOCK_BUFFERS(owner->ipc);
  owner->ipc->user_buffers[id] = buf;
  owner->ipc->num_buffers++;
  IPC_UNLOCK_BUFFERS(owner->ipc);

  UNLOCK_IPC(owner->ipc);
  return id;
out_free_buffer:
  __free_buffer(buf);
out_put_id:
  linked_array_free_item(&owner->ipc->buffers_array,id);
out_unlock:
  UNLOCK_IPC(owner->ipc);
  return r;
}

status_t ipc_destroy_buffer(task_t *owner,ulong_t id)
{
  ipc_user_buffer_t *buf;  
  status_t r;

  LOCK_IPC(owner->ipc);

  if( id >= owner->limits->limits[LIMIT_IPC_MAX_USER_BUFFERS] ) {
    r = -EINVAL;
    goto out_unlock;
  }

  IPC_LOCK_BUFFERS(owner->ipc);
  buf = owner->ipc->user_buffers[id];
  owner->ipc->user_buffers[id] = NULL;
  IPC_UNLOCK_BUFFERS(owner->ipc);

  UNLOCK_IPC(owner->ipc);

  if( buf != NULL ) {
    __free_buffer(buf);
    r = 0;
  } else {
    r = -EINVAL;
  }

  return r;
out_unlock:
  UNLOCK_IPC(owner->ipc);
  return r;
}
