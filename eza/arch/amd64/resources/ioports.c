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
 * eza/amd64/ioports.c: AMD64-specific I/O ports-related logic.
 *
 */

#include <eza/resource.h>
#include <mm/pfalloc.h>
#include <eza/errno.h>
#include <eza/arch/context.h>
#include <mm/pfalloc.h>
#include <mlibc/stddef.h>
#include <eza/arch/interrupt.h>

int arch_allocate_ioports(task_t *task,ulong_t start_port,
                          ulong_t end_port)
{
  arch_context_t *ctx = (arch_context_t*)&(task->arch_context[0]);
  tss_t *tss, *master_tss;
  int r;
  ulong_t start_idx,end_idx,start_bit,end_bit;

  //LOCK_TASK_VM(task);

  /* If task has default TSS, then we can expand its TSS to
   * fit the default number of I/O ports provided.
   */
  if( ctx->tss == NULL ) {
    ctx->tss=alloc_pages_addr(TSS_IOPORTS_PAGES,AF_ZERO);
    if( !ctx->tss ) {
      r=-ENOMEM;
      goto out_unlock;
    }
    ctx->tss_limit=TSS_IOPORTS_LIMIT;
    tss=ctx->tss;

    /* By default, all I/O ports are inaccessible. */
    memset(tss,0xff,TSS_IOPORTS_PAGES*PAGE_SIZE);

    /* Initialize main fields for new TSS. */
    master_tss=get_cpu_tss(task->cpu);
    copy_tss(tss,master_tss);
    tss->iomap_base=offsetof(tss_t,iomap);

    /* Now reload this TSS. */
    interrupts_disable();
    load_tss(task->cpu,tss,TSS_IOPORTS_LIMIT);
    interrupts_enable();
  } else {
    tss=ctx->tss;
  }

  /* Now we should reset all bits that cirrespond to target I/O ports. */
  start_idx=start_port>>3;
  start_bit=start_port & 7;

  end_idx=end_port>>3;
  end_bit=end_port & 7;

  if( start_idx == end_idx ) { /* Same IOPM byte ? */
    uint8_t mask=((1<<start_bit)-1);

    mask |= (~((1<<end_bit)-1))<<1;
    tss->iomap[start_idx] &= mask;
  } else { /* Different IOPM bytes. */
    uint8_t first_mask=((1<<start_bit)-1);
    uint8_t last_mask=(~((1<<end_bit)-1))<<1;
    uint8_t *fp=&tss->iomap[start_idx];
    uint8_t *lp=&tss->iomap[end_idx];

    *fp &= first_mask;
    *lp &= last_mask;

    fp++;
    while( fp<lp ) {
      *fp++=0;
    }
  }

  r=0;
out_unlock:
  //UNLOCK_TASK_VM(task);
  return r;
}

int arch_free_ioports(task_t *task,ulong_t start_port,
                           ulong_t end_port)
{
  arch_context_t *ctx = (arch_context_t*)&(task->arch_context[0]);
  tss_t *tss;
  int r;
  ulong_t start_idx,end_idx,start_bit,end_bit;

  //LOCK_TASK_VM(task);

  tss=ctx->tss;
  if( !tss ) {
    r=-EINVAL;
    goto out_unlock;
  }

  /* Now we should set all bits that cirrespond to target I/O ports. */
  start_idx=start_port>>3;
  start_bit=start_port & 7;

  end_idx=end_port>>3;
  end_bit=end_port & 7;  

  if( start_idx == end_idx ) { /* Same IOPM byte ? */
    uint8_t mask=((1<<start_bit)-1);

    mask |= (~((1<<end_bit)-1))<<1;
    tss->iomap[start_idx] |= ~mask;
  } else { /* Different IOPM bytes. */
    uint8_t first_mask=((1<<start_bit)-1);
    uint8_t last_mask=(~((1<<end_bit)-1))<<1;
    uint8_t *fp=&tss->iomap[start_idx];
    uint8_t *lp=&tss->iomap[end_idx];

    *fp |= ~first_mask;
    *lp |= ~last_mask;

    fp++;
    while( fp<lp ) {
      *fp++=0xff;
    }
  }
  
  r=0;
out_unlock:
  //UNLOCK_TASK_VM(task);
  return r;
}
