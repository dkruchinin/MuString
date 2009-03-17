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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
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

/*
 * LOCK_TASK_VM and UNLOCK_TASK_VM macros are used for both kernel threads
 * and complete tasks. Kernel threads don't have task_mm structure instead
 * they only have a pointer to their root page directory(i.e. kernel page directory),
 * so they haven't anything to lock.
 */

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

  /* TODO DK: besides pinning page must be locked(i.e. "mlock'ed") */
  pin_page_frame(page);
  *buf_data->pchunk = pframe_number(page);
  buf_data->pchunk++;
  buf_data->chunk_num++;
}

int ipc_setup_buffer_pages(iovec_t *iovecs, uint32_t numvecs, page_idx_t *idx_array,
                           ipc_buffer_t *bufs, bool is_sender_buffer)
{
  int r = -EFAULT;
  struct setup_buf_helper buf_data;
  ipc_buffer_t *buf;
  task_t *owner = current_task();
  
  LOCK_TASK_VM(owner);
  for (buf = bufs; numvecs; numvecs--, iovecs++, buf++) {
    buf->chunks = idx_array;
    buf_data.pchunk = buf->chunks;
    buf_data.chunk_num = 0;

    /*
     * We assume that ipc_setup_buffer_pages receives already checked iovectors.
     * So if iovec's address range is not belong to user-space, then the buffer is
     * in kernel and we don't need to fault and pin each iovec page.
     */
    if (likely(valid_user_address_range((uintptr_t)iovecs->iov_base, iovecs->iov_len))) {
      uint32_t pfmask = PFLT_READ;

      kprintf("Am I here?\n");
      ASSERT(!is_kernel_thread(owner));
      if (!is_sender_buffer) { /* buffer for reveibing must be writable */
        pfmask |= PFLT_WRITE;
      }

      /*
       * When user gives us a buffer we have to take care about pages the buffer belongs to.
       * First of all the pages may not be present and we have to fault in each such page.
       * Also all pages that are used by the buffer must be pinned and locked.
       * We don't want that pages be unmapped from the owner's address space or simply swapped out
       * while we are resolving their indices.
       */
      r = fault_in_user_pages(owner->task_mm, (uintptr_t)iovecs->iov_base, iovecs->iov_len,
                              pfmask, __save_pfns_in_buffer, &buf_data);
      if (r) {
        goto out;
      }
    }
    else {      
      uintptr_t vaddr_start, vaddr_end;

      vaddr_start = PAGE_ALIGN_DOWN(iovecs->iov_base);
      vaddr_end = PAGE_ALIGN((uintptr_t)iovecs->iov_base + iovecs->iov_len);
      kprintf("S: %p; E: %p\n", vaddr_start, vaddr_end);
      while (vaddr_start < vaddr_end) {
        kprintf("(%d) Save ID: %#x\n", numvecs, virt_to_pframe_id((void *)vaddr_start));
        *buf_data.pchunk = virt_to_pframe_id((void *)vaddr_start);
        buf_data.pchunk++;
        buf_data.chunk_num++;
        vaddr_start += PAGE_SIZE;
      }
    }

    /* Offset from very first page to iov_base will be saved in buf->offset */
    buf->offset = (uintptr_t)iovecs->iov_base - PAGE_ALIGN_DOWN(iovecs->iov_base);

    kprintf("(%d) SND: first addr: %p -> %p\n", numvecs, iovecs->iov_base, (char *)iovecs->iov_base + (PAGE_SIZE - buf->offset));
    //*pchunk=(uintptr_t)pframe_to_virt(page)+(start_addr & PAGE_MASK);

    buf->num_chunks = buf_data.chunk_num;
    buf->length = iovecs->iov_len;
    idx_array += buf_data.chunk_num;
  }
  
  r = 0;
out:
  /* TODO DK: Unpin all pages if any was pinned earlier */
  UNLOCK_TASK_VM(owner);
  return r;
}

