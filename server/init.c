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
#include <kernel/vm.h>
#include <mlibc/kprintf.h>
#include <server.h>
#include <mlibc/unistd.h>

static status_t __create_task_mm(task_t *task, int num)
{
  page_frame_iterator_t pfi;
  uintptr_t code;
  size_t code_size,data_size,text_size,bss_size;
  page_frame_t *stack;
  page_frame_t *bss;
  ulong_t *pp;
  elf_head_t ehead;
  elf_pr_t epr;
  elf_sh_t esh;
  page_idx_t idx;
  uintptr_t text,data_bss,bss_virt;
  size_t real_code_size=0,real_data_size=0;
  size_t last_data_size,real_data_offset=0;
  size_t last_offset,last_sect_size,last_data_offset;
  ITERATOR_CTX(page_frame,PF_ITER_INDEX) pfi_idx_ctx;
  status_t r;
  int i;

  stack=alloc_pages(USER_STACK_SIZE,AF_PGEN);
  if(!stack)
    return -1;

  code_size=init.server[num].size>>PAGE_WIDTH; /*it's a code size in pages */
  code_size++;

  code=init.server[num].addr; /* we're have a physical address of code here */
  code>>=PAGE_WIDTH; /* get page number */
  pp=pframe_id_to_virt(code);
    /**
     * ELF header   
     * unsigned char e_ident[EI_NIDENT];  ELF64 magic number 
     * uint16_t e_type;  elf type 
     * uint16_t e_machine;  elf required architecture 
     * uint32_t e_version;  elf object file version 
     * uintptr_t e_entry;  entry point virtual address 
     * uintptr_t e_phoff;  program header file offset 
     * uintptr_t e_shoff;  section header file offset 
     * uint32_t e_flags;  processor specific object tags 
     * uint16_t e_ehsize;  elf header size in bytes 
     * uint16_t e_phentsize;  program header table entry size 
     * uint16_t e_phnum;  program header count 
     * uint16_t e_shentsize;  section header table entry size 
     * uint16_t e_shnum;  section header count 
     * uint16_t e_shstrndx;  section header string table index 
     */
  /* read elf headers */
  memcpy(&ehead,pframe_id_to_virt(code),sizeof(elf_head_t));
  /* printf elf header info */
  /*kprintf("ELF header(%s): %d type, %d mach, %d version\n",ehead.e_ident,ehead.e_type,ehead.e_machine,ehead.e_version);
  kprintf("Entry: %p,Image off: %p,sect off:%p\n",ehead.e_entry,ehead.e_phoff,ehead.e_shoff);*/

  for(i=0;i<ehead.e_phnum;i++) {
    /* read program size */
    memcpy(&epr,pframe_id_to_virt(code)+sizeof(ehead)+i*(ehead.e_phentsize),sizeof(epr));
    /*kprintf("PHeader(%d): offset: %p\nvirt: %p\nphy: %p\n",
	    i,epr.p_offset,epr.p_vaddr,epr.p_paddr);*/

  }
  for(i=0;i<ehead.e_shnum;i++) {
    memcpy(&esh,pframe_id_to_virt(code)+ehead.e_shoff+i*(ehead.e_shentsize),sizeof(esh));
    if(esh.sh_size!=0) {
/*      kprintf("SHeader(%d): shaddr: %p\nshoffset:%p\n",i,esh.sh_addr,esh.sh_offset);*/
      if(esh.sh_flags & ESH_ALLOC && esh.sh_type==SHT_PROGBITS) {
	if(esh.sh_flags & ESH_EXEC) {
	  real_code_size+=esh.sh_size;
	  last_offset=esh.sh_addr;
	  last_sect_size=esh.sh_size;
	}
	if(esh.sh_flags & ESH_WRITE) {
	  real_data_size+=esh.sh_size;
	  if(real_data_offset==0) 
	    real_data_offset=esh.sh_addr;
	  last_data_offset=esh.sh_addr;
	  last_data_size=esh.sh_size;
	}
/*	atom_usleep(100);*/
      } else if(esh.sh_flags & ESH_ALLOC && esh.sh_type==SHT_NOBITS) { /* seems to be an bss section */
	bss_virt=esh.sh_addr; 
	bss_size=esh.sh_size;
      }
    }
  }

  /* print debug info */
/*  kprintf("Code: real size: %d, last_offset= %p, last section size= %d\n",
	  real_code_size,last_offset,last_sect_size);  code parsed values */
/*  kprintf("Data: real size: %d, last offset= %p, last section size= %d\nData offset: %p\n",
	  real_data_size,last_data_offset,last_data_size,real_data_offset);*/
  /* calculate text */
  code=init.server[num].addr+0x1000;
  text_size=real_code_size>>PAGE_WIDTH;
  if(real_code_size%PAGE_SIZE)    text_size++;
  data_bss=init.server[num].addr+real_data_offset-0x1000000;
/*  kprintf("data bss: %p\n text: %p\n",data_bss,code);*/
  data_size=real_data_size>>PAGE_WIDTH;
  if(real_data_size%PAGE_SIZE)    data_size++;
  /* calculate bss */
  if(bss_size%PAGE_SIZE) {
    bss_size>>=PAGE_WIDTH;
    bss_size++;
  } else 
    bss_size>>=PAGE_WIDTH;

  /* alloc memory for bss */
  bss=alloc_pages(bss_size,AF_PGEN|AF_ZERO);
  if(!bss)    return -1;


  /*  kprintf("elf entry -> %p\n",ehead.e_entry); */

  /*remap pages*/
  mm_init_pfiter_index(&pfi,&pfi_idx_ctx,code>>PAGE_WIDTH,(code>>PAGE_WIDTH)+text_size-1); /* .text + .rodata sections */
  r = mm_map_pages(&task->page_dir,&pfi,USER_START_VIRT,text_size,MAP_RW);
  if(r!=0)    return -1;

  mm_init_pfiter_index(&pfi,&pfi_idx_ctx,data_bss>>PAGE_WIDTH,(data_bss>>PAGE_WIDTH)+data_size-1); /* .text + .rodata sections */
  r = mm_map_pages(&task->page_dir,&pfi,real_data_offset,data_size,MAP_RW);
  if(r!=0)    return -1;

  mm_init_pfiter_index(&pfi,&pfi_idx_ctx,pframe_number(bss),pframe_number(bss)+bss_size);
  r=mm_map_pages(&task->page_dir,&pfi,bss_virt,bss_size,MAP_RW);
  if(r!=0)    return -1;

  mm_init_pfiter_index(&pfi,&pfi_idx_ctx,pframe_number(stack),pframe_number(stack)+USER_STACK_SIZE);
  r=mm_map_pages(&task->page_dir,&pfi,USPACE_END-0x40000,USER_STACK_SIZE,MAP_RW);
  if(r!=0)    return -1;

  r=do_task_control(task,SYS_PR_CTL_SET_ENTRYPOINT,ehead.e_entry);
  r|=do_task_control(task,SYS_PR_CTL_SET_STACK,USPACE_END-0x40000+(USER_STACK_SIZE<<PAGE_WIDTH));

  idx=mm_pin_virtual_address(&task->page_dir,USER_START_VIRT);
  idx=mm_pin_virtual_address(&task->page_dir,KERNEL_BASE + 0x8000);

  if(r!=0)    return -1;

  /*  kprintf("Grub module: %p\n size: %ld\n",init.server[num].addr,init.server[num].size);*/



  return 0;
}

void server_run_tasks(void)
{
  int i=server_get_num(),a;
  task_t *server;
  status_t r;

  if(i<=0)
    return;

  kprintf("[SRV] Starting servers ... \n");

  for(a=0;a<i;a++) {
    r=create_task(current_task(),0,TPL_USER,&server);
    if(r)      continue;
    r=__create_task_mm(server,a);
    if(r)      continue;
    r=sched_change_task_state(server,TASK_STATE_RUNNABLE);
    if(r)      continue;
  }

  return;
}


