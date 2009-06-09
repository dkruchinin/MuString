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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * mstring/amd64/mm/init.c: initing function for memory manager
 *                      amd64 specific
 *
 */

#include <arch/types.h>
#include <arch/asm.h>
#include <arch/cpu.h>
#include <arch/bios.h>
#include <mstring/smp.h>
#include <mstring/interrupt.h>

/* local functions prototypes */
/* Cleaning AM flag for CR0 register
 * will disable align checking
 */
static void cpu_clean_am_flag(void);
/* To disable i/o on nonprivelenged levels we're need
 * clean EFLAGS for IOPL and NT
 */
static void cpu_clean_iopl_nt_flags(void);

void arch_mm_stage0_init(cpu_id_t cpu)
{
  //set_efer_flag(AMD_NXE_FLAG);
  /* prepare FPU to use */
  //cpu_setup_fpu();

  if( cpu == 0 ) {
    arch_bios_init();
  }

  /* prepare segmentation */
  arch_pmm_init(cpu);

  /* disable i/o on upper levels */
  cpu_clean_iopl_nt_flags();
  /* disable align checking */
  cpu_clean_am_flag();

  return;
}

/* local functions implementations */
static void cpu_clean_am_flag(void)
{
  asm (                                                                   
       "mov %%cr0, %%rax\n"
       "and $~(0x40000), %%rax\n"
       "mov %%rax, %%cr0\n"
       :
       :
       : "%rax");

}

static void cpu_clean_iopl_nt_flags(void)
{
  asm (
       "pushfq\n"
       "pop %%rax\n"
       "and $~(0x7000), %%rax\n"
       "pushq %%rax\n"
       "popfq\n"
       :
       :
       : "%rax");

}
