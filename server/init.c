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

#include <arch/types.h>
#include <mstring/scheduler.h>
#include <mstring/process.h>
#include <arch/mem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/vmm.h>
#include <mm/rmap.h>
#include <arch/elf.h>
#include <mstring/kconsole.h>
#include <mstring/errno.h>
#include <kernel/elf.h>
#include <mstring/kprintf.h>
#include <server.h>
#include <mstring/unistd.h>
#include <mstring/process.h>
#include <mstring/ptd.h>
#include <mstring/gc.h>
#include <config.h>
#include <mm/page.h>

long initrd_start_page,initrd_num_pages;
struct server_ops *server_ops = NULL;

#define BOOTENV  "BOOTSTAGE=grub"

#define USER_STACK_SIZE 16
#ifndef CONFIG_TEST

static int mmap_core(vmm_t *vmm, uintptr_t addr, page_idx_t first_page,
                     pgoff_t npages, kmap_flags_t flags)
{
  pgoff_t i;
  int ret = 0;
  page_frame_t *page;
  vmrange_t *vmr;

  vmr = vmrange_find(vmm, addr, addr + 1, NULL);
  ASSERT(vmr != NULL);
  RPD_LOCK_WRITE(&vmm->rpd);
  for (i = 0; i < npages; i++, first_page++, addr += PAGE_SIZE) {
    ret = mmap_page(&vmm->rpd, addr, first_page, flags);
    if (ret)
      goto out;
    if (likely(page_idx_is_present(first_page))) {
      page = pframe_by_id(first_page);
      pin_page_frame(page);
      lock_page_frame(page, PF_LOCK);
      rmap_register_anon(page, vmm, addr);
      unlock_page_frame(page, PF_LOCK);

      page->offset = addr2pgoff(vmr, addr);
    }
  }

out:
  RPD_UNLOCK_WRITE(&vmm->rpd);
  return ret;
}

