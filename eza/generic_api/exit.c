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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/amd64/scheduler.h: AMD-specific functions for dealing with
 *                                scheduler-related information.
 */

#include <eza/arch/types.h>
#include <kernel/syscalls.h>
#include <eza/smp.h>
#include <eza/process.h>
#include <eza/kernel.h>
#include <eza/task.h>
#include <ipc/ipc.h>

static void __exit_ipc(task_t *exiter) {
  task_ipc_t *ipc;
  task_ipc_priv_t *p;  

  LOCK_TASK_MEMBERS(exiter);
  ipc=exiter->ipc;
  p=exiter->ipc_priv;
  exiter->ipc=NULL;
  exiter->ipc_priv=NULL;
  UNLOCK_TASK_MEMBERS(exiter);

  if( ipc ) {
    release_task_ipc(ipc);
  }
  if( p ) {
    release_task_ipc_priv(p);
  }
}

static void __exit_scheduler(task_t *exiter)
{
  sched_del_task(exiter);
}

static void __exit_limits(task_t *exiter)
{
}

static void __exit_resources(task_t *exiter)
{
}

void do_exit(ulong_t code)
{
  task_t *exiter=current_task();

  if( !exiter->pid ) {
    panic( "do_exit(): Exiting form the idle task on CPU N%d !\n",
           cpu_id() );
  }

  if( in_interrupt() ) {
    panic( "do_exit(): Exiting in interrupt context on CPU N%d !\n",
           cpu_id() );
  }

  zombify_task(exiter);

  /* Cleanup all resources this task owns. */
  __exit_ipc(exiter);
  __exit_limits(exiter);
  __exit_resources(exiter);

  /* Yield the CPU forever. */
  __exit_scheduler(exiter);

  /* TODO: [mt] release task struct internals (kernel stack) via RCU */

  panic( "do_exit(): zombie task is still running !\n" );
}

void sys_exit(ulong_t code)
{
  do_exit(code);
}
