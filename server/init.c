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

struct bin_map {
  ulong_t bin_addr;
  ulong_t virt_addr;
  ulong_t size;
  uint32_t flags;
  uint32_t type;
  struct bin_map *next;
  struct bin_map *prev;
};

/* memalloc / memfree */

static struct bin_map *__add_bm_region(struct bin_map *cur,
                                       struct bin_map *new)
{
  cur->next = new;
  new->next = NULL;
  new->prev = cur;

  return new;
}

static void __destroy_bin_map(struct bin_map *root)
{
  struct bin_map *cur;

  while(root!=NULL) {
    cur = root;
    root = cur->next;
    memfree(cur);
  }

  return;
}

static struct bin_map *__get_elf_map(task_t *task, init_server_t *srv)
{
  char *addr;
  elf_head_t ehead;
  elf_pr_t epr;
  elf_sh_t esh;
  struct bin_map *root, *cur = NULL, *new = NULL;
  int i;

  addr = pframe_id_to_virt((srv->addr)>>PAGE_WIDTH);

  /* read elf headers */
  memcpy(&ehead, addr, sizeof(elf_head_t));

  /* FIXME: add ELF header check */

  root = memalloc(sizeof(struct bin_map));
  if(!root) return NULL;
  else {
    root->next = NULL;
    root->prev = NULL;
  }

#if 0
  /* printf elf header info */
  kprintf("ELF header(%4s): %d type, %d mach, %d version\n", ehead.e_ident,
          ehead.e_type, ehead.e_machine, ehead.e_version);
  kprintf("Entry: %p,Image off: %p,sect off:%p\n", ehead.e_entry, ehead.e_phoff,
          ehead.e_shoff);
#endif

  for(i=0; i<ehead.e_shnum; i++) {
    memcpy(&esh, addr + ehead.e_shoff +
           i*(ehead.e_shentsize), sizeof(esh));

    if((esh.sh_size != 0) && ((esh.sh_flags & ESH_ALLOC) &&
                              ((esh.sh_type == SHT_PROGBITS) ||
                               (esh.sh_type == SHT_NOBITS)))) {
      kprintf("Got section:\n");
      kprintf("\tType: ");
      if(esh.sh_type == SHT_NOBITS) kprintf("Absent in image\n");
      else kprintf("Exist in image\n");
      kprintf("\tVirt addr: %p, size %ld\n", esh.sh_addr, esh.sh_size);

      if(!cur) {
        root->virt_addr = esh.sh_addr;
        root->bin_addr = esh.sh_offset;
        root->size = esh.sh_size;
        root->type = esh.sh_type;
        root->flags = esh.sh_flags;

        cur = root;
      } else {
        if(!(new = memalloc(sizeof(struct bin_map))))
          return NULL;

        new->virt_addr = esh.sh_addr;
        new->bin_addr = esh.sh_offset;
        new->size = esh.sh_size;
        new->type = esh.sh_type;
        new->flags = esh.sh_flags;

        cur = __add_bm_region(cur, new);
      }
    }

  }

  /* split areas */
  cur = root;
  while(cur->next != NULL) {
    /* might be splitted */
    if((PAGE_ALIGN_DOWN(cur->virt_addr + cur->size) ==
        PAGE_ALIGN_DOWN(cur->next->virt_addr)) && (cur->type == cur->next->type)) {
      kprintf("SPLIT!:");
      kprintf("newsize %ld + %ld (%ld)\n", cur->size, cur->next->size,
              cur->size + cur->next->size);
      cur->size += cur->next->size;
      new = cur->next;
      cur->next = cur->next->next;
      if(cur->next)
        cur->next->prev = cur;
      memfree(new);
    } else cur = cur->next;
  }

  cur = root;
  while(cur) {
    kprintf("Got section:\n");
    kprintf("\tType: ");
    if(cur->type == SHT_NOBITS) kprintf("Absent in image\n");
    else kprintf("Exist in image\n");
    kprintf("\tVirt addr: %p, size %ld, img_addr %p\n", cur->virt_addr,
            cur->size, cur->bin_addr);

    cur = cur->next;
  }

  return root;
}

static ulong_t __get_elf_entry(task_t *task, init_server_t *srv)
{
  char *addr;
  elf_head_t ehead;

  addr = pframe_id_to_virt((srv->addr)>>PAGE_WIDTH);

  /* read elf headers */
  memcpy(&ehead, addr, sizeof(elf_head_t));

#if 0
  /* printf elf header info */
  kprintf("ELF header(%4s): %d type, %d mach, %d version\n", ehead.e_ident,
          ehead.e_type, ehead.e_machine, ehead.e_version);
  kprintf("Entry: %p,Image off: %p,sect off:%p\n", ehead.e_entry, ehead.e_phoff,
          ehead.e_shoff);
#endif

  return ehead.e_entry;
}

static void __create_task_mm(task_t *task, int num, init_server_t *srv)
{
  struct bin_map *emap = __get_elf_map(task,srv);
  per_task_data_t *ptd;
  vmm_t *vmm = task->task_mm;
  ulong_t entry = __get_elf_entry(task,srv);
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
      kprintf("sseek = %p\n", sseek);

      /* if it's NO_BITS section it should be zeroed */
      if(cur->type == SHT_NOBITS) {
        sbss = user_to_kernel_vaddr(task_get_rpd(task), PAGE_ALIGN_DOWN(cur->virt_addr -
                                                                        sseek));
        kprintf("sbss = %p\n", (sbss + PAGE_SIZE) - sseek);
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
    kprintf("psize = %d\n", psize);
    if(psize<<PAGE_WIDTH < (cur->size + (cur->virt_addr -
                                         PAGE_ALIGN_DOWN(cur->virt_addr)))) psize++;

    kprintf("Mapping region at %p %d pages\n", PAGE_ALIGN_DOWN(cur->virt_addr),
            psize);
    kprintf("VMMAP flags:");
    if(flags & VMR_WRITE) kprintf("W");
    if(flags & VMR_EXEC) kprintf("E");
    if(flags & VMR_READ) kprintf("R");
    kprintf("\n");

    r = vmrange_map(generic_memobj, vmm, PAGE_ALIGN_DOWN(cur->virt_addr), psize,
                    flags, 0);
    if(!PAGE_ALIGN(r))
      panic("Server [#%d]: Failed to create VM range for section. (ERR = %d)", num, r);

    if(cur->type == SHT_PROGBITS) {
      r = mmap_core(vmm, PAGE_ALIGN_DOWN(cur->virt_addr),
                    PAGE_ALIGN_DOWN(cur->bin_addr) >> PAGE_WIDTH, psize, kflags);
      kprintf("MAP bin: %p to %p binary with %d pages\n", cur->virt_addr, cur->bin_addr,
              psize);
      kprintf("MAP kflags:");
      if(kflags & KMAP_WRITE) kprintf("W");
      if(kflags & KMAP_EXEC) kprintf("E");
      if(kflags & KMAP_READ) kprintf("R");
      kprintf("\n");
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

  r=arch_process_context_control(task,SYS_PR_CTL_SET_ENTRYPOINT,entry);
  if (r < 0)
    panic("Server [#%d]: Failed to set task's entry point(%p). (ERR = %d)",
          num, entry, r);

  r=arch_process_context_control(task,SYS_PR_CTL_SET_STACK,ustack_top);
  kprintf("ustack_top = %p\n", ustack_top);
  if (r < 0)
    panic("Server [#%d]: Failed to set task's stack(%p). (ERR = %d)", num,
          ustack_top, r);
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