static void __create_task_mm(task_t *task, int num, init_server_t *srv)
{
  vmm_t *vmm = task->task_mm;
  uintptr_t code;
  size_t code_size,text_size;
  ulong_t *pp;
  elf_head_t ehead;
  elf_pr_t epr;
  elf_sh_t esh;
  int *argc;
  void *sbss;
  uintptr_t *argv, *envp;
  char *arg1, *envp1;
  uintptr_t ustack_top;
  /* sections */
  uintptr_t exec_virt_addr = 0, exec_size = 0, ro_virt_addr = 0, ro_size = 0;
  uintptr_t rw_virt_addr = 0, rw_size = 0, bss_virt_addr = 0, bss_size = 0;
  uintptr_t exec_off = 0, ro_off = 0, rw_off = 0, bss_off = 0, rw_size_k;
  long r;
  int i;
  per_task_data_t *ptd;

  code_size = srv->size >> PAGE_WIDTH;
  code_size++;

  code = srv->addr;
  code>>=PAGE_WIDTH; /* get page number */

  pp = pframe_id_to_virt(code);

  /* read elf headers */
  memcpy(&ehead, pframe_id_to_virt(code), sizeof(elf_head_t));
#if 0
  /* printf elf header info */
  kprintf("ELF header(%4s): %d type, %d mach, %d version\n", ehead.e_ident,
          ehead.e_type, ehead.e_machine, ehead.e_version);
  kprintf("Entry: %p,Image off: %p,sect off:%p\n", ehead.e_entry, ehead.e_phoff,
          ehead.e_shoff);
#endif
  /* just for info */
  for(i=0; i<ehead.e_phnum; i++) {
    /* read program size */
    memcpy(&epr,pframe_id_to_virt(code) + sizeof(ehead) + i*(ehead.e_phentsize),
           sizeof(epr));
    /*kprintf("PHeader(%d): offset: %p\nvirt: %p\nphy: %p\n",
      i, epr.p_offset, epr.p_vaddr, epr.p_paddr);*/

  }

  for(i=0; i<ehead.e_shnum; i++) {
    memcpy(&esh, pframe_id_to_virt(code) + ehead.e_shoff +
           i*(ehead.e_shentsize), sizeof(esh));

    if(esh.sh_size != 0) {
      //kprintf("SHeader(%d): shaddr: %p\nshoffset:%p\n",i,esh.sh_addr,esh.sh_offset);

      if((esh.sh_flags & ESH_ALLOC) && (esh.sh_type == SHT_PROGBITS)) {
	if(esh.sh_flags & ESH_EXEC) { /* text segments */
          if(!exec_virt_addr) exec_virt_addr = esh.sh_addr;
          if(!exec_off) exec_off = esh.sh_offset;
          exec_size += esh.sh_size;
	} else if((esh.sh_flags & ESH_WRITE)) { /* data segments */
          if(!rw_virt_addr) rw_virt_addr = esh.sh_addr;
          if(!rw_off) rw_off = esh.sh_offset;
          rw_size += esh.sh_size;
	} else { /* rodata segments */
          if(!ro_virt_addr) ro_virt_addr = esh.sh_addr;
          if(!ro_off) ro_off = esh.sh_offset;
          ro_size += esh.sh_size;
	}
      } else if(esh.sh_flags & ESH_ALLOC && esh.sh_type==SHT_NOBITS) { /* bss */
        if(!bss_virt_addr) bss_virt_addr = esh.sh_addr;
        if(!bss_off) bss_off = esh.sh_offset;
        bss_size += esh.sh_size;
      }
    }
  }
#if 0
  kprintf("Map stats:\n\t text segments: va %p, bo %p, sz %ld\n"
          "\t rodata segments: va %p, bo %p, sz %ld\n"
          "\t data segments: va %p, bo %p, sz %ld\n"
          "\t bss segments: va %p, bo %p, sz %ld\n",
          exec_virt_addr, exec_off, exec_size, ro_virt_addr, ro_off, ro_size,
          rw_virt_addr, rw_off, rw_size, bss_virt_addr, bss_off, bss_size);
#endif
  /* now we're need to determine ranges */
  if(PAGE_ALIGN(exec_virt_addr + exec_size) == PAGE_ALIGN(ro_virt_addr)) {
    /* sysv abi allows to split this segments, but we're won't */
    ro_size -= (PAGE_ALIGN(ro_virt_addr) - ro_virt_addr);
    ro_off += (PAGE_ALIGN(ro_virt_addr) - ro_virt_addr);
    ro_virt_addr = PAGE_ALIGN(ro_virt_addr);
  }
  if(PAGE_ALIGN(rw_virt_addr + rw_size) == PAGE_ALIGN(bss_virt_addr)) {
    /* let's keep virt addr to clean it afterwhile */
    bss_off = bss_virt_addr;
    bss_size -= (PAGE_ALIGN(bss_virt_addr) - bss_virt_addr);
    bss_virt_addr = PAGE_ALIGN(bss_virt_addr);
  }
#if 0
  kprintf("Map stats:\n\t text segments: va %p, bo %p, sz %ld\n"
          "\t rodata segments: va %p, bo %p, sz %ld\n"
          "\t data segments: va %p, bo %p, sz %ld\n"
          "\t bss segments: va %p, bo %p, sz %ld\n",
          exec_virt_addr, exec_off, exec_size, ro_virt_addr, ro_off, ro_size,
          rw_virt_addr, rw_off, rw_size, bss_virt_addr, bss_off, bss_size);
#endif
  text_size = exec_size>>PAGE_WIDTH;
  if(exec_size%PAGE_SIZE)    text_size++;
  code = srv->addr + exec_off;

  r = vmrange_map(generic_memobj, vmm, USPACE_VADDR_BOTTOM, text_size,
                  VMR_READ | VMR_EXEC | VMR_PRIVATE | VMR_FIXED, 0);
  if (!PAGE_ALIGN(r))
    panic("Server [#%d]: Failed to create VM range for \"text\" section. (ERR = %d)",
          num, r);

  r = mmap_core(vmm, USPACE_VADDR_BOTTOM, code >> PAGE_WIDTH,
                text_size, KMAP_READ | KMAP_EXEC);
  if (r)
    panic("Server [#%d]: Failed to map \"text\" section. (ERR = %d)", num, r);

  if (ro_size) {
    if(ro_size%PAGE_SIZE) {
      ro_size>>=PAGE_WIDTH;
      ro_size++;
    } else
      ro_size>>=PAGE_WIDTH;

    r = vmrange_map(generic_memobj, vmm, ro_virt_addr, ro_size,
                    VMR_READ | VMR_PRIVATE | VMR_FIXED, 0);
    if (!PAGE_ALIGN(r))
      panic("Server [#%d]: Failed to create VM range for \"rodata\" section. (ERR = %d)",
            num, r);

    r = mmap_core(vmm, ro_virt_addr, (srv->addr + ro_off) >> PAGE_WIDTH,
                  ro_size, KMAP_READ);
    if (r)
      panic("Server [#%d]: Failed to map \"rodata\" section. (ERR = %d)", num, r);
  }

  if (rw_size) {
    rw_size_k = rw_size;
    if(rw_size%PAGE_SIZE) {
      rw_size>>=PAGE_WIDTH;
      rw_size++;
    } else
      rw_size>>=PAGE_WIDTH;

    r = vmrange_map(generic_memobj, vmm, PAGE_ALIGN_DOWN(rw_virt_addr), rw_size,
                    VMR_READ | VMR_WRITE | VMR_PRIVATE | VMR_FIXED, 0);
    if (!PAGE_ALIGN(r))
      panic("Server [#%d]: Failed to create VM range for \"rwdata\" section. (ERR = %d)",
            num, r);

    r = mmap_core(vmm, PAGE_ALIGN_DOWN(rw_virt_addr), (srv->addr + rw_off) >> PAGE_WIDTH,
                  rw_size, KMAP_READ | KMAP_WRITE);
    if (r)
      panic("Server [#%d]: Failed to map \"rwdata\" section. (ERR = %d)", num, r);
  }

  if (bss_size) {
    if(bss_size%PAGE_SIZE) {
      bss_size>>=PAGE_WIDTH;
      bss_size++;
    } else
      bss_size>>=PAGE_WIDTH;

    r = vmrange_map(generic_memobj, vmm, bss_virt_addr, bss_size,
                    VMR_READ | VMR_WRITE | VMR_PRIVATE | VMR_FIXED | VMR_POPULATE, 0);
    if (!PAGE_ALIGN(r))
      panic("Server [#%d]: Failed to create VM range for \"bss\" section. (ERR = %d)",
            num, r);

    /* let's zero small bss chunk */
    if(PAGE_ALIGN(rw_virt_addr + rw_size_k) == PAGE_ALIGN(bss_off)) {
      sbss = user_to_kernel_vaddr(task_get_rpd(task), bss_off);
      memset(sbss, 0, PAGE_ALIGN(bss_off) - bss_off);
    }
  }

  /*remap pages*/
  r = vmrange_map(generic_memobj, vmm, USPACE_VADDR_TOP - 0x40000, USER_STACK_SIZE,
                  VMR_READ | VMR_WRITE | VMR_STACK | VMR_PRIVATE | VMR_POPULATE
                  | VMR_FIXED, 0);
  /*r = mmap_core(task_get_rpd(task), USPACE_VA_TOP-0x40000,
    pframe_number(stack), USER_STACK_SIZE, KMAP_READ | KMAP_WRITE);*/
  if (!PAGE_ALIGN(r))
    panic("Server [#%d]: Failed to create VM range for stack. (ERR = %d)", num, r);

  /* Now allocate stack space for per-task user data. */
  ustack_top=USPACE_VADDR_TOP-0x40000+(USER_STACK_SIZE<<PAGE_WIDTH);
  ustack_top-=PER_TASK_DATA_SIZE;

  ptd=user_to_kernel_vaddr(task_get_rpd(task),ustack_top);
  if( !ptd ) {
    panic("Server [#%d]: Invalid address: %p", num, ustack_top);
  }

  ptd->ptd_addr=(uintptr_t)ustack_top;
  r=arch_process_context_control(task,SYS_PR_CTL_SET_PERTASK_DATA,(uintptr_t)ustack_top);
  if(r < 0) {
    panic("Server [#%d]: Failed to set pertask data(%p). (ERR = %d)", num, ustack_top, r);
  }

  /* Insufficient return address to prevent task from returning to void. */
  ustack_top -= sizeof(uintptr_t);
  /* setup argc, argv, env */
  ustack_top -= (5*sizeof(uintptr_t) + strlen(srv->name) + 2*sizeof(char) +
                 strlen(BOOTENV));
  argc = user_to_kernel_vaddr(task_get_rpd(task),ustack_top);
  *argc = 1; /* we're actually set only srv name */

  /* set argv, envp pointers */
  argv = (uintptr_t *)((char *)argc + sizeof(uintptr_t));
  *argv = (uintptr_t)((char *)ustack_top + 5*sizeof(uintptr_t));
  envp = (uintptr_t *)((char *)argv + 2*sizeof(uintptr_t));
  *envp = (uintptr_t)((char *)ustack_top + 5*sizeof(uintptr_t) +
                      strlen(srv->name) + sizeof(char));

  /* fill values for argv, envp */
  arg1 = (char *)((char *)argc + 5*sizeof(uintptr_t));
  envp1 = (char *)((char *)argc + 5*sizeof(uintptr_t) +
                   strlen(srv->name) + sizeof(char));
  memset(arg1, 0, strlen(srv->name) + sizeof(char));
  memset(envp1, 0, strlen(BOOTENV) + sizeof(char));
  memcpy(arg1, srv->name, strlen(srv->name));
  memcpy(envp1, BOOTENV, strlen(BOOTENV));

  r=arch_process_context_control(task,SYS_PR_CTL_SET_ENTRYPOINT,ehead.e_entry);
  if (r < 0)
    panic("Server [#%d]: Failed to set task's entry point(%p). (ERR = %d)",
          num, ehead.e_entry, r);

  r=arch_process_context_control(task,SYS_PR_CTL_SET_STACK,ustack_top);
  if (r < 0)
    panic("Server [#%d]: Failed to set task's stack(%p). (ERR = %d)", num, ustack_top, r);
}