/* TODO DK: unpin pages after transferring is end */
int ipc_transfer_buffer_data_iov(ipc_buffer_t *bufs, uint32_t numbufs, iovec_t *iovecs,
                                 uint32_t numvecs, ulong_t offset, bool to_buffer)
{
  char *page_end;
  page_idx_t *chunk;
  ulong_t to_copy, iov_size, bufsize, data_size;
  char *dest_kaddr;
  char *user_addr;
  long r, buf_offset = offset;
  ipc_buffer_t *start_buf = NULL;

  for (bufsize = 0, to_copy = 0; to_copy < numbufs; to_copy++) {
    bufsize += bufs[to_copy].length;
    if (offset) {
      if (!start_buf) {
        if (buf_offset < bufs[to_copy].length) {
          start_buf = &bufs[to_copy];
          /* We don't adjust buffer size here since it will be recalculated later. */
        }
        else {
          buf_offset -= bufs[to_copy].length;
          bufsize -= bufs[to_copy].length;
        }
      }
    }
  }

  for (iov_size = 0, to_copy = 0; to_copy <numvecs; to_copy++) {
    iov_size += iovecs[to_copy].iov_len;
  }

  data_size = MIN(bufsize, iov_size);
  iov_size = iovecs->iov_len;

  if (offset) {
    if (!start_buf) {
      return -E2BIG; /* Too big offset. */
    }

    /* Now we can adjust buffer size and get the first data chunk. */
    bufs = start_buf;
    if (buf_offset < (PAGE_SIZE - bufs->offset)) {
      chunk = bufs->chunks;
      dest_kaddr = (char *)pframe_id_to_virt(*chunk) + bufs->offset;
      if (bufs->length >= (PAGE_SIZE - bufs->offset)) {
        page_end = dest_kaddr + (PAGE_SIZE - bufs->offset);
      }
      else {
        page_end = dest_kaddr + bufs->length;
      }
      
      kprintf("1 [RD]: First address %p (id = %#x)\n", dest_kaddr, *chunk);
      dest_kaddr += buf_offset;
    }
    else {
      ulong_t offs = (buf_offset - (PAGE_SIZE - bufs->offset));
      
      chunk = bufs->chunks + ((offs >> PAGE_WIDTH) + 1);
      dest_kaddr = (char *)pframe_id_to_virt(*chunk);
      page_end = dest_kaddr + PAGE_SIZE;
      dest_kaddr += offs & PAGE_MASK;
      kprintf("2 [RD]: First address %p (id = %#x)\n", dest_kaddr, *chunk);
      if ((page_end - dest_kaddr) > (bufs->length - buf_offset)) {
        page_end = dest_kaddr + (bufs->length - buf_offset);
      }
    }
    
    bufsize = bufs->length - buf_offset;
  }

  user_addr = iovecs->iov_base;
  if (!offset) {
    goto copy_iteration;
  }

  for (; data_size ;) {
    /* Process one buffer. */
    while (data_size) {
      to_copy = MIN(bufsize, iov_size);
      to_copy = MIN(to_copy, data_size);
      to_copy = MIN(to_copy, page_end - dest_kaddr);

      if (to_buffer) {
        r = copy_from_user(dest_kaddr, user_addr, to_copy);
      }
      else {
        r = copy_to_user(user_addr, dest_kaddr, to_copy);
      }
      if (r) {
        return -EFAULT;
      }

      data_size -= to_copy;
      iov_size -= to_copy;
      bufsize -= to_copy;

      dest_kaddr += to_copy;
      user_addr += to_copy;

      if (bufsize && (dest_kaddr >= page_end)) {
        chunk++;
        dest_kaddr = (char *)pframe_id_to_virt(*chunk);
        kprintf("[RD]: %#x\n", *(page_idx_t *)chunk);
        page_end = dest_kaddr + PAGE_SIZE;
      }
      if (data_size) {
        if (!iov_size && numvecs) {
          numvecs--;
          iovecs++;
          user_addr = iovecs->iov_base;
          iov_size = iovecs->iov_len;
        }
        if (!bufsize) {
          bufs++;
          break;
        }
      }
    }
  copy_iteration:
    chunk = bufs->chunks;
    bufsize = bufs->length;
    dest_kaddr = (char *)pframe_id_to_virt(*chunk) + bufs->offset;
    if (bufs->length >= (PAGE_SIZE - bufs->offset)) {
      kprintf("OFFSET: %#x\n", bufs->offset);
      page_end = dest_kaddr + (PAGE_SIZE - bufs->offset);
    }
    else {
      page_end = dest_kaddr + PAGE_SIZE;
    }

    kprintf("[RD] first addr %p (%#x) (%p)\n", dest_kaddr, *(page_idx_t *)chunk, page_end);
  }
  
  return 0;
}
