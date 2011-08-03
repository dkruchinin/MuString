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
 * (c) Copyright 2005,2008,2011 Tirra <madtirra@jarios.org>
 * (c) Copyright 2010,2011 Jari OS ry <http://jarios.org>
 *
 * server/elf.c: ELF binary format hepler functions
 *
 */

#include <arch/types.h>
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

static struct bin_map *__add_bm_region(struct bin_map *cur,
                                       struct bin_map *new)
{
  cur->next = new;
  new->next = NULL;
  new->prev = cur;

  return new;
}

void destroy_bin_map(struct bin_map *root)
{
  struct bin_map *cur;

  while(root!=NULL) {
    cur = root;
    root = cur->next;
    memfree(cur);
  }

  return;
}

struct bin_map *get_elf_map(task_t *task, init_server_t *srv)
{
  char *addr;
  elf_head_t ehead;
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

      if(!cur) {
        root->virt_addr = esh.sh_addr;
        root->bin_addr = srv->addr + esh.sh_offset;
        root->size = esh.sh_size;
        root->type = esh.sh_type;
        root->flags = esh.sh_flags;

        cur = root;
      } else {
        if(!(new = memalloc(sizeof(struct bin_map))))
          return NULL;

        new->virt_addr = esh.sh_addr;
        new->bin_addr = srv->addr + esh.sh_offset;
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

      cur->size += cur->next->size;
      new = cur->next;
      cur->next = cur->next->next;
      if(cur->next)
        cur->next->prev = cur;
      memfree(new);
    } else cur = cur->next;
  }

#if 0
  cur = root;
  while(cur) {
    kprintf("Got section:\n");
    kprintf("\tType: ");
    if(cur->type == SHT_NOBITS) kprintf("Absent in image\n");
    else kprintf("Exist in image\n");
    kprintf("\tVirt addr: %p, size %ld, img_addr %p\n", cur->virt_addr,
            cur->size, cur->bin_addr - srv->addr);

    cur = cur->next;
  }
#endif

  return root;
}

ulong_t get_elf_entry(task_t *task, init_server_t *srv)
{
  char *addr;
  elf_head_t ehead;

  addr = pframe_id_to_virt((srv->addr)>>PAGE_WIDTH);

  /* read elf headers */
  memcpy(&ehead, addr, sizeof(elf_head_t));

  return ehead.e_entry;
}