static void __server_task_runner(void *data)
{
  int i,a;
  task_t *server;
  init_server_t srv;
  int r,sn;
  kconsole_t *kconsole=default_console();
  int delay=CONFIG_CORESERVERS_LAUNCH_DELAY > 300 ? CONFIG_CORESERVERS_LAUNCH_DELAY : 300;

  i = server_ops->get_num_servers();
  if( i > 0 ) {
    kprintf("[LAUNCHER] Starting %d servers with delay %d. First user (non-NS) PID is %d\n",
            i,delay,2*CONFIG_NRCPUS+3);
    if (kconsole == &vga_console)
      kconsole->disable();
  }

  for(sn=0,a=0;a<i;a++) {
    char *modvbase;

    server_ops->get_server_by_num(a, &srv);
    if (srv.name != NULL) {
      kprintf("[LAUNCHER] Starting server: %s\n", srv.name);
    }
    else {
      kprintf("[LAUNCHER] Starting server: %d.\n", a + 1);
    }

    modvbase=pframe_id_to_virt(srv.addr>>PAGE_WIDTH);
    if( *(uint32_t *)modvbase == ELF_MAGIC ) { /* ELF module ? */
      ulong_t t;

      if( !sn ) { /* First module is always NS. */
        t=TASK_INIT;
      } else {
        t=0;
      }

      r=create_task(current_task(),t,TPL_USER,&server,NULL);
      if( r ) {
        panic("server_run_tasks(): Can't create task N %d !\n",a+1);
      }

      if( !sn ) {
        if( server->pid != 1 ) {
          panic( "server_run_tasks(): NameServer has impropr PID: %d !\n",
                 server->pid );
        }
      }

      __create_task_mm(server, a, &srv);

#ifdef CONFIG_CORESERVERS_PERCPU_LAUNCH
      t=sn % CONFIG_NRCPUS;

      if( t != cpu_id() ){ 
        sched_move_task_to_cpu(server,t);
      }
#endif

      r=sched_change_task_state(server,TASK_STATE_RUNNABLE);
      if( r ) {
        panic( "server_run_tasks(): Can't launch core task N%d !\n",a+1);
      }

      sn++;
      sleep(delay);
    } else if( !strncmp(&modvbase[257],"ustar",5 ) ) { /* TAR-based ramdisk ? */
      if( initrd_start_page ) {
        panic("Only one instance of initial RAM disk is allowed !");
      }

      initrd_start_page=srv.addr>>PAGE_WIDTH;
      initrd_num_pages=PAGE_ALIGN(srv.size)>>PAGE_WIDTH;
    } else {
      panic("Unrecognized kernel module N %d !\n",a+1);
    }
  }
  kprintf("[LAUNCHER]: All servers started. Exiting ...\n");
  sys_exit(0);
}

void server_run_tasks(void)
{
  ASSERT(server_ops != NULL);
  if( kernel_thread(__server_task_runner,NULL,NULL) ) {
    panic("Can't launch a Core Servers runner !");
  }
}

#else

void server_run_tasks(void)
{
  /* In test mode we do nothing. */
}

#endif /* !CONFIG_TEST */
