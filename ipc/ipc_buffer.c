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
#include <ipc/gen_port.h>


/*
  page_frame_t *pd = owner->page_dir;
  page_idx_t idx;
  ulong_t chunk_num;
  status_t r=-EFAULT;
  uintptr_t adr;
  ulong_t first,ssize;
  ipc_user_buffer_chunk_t *pchunk;

  buf->length=0;
  buf->num_chunks=0;
  ssize=size;

  LOCK_TASK_VM(owner);

  idx = mm_pin_virt_addr(pd, start_addr);
  if (idx < 0)
    goto out;

  pchunk = buf->chunks;

  adr=(start_addr+PAGE_SIZE) & PAGE_ADDR_MASK;
  first=adr-start_addr;
  if( size <= first ) {
    first = size;
  }

  buf->first=first;
  pchunk->kaddr=pframe_id_to_virt(idx)+(start_addr & ~PAGE_ADDR_MASK);

  size-=first;
  start_addr+=first;
  chunk_num=1;

  while( size ) {
    idx = mm_pin_virt_addr(pd, start_addr);
    if(idx < 0) {
      goto out;
    }
    pchunk++;
    chunk_num++;

    if(size<PAGE_SIZE) {
      size=0;
    } else {
      size-=PAGE_SIZE;
    }

    pchunk->kaddr = pframe_id_to_virt(idx);
    start_addr+=PAGE_SIZE;
  }

  buf->num_chunks=chunk_num;
  buf->length=ssize;
  r = 0;
out:
  UNLOCK_TASK_VM(owner);
  return r;
*/

status_t ipc_setup_buffer_pages(task_t *owner,iovec_t *iovecs,ulong_t numvecs,
                                uintptr_t *addr_array,ipc_user_buffer_t *bufs)
{
  page_frame_t *pd = owner->page_dir;
  page_idx_t idx;
  ulong_t chunk_num;
  status_t r=-EFAULT;
  uintptr_t adr,*pchunk;

  LOCK_TASK_VM(owner);

  for(;numvecs;numvecs--,iovecs++,bufs++) {
    ipc_user_buffer_t *buf=bufs;
    uintptr_t start_addr=(uintptr_t)iovecs->iov_base;
    ulong_t first,size=iovecs->iov_len;

    buf->chunks=addr_array;

    /* Process the first chunk. */
    idx=mm_pin_virt_addr(pd,start_addr);
    if( idx < 0 ) {
      goto out;
    }

    pchunk=buf->chunks;

    adr=(start_addr+PAGE_SIZE) & PAGE_ADDR_MASK;
    first=adr-start_addr;
    if( size <= first ) {
      first = size;
    }

    buf->first=first;
    *pchunk=(uintptr_t)pframe_id_to_virt(idx)+(start_addr & ~PAGE_ADDR_MASK);

    size-=first;
    start_addr+=first;
    chunk_num=1;

    /* Process the rest of chunks. */
    while( size ) {
      idx=mm_pin_virt_addr(pd, start_addr);
      if(idx < 0) {
        goto out;
      }
      pchunk++;
      chunk_num++;

      if(size<PAGE_SIZE) {
        size=0;
      } else {
        size-=PAGE_SIZE;
      }

      *pchunk=(uintptr_t)pframe_id_to_virt(idx);
      start_addr+=PAGE_SIZE;
    }

    buf->num_chunks=chunk_num;
    buf->length=iovecs->iov_len;
    addr_array+=chunk_num;
  }
  r = 0;
out:
  UNLOCK_TASK_VM(owner);
  return r;
}

