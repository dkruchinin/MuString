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
 * eza/amd64/cpu.c: varios CPU functions
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/cpu.h>
#include <eza/arch/asm.h>
#include <eza/arch/bios.h>

extern void syscall_point(void);

static void arch_syscall_setup_cpu(void);

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

/* cpu_setup_fpu(): checkin CPU flags 
 * to make FPU usable for use
 */
static void cpu_setup_fpu(void)
{
  asm volatile(
	       "movq %%cr0, %%rax;"
	       "btsq $1, %%rax;"
	       "btrq $2, %%rax;"
	       "movq %%rax, %%cr0;"
	       "movq %%cr4, %%rax;"
	       "bts $9, %%rax;"
	       "bts $10, %%rax;"
	       "movq %%rax, %%cr4;"
	       :
	       :
	       :"%rax");

}

void arch_cpu_init(cpu_id_t cpu)
{  
  /* prepare FPU to use */
  set_efer_flag(AMD_NXE_FLAG);
  cpu_setup_fpu();
  if( cpu == 0 ) {
    arch_bios_init();
  }

  /* prepare segmentation */
  arch_pmm_init(cpu);

  /* disable i/o on upper levels */
  cpu_clean_iopl_nt_flags();

  /* disable align checking */
  cpu_clean_am_flag();

  /* setup syscall entrypoint */
  arch_syscall_setup_cpu();
}

/* init syscall/sysret entry function */
static void arch_syscall_setup_cpu(void)
{
  /* enable */
  set_efer_flag(AMD_SCE_FLAG);
  /* setup the entry address */
  write_msr(AMD_MSR_STAR,((uint64_t)(gdtselector(KDATA_DES) | PL_USER) << 48) | 
	    ((uint64_t)(gdtselector(KTEXT_DES) | PL_KERNEL) << 32));
  write_msr(AMD_MSR_LSTAR,(uint64_t)syscall_point);
  /* Disable interrupts upon entering syscalls. */
  write_msr(AMD_MSR_SFMASK,0x200);

  return;
}
