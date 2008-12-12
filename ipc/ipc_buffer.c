#include <ipc/buffer.h>
#include <eza/task.h>
#include <kernel/vm.h>
#include <eza/errno.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <ds/linked_array.h>
#include <eza/limits.h>
#include <eza/vm.h>
#include <eza/arch/page.h>
#include <mm/page.h>
#include <kernel/vm.h>
#include <mlibc/stddef.h>
#include <eza/arch/mm.h>

#define REF_BUFFER(b) atomic_inc(&b->use_count)
#define UNREF_BUFFER(b) atomic_dec(&b->use_count)

static ipc_user_buffer_t *__alloc_buffer(uintptr_t start_addr,ulong_t size)
{
  ipc_user_buffer_t *buf;
  uintptr_t adr;
  ulong_t t,chunks;

  adr=(start_addr+PAGE_SIZE) & PAGE_ADDR_MASK;
  t=adr-start_addr;
  if( size <= t ) {
    t = size;
  }
  size-=t;
  chunks=1;
  t=PAGE_SIZE;

  /* Determine the number of chunks for this buffer. */
  while(size>0) {
    chunks++;

    if(size<PAGE_SIZE) {
      t=size;
    }
    size-=t;
  }

  /* TODO: [mt] Allocate new buffers via slabs. */
  buf = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  buf->chunks=alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( buf != NULL ) {
    atomic_set(&buf->use_count,1);
  }
  return buf;
}

static void __free_buffer( ipc_user_buffer_t *buf )
{
  free_pages_addr(buf);
}

static void ipc_put_buffer( ipc_user_buffer_t *buf )
{
  /* TODO: [mt] Free buffer memory upon deletion. */
  UNREF_BUFFER(buf);
}

status_t ipc_setup_buffer_pages(task_t *owner,ipc_user_buffer_t *buf,
                                uintptr_t start_addr, ulong_t size)
{
  rpd_t *rpd = &owner->rpd;
  page_idx_t pfn;
  ulong_t chunk_num;
  status_t r=-EFAULT;
  uintptr_t adr;
  ulong_t first,ssize;
  ipc_user_buffer_chunk_t *pchunk;

  buf->length=0;
  buf->num_chunks=0;
  ssize=size;

  LOCK_TASK_VM(owner);

  /* Process the first chunk. */
  pfn = mm_vaddr2page_idx(rpd, start_addr);
  if (pfn == PAGE_IDX_INVAL)
    goto out;

  pchunk = buf->chunks;

  adr=(start_addr+PAGE_SIZE) & PAGE_ADDR_MASK;
  first=adr-start_addr;
  if( size <= first ) {
    first = size;
  }

  buf->first=first;
  pchunk->kaddr=pframe_id_to_virt(pfn)+(start_addr & ~PAGE_ADDR_MASK);

  size-=first;
  start_addr+=first;
  chunk_num=1;

  /* Process the rest of chunks. */
  while( size ) {
    pfn = mm_vaddr2page_idx(rpd, start_addr);
    if(pfn == PAGE_IDX_INVAL) {
      goto out;
    }
    pchunk++;
    chunk_num++;

    if(size<PAGE_SIZE) {
      size=0;
    } else {
      size-=PAGE_SIZE;
    }

    pchunk->kaddr = pframe_id_to_virt(pfn);
    start_addr+=PAGE_SIZE;
  }

  buf->num_chunks=chunk_num;
  buf->length=ssize;
  r = 0;
out:
  UNLOCK_TASK_VM(owner);
  return r;
}

ipc_user_buffer_t *ipc_get_buffer(task_t *owner,ulong_t buf_id)
{
  ipc_user_buffer_t *buf;
  task_ipc_t *ipc = owner->ipc;

  if( !ipc ) {
    return NULL;
  }

  IPC_LOCK_BUFFERS(ipc);
  if( buf_id < owner->limits->limits[LIMIT_IPC_MAX_USER_BUFFERS] ) {
    buf = ipc->user_buffers[buf_id];
    if( buf != NULL ) {
      REF_BUFFER(buf);
    }
  } else {
    buf = NULL;
  }
  IPC_UNLOCK_BUFFERS(ipc);

  return buf;
}

status_t ipc_transfer_buffer_data(ipc_user_buffer_t *buf,ulong_t buf_offset,
                                  ulong_t to_copy, void *user_addr,
                                  bool to_buffer)
{
  char *dest_kaddr;
  ipc_user_buffer_chunk_t *chunk;
  status_t r;
  ulong_t delta,skip,offset;

  if( !to_copy ) {
    return -EINVAL;
  }

//  if( !valid_user_address_range((uintptr_t)user_addr,to_copy) ) {
//    return -EFAULT;
//  }

  if( (buf->length-buf_offset) < to_copy ) {
    r=-EINVAL;
    goto out;
  }

  chunk=buf->chunks;
  r=-EFAULT;

  if( buf_offset < buf->first ) { /* Start filling from the first chunk. */
    dest_kaddr=chunk->kaddr+buf_offset;
    delta=MIN(buf->first-buf_offset,to_copy);

    if( to_buffer ) {
      r=copy_from_user(dest_kaddr,user_addr,delta);
    } else {
      r=copy_to_user(user_addr,dest_kaddr,delta);
    }
    if( r ) {
      goto out;
    }
  } else { /* Skip some medium pages. */
    delta=buf_offset-buf->first; /* Skip the first chunk. */
    skip=(delta >> PAGE_WIDTH)+1;
    offset=delta & PAGE_OFFSET_MASK;

    /* Since 'chunk' points to the first chunk, we should skip it. */
    chunk+=skip;
    dest_kaddr=chunk->kaddr+offset;
    delta=MIN(PAGE_SIZE-offset,to_copy);

    if( to_buffer ) {
      r=copy_from_user(dest_kaddr,user_addr,delta);
    } else {
      r=copy_to_user(user_addr,dest_kaddr,delta);
    }
    if( r ) {
      goto out;
    }
  }
  to_copy-=delta;
  user_addr+=delta;

  /* Fill the rest of the buffer. */
  while( to_copy > 0 ) {
    delta=MIN(PAGE_SIZE,to_copy);
    chunk++;

    dest_kaddr=chunk->kaddr;
    if( to_buffer ) {
      r=copy_from_user(dest_kaddr,user_addr,delta);
    } else { 
      r=copy_to_user(user_addr,dest_kaddr,delta);
    }
    if( r ) {
      goto out;
    }
    to_copy-=delta;
    user_addr+=delta;
  }

  r = 0;
out:
  ipc_put_buffer(buf);
  return r;
}

status_t ipc_create_buffer(task_t *owner,uintptr_t start_addr, ulong_t size)
{
  ipc_user_buffer_t *buf;
  status_t r;
  ulong_t id;

  if( !owner->ipc || !size ) {
    return -EINVAL;
  }

/*
  if( !valid_user_address_range(start_addr,size) ) {
    return -EFAULT;
  }
*/

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
  if( owner->ipc->user_buffers == NULL ) {
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

  buf = __alloc_buffer(start_addr,size);
  if( !buf ) {
    r = -ENOMEM;
    goto out_put_id;
  }

  /* OK, metadata is ready for this buffer. So pin all pages
   * for this buffer.
   */
  r = ipc_setup_buffer_pages(owner,buf,start_addr,size);
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
    ipc_put_buffer(buf);
    r = 0;
  } else {
    r = -EINVAL;
  }

  return r;
out_unlock:
  UNLOCK_IPC(owner->ipc);
  return r;
}
