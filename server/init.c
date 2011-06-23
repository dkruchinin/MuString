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
 * (c) Copyright 2005,2008,2011 Tirra <madtirra@jarios.org>
 * (c) Copyright 2010,2011 Jari OS ry <http://jarios.org>
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

static char **__get_argvs(char *cmd, int *argc)
{
  char *ln = cmd, *lp;
  int as = 1, i = 0;
  char **argv_list = NULL;

  while(*ln) {
    ln += sizeof(char);
    if(*ln == ' ') as++;
  }

  if(!(argv_list = memalloc(sizeof(uintptr_t)*as))) {
  __enomem:
    panic("[Service init] Cannot allocate memory.\n");
  }

  *argc = as;

  if(as == 1) {
    argv_list[0] = memalloc(strlen(cmd) + sizeof(char));
    if(!argv_list[0]) goto __enomem;
    memset(argv_list[0], 0, strlen(cmd) + sizeof(char));
    memcpy(argv_list[0], cmd, strlen(cmd) + sizeof(char));
  } else {
    ln = cmd; lp = cmd;
    while(true) {
      if(*ln == ' ' || *ln == '\0') {
        argv_list[i] = memalloc(sizeof(char)*(ln - lp) + sizeof(char));
        if(!argv_list[i]) goto __enomem;
        memset(argv_list[i], 0, sizeof(char)*(ln - lp) + sizeof(char));
        memcpy(argv_list[i], lp, sizeof(char)*(ln - lp));

        lp = ln + sizeof(char); i++;
      }
      if(*ln != '\0') ln += sizeof(char);
      else break;
    }

  }

  return argv_list;
}

static void __create_task_mm(task_t *task, int num, init_server_t *srv)
{
  struct bin_map *emap = get_elf_map(task,srv);
  per_task_data_t *ptd;
  vmm_t *vmm = task->task_mm;
  ulong_t entry = get_elf_entry(task,srv);
  struct bin_map *cur = emap;
  ulong_t sseek = 0, psize;
  void *sbss;
  int r, flags, kflags;
  int *argc;
  uintptr_t ustack_top;
  uintptr_t *argv, *envp;
  char *arg1, *envp1;

  if(!emap)
    panic("[Service start] Cannot load ELF map of module %d\n", num);

  /* map image sections */
  while(cur) {
    /* check for override */
    if(cur->prev && (cur->virt_addr <
                     PAGE_ALIGN(cur->prev->virt_addr + cur->prev->size))) {
      sseek = PAGE_ALIGN(cur->virt_addr) - cur->virt_addr;
      cur->bin_addr += sseek;
      cur->virt_addr = PAGE_ALIGN(cur->virt_addr);
      cur->size -= sseek;

      /* if it's NO_BITS section it should be zeroed */
      if(cur->type == SHT_NOBITS) {
        sbss = user_to_kernel_vaddr(task_get_rpd(task), PAGE_ALIGN_DOWN(cur->virt_addr -
                                                                        sseek));
        memset((sbss + PAGE_SIZE) - sseek, 0, sseek);
      }
    }

    /* create vm range for this region */
    flags = VMR_PRIVATE | VMR_FIXED;
    kflags = 0;
    if(cur->flags & ESH_EXEC) {
      flags |= VMR_EXEC;
      kflags |= KMAP_EXEC;
    }

    flags |= VMR_READ;
    kflags |= KMAP_READ;

    if(cur->flags & ESH_WRITE) {
      flags |= VMR_WRITE;
      kflags |= KMAP_WRITE;
    }
    if(cur->flags == SHT_NOBITS) flags |= VMR_POPULATE;

    psize = (cur->size + (cur->virt_addr - PAGE_ALIGN_DOWN(cur->virt_addr)))
      >> PAGE_WIDTH;
    if(psize<<PAGE_WIDTH < (cur->size + (cur->virt_addr -
                                         PAGE_ALIGN_DOWN(cur->virt_addr)))) psize++;

    r = vmrange_map(generic_memobj, vmm, PAGE_ALIGN_DOWN(cur->virt_addr), psize,
                    flags, 0);
    if(!PAGE_ALIGN(r))
      panic("Server [#%d]: Failed to create VM range for section. (ERR = %d)", num, r);

    if(cur->type == SHT_PROGBITS) {
      r = mmap_core(vmm, PAGE_ALIGN_DOWN(cur->virt_addr),
                    PAGE_ALIGN_DOWN(cur->bin_addr) >> PAGE_WIDTH, psize, kflags);
      if(r)
        panic("Server [#%d]: Failed to map section. (ERR = %d)", num, r);
    }

    cur = cur->next;
  }

  /* map stack pages */
  r = vmrange_map(generic_memobj, vmm, USPACE_VADDR_TOP - 0x40000, USER_STACK_SIZE,
                  VMR_READ | VMR_WRITE | VMR_STACK | VMR_PRIVATE | VMR_POPULATE
                  | VMR_FIXED, 0);

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

  int args_n = 0, i, al = 0;
  char **argvs = NULL;

  argvs = __get_argvs(srv->name, &args_n);
  /* Insufficient return address to prevent task from returning to void. */
  ustack_top -= sizeof(uintptr_t);

  /* setup argc, argv, env */
  ustack_top -= ((3 + args_n)*sizeof(uintptr_t) + strlen(srv->name) + 2*sizeof(char) +
                 strlen(BOOTENV));
  argc = user_to_kernel_vaddr(task_get_rpd(task), ustack_top);
  *argc = args_n; /* we're actually set only srv name */

  argv = (uintptr_t *)((char *)argc + sizeof(uintptr_t));
  for(i = 0; i < args_n; i++) {
    argv[i] = (uintptr_t)((char *)ustack_top + ((3 + args_n)*sizeof(uintptr_t) + al));
    arg1 = user_to_kernel_vaddr(task_get_rpd(task), (uintptr_t) argv[i]);
    memcpy(arg1, argvs[i], strlen(argvs[i]));
    al += (sizeof(char) + strlen(argvs[i]));
  }
  /* set argv, envp pointers */
  envp = (uintptr_t *)((char *)argv + (1 + args_n)*sizeof(uintptr_t));
  *envp = (uintptr_t)((char *)ustack_top + (3 + args_n)*sizeof(uintptr_t) +
                      strlen(srv->name) + sizeof(char));

  /* fill values for argv, envp */
  envp1 = user_to_kernel_vaddr(task_get_rpd(task), (uintptr_t) *envp);
  memcpy(envp1, BOOTENV, strlen(BOOTENV));

  r=arch_process_context_control(task,SYS_PR_CTL_SET_ENTRYPOINT,entry);
  if (r < 0)
    panic("Server [#%d]: Failed to set task's entry point(%p). (ERR = %d)",
          num, entry, r);

  r=arch_process_context_control(task,SYS_PR_CTL_SET_STACK,ustack_top);
  if (r < 0)
    panic("Server [#%d]: Failed to set task's stack(%p). (ERR = %d)", num,
          ustack_top, r);

  destroy_bin_map(emap);

  return;
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