status_t ipc_transfer_buffer_data_iov(ipc_user_buffer_t *bufs,ulong_t numbufs,
                                      struct __iovec *iovecs,ulong_t numvecs,
                                      bool to_buffer)
{
  int i,buflen,iovlen,data_size;
  status_t r=0;

  for(buflen=0,i=0;i<numbufs;i++) {
    buflen += bufs[i].length;
  }

  for(iovlen=0,i=0;i<numvecs;i++) {
    iovlen += iovecs[i].iov_len;
  }

  data_size=MIN(buflen,iovlen);

  for(;data_size;) {
    ulong_t *chunk=bufs->chunks;
    ulong_t to_copy,iov_size,bufsize;
    char *dest_kaddr=(char *)*chunk;
    char *user_addr=iovecs->iov_base;
    bool repeat_first;

    /* First, process the first not page-aligned chunk. */
    iov_size=iovecs->iov_len;
    bufsize=bufs->first;

    do {
      repeat_first=false;

      if( bufs->first ) {
        to_copy=MIN(bufsize,iov_size);
        if( to_buffer ) {
          r=copy_from_user(dest_kaddr,user_addr,to_copy);
        } else {
          r=copy_to_user(user_addr,dest_kaddr,to_copy);
        }

        if( r ) {
          break;
        }

        data_size-= to_copy;
        iov_size -= to_copy;
        bufsize -= to_copy;

        if( data_size ) {
          if( iov_size ) {
            user_addr += to_copy;
          } else {
            user_addr = ++iovecs->iov_base;
            iov_size = iovecs->iov_len;
            repeat_first=true;
          }

          if( bufsize ) {
            dest_kaddr += to_copy;
          } else {
            if( bufs->first == bufs->length ) {
              /* This buffer is over, so process the next one from the beginning. */
              bufs++;
              repeat_first=true;
              bufsize=bufs->first;
              chunk=bufs->chunks;
              dest_kaddr=(char *)*chunk;
            }
          }
        }
      }
    } while(repeat_first);

    /* Handle the rest of the buffer. */
    if( data_size ) {
      ulong_t delta;

      bufsize=MIN(data_size,bufs->length-bufs->first);
crunch_buffer:
      delta=MIN(bufsize,iov_size);

      bufsize-=delta;
      iov_size-=delta;
      data_size-=delta;

      while( delta ) {
        chunk++;
        to_copy=MIN(delta,PAGE_SIZE);
        dest_kaddr=(char *)*chunk;

        if( to_buffer ) {
          r=copy_from_user(dest_kaddr,user_addr,to_copy);
        } else {
          r=copy_to_user(user_addr,dest_kaddr,to_copy);
        }

        delta-=to_copy;
        user_addr+=to_copy;
      }

      if( bufsize && data_size ) {
        if( !iov_size ) {
          user_addr = ++iovecs->iov_base;
          iov_size = iovecs->iov_len;
        }
        goto crunch_buffer;
      }
    }
  }
  return r ? -EFAULT : 0;
}

status_t ipc_transfer_buffer_data(ipc_user_buffer_t *bufs,ulong_t numbufs,
                                  void *user_addr,ulong_t to_copy,bool to_buffer)
{
  char *dest_kaddr;
  status_t r;

  for(;numbufs && to_copy;bufs++,numbufs--) {
    ulong_t *chunk=bufs->chunks;
    ulong_t copy;
    dest_kaddr=(char *)*chunk;

    /* First, process the first not page-aligned chunk. */
    if( bufs->first ) {
      copy=MIN(to_copy,bufs->first);
      if( to_buffer ) {
        r=copy_from_user(dest_kaddr,user_addr,copy);
      } else {
        r=copy_to_user(user_addr,dest_kaddr,copy);
      }
      to_copy-=copy;
      user_addr += copy;
    }

    /* Process the rest of current buffer. */
    if( to_copy ) {
      copy=MIN(to_copy,bufs->length-bufs->first);
      to_copy-=copy;

      while( copy ) {
        ulong_t delta=MIN(PAGE_SIZE,copy);
        chunk++;
        dest_kaddr=(char *)*chunk;  

        if( to_buffer ) {
          r=copy_from_user(dest_kaddr,user_addr,delta);
        } else { 
          r=copy_to_user(user_addr,dest_kaddr,delta);
        }
        if( r ) {
          goto out;
        }
        copy-=delta;
        user_addr += delta;
      }
    }
  }

  r = 0;
out:
  return r;
}
