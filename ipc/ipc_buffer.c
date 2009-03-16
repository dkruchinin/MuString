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
#include <mm/page.h>
#include <mlibc/stddef.h>
#include <ipc/port.h>
#include <ipc/channel.h>
#include <eza/usercopy.h>

#define LOCK_TASK_VM(t)                         \
  do {                                          \
    if (likely(!is_kernel_thread(t)))           \
      rwsem_down_read(&(t)->task_mm->rwsem);    \
  } while (0)

#define UNLOCK_TASK_VM(t)                       \
  do {                                          \
    if (likely(!is_kernel_thread(t)))           \
      rwsem_up_read(&(t)->task_mm->rwsem);      \
  } while (0)
  
struct setup_buf_helper {
  ulong_t chunk_num;
  uintptr_t *pchunk;
};

static void __save_pfns_in_buffer(vmrange_t *unused, page_frame_t *page, void *helper)
{
  struct setup_buf_helper *buf_data = helper;

  pin_page_frame(page);
  *(page_idx_t *)(buf_data->pchunk) = pframe_number(page);
  buf_data->pchunk++;
  buf_data->chunk_num++;
}

int ipc_setup_buffer_pages(ipc_channel_t *channel,iovec_t *iovecs,ulong_t numvecs,
                           uintptr_t *addr_array,ipc_user_buffer_t *bufs, bool is_snd_buf)
{
  int r=-EFAULT;
  struct setup_buf_helper buf_data;
  ipc_user_buffer_t *buf;
  ulong_t first;
  
  buf_data.chunk_num = 0;
  buf_data.pchunk = NULL;
  
  LOCK_TASK_VM(current_task());
  for(;numvecs;numvecs--,iovecs++,bufs++) {
    buf=bufs;    

    buf->chunks=addr_array;
    buf_data.pchunk = buf->chunks;
    if (channel && likely(!(channel->flags & IPC_KERNEL_SIDE))) {
      uint32_t pfmask = PFLT_READ;

      if (!is_snd_buf)
        pfmask |= PFLT_WRITE;
      
      r = fault_in_user_pages(current_task()->task_mm, (uintptr_t)iovecs->iov_base, iovecs->iov_len,
                              pfmask, __save_pfns_in_buffer, &buf_data);
      if (r)
        goto out;
    }
    else {      
      uintptr_t vaddr_start, vaddr_end;

      vaddr_start = PAGE_ALIGN_DOWN(iovecs->iov_base);
      vaddr_end = PAGE_ALIGN((uintptr_t)iovecs->iov_base + iovecs->iov_len);
      while (vaddr_start < vaddr_end) {
        *(page_idx_t *)buf_data.pchunk = virt_to_pframe_id((void *)vaddr_start);
        buf_data.pchunk++;
        buf_data.chunk_num++;
      }
    }
    /* Process the first chunk. */

    first =(((uintptr_t)iovecs->iov_base + PAGE_SIZE) & ~PAGE_MASK) - (uintptr_t)iovecs->iov_base;
    if(iovecs->iov_len <= first) {
      first = iovecs->iov_len;
    }

    buf->first=first;
    //*pchunk=(uintptr_t)pframe_to_virt(page)+(start_addr & PAGE_MASK);

    buf->num_chunks=buf_data.chunk_num;
    buf->length=iovecs->iov_len;
    addr_array+=buf_data.chunk_num;
  }
  
  r = 0;
out:
  /* TODO DK: Unpin all pages if any was pinned earlier */
  UNLOCK_TASK_VM(current_task());
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

  data_size=MIN(bufsize,iov_size);
  iov_size=iovecs->iov_len;

  if( offset ) {
    if( !start_buf ) {
      return -EINVAL; /* Too big offset. */
    }

    /* Now we can adjust buffer size and get the first data chunk.
     */
    bufs=start_buf;
    if( buf_offset < bufs->first ) {
      chunk=bufs->chunks;
      dest_kaddr=(char *)pframe_id_to_virt(*(page_idx_t *)chunk) + (PAGE_SIZE - bufs->first);
      page_end=dest_kaddr+bufs->first;
      dest_kaddr+=buf_offset;
    } else {
      chunk=bufs->chunks+(((buf_offset-bufs->first) >> PAGE_WIDTH)+1);
      page_end=(char *)pframe_id_to_virt(*(page_idx_t *)chunk)+PAGE_SIZE;
      dest_kaddr = (page_end - PAGE_SIZE) + ((buf_offset-bufs->first) & PAGE_MASK);

      if( (page_end - dest_kaddr) > bufs->length-buf_offset ) {
        page_end=dest_kaddr+(bufs->length-buf_offset);
      }
    }
    bufsize=bufs->length-buf_offset;
  }

  user_addr=iovecs->iov_base;

  if( !offset ) {
    goto copy_iteration;
  }

  for(;data_size;) {
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
        dest_kaddr=(char *)pframe_id_to_virt(*(page_idx_t *)chunk);
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
  copy_iteration:
    chunk=bufs->chunks;
    bufsize=bufs->length;
    dest_kaddr=(char *)pframe_id_to_virt(*(page_idx_t *)chunk) + PAGE_SIZE - bufs->first;
    page_end=dest_kaddr+bufs->first;
  }
  return 0;
}
