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
 * (c) Copyright 2008 MadTirra <madtirra@jarios.org>
 *
 * kernel/mmap.c: Kernel mmap syscall
 *
 */

#include <eza/arch/types.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/process.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/mm.h>
#include <eza/errno.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/pt.h>
#include <mlibc/kprintf.h>
#include <server.h>
#include <kernel/vm.h> 
#include <kernel/mman.h> 

/*TODO: ak, redesign it according to mm changes */

/* system call used to map memory pages, ``memory'' means a regular area */
status_t sys_mmap(uintptr_t addr,size_t size,uint32_t flags,shm_id_t fd,uintptr_t offset)
{
  page_frame_t *aframe;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame,PF_ITER_INDEX) pfi_idx_ctx;
  status_t e;
  task_t *task;
  int _flags;

  /* simple check */
  if(!addr || !size) {
    return -EINVAL;
  }

  task=current_task();

  if(fd==NOFD) {
    if(flags & MMAP_PHYS) {
      /* TODO: ak, check rights */
      mm_init_pfiter_index(&pfi,&pfi_idx_ctx,pframe_number(aframe),pframe_number(aframe)+size);
      e=mm_map_pages(&task->page_dir,&pfi,addr,size,MAP_RW|MMAP_NONCACHABLE); /* TODO: ak, valid mapping */
      if(e!=0) return e;
      return 0;
    }
    aframe=alloc_pages(size,AF_PGEN);
    if(!aframe)
      return -ENOMEM; /* no free pages to allocate */
    
    if(flags | MMAP_RW) /*map_rd map_rdonly*/
      _flags |= MAP_RW;
    if(flags | MMAP_RDONLY)
      _flags|= MAP_RDONLY;

    mm_init_pfiter_index(&pfi,&pfi_idx_ctx,pframe_number(aframe),pframe_number(aframe)+size);
    e=mm_map_pages(&task->page_dir,&pfi,addr,size,_flags); /* TODO: ak, valid mapping */
    if(e!=0) return e;
  } else 
    return -ENOSYS;
  
  return 0;
}

