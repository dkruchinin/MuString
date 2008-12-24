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

  kprintf( "> " );

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

    kprintf( " %p ", *pchunk );

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
  kprintf( "\n" );
  r = 0;
out:
  UNLOCK_TASK_VM(owner);
  return r;
}

static bool verbose=false;

status_t ipc_transfer_buffer_data_iov(ipc_user_buffer_t *bufs,ulong_t numbufs,
                                      struct __iovec *iovecs,ulong_t numvecs,
                                      bool to_buffer)
{
  int i,buflen,iovlen,data_size;
  status_t r=0;
  char *page_end;

  for(buflen=0,i=0;i<numbufs;i++) {
    buflen += bufs[i].length;
  }

  for(iovlen=0,i=0;i<numvecs;i++) {
    iovlen += iovecs[i].iov_len;

    if( verbose ) {
      kprintf( " %d ",iovecs[i].iov_len );
    }
  }

  data_size=MIN(buflen,iovlen);

  kprintf("+++ NUMBUFS: %d, NUMVECS: %d, DATA: %d\n",
          numbufs,numvecs,data_size);
  kprintf("+++ BUFLEN: %d, IOVLEN: %d\n",buflen,iovlen);

  if( verbose ) {
    kprintf( "\n### Data size: %d\n",data_size );
  }

  for(;data_size;) {
    ulong_t *chunk;
    ulong_t to_copy,iov_size,bufsize;
    char *dest_kaddr;
    char *user_addr=iovecs->iov_base;

    /* First, process the first not page-aligned chunk. */
    iov_size=iovecs->iov_len;
new_buffer:
    chunk=bufs->chunks;    
    bufsize=bufs->first;
    dest_kaddr=(char *)*chunk;

    kprintf( ">>>>> bufs->first=%d,bufs->length=%d, first buf addr: %p\n",
             bufs->first,bufs->length,dest_kaddr);
    while( bufsize ) {
      to_copy=MIN(bufsize,iov_size);
      to_copy=MIN(to_copy,data_size);

      if( to_buffer ) {
        r=copy_from_user(dest_kaddr,user_addr,to_copy);
      } else {
        r=copy_to_user(user_addr,dest_kaddr,to_copy);
      }

      data_size-= to_copy;
      iov_size -= to_copy;
      bufsize -= to_copy;

      dest_kaddr += to_copy;
      user_addr += to_copy;

      if( data_size ) {
        if( !iov_size ) {
          iovecs++;
          user_addr = iovecs->iov_base;
          iov_size = iovecs->iov_len;

          kprintf( "# NEXT IOV (of %d) has length %d. Data left: %d\n",
                   numvecs,iov_size,data_size );
        }

        if( !bufsize ) {
          if( bufs->first == bufs->length ) {
            /* This buffer is over, so process the next one from the beginning. */
            bufs++;
            kprintf( "# NEXT BUF (of %d).\n",numbufs );
            goto new_buffer;
          }
        }
      }
    }

/*
    do {
      repeat_first=false;

      kprintf( "***** DATA SIZE: %d, bufs->first = %d, bufs->length = %d\n",
               data_size,bufs->first,bufs->length );
      if( bufs->first ) {
        to_copy=MIN(bufsize,iov_size);
        to_copy=MIN(to_copy,data_size);
        kprintf( "*****  to_copy: %d, iov_size: %d\n",to_copy,iov_size );
        if( to_buffer ) {
          r=copy_from_user(dest_kaddr,user_addr,to_copy);
        } else {
          r=copy_to_user(user_addr,dest_kaddr,to_copy);
        }

        if( r ) {
          kprintf( "FFFFFFFFFFFFFFFFFFAULT !\n" );
          break;
        }

        data_size-= to_copy;
        iov_size -= to_copy;
        bufsize -= to_copy;

        kprintf( ">> IOVSIZE=%d, BUFSIZE: %d, DATA SIZE: %d\n",
                 iov_size,bufsize,data_size);

        if( data_size ) {
          if( iov_size ) {
            user_addr += to_copy;
          } else {
            iovecs++;
            user_addr = iovecs->iov_base;
            iov_size = iovecs->iov_len;

            kprintf( "# NEXT IOV (of %d) has length %d. Data left: %d\n",
                     numvecs,iov_size,data_size );
            repeat_first=true;
          }

          if( bufsize ) {
            dest_kaddr += to_copy;
          } else {
            if( bufs->first == bufs->length ) {
              bufs++;
              repeat_first=true;
              bufsize=bufs->first;
              chunk=bufs->chunks;
              dest_kaddr=(char *)*chunk;

              kprintf( "# NEXT BUF (of %d).\n",numbufs );
            }
          }
        }
      }
    } while(repeat_first);
*/

    /* Handle the rest of the buffer. */
    if( data_size ) {
      ulong_t delta;

      chunk++;
      dest_kaddr=(char *)*chunk;
      page_end = dest_kaddr+PAGE_SIZE;

      bufsize=MIN(data_size,bufs->length-bufs->first);
crunch_buffer:
      delta=MIN(bufsize,iov_size);
      delta=MIN(delta,data_size);
      kprintf( "------ IOVSIZE=%d, BUFSIZE: %d, DELTA: %d, DATA SIZE: %d\n",
               iov_size,bufsize,delta,data_size);

      while( delta ) {
        to_copy=MIN(delta,page_end-dest_kaddr);

        if( to_buffer ) {
          r=copy_from_user(dest_kaddr,user_addr,to_copy);
        } else {
          r=copy_to_user(user_addr,dest_kaddr,to_copy);
        }

        bufsize-=to_copy;
        iov_size-=to_copy;
        data_size-=to_copy;
        delta -= to_copy;

        dest_kaddr += to_copy;
        user_addr+=to_copy;

        if( bufsize && (dest_kaddr >= page_end) ) {
          chunk++;
          dest_kaddr=(char *)*chunk;
          page_end=dest_kaddr+PAGE_SIZE;
        }
      }

      if( bufsize && data_size ) {
        if( !iov_size ) {
          iovecs++;
          user_addr = iovecs->iov_base;
          iov_size = iovecs->iov_len;
        }
        goto crunch_buffer;
      } else if( !bufsize ) {
        /* Current buffer is over. */
        kprintf( "  ] Buffer is over !\n" );
        bufs++;
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
