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
 * (c) Copyright 2005,2008 Tirra <madtirra@jarios.org>
 *
 * server/init.c: servers initialization and running
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/process.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/mm.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/pt.h>
#include <eza/arch/elf.h>
#include <kernel/elf.h>
#include <mlibc/kprintf.h>
#include <server.h>

static status_t __create_task_mm(task_t *task, int num)
{
  page_frame_iterator_t pfi;
  uintptr_t code;
  size_t code_size;
  page_frame_t *stack;
  ulong_t *pp;
  elf_head_t ehead;
  page_idx_t idx;
  ITERATOR_CTX(page_frame,PF_ITER_INDEX) pfi_idx_ctx;
  status_t r;

  /*TODO: make default macro for stack pages*/
  stack=alloc_pages(4,AF_PGEN);
  if(!stack)
    return -1;

  code_size=init.server[num].size>>PAGE_WIDTH; /*it's a code size in pages */
  code_size++;

  code=init.server[num].addr; /* we're have a physical address of code here */
  code>>=PAGE_WIDTH; /* get page number */
  pp=pframe_id_to_virt(code);

  memcpy(&ehead,pframe_id_to_virt(code),sizeof(elf_head_t));

  kprintf("elf entry -> %p\n",ehead.e_entry);

  /*remap pages*/
  mm_init_pfiter_index(&pfi,&pfi_idx_ctx,code,code+code_size-1);

  r = mm_map_pages( &task->page_dir, &pfi,
                    0x1fffff000000, code_size,
                    MAP_RW );

  if(r!=0)
    return -1;

  mm_init_pfiter_index(&pfi,&pfi_idx_ctx,pframe_number(stack),pframe_number(stack)+4);
  r=mm_map_pages(&task->page_dir,&pfi,0x1fffff000000+(code_size<<PAGE_WIDTH),4,MAP_RW);
  if(r!=0)
    return -1;

  r=do_task_control(task,SYS_PR_CTL_SET_ENTRYPOINT,0x1fffff000000+ehead.e_entry);
  r|=do_task_control(task,SYS_PR_CTL_SET_STACK,0x1fffff000000+(code_size<<12)+(4<<PAGE_WIDTH));

  idx=mm_pin_virtual_address(&task->page_dir,0x1fffff000000);
  idx=mm_pin_virtual_address(&task->page_dir,KERNEL_BASE + 0x8000);

  if(r!=0)
    return -1;

  kprintf("[SRV] Server #(%d) code size: %d pages\n",num,code_size);

  /*  kprintf("Grub module: %p\n size: %ld\n",init.server[num].addr,init.server[num].size);*/

  //for(;;);

  return 0;
}

void server_run_tasks(void)
{
  int i=server_get_num();
  task_t *server;
  status_t r;

  if(i<=0)
    return;
  while(i>0) {
    r=create_task(current_task(),0,TPL_USER,&server);
    if(!r) r=__create_task_mm(server,0);
    if(!r) r=sched_change_task_state(server,TASK_STATE_RUNNABLE);

    i--;
  }

  return;
}


