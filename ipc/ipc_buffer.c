/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * ipc/ipc_buffer.c: routines for dealing with IPC buffers.
 *
 */

#include <ipc/buffer.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <ds/linked_array.h>
#include <eza/limits.h>
#include <eza/arch/page.h>
#include <mm/page.h>
#include <mlibc/stddef.h>
#include <ipc/gen_port.h>
#include <eza/usercopy.h>

#define LOCK_TASK_VM(x)
#define UNLOCK_TASK_VM(x)

int ipc_setup_buffer_pages(task_t *owner,iovec_t *iovecs,ulong_t numvecs,
                                uintptr_t *addr_array,ipc_user_buffer_t *bufs)
{
  rpd_t *rpd = task_get_rpd(owner);
  page_idx_t idx;
  ulong_t chunk_num;
  int r=-EFAULT;
  uintptr_t adr,*pchunk;

  LOCK_TASK_VM(owner);

  for(;numvecs;numvecs--,iovecs++,bufs++) {
    ipc_user_buffer_t *buf=bufs;
    uintptr_t start_addr=(uintptr_t)iovecs->iov_base;
    ulong_t first,size=iovecs->iov_len;

    buf->chunks=addr_array;

    /* Process the first chunk. */
    idx=mm_vaddr2page_idx(rpd,start_addr);
    if( idx < 0 ) {
      goto out;
    }

    pchunk=buf->chunks;

    adr=(start_addr+PAGE_SIZE) & ~PAGE_MASK;
    first=adr-start_addr;
    if( size <= first ) {
      first = size;
    }

    buf->first=first;
    *pchunk=(uintptr_t)pframe_id_to_virt(idx)+(start_addr & PAGE_MASK);

    size-=first;
    start_addr+=first;
    chunk_num=1;

    /* Process the rest of chunks. */
    while( size ) {
      idx=mm_vaddr2page_idx(rpd, start_addr);
      if(idx < 0) {
        goto out;
      }
      pchunk++;
      chunk_num++;

      if(size<=PAGE_SIZE) {
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

int ipc_transfer_buffer_data_iov(ipc_user_buffer_t *bufs,ulong_t numbufs,
                                 struct __iovec *iovecs,ulong_t numvecs,
                                 ulong_t offset,bool to_buffer)
{
  char *page_end;
  ulong_t *chunk;
  ulong_t to_copy,iov_size,bufsize,data_size;
  char *dest_kaddr;
  char *user_addr;
  long r,buf_offset=offset;
  ipc_user_buffer_t *start_buf=NULL;

  for(bufsize=0,to_copy=0;to_copy<numbufs;to_copy++) {
    bufsize+=bufs[to_copy].length;
    if( offset ) {
      if( !start_buf ) {
        if( buf_offset < bufs[to_copy].length ) {
          start_buf=&bufs[to_copy];
          /* We don't adjust buffer size here since it will be recalculated
           * later.
           */
        } else {
          buf_offset-=bufs[to_copy].length;
          bufsize-=bufs[to_copy].length;
        }
      }
    }
  }

  for(iov_size=0,to_copy=0;to_copy<numvecs;to_copy++) {
    iov_size+=iovecs[to_copy].iov_len;
  }

  if( offset ) {
    if( !start_buf ) {
      return -EINVAL; /* Too big offset. */
    }

    /* Now we can adjust buffer size and get the first data chunk.
     */
    bufs=start_buf;
    bufsize-=buf_offset;
    if( buf_offset < bufs->first ) {
      chunk=bufs->chunks;
      dest_kaddr=(char *)*chunk;
      page_end=dest_kaddr+bufs->first;
      dest_kaddr+=offset;
    } else {
      int d=((buf_offset-bufs->first) >> PAGE_WIDTH)+1;

      chunk=bufs->chunks+d;
      dest_kaddr=(char *)*chunk;
      page_end=dest_kaddr;
      page_end+=((bufs->length - buf_offset) < PAGE_SIZE) ? bufs->length - buf_offset : PAGE_SIZE;
      dest_kaddr+=((buf_offset-bufs->first) & PAGE_MASK);
    }
  }

  data_size=MIN(bufsize,iov_size);
  iov_size=iovecs->iov_len;
  user_addr=iovecs->iov_base;

  for(;data_size;) {
    chunk=bufs->chunks;
    bufsize=bufs->length;
    dest_kaddr=(char *)*chunk;
    page_end=dest_kaddr+bufs->first;

    /* Process one buffer. */
    while( data_size ) {
      to_copy=MIN(bufsize,iov_size);
      to_copy=MIN(to_copy,data_size);
      to_copy=MIN(to_copy,page_end-dest_kaddr);

      if( to_buffer ) {
        r=copy_from_user(dest_kaddr,user_addr,to_copy);
      } else {
        r=copy_to_user(user_addr,dest_kaddr,to_copy);
      }

      if( r ) {
        return -EFAULT;
      }

      data_size-= to_copy;
      iov_size -= to_copy;
      bufsize -= to_copy;

      dest_kaddr += to_copy;
      user_addr += to_copy;

      if( bufsize && (dest_kaddr >= page_end) ) {
        chunk++;
        dest_kaddr=(char *)*chunk;
        page_end=dest_kaddr+PAGE_SIZE;
      }

      if( data_size ) {
        if( !iov_size && numvecs ) {
          numvecs--;
          iovecs++;
          user_addr = iovecs->iov_base;
          iov_size = iovecs->iov_len;
        }
        if( !bufsize ) {
          bufs++;
          break;
        }
      }
    }
  }
  return 0;
}
